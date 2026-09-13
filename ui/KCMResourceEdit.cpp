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
#include "ITextControlData.h"		// what the row the search found actually says
#include "ITreeViewController.h"	// DeselectAll / Select - WITHOUT notifying
#include "ITreeViewHierarchyAdapter.h"	// the tree's own rows, open or closed
#include "ITreeViewMgr.h"			// ScrollToNode
#include "ITreeViewTypeAhead.h"		// GetStringForNode - a node's name without its row

// ----- General -----
#include "NodeID.h"					// NodeID / kInvalidNodeID
#include "PersistUtils.h"			// ::GetClass - is the front modal really OUR dialog?
// The bosses that supply the dialog's pages. Published under source/open, reached by a relative
// path rather than by adding an include directory - the same reasoning, and the same route, as
// KCMStoryRowEH.cpp's TreeNodeEventHandler.h: the build files that would carry such a directory
// live outside this plug-in's repository, so a path added there would not survive a fresh checkout.
#include "../../open/interfaces/text/CharPanelID.h"	// kCharDialogHookBoss - the Basic Character Formats page
#include "StylePanelID.h"			// the Character Styles panel, its tree, and its action
#include "TextStylePanelID.h"		// kTextSelectableCharDialogBoss - what that dialog's panel is
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
// Opening the dialog AT THE PAGE that holds the attribute the reader double-clicked
//========================================================================================

/** Which of the dialog's pages holds a given IDML attribute, BY THE BOSS THAT SUPPLIES THE PAGE.

	★★★NOT BY POSITION, AND THE REASON IS THE LANGUAGE (2026-09-13, the user's question: "I am
	testing on the Japanese version - will the English one pick the right item too?"). **The set of
	pages differs by feature set.** A Japanese InDesign shows seven pages the Roman one has not -
	縦中横, five ruby pages and two kenten pages - so every position below them means a different
	page in the two. The Basic Character Formats page happens to be second from the top in both,
	which is exactly the kind of coincidence that makes a positional table look right until the day
	it is not.

	★★WHAT A PAGE IS, INSTEAD: the boss of the panel that supplies it. Measured in the service
	registry - the pages register under service kTextStyleCharDialogBoss (0x5b09):

	    kTextStyleCharGeneralPanelBoss   General                    TEXT STYLE PANEL
	    kCharDialogHookBoss              Basic Character Formats    CHARACTER PANEL
	    kCharDialog2HookBoss             Advanced Character Formats CHARACTER PANEL
	    kTextColorDialogHookBoss         Character Color            TEXT COLOR PANEL
	    kCharOpenTypeDialogHookBoss      OpenType Features          CHARACTER PANEL
	    kCharUnderlineDialogHookBoss     Underline Options          CHARACTER PANEL
	    kCharStrikeThroughDialogHookBoss Strikethrough Options      CHARACTER PANEL
	    kStyleToTagMapDialogBoss         Export Tagging             TEXT STYLE PANEL

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

const KCMAttributePage kKCMAttributePages[] =
{
	{ "AppliedFont",				kCharDialogHookBoss },				// 基本文字形式 / Basic Character Formats
	{ "ExtendedKeyboardShortcut",	kTextStyleCharGeneralPanelBoss },	// 一般 / General - the shortcut field
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
ClassID		gWantedPageBoss	= kInvalidClass;
UINT_PTR	gPageTimer		= 0;
int			gPageTicks		= 0;
const UINT	kPageTimerMs	= 100;
const int	kPageGiveUpTicks = 50;		// five seconds, then stop by itself

void StopPageTimer()
{
	if (gPageTimer != 0)
	{
		::KillTimer(nullptr, gPageTimer);
		gPageTimer = 0;
	}
	gWantedPageBoss = kInvalidClass;
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
	if (windowData == nil || windowData->Length() == 0)
		return;
	IControlView* panel = windowData->GetWidget(0);
	if (panel == nil)
		return;

	// ⚠★★★IS IT REALLY OUR DIALOG. This timer fires inside WHATEVER modal loop is running, and the
	//   reader may have raised something else in the meantime. Switching a page on a dialog that
	//   merely happens to be in front would be acting on a stranger - so the panel's boss is
	//   checked, which is the one thing that says what this window IS.
	if (::GetClass(panel) != kTextSelectableCharDialogBoss)
		return;

	InterfacePtr<ISelectableDialogSwitcher> switcher(panel, UseDefaultIID());
	if (switcher == nil)
	{
		StopPageTimer();
		return;
	}
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
	StopPageTimer();
}

/** Arm the page switch, to happen once the dialog is up. Does nothing for kInvalidClass. */
void ArmPageSwitch(ClassID pageBoss)
{
	StopPageTimer();
	if (pageBoss == kInvalidClass)
		return;
	gWantedPageBoss = pageBoss;
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

	// ***** ONLY A CHARACTER STYLE, FOR NOW. ***** Said out loud rather than swallowed: a double
	// click that does nothing looks broken, and the reader has no way to tell "not yet" from
	// "not ever".
	if (!kind.IsEqual(PMString(kKCMCharStyleKind)))
	{
		KCMSetStatus("Double-click opens the editor for character styles only, so far.");
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
			ArmPageSwitch(KCMPageBossForAttribute(attrName));
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
	return CanEditResourceRow(row);
}

void KCMEditMenuResourceAttr()
{
	int32 row = -1, attr = -1;
	if (!KCMStoryGetMenuChange(row, attr))
		return;
	KCMEditSelectedResource(row, attr);
}

// End, KCMResourceEdit.cpp.
