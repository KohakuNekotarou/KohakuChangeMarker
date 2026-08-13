//========================================================================================
//
//  KESCMOversetApply.h
//
//  Applying an overset scan to a document: collecting the overset locations and pages into
//  the engine's state so the Pages panel thumbnails, the scrollbar map and the Prev/Next
//  cycle all see them.
//
//  Split out of KESCMActionComponent.cpp on 2026-08-13. Behaviour unchanged.
//
//  MODEL side. The Find Overset menu item that calls it stays on the UI side.
//
//========================================================================================

#ifndef __KESCMOversetApply_h__
#define __KESCMOversetApply_h__

class IDataBase;

// Scan db (non-nil) for overset locations and pages, store them in the engine state, and
// refresh the displays that show them. Does not write the status line -- the caller emits
// its own message for its own context.
//
// Called from Find Overset, from Refresh while armed, and from Start when the overset
// option is ON. While a comparison is running this must be re-bound to the Target document,
// so when the document differs from last time the previous document's thumbnail marks are
// cleared first.
void		KESCMApplyOversetForDoc(IDataBase* db);

// Which document an overset scan should look at: the comparison Target while a comparison is
// running (so marks and overset can share one Prev/Next cycle), the active document
// otherwise. nil when there is nothing to scan.
//
// ★Declared here rather than kept file-static because all three callers stayed behind in
//  KESCMActionComponent.cpp (the Find Overset and Refresh Overset menu handlers, and
//  UpdateActionStates, which greys the item out when this answers nil). The answer is model
//  state, so the function moved with the scan it feeds.
IDataBase*	KESCMOversetScanTargetDB();

#endif // __KESCMOversetApply_h__
