//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  THE STORY EDITS LIST FOLLOWS AN UNDO AND A REDO (2026-09-25, the user: "after Ctrl+Z the list is out of date -
//  fix it", and for an import undone at once: "leave the rows, showing None").
//
//  A lazy observer AddIn'd on kDocBoss (IID_IKCMSTORYFOLLOWOBSERVER), attached at run time to the TARGET DOCUMENT's
//  subject under the notifications a story's change repeats there (KCMStoryFollowObserver.cpp lists them).
//  ★LAZY BECAUSE LAZY IS THE ONLY NOTIFICATION AN UNDO AND A REDO BROADCAST - the same message ids as the Do
//  (LazyNotificationData.h:50-58); a responder and a command interceptor are not called at all (memory:
//  command-history-and-undo-stack).
//  ★★ON THE DOCUMENT, NOT ON EACH STORY (2026-10-25 - open item 10 of the Track mode): from 2026-09-25 it sat on each
//   listed story's subject under IID_ITEXTMODEL, and a test build's trace saw InDesign build all six stories again
//   right after Start - new ISubject addresses, nothing attached, no notification (memory
//   observer-lost-when-object-purged, KFC's same loss). A document is not purged while it is open, and KT's docnotify
//   probe measured what reaches its subject: IID_ITEXTMODELCHANGED for any story's text or attributes, its Undo and its
//   Redo, and IID_ACCEPT/REJECT..REDLINE_DOCUMENT for an accept or a reject - an accept changes no text and moves no
//   counter, which the per-story observer could not have seen either (AR-2 and AR-9 of tests-track-ar.ps1).
//
//  ★★WHAT IT DOES: when a story has come back to a state its row was compared at, or below the last one (an Undo or a
//   Redo - KCMStoryList::NeedsCompareAgain), or a taken-back record changed state, every row in that state is compared
//   again (KCMStoryDiffRun::RunOne - the same as "Refresh Story Comparison") and the panel is told ONCE: one Undo can
//   move many stories (a whole import is one step). The Track mode reads its whole list again under the same counter
//   rule (KCMTrackList::AnyNeedsReadAgain), or when an accept or a reject moved the Target's records
//   (KCMTrackRead::RecordsMovedSinceRead). The notification says neither which story nor that it was an undo; the
//   counters do.
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

/** Attach the observer to the Target document's subject under each protocol it follows, where it is not attached yet.
	Called when the list has been built and after a KCM write (cheap: a document answers IsAttached at once).
	⚠NO DETACH, as KCMMarksObserver has none: it is a run-time attachment (never written into the document), it does
	nothing for a document that is not the armed Target, and it goes with the document. ★A story deleted and brought
	back by an undo needs nothing now: the document, not the story, carries the attachment (the case re-audit M19 of
	2026-09-27 left open). */
void KCMStoryFollowEnsureObservers(IDataBase* targetDB);

#ifdef KCM_DIAG
/** TEST BUILDS ONLY (2026-10-25 - open item 10): one trace line - the Target document's subject's address and how
	many of the followed protocols the observer is attached under now. `when` names the caller. Changes nothing. */
void KCMStoryFollowDiagReport(IDataBase* targetDB, const char* when);
#endif

#endif // __KCMStoryFollowObserver_h__

// End, KCMStoryFollowObserver.h.
