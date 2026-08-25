//========================================================================================
//
//  KCMHideUnchanged.h
//
//  The "Hide Unchanged Spreads" implementation: hiding the spreads that carry no marks and
//  restoring the ones we hid.
//
//  It lives here, not in the UI half, because the five statics it writes (the toggle, and the
//  database + spread list for each side) are the same ones KCMResetHideUnchanged clears --
//  leaving the writer on the UI side would have split that state across the boundary.
//
//  This is MODEL side: it issues kHideSpreadCmdBoss, i.e. it changes the document. The menu
//  item that turns the feature on and off stays on the UI side in KCMActionComponent.cpp.
//
//========================================================================================

#ifndef __KCMHideUnchanged_h__
#define __KCMHideUnchanged_h__

#include "BaseType.h"

class IDataBase;

// The flyout toggle "Hide Unchanged Spreads", both directions.
//
// OFF -> ON: confirm (the command writes to the document file), then collect every spread
// that carries no mark, no registered page and no overflow page, hide them with one
// kHideSpreadCmdBoss, and remember what was hidden. The Source document is then classified
// the same way through the pairing table and hidden too. ON -> OFF: show back exactly what
// we hid, on both sides, without asking.
//
// **Both commands (Target and Source) run inside ONE CmdUtils::SequenceContext, in both
// directions** -- a hide/restore that spans two documents must be one undo step. Splitting it
// per document is the shape that leaves one document stranded when the user hits Ctrl+Z
// (measured: kHideSpreadCmdBoss pushes one undo step per call).
//
// **THE SPREADS ARE PUT BACK WHEN THE DOCUMENT IS SAVED.** (The user's call.) This toggle is a
//   persistent edit, so spreads left hidden stay hidden in the .indd -- and **a hidden spread
//   neither prints nor exports**, which is a page silently missing from whatever gets sent out.
//   KCMBeforeSaveDocResponder (KCMDocResponder.cpp) **restores both sides just before the save**.
//   **Save As is deliberately not covered, and that is the division** (measured): Save, and the
//     save a close performs, restore first; Save As writes the document with the spreads still
//     hidden.
//   @warning **kBeforeCloseDoc would be too late** -- by then the save has already happened
//     (IDataBase::IsModified() == 0, measured in a diagnostic build; KCMDocResponder.cpp has it).
//
// **It also removes a Target/Source disagreement.** Before it existed, the side closed FIRST was
//   saved while still hidden, while the side left open was restored by the close sweep's
//   KCMResetHideUnchanged(kTrue) -- so **the result depended on whether the save came before or
//   after the Stop**. Restoring both sides before every save makes the order irrelevant.
//
// **THE RECORD DOES NOT FOLLOW UNDO/REDO. THIS IS KNOWN, AND IT IS STAYING.** (The user's call
//   -- **do not propose it again.**) kHideSpreadCmdBoss is an undoable persistent command, but
//   the five statics below are not, so an Undo leaves the document and the record out of step.
//   Both paths were measured:
//     - Hide ON -> Undo ...... the document comes back ([-,-,-,-]) while the toggle stays ON
//       => little harm: pressing the toggle re-shows what is already shown
//     - Hide ON -> save -> Undo -> Redo ...... **the document hides** while the toggle is OFF
//       => the record is empty, so **this toggle cannot put it back** (the Pages panel by hand,
//          or a fresh Start)
//   **The restore the save issues is not pushed onto the undo stack**, which is the mechanism
//     behind the second one (measured). A save that does not dirty the undo history is a good
//     property, and it is also what lets the stack and the model disagree; Redo is where that
//     shows.
//   @warning **undoName is the same for hide and for unhide**, so the Edit menu says nothing
//     about which of the two is on the stack -- this only came out by running Undo/Redo.
//   There is an official answer (the Snapshot interface, which makes non-persistent data follow
//     Undo/Redo); **not taken, because the sequence of steps that reaches this is rare.**
//     Reconsider only in a rewrite.
void		KCMHideUnchangedToggle();

// Reset the toggle on both sides. With restoreSpreads=kTrue the spreads we remember hiding
// are shown again with kHideSpreadCmdBoss(kFalse) before the state is dropped (deleted UIDs
// are skipped). Document liveness is checked internally by pointer comparison against
// IDocumentList, never by dereferencing, so passing kTrue is safe even when one side has
// been closed -- only the surviving side is restored. With kFalse the databases are not
// touched at all and only the state is dropped.
//
// Callers -- all FIVE of them:
//   1. re-comparison    KCMDoMarkChangesDoc        (KCMCore.cpp)         -- kTrue
//   2. Stop             KCMDoClearMarks            (KCMCore.cpp)         -- kTrue
//   3. before a save    KCMBeforeSaveDocResponder  (KCMDocResponder.cpp) -- kTrue
//   4. the close sweep  KCMHandleDocsClosed        (KCMPeek.cpp)         -- kTrue, or kFalse
//                                                                           while quitting
//   5. the model's Shutdown KCMPeekStartup::Shutdown (KCMPeek.cpp)       -- kFalse
// @warning number 5 is the one the kFalse sentence above is written for: it is the only caller
//   that ALWAYS passes kFalse. **This list has been wrong once already** -- it said four, and
//   the save responder (3) arrived after it was written. A header that explains an argument
//   nobody in its own caller list ever passes is a header that has not been re-counted.
void		KCMResetHideUnchanged(bool16 restoreSpreads);

// kTrue while the toggle is ON, i.e. while spreads hidden by this feature are being held.
// UpdateActionStates asks for the check mark next to the menu item. The flag itself lives in
// KCMHideUnchanged.cpp with the rest of the state.
bool16		KCMGetHideUnchangedOn();

// The documents currently hiding spreads for this feature, or nil. The close sweep uses
// these for liveness checks (comparison against FindDocByDataBase only, never a deref).
IDataBase*	KCMGetHideUnchangedDB();
IDataBase*	KCMGetHideUnchangedSrcDB();

#endif // __KCMHideUnchanged_h__
