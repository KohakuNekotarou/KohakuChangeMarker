//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryTreeController.h. The list's controller is the tree's own (CTreeViewController, public/includes) made a
//  multiple-selection one, with KCM's rule on top: ROWS ARE SELECTED TOGETHER WHEN THEY ARE CHANGE ROWS OF THE STORY
//  LIST - of any story (design 1-3). A story row is selected alone; so is every row of the Resources mode's list (its
//  rows act on one definition at a time). The shape is KFC's (KFCResultTreeController.cpp), itself Adobe's: the Layers
//  panel's controller runs the stock rules and then takes rows of the other kind out (open/components/layerpanel/
//  LayersPanelTreeViewController.cpp).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"			// the panel manager (TakeKeyboard) and the keyboard
#include "IControlView.h"
#include "IEvent.h"
#include "IEventHandler.h"			// the list's handler - the keyboard's holder
#include "IKeyBoard.h"
#include "IPanelControlData.h"		// SetKeyboardFocus - the list given the keyboard by its parent panel
#include "IPanelMgr.h"				// ShowPanelByWidgetID(..., giveKeyFocus) - the panel made the active one
#include "ISession.h"
#include "ITreeNodeIDData.h"
#include "ITreeViewController.h"
#include "ITreeViewHierarchyAdapter.h"
#include "ITreeViewMgr.h"
#include "IWidgetParent.h"			// the list's parent panel

// General includes:
#include "CTreeViewController.h"	// the stock controller (public/includes) - DV_WidgetBin
#include "CTreeViewMgr.h"			// AboveOrBelow - which way a run of rows goes
#include "TreeNodeTraverser.h"		// the rows in the order they are drawn (public/libs/widgetbin/includes)

#include <vector>

// Project includes:
#include "KCMUIID.h"
#include "KCMStoryNodeID.h"
#include "KCMStoryTree.h"			// KCMListShowsResources - whose rows are never selected together
#include "KCMStoryJump.h"			// KCMStoryJumpToChange - the landing the page follows
#include "KCMUIShared.h"			// KCMFindPanelWidget - the list on screen
#include "KCMPanelAlpha.h"			// KCMPanelHasSystemKeyboard - the second keyboard question
#include "KCMStoryTreeController.h"

namespace
{

// CAN TWO ROWS BE SELECTED TOGETHER? As change rows of the Story list only.
bool16 Joins(const NodeID& a, const NodeID& b)
{
	if (KCMListShowsResources())
		return kFalse;
	TreeNodePtr<KCMStoryNodeID> first(a), second(b);
	return (first != nil && second != nil && first->IsChangeRow() && second->IsChangeRow()) ? kTrue : kFalse;
}

// THE MOVING END OF A RUN: where the last click or Shift+arrow left it (Shift+Up / Shift+Down move it; the run's other end
// is the controller's master item, where the run began). Two numbers rather than a NodeID, which owns a heap object (a
// static NodeID would be destroyed at DLL unload, after the session it belongs to - KFC's note).
int32 gRunEndRow = -1;
int32 gRunEndChange = -1;

void NoteRunEnd(const NodeID& node)
{
	TreeNodePtr<KCMStoryNodeID> nodeID(node);
	gRunEndRow = (nodeID != nil && nodeID->IsChangeRow()) ? nodeID->GetRow() : -1;
	gRunEndChange = (nodeID != nil && nodeID->IsChangeRow()) ? nodeID->GetChange() : -1;
}

// The list on screen - nil while the panel is closed or the section has not been built.
IControlView* ListView()
{
	return KCMFindPanelWidget(kKCMStoryTreeWidgetID);
}

// A RUN OF ROWS selected: anchor to end, in the order drawn, each through the controller's Select (which turns away a row
// that cannot join - a story row between two stories' change rows is passed over). Announced once, by the row that ends
// it - the Layers panel's shape, which KFC's SelectRun follows.
void SelectRun(ITreeViewController* controller, const NodeID& anchor, const NodeID& end)
{
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	(void)controller->Select(anchor, (anchor == end) ? kTrue : kFalse, kTrue);
	if (anchor == end)
		return;
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (adapter == nil || treeMgr == nil)
		return;
	const int32 aboveOrBelow = CTreeViewMgr::AboveOrBelow(adapter, anchor, end);
	TreeNodeTraverser walk(anchor, treeMgr, kTrue /*expandedNodesOnly*/,
		(aboveOrBelow < 0) ? TreeNodeTraverser::eForward : TreeNodeTraverser::eReverse);
	NodeID next;
	while (!walk.Completed() && next != end)
	{
		next = walk.Next();
		if (next.IsValid())
			(void)controller->Select(next, (next == end) ? kTrue : kFalse, kTrue);
	}
}

}	// anonymous namespace

//========================================================================================
// The controller
//========================================================================================
class KCMStoryTreeController : public CTreeViewController
{
public:
	KCMStoryTreeController(IPMUnknown* boss) : CTreeViewController(boss) { ForceMultipleSelection(); }
	virtual ~KCMStoryTreeController() {}

	virtual void ProcessSelectionRules(IEvent* event, UID nodeWidgetUID, bool16 notifyOfChange);
	virtual SelectCode Select(const NodeID& node, bool16 notifyOfChange = kTrue, bool16 changeHilite = kTrue);

	/** The stock controller's own data, then the mode set again (ForceMultipleSelection). Called by the persistence macro
		on this class (InterfaceFactory.h, PRIVATE_DEFINE_READWRITE), so it is the one that runs. */
	void ReadWrite(IPMStream* s, ImplementationID prop);

protected:
	virtual bool16 IgnoreIfNodeIsSelected() const;

private:
	// MULTIPLE SELECTION, SEVERAL PARENTS, GAPS ALLOWED (KCMUI.fr says the same). Set here as well because the stock
	// controller keeps its mode with the panel's saved state (CTreeViewController::ReadWrite): a panel saved by an earlier
	// KCM, one row at a time, would come back that way whatever the .fr says now (KFC's note). Several parents: a change
	// row hangs off its story row, and the rows of two stories are selected together; Joins keeps them change rows.
	void ForceMultipleSelection()
	{
		fNumSelectableItems = eAllowMultipleSelection;
		fAllowMultipleParents = kTrue;
		fAllowDiscontiguousSelection = kTrue;
	}
};

// PERSISTENT, as the stock one it replaces is: a non-persistent implementation over an inherited persistent one fails as
// the resource is read (memory widget-boss-inherited-persistent-interface; KFC's KFCUI.fr says the same).
CREATE_PERSIST_PMINTERFACE(KCMStoryTreeController, kKCMStoryTreeControllerImpl)

void KCMStoryTreeController::ReadWrite(IPMStream* s, ImplementationID prop)
{
	CTreeViewController::ReadWrite(s, prop);
	ForceMultipleSelection();
}

// A PLAIN CLICK ON ONE OF SEVERAL SELECTED ROWS SELECTS THAT ROW ALONE. The stock rule leaves the selection as it is when
// the row clicked is already selected; with several selected, the click's jump would then act on one row under a list
// still showing several.
bool16 KCMStoryTreeController::IgnoreIfNodeIsSelected() const
{
	return (fSelectedNodes.size() <= 1) ? kTrue : kFalse;
}

void KCMStoryTreeController::ProcessSelectionRules(IEvent* event, UID nodeWidgetUID, bool16 notifyOfChange)
{
	// A LEFT PRESS WITH SHIFT OR CTRL ON A ROW THAT CANNOT JOIN: that row alone (the Layers panel's rule - rows of one kind
	// together; the newly clicked kind stays). Everything else is the stock rules', the run's end noted after.
	InterfacePtr<IControlView> nodeView(::GetDataBase(this), nodeWidgetUID, UseDefaultIID());
	InterfacePtr<ITreeNodeIDData> data(nodeView, UseDefaultIID());
	if (event == nil || data == nil || event->GetType() != IEvent::kLButtonDn)
	{
		CTreeViewController::ProcessSelectionRules(event, nodeWidgetUID, notifyOfChange);
		return;
	}
	const NodeID node(data->Get());
	const bool16 modified = (event->ShiftKeyDown() || event->CmdKeyDown()) ? kTrue : kFalse;
	if (modified && !IsSelected(node) && !fSelectedNodes.empty() && !Joins(fSelectedNodes[0], node))
	{
		DeselectAll(kFalse, kTrue);
		(void)Select(node, notifyOfChange);
		NoteRunEnd(node);
		return;
	}
	CTreeViewController::ProcessSelectionRules(event, nodeWidgetUID, notifyOfChange);
	NoteRunEnd(node);		// the run's moving end is the row clicked
}

ITreeViewController::SelectCode KCMStoryTreeController::Select(const NodeID& node, bool16 notifyOfChange,
	bool16 changeHilite)
{
	// NOT WITH THESE ROWS: a row that cannot join the selection is turned away whoever asks - a click, a run of rows, a key.
	// The stock answer for "not with the selection" (eSingleParentOnly); nothing of KCM's reads it.
	if (!IsSelected(node) && !fSelectedNodes.empty() && !Joins(fSelectedNodes[0], node))
		return eSingleParentOnly;
	return CTreeViewController::Select(node, notifyOfChange, changeHilite);
}

//========================================================================================
// What the clicks, the keys and the menus ask (KCMStoryTreeController.h)
//========================================================================================

bool16 KCMStoryGetSelectedChangeRows(std::vector<KCMStoryChangeRow>& out)
{
	out.clear();
	IControlView* const listView = ListView();
	InterfacePtr<ITreeViewController> controller(listView, UseDefaultIID());
	if (controller == nil || KCMListShowsResources())
		return kFalse;
	NodeIDList selected;
	controller->GetSelectedItemsDisplayOrder(selected);
	for (size_t k = 0; k < selected.size(); ++k)
	{
		TreeNodePtr<KCMStoryNodeID> nodeID(selected[k]);
		if (nodeID == nil || !nodeID->IsChangeRow())
		{
			out.clear();
			return kFalse;		// a story row selected: not a selection of change rows
		}
		out.push_back(KCMStoryChangeRow(nodeID->GetRow(), nodeID->GetChange()));
	}
	return out.empty() ? kFalse : kTrue;
}

void KCMStoryRowsOfRightClick(int32 row, int32 change, std::vector<KCMStoryChangeRow>& out)
{
	std::vector<KCMStoryChangeRow> selected;
	if (KCMStoryGetSelectedChangeRows(selected) && selected.size() >= 2)
		for (size_t k = 0; k < selected.size(); ++k)
			if (selected[k].first == row && selected[k].second == change)
			{
				out = selected;		// one of several selected together: all of them
				return;
			}
	out.assign(1, KCMStoryChangeRow(row, change));
}

void KCMStoryFollowModifiedClick(const NodeID& clicked)
{
	TreeNodePtr<KCMStoryNodeID> nodeID(clicked);
	if (nodeID == nil || !nodeID->IsChangeRow() || KCMListShowsResources())
		return;		// a story row with Shift / Ctrl: selected alone, and no jump
	InterfacePtr<ITreeViewController> controller(ListView(), UseDefaultIID());
	if (controller != nil && controller->IsSelected(clicked))
		KCMStoryJumpToChange(nodeID->GetRow(), nodeID->GetChange());	// a row added: the page shows it
}

void KCMStorySelectAloneInView(ITreeViewController* controller, const NodeID& row)
{
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (controller == nil || adapter == nil || treeMgr == nil)
		return;
	// Every row above it open, top down, so the tree draws it (ExpandNode on an open row is a harmless no-op).
	std::vector<NodeID> above;
	for (NodeID up(adapter->GetParentNode(row)); up.IsValid(); up = NodeID(adapter->GetParentNode(up)))
	{
		TreeNodePtr<KCMStoryNodeID> upID(up);
		if (upID == nil || upID->IsRoot())
			break;		// the hidden root is never drawn
		above.push_back(up);
	}
	for (std::vector<NodeID>::reverse_iterator it = above.rbegin(); it != above.rend(); ++it)
		treeMgr->ExpandNode(*it, kFalse /*expandAllDescendants*/);
	// THE SELECTION BEFORE IT OFF FIRST: Select does not move a selection - in this list it adds to it. The deselect tells
	// nobody; only the row that ends selected is announced (KFC's SelectAloneInView).
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	(void)controller->Select(row, kTrue /*notifyOfChange*/, kTrue /*changeHilite*/);
	treeMgr->ScrollToNode(row, ITreeViewMgr::eScrollIntoView);
	NoteRunEnd(row);
}

void KCMStoryReselectRows(ITreeViewController* controller, const std::vector<KCMStoryChangeRow>& rows)
{
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (controller == nil || treeMgr == nil || rows.empty())
		return;
	// The deselect tells nobody; the rows are announced once, by the last (KFC's SelectAllRows' rule).
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	for (size_t k = 0; k < rows.size(); ++k)
		(void)controller->Select(KCMStoryNodeID::CreateChange(rows[k].first, rows[k].second),
								 (k + 1 == rows.size()) ? kTrue : kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	const NodeID last(KCMStoryNodeID::CreateChange(rows.back().first, rows.back().second));
	treeMgr->ScrollToNode(last, ITreeViewMgr::eScrollIntoView);
	NoteRunEnd(last);
}

bool16 KCMStoryExtendSelection(ITreeViewController* controller, bool16 down)
{
	if (controller == nil || KCMListShowsResources())
		return kFalse;
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	if (selected.empty())
		return kFalse;
	// Where the run began - the stock controller's master item (the first row selected into an empty selection).
	NodeID anchor(controller->GetMasterItem());
	if (!anchor.IsValid() || !controller->IsSelected(anchor))
		anchor = selected[0];
	TreeNodePtr<KCMStoryNodeID> anchorID(anchor);
	if (anchorID == nil || !anchorID->IsChangeRow())
		return kFalse;		// Shift+arrow on a story row: not a run of change rows
	// The moving end: where the last click or Shift+arrow left it, while it is still selected - else the anchor.
	NodeID end(anchor);
	if (gRunEndRow >= 0 && gRunEndChange >= 0)
	{
		const NodeID remembered(KCMStoryNodeID::CreateChange(gRunEndRow, gRunEndChange));
		if (controller->IsSelected(remembered))
			end = remembered;
	}
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (treeMgr == nil)
		return kFalse;
	// The next CHANGE row the end can move to - story rows passed over, so a run goes on into the next story's rows. The
	// rows drawn are the ones walked: a closed story's are not.
	TreeNodeTraverser walk(end, treeMgr, kTrue /*expandedNodesOnly*/,
		down ? TreeNodeTraverser::eForward : TreeNodeTraverser::eReverse);
	NodeID next;
	while (!walk.Completed())
	{
		next = walk.Next();
		TreeNodePtr<KCMStoryNodeID> nextID(next);
		if (nextID != nil && nextID->IsChangeRow())
			break;
		next = NodeID();
	}
	if (!next.IsValid())
		return kTrue;		// the list's end: nothing to add
	SelectRun(controller, anchor, next);
	NoteRunEnd(next);
	treeMgr->ScrollToNode(next, ITreeViewMgr::eScrollIntoView);
	// The page shows the run's moving end, grown or shrunk - its landing, as a click makes it.
	TreeNodePtr<KCMStoryNodeID> nextID(next);
	if (nextID != nil)
		KCMStoryJumpToChange(nextID->GetRow(), nextID->GetChange());
	return kTrue;
}

bool16 KCMStoryTakeKeyboard()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (app == nil)
		return kFalse;
	// 1. The panel made the active one with the key focus given to it - "give key focus to panel" (IPanelMgr.h). A panel
	//    that is closed is not opened for this: the user closed it.
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil || !panelMgr->IsPanelWithWidgetIDShown(kKCMPanelWidgetID))
		return kFalse;
	panelMgr->ShowPanelByWidgetID(kKCMPanelWidgetID, kTrue);
	// 2. The panel holding the list gives it the keyboard - SetKeyboardFocus wants "one of its children"
	//    (IPanelControlData.h), so the list's own parent panel (Adobe's Links panel does the same for its caption rows).
	IControlView* const listView = ListView();
	if (listView == nil)
		return kFalse;
	InterfacePtr<const IWidgetParent> listParent(listView, UseDefaultIID());
	InterfacePtr<IPanelControlData> holder(listParent != nil
		? static_cast<IPanelControlData*>(listParent->QueryParentFor(IID_IPANELCONTROLDATA)) : nil);
	if (holder != nil)
		holder->SetKeyboardFocus(listView);
	// 3. Checked - and taken by IKeyBoard if the panel's doors did not land it there. ★The Query of the list's
	//    IID_IEVENTHANDLER is also what brings KCMStoryTreeEH into being (implementations are made on first use).
	InterfacePtr<IEventHandler> listEH(listView, UseDefaultIID());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (listEH == nil || keyBoard == nil)
		return kFalse;
	if (keyBoard->GetKeyFocus() != listEH)
		keyBoard->AcquireKeyFocus(listEH);
	return (keyBoard->GetKeyFocus() == listEH) ? kTrue : kFalse;
}

bool16 KCMStoryListHoldsKeyboard()
{
	IControlView* const listView = ListView();
	if (listView == nil)
		return kFalse;
	InterfacePtr<IEventHandler> listEH(listView, UseDefaultIID());
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (listEH == nil || keyBoard == nil || keyBoard->GetKeyFocus() != listEH)
		return kFalse;
	return KCMPanelHasSystemKeyboard();
}

// End, KCMStoryTreeController.cpp.
