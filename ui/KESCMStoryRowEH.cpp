//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Which click on a Story Edits row means what. What each one DOES is KESCMStoryJump.cpp.
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
//  ★THE KEYBOARD IS LEFT ALONE. KBS takes the key focus onto its tree so the arrows walk the
//  results; KESCM deliberately does not (user's call, 2026-08-10). This panel's job is to send the
//  user INTO the document, and a panel holding the keyboard is a panel where the tool shortcuts
//  have quietly stopped working.
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
#include "IApplication.h"			// the app boss carries IKeyBoard
#include "IEvent.h"					// ShiftKeyDown / CmdKeyDown
#include "IEventHandler.h"			// the LIST's handler - the arrow keys' owner (see the hand-off below)
#include "IKeyBoard.h"				// AcquireKeyFocus / RelinquishKeyFocus
#include "ISession.h"				// GetExecutionContextSession - the route to the application
#include "ITreeNodeIDData.h"		// this node's NodeID
#include "ITreeViewController.h"	// IsSelected - is this the row the click landed on?
#include "IWidgetParent.h"			// QueryParentFor - the row -> the tree that owns the selection

// General includes:
#include "ListIndexNodeID.h"		// the node class ListTreeViewAdapter hands out (KESCMStoryTreeAdapter.cpp)

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KESCMStoryTreeWidgetMgr.cpp's
// DVPublicUtilities.h: the build files that would carry such a directory live outside this
// plug-in's repository, so a path added there would not survive a fresh checkout.
// ⚠KBS includes this one by name alone; its project carries the directory. KESCM's does not, and
//   the difference is deliberate rather than an oversight (measured 2026-08-10: C1083).
#include "../../open/includes/widgets/TreeNodeEventHandler.h"	// stock base for a tree row

// Project includes:
#include "KCMUIID.h"
#include "KESCMStoryJump.h"

namespace
{

// A file static rather than a member: row widgets are recycled as the list scrolls, so this belongs
// to "the click going on right now" rather than to any one row. One click happens at a time.
bool gSelectOnNextButtonUp = false;

}	// anonymous namespace

class KESCMStoryRowEH : public TreeNodeEventHandler
{
public:
	KESCMStoryRowEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KESCMStoryRowEH() {}

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 ButtonDblClk(IEvent* e);

private:
	/** Which row this click is for, or -1 when it should be left alone.

		Answers -1 for a click the stock handler already used, for a modified click, and for a row
		that is not the selected one - so a caller gets one test instead of four.
	*/
	int32 RowForClick(IEvent* e, bool16 baseHandled) const;
};

CREATE_PMINTERFACE(KESCMStoryRowEH, kKESCMStoryRowEHImpl)

int32 KESCMStoryRowEH::RowForClick(IEvent* e, bool16 baseHandled) const
{
	if (baseHandled || e == nil || e->ShiftKeyDown() || e->CmdKeyDown())
		return -1;

	// The node's NodeID lives on this boss's ITreeNodeIDData (every tree node widget carries it).
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return -1;
	const NodeID& node = nodeData->Get();
	TreeNodePtr<ListIndexNodeID> nodeID(node);
	if (nodeID == nil)
		return -1;

	// The selection lives on the tree, not on the row, so ask upwards for it.
	InterfacePtr<const IWidgetParent> widgetParent(this, UseDefaultIID());
	if (widgetParent == nil)
		return -1;
	InterfacePtr<ITreeViewController> treeController(
		static_cast<ITreeViewController*>(widgetParent->QueryParentFor(ITreeViewController::kDefaultIID)));
	if (treeController == nil || !treeController->IsSelected(node))
		return -1;

	return nodeID->GetIndex();
}

// Nothing of this plug-in's own happens on the way DOWN. The one job here is to start every click
// with the double-click flag down.
bool16 KESCMStoryRowEH::LButtonDn(IEvent* e)
{
	gSelectOnNextButtonUp = false;
	return TreeNodeEventHandler::LButtonDn(e);
}

// The second click of a double click. Only raises the flag - see the note at the head of this file.
bool16 KESCMStoryRowEH::ButtonDblClk(IEvent* e)
{
	const bool16 result = TreeNodeEventHandler::ButtonDblClk(e);
	if (this->RowForClick(e, result) >= 0)
		gSelectOnNextButtonUp = true;
	return result;
}

bool16 KESCMStoryRowEH::LButtonUp(IEvent* e)
{
	// Consumed here, on the way in, so that every path out of this function leaves it down.
	const bool selectRatherThanJump = gSelectOnNextButtonUp;
	gSelectOnNextButtonUp = false;

	// Let the stock handler finish the click first (selection, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);

	const int32 rowIndex = this->RowForClick(e, result);
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
	// these two event handlers were the only ones that did not, KESCMStorySection.cpp:130 included).
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());

	// ***** The second click of a double click SELECTS THE STORY, rather than jumping again. *****
	// The first click already moved the view there, so repeating it would only redo that. What is
	// added is handing the user the text itself - Type tool, the whole story selected.
	if (selectRatherThanJump)
	{
		if (KESCMStorySelectWholeStory(rowIndex))
		{
			// ***** AND GIVE THE KEYBOARD BACK. ***** The FIRST click of this double click ended in
			// the AcquireKeyFocus below, so the tree is holding the keyboard right now. Left that
			// way, the story would sit selected while the arrows walked the panel and typing went
			// nowhere - which is the one thing a user who just selected a story wants to do.
			// ! Relinquish is a POP, not a hand-off: IKeyBoard.h:49-53 restores the PREVIOUS holder,
			//   and what makes that the document window is the ORDER of the first click - the jump
			//   fronted the window and only then did the tree acquire. (Same reasoning, and the same
			//   caveat, as KBS's KBSResultNodeEH.cpp:175-202.)
			if (treeEH != nil && keyBoard != nil && keyBoard->GetKeyFocus() == treeEH)
				keyBoard->RelinquishKeyFocus();
			return result;
		}
		// It refused, and has said why. Fall through: the arrows below still want the tree.
	}
	else
	{
		KESCMStoryJumpToRow(rowIndex);
	}

	// ***** HAND THE KEYBOARD TO THE LIST, so the up / down arrows walk it from here on. *****
	// (KESCMStoryTreeEH, added 2026-08-11 at the user's request - "like KBS, move up and down with
	// the cursor".) ⚠This is the deliberate decision noted at the head of this file being reversed:
	// while the tree holds the keyboard, the tool shortcuts do not reach the document. The double
	// click above is the way back.
	// ★The QUERY of treeEH above is not incidental - it BRINGS KESCMStoryTreeEH INTO EXISTENCE.
	//   Implementations are created on first use and nothing else in this plug-in ever asks the tree
	//   for its event handler, so without it the class is never constructed and the arrows keep the
	//   stock behaviour ([[lazy-interface-instantiation]]; KBS measured exactly this).
	if (treeEH != nil && keyBoard != nil)
		keyBoard->AcquireKeyFocus(treeEH);

	return result;
}

// End, KESCMStoryRowEH.cpp.
