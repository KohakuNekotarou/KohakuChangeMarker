//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  THE STORY EDITS LIST FOLLOWS AN UNDO AND A REDO (2026-09-25, the user: "after Ctrl+Z the list is out of date -
//  fix it", and for an import undone at once: "leave the rows, showing None").
//
//  A lazy observer AddIn'd on kTextStoryBoss (IID_IKCMSTORYFOLLOWOBSERVER), attached at run time to the Target's
//  stories the list holds, listening under the SDK's own IID_ITEXTMODEL. ★LAZY BECAUSE LAZY IS THE ONLY
//  NOTIFICATION AN UNDO AND A REDO BROADCAST - the same message ids as the Do (LazyNotificationData.h:50-58); a
//  responder and a command interceptor are not called at all (memory: command-history-and-undo-stack).
//
//  ★★WHAT IT DOES: when the row's story has come back to a state the row was compared at, or below the last one
//   (an Undo or a Redo - KCMStoryList::NeedsCompareAgain), or a taken-back record changed state, every row in that
//   state is compared again (KCMStoryDiffRun::RunOne - the same as "Refresh Story Comparison") and the panel is told
//   ONCE: one Undo can move many stories (a whole import is one step), and each would otherwise rebuild the panel.
//   The notification does not say it was an undo; the counters do.
//  ⚠★PLAIN TYPING IS LEFT ALONE: it moves the counter to values never compared at, and comparing a story on every
//   keystroke would be the price of it. A row's own items compare it again before they act, as before.
//
//  ⛔The observer of 2026-09-15 (+16 / +60, KCMStoryUndoObserver) only asked the panel to redraw, and went with the
//   restore on 2026-09-21; its numbers stay retired (KCMID.h).
//
//========================================================================================

#ifndef __KCMStoryFollowObserver_h__
#define __KCMStoryFollowObserver_h__

class IDataBase;

/** Attach the observer to every Target story the Story Edits list has a row for, where it is not attached yet.
	Called when the list has been built. ⚠NO DETACH, as the 2026-09-15 observer had none: it is a run-time
	attachment (never written into the document), it does nothing for a story no row names, and a document's
	stories go with the document. */
void KCMStoryFollowEnsureObservers(IDataBase* targetDB);

#endif // __KCMStoryFollowObserver_h__

// End, KCMStoryFollowObserver.h.
