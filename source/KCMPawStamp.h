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
	/** How big this one is, as a multiple of the page's ordinary paw size: kKCMPawNormalScale for
		a plain press, kKCMPawBigScale for Alt. ★It is kept PER STAMP rather than as a flag, so the
		drawing and the hit box read the same number and a big paw is lifted by pressing anywhere
		on the big paw. */
	PMReal fScale;
	KCMPawStamp() : fPageUID(kInvalidUID), fScale(1.0) {}
	KCMPawStamp(UID p, const PMReal& x, const PMReal& y, const PMReal& scale)
		: fPageUID(p), fX(x), fY(y), fScale(scale) {}
};

/** Place a paw at (x, y) on that page. scale is a multiple of the page's ordinary paw size
	(kKCMPawNormalScale, or kKCMPawBigScale for Alt); baseHalf is half the page's ordinary size,
	the same value the lift takes.
	★★A PLAIN PRESS ALWAYS PLACES -- it never lifts (changed 2026-09-04 at the user's request).
	  It began as a toggle, and stamping repeatedly is what a reader actually does: with a toggle,
	  a second paw beside the first kept taking the first one off. Lifting has a key of its own.
	★★AND IT REFUSES TO STACK. A press that lands on a paw already there does nothing (the user's
	  request, the same day): two paws on one spot look like one and only the top can be lifted, so
	  the second press is far more likely to be a slip than an intention.
	@return kTrue when one was placed, kFalse when a paw was already there (or the arguments were
	  no good).
	@warning writes go to THIS db and no other: unlike the readers below there is no fallback on
	  file identity, because a write always happens on the main thread and means "add to the
	  document I am looking at". Growing a clone's entry would be a wrong document, not a rescue. */
bool16 KCMPawStampPlaceAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                          const PMReal& scale, const PMReal& baseHalf);

/** Lift the paw under (x, y) -- Shift + press. baseHalf is half the page's ORDINARY paw size;
	each stamp's own reach is that times its fScale.
	Where paws overlap, the one placed last comes off first.
	@return kTrue when one was lifted, kFalse when the press landed on none. */
bool16 KCMPawStampLiftAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                         const PMReal& baseHalf);

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

/** Half an ORDINARY paw's drawn size on this page, in points: the page's short side times
	kKCMPawSizeRatio, halved. A stamp placed with Alt is that times kKCMPawBigScale, which is what
	each stamp's own fScale carries -- this answers the page's baseline, not any one stamp's size.
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
