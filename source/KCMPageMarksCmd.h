//========================================================================================
//
//  KCMPageMarksCmd.h
//
//  The ONE way a tick or a cat paw is ever written.
//
//  ***** WHY A COMMAND OF OUR OWN *****
//
//  Until 2026-09-07 the marks lived in this plug-in's own session store and were copied into the
//  document only when the reader pressed a menu item. That arrangement had the defect every
//  two-copy arrangement has ([[one-question-one-place]]): Ctrl+Z could put the LABEL back but not
//  the store, so the tick stayed on screen after being undone.
//
//  So the direction is reversed. The document's script labels are the marks; the session store is
//  a cache of them. This command is the only thing that writes the labels, and the observer next
//  door (KCMMarksObserver.h) is the only thing that writes the store -- it rebuilds it from the
//  labels whenever they change.
//
//  ***** WHAT MAKES UNDO AND REDO WORK *****
//
//  Do() writes the labels; DoNotify() raises ISubject::ModelChange on the DOCUMENT'S subject. That
//  second half is not decoration, it is the mechanism:
//
//    ISubject.h:78-82  "Lazy notification is also broadcast on undo or redo, therefore, observers
//                       must use lazy attachment in order to be called on undo or redo. ... Note
//                       that the IObserver::Update method is not called on undo or redo."
//    LazyNotificationData.h:50-58  "At Undo, the InDesign runtimes queue up the same message IDs
//                       that were queued up in Do ... At Redo, all we do is queue up the same
//                       message IDs ... that were queued up in Do."
//
//  ⇒ Do, Undo and Redo all arrive at the observer down the SAME road. There is no undo-specific
//    code anywhere in KCM, and there must never be: the moment undo has a path of its own, the two
//    paths start to differ.
//
//  ⚠**ModelChange, never Change** (ISubject.h:61-64): a change to an object that persists in a
//    database that supports undo MUST be notified with ModelChange. That is also why the
//    notification goes to the document's subject and not to the application's, which is where the
//    model's other notifications go (KCMModelNotify.h) -- the application is not in a database that
//    supports undo, so a Change raised there would never be replayed.
//
//========================================================================================

#ifndef __KCMPageMarksCmd_h__
#define __KCMPageMarksCmd_h__

#include "BaseType.h"		// int32, bool16, ErrorCode
#include "OMTypes.h"		// UID
#include <vector>

#include "KCMPawStamp.h"	// KCMPawStamp -- the value written, not just referred to

class IDataBase;

/** What ONE page is to carry once the write is done. It is a whole answer, not a difference: the
	command replaces that page's marks with exactly this, which is what lets one step both add a
	paw and take a tick off. */
struct KCMPageMarks
{
	UID                      fPage;
	bool16                   fCheck;
	std::vector<KCMPawStamp> fPaws;

	KCMPageMarks() : fPage(kInvalidUID), fCheck(kFalse) {}
	KCMPageMarks(UID page, bool16 check) : fPage(page), fCheck(check) {}
};

/** Write these pages' marks into the document as ONE undo step.

	@param undoName what the reader sees in Edit > Undo. English, and it is passed through
	  untranslated -- KCM has no string table for the model half (KCMModelNotify.h says why).
	@return kSuccess, or the failure that stopped it. An empty list is kSuccess: "nothing to write"
	  is not an error, and it must not leave an empty step on the undo stack.

	⚠**THE CALLER DOES NOT TOUCH THE SESSION STORE.** It only says what the pages should carry. The
	  store is put back by KCMMarksObserver when the notification arrives -- on the way in and on
	  the way back out again. A caller that "helpfully" also updates the store re-creates the very
	  two-copy defect this command exists to remove.
	@see KCMMarksObserver.h */
ErrorCode	KCMMarksWrite(IDataBase* db, const std::vector<KCMPageMarks>& pages, const char* undoName);

#endif // __KCMPageMarksCmd_h__

// End, KCMPageMarksCmd.h.
