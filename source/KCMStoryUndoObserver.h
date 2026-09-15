//========================================================================================
//
//  KCMStoryUndoObserver.h
//
//  The panel follows an undo (2026-09-15).
//
//  ***** WHAT IT IS FOR *****
//
//  Restoring a change writes into the story and then diffs it again, so the list is right the
//  moment the reader presses the item. Ctrl+Z puts the text back - and nothing told the panel.
//  Measured 2026-09-15: after an undo the rows still described the document as it had been a
//  moment earlier, and closing and reopening the section was the only way to bring them back.
//  It showed itself again the same evening in the bulk item's question, which counted the rows
//  and therefore asked about one change when three were there.
//
//  ★**THE MODEL IS ALREADY RIGHT, AND THAT IS WHY THIS IS ONE LINE OF WORK.** Whether a change is
//    drawn as "taken in" is derived from the story's text change counter, which an undo takes back
//    by itself (KCMStoryList.h, fReplacedCount). Nothing here recomputes or stores anything: the
//    only thing missing was the signal to redraw.
//
//  ***** WHY LAZY ATTACHMENT *****
//
//  ISubject.h:78-82 - "Lazy notification is also broadcast on undo or redo, therefore, observers
//  must use lazy attachment in order to be called on undo or redo. ... Note that the
//  IObserver::Update method is not called on undo or redo."
//
//  So the attachment is kLazyAttachment and the work is in LazyUpdate, with Update deliberately
//  empty - the same shape, and the same reasoning, as KCMMarksObserver, whose header carries the
//  longer account (including that "lazy" does not mean "later": the flush is at the end of the
//  command sequence, measured 2026-09-07).
//
//  ***** WHERE IT LIVES *****
//
//  An AddIn on kTextStoryBoss under an IID of our own, attached at run time and never
//  persistently - an attachment written into the .indd would be a change to the reader's file made
//  by merely comparing it.
//
//  ⚠**THE PROTOCOL IS THE SDK's IID_ITEXTMODEL, NOT ONE OF OURS.** KCMMarksObserver listens for a
//    notification KCM's own command raises, so it filters on KCM's own protocol IID. This one
//    listens for the story's ordinary text change, raised by InDesign whoever caused it - which is
//    exactly what is wanted, since the text can be changed by the restore, by the reader typing,
//    or by an undo of either. The SDK's own gotolasttextedit sample attaches under the same
//    protocol (kRegularAttachment there, because it does not care about undo).
//
//  ★**ATTACHED TO THE STORIES THE LIST HOLDS, not to every story in the document**: a story with
//    no row has nothing on the panel to redraw. The list is walked after each comparison.
//
//  ★**THERE IS NO DETACH, and that is a decision**: the observer is an AddIn on the very story
//    whose subject it is attached to, so no order of events leaves the subject holding a pointer
//    into a dead object. (The same argument KCMMarksObserver's header makes for kDocBoss.)
//
//========================================================================================

#ifndef __KCMStoryUndoObserver_h__
#define __KCMStoryUndoObserver_h__

#include "BaseType.h"

class IDataBase;

/** Attach the observer to every story the Story Edits list currently holds, in `targetDB`.

	Cheap and idempotent - it asks ISubject::IsAttached first - so callers may call it after every
	comparison. Does nothing for a nil database, and skips rows whose story is not in it (a Removed
	story lives in the Source).
*/
void KCMStoryUndoEnsureObservers(IDataBase* targetDB);

#endif // __KCMStoryUndoObserver_h__

// End, KCMStoryUndoObserver.h.
