//========================================================================================
//
//  KCMMarksObserver.h
//
//  The one thing that writes the session store of ticks and cat paws.
//
//  ***** WHAT IT IS FOR *****
//
//  The marks live in the document, as script labels on the pages that carry them
//  (KCMPageMarksDoc.h). The session store -- KCMPageCheck's set and KCMPawStamp's list -- is a
//  CACHE of those labels: it exists because the drawing side has to answer "is this page ticked"
//  once per page per draw, on a background thread as well as the main one, and reading and parsing
//  a label there would be neither fast nor safe.
//
//  A cache has to be refilled when the thing it caches changes. This observer is that refill.
//  ★To be exact about it, since a claim like this is worth nothing if it is not: the refill itself
//    is KCMMarksSyncFromDocument, and it -- not this file -- is the only caller of
//    KCMPageCheckReplaceAll and KCMPawStampReplaceAll. TWO things call it: this observer, and the
//    document-opened responder (a document that arrives already carrying labels has to be read
//    once, and no notification announces that). Both are the same road in the same direction:
//    document -> store, never the other way. The only other writers of either store are the
//    closed-document sweep and the shutdown clear, and neither is a change to any document.
//
//  ***** WHY LAZY ATTACHMENT, AND WHY THAT IS THE WHOLE TRICK *****
//
//  ISubject.h:78-82 -- "Lazy notification is also broadcast on undo or redo, therefore, observers
//  must use lazy attachment in order to be called on undo or redo. ... Note that the
//  IObserver::Update method is not called on undo or redo."
//
//  So the attachment is kLazyAttachment and the work is in LazyUpdate. Update is implemented as a
//  no-op on purpose: if it did the work too, Do would be served by a path that Undo does not have,
//  and the two would drift the first time one of them was changed. One road, three directions.
//
//  ⚠**"LAZY" DOES NOT MEAN "LATER", AND THIS WAS MEASURED** (2026-09-07). The header speaks of
//    lazy notifications being broadcast "when the application is idle", and this file said so too
//    until the measurement contradicted it: the refill had ALREADY happened by the next statement
//    after the command returned -- no message pump runs in between, so the flush is at the end of
//    the command sequence. It showed up in the status line, which had been written to correct for
//    a delay that does not occur and printed "check -1, total -1" (KCMPageCheck.cpp carries the
//    full account). ⇒ **Do not write code that waits for the refill, and do not write code that
//    assumes it has not happened yet.** Neither bet is needed: read what you need BEFORE the write.
//
//  ⚠LazyUpdate is handed neither the ClassID of the change nor the changedBy pointer
//    (IObserver.h:112), and its data may be nil (IObserver.h:101-106). It cannot be told what
//    changed, so it re-reads the document. This is what Adobe's own header prescribes.
//
//  ***** WHERE IT LIVES, AND WHY IT NEVER DETACHES *****
//
//  An AddIn on kDocBoss under an IID of our own (kDocBoss already carries somebody else's
//  IID_IOBSERVER, and the unit of collision is the ImplementationID). It is attached at run time
//  and never persistently: an attachment written into the .indd would be a change to the reader's
//  file made by merely opening it.
//
//  ★**There is deliberately no detach, and that is NOT an oversight** -- KCM's three other
//    observers all have one (KCMViewSync, KCMPanelAlpha, KCMModelChangeObserver), so the absence
//    here needs its reason written down. Those three attach to things that OUTLIVE the plug-in --
//    the application, the active context, the panel manager -- so a subject left holding a pointer
//    into unloaded code is a real prospect and detaching is what prevents it. This one attaches a
//    document's observer to THAT SAME DOCUMENT'S subject: both interfaces are on one boss, so
//    there is no order of events in which the subject outlives the observer. (And if the plug-in
//    could unload while a document still lived, the AddIn interface itself would already be a
//    dangling vtable -- detaching would not save it.)
//
//  ⚠**Attached where it is USED, not where a document appears.** KCM has a kAfterOpenDoc responder
//    but no kAfterNewDoc one, so attaching "when a document opens" would silently miss every
//    document made with File > New -- and a brand-new document is exactly where a reader tries the
//    first paw. Both doors call KCMMarksEnsureObserver instead: the command before it runs, and
//    the responder before it restores.
//
//========================================================================================

#ifndef __KCMMarksObserver_h__
#define __KCMMarksObserver_h__

#include "BaseType.h"

class IDataBase;

/** Make sure this document's marks observer is attached. Cheap and idempotent -- it asks
	ISubject::IsAttached first -- so callers may call it on every write.
	Does nothing for a nil database, or one with no root object. */
void	KCMMarksEnsureObserver(IDataBase* db);

#endif // __KCMMarksObserver_h__

// End, KCMMarksObserver.h.
