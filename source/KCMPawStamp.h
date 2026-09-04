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
#include "KCMConstants.h"	// KCMPawColour -- what fColour below holds
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
	/** Which of the three colours this one was placed in (a KCMPawColour: pink / cyan / green,
		chosen by the modifier keys). ★Kept PER STAMP, because the point of the colours is that
		paws of different kinds sit on the same page at the same time. */
	int32 fColour;
	KCMPawStamp() : fPageUID(kInvalidUID), fColour(kKCMPawColourPink) {}
	KCMPawStamp(UID p, const PMReal& x, const PMReal& y, int32 colour)
		: fPageUID(p), fX(x), fY(y), fColour(colour) {}
};

/** Place a paw at (x, y) on that page, in one of the three colours (a KCMPawColour, chosen by the
	modifier keys). baseHalf is half the page's paw size, the same value the lift takes.
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
                          int32 colour, const PMReal& baseHalf);

/** Lift the paw under (x, y) -- Shift + press. baseHalf is half a paw's size on that page.
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

/** Half a paw's drawn size on this page, in points: the page's short side times kKCMPawSizeRatio,
	halved. ★Every paw is this size -- the modifier keys choose the COLOUR, not the size.
	★★THE ONE PLACE THE SIZE COMES FROM. The tracker asks for the hit box and the drawing side
	  asks for the picture, so what can be seen is exactly what can be lifted. Answers 0 when the
	  page cannot be measured, which the caller reads as "do not stamp here". */
PMReal KCMPawHalfSizeForPage(IDataBase* db, UID pageUID);

/** Every stamp of one document, in the order they were placed -- what the JSON is written from.
	out is cleared first. Answers nothing for a document that holds none. */
void KCMPawStampGetForSave(IDataBase* db, std::vector<KCMPawStamp>& out);

/** Put a document's stamps back to exactly this list -- what the JSON is read into.
	★It REPLACES rather than merges: loading is "restore the state that was saved", and merging
	  would make a second load double everything.
	⚠By pointer, no fallback on file identity: loading happens on the main thread and means "this
	  document I have open" (the writers' rule -- see the head of the .cpp). An empty list drops
	  the entry, which keeps the "an entry that exists has something in it" promise. */
void KCMPawStampReplaceAll(IDataBase* db, const std::vector<KCMPawStamp>& in);

/** Drop every stamp of one document (the flyout's "clear"). */
void KCMPawStampClearDoc(IDataBase* db);

/** The liveness sweep run after documents close: drops the entries of documents that have gone.
	@warning a closed database is never dereferenced -- this compares pointers through
	  KCMIsDocDBOpen and nothing more ([[uidref-reuse-after-close]]). */
void KCMPawStampSweepClosedDocs();

#endif // __KCMPawStamp_h__

// End, KCMPawStamp.h.
