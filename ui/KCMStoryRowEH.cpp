//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Which click on a Story Edits row means what. What each one DOES is KCMStoryJump.cpp.
//
//  The shape is the layer panel's row handler (LayerTreeRowPanelEH::LButtonUp), by way of KBS's
//  result rows: act on the button coming UP, only when the base handler did not claim the event,
//  only without Shift / Cmd, and only for the row that ended up selected. What each buys:
//    * UP, not DOWN  - a press that turns into a drag, or that the user rolls off before letting
//                      go, is not a request to go anywhere.
//    * !result       - the base returns kTrue for what it handled itself. Jumping on top of that
//                      would be a second action out of one click.
//    * no Shift/Cmd  - those are selection modifiers, not "take me there".
//    * IsSelected    - the row the click actually landed on (the press already set the selection).
//
//  ★THE KEYBOARD IS TAKEN, AND IT USED NOT TO BE. Until 2026-08-11 this file left the key focus
//  alone on purpose (user's call, 2026-08-10): this panel's job is to send the user INTO the
//  document, and a panel holding the keyboard is a panel where the tool shortcuts have quietly
//  stopped working. That was reversed at the user's request - "like KBS, I want to move up and down
//  with the cursor" - so a click now hands the focus to the list (the foot of LButtonUp), and a
//  click in the document hands it back. The cost is still real and is still the cost described above;
//  it was accepted in exchange for being able to walk the list.
//  ⚠This paragraph said "KCM deliberately does not" until 2026-08-18 (bug recheck B-U4), while
//  the code 180 lines below had already been doing the opposite for a week and said so.
//
//  ★★A DOUBLE CLICK IS TWO CLICKS (2026-10-25 - the author: "remove the double click's behaviour", "the way KFC
//   does it"; KFC's KFCResultNodeEH tells no double click apart): each button-up jumps again, and the keyboard stays
//   on the list. Since 2026-10-24 night one click selects a change's words, so what the double click still added was
//   the keyboard handed to the document - and, on a story row, the whole story selected.
//   (⛔Until then ButtonDblClk raised a flag that the trailing button-up read - a double click arrives as LButtonDn,
//   LButtonUp, ButtonDblClk, LButtonUp, so the first up had already jumped - and LButtonDn cleared it.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"			// the route from the application to the menu manager
#include "IApplication.h"			// the app boss carries IKeyBoard
#include "IEvent.h"					// ShiftKeyDown / CmdKeyDown, GlobalWhere
#include "IEventHandler.h"			// the LIST's handler - the arrow keys' owner (see the hand-off below)
#include "IKeyBoard.h"				// AcquireKeyFocus - the hand-off's fallback
#include "IMenuManager.h"			// HandlePopupMenu - pops kKCMStoryRowMenuName at the cursor
#include "ISession.h"				// GetExecutionContextSession - the route to the application
#include "ITreeNodeIDData.h"		// this node's NodeID
#include "ITreeViewController.h"	// IsSelected - is this the row the click landed on?
#include "IWidgetParent.h"			// QueryParentFor - the row -> the tree that owns the selection

// General includes:
#include "KCMStoryNodeID.h"		// our node class: (row, change). Was ListIndexNodeID until 2026-08-20
#include "IKCMStoryEditsFacade.h"	// IsImportSource / GetChange - which menu a change row pops (2026-10-24 evening)
#include "Utils.h"

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KCMStoryTreeWidgetMgr.cpp's
// DVPublicUtilities.h: the build files that would carry such a directory live outside this
// plug-in's repository, so a path added there would not survive a fresh checkout.
// ⚠KBS includes this one by name alone; its project carries the directory. KCM's does not, and
//   the difference is deliberate rather than an oversight (measured 2026-08-10: C1083).
#include "../../open/includes/widgets/TreeNodeEventHandler.h"	// stock base for a tree row

// Project includes:
#include "KCMUIID.h"
#include "KCMStoryJump.h"
#include "KCMStoryRefresh.h"		// where the right-click menu's row is stashed for the action to read
// (⛔KCMStoryCopy.h - where a CHANGE row's menu stashed the row AND the change - went on
//  2026-09-21 with the last item that read it, the Resources mode's "Edit...". Nothing here has a
//  second stash to write, because a change row no longer raises a menu to write one for.)
#include "KCMStoryTreeController.h"	// FollowModifiedClick / TakeKeyboard - rows selected together (2026-10-24 night)

class KCMStoryRowEH : public TreeNodeEventHandler
{
public:
	KCMStoryRowEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KCMStoryRowEH() {}

	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 RButtonDn(IEvent* e);

private:
	/** Which story row this widget IS, read from its own node and nothing else.

		★NOT THE SAME QUESTION AS RowForClick, which is why it is a separate one. That one answers
		"which row should this CLICK act on" and refuses every row that is not selected - correctly,
		for a left click, because the press has just moved the selection there. A RIGHT click
		deliberately does NOT move the selection, so it has no such row to point at: what it acts on
		is the row the cursor is over, which is this widget. (2026-08-21)

		@param outChange [out] optional. Receives which CHANGE this row names, or -1 for a story row.
		@return the story row's index, or -1 when this widget carries no node of ours.
	*/
	int32 RowFromNode(int32* outChange = nil) const;

	/** ★The Track mode's AUTHOR row this widget is (2026-10-25 - its own menu: accept and reject all of that author's
		changes): the author's index, or -1 for every other row. An author row's node names no story row (RowFromNode
		answers -1 for it), so a right click asks this first. */
	int32 AuthorFromNode() const;

	/** Which row this click is for, or -1 when it should be left alone.

		Answers -1 for a click the stock handler already used, for a modified click, and for a row
		that is not the selected one - so a caller gets one test instead of four.
	*/
	/** @param outChange [out] optional. Receives which CHANGE the clicked row names, or -1 when it
		is a story row. Only meaningful when the return value is >= 0. Added 2026-08-20 with the
		second level: the two kinds of row do different things on a click, and the node knows which
		it is - asking it twice would be asking the same question in two places. */
	int32 RowForClick(IEvent* e, bool16 baseHandled, int32* outChange = nil) const;
};

CREATE_PMINTERFACE(KCMStoryRowEH, kKCMStoryRowEHImpl)

int32 KCMStoryRowEH::RowFromNode(int32* outChange) const
{
	if (outChange != nil)
		*outChange = -1;

	// The node's NodeID lives on this boss's ITreeNodeIDData (every tree node widget carries it).
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return -1;
	TreeNodePtr<KCMStoryNodeID> nodeID(nodeData->Get());
	if (nodeID == nil)
		return -1;

	// ★A CHANGE ROW ANSWERS ITS STORY'S index (2026-08-20), and says which change it was through
	//   outChange. Everything that asks "which row is this" wants the story either way - a change
	//   row belongs to exactly one - and the caller that cares about the difference now has it
	//   without going back to the node.
	if (outChange != nil && nodeID->IsChangeRow())
		*outChange = nodeID->GetChange();
	return nodeID->GetRow();
}

int32 KCMStoryRowEH::AuthorFromNode() const
{
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return -1;
	TreeNodePtr<KCMStoryNodeID> nodeID(nodeData->Get());
	return (nodeID != nil && nodeID->IsAuthorRow()) ? nodeID->GetAuthor() : -1;
}

int32 KCMStoryRowEH::RowForClick(IEvent* e, bool16 baseHandled, int32* outChange) const
{
	if (outChange != nil)
		*outChange = -1;

	if (baseHandled || e == nil || e->ShiftKeyDown() || e->CmdKeyDown())
		return -1;

	// This row's node again (RowFromNode says where it lives) - wanted here to ask the tree
	// whether this is the row the click landed on.
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

	// WHICH row this is, and which change, is the node's own answer and is given in one place
	// (RowFromNode). Everything above was about the CLICK; nothing above it changes the answer.
	return this->RowFromNode(outChange);
}

// Nothing of this plug-in's own happens on the way DOWN, nor on a double click (the note at the head of this file):
// every button-up is a click.
bool16 KCMStoryRowEH::LButtonUp(IEvent* e)
{
	// Let the stock handler finish the click first (selection, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);

	// ★SHIFT / CTRL - ROWS SELECTED TOGETHER (2026-10-24 night - design 1-3, KFC's KFCResultNodeEH): the press has made
	//   the selection (KCMStoryTreeController); the page follows it - the row ADDED landed on, as a click lands (the page
	//   shows the row selected last), a row taken away leaving the page as it is. A story row so clicked is selected alone,
	//   with no jump. The keyboard to the list after it, as after every click, so Shift+Down goes on from here.
	if (!result && e != nil && (e->ShiftKeyDown() || e->CmdKeyDown()))
	{
		InterfacePtr<ITreeNodeIDData> clickedData(this, UseDefaultIID());
		if (clickedData != nil)
			KCMStoryFollowModifiedClick(clickedData->Get());
		(void)KCMStoryTakeKeyboard();
		return result;
	}

	int32 changeIndex = -1;
	const int32 rowIndex = this->RowForClick(e, result, &changeIndex);
	if (rowIndex < 0)
		return result;

	// The tree owns the selection, and from here on it owns the keyboard too - both are asked for
	// through the parent. (RowForClick has already used this route, but it does not hand the
	// interface back, and a second Query costs nothing worth avoiding.)
	InterfacePtr<const IWidgetParent> widgetParent(this, UseDefaultIID());
	InterfacePtr<ITreeViewController> treeController(widgetParent == nil ? nil :
		static_cast<ITreeViewController*>(widgetParent->QueryParentFor(ITreeViewController::kDefaultIID)));
	InterfacePtr<IEventHandler> treeEH(treeController, UseDefaultIID());
	// The session is nil while the app is tearing down - this plug-in's standing rule is to ask
	// before dereferencing it, and every other call site here does (2026-08-11, block 15 audit C-1:
	// these two event handlers were the only ones that did not; KCMStorySection.cpp asks in both
	// of the places it needs the session, KeepPanelOnScreen and ResizePanelByDelta).
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());

	// ★The jump, on every button-up: a double click's second click is a click like the first (the note at the head of
	//  this file - KCMStorySelectChange and KCMStorySelectWholeStory went with the double click on 2026-10-25).
	if (changeIndex >= 0)
	{
		// ★A CHANGE ROW: go to the edit itself and select the words it names (2026-08-20; the selection again since
		//   2026-10-24 night). The story row's jump goes to the top of the story; this one goes to the place inside it.
		KCMStoryJumpToChange(rowIndex, changeIndex);
	}
	else
	{
		KCMStoryJumpToRow(rowIndex);
	}

	// ***** HAND THE KEYBOARD TO THE LIST, so the up / down arrows walk it from here on. *****
	// (KCMStoryTreeEH, added 2026-08-11 at the user's request - "like KBS, move up and down with
	// the cursor".) ⚠This is the deliberate decision noted at the head of this file being reversed:
	// while the tree holds the keyboard, the tool shortcuts do not reach the document. A click in the
	// document is the way back (the double click was one too, until 2026-10-25).
	// ★The QUERY of treeEH above is not incidental - it BRINGS KCMStoryTreeEH INTO EXISTENCE.
	//   Implementations are created on first use and nothing else in this plug-in ever asks the tree
	//   for its event handler, so without it the class is never constructed and the arrows keep the
	//   stock behaviour ([[lazy-interface-instantiation]]; KBS measured exactly this).
	// ★★THROUGH THE PANEL'S DOORS SINCE 2026-10-24 NIGHT (KCMStoryTakeKeyboard - KFC's TakeKeyboard; design 6-2, the
	//   10-10 item): the click now SELECTS the change's words and makes the Target the active document, and a list holding
	//   InDesign's keyboard inside a panel that is no longer the active one lost the next key to the document (KFC
	//   measured it after a click on another window's title bar). IKeyBoard alone stays as the fallback.
	if (!KCMStoryTakeKeyboard() && treeEH != nil && keyBoard != nil && keyBoard->GetKeyFocus() != treeEH)
		keyBoard->AcquireKeyFocus(treeEH);

	return result;
}

// Right-click on a row: pop the row menu at the cursor (2026-08-21). The same machinery as the
// chapter rows in the book comparison dialog (KCMBookRowEH::RButtonDn), the real Links / Layers
// panel row menus, and KBS's result rows: HandlePopupMenu pops the MenuDef subtree named
// kKCMStoryRowMenuName, and the item the user picks fires through the ordinary action component.
//
// ***** THE ROW IS STASHED FIRST. ***** The action is handed no widget context of its own - it runs
// later, from the menu, knowing only its ActionID - so KCMStorySetMenuRow is how it learns which
// story the menu was about. Both the action and its enabling test read it back.
//
// ***** A CHANGE ROW POPS ITS OWN MENU (kKCMChangeRowMenuName). ***** It had one from 2026-09-12,
// carrying only items about the CHANGE under the cursor; the last of them ("Edit...") went on
// 2026-09-21 and the subtree with it, and from then until 2026-09-24 a child row raised nothing.
// ★It came back for "Reject This Import Change" (stage 2 A - retired 2026-10-25), and carries Restore from
// Source, Match the Source and, against an import's copy, Apply This Change / Restore from Copy now - each
// acting on that one change only - so the decision of 2026-08-21 still holds: a reader pointing at ONE
// difference is never handed an action over the whole story, and the story row's menu is never popped on
// a child row.
//
// Deliberately NOT calling the stock handler and NOT changing the selection: a right click that is
// only asking for a menu should not move the user's place in the list - the same rule the chapter
// rows and KBS both settled on. ⚠It is also what makes the row under the cursor the ONLY thing this
// can be about, which is why the row is read from this widget's own node.
// ★★**THE MENU SERVES BOTH MODES** (2026-09-09). It was closed to the Resources mode for a few
// hours, because every item on it then acted on a STORY and a definition row carries the same kind
// of index - the menu would have come up looking right and acted on whatever story sat at that
// number. It is open again now that the menu HAS an item for definitions ("Show as XML"), and what
// keeps the two apart is not this line but their enabling: each is live in one mode only, and a
// greyed item does not appear at all. ⇒ In the Pixel mode both are greyed and no menu appears,
// which is what a right click there did before any of this existed.
bool16 KCMStoryRowEH::RButtonDn(IEvent* e)
{
	int32 changeIndex = -1;
	const int32 rowIndex = this->RowFromNode(&changeIndex);
	const int32 authorIndex = this->AuthorFromNode();
	if ((rowIndex < 0 && authorIndex < 0) || e == nil)
		return TreeNodeEventHandler::RButtonDn(e);

	// ★★A CHANGE ROW HAS ITS OWN MENU (2026-09-24): its items are each about the change under the cursor only -
	//   so the rule of 2026-08-21 (never the STORY row's menu on a child row, never an action over the whole
	//   story) still holds - and a menu with only greyed items does not appear.
	// ★★AND A TRACK AUTHOR ROW HAS ITS OWN (2026-10-25): all of that author's changes, accepted or rejected.
	const bool16 isChangeRow = (changeIndex >= 0);
	if (authorIndex >= 0)
		KCMStorySetMenuAuthor(authorIndex);
	else if (isChangeRow)
		KCMStorySetMenuChange(rowIndex, changeIndex);
	else
		KCMStorySetMenuRow(rowIndex);	// for the action and its enabling test to read (see above)

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

	// ★The author row's two items, grey kept (the author's "grey is fine when it cannot be done" - the import's rows' rule).
	if (authorIndex >= 0)
	{
		menuMgr->HandlePopupMenu(kKCMTrackAuthorMenuName, e->GlobalWhere(), e->GlobalWhere(),
								 kFalse /*removeDisabledItems*/, this);
		return kTrue;
	}

	// ★★AGAINST AN IMPORT'S COPY A CHANGE ROW POPS THE IMPORT'S OWN MENU, GREY ITEMS KEPT (2026-10-24 evening - the author:
	//   "there are rows I cannot right-click"; a "=" row's items were all grey, and the fourth argument kTrue drops grey
	//   items, so the menu did not open at all - KFC found the same argument behind its SET-11, and the author chose grey
	//   there: "grey is fine when it cannot be done"). Apply This Change and Restore from Copy alone (kKCMChangeRowImportMenuName),
	//   so the grey line says which of the two this row cannot do. ⚠A "!" row stays without a menu: it holds nothing to put in.
	Utils<IKCMStoryEditsFacade> edits;
	if (isChangeRow && edits && edits->IsImportSource())
	{
		IKCMStoryEditsFacade::Change change;
		if (!edits->GetChange(rowIndex, changeIndex, change) || change.fWhat == IKCMStoryEditsFacade::Change::kWhatRefused)
			return kTrue;
		menuMgr->HandlePopupMenu(kKCMChangeRowImportMenuName, e->GlobalWhere(), e->GlobalWhere(),
								 kFalse /*removeDisabledItems*/, this);
		return kTrue;
	}
	menuMgr->HandlePopupMenu(isChangeRow ? kKCMChangeRowMenuName : kKCMStoryRowMenuName,
							 e->GlobalWhere(), e->GlobalWhere(), kTrue, this);
	return kTrue;
}

// End, KCMStoryRowEH.cpp.
