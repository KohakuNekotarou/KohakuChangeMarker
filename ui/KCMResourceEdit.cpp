//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMResourceEdit.h for what this opens and how the route was arrived at.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// ----- Interfaces -----
#include "IActionManager.h"			// PerformAction - the product's own "Style Options..."
#include "IApplication.h"
#include "IControlView.h"
#include "IDialogMgr.h"				// GetFrontmostDialogWindow - reaching INTO the modal dialog
#include "ISelectableDialogSwitcher.h"	// SwitchDialogPanel - which of its sixteen pages to show
#include "IWindow.h"
#include "IPalettePanelUtils.h"		// QueryPanelByWidgetID - reaching somebody else's panel
#include "IPanelControlData.h"		// FindWidget - down to the tree inside it
#include "IPanelMgr.h"				// ShowPanelByMenuID / IsPanelWithMenuIDShown
#include "ISession.h"
#include "ISubject.h"				// Change - telling the page that the proxy switched sides
#include "ITextControlData.h"		// what the row the search found actually says
#include "ITreeViewController.h"	// DeselectAll / Select - WITHOUT notifying
#include "ITreeViewHierarchyAdapter.h"	// the tree's own rows, open or closed
#include "ITreeViewMgr.h"			// ScrollToNode
#include "ITreeViewTypeAhead.h"		// GetStringForNode - a node's name without its row

// ----- General -----
#include "NodeID.h"					// NodeID / kInvalidNodeID
#include "PersistUtils.h"			// ::GetClass - which page a dialog panel IS
// The bosses that supply the dialog's pages. Published under source/open, reached by a relative
// path rather than by adding an include directory - the same reasoning, and the same route, as
// KCMStoryRowEH.cpp's TreeNodeEventHandler.h: the build files that would carry such a directory
// live outside this plug-in's repository, so a path added there would not survive a fresh checkout.
#include "../../open/interfaces/text/CharPanelID.h"	// kCharDialogWidget - the Basic Character Formats page
#include "IStrokeFillControlData.h"	// SetActive - which square of the fill/stroke proxy is in front
#include "ToolboxProxyTypes.h"		// ToolboxProxy::kFillActive / kStrokeActive
#include "widgetid.h"				// kStrokeFillWidgetBoss - the proxy's class, and this one IS public
#include "StylePanelID.h"			// the Character Styles panel, its tree, and its action
#include "TextStylePanelID.h"		// kStyleCharParentWidgetID - the panel that switches the pages
#include "Utils.h"

#include <windows.h>
#include <string>
#include <vector>

// ----- Project -----
#include "KCMUIID.h"
#include "IKCMCompareFacade.h"		// GetArmedTargetDB / IsDocDBOpen / GetActiveDocDB
#include "IKCMResourcesFacade.h"	// GetNthChange
#include "KCMUIShared.h"			// KCMSetStatus
#include "KCMStoryTree.h"			// KCMListShowsResources - which list the stashed row belongs to
#include "KCMStoryRefresh.h"		// KCMStoryMenuRow - the definition row the menu was popped over
#include "KCMStoryCopy.h"			// KCMStoryGetMenuChange - the attribute row, the same way
#include "KCMStoryJump.h"			// KCMActivateDocument - the Target to the front
#include "KCMXmlPretty.h"			// KCMDecodePercentEscapes - `%3a` is the group separator
#include "KCMResourceEdit.h"

namespace
{

/** The element name the exporter gives a character style, and the prefix its key carries.
	★The key is `<kind>/<Self>`, the same spelling the Kind and Definition columns show
	(IKCMResourcesFacade.h: "Color/Black"). */
const char* const kKCMCharStyleKind = "CharacterStyle";

/** The style's FULL PATH as the panel spells it, from the row's key: `group:group:name`.

	★★TWO SPELLINGS MEET HERE. The exporter writes a style inside a group as
	`CharacterStyle/グループ%3a名前` (KCMXmlPretty.h measured the escape); the Character Styles panel
	draws that style as a row saying `名前` UNDER a row saying `グループ`. So the kind prefix comes
	off and the escapes are decoded, which leaves the colon-separated path the panel's own rows
	spell out between them.
	⚠Percent-decoding works on bytes, so the round trip is UTF-8 both ways, never the platform
	  string - a style really is called `段落スタイル 1` here.
	⚠A `/` INSIDE a name arrives as `%2f` and survives, because only the exporter's own separator
	  is written plain.
	⚠★A COLON INSIDE A NAME IS ESCAPED THE SAME WAY THE SEPARATOR IS, so a style literally called
	  `A:B` and a style `B` inside a group `A` produce the same path here. Nothing in the key can
	  tell them apart - and neither can the panel's rows, which spell both the same way - so the
	  check below cannot either. It is written down rather than guarded against: a document holding
	  both at once is the only case it could matter in.
*/
std::string DecodedKeyPath(const PMString& key)
{
	std::string utf8 = key.GetUTF8String();
	const std::string prefix = std::string(kKCMCharStyleKind) + "/";
	if (utf8.compare(0, prefix.size(), prefix) == 0)
		utf8.erase(0, prefix.size());
	return KCMDecodePercentEscapes(utf8);
}

/** The last segment of that path - the name the panel's own search matches, since a row says only
	its own name and leaves the groups to the rows above it. */
PMString LeafOfPath(const std::string& path)
{
	std::string leaf = path;
	const std::string::size_type colon = leaf.rfind(':');
	if (colon != std::string::npos)
		leaf.erase(0, colon + 1);

	PMString name;
	name.SetUTF8String(leaf);
	name.SetTranslatable(kFalse);
	return name;
}

/** Bring the Character Styles panel on screen, if it is not already there.

	⚠NOT by firing kCharacterStylesPanelActionID: that menu item is a TOGGLE, so pressing it when
	the panel is already up would close the very panel this function exists to show. IPanelMgr says
	"show" and means it (the same pair snippetrunner uses - IsPanelWithMenuIDShown then
	ShowPanelByMenuID).
	★giveKeyFocus is kFalse. The reader's next act is usually in the dialog this opens, or back in
	their document; taking the keyboard for a panel they did not ask to type in is an intervention
	of its own (the same reasoning the Story list's click records about holding the keyboard).
*/
void ShowCharacterStylesPanel()
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IPanelMgr> panelMgr(app, UseDefaultIID());
	if (panelMgr == nil)
		return;
	if (!panelMgr->IsPanelWithMenuIDShown(kCharacterStylesPanelActionID))
		panelMgr->ShowPanelByMenuID(kCharacterStylesPanelActionID, kFalse);
}

/** The path split at its colons: `KCMOuter:KCMInner:KCMDeep` -> three segments. */
std::vector<std::string> SplitPath(const std::string& path)
{
	std::vector<std::string> parts;
	std::string::size_type at = 0;
	for (;;)
	{
		const std::string::size_type colon = path.find(':', at);
		if (colon == std::string::npos)
		{
			parts.push_back(path.substr(at));
			break;
		}
		parts.push_back(path.substr(at, colon - at));
		at = colon + 1;
	}
	return parts;
}

/** The one node whose path down the tree is exactly `segments`, or kInvalidNodeID.

	★★★WHY A WALK AND NOT ITreeViewMgr::Search. Search takes a NAME, so it cannot tell two styles
	of the same name in different groups apart - and it answers a row whose text merely STARTS WITH
	what it was given (ITreeViewMgr.h:162-165). **Measured 2026-09-13: a row reading `ZZZ:Same`
	opened `AAA:Same`** - the wrong style, silently, with its own settings shown as though they were
	the ones the reader had just been looking at. That is exactly the failure a comparison tool must
	not have.
	★★WHAT MAKES THE WALK POSSIBLE is ITreeViewTypeAhead::GetStringForNode, which names a node
	WITHOUT its row being built - so a group nobody has opened is walked through just the same, and
	nothing has to be expanded or scrolled to find the style. (The nodes themselves cannot be built
	from outside: measured, they are not the public UIDNodeID but a class of the styles panel's own.)
	⚠A name containing a colon is indistinguishable from a group separator here - the exporter
	  escapes both as `%3a` - so the caller tries the whole path as one name when this fails.
*/
NodeID FindNodeByPath(ITreeViewHierarchyAdapter* adapter, ITreeViewTypeAhead* typeAhead,
					  const std::vector<std::string>& segments)
{
	if (adapter == nil || typeAhead == nil || segments.empty())
		return kInvalidNodeID;

	NodeID node = adapter->GetRootNode();
	for (size_t level = 0; level < segments.size(); ++level)
	{
		const int32 count = adapter->GetNumChildren(node);
		NodeID next = kInvalidNodeID;
		for (int32 i = 0; i < count; ++i)
		{
			const NodeID child = adapter->GetNthChild(node, i);
			PMString name = typeAhead->GetStringForNode(child);
			name.SetTranslatable(kFalse);
			if (name.GetUTF8String() == segments[level])
			{
				next = child;
				break;
			}
		}
		if (next == kInvalidNodeID)
			return kInvalidNodeID;
		node = next;
	}
	return node;
}

//========================================================================================
// Opening the dialog AT THE PAGE that holds the attribute the reader chose "Edit..." on
//========================================================================================

/** Which of the dialog's pages holds a given IDML attribute, BY THE BOSS THAT SUPPLIES THE PAGE.

	★★★NOT BY POSITION, AND THE REASON IS THE LANGUAGE (2026-09-13, the user's question: "I am
	testing on the Japanese version - will the English one pick the right item too?"). **The set of
	pages differs by feature set.** A Japanese InDesign shows seven pages the Roman one has not -
	縦中横, five ruby pages and two kenten pages - so every position below them means a different
	page in the two. The Basic Character Formats page happens to be second from the top in both,
	which is exactly the kind of coincidence that makes a positional table look right until the day
	it is not.

	★★WHAT A PAGE IS, INSTEAD: the boss of the VIEW the switcher hands back for it. Read off the
	open dialog on 2026-09-13 (KT's app.ktStyleDlgProbe, Japanese InDesign, seventeen pages, all
	seventeen views already built while the dialog was up - none of them is created lazily):

	    [ 0] 0x5bfd kTextStyleCharGeneralPanelBoss  General                     TEXT STYLE PANEL
	    [ 1] 0x6908 kCharDialogWidget               Basic Character Formats     CHARACTER PANEL
	    [ 2] 0x6909 kCharDialog2Widget              Advanced Character Formats  CHARACTER PANEL
	    [ 3] 0x5c05 kTextColorDialogWidget          Character Colour            TEXT COLOR PANEL
	    [ 4] 0x69a3 kCharOpenTypePanelBoss          OpenType Features           CHARACTER PANEL
	    [ 5] 0x69bb kCharUnderlinePanelBoss         Underline Options           CHARACTER PANEL
	    [ 6] 0x69be kCharStrikeThroughPanelBoss     Strikethrough Options       CHARACTER PANEL
	    [ 7] 0xc804 (no public name)                                 Japanese feature set only
	    [ 8] 0xc664 (no public name)                                 Japanese feature set only
	    [ 9] 0xc665 (no public name)                                 Japanese feature set only
	    [10] 0xc666 (no public name)                                 Japanese feature set only
	    [11] 0xc667 (no public name)                                 Japanese feature set only
	    [12] 0xc764 (no public name)                                 Japanese feature set only
	    [13] 0xc765 (no public name)                                 Japanese feature set only
	    [14] 0xcf04 (no public name)                                 Japanese feature set only
	    [15] 0x5b1e kStyleToTagMapDialogBoss        Export Tagging              TEXT STYLE PANEL
	    [16] 0xc836 (no public name)                                 Japanese feature set only

	⚠★★★THE HOOK BOSS IS NOT THE PAGE (this table said it was for a day, and the page never
	  switched). kCharDialogHookBoss (0x6904) is the SERVICE PROVIDER that registers the Basic
	  Character Formats page; the view it then creates is a different boss, kCharDialogWidget
	  (0x6908), and the view is what GetDialogPanel hands back. They sit FOUR LINES APART in
	  CharPanelID.h (215 and 219), and line 215 says so in its own comment - "hooks the
	  CharStyleDialog into the CharDialog and the StyleDialog". It was read as an answer to "which
	  boss supplies this page" when it answers "which boss puts this page there", and nothing after
	  that could tell the difference. The General and Export Tagging pages hide the distinction
	  entirely, because for those two the supplier IS the view.
	  ⇒ ★**A page boss in this table has to come from the open dialog, never from the registry.**

	⚠★★The eight pages with no public name are the J feature set's own (縦中横, ruby, kenten): their
	  plug-in prefixes are not in the SDK at all, so an attribute of theirs can only be written here
	  as the measured number, with a comment saying where it came from.

	A ClassID is the same number in every language and stays put when a page is inserted above it.
	★One line per attribute. An attribute not in the table, and a definition row, leave the dialog
	  on whichever page it opens itself at.

	★★WHY A PLAIN ARRAY AND NOT A MAP (2026-09-13, the user's question). A std::map would run a
	constructor at plug-in load, allocate, and bring the static initialisation order of two
	translation units into it; this array has no constructor at all and lives in the binary's
	read-only data. What it costs is a linear scan - of string compares, on a gesture the reader
	made, immediately before a modal dialog is built. **The array IS the map**; a container would
	change where it is kept and nothing about what it says. A map would start to earn its keep at
	hundreds of entries consulted inside a loop, which is not what this is.
*/
struct KCMAttributePage
{
	const char*	fAttribute;
	ClassID		fPageBoss;
};

/** The Character Colour page's view, AS A MEASURED NUMBER because the SDK declares no name for it.

	⚠★★★`kTextColorDialogWidget` is what InDesign's own object model answers for 0x5c05 - it is a
	real name, read off the running dialog - but **no public header declares it**, and neither does
	any header declare the prefix of the 0x5c00 band it belongs to (TEXT COLOR PANEL). Grepping the
	whole of `source/` for the spelling finds exactly one hit: the page table's own comment, above.
	⇒ This is the case the table's header already set the rule for ("a page of theirs can only be
	  written here as the measured number, with a comment saying where it came from"), met for the
	  first time by a page that is NOT one of the Japanese-only eight.
	★It is a ClassID, so it is the same number in every language and in every feature set. */
const ClassID kKCMTextColourPageBoss(0x5c05);

const KCMAttributePage kKCMAttributePages[] =
{
	// 一般 / General
	// ⚠★★THESE TWO CANNOT BE SEEN TO WORK. The dialog opens on page 0 and General IS page 0
	//   (measured: current=0), so the switch is a no-op and the reader sees the same thing whether
	//   the table is right, wrong, or missing the attribute. They are kept because they say what
	//   the page IS, and because the day a page is inserted above General they start to matter.
	//   ⇒ ★A change to this table has to be tested on an attribute whose page is NOT General.
	{ "BasedOn",					kTextStyleCharGeneralPanelBoss },
	{ "ExtendedKeyboardShortcut",	kTextStyleCharGeneralPanelBoss },
	// 基本文字形式 / Basic Character Formats
	// ⚠kCharDialogWidget, NOT kCharDialogHookBoss - see the note above the table.
	{ "AppliedFont",				kCharDialogWidget },
	{ "FontStyle",					kCharDialogWidget },
	{ "PointSize",					kCharDialogWidget },
	{ "Leading",					kCharDialogWidget },
	// The rest of that page, 2026-09-13 (the user read them off it).
	// ★SPELT AS THE EXPORT SPELLS THEM, and checked one by one against a real INX
	//   (work/inx-probe/origin.xml) rather than against the panel's labels: `Underline` has a small
	//   l, and `KerningMethod` is easy to mistype. A misspelt entry does not fail loudly - it simply
	//   never matches, and the item it belongs to is then greyed out (CanEditResourceAttr below),
	//   which reads exactly like "this one is not supported yet".
	{ "KerningMethod",				kCharDialogWidget },
	{ "Tracking",					kCharDialogWidget },
	{ "Capitalization",				kCharDialogWidget },
	{ "Position",					kCharDialogWidget },
	{ "Underline",					kCharDialogWidget },
	{ "StrikeThru",					kCharDialogWidget },
	{ "Ligatures",					kCharDialogWidget },
	{ "NoBreak",					kCharDialogWidget },
	{ "CharacterAlignment",			kCharDialogWidget },
	// 詳細文字形式 / Advanced Character Formats (2026-09-13, the user read them off that page)
	// ⚠kCharDialog2Widget sits ONE LINE below kCharDialogWidget in CharPanelID.h (219 and 220) and
	//   differs from it by one character here. The two pages are next to each other in the dialog
	//   as well, so a swap would open a page that looks almost right - check against the page
	//   TITLE when either of these is touched, never against its position.
	// ★All three are shown as PERCENTAGES rather than as the export wrote them
	//   (KCMResourceUnits.h); that is a display rule and has nothing to do with this table.
	{ "HorizontalScale",			kCharDialog2Widget },
	{ "VerticalScale",				kCharDialog2Widget },
	{ "Tsume",						kCharDialog2Widget },
	// ⚠`BaselineShift` has a SMALL l - the export's spelling, measured; the page reads it as
	//   "BaseLineShift" and it is not that. ★Its unit moved to the text size (Q) in the same
	//   change: KCMResourceUnits.h, where the wrong letter beside the right number is written up.
	{ "BaselineShift",				kCharDialog2Widget },
	{ "Skew",						kCharDialog2Widget },
	{ "CharacterRotation",			kCharDialog2Widget },
	{ "LeadingAki",					kCharDialog2Widget },
	{ "TrailingAki",				kCharDialog2Widget },
	{ "Jidori",						kCharDialog2Widget },
	{ "GlyphForm",					kCharDialog2Widget },
	{ "AppliedLanguage",			kCharDialog2Widget },
	{ "ScaleAffectsLineHeight",		kCharDialog2Widget },
	{ "CjkGridTracking",			kCharDialog2Widget },
	// 文字カラー / Character Colour (2026-09-13)
	// ★**AND THESE TWO ALSO PRESS A SQUARE.** The page shows ONE swatch list, and which of the
	//   style's two colours it is about is decided by the fill/stroke proxy on it - so these two
	//   rows would otherwise open the same page with the same square in front, and one of them
	//   would be about the wrong colour. See KCMProxyForAttribute.
	{ "FillColor",					kKCMTextColourPageBoss },
	{ "StrokeColor",				kKCMTextColourPageBoss },
	{ "FillTint",					kKCMTextColourPageBoss },
	{ "StrokeTint",					kKCMTextColourPageBoss },
	{ "OverprintFill",				kKCMTextColourPageBoss },
	{ "OverprintStroke",			kKCMTextColourPageBoss },
	{ "StrokeWeight",				kKCMTextColourPageBoss },
	{ "MiterLimit",					kKCMTextColourPageBoss },
	{ "EndJoin",					kKCMTextColourPageBoss },
	{ "StrokeAlignment",			kKCMTextColourPageBoss },
	// ⚠**KerningValue is absent on purpose, and NOT because its page is unknown**: it no longer
	//   reaches this list at all. It rides along with KerningMethod and cannot be edited for a
	//   definition, so the model leaves it out of the compared body (KCMResourceParse.cpp,
	//   AppendOpenTag, where the reason is written down). ★Said here because this is where somebody
	//   who misses it will come looking.
};

/** The page boss for `attributeName`, or kInvalidClass when the table does not know it. */
ClassID KCMPageBossForAttribute(const PMString& attributeName)
{
	const std::string name = attributeName.GetUTF8String();
	for (size_t i = 0; i < sizeof(kKCMAttributePages) / sizeof(kKCMAttributePages[0]); ++i)
	{
		if (name == kKCMAttributePages[i].fAttribute)
			return kKCMAttributePages[i].fPageBoss;
	}
	return kInvalidClass;
}

// ***** THE SWITCH HAS TO HAPPEN FROM INSIDE SOMEBODY ELSE'S MODAL LOOP. *****
//
// PerformAction does not return until the dialog is dismissed, so there is no "after it opened"
// for this code. What runs in there is a WIN32 THREAD TIMER: a modal loop is an ordinary
// GetMessage / DispatchMessage pump, so WM_TIMER still arrives ([[modal-dialog-reach-via-timer]],
// proved on the Keyboard Shortcuts editor and used by KT's dialog watcher ever since).
// ⚠★★AN IDLE TASK WOULD NOT DO - not ICallbackTimer either, which is this plug-in's usual answer
//   ([[avoid-timers-and-idle-tasks]]). Idle tasks are serviced by the APPLICATION's event loop,
//   and a modal dialog is not running it. ::SetTimer is not a preference here; it is the only
//   thing that fires.
/** Which square of the Character Colour page's fill/stroke proxy an attribute is about.
	★Declared up here, beside the page the switch is aiming at, because the two are ONE gesture:
	  armed together, cleared together, and meaningless apart. What they mean is under
	  KCMProxyForAttribute, below. */
enum KCMWantedProxy
{
	kKCMProxyLeaveAlone = 0,	// every other attribute: the proxy is left exactly as it was
	kKCMProxyFill,
	kKCMProxyStroke
};

ClassID			gWantedPageBoss	= kInvalidClass;
KCMWantedProxy	gWantedProxy	= kKCMProxyLeaveAlone;
UINT_PTR	gPageTimer		= 0;
int			gPageTicks		= 0;
const UINT	kPageTimerMs	= 100;
const int	kPageGiveUpTicks = 100;		// ten seconds, then stop by itself
// ★TEN AND NOT FIVE: a page of this dialog is built when it is first needed, so a tick that finds
//   the switcher but not yet the page is early rather than wrong, and the count has to cover the
//   whole of the dialog's assembly on a machine slower than this one. Nothing is spent waiting -
//   the timer stops the moment the page is found.

void StopPageTimer()
{
	if (gPageTimer != 0)
	{
		::KillTimer(nullptr, gPageTimer);
		gPageTimer = 0;
	}
	gWantedPageBoss = kInvalidClass;
	gWantedProxy = kKCMProxyLeaveAlone;
}

/** Which of the switcher's pages is supplied by `wanted`, or -1.

	★The switcher names its pages by WidgetID and hands the view back for one
	(ISelectableDialogSwitcher::GetPanelWidgetID / GetDialogPanel), and the view's boss is what says
	which page it IS - see the table above for why that and not the position.
*/
int32 PageIndexOfBoss(ISelectableDialogSwitcher* switcher, ClassID wanted)
{
	if (switcher == nil || wanted == kInvalidClass)
		return -1;
	const int32 count = switcher->GetNumDialogPanels();
	for (int32 i = 0; i < count; ++i)
	{
		IControlView* page = switcher->GetDialogPanel(switcher->GetPanelWidgetID(i));
		if (page != nil && ::GetClass(page) == wanted)
			return i;
	}
	return -1;
}

/** The page's own view, for the index PageIndexOfBoss just handed back. */
IControlView* PageViewAt(ISelectableDialogSwitcher* switcher, int32 index)
{
	if (switcher == nil || index < 0 || index >= switcher->GetNumDialogPanels())
		return nil;
	return switcher->GetDialogPanel(switcher->GetPanelWidgetID(index));
}

//========================================================================================
// The fill / stroke proxy on the Character Colour page
//========================================================================================

/** Whether opening the editor for `attributeName` should bring one square of the proxy to the front.

	★★★WHY THE PAGE IS NOT ENOUGH (2026-09-13, the user pointing at the two squares: "for FillColor
	and for StrokeColor, press this"). The Character Colour page shows ONE swatch list, one tint,
	one overprint - and which of the style's two colours they are about is decided by the little
	proxy at the top left. So a FillColor row and a StrokeColor row would open the same page with
	the same square in front, and one of the two would quietly be about the wrong colour.

	★★HOW IT WAS FOUND, because the shape of the search is the reusable part. The squares are NOT
	Win32 controls - read from outside, that dialog's 173 child windows are edit boxes and their
	containers and nothing else, because InDesign draws its own dialogs. So the page's widget tree
	was dumped FROM INSIDE (KT's app.ktStyleDlgProbe, extended the same day the seventeen pages
	were), every ClassID on it was turned back into a name with the debug build's object-model dump,
	and one of them was `kStrokeFillWidgetBoss` - **declared in the public `widgetid.h:144`**, with
	`IStrokeFillControlData` (public header) on it and `SetActive(kFillActive/kStrokeActive)` in it.
	⇒ ★**The user's own observation is why it is public**: "the same fill and stroke colours are in
	  lots of places - the toolbox has them, paragraph styles have them". A widget that appears in
	  that many places lives in WIDGETS.RPLN and has a published name.
	⚠★★But "the same square" is NOT "the same target": the toolbox's proxy is about the selection
	  and the defaults, this one is about the style being edited. That is why the actions that drive
	  the toolbox (kToggleFillAndStrokeActionID, the X key) were the wrong answer - and they are
	  disabled during a modal dialog anyway, unless declared kEnableEvenDuringDialogs.

	★EVERY ATTRIBUTE OF THAT PAGE IS HERE, and the last four were ASKED rather than assumed.
	  OverprintStroke, MiterLimit, EndJoin and StrokeAlignment carry no "which square" in the list
	  they arrived in - the other seven did - so they were left out of the first build and put to
	  the user, who answered "those are the stroke" (2026-09-13). They are stroke because somebody
	  said so, not because their names look like it.
	⇒ ★**Anything else that lands on this page has no square and gets none**: the proxy is then left
	  exactly where the reader put it, which is what an attribute missing from this table does too.
	  The two cases are deliberately the same, so that forgetting a line here is harmless. */
KCMWantedProxy KCMProxyForAttribute(const PMString& attributeName)
{
	const std::string name = attributeName.GetUTF8String();
	if (name == "FillColor" || name == "FillTint" || name == "OverprintFill")
		return kKCMProxyFill;
	if (name == "StrokeColor" || name == "StrokeTint" || name == "StrokeWeight"
		|| name == "OverprintStroke" || name == "MiterLimit" || name == "EndJoin"
		|| name == "StrokeAlignment")
		return kKCMProxyStroke;
	return kKCMProxyLeaveAlone;
}

/** Bring `which` square of the proxy on `page` to the front. Does nothing, quietly, when the page
	has no proxy - every page but Character Colour, and that is not an error.

	★FOUND BY CLASS, NOT BY WidgetID, for the same reason the PAGE is: the proxy's WidgetID (0x5ccb,
	measured) belongs to TEXT COLOR PANEL and has no published name, while its CLASS does. A walk
	for the class also survives the widget being moved or renumbered in a later InDesign. */
bool16 SetProxyOnPage(IControlView* view, KCMWantedProxy which, int& budget)
{
	if (view == nil || which == kKCMProxyLeaveAlone || budget <= 0)
		return kFalse;
	--budget;					// ★one shared count for the whole walk, so a deep tree cannot spin

	if (::GetClass(view) == kStrokeFillWidgetBoss)
	{
		InterfacePtr<IStrokeFillControlData> proxy(view, UseDefaultIID());
		if (proxy == nil)
			return kFalse;
		proxy->SetActive((which == kKCMProxyFill) ? ToolboxProxy::kFillActive
												  : ToolboxProxy::kStrokeActive);

		// ★★★THE SQUARE IS NOT THE PAGE (2026-09-13, the user: "it LOOKS pressed, but the contents
		//   have not changed - does it need a refresh?"). SetActive writes the value the proxy DRAWS
		//   ITSELF from and tells nobody: IStrokeFillControlData is "a widget data interface for the
		//   stroke/fill proxy widget ... for rendering" (its own header). The swatch list, the tint
		//   and the overprint are OTHER widgets, so until something tells them, they go on showing
		//   the side that was in front before.
		//   ⚠That is WORSE THAN NOT PRESSING IT: the square says fill while the fields below are
		//     still about the stroke, so the reader trusts the square and reads the wrong colour.
		//   ★The product's own click does both halves, and the SDK publishes the second one:
		//     widgetid.h:464-466, "Messages sent by StrokeFillWidget" - kWidgetFillActiveMessage and
		//     kWidgetStrokeActiveMessage. The page's observer is listening for those.
		//   ⚠NO WORKED EXAMPLE EXISTS: those two DECLARE_PMID lines are the whole of it in the SDK
		//     (measured across all of source/), so the protocol below is the one thing here that was
		//     reasoned rather than read - a widget announcing that its data changed names that data
		//     interface, which is why it is the proxy's own kDefaultIID.
		//   ★A message nobody is attached to costs nothing and changes nothing, so this cannot make
		//     the dialog worse than it was; what it can do is be ignored, and that is measurable.
		InterfacePtr<ISubject> subject(view, UseDefaultIID());
		if (subject != nil)
			subject->Change((which == kKCMProxyFill) ? kWidgetFillActiveMessage
													 : kWidgetStrokeActiveMessage,
							IStrokeFillControlData::kDefaultIID);

		// The widget draws itself from that value, and nothing has told it to redraw.
		// ★KEPT even now that the message goes out: if no observer is listening, the square itself
		//   must still tell the truth about which side SetActive put in front.
		view->Invalidate();
		return kTrue;
	}

	InterfacePtr<IPanelControlData> children(view, UseDefaultIID());
	if (children == nil)
		return kFalse;
	for (int32 i = 0; i < children->Length(); ++i)
	{
		if (SetProxyOnPage(children->GetWidget(i), which, budget))
			return kTrue;		// there is one on that page; nothing below wants the second
	}
	return kFalse;
}

void CALLBACK KCMSwitchPageProc(HWND, UINT, UINT_PTR, DWORD)
{
	if (++gPageTicks > kPageGiveUpTicks || gWantedPageBoss == kInvalidClass)
	{
		StopPageTimer();
		return;
	}

	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDialogMgr> dialogMgr(app, UseDefaultIID());
	if (dialogMgr == nil || !dialogMgr->IsModalDialogOpen())
		return;						// not up yet - the dialog is built while this ticks
	IWindow* window = dialogMgr->GetFrontmostDialogWindow();	// raw: not a Query
	if (window == nil)
		return;

	InterfacePtr<IPanelControlData> windowData(window, UseDefaultIID());
	if (windowData == nil)
		return;

	// ⚠★★★IS IT REALLY OUR DIALOG, AND WHERE IN IT IS THE SWITCHER. Both questions are answered by
	//   ONE LOOKUP: the widget kStyleCharParentWidgetID, searched through the whole window. It is
	//   declared in TextStylePanelID.h and belongs to the CHARACTER style options dialog and
	//   nothing else, so finding it identifies the window; and it is the panel that carries
	//   ISelectableDialogSwitcher, so it is also the thing we came for.
	//
	//   ★★★THIS REPLACES TWO ASSUMPTIONS, EACH OF WHICH WAS WRONG OR UNMEASURED (2026-09-13, after
	//   the page switch failed for every attribute in the table while the menu item itself worked):
	//     - "the switcher is the window's FIRST widget" (windowData->GetWidget(0)). Never measured.
	//       ISelectableDialogSwitcher.h's own usage example does not do it that way, and neither
	//       does the product: SpellMenuComponent.cpp:212-214 and this plug-in's own proven reach
	//       into the shortcut editor (KESCLShortcutSetup.cpp:117-126) both do FindWidget with
	//       kSearchLevel_AllDescendants. A dialog's window hands back the dialog's outer panel,
	//       and the selectable panel is a CHILD of it.
	//     - "the panel's class is kTextSelectableCharDialogBoss". TextStylePanelID.h:109-110 says
	//       in so many words that kTextSelectableParaDialogBoss and kTextSelectableCharDialogBoss
	//       are "empty ... used because the CSelectableDialogSwitcher of the
	//       kTextSelectableDialogWidgetBoss sets the bosses classid as the default service id" -
	//       i.e. that ClassID names the SERVICE, and the widget in the window is a different boss.
	//       A test that can never pass costs nothing and reports nothing, which is why this failed
	//       in silence for a day.
	IControlView* panel = windowData->FindWidget(kStyleCharParentWidgetID,
												 IPanelControlData::kSearchLevel_AllDescendants);
	if (panel == nil)
		return;						// another modal dialog, or ours is still being built

	InterfacePtr<ISelectableDialogSwitcher> switcher(panel, UseDefaultIID());
	if (switcher == nil)
		return;						// ⚠wait, do not give up: the dialog may still be assembling
	// ⚠Wait rather than give up: the pages arrive as the dialog builds, so a tick that finds none
	//   is early rather than wrong. ★The give-up count above is what stops this waiting for ever
	//   on an InDesign whose page this build does not know.
	const int32 index = PageIndexOfBoss(switcher, gWantedPageBoss);
	if (index < 0)
		return;

	// ★validate = kFalse. The page being left is the one the dialog just opened on and the reader
	//   has not touched it, so there is nothing to validate and nothing to refuse the move.
	if (switcher->GetCurrentPanelIndex() != index)
		switcher->SwitchDialogPanel(index, kFalse);

	// ★AND THE SQUARE, AFTER THE PAGE IS IN FRONT. All seventeen views exist from the moment the
	//   dialog opens (measured), so the widget could be reached either way round; doing it in this
	//   order means the redraw it asks for is one that is on screen.
	//   ⚠Does nothing on every page but Character Colour, and that is not an error - it is how an
	//     attribute that has no square is told from one that has.
	if (gWantedProxy != kKCMProxyLeaveAlone)
	{
		int budget = 200;
		SetProxyOnPage(PageViewAt(switcher, index), gWantedProxy, budget);
	}
	StopPageTimer();
}

/** Arm the page switch, to happen once the dialog is up. Does nothing for kInvalidClass. */
void ArmPageSwitch(ClassID pageBoss, KCMWantedProxy proxy)
{
	StopPageTimer();
	if (pageBoss == kInvalidClass)
		return;					// ★no page, no proxy either: the timer is what would set it
	gWantedPageBoss = pageBoss;
	gWantedProxy = proxy;
	gPageTicks = 0;
	gPageTimer = ::SetTimer(nullptr, 0, kPageTimerMs, KCMSwitchPageProc);
	if (gPageTimer == 0)
		gWantedPageBoss = kInvalidClass;	// no timer, no switch - the dialog still opens
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMEditSelectedResource (declared in KCMResourceEdit.h)
//----------------------------------------------------------------------------------------
bool16 KCMEditSelectedResource(int32 row, int32 attrIndex)
{
	// ***** NOT WHILE ONE IS ALREADY UP. *****
	//
	// ⚠MEASURED 2026-09-13: a second Character Style Options dialog appeared behind the first. The
	//   dialog opened below is MODAL and runs a message loop of its own, and that loop goes on
	//   delivering the mouse messages still queued for the panel - so the row's handler can be
	//   entered again while this call has not returned, and open another dialog on top of its own.
	// ★A file static rather than a member: this is about "an edit dialog is open right now", which
	//   belongs to the plug-in and not to any one row widget (the row widgets are recycled). The
	//   same shape as the double-click flag in KCMStoryRowEH.cpp.
	// ★Cleared on every path out, including the refusals, by the guard object.
	static bool sOpening = false;
	if (sOpening)
		return kFalse;
	struct Guard
	{
		bool& fFlag;
		Guard(bool& f) : fFlag(f) { fFlag = true; }
		~Guard() { fFlag = false; }
	} guard(sOpening);

	// ⚠★★THE GUARD GOES ON THE Utils OBJECT, NOT ON WHAT IT HANDS BACK. `Utils<T>()->M()`
	//  dereferences before there is anything to test, so a facade that is not registered takes the
	//  panel down with it rather than returning nil ([[utils-boss-facade-access]]). This very
	//  facade has been unregistered once (2026-09-09, KCMFactoryList.h missing its line).
	Utils<IKCMResourcesFacade> resources;
	if (!resources)
		return kFalse;

	PMString kind, key;
	KCMResourceChangeKind what = kKCMResourceChanged;
	if (row < 0 || !resources->GetNthChange(row, kind, key, what))
		return kFalse;	// rebuilt under the click, or the placeholder row: nothing to edit, silently

	// ***** ONLY A CHARACTER STYLE, FOR NOW. ***** ⚠A BACKSTOP AND NOT THE ANSWER THE READER GETS:
	// the menu item is already greyed out for every other kind (CanEditResourceRow), so the ordinary
	// way in cannot reach this line. It is kept for the day the action is fired from somewhere else,
	// and it says which it is rather than failing silently.
	// ⚠It said "Double-click" until 2026-09-13, which stopped being true the evening the gesture
	//   became a right-click menu item.
	if (!kind.IsEqual(PMString(kKCMCharStyleKind)))
	{
		KCMSetStatus("Edit... opens the editor for character styles only, so far.");
		return kFalse;
	}

	// A definition that exists only in the older document has nothing in the Target to open, and
	// the Character Styles panel only ever shows one document's styles.
	if (what == kKCMResourceRemoved)
	{
		KCMSetStatus("That style is only in the Source - there is nothing to edit in the Target.");
		return kFalse;
	}

	Utils<IKCMCompareFacade> compare;
	if (!compare)
		return kFalse;

	IDataBase* targetDB = compare->GetArmedTargetDB();
	if (targetDB == nil || !compare->IsDocDBOpen(targetDB))
	{
		KCMSetStatus("The comparison is no longer running.");
		return kFalse;
	}

	const std::string wantedPath = DecodedKeyPath(key);
	const PMString panelName = LeafOfPath(wantedPath);
	if (panelName.IsEmpty())
		return kFalse;

	// ★A BUILT-IN STYLE HAS NO OPTIONS TO EDIT, and it is refused by its spelling rather than by
	//   being hunted for and not found: the export writes `[None]` as a `$ID/` key, and the panel
	//   draws it under the translated name, so a search for the key's own text would fail and the
	//   reader would be told to refresh a comparison that is perfectly fine.
	if (wantedPath.compare(0, 4, "$ID/") == 0)
	{
		KCMSetStatus("That is a built-in style and has no options to edit.");
		return kFalse;
	}

	// ***** THE TARGET GOES TO THE FRONT FIRST. ***** The Character Styles panel shows the ACTIVE
	// document's styles, and the action below edits the style selected in it - so with the Source
	// in front, the row and the dialog would be about two different files.
	// ★KCMActivateDocument is the route the Deleted row's double click already takes for the Source
	//   (KCMStoryJump.cpp carries why it is a presentation and not a window). It does nothing when
	//   the Target is already in front.
	if (compare->GetActiveDocDB() != targetDB)
		KCMActivateDocument(targetDB);

	ShowCharacterStylesPanel();

	// ***** THE PANEL'S OWN TREE. ***** Reached the way the product reaches somebody else's panel:
	// the palette utilities by WidgetID, then down to the widget wanted (LinksUIUtils,
	// LayerPanelUtils).
	InterfacePtr<IPanelControlData> panelData(
		Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kCharStylePanelWidgetID));
	IControlView* treeView = (panelData == nil) ? nil
		: panelData->FindWidget(kCharStyleTreeViewWidgetID, IPanelControlData::kSearchLevel_AllDescendants);
	if (treeView == nil)
	{
		KCMSetStatus("The Character Styles panel could not be opened.");
		return kFalse;
	}

	InterfacePtr<ITreeViewMgr> treeMgr(treeView, UseDefaultIID());
	InterfacePtr<ITreeViewController> controller(treeView, UseDefaultIID());
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(treeView, UseDefaultIID());
	InterfacePtr<ITreeViewTypeAhead> typeAhead(treeView, UseDefaultIID());
	if (treeMgr == nil || controller == nil || adapter == nil || typeAhead == nil)
		return kFalse;

	// ***** WALK THE TREE DOWN THE PATH, NAMING EACH STEP. ***** See FindNodeByPath for why this
	// is a walk and not ITreeViewMgr::Search, and for the measurement that made the difference
	// visible: a row reading `ZZZ:Same` opened `AAA:Same`.
	NodeID found = FindNodeByPath(adapter, typeAhead, SplitPath(wantedPath));
	if (found == kInvalidNodeID)
	{
		// ★THE WHOLE PATH AS ONE NAME. A colon inside a style's own name is escaped exactly as the
		//   group separator is, so `A:B` may be one style rather than a style in a group. Tried
		//   second, because a group is by far the commoner reading of the same characters.
		std::vector<std::string> whole;
		whole.push_back(wantedPath);
		found = FindNodeByPath(adapter, typeAhead, whole);
	}
	if (found == kInvalidNodeID)
	{
		// Renamed or deleted since the comparison ran - the list is a photograph, not a live view.
		KCMSetStatus("That style is not in the Character Styles panel. Start or Refresh the comparison.");
		return kFalse;
	}

	// ★Into view: ScrollToNode opens the groups above it (ITreeViewMgr.h:114), which is the half of
	//   this feature the reader asked for in so many words - the panel showing the thing the row
	//   names. ⚠It is not needed to FIND anything; the walk above works through closed groups.
	treeMgr->ScrollToNode(found, ITreeViewMgr::eScrollIntoView);

	// ***** SELECT IT - AND TELL NOBODY. *****
	//
	// ★★★notifyOfChange = kFalse IS THE WHOLE FEATURE. Measured 2026-09-13: an ordinary click on a
	//   style row sets the DOCUMENT'S default character style ([なし] -> KCMEditProbe), because the
	//   panel's observer applies whatever the selection broadcasts. A comparison panel must not
	//   change the document it is comparing - it would appear in KCM's own Resources list as a
	//   difference the reader never made. The hilite is the selection; the NOTIFICATION is what
	//   applies. With the notification off, the document was measured unchanged: the default style
	//   stayed [なし] and both probe styles kept their sizes.
	// ⚠★★DESELECT FIRST. Select() has no "deselect the others" argument (its second and third are
	//   notifyOfChange and changeHilite), so without this the row the panel was already on stays
	//   selected too - measured, [なし] and the wanted style both came back selected - and an action
	//   that edits ONE style stays greyed out.
	controller->DeselectAll(kFalse, kTrue);
	controller->Select(found, kFalse, kTrue);

	// ***** AND LET THE PRODUCT OPEN ITS OWN DIALOG. *****
	//
	// ★★★WHY THE ACTION AND NOT A DIALOG OF OUR OWN. Building the dialog by hand was tried and
	//   measured to the end (2026-09-13): the resource does hand back the product's very panel
	//   (kTextSelectableCharDialogBoss, widget kStyleCharParentWidgetID) with the right service id,
	//   but its sixteen pages never arrive - they are driven by an ITextTargetServer the product
	//   starts, which nothing public reaches - and a dialog opened in that state shows "[None]"
	//   with an empty list and **ends the process when it is cancelled**
	//   (CSelectableDialogSwitcher::QueryGroupPanelControlData, three times).
	// ★kCharStyleOptionsActionID is the panel menu's own "Style Options...", and it reads the very
	//   selection set above. Measured: greyed while nothing was selected, live the moment the
	//   silent selection was made, and it opened the complete dialog on the right style.
	// ⚠NOT kContextMenuStyleOptionsActionID. That one is the right-click twin and reads the row the
	//   menu was popped over, which the styles panel stashes privately - nothing outside it can say
	//   which row that was.
	// ★PerformAction with the session's active context, the form every product caller uses
	//   (LinksUIButtonObserver.cpp:175-177 and three more in dynamicdocumentsui).
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IActionManager> actionMgr(app == nil ? nil : app->QueryActionManager());
	if (actionMgr == nil)
		return kFalse;

	// ***** AND OPEN IT AT THE PAGE THAT HOLDS THE ATTRIBUTE. ***** Armed BEFORE the action,
	// because PerformAction does not come back until the dialog is dismissed - see the timer's own
	// note for why a thread timer is the only thing that runs in there.
	// ★An attribute the table does not know, and a DEFINITION row (attrIndex < 0), arm nothing and
	//   the dialog opens on its own first page.
	if (attrIndex >= 0)
	{
		PMString attrName, attrSource, attrTarget;
		if (resources->GetNthAttr(row, attrIndex, attrName, attrSource, attrTarget))
			ArmPageSwitch(KCMPageBossForAttribute(attrName), KCMProxyForAttribute(attrName));
	}

	actionMgr->PerformAction(GetExecutionContextSession()->GetActiveContext(),
							 kCharStyleOptionsActionID);

	// ★The timer stops itself once it has switched, and again after five seconds if it never found
	//   the dialog - but the dialog has certainly gone by the time PerformAction returns, so this
	//   is the honest place to be sure nothing is left ticking.
	StopPageTimer();
	return kTrue;
}

//========================================================================================
// The two "Edit..." items on the row menus
//========================================================================================

namespace
{

/** Whether row `row` of the Resources list is one this can open an editor for.

	★THE SAME QUESTION THE ACTION ASKS ITSELF, so the menu and the outcome cannot part company -
	the shape every other item on these menus follows (KCMResourceRowHasXml,
	KCMChangeRowCanCopySource).
	⚠The MODE is asked first: the stashed row is an index into whichever list was on screen, so in
	  the Story mode it names a story and reading it as a definition would answer about whatever
	  definition happens to sit at that number.
*/
bool16 CanEditResourceRow(int32 row)
{
	if (!KCMListShowsResources() || row < 0)
		return kFalse;

	Utils<IKCMCompareFacade> compare;
	if (!compare || !compare->IsArmed())
		return kFalse;

	Utils<IKCMResourcesFacade> resources;
	if (!resources)
		return kFalse;

	PMString kind, key;
	KCMResourceChangeKind what = kKCMResourceChanged;
	if (!resources->GetNthChange(row, kind, key, what))
		return kFalse;		// the list was rebuilt shorter, or this is the placeholder row

	// Only what there is an editor for, and only what the Target still has.
	return (kind.IsEqual(PMString(kKCMCharStyleKind)) && what != kKCMResourceRemoved) ? kTrue : kFalse;
}

/** Whether attribute `attr` of row `row` is one this can open the editor AT ITS OWN PAGE.

	★★★AN ATTRIBUTE WHOSE PAGE IS NOT KNOWN IS REFUSED, NOT OPENED ANYWAY (2026-09-13, the user's
	call: "when it is not handled, I would rather the Edit... item were greyed out, or not there").
	Opening the dialog at whatever page it opens itself at is not a smaller version of this item -
	it is a different thing, and one the reader cannot tell apart from the item having worked. They
	would be looking at the General page for a strikethrough, with nothing on screen saying that
	this is not where it lives.
	★NOTHING IS LOST BY REFUSING: the DEFINITION row above it still offers "Edit..." and still opens
	  the same dialog, so the style is always reachable - what the greying withholds is only the
	  promise to land on the right page.
	★★WHAT THE READER ACTUALLY SEES IS NO MENU AT ALL (measured 2026-09-13, the user: "it is not
	  greyed - the menu does not come up, and that is fine"). The other two items on this menu are
	  the Story mode's, so in the Resources mode every item on it is disabled at once, and InDesign
	  does not pop a menu with nothing live on it. ⇒ **"Greyed" below is what this code does; "no
	  menu" is what it looks like.** The mirror image holds in the other direction: Restore Source
	  Text is greyed in the Resources mode, and this item is the only one that could be live there.
	⇒ **This is the one place that decides it, and the table is the one place that answers it.**
	  An attribute added to kKCMAttributePages becomes live here on the same build.
*/
bool16 CanEditResourceAttr(int32 row, int32 attr)
{
	if (!CanEditResourceRow(row) || attr < 0)
		return kFalse;

	Utils<IKCMResourcesFacade> resources;
	if (!resources)
		return kFalse;

	PMString name, source, target;
	if (!resources->GetNthAttr(row, attr, name, source, target))
		return kFalse;		// the list was rebuilt under the menu, or the attribute is gone

	return (KCMPageBossForAttribute(name) != kInvalidClass) ? kTrue : kFalse;
}

}	// anonymous namespace

bool16 KCMResourceRowCanEdit()
{
	return CanEditResourceRow(KCMStoryMenuRow());
}

void KCMEditMenuResourceRow()
{
	KCMEditSelectedResource(KCMStoryMenuRow(), -1);
}

bool16 KCMResourceAttrCanEdit()
{
	int32 row = -1, attr = -1;
	if (!KCMStoryGetMenuChange(row, attr))
		return kFalse;
	return CanEditResourceAttr(row, attr);
}

void KCMEditMenuResourceAttr()
{
	int32 row = -1, attr = -1;
	if (!KCMStoryGetMenuChange(row, attr))
		return;
	// ★The same test the menu asked, asked again here. It cannot normally be false - a greyed item
	//   is not dispatched - but the pair is kept honest the way every other item on these menus is,
	//   so that the day one of them is fired from somewhere else the answer is still the same one.
	if (!CanEditResourceAttr(row, attr))
		return;
	KCMEditSelectedResource(row, attr);
}

// End, KCMResourceEdit.cpp.
