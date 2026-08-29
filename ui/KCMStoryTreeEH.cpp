//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
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
//  Until today KCM did NOT take the keyboard when a row was clicked, and the reason is written in
//  KCMStoryRowEH.cpp: this panel sends the user into the document and then gets out of the way,
//  and a panel holding the keyboard is a panel where the tool shortcuts have quietly stopped
//  working. That cost has not gone away - it has been accepted in exchange for the walk, with two
//  ways back out: a DOUBLE click hands the keyboard to the document (KCMStoryRowEH.cpp), and so
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
//    QueryInterface, so naming it in KCMUI.fr is not enough - the key-focus hand-off at the foot of
//    KCMStoryRowEH::LButtonUp is what brings it into being, and what puts the arrows here at all.
//
//  Same shape as KBS's KBSResultTreeEH. ⚠This said "the list is flat, so there is nothing to
//  expand on arrival" until 2026-08-20, when the list was given a second level. Nothing is
//  expanded here all the same, for a different reason: the rebuild opens every story
//  (KCMStoryTreeRebuild), so an arrow can only land on a row that is already on screen.
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
#include "KCMStoryNodeID.h"		// our node class: (row, change). Was ListIndexNodeID until 2026-08-20

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KCMStoryRowEH.cpp's TreeNodeEventHandler.
#include "../../open/includes/widgets/TreeViewEventHandler.h"	// stock base for a tree widget

// Project includes:
#include "KCMUIID.h"
#include "KCMStoryJump.h"

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
class KCMStoryTreeEH : public TreeViewEventHandler
{
public:
	KCMStoryTreeEH(IPMUnknown* boss) : TreeViewEventHandler(boss) {}
	virtual ~KCMStoryTreeEH() {}

	virtual bool16 HandleUpDownKey(IEvent* e, const VirtualKey& key);
};

CREATE_PMINTERFACE(KCMStoryTreeEH, kKCMStoryTreeEHImpl)

bool16 KCMStoryTreeEH::HandleUpDownKey(IEvent* e, const VirtualKey& key)
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

	TreeNodePtr<KCMStoryNodeID> node(selected[0]);
	if (node == nil)
		return handled;

	// The row's action - the same one a click on it would run. It reports its own refusals to the
	// status line, and answers kFalse quietly for the "No edits" placeholder row.
	//
	// ★★★WALKING ONTO A CHANGE ROW NOW GOES TO THE EDIT (2026-08-20, later the same day). It went
	//   to the row's STORY until then, and the reason written here was that the walk fires on every
	//   arrow press, so selecting text each time would fight whatever the user is doing in the
	//   document.
	//   ⇒ ★THAT REASON IS GONE: a single click no longer selects anything either - it moves the
	//     view and flashes a mark that takes itself away (KCMStoryJumpToChange). Walking is now
	//     the same gesture as clicking, which is what the user asked for ("move with the up and down
	//     arrows, in the same way as KBS"), and the limitation this comment described was a consequence of
	//     the selection, not of the walk.
	//   ⚠A LIMITATION THAT OUTLIVES ITS CAUSE READS AS A DESIGN DECISION. This one was written down
	//     properly, which is the only reason it could be found and removed when the cause went.
	if (node->IsChangeRow())
		KCMStoryJumpToChange(node->GetRow(), node->GetChange());
	else
		KCMStoryJumpToRow(node->GetRow());

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

// End, KCMStoryTreeEH.cpp.
