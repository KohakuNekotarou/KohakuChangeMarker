//========================================================================================
//
//  KCMOversetApply.h
//
//  Applying an overset scan to a document: collecting the overset locations and pages into
//  the engine's state so the Pages panel thumbnails, the scrollbar map and the Prev/Next
//  cycle all see them.
//
//  Split out of KCMActionComponent.cpp on 2026-08-13. Behaviour unchanged.
//
//  MODEL side. The Find Overset menu item that calls it stays on the UI side.
//
//========================================================================================

#ifndef __KCMOversetApply_h__
#define __KCMOversetApply_h__

class IDataBase;

// Scan db (non-nil) for overset locations and pages, store them in the engine state, and
// refresh the displays that show them. Does not write the status line -- the caller emits
// its own message for its own context.
//
// ★FOUR CALLERS, counted 2026-08-17. Two of them cross the boundary through
// IKCMCompareFacade::ApplyOversetForDoc, so grepping for this name alone finds the other two
// and the facade forwarder -- never the files that actually ask for a scan:
//   1. Find Overset, switching ON      -- ui/KCMActionComponent.cpp, through the facade
//   2. Refresh Overset                 -- ui/KCMActionComponent.cpp, through the facade.
//                                         Needs Find Overset ON, NOT an armed comparison.
//   3. Start, when Find Overset is ON  -- KCMComparisonRun.cpp:163. Re-binds the scan to the
//                                         comparison Target so marks and overset share one
//                                         Prev/Next cycle.
//   4. Stop, when Find Overset is ON   -- KCMComparisonRun.cpp:116. The ONE caller that hands
//                                         over a saved pointer (sOversetDB) rather than a
//                                         freshly resolved one; see the liveness test at the
//                                         head of the .cpp, which exists for this caller.
// ⚠This list read "Find Overset, Refresh while armed, and Start" until 2026-08-17. Stop was
//  missing although the .cpp singles it out by name, and Refresh needs no armed comparison --
//  two descriptions of one thing, kept in two places, disagreeing ([[one-question-one-place]]).
//
// While a comparison is running the scan must be re-bound to the Target document, so when the
// document differs from last time the previous document's thumbnail marks are cleared first.
void		KCMApplyOversetForDoc(IDataBase* db);

// Which document an overset scan should look at: the comparison Target while a comparison is
// running (so marks and overset can share one Prev/Next cycle), the active document
// otherwise. nil when there is nothing to scan.
//
// ★Declared here rather than kept file-static because all three callers stayed behind in
//  KCMActionComponent.cpp (the Find Overset and Refresh Overset menu handlers, and
//  UpdateActionStates, which greys the item out when this answers nil). The answer is model
//  state, so the function moved with the scan it feeds.
//  ⚠Since the split those three live on the UI side and reach this through
//  IKCMCompareFacade::GetOversetScanTargetDB(), so this spelling appears in exactly one
//  place besides the definition -- the facade forwarder. Grep BOTH names, or the callers look
//  like none at all (the same trap B5 hit with KCMBaseScreenOpacity on 2026-08-17).
IDataBase*	KCMOversetScanTargetDB();

#endif // __KCMOversetApply_h__
