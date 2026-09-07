//========================================================================================
//
//  KCMPawStamp.h
//
//  The cat-paw stamps: the reader's own "I have looked at this spot" marks, one per point they
//  pressed with the stamp tool. Session-only state, held here on the model side.
//
//  ⚠★★★**THIS FILE'S STORE IS A CACHE. THE PAWS LIVE IN THE DOCUMENT** (2026-09-07). Each one is
//    part of a script label on the page that carries it (KCMPageMarksDoc.h), it is written by a
//    command so Ctrl+Z takes it back, and this map is refilled from those labels by
//    KCMMarksObserver. ⚠**The old promise printed here -- "NOTHING IS WRITTEN TO THE DOCUMENT ...
//    the .indd is never touched" -- became false the moment writing went automatic, and it stood
//    here for half a day afterwards.** The comparison marks still keep that promise; the paw and
//    the tick are the two exceptions the user asked for, because they are the READER'S OWN marks.
//    A paw can still be put on a document that is not being compared at all.
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
#include "PMString.h"		// the word an Alt press puts beside a paw
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
	/** Which of the two colours this one was placed in (a KCMPawColour: red / blue, swapped with
		Shift+Alt). ★Kept PER STAMP, because the point of the colours is that paws of different
		kinds sit on the same page at the same time.
		⚠**A stamp read from a document may carry a RETIRED number** (0 pink / 1 cyan / 2 green).
		  Everything that reads one runs it through KCMPawColourFromStored below; nothing else in
		  KCM knows the old numbers exist. */
	int32 fColour;
	/** The word the reader typed when they placed it with Alt, or empty. Drawn beside the paw and
		kept in the page's script label with the rest of the stamp.
		⚠Empty is the ordinary case -- a plain press has no word -- so nothing may assume one. */
	PMString fText;

	KCMPawStamp() : fPageUID(kInvalidUID), fColour(kKCMPawColourRed) {}
	KCMPawStamp(UID p, const PMReal& x, const PMReal& y, int32 colour)
		: fPageUID(p), fX(x), fY(y), fColour(colour) {}
	KCMPawStamp(UID p, const PMReal& x, const PMReal& y, int32 colour, const PMString& text)
		: fPageUID(p), fX(x), fY(y), fColour(colour), fText(text) {}
};

/** Turn a colour number READ FROM A DOCUMENT into one this build draws.

	★**The one place that knows the retired numbers.** Paws saved before 2026-09-07 carry 0 (pink),
	 1 (cyan) or 2 (green); those three colours are gone, and the user's ruling is pink and cyan
	 read as BLUE and green as RED. Anything unrecognised reads as red, the default -- a stamp
	 whose colour cannot be understood is still a stamp, and dropping it would lose the reader's
	 work over a number.
	⚠Applied at READ time only. Nothing writes an old number again, so a document rewrites itself
	 into the new numbering the first time any of its marks is touched. */
inline int32 KCMPawColourFromStored(int32 stored)
{
	switch (stored)
	{
		case 0:						// retired: pink, what a plain press used to be
		case 1:						// retired: cyan, what Alt used to place
			return kKCMPawColourBlue;
		case 2:						// retired: green, what Shift+Alt used to place
			return kKCMPawColourRed;
		case kKCMPawColourBlue:
			return kKCMPawColourBlue;
		default:
			return kKCMPawColourRed;
	}
}

/** Place a paw at (x, y) on that page, in one of the two colours (a KCMPawColour; Shift+Alt swaps
	which one the tool is holding). baseHalf is half the page's paw size, the same value the lift
	takes.
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
/** @param text the word to put beside it, or empty for none (an Alt press asks the reader for one;
	  every other press passes nothing). */
bool16 KCMPawStampPlaceAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                          int32 colour, const PMReal& baseHalf, const PMString& text);

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

/** Drop every stamp on ONE page -- Shift + DOUBLE click (2026-09-07, the user's request).
	The page's tick is left exactly as it was: this clears paws, and a write says what the whole
	page carries afterwards, so the tick has to be carried along rather than merely not mentioned.
	@return how many paws went (0 for a page carrying none, which is not a failure). */
int32 KCMPawStampClearPage(IDataBase* db, UID pageUID);

/** The liveness sweep run after documents close: drops the entries of documents that have gone.
	@warning a closed database is never dereferenced -- this compares pointers through
	  KCMIsDocDBOpen and nothing more ([[uidref-reuse-after-close]]). */
void KCMPawStampSweepClosedDocs();

#endif // __KCMPawStamp_h__

// End, KCMPawStamp.h.
