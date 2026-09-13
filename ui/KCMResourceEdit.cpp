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
#include "ITreeViewMgr.h"			// Search(name) / ScrollToNode

// ----- General -----
#include "NodeID.h"					// NodeID / kInvalidNodeID
#include "StylePanelID.h"			// the Character Styles panel, its tree, and its action
#include "Utils.h"

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

/** The style's name AS THE PANEL SPELLS IT, from the row's key.

	★★TWO SPELLINGS MEET HERE, AND THE PANEL'S IS THE SHORTER ONE. The exporter writes a style
	inside a group as `CharacterStyle/グループ%3a名前` (KCMXmlPretty.h measured the escape); the
	Character Styles panel draws that style as a row saying `名前` UNDER a row saying `グループ`.
	So the kind prefix comes off, the escapes are decoded, and what is left after the LAST colon is
	what the panel's own search will match.
	⚠Percent-decoding works on bytes, so the round trip is UTF-8 both ways, never the platform
	  string - a style really is called `段落スタイル 1` here.
	⚠A `/` INSIDE a name arrives as `%2f` and survives, because only the exporter's own separator
	  is written plain.
*/
PMString PanelNameFromKey(const PMString& key)
{
	std::string utf8 = key.GetUTF8String();
	const std::string prefix = std::string(kKCMCharStyleKind) + "/";
	if (utf8.compare(0, prefix.size(), prefix) == 0)
		utf8.erase(0, prefix.size());

	utf8 = KCMDecodePercentEscapes(utf8);
	const std::string::size_type colon = utf8.rfind(':');
	if (colon != std::string::npos)
		utf8.erase(0, colon + 1);

	PMString name;
	name.SetUTF8String(utf8);
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

/** The row text of the node the search landed on, or empty when that row is not built.

	★WHY IT IS CHECKED. ITreeViewMgr::Search matches a node whose text STARTS WITH what is given
	(ITreeViewMgr.h:162-165), so a document holding both `Body` and `BodyText` can answer the wrong
	one. The row itself is the only thing that can settle it.
	⚠QueryWidgetFromNode answers nil for a row that is not built - one scrolled out of view, or
	  inside a closed group (ITreeViewMgr.h:184-187) - which is why ScrollToNode runs before this
	  and why an empty answer is treated as "cannot tell" rather than as "wrong row".
	★The name is in the SECOND cell; the first and third are the style's icons and came back empty
	  when this was measured (2026-09-13). So every cell is read and the first non-empty one wins,
	  rather than an index being written down that the next version of the panel could move.
*/
PMString RowTextOf(ITreeViewMgr* treeMgr, const NodeID& node)
{
	PMString text;
	text.SetTranslatable(kFalse);
	if (treeMgr == nil)
		return text;

	// ⚠QueryWidgetFromNode hands back a reference; InterfacePtr takes ownership of it.
	InterfacePtr<IControlView> rowWidget(treeMgr->QueryWidgetFromNode(node));
	if (rowWidget == nil)
		return text;

	InterfacePtr<IPanelControlData> cells(rowWidget, UseDefaultIID());
	if (cells == nil)
		return text;
	for (int32 i = 0; i < cells->Length(); ++i)
	{
		InterfacePtr<ITextControlData> cellText(cells->GetWidget(i), UseDefaultIID());
		if (cellText == nil)
			continue;
		PMString candidate = cellText->GetString();
		candidate.SetTranslatable(kFalse);
		if (!candidate.IsEmpty())
			return candidate;
	}
	return text;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMEditSelectedResource (declared in KCMResourceEdit.h)
//----------------------------------------------------------------------------------------
bool16 KCMEditSelectedResource(int32 row)
{
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

	const PMString panelName = PanelNameFromKey(key);
	if (panelName.IsEmpty())
		return kFalse;

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
	if (treeMgr == nil || controller == nil)
		return kFalse;

	// ***** ASK THE TREE FOR THE ROW BY NAME. *****
	//
	// ★★★THIS IS THE ONLY WAY IN, AND IT IS NOT A CONVENIENCE. Measured 2026-09-13: the nodes of
	//   the Character Styles tree are NOT the public UIDNodeID but a class of the styles panel's
	//   own, so nothing outside that plug-in can build a NodeID for a style. Search() takes a NAME
	//   and needs the tree to carry ITreeViewTypeAhead - which this one does
	//   (kCharStyleTreeViewTypeAheadImpl, in the boss dump).
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IActionManager> actionMgr(app == nil ? nil : app->QueryActionManager());
	if (actionMgr == nil)
		return kFalse;

	NodeID found = treeMgr->Search(panelName);
	if (found == kInvalidNodeID)
	{
		// ***** A STYLE INSIDE A CLOSED GROUP IS NOT THERE TO BE FOUND. *****
		//
		// ⚠MEASURED 2026-09-13: `KCMGroup:KCMGrouped` came back kInvalidNodeID while the plain
		//   `KCMEditProbe` was found at once. Search works through ITreeViewTypeAhead, which reads
		//   the rows the tree has BUILT, and a closed group has built none of its children. (This
		//   is also why ScrollToNode cannot be the answer: its "ancestors will be expanded" needs
		//   the node, which is the thing we have not got.)
		// ★So the panel is asked to open its groups, using its own menu item, and the search is
		//   repeated. This changes what the PANEL shows and nothing in the document - the same
		//   kind of change as scrolling it.
		// ★Only on failure. A style that is not in a group never disturbs the reader's groups.
		actionMgr->PerformAction(GetExecutionContextSession()->GetActiveContext(),
								 kCharOpenAllStyleGroupsActionID);
		found = treeMgr->Search(panelName);
	}
	if (found == kInvalidNodeID)
	{
		// Renamed or deleted since the comparison ran - the list is a photograph, not a live view.
		KCMSetStatus("That style is not in the Character Styles panel. Start or Refresh the comparison.");
		return kFalse;
	}

	// ★Into view before anything else: ScrollToNode opens the groups above it (ITreeViewMgr.h:114),
	//   which is both what the reader asked for - the panel showing the thing the row names - and
	//   what makes the row below readable.
	treeMgr->ScrollToNode(found, ITreeViewMgr::eScrollIntoView);

	// ★The search is a PREFIX match, so the row it landed on is read back. An empty answer means
	//   the row is not built and the check simply cannot be made; a different answer means the
	//   search found somebody else's style and this must not go on to open a dialog on it.
	const PMString rowText = RowTextOf(treeMgr, found);
	if (!rowText.IsEmpty() && !rowText.IsEqual(panelName))
	{
		KCMSetStatus("The panel matched a different style. Not opening it.");
		return kFalse;
	}

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
	actionMgr->PerformAction(GetExecutionContextSession()->GetActiveContext(),
							 kCharStyleOptionsActionID);
	return kTrue;
}

// End, KCMResourceEdit.cpp.
