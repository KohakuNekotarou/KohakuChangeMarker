//========================================================================================
//
//  KESCMHideUnchanged.h
//
//  The "Hide Unchanged Spreads" implementation: hiding the spreads that carry no marks and
//  restoring the ones we hid.
//
//  Split out of KESCMActionComponent.cpp on 2026-08-13. Behaviour unchanged.
//
//  This is MODEL side: it issues kHideSpreadCmdBoss, i.e. it changes the document. The menu
//  item that turns the feature on and off stays on the UI side in KESCMActionComponent.cpp.
//
//========================================================================================

#ifndef __KESCMHideUnchanged_h__
#define __KESCMHideUnchanged_h__

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
// ★Both commands (Target and Source) run inside ONE CmdUtils::SequenceContext, in both
// directions -- a hide/restore that spans two documents must be one undo step. Splitting it
// per document is the shape that leaves one document stranded when the user hits Ctrl+Z
// (measured 2026-08-16: kHideSpreadCmdBoss pushes one undo step per call). API audit B10.
//
// ★Split out of KESCMActionComponent::DoHideUnchangedToggle on 2026-08-13, body unchanged.
//  It moved as a whole because the five statics it writes (the toggle, and the database +
//  spread list for each side) are the same ones KESCMResetHideUnchanged clears -- leaving
//  the writer on the UI side would have split that state across the boundary.
void		KESCMHideUnchangedToggle();

// Reset the toggle on both sides. With restoreSpreads=kTrue the spreads we remember hiding
// are shown again with kHideSpreadCmdBoss(kFalse) before the state is dropped (deleted UIDs
// are skipped). Document liveness is checked internally by pointer comparison against
// IDocumentList, never by dereferencing, so passing kTrue is safe even when one side has
// been closed -- only the surviving side is restored. With kFalse the databases are not
// touched at all and only the state is dropped.
//
// Callers -- all FOUR of them (counted 2026-08-18, bug recheck B10):
//   1. re-comparison  KESCMDoMarkChangesDoc  (KESCMCore.cpp)  -- kTrue
//   2. Stop           KESCMDoClearMarks      (KESCMCore.cpp)  -- kTrue
//   3. the close sweep KESCMHandleDocsClosed (KESCMPeek.cpp)  -- kTrue, or kFalse while quitting
//   4. ★the model's Shutdown                (KESCMPeek.cpp)  -- kFalse
// ⚠Number 4 was missing from this list, and it is the one the kFalse sentence above is written
// for: it is the only caller that ALWAYS passes kFalse. A header that explains an argument
// nobody in its own caller list ever passes is a header that has not been re-counted.
void		KESCMResetHideUnchanged(bool16 restoreSpreads);

// kTrue while the toggle is ON, i.e. while spreads hidden by this feature are being held.
// UpdateActionStates asks for the check mark next to the menu item. (Split out on
// 2026-08-13: the flag itself lives in KESCMHideUnchanged.cpp with the rest of the state.)
bool16		KESCMGetHideUnchangedOn();

// The documents currently hiding spreads for this feature, or nil. The close sweep uses
// these for liveness checks (comparison against FindDocByDataBase only, never a deref).
IDataBase*	KESCMGetHideUnchangedDB();
IDataBase*	KESCMGetHideUnchangedSrcDB();

#endif // __KESCMHideUnchanged_h__
