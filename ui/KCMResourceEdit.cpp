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
#include "StylePanelID.h"			// the Character Styles panel, its tree, and its action
#include "Utils.h"

#include <string>
#include <vector>

// ----- Project -----
#include "KCMUIID.h"
#include "IKCMCompareFacade.h"		// GetArmedTargetDB / IsDocDBOpen / GetActiveDocDB
#include "IKCMResourcesFacade.h"	// GetNthChange
#include "KCMUIShared.h"			// KCMSetStatus
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

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMEditSelectedResource (declared in KCMResourceEdit.h)
//----------------------------------------------------------------------------------------
bool16 KCMEditSelectedResource(int32 row)
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
	actionMgr->PerformAction(GetExecutionContextSession()->GetActiveContext(),
							 kCharStyleOptionsActionID);
	return kTrue;
}

// End, KCMResourceEdit.cpp.
