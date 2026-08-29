//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  Which click on a chapter row of the book comparison dialog means what. What each one DOES is
//  KCMBookOpen.cpp.
//
//    * double click  -> open that chapter
//    * right click   -> pop the row menu ("Start Change Marker")
//    * single click  -> nothing of ours. The stock handler selects the row and that is all it does.
//
//  ★SINGLE CLICK IS DELIBERATELY EMPTY, where the panel's Story Edits row jumps on it. A jump moves
//  the view of a document already on screen; here a single click would have to OPEN a document, and
//  opening one by brushing past a row in a list is not something a user can undo by clicking
//  elsewhere. Two clicks to open is what every file list in the OS does.
//
//  ***** THE DOUBLE CLICK'S ONE BIT OF STATE. ***** A double click arrives as FOUR events:
//
//      LButtonDn   LButtonUp   ButtonDblClk   LButtonUp
//
//  - so ButtonDblClk is announced only after the first up has already been and gone, and a SECOND up
//  follows it. Acting inside ButtonDblClk therefore fires while the trailing up is still to come. So
//  ButtonDblClk only RAISES A FLAG and the trailing up reads it.
//
//  ! The flag is cleared in LButtonDn, which is what makes it safe: every click begins with a down,
//    so a flag that was set but never consumed cannot survive into the next click and turn an
//    ordinary single click into an open. (Story Edits learned this the same way - the paragraph
//    headed "THE DOUBLE CLICK'S ONE BIT OF STATE" in KCMStoryRowEH.cpp; KBS before it,
//    KBSResultNodeEH.cpp:75-93.)
//    ⚠ That was written as KCMStoryRowEH.cpp:24-36 and pointed five lines short by 2026-08-18
//      (bug recheck B-U5). B-U4 had corrected a stale claim at the head of that file the SAME DAY,
//      which pushed the paragraph down - a reference into a file being edited goes wrong on the day
//      it is edited. ★The heading is what to quote: it has been there since the file was written
//      and does not move when the lines above it do.
//
//  ★THE KEYBOARD IS LEFT ALONE - no AcquireKeyFocus anywhere in this file. The panel's list takes
//  the key focus so the arrows walk it; this list lives in a DIALOG, where the keyboard belongs to
//  the dialog's own machinery. Taking it here would break that for the sake of arrow keys nobody
//  asked for.
//  ⚠ That machinery was written out as "tab order, Return = OK, Esc = close" until 2026-08-18 (bug
//    recheck B-U5, second pass). ***THIS DIALOG HAS NO BUTTONS AT ALL*** - they went when the
//    Compare button was removed on 2026-08-12 (KCMUI.fr's view resource holds two path lines, the
//    status line, the hint and this list, and nothing else), so there is no OK for Return to press.
//    What closes it is the close box and Esc. The examples were quoted from what a dialog USUALLY
//    has rather than read off this one.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"			// the route from the app to IMenuManager (the right-click popup)
#include "IApplication.h"
#include "IEvent.h"					// ShiftKeyDown / CmdKeyDown; GlobalWhere - where to pop the menu
#include "IMenuManager.h"			// HandlePopupMenu - pops kKCMBookRowMenuName at the cursor
#include "ISession.h"
#include "ITreeNodeIDData.h"		// this node's NodeID
#include "ITreeViewController.h"	// IsSelected - is this the row the click landed on?
#include "IWidgetParent.h"			// QueryParentFor - the row -> the tree that owns the selection

// General includes:
#include "ListIndexNodeID.h"		// the node class ListTreeViewAdapter hands out (KCMBookTreeAdapter.cpp)

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KCMStoryRowEH.cpp and
// KCMBookTreeWidgetMgr.cpp: the build files that would carry such a directory live outside this
// plug-in's repository, so a path added there would not survive a fresh checkout.
#include "../../open/includes/widgets/TreeNodeEventHandler.h"	// stock base for a tree row

// Project includes:
#include "KCMBookOpen.h"
#include "KCMUIID.h"

namespace
{

// A file static rather than a member: row widgets are recycled as the list scrolls, so this belongs
// to "the click going on right now" rather than to any one row. One click happens at a time.
bool gOpenOnNextButtonUp = false;

}	// anonymous namespace

class KCMBookRowEH : public TreeNodeEventHandler
{
public:
	KCMBookRowEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KCMBookRowEH() {}

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 ButtonDblClk(IEvent* e);
	virtual bool16 RButtonDn(IEvent* e);

private:
	/** This row's index in the model, or -1 when this widget cannot say. No selection test - the
		right click asks it about the row under the pointer, which is not necessarily selected. */
	int32 RowIndex() const;

	/** Which row this click is for, or -1 when it should be left alone.

		Answers -1 for a click the stock handler already used, for a modified click, and for a row
		that is not the selected one - so a caller gets one test instead of four. */
	int32 RowForClick(IEvent* e, bool16 baseHandled) const;
};

CREATE_PMINTERFACE(KCMBookRowEH, kKCMBookRowEHImpl)

int32 KCMBookRowEH::RowIndex() const
{
	// The node's NodeID lives on this boss's ITreeNodeIDData (every tree node widget carries it).
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return -1;
	TreeNodePtr<ListIndexNodeID> nodeID(nodeData->Get());
	if (nodeID == nil)
		return -1;
	return nodeID->GetIndex();
}

int32 KCMBookRowEH::RowForClick(IEvent* e, bool16 baseHandled) const
{
	if (baseHandled || e == nil || e->ShiftKeyDown() || e->CmdKeyDown())
		return -1;

	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return -1;
	const NodeID& node = nodeData->Get();

	// The selection lives on the tree, not on the row, so ask upwards for it.
	InterfacePtr<const IWidgetParent> widgetParent(this, UseDefaultIID());
	if (widgetParent == nil)
		return -1;
	InterfacePtr<ITreeViewController> treeController(
		static_cast<ITreeViewController*>(widgetParent->QueryParentFor(ITreeViewController::kDefaultIID)));
	if (treeController == nil || !treeController->IsSelected(node))
		return -1;

	// WHICH row this is is the node's own answer, and is given in one place (RowIndex) - the
	// shape KCMStoryRowEH::RowForClick ends in.
	return this->RowIndex();
}

// Nothing of this plug-in's own happens on the way DOWN. The one job here is to start every click
// with the double-click flag down.
bool16 KCMBookRowEH::LButtonDn(IEvent* e)
{
	gOpenOnNextButtonUp = false;
	return TreeNodeEventHandler::LButtonDn(e);
}

// The second click of a double click. Only raises the flag - see the note at the head of this file.
bool16 KCMBookRowEH::ButtonDblClk(IEvent* e)
{
	const bool16 result = TreeNodeEventHandler::ButtonDblClk(e);
	if (this->RowForClick(e, result) >= 0)
		gOpenOnNextButtonUp = true;
	return result;
}

bool16 KCMBookRowEH::LButtonUp(IEvent* e)
{
	// Consumed here, on the way in, so that every path out of this function leaves it down.
	const bool openIt = gOpenOnNextButtonUp;
	gOpenOnNextButtonUp = false;

	// Let the stock handler finish the click first (selection, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);

	if (!openIt)
		return result;		// an ordinary single click: the row is selected, and that is all

	const int32 rowIndex = this->RowForClick(e, result);
	if (rowIndex < 0)
		return result;

	KCMBookOpenChapterForRow(rowIndex);
	return result;
}

// Right-click on a row: pop the row menu at the cursor. Same machinery as the real Links and Layers
// panel row menus, and as KBS's result rows (KBSResultNodeEH::RButtonDn), which this is copied from:
// HandlePopupMenu pops the MenuDef subtree named kKCMBookRowMenuName, and the item the user picks
// fires through the ordinary action component.
//
// ***** THE ROW IS STASHED FIRST. ***** The action is handed no widget context of its own - it runs
// later, from the menu, knowing only its ActionID - so KCMBookMenuRow() is how it learns which
// chapter the menu was about. Both the action and its enabling test read it.
//
// Deliberately NOT calling the stock handler and NOT changing the selection: a right click that is
// only asking for a menu should not move the user's place in the list. (KBS settled on the same
// rule.)
bool16 KCMBookRowEH::RButtonDn(IEvent* e)
{
	const int32 rowIndex = this->RowIndex();
	if (rowIndex < 0 || e == nil)
		return TreeNodeEventHandler::RButtonDn(e);

	KCMBookSetMenuRow(rowIndex);

	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (app == nil)
		return kTrue;
	InterfacePtr<IActionManager> actionMgr(app->QueryActionManager());
	if (actionMgr == nil)
		return kTrue;
	InterfacePtr<IMenuManager> menuMgr(actionMgr, UseDefaultIID());
	if (menuMgr == nil)
		return kTrue;

	menuMgr->HandlePopupMenu(kKCMBookRowMenuName, e->GlobalWhere(), e->GlobalWhere(), kTrue, this);
	return kTrue;
}

// End, KCMBookRowEH.cpp.
