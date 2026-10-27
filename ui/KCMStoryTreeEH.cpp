//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Event handler for the Story Edits list ITSELF (the tree-view boss, not a row). What it adds to the stock keys:
//
//    1. THE ROW A KEY LANDS ON IS SHOWN, exactly as a click on that row would show it - the up / down arrows (2026-08-11,
//       "like KBS, I want to move up and down with the cursor"), and since 2026-10-24 night Home / End / PageUp /
//       PageDown too (KFC's: moving the selection alone left the page on the row before, and the next Return wrote a
//       row it did not show).
//    2. SHIFT+UP / SHIFT+DOWN GROW OR SHRINK A RUN OF CHANGE ROWS selected together, the page following
//       (KCMStoryExtendSelection - 2026-10-24 night, design 1-3).
//    3. RETURN / ENTER ON THE CHANGE ROWS SELECTED APPLIES THEM, against an import's copy - one undo step for several
//       (IKCMStoryEditsFacade::ApplyChanges); SHIFT+RETURN applies and lands on the next row below that can be applied; a
//       Return held down applies once (2026-10-24 night, design 1-4 - the author: "like KFC, change with Return"). ⚠The
//       Story mode takes no Return (design 1-8: there it would be "put the Source back", and a stray press would take
//       the reader's edit away) - ★and since 2026-10-27 a Return that acts on nothing (that mode, a story row, nothing
//       armed) is taken whole and does nothing, so the list keeps the keyboard (TakeReturn says what passing it on cost).
//       The Return is taken by a FILTER on the application's event dispatcher while the list
//       holds the keyboard (KCMReturnFilterEH, below) - a Return handled by the list's own KeyDown is followed by the
//       widget layer handing the keyboard back to the document, and the next arrow then moves the page item selected in
//       the layout (memory panel-return-gives-keyboard-to-document; KFC measured it, 2026-10-07).
//
//  *** THE PRICE OF HOLDING THE KEYBOARD (2026-08-11, still real) ***
//
//  This panel's job is to send the user into the document, and while the list holds the keyboard the tool shortcuts do
//  not reach it. Accepted in exchange for the walk; a click in the document hands the keyboard back to it (a DOUBLE
//  click on a row did too, until 2026-10-25 - KCMStoryRowEH.cpp).
//
//  *** WHERE THE MOVEMENT COMES FROM ***
//
//  Not from here. The stock handler moves the selection and this class only asks where it ended up: it knows which rows
//  are on screen and how the selection scrolls, and it can only ever land on a row the tree really has (KBS deleted its
//  own arithmetic after it disagreed - KBSResultTreeEH.cpp). Shift+Return has no stock move to lean on (it skips rows),
//  so it walks - through the tree's OWN hierarchy adapter, never the model's counts.
//
//  ! THIS CLASS ONLY EXISTS IF SOMETHING ASKS FOR IT. Implementations are created on first QueryInterface; the keyboard
//    hand-off (KCMStoryTakeKeyboard, after every click) is what brings it into being.
//
//  Written from KFC's KFCResultTreeEH.cpp (2026-10-24 night).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IEvent.h"
#include "IEventDispatcher.h"		// Push / Remove - the Return filter on the application's stack (KCMReturnFilterEH)
#include "IEventHandler.h"
#include "IEventUtils.h"			// RemoveNextKeyCmd - a Return taken whole (TakeReturn)
#include "IKeyBoard.h"				// taking the key focus back after a landing
#include "ISession.h"
#include "ITreeViewController.h"
#include "ITreeViewHierarchyAdapter.h"	// the tree's own order - the row Shift+Return goes on to
#include "ITreeViewMgr.h"

// General includes:
#include "CEventHandler.h"			// the Return filter's base: every event passed on unless it is taken
#include "CreateObject.h"			// ::CreateObject2 - the Return filter's boss
#include "keyboarddefs.h"			// the arrows, Home / End, PageUp / PageDown, Return / Enter (kVirtual...Key)
#include "Utils.h"
#include "KCMStoryNodeID.h"		// our node class: (row, change). Was ListIndexNodeID until 2026-08-20

#include <chrono>					// a Return's hold on the keyboard - how long at most (gHolding)
#include <vector>

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KCMStoryRowEH.cpp's TreeNodeEventHandler.
#include "../../open/includes/widgets/TreeViewEventHandler.h"	// stock base for a tree widget

// Project includes:
#include "KCMUIID.h"
#include "KCMStoryJump.h"
#include "KCMStoryTreeController.h"	// the change rows selected, the run, the keyboard hand-off (2026-10-24 night)
#include "KCMUIShared.h"			// KCMSetStatus - what a Return did
#include "IKCMStoryEditsFacade.h"	// IsImportSource / CanApplyChange / ApplyChanges - Return
#include "IKCMCompareFacade.h"		// IsArmed

namespace
{

// One walk at a time. A landing scrolls two document windows and the Pages panel, and a held-down
// arrow key can arrive back here while that is still going on - stepping from a selection the
// previous walk has not finished making. KBS needed this guard for the same reason.
bool gWalking = false;

class WalkGuard
{
public:
	WalkGuard() { gWalking = true; }
	~WalkGuard() { gWalking = false; }
};

// THE KEYBOARD KEPT THROUGH A RETURN (KFC's gHolding): a Return applied must leave the keyboard on the list, so the arrows
// walk on - but InDesign asks the holder to let go straight after the KeyDown and hands the keyboard to the document. So
// a handled Return HOLDS: give-up requests are refused until that Return's own KeyUp arrives, and for kHoldAfterReturnMs
// at most, so a hold whose KeyUp never comes cannot keep refusing a click into the document later.
const int32 kHoldAfterReturnMs = 500;
bool gHolding = false;
std::chrono::steady_clock::time_point gHoldUntil;

void HoldAfterReturn()
{
	gHolding = true;
	gHoldUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHoldAfterReturnMs);
}

bool StillHolding()
{
	if (gHolding && std::chrono::steady_clock::now() > gHoldUntil)
		gHolding = false;
	return gHolding;
}

// RETURN, OR THE KEYPAD'S ENTER: asked the same way at every step - the filter's KeyDown, KeyCmd and KeyUp, and the
// list's own - so the filter never takes a Return's KeyDown and lets its KeyCmd or KeyUp go by.
bool IsReturnKey(const VirtualKey& key)
{
	return key == kVirtualReturnKey || key == kVirtualEnterKey;
}

// The row after `from` in the order the rows are drawn, top to bottom - asked of the tree's own hierarchy adapter: a row's
// first child, else its next sibling, else the next sibling of the nearest row above it that has one. An invalid NodeID
// after the last row (KFC's NextRowInTreeOrder).
NodeID NextRowInTreeOrder(const ITreeViewHierarchyAdapter* adapter, const NodeID& from)
{
	if (adapter->GetNumChildren(from) > 0)
		return NodeID(adapter->GetNthChild(from, 0));
	NodeID node = from;
	for (;;)
	{
		TreeNodePtr<KCMStoryNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsRoot())
			return NodeID();
		const NodeID parent(adapter->GetParentNode(node));
		if (!parent.IsValid())
			return NodeID();
		const int32 next = adapter->GetChildIndex(parent, node) + 1;
		if (next > 0 && next < adapter->GetNumChildren(parent))
			return NodeID(adapter->GetNthChild(parent, next));
		node = parent;
	}
}

// SHIFT+RETURN GOES ON (KFC's: InDesign's own Change/Find, "change, then find the next", for the list): after the rows it
// applied, the next CHANGE row below that can be applied (CanApplyChange - the question the row menu greys Apply by) is
// selected alone, brought into view and landed on. False = no such row below: the selection stays where it is.
bool GoOnToNextApplicableRow(ITreeViewController* controller, const NodeID& from)
{
	InterfacePtr<const ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	if (adapter == nil)
		return false;
	NodeID next = from;
	for (;;)
	{
		next = NextRowInTreeOrder(adapter, next);
		TreeNodePtr<KCMStoryNodeID> nodeID(next);
		if (nodeID == nil)
			return false;
		if (nodeID->IsChangeRow() && Utils<IKCMStoryEditsFacade>()->CanApplyChange(nodeID->GetRow(), nodeID->GetChange()))
		{
			KCMStorySelectAloneInView(controller, next);
			{
				WalkGuard walkGuard;
				KCMStoryJumpToChange(nodeID->GetRow(), nodeID->GetChange());
			}
			(void)KCMStoryTakeKeyboard();
			return true;
		}
	}
}

// RETURN ON THE CHANGE ROWS SELECTED, one place for the two that take it: the Return filter and the list's own KeyDown.
// Return or the keypad's Enter, no Ctrl / Alt, change rows selected, the Source an import's copy: they are applied (ONE
// undo step for several - ApplyChanges), the list keeps the keyboard, and the key is taken whole. With Shift, the same,
// and then - only when something went in - on to the next row below that can be applied. kFalse = not a Return, or one
// with Ctrl / Alt: the caller passes it on. (A Return the list acts on nothing with is taken whole too - below.)
//  - The KeyCmd a Return's character makes next is removed first - the official shape for a list that acts on Return
//    (open/components/spellpanel/SpellListBoxEH.cpp, KeyDown), through IEventUtils.
//  - A REPEAT (a Return held down) is taken and writes nothing (KFC's: one second of Shift+Return was a dozen rows).
//  - Rows of which none can be applied ("=" rows, "!" rows) take the Return all the same and write nothing - a Return
//    passed on would go to the widget layer, which hands the keyboard to the document.
bool16 TakeReturn(IEvent* e, ITreeViewController* controller)
{
	if (e == nil || controller == nil || !IsReturnKey(e->GetVirtualKey()) || e->OptionAltKeyDown() || e->CmdKeyDown())
		return kFalse;
	// ★★A RETURN THAT ACTS ON NOTHING IS TAKEN WHOLE ALL THE SAME (2026-10-27, the re-inspection's U5 - measured): the Story
	//   mode takes no Return (design 1-8), nor does a list with nothing armed, nor a story row - and passed on, the Return
	//   reached the widget layer, which handed the keyboard to the document, where the click had just selected the change's
	//   words: the next letter typed replaced them (S-pc.indd, three characters made "x", an undo step "Typing"). The list
	//   keeps the keyboard, as it does for the import's Return (design 1-4), and nothing is done.
	Utils<IKCMStoryEditsFacade> edits;
	std::vector<KCMStoryChangeRow> rows;
	if (!edits || !Utils<IKCMCompareFacade>()->IsArmed() || !edits->IsImportSource() || !KCMStoryGetSelectedChangeRows(rows))
	{
		Utils<IEventUtils>()->RemoveNextKeyCmd(e);
		HoldAfterReturn();
		return kTrue;
	}
	const bool16 goOn = e->ShiftKeyDown();
	const NodeID last(KCMStoryNodeID::CreateChange(rows.back().first, rows.back().second));
	Utils<IEventUtils>()->RemoveNextKeyCmd(e);
	if (e->IsRepeatKey() || gWalking)
	{
		HoldAfterReturn();
		return kTrue;
	}
	// Only the rows that can be applied - the others are left out before the step, not refused inside it.
	std::vector<int32> applyRows;
	std::vector<int32> applyChanges;
	for (size_t k = 0; k < rows.size(); ++k)
		if (edits->CanApplyChange(rows[k].first, rows[k].second))
		{
			applyRows.push_back(rows[k].first);
			applyChanges.push_back(rows[k].second);
		}
	PMString status;
	status.SetTranslatable(kFalse);
	int32 done = -1;
	if (applyRows.empty())
		status = (rows.size() == 1) ? "nothing to apply on this row" : "nothing to apply on the rows selected";
	else
		done = edits->ApplyChanges(&applyRows[0], &applyChanges[0], static_cast<int32>(applyRows.size()), status);
	// The apply fronted the document and repainted the list - the keyboard taken back, with the panel made the active one.
	(void)KCMStoryTakeKeyboard();
	// Shift: on to the next row that can be applied. ★Otherwise THE ROWS STAY SELECTED (measured 2026-10-24 night, tests-ui
	//   T-R4): the apply compares the rows again and the panel rebuilds the list on the model's notice, which leaves nothing
	//   selected - the Down after a Return then started from nowhere (KCMStoryReselectRows).
	const bool wentOn = (goOn && done > 0) ? GoOnToNextApplicableRow(controller, last) : false;
	if (!wentOn)
		KCMStoryReselectRows(controller, rows);
	if (goOn && done > 0 && !wentOn)
		status.Append(" - no row below can be applied");
	status.SetTranslatable(kFalse);
	KCMSetStatus(status);
	HoldAfterReturn();
	return kTrue;
}

// THE RETURN FILTER (KFC's gReturnFilter): its own boss (kKCMReturnFilterBoss, KCMUI.fr), made once and released at
// shutdown, pushed on the application's IEventDispatcher while the list holds the keyboard - the stack the dispatcher
// offers an event to from the top down before anything below it (IEventDispatcher.h). Adobe's Layers panel pushes a
// handler the same way for the length of a drag (open/components/layerpanel/LayerProxyDragDropSourceEH.cpp).
IEventHandler* gReturnFilter = nil;
bool gFilterPushed = false;
bool gFilterShutDown = false;

IEventDispatcher* QueryDispatcher()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	return (app != nil) ? InterfacePtr<IEventDispatcher>(app, UseDefaultIID()).forget() : nil;
}

void PushReturnFilter()
{
	if (gFilterShutDown || gFilterPushed)
		return;
	if (gReturnFilter == nil)
		gReturnFilter = ::CreateObject2<IEventHandler>(kKCMReturnFilterBoss, IID_IEVENTHANDLER);
	InterfacePtr<IEventDispatcher> dispatcher(QueryDispatcher());
	if (gReturnFilter == nil || dispatcher == nil)
		return;
	dispatcher->Push(gReturnFilter, IEventDispatcher::kPushOnTop);
	gFilterPushed = true;
}

void RemoveReturnFilter()
{
	if (!gFilterPushed)
		return;
	InterfacePtr<IEventDispatcher> dispatcher(QueryDispatcher());
	if (dispatcher != nil)
		dispatcher->Remove(gReturnFilter);
	gFilterPushed = false;
}

}	// anonymous namespace

/** The keys that move the selection land on what they reached; Shift+arrows grow a run; Return applies (see the top). */
class KCMStoryTreeEH : public TreeViewEventHandler
{
public:
	KCMStoryTreeEH(IPMUnknown* boss) : TreeViewEventHandler(boss) {}
	virtual ~KCMStoryTreeEH() {}

	virtual bool16 HandleUpDownKey(IEvent* e, const VirtualKey& key);
	virtual bool16 HandleHomeKey(IEvent* e);
	virtual bool16 HandleEndKey(IEvent* e);
	virtual bool16 HandlePageUpDownKey(IEvent* e, const VirtualKey& key);
	virtual bool16 KeyDown(IEvent* e);
	virtual bool16 KeyUp(IEvent* e);					// ends a Return's hold (gHolding)
	virtual bool16 WillingToGiveUpKeyFocus();		// refuses while a Return holds
	virtual void PostGetKeyFocus();					// pushes the Return filter
	virtual void PostGiveUpKeyFocus();				// takes it off again

private:
	// The stock move for `key`, then the landing on the row it reached - one place for the arrows, Home / End and
	// PageUp / PageDown.
	bool16 StockMoveThenLand(IEvent* e, const VirtualKey& key);
};

CREATE_PMINTERFACE(KCMStoryTreeEH, kKCMStoryTreeEHImpl)

void KCMStoryTreeEH::PostGetKeyFocus()
{
	TreeViewEventHandler::PostGetKeyFocus();
	PushReturnFilter();
}

void KCMStoryTreeEH::PostGiveUpKeyFocus()
{
	TreeViewEventHandler::PostGiveUpKeyFocus();
	RemoveReturnFilter();
}

bool16 KCMStoryTreeEH::WillingToGiveUpKeyFocus()
{
	if (StillHolding())
		return kFalse;		// a Return holds (gHolding): InDesign's own request straight after the KeyDown is refused
	return TreeViewEventHandler::WillingToGiveUpKeyFocus();
}

bool16 KCMStoryTreeEH::KeyUp(IEvent* e)
{
	if (e != nil && IsReturnKey(e->GetVirtualKey()))
		gHolding = false;
	return TreeViewEventHandler::KeyUp(e);
}

bool16 KCMStoryTreeEH::KeyDown(IEvent* e)
{
	gHolding = false;		// any key ends a previous Return's hold; this one may start a new one (TakeReturn)
	// THE RETURN, WHEN THE FILTER DID NOT GET IT FIRST (the filter off the stack): the same door, with only the hold to keep
	// the keyboard.
	InterfacePtr<ITreeViewController> controller(this, UseDefaultIID());
	if (TakeReturn(e, controller))
		return kTrue;
	return TreeViewEventHandler::KeyDown(e);
}

bool16 KCMStoryTreeEH::HandleUpDownKey(IEvent* e, const VirtualKey& key)
{
	if (!(key == kVirtualDownArrowKey) && !(key == kVirtualUpArrowKey))
		return TreeViewEventHandler::HandleUpDownKey(e, key);

	// A previous landing is still moving views - see gWalking. Swallow the key rather than stepping
	// from a half-made selection.
	if (gWalking)
		return kTrue;
	WalkGuard walkGuard;

	// ★SHIFT+DOWN / SHIFT+UP (2026-10-24 night, KFC's): the run of change rows grown or shrunk by one row, the page
	//   following (KCMStoryExtendSelection) - not the stock's own Shift move, whose rules know nothing of the story rows
	//   between. On a story row it is the stock key's.
	if (e != nil && e->ShiftKeyDown() && !e->CmdKeyDown() && !e->OptionAltKeyDown())
	{
		InterfacePtr<ITreeViewController> runController(this, UseDefaultIID());
		if (KCMStoryExtendSelection(runController, (key == kVirtualDownArrowKey) ? kTrue : kFalse))
		{
			(void)KCMStoryTakeKeyboard();	// the page's follow fronted the document, which took the keyboard
			return kTrue;
		}
	}
	return this->StockMoveThenLand(e, key);
}

// HOME / END / PAGEUP / PAGEDOWN LAND AS THE ARROWS DO (2026-10-24 night, KFC's): the stock moves, and the row it reached
// is landed on - only when the selection moved: PageUp / PageDown scroll the list and leave the selection where it is
// (KFC measured it), and a landing on the row left selected would run its action again for a scroll.
bool16 KCMStoryTreeEH::HandleHomeKey(IEvent* e)
{
	if (gWalking)
		return kTrue;
	WalkGuard walkGuard;
	return this->StockMoveThenLand(e, kVirtualHomeKey);
}

bool16 KCMStoryTreeEH::HandleEndKey(IEvent* e)
{
	if (gWalking)
		return kTrue;
	WalkGuard walkGuard;
	return this->StockMoveThenLand(e, kVirtualEndKey);
}

bool16 KCMStoryTreeEH::HandlePageUpDownKey(IEvent* e, const VirtualKey& key)
{
	if (!(key == kVirtualPageUpKey) && !(key == kVirtualPageDownKey))
		return TreeViewEventHandler::HandlePageUpDownKey(e, key);
	if (gWalking)
		return kTrue;
	WalkGuard walkGuard;
	return this->StockMoveThenLand(e, key);
}

bool16 KCMStoryTreeEH::StockMoveThenLand(IEvent* e, const VirtualKey& key)
{
	InterfacePtr<ITreeViewController> controller(this, UseDefaultIID());

	// Home / End / PageUp / PageDown land only where they moved the selection: the selection before. The arrows land
	// wherever they leave one row selected, as they always have.
	const bool arrow = key == kVirtualDownArrowKey || key == kVirtualUpArrowKey;
	NodeIDList before;
	if (controller != nil)
		controller->GetSelectedItems(before);

	// The stock handler owns the movement (see the note at the top).
	bool16 handled = kFalse;
	if (key == kVirtualHomeKey)
		handled = TreeViewEventHandler::HandleHomeKey(e);
	else if (key == kVirtualEndKey)
		handled = TreeViewEventHandler::HandleEndKey(e);
	else if (key == kVirtualPageUpKey || key == kVirtualPageDownKey)
		handled = TreeViewEventHandler::HandlePageUpDownKey(e, key);
	else
		handled = TreeViewEventHandler::HandleUpDownKey(e, key);
	if (controller == nil)
		return handled;

	// Where it landed. A plain move leaves one row selected, so anything else means the move did not happen (an empty
	// list, or already at the end) and there is nothing to show.
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	if (selected.size() != 1)
		return handled;
	if (!arrow && before.size() == 1 && before[0] == selected[0])
		return handled;		// the selection did not move (a PageUp / PageDown's scroll, Home on the first row)

	TreeNodePtr<KCMStoryNodeID> node(selected[0]);
	if (node == nil || node->IsRoot() || node->IsAuthorRow())
		return handled;

	// The row's action - the same one a click on it would run. It reports its own refusals to the status line, and answers
	// kFalse quietly for the "No edits" placeholder row.
	// ★★★WALKING ONTO A CHANGE ROW GOES TO THE EDIT (2026-08-20) - and since 2026-10-24 night selects its words, as a
	//   click does (KCMStoryJumpToChange: KFC's landing rules).
	if (node->IsChangeRow())
		KCMStoryJumpToChange(node->GetRow(), node->GetChange());
	else
		KCMStoryJumpToRow(node->GetRow());

	// ***** TAKE THE KEYBOARD BACK. ***** That jump activated a document window, which took the key focus with it; without
	// this, the NEXT key would go to the document and the walk would stop after one step - and since 2026-10-24 night it is
	// taken back with the panel made the active one again (KCMStoryTakeKeyboard - a list holding InDesign's keyboard
	// inside a panel that is no longer active lost the next Return to the document, KFC measured).
	(void)KCMStoryTakeKeyboard();
	return kTrue;
}

/** THE RETURN FILTER - see gReturnFilter. Takes one thing: a Return or Enter on the change rows selected while the Story
	Edits list holds the keyboard - and that Return's KeyCmd and KeyUp with it, so no part of it reaches the widget layer,
	whose DVEventHandler gives the keyboard back to the document after a key it delivered (RestoreFocusOnDocument).
	Everything else is passed on untouched: CEventHandler answers kFalse to every event. */
class KCMReturnFilterEH : public CEventHandler
{
public:
	KCMReturnFilterEH(IPMUnknown* boss) : CEventHandler(boss), fTook(false) {}
	virtual ~KCMReturnFilterEH() {}

	virtual bool16 KeyDown(IEvent* e);
	virtual bool16 KeyCmd(IEvent* e);
	virtual bool16 KeyUp(IEvent* e);

private:
	bool		fTook;		// this filter took the key now going through - its KeyCmd and KeyUp are taken too
	VirtualKey	fTookKey;	// ...that key
};

CREATE_PMINTERFACE(KCMReturnFilterEH, kKCMReturnFilterEHImpl)

bool16 KCMReturnFilterEH::KeyDown(IEvent* e)
{
	fTook = false;
	// Every key the application dispatches passes here while the filter is on the stack, so the cheap question first.
	if (e == nil || !IsReturnKey(e->GetVirtualKey()))
		return kFalse;
	// Only while the list holds the keyboard - InDesign's holder AND the window the system sends the keys to
	// (KCMStoryListHoldsKeyboard): a Return in a document or a dialog is not ours.
	if (!KCMStoryListHoldsKeyboard())
		return kFalse;
	InterfacePtr<ITreeViewController> controller(KCMFindPanelWidget(kKCMStoryTreeWidgetID), UseDefaultIID());
	if (!TakeReturn(e, controller))
		return kFalse;
	fTook = true;
	fTookKey = e->GetVirtualKey();
	return kTrue;
}

bool16 KCMReturnFilterEH::KeyCmd(IEvent* e)
{
	return (fTook && e != nil && e->GetVirtualKey() == fTookKey) ? kTrue : kFalse;
}

bool16 KCMReturnFilterEH::KeyUp(IEvent* e)
{
	if (!fTook || e == nil || !(e->GetVirtualKey() == fTookKey))
		return kFalse;
	fTook = false;
	gHolding = false;		// the Return is over; the keyboard is the user's to move again (gHolding)
	return kTrue;
}

//----------------------------------------------------------------------------------------
// KCMStoryShutdownReturnFilter (declared in KCMStoryTreeController.h)
//----------------------------------------------------------------------------------------
void KCMStoryShutdownReturnFilter()
{
	// Off the stack and let go, and nothing pushed again: a handler left on the application's dispatcher when the .pln
	// unloads would be called into nothing.
	gFilterShutDown = true;
	RemoveReturnFilter();
	if (gReturnFilter != nil)
	{
		gReturnFilter->Release();
		gReturnFilter = nil;
	}
}

// End, KCMStoryTreeEH.cpp.
