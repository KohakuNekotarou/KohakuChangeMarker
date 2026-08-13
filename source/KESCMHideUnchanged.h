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
// Callers: re-comparison (KESCMDoMarkChangesDoc), Stop (KESCMDoClearMarks) and the close
// sweep (KESCMHandleDocsClosed) may all pass kTrue.
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
