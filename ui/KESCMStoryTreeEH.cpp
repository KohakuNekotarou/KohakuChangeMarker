//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Event handler for the Story Edits list ITSELF (the tree-view boss, not a row). It adds one thing
//  to the stock up / down arrows and leaves every other key alone: THE ROW IT LANDS ON IS SHOWN,
//  exactly as a click on that row would show it.
//
//  So the list can be walked: click a row once, then hold the down arrow and each story in turn
//  comes to the middle of the window, in both documents. Asked for 2026-08-11 ("like KBS, I want to
//  move up and down with the cursor").
//
//  *** THIS REVERSES A DELIBERATE DECISION, AND THE PRICE IS REAL ***
//
//  Until today KESCM did NOT take the keyboard when a row was clicked, and the reason is written in
//  KESCMStoryRowEH.cpp: this panel sends the user into the document and then gets out of the way,
//  and a panel holding the keyboard is a panel where the tool shortcuts have quietly stopped
//  working. That cost has not gone away - it has been accepted in exchange for the walk, with two
//  ways back out: a DOUBLE click hands the keyboard to the document (KESCMStoryRowEH.cpp), and so
//  does clicking in the document.
//
//  *** WHERE THE MOVEMENT COMES FROM ***
//
//  Not from here. The stock handler moves the selection and this class only asks where it ended up:
//  it knows which rows are on screen and how the selection scrolls, and it can only ever land on a
//  row the tree really has. Written the other way round - working out the next index ourselves - it
//  would have to agree with the model about how many rows there are, which is exactly the kind of
//  arithmetic KBS had to delete after it disagreed (KBSResultTreeEH.cpp:19-26).
//
//  ! THIS CLASS ONLY EXISTS IF SOMETHING ASKS FOR IT. Interface implementations are created on first
//    QueryInterface, so naming it in KESCM.fr is not enough - the key-focus hand-off at the foot of
//    KESCMStoryRowEH::LButtonUp is what brings it into being, and what puts the arrows here at all.
//
//  Same shape as KBS's KBSResultTreeEH, minus the branch handling: this list is flat, so there is
//  nothing to expand on arrival.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IEvent.h"
#include "IKeyBoard.h"				// taking the key focus back after a landing
#include "ISession.h"
#include "ITreeViewController.h"

// General includes:
#include "keyboarddefs.h"			// kVirtualUpArrowKey / kVirtualDownArrowKey
#include "ListIndexNodeID.h"		// the node class ListTreeViewAdapter hands out

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KESCMStoryRowEH.cpp's TreeNodeEventHandler.
#include "../../open/includes/widgets/TreeViewEventHandler.h"	// stock base for a tree widget

// Project includes:
#include "KCMUIID.h"
#include "KESCMStoryJump.h"

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

}	// anonymous namespace

/** Up / down arrows that show the row they land on. */
class KESCMStoryTreeEH : public TreeViewEventHandler
{
public:
	KESCMStoryTreeEH(IPMUnknown* boss) : TreeViewEventHandler(boss) {}
	virtual ~KESCMStoryTreeEH() {}

	virtual bool16 HandleUpDownKey(IEvent* e, const VirtualKey& key);
};

CREATE_PMINTERFACE(KESCMStoryTreeEH, kKESCMStoryTreeEHImpl)

bool16 KESCMStoryTreeEH::HandleUpDownKey(IEvent* e, const VirtualKey& key)
{
	if (!(key == kVirtualDownArrowKey) && !(key == kVirtualUpArrowKey))
		return TreeViewEventHandler::HandleUpDownKey(e, key);

	// A previous landing is still moving views - see gWalking. Swallow the key rather than stepping
	// from a half-made selection.
	if (gWalking)
		return kTrue;
	WalkGuard walkGuard;

	// The stock handler owns the movement (see the note at the top).
	const bool16 handled = TreeViewEventHandler::HandleUpDownKey(e, key);

	InterfacePtr<ITreeViewController> controller(this, UseDefaultIID());
	if (controller == nil)
		return handled;

	// Where it landed. The list is single-selection, so anything else means the move did not happen
	// (an empty list, or already at the end) and there is nothing to show.
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	if (selected.size() != 1)
		return handled;

	TreeNodePtr<ListIndexNodeID> node(selected[0]);
	if (node == nil)
		return handled;

	// The row's action - the same one a click on it would run. It reports its own refusals to the
	// status line, and answers kFalse quietly for the "No edits" placeholder row.
	KESCMStoryJumpToRow(node->GetIndex());

	// ***** TAKE THE KEYBOARD BACK. ***** That jump activated a document window, which took the key
	// focus with it; without this, the NEXT arrow press would go to the document and the walk would
	// stop after one step. IKeyBoard lives on the application boss.
	// ! The session is nil while the app is tearing down - this plug-in's standing rule is to ask
	//   before dereferencing it (2026-08-11, block 15 audit C-1).
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (keyBoard != nil && keyBoard->GetKeyFocus() != this)
		keyBoard->AcquireKeyFocus(this);
	return kTrue;
}

// End, KESCMStoryTreeEH.cpp.
