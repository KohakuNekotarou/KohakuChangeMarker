//========================================================================================
//
//  KCMExternalSource.h
//
//  A Source that is NOT an open document: a database another plug-in lends to the comparison.
//
//  WHY IT EXISTS (2026-09-02). Kohaku InDesign MCP holds a task-start copy of a document -- an
//  IDataBase::Clone() taken when a task began -- and the user asked to see "what changed since
//  the task started" as KCM's marks. The comparison engine already takes two databases
//  (KCMDoMarkChangesDoc), so drawing was never the problem. What refused was everything around
//  it: KCM's rule that "alive" means "in IDocumentList", asked at some thirty places, and a clone
//  is in no list. The first draw would have run KCMHandleDocsClosed and cleared the marks -- the
//  same shape the background thread's clone produced during asynchronous PDF export.
//
//  ★THE INVARIANT: **a database is registered here while it is the CHOSEN Source or the ARMED
//    Source -- or until the lender takes it back, whichever is later.** Registration happens
//    inside KCMStartComparisonWithSourceDB and nowhere else, which also CHOOSES it (Set as Target
//    + Set as Source + Start in one, the user's ask of 2026-09-02: a Stop keeps the pair on the
//    panel, and the flyout's own Start compares against the copy again). It is forgotten in
//    exactly three places: the lender's KCMReleaseExternalSource (the copy is going), "Set as
//    Source" naming a real document in its place (KCMSetChosenSourceToActive, when nothing is
//    drawing from it), and the model's shutdown (KCMClearChosenDocs). A Stop does NOT forget it,
//    deliberately.
//    ⚠**THE "whichever is later" IS NOT SLACK IN THE WORDING** -- it is a state the code really
//     reaches, and this line said "and at no other time" until 2026-09-04. Choose a real document
//     as Source WHILE THE COMPARISON IS ARMED and KCMSetChosenSourceToActive declines to forget
//     the registration (something is drawing from it); Stop then leaves a database registered
//     that is neither chosen nor armed, until the lender releases it. **No harm follows** --
//     KCMIsDbAlive answers kTrue about a database nothing draws from -- but a reader checking
//     the code against this paragraph would have found the paragraph wrong, which is worse than
//     the state it describes.
//
//  ⚠THE LENDER OWNS THE DATABASE AND MUST CALL KCMReleaseExternalSource (through the facade's
//    ReleaseExternalSourceDB) BEFORE DELETING IT. KCM never deletes it and never asks whether it
//    is still there -- it cannot: DBUtils::IsValidDataBase answers "yes" about a clone whose
//    source is gone (Kohaku InDesign MCP measured that, KIDMCPDbClone.h).
//
//  **The pointer is compared, never dereferenced, in every function here** -- the same rule the
//  rest of this plug-in keeps for a database it holds ([[uidref-reuse-after-close]]).
//
//  THREADS. The pointer is written on the main thread only (Start / Stop / close) and read from
//  the drawing, which the asynchronous export runs on a background thread. A single pointer
//  assignment needs no lock, exactly as KCMDrawEventHandler::sSrcDB is treated (its comment at
//  the assignment says why). The label is read on the main thread only (panel, TSV export).
//
//========================================================================================

#ifndef __KCMExternalSource_h__
#define __KCMExternalSource_h__

#include "BaseType.h"

class IDataBase;
class IDocumentList;
class PMString;

/** Registers db as the lent Source, with the words the panel shows for it ("task-start copy
	(taken 20:41:07 from the menu)"). Replaces any earlier registration. nil is ignored.
	@warning call ONLY from KCMStartComparisonWithSourceDB -- see the invariant above. */
void		KCMRegisterExternalSource(IDataBase* db, const PMString& label);

/** Forgets the registration, whatever it was. Idempotent; assignment only, so it is safe at any
	point of the shutdown sequence. */
void		KCMForgetExternalSource();

/** The registered database, or nil. Compared, never dereferenced, by callers too. */
IDataBase*	KCMExternalSourceDB();

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
