//========================================================================================
//
//  KCMExternalSource.h
//
//  A Source that is NOT an open document: a database clone lent to the comparison. The one lender is
//  this plug-in's "Task Start" kept in memory (KCMTaskStartClone.h), which takes the clone, registers it
//  here, and lets it go through KCMForgetExternalSource.
//
//  WHY IT EXISTS. The comparison engine takes two databases (KCMDoMarkChangesDoc), so drawing from
//  a clone is no problem. What refuses is everything around it: KCM's rule that "alive" means "in
//  IDocumentList", asked in many places, and a clone is in no list. The first draw would run
//  KCMHandleDocsClosed and clear the marks -- the same shape the background thread's clone produces
//  during asynchronous PDF export. Registered here, the clone passes every test that understands a
//  lent Source - the liveness tests, the panel's Source line, the same-document test, the refusals
//  of the Resources export and the report.
//
//  THE INVARIANT: **a database is registered here while it is the CHOSEN Source or the ARMED
//    Source -- or until the lender takes it back, whichever is later.** Registration happens
//    inside KCMTakeTaskStartClone (Task Start kept in memory), and nowhere else; it CHOOSES it as
//    well, and starts nothing. It is forgotten by the lender (the let-go in KCMTaskStartClone),
//    by a real document or a file taking its
//    place on the Source end (KCMSetChosenSourceToActive / KCMSetChosenSourceFile, when nothing is
//    drawing from it), and by KCMClearChosenDocs -- which is BOTH the model's shutdown AND the
//    flyout's "Clear Target and Source" (that item stops the comparison first, so the copy is
//    never forgotten while it is being drawn from). A Stop does NOT forget it,
//    deliberately.
//    ⚠**THE "whichever is later" IS A STATE THE CODE REALLY REACHES**: choose a real document as
//     Source WHILE THE COMPARISON IS ARMED and KCMSetChosenSourceToActive declines to forget the
//     registration (something is drawing from it); Stop then leaves a database registered that is
//     neither chosen nor armed, until the lender lets it go. **No harm follows** -- KCMIsDbAlive
//     answers kTrue about a database nothing draws from. Do not "tighten" the invariant to
//     "and at no other time".
//
//  ⚠THE LENDER OWNS THE DATABASE AND MUST LET IT GO HERE (KCMForgetExternalSource, after stopping
//    a comparison that draws from it) BEFORE DELETING IT - KCMTaskStartClone does. Nothing else
//    asks whether it is still there -- it cannot: DBUtils::IsValidDataBase answers "yes" about a
//    clone whose source is gone (measured: memory second-database-and-db-cloning).
//
//  **The pointer is compared, never dereferenced, in every function here** -- the same rule the
//  rest of this plug-in keeps for a database it holds (memory uidref-reuse-after-close).
//
//  THREADS. The pointer is written on the main thread only (Start / Stop / close) and read from
//  the drawing, which the asynchronous export runs on a background thread. A single pointer
//  assignment needs no lock, exactly as KCMDrawEventHandler::sSrcDB is treated (its comment at
//  the assignment says why). The label is read on the main thread only (the panel, the status
//  line, the report's names).
//
//========================================================================================

#ifndef __KCMExternalSource_h__
#define __KCMExternalSource_h__

#include "BaseType.h"

class IDataBase;
class IDocumentList;
class PMString;

/** Registers db as the lent Source, with the words the panel shows for it ("Task Start (in memory)
	- Report.indd - 20:41:07"). Replaces any earlier registration. nil is ignored.
	@warning call ONLY from KCMTakeTaskStartClone -- see the invariant above. */
void		KCMRegisterExternalSource(IDataBase* db, const PMString& label);

/** Forgets the registration, whatever it was. Idempotent; assignment only, so it is safe at any
	point of the shutdown sequence. */
void		KCMForgetExternalSource();

/** kTrue when db is the registered database. A pointer comparison; nil answers kFalse. */
bool16		KCMIsExternalSource(IDataBase* db);

/** kTrue, and outLabel filled, when db is the registered database. kFalse leaves outLabel alone. */
bool16		KCMExternalSourceLabel(IDataBase* db, PMString& outLabel);

/** THE ONE LIVENESS TEST: kTrue when db is an open document's database (in docList) or the
	registered lent Source. nil db answers kFalse. A nil docList tests the registration only --
	callers that treat "no list" as "cannot judge" keep their own early return before this.
	@warning replaces the bare `docList->FindDocByDataBase(db) != nil` test everywhere a database
	KCM holds is checked for being still there. A site that keeps the bare test will clear the
	comparison the moment a lent Source is drawn. */
bool16		KCMIsDbAlive(IDocumentList* docList, IDataBase* db);

#endif // __KCMExternalSource_h__

// End, KCMExternalSource.h.
