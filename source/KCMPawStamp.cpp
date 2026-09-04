//========================================================================================
//
//  KCMPawStamp.cpp
//
//  The cat-paw stamps, held per document for the session. What each entry point promises is in
//  KCMPawStamp.h; what is written out here is why the container behaves the way it does.
//
//  ★★IT IS BUILT TO KCMDocUidSet'S RULES. That class holds the registered pages and the ticks
//    ("document -> set of page UIDs"); this holds "document -> stamps with coordinates", so the
//    value type differs and the container could not be shared -- but the three rules that make
//    that one safe are followed here line for line, because each of them was learnt the hard way:
//
//      1. READERS TAKE THE LOCK. Draw events reach a kModelPlugIn on background threads too
//         (measured -- KCMThreadSafety.h), so a reader can run while the tool is writing.
//      2. READERS FALL BACK ON FILE IDENTITY. A background thread is handed a CLONE of the
//         database with a different pointer, so a pointer-only lookup misses every time and the
//         paws would be missing from an exported PDF -- which is exactly what happened to the
//         green "/" and the ticks before KCMDocUidSet::FindDoc existed.
//      3. WRITERS DO NOT. A write happens on the main thread and means "add to the document in
//         front of me"; growing a clone's entry would be a wrong document, not a rescue.
//
//  ⚠A CLOSED DATABASE IS NEVER DEREFERENCED. The key is compared and nothing else
//    ([[uidref-reuse-after-close]]: a closed document's pointer gets reused).
//  ⚠An entry whose vector became empty is dropped at once, which keeps the sweep and the
//    "does this document have any" test cheap -- KCMDocUidSet's rule, and the reason
//    KCMPawStampHasAny can answer by existence alone.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IGeometry.h"
#include "IGeometryFacade.h"	// GetItemBounds -- the page's own rectangle, to size a paw by
#include "PMRect.h"
#include "UIDRef.h"
#include "Utils.h"

#include "KCMPawStamp.h"
#include "KCMConstants.h"		// kKCMPawSizeRatio
#include "KCMCore.h"			// KCMIsDocDBOpen (the liveness test, pointer comparison only)
#include "KCMThreadSafety.h"	// KCMIsSameDoc / KCMMarkStateLock / KCMMarkStateMutex

#include <map>

typedef std::map<IDataBase*, std::vector<KCMPawStamp> > KCMPawMap;

// The stamps, session only. Never written to a document file; Load and Save (Task 4) move them
// through KCM's own JSON.
static KCMPawMap sPaws;

// The entry for db, falling back on file identity when the pointer misses -- the shape of
// KCMDocUidSet::FindDoc, for rule 2 above.
// @warning readers only.
static KCMPawMap::iterator KCMPawFindDoc(IDataBase* db)
{
	if (db == nil)
		return sPaws.end();

	KCMPawMap::iterator it = sPaws.find(db);
	if (it != sPaws.end())
		return it;						// the main thread's ordinary route settles here

	for (it = sPaws.begin(); it != sPaws.end(); ++it)
	{
		if (KCMIsSameDoc(it->first, db))
			return it;					// a background thread's clone is caught here
	}
	return sPaws.end();
}

//========================================================================================
// Placing and lifting
//========================================================================================

// Which paw is under (x, y) on that page, searched from the most recently placed -- or -1 for
// none.
//
//  ★★ONE PLACE ANSWERS "IS THERE A PAW HERE", and BOTH gestures ask it: Shift lifts the paw it
//    names, and a plain press refuses to stack on the paw it names. Written twice, the two tests
//    would drift, and the drift would read as "it says one is there but Shift will not take it
//    off" ([[one-question-one-place]]).
//  ★THE REACH IS A SQUARE -- the paw's own bounding box. It was made a circle for a few minutes on
//    2026-09-04 and the user chose the square, having been told the difference: a box's corner
//    sits 1.41 times the radius from the centre, so the square is the more forgiving of the two
//    and a press a little wide of a paw still finds it. For a mark you drop by hand and take off
//    by hand, forgiving is the right way to be wrong.
//  ★Every paw is the same size, so one half-size serves them all. (It was per-stamp while the
//    modifier keys changed the SIZE; they choose the COLOUR now, and colour has no reach.)
//  ★Backwards, so where paws overlap the one placed LAST is found: that makes repeated pressing
//    behave like undo rather than a lottery.
static int32 KCMPawIndexAt(const std::vector<KCMPawStamp>& v, UID pageUID,
                           const PMReal& x, const PMReal& y, const PMReal& baseHalf)
{
	for (int32 i = (int32)v.size() - 1; i >= 0; --i)
	{
		if (v[i].fPageUID != pageUID)
			continue;

		const PMReal dx = v[i].fX - x;
		const PMReal dy = v[i].fY - y;
		if (dx >= -baseHalf && dx <= baseHalf && dy >= -baseHalf && dy <= baseHalf)
			return i;
	}
	return -1;
}

bool16 KCMPawStampPlaceAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                          int32 colour, const PMReal& baseHalf)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;

	KCMMarkStateLock lock(KCMMarkStateMutex());

	// ★★NO STACKING (the user's request). Two paws on one spot look like one, and only the top one
	//   comes off when Shift is pressed, so the second press is far likelier to be a slip than an
	//   intention. The test is the very one the lift uses.
	// ⚠sPaws.find, NOT sPaws[db]: operator[] would create an EMPTY entry for a document that gets
	//   nothing placed, and an empty entry is exactly what KCMPawStampHasAny reads as "this
	//   document has paws" -- the drawing side would then walk every page of it for nothing.
	KCMPawMap::iterator entry = sPaws.find(db);
	if (entry != sPaws.end() && KCMPawIndexAt(entry->second, pageUID, x, y, baseHalf) >= 0)
		return kFalse;					// one is already there

	// ★The write goes to THIS db. No fallback on file identity (rule 3).
	// ★★AND IT ONLY EVER ADDS. This was a toggle for one day (2026-09-04) and the user found the
	//   fault in it within minutes of first use: putting paws down in a row, the second press near
	//   the first took the first one off. Placing and lifting are two intentions, so they are two
	//   gestures -- plain press and Shift + press.
	sPaws[db].push_back(KCMPawStamp(pageUID, x, y, colour));
	return kTrue;
}

bool16 KCMPawStampLiftAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                         const PMReal& baseHalf)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;

	KCMMarkStateLock lock(KCMMarkStateMutex());

	KCMPawMap::iterator entry = sPaws.find(db);		// by pointer: a lift is a main-thread request
	if (entry == sPaws.end())
		return kFalse;
	std::vector<KCMPawStamp>& v = entry->second;

	const int32 i = KCMPawIndexAt(v, pageUID, x, y, baseHalf);
	if (i < 0)
		return kFalse;					// the press landed on no paw

	v.erase(v.begin() + i);
	if (v.empty())
		sPaws.erase(entry);				// an emptied entry goes at once
	return kTrue;
}

//========================================================================================
// Readers (background threads reach these)
//========================================================================================

void KCMPawStampsOnPage(IDataBase* db, UID pageUID, std::vector<KCMPawStamp>& out)
{
	out.clear();
	if (db == nil || pageUID == kInvalidUID)
		return;

	KCMMarkStateLock lock(KCMMarkStateMutex());
	KCMPawMap::const_iterator it = KCMPawFindDoc(db);
	if (it == sPaws.end())
		return;

	for (size_t i = 0; i < it->second.size(); ++i)
	{
		if (it->second[i].fPageUID == pageUID)
			out.push_back(it->second[i]);
	}
}

bool16 KCMPawStampHasAny(IDataBase* db)
{
	if (db == nil)
		return kFalse;

	KCMMarkStateLock lock(KCMMarkStateMutex());
	// Existence is the whole answer: an emptied entry is dropped when it empties, so an entry
	// that is here is an entry with something in it.
	return (KCMPawFindDoc(db) != sPaws.end()) ? kTrue : kFalse;
}

int32 KCMPawStampCount(IDataBase* db)
{
	if (db == nil)
		return 0;

	KCMMarkStateLock lock(KCMMarkStateMutex());
	KCMPawMap::const_iterator it = KCMPawFindDoc(db);
	return (it == sPaws.end()) ? 0 : (int32)it->second.size();
}

//========================================================================================
// The one place the size comes from
//========================================================================================

PMReal KCMPawHalfSizeForPage(IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return PMReal(0.0);

	// ★No document lookup here, and none is needed: a UID survives the cloning (measured --
	//   KCMThreadSafety.h), so a background thread's clone measures the very same page.
	// ⚠Keep the IGeometry query and its nil test: whether this UID has geometry at all is not the
	//   facade's guarantee (the same pairing as KCMQueryPageRect in KCMDrawEventHandler.cpp).
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return PMReal(0.0);

	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// The rectangle is not promised normalised, so the sides are taken as magnitudes. Which
	// coordinate space it came in does not matter here -- a width is a width.
	const PMReal w = (pr.Width()  < PMReal(0.0)) ? -pr.Width()  : pr.Width();
	const PMReal h = (pr.Height() < PMReal(0.0)) ? -pr.Height() : pr.Height();
	const PMReal shortSide = (w < h) ? w : h;

	return shortSide * kKCMPawSizeRatio / PMReal(2.0);
}

//========================================================================================
// Forgetting
//========================================================================================

void KCMPawStampClearDoc(IDataBase* db)
{
	if (db == nil)
		return;

	KCMMarkStateLock lock(KCMMarkStateMutex());
	// ★By pointer, deliberately: "clear the document in front of me" is a main-thread request
	//   about one document, the same reasoning as rule 3.
	sPaws.erase(db);
}

void KCMPawStampSweepClosedDocs()
{
	// @warning this reasons "not in the document list, therefore closed", which **does not hold on
	//   a background thread** (it sees a different database). The single caller,
	//   KCMHandleDocsClosed, returns early unless it is on the main thread, so the test is not
	//   repeated here ([[one-question-one-place]]).
	KCMMarkStateLock lock(KCMMarkStateMutex());

	KCMPawMap::iterator it = sPaws.begin();
	while (it != sPaws.end())
	{
		if (!KCMIsDocDBOpen(it->first))
			it = sPaws.erase(it);		// the key is compared, never dereferenced
		else
			++it;
	}
}

// End, KCMPawStamp.cpp.
