//========================================================================================
//
//  KCMPawStamp.h
//
//  The cat-paw stamps: the reader's own "I have looked at this spot" marks, one per point they
//  pressed with the stamp tool. Session-only state, held here on the model side.
//
//  ★NOTHING IS WRITTEN TO THE DOCUMENT. The stamps live in this plug-in and are saved to KCM's
//    own JSON, so the .indd is never touched -- the same promise the comparison marks make, and
//    the reason a paw can be put on a document that is not being compared at all.
//
//  ★THE SHAPE OF THE CONTAINER IS KCMDocUidSet'S, deliberately. That class holds
//    "document -> set of page UIDs" for the registered pages and the ticks; this holds
//    "document -> stamps with coordinates", so the value type differs and the container cannot be
//    shared -- but every rule it carries applies here word for word and is followed:
//      - a closed database is NEVER dereferenced (pointer comparison only)
//      - an entry that became empty is dropped at once
//      - readers take the lock; readers fall back on file identity; writers do not
//    (the last one is the important one -- see the warning on KCMPawStampsOnPage).
//
//========================================================================================
#ifndef __KCMPawStamp_h__
#define __KCMPawStamp_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include "PMReal.h"
#include <vector>

class IDataBase;

/** One cat-paw stamp.
	@warning the position is measured from the PAGE'S TOP-LEFT in points, never in pasteboard
	  coordinates. A pasteboard point is only correct within one session -- add or delete a page
	  and the spread's layout shifts, so a stamp saved that way would come back somewhere else.
	  Measured 2026-09-04: the pasteboard and spread spaces differ by a whole spread from the
	  second spread onwards, and it is the page rectangle that cancels that out. */
struct KCMPawStamp
{
	UID    fPageUID;
	PMReal fX, fY;
	KCMPawStamp() : fPageUID(kInvalidUID) {}
	KCMPawStamp(UID p, const PMReal& x, const PMReal& y) : fPageUID(p), fX(x), fY(y) {}
};

/** Lift the stamp under (x, y) if there is one, otherwise place a new one there. hitRadius is
	half the drawn size, so "under" means inside the paw's own square.
	@return kTrue when a stamp was ADDED, kFalse when one was lifted (or nothing happened).
	@warning writes go to THIS db and no other: unlike the readers below there is no fallback on
	  file identity, because a write always happens on the main thread and means "add to the
	  document I am looking at". Growing a clone's entry would be a wrong document, not a rescue. */
bool16 KCMPawStampToggleAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                           const PMReal& hitRadius);

/** The stamps on one page, in the order they were placed. out is cleared first.
	@warning ★THIS IS READ FROM A BACKGROUND THREAD (the asynchronous PDF export draws there), and
	  a background thread is handed **a clone of the database with a different pointer**. Looking
	  the document up by pointer alone therefore MISSES, and the paws would be absent from the
	  exported PDF -- which is exactly what happened to the registered pages' green "/" and to the
	  ticks before KCMDocUidSet::FindDoc was written. The lookup here falls back on file identity
	  (KCMIsSameDoc) for that reason. */
void KCMPawStampsOnPage(IDataBase* db, UID pageUID, std::vector<KCMPawStamp>& out);

/** Does this document hold any stamp at all -- existence only, for the drawing side's early out.
	Falls back on file identity, as KCMPawStampsOnPage does and for the same reason. */
bool16 KCMPawStampHasAny(IDataBase* db);

/** How many stamps this document holds (the status line reports it). */
int32 KCMPawStampCount(IDataBase* db);

/** Half the drawn size of a paw on this page, in points: the page's short side times
	kKCMPawSizeRatio, halved.
	★★THE ONE PLACE THE SIZE COMES FROM. The tracker asks for the hit box and the drawing side
	  asks for the picture, so what can be seen is exactly what can be lifted. Answers 0 when the
	  page cannot be measured, which the caller reads as "do not stamp here". */
PMReal KCMPawHalfSizeForPage(IDataBase* db, UID pageUID);

/** Drop every stamp of one document (the flyout's "clear"). */
void KCMPawStampClearDoc(IDataBase* db);

/** The liveness sweep run after documents close: drops the entries of documents that have gone.
	@warning a closed database is never dereferenced -- this compares pointers through
	  KCMIsDocDBOpen and nothing more ([[uidref-reuse-after-close]]). */
void KCMPawStampSweepClosedDocs();

#endif // __KCMPawStamp_h__

// End, KCMPawStamp.h.
