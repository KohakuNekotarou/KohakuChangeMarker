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
//  with the cursor" - so a click now hands the focus to the list (the foot of LButtonUp) and a
//  DOUBLE click hands it back. The cost is still real and is still the cost described above; it was
//  accepted in exchange for being able to walk the list.
//  ⚠This paragraph said "KCM deliberately does not" until 2026-08-18 (bug recheck B-U4), while
//  the code 180 lines below had already been doing the opposite for a week and said so.
//
//  ***** THE DOUBLE CLICK'S ONE BIT OF STATE. ***** A double click arrives as FOUR events:
//
//      LButtonDn   LButtonUp   ButtonDblClk   LButtonUp
//
//  - so the FIRST up has already jumped by the time the double click is announced, and a SECOND up
//  follows it. Selecting the story inside ButtonDblClk therefore does not work: the trailing up would
//  run the jump all over again. So ButtonDblClk only RAISES A FLAG, and the trailing up reads it
//  and selects the story instead of jumping.
//
//  ! The flag is cleared in LButtonDn, which is what makes it safe: every click begins with a down,
//    so a flag that was set but never consumed cannot survive into the next click and turn an
//    ordinary single click into something else. (KBS learned this the same way -
//    KBSResultNodeEH.cpp:75-93.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"			// the route from the application to the menu manager
#include "IApplication.h"			// the app boss carries IKeyBoard
#include "IEvent.h"					// ShiftKeyDown / CmdKeyDown, GlobalWhere
#include "IEventHandler.h"			// the LIST's handler - the arrow keys' owner (see the hand-off below)
#include "IKeyBoard.h"				// AcquireKeyFocus / RelinquishKeyFocus
#include "IMenuManager.h"			// HandlePopupMenu - pops kKCMStoryRowMenuName at the cursor
#include "ISession.h"				// GetExecutionContextSession - the route to the application
#include "ITreeNodeIDData.h"		// this node's NodeID
#include "ITreeViewController.h"	// IsSelected - is this the row the click landed on?
#include "IWidgetParent.h"			// QueryParentFor - the row -> the tree that owns the selection

// General includes:
#include "KCMStoryNodeID.h"		// our node class: (row, change). Was ListIndexNodeID until 2026-08-20

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

namespace
{

// A file static rather than a member: row widgets are recycled as the list scrolls, so this belongs
// to "the click going on right now" rather than to any one row. One click happens at a time.
bool gSelectOnNextButtonUp = false;

}	// anonymous namespace

class KCMStoryRowEH : public TreeNodeEventHandler
{
public:
	KCMStoryRowEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KCMStoryRowEH() {}

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 ButtonDblClk(IEvent* e);
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

int32 KCMStoryRowEH::RowForClick(IEvent* e, bool16 baseHandled, int32* outChange) const
{
	if (outChange != nil)
		*outChange = -1;

	if (baseHandled || e == nil || e->ShiftKeyDown() || e->CmdKeyDown())
		return -1;

	// The node's NodeID lives on this boss's ITreeNodeIDData (every tree node widget carries it).
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

// Nothing of this plug-in's own happens on the way DOWN. The one job here is to start every click
// with the double-click flag down.
bool16 KCMStoryRowEH::LButtonDn(IEvent* e)
{
	gSelectOnNextButtonUp = false;
	return TreeNodeEventHandler::LButtonDn(e);
}

// The second click of a double click. Only raises the flag - see the note at the head of this file.
bool16 KCMStoryRowEH::ButtonDblClk(IEvent* e)
{
	const bool16 result = TreeNodeEventHandler::ButtonDblClk(e);
	if (this->RowForClick(e, result) >= 0)
		gSelectOnNextButtonUp = true;
	return result;
}

bool16 KCMStoryRowEH::LButtonUp(IEvent* e)
{
	// Consumed here, on the way in, so that every path out of this function leaves it down.
	const bool selectRatherThanJump = gSelectOnNextButtonUp;
	gSelectOnNextButtonUp = false;

	// Let the stock handler finish the click first (selection, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);

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

	// ***** The second click of a double click SELECTS, rather than jumping again. ***** The first
	// click already moved the view there, so repeating it would only redo that. What is added is
	// handing the user the text itself - Type tool, and a selection.
	// ★WHAT gets selected depends on which row it is, and the two answers are not the same size:
	//     * a STORY row  -> the whole story. That is what a row naming a story is a report of.
	//     * a CHANGE row -> just the words that edit names (2026-08-20, user's call). Widening to
	//       the story here would throw away the very thing the reader double-clicked on.
	//   ⚠Until 2026-08-20 a change row's double click did NOTHING, because its SINGLE click already
	//     made this selection. The single click is a mark now, so the selection moved here.
	if (selectRatherThanJump)
	{
		const bool16 selected = (changeIndex >= 0)
								? KCMStorySelectChange(rowIndex, changeIndex)
								: KCMStorySelectWholeStory(rowIndex);
		if (selected)
		{
			// ***** AND GIVE THE KEYBOARD BACK. ***** The FIRST click of this double click ended in
			// the AcquireKeyFocus below, so the tree is holding the keyboard right now. Left that
			// way, the story would sit selected while the arrows walked the panel and typing went
			// nowhere - which is the one thing a user who just selected a story wants to do.
			// ! Relinquish is a POP, not a hand-off: IKeyBoard.h:49-53 restores the PREVIOUS holder,
			//   and what makes that the document window is the ORDER of the first click - the jump
			//   fronted the window and only then did the tree acquire.
			// ! KBS's KBSResultNodeEH.cpp:175-202 carries the same reasoning and THREE caveats, not
			//   the one this used to point at: (a) if anything comes to hold the focus between the
			//   jump and the acquire - another palette, an edit box of ours - the pop hands the
			//   keyboard to THAT, and it looks like the double click stopped working; (b) the product
			//   does not lean on the pop when it cares where the focus lands, it remembers the handler
			//   and calls AcquireKeyFocus(saved) instead (spellpanel's SaveKeyboardEventHandler);
			//   (c) the bool16 both calls return - kFalse meaning the current holder would not let go
			//   - is ignored here, as it is at every product call site. Written out rather than
			//   pointed at, because "the same caveat" lost two of them (2026-08-18, bug recheck B-U4).
			if (treeEH != nil && keyBoard != nil && keyBoard->GetKeyFocus() == treeEH)
				keyBoard->RelinquishKeyFocus();
			return result;
		}
		// It refused, and has said why. Fall through: the arrows below still want the tree.
	}
	else if (changeIndex >= 0)
	{
		// ★A CHANGE ROW: go to the edit itself and select the words it names (2026-08-20).
		//   The story row's jump goes to the top of the story; this one goes to the place inside it.
		KCMStoryJumpToChange(rowIndex, changeIndex);
	}
	else
	{
		KCMStoryJumpToRow(rowIndex);
	}

	// ***** HAND THE KEYBOARD TO THE LIST, so the up / down arrows walk it from here on. *****
	// (KCMStoryTreeEH, added 2026-08-11 at the user's request - "like KBS, move up and down with
	// the cursor".) ⚠This is the deliberate decision noted at the head of this file being reversed:
	// while the tree holds the keyboard, the tool shortcuts do not reach the document. The double
	// click above is the way back.
	// ★The QUERY of treeEH above is not incidental - it BRINGS KCMStoryTreeEH INTO EXISTENCE.
	//   Implementations are created on first use and nothing else in this plug-in ever asks the tree
	//   for its event handler, so without it the class is never constructed and the arrows keep the
	//   stock behaviour ([[lazy-interface-instantiation]]; KBS measured exactly this).
	if (treeEH != nil && keyBoard != nil)
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
// ***** STORY ROWS ONLY. ***** A right click on a CHANGE row raises nothing (user's call,
// "do not bring the context menu up on a child row"). The first build did offer the menu there,
// aimed at the change's parent story - which is a defensible answer to "what would it even mean"
// and the wrong one to give: the reader is pointing at ONE difference and would be handed an action
// over the whole story, so the menu would be doing something other than what it appears to. A row
// that has nothing to offer should stay silent rather than offer something adjacent.
//
// Deliberately NOT calling the stock handler and NOT changing the selection: a right click that is
// only asking for a menu should not move the user's place in the list - the same rule the chapter
// rows and KBS both settled on. ⚠It is also what makes the row under the cursor the ONLY thing this
// can be about, which is why the row is read from this widget's own node.
bool16 KCMStoryRowEH::RButtonDn(IEvent* e)
{
	int32 changeIndex = -1;
	const int32 rowIndex = this->RowFromNode(&changeIndex);
	if (rowIndex < 0 || changeIndex >= 0 || e == nil)
		return TreeNodeEventHandler::RButtonDn(e);

	KCMStorySetMenuRow(rowIndex);

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

	menuMgr->HandlePopupMenu(kKCMStoryRowMenuName, e->GlobalWhere(), e->GlobalWhere(), kTrue, this);
	return kTrue;
}

// End, KCMStoryRowEH.cpp.
