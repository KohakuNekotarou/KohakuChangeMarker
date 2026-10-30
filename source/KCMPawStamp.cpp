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
#include "KCMPageMarksCmd.h"	// KCMPageMarks / KCMMarksWriteForReader -- the reader's door to a change
#include "KCMPageMarksDoc.h"	// KCMMarksSyncFromDocument / KCMPageMarksNow -- what the document says (a page's tick comes along)
#include "KCMThreadSafety.h"	// KCMIsSameDoc / KCMMarkStateLock / KCMMarkStateMutex

#include <map>
#include <set>

typedef std::map<IDataBase*, std::vector<KCMPawStamp> > KCMPawMap;

// The stamps, per document, for the session.
//
// ★★★**THIS IS A CACHE, NOT THE TRUTH** (2026-09-07). The paws live in the document, as script
//   labels on the pages that carry them (KCMPageMarksDoc.h). What is held here is a copy of them,
//   kept because the drawing side has to answer "any paws on this page" once per page per draw, on
//   background threads as well as the main one.
// ⚠**NOTHING IN THIS FILE MAY WRITE IT.** The single writer is KCMMarksObserver, which refills it
//   from the labels whenever they change -- on the way in, and again on undo and redo. The
//   placing and lifting below therefore READ this map and then ask the command to write the
//   document, and the observer refills it.
//   The two exceptions are KCMPawStampReplaceAll (which IS the observer's write) and
//   KCMPawStampSweepClosedDocs (a closed document has no labels left to read).
// ⚠**The refill is NOT deferred to an idle** (measured 2026-09-07): it has already happened by the
//   next statement after KCMMarksWrite returns. Read anything you need out of this map BEFORE the
//   write rather than betting either way on the timing.
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

	// Only the clone this thread exports is looked for by identity (KCMIsExportCloneOnThisThread says why: a placed
	//   copy of the document names the same file, and its paws printed in the other document's PDF).
	if (!KCMIsExportCloneOnThisThread(db))
		return sPaws.end();
	for (it = sPaws.begin(); it != sPaws.end(); ++it)
	{
		if (KCMIsSameDoc(it->first, db))
			return it;					// the export's clone is caught here
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

bool16 KCMPawStampIsAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y, const PMReal& baseHalf)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;

	KCMMarkStateLock lock(KCMMarkStateMutex());
	KCMPawMap::const_iterator entry = sPaws.find(db);	// by pointer: the tool's question about the document in front
	return (entry != sPaws.end() && KCMPawIndexAt(entry->second, pageUID, x, y, baseHalf) >= 0) ? kTrue : kFalse;
}

bool16 KCMPawStampPlaceAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                          int32 colour, const PMReal& baseHalf, const PMString& text,
                          const PMReal& angle)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;

	// This page's paws as they stand. Read under the lock and COPIED OUT: the write below runs a
	// command, and a command must never run while this file holds the store's mutex.
	std::vector<KCMPawStamp> onPage;
	{
		KCMMarkStateLock lock(KCMMarkStateMutex());

		// ★★NO STACKING (the user's request). Two paws on one spot look like one, and only the top
		//   one comes off when Shift is pressed, so the second press is far likelier to be a slip
		//   than an intention. The test is the very one the lift uses.
		// ⚠sPaws.find, NOT sPaws[db]: operator[] would create an EMPTY entry for a document that
		//   gets nothing placed, and an empty entry is exactly what KCMPawStampHasAny reads as
		//   "this document has paws" -- the drawing side would then walk every page for nothing.
		KCMPawMap::iterator entry = sPaws.find(db);
		if (entry != sPaws.end())
		{
			if (KCMPawIndexAt(entry->second, pageUID, x, y, baseHalf) >= 0)
				return kFalse;			// one is already there

			for (size_t i = 0; i < entry->second.size(); ++i)
				if (entry->second[i].fPageUID == pageUID)
					onPage.push_back(entry->second[i]);
		}
	}

	// ★★IT ONLY EVER ADDS. This was a toggle for one day (2026-09-04) and the user found the fault
	//   in it within minutes of first use: putting paws down in a row, the second press near the
	//   first took the first one off. Placing and lifting are two intentions, so they are two
	//   gestures -- plain press and Shift + press.
	onPage.push_back(KCMPawStamp(pageUID, x, y, colour, text, angle));

	// ★The paw goes into the DOCUMENT, and the store catches up when the notification arrives.
	//  The page as it stands (KCMPageMarksNow - its tick comes along), with these paws.
	std::vector<KCMPageMarks> wanted;
	wanted.push_back(KCMPageMarksNow(db, pageUID));
	wanted.back().fPaws = onPage;
	return (KCMMarksWriteForReader(db, wanted, "Place Cat Paw") == kSuccess) ? kTrue : kFalse;
}

bool16 KCMPawStampLiftAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                         const PMReal& baseHalf)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;

	// The same read-then-write shape as the placing above, and for the same reason.
	std::vector<KCMPawStamp> onPage;
	{
		KCMMarkStateLock lock(KCMMarkStateMutex());

		KCMPawMap::iterator entry = sPaws.find(db);	// by pointer: a lift is a main-thread request
		if (entry == sPaws.end())
			return kFalse;

		const int32 i = KCMPawIndexAt(entry->second, pageUID, x, y, baseHalf);
		if (i < 0)
			return kFalse;				// the press landed on no paw

		// Everything on this page except the one that was hit. ⚠The index is into the DOCUMENT'S
		//   vector, not into a per-page one, so the comparison has to be made there.
		for (size_t k = 0; k < entry->second.size(); ++k)
		{
			if (entry->second[k].fPageUID == pageUID && (int32)k != i)
				onPage.push_back(entry->second[k]);
		}
	}

	std::vector<KCMPageMarks> wanted;
	wanted.push_back(KCMPageMarksNow(db, pageUID));		// its tick comes along
	wanted.back().fPaws = onPage;		// empty is meaningful: it takes our paws label off the page
	return (KCMMarksWriteForReader(db, wanted, "Lift Cat Paw") == kSuccess) ? kTrue : kFalse;
}

//========================================================================================
// The paw being placed (2026-10-27) -- drawn while the button is down, kept nowhere
//========================================================================================

// ★ONE SLOT, because one tool in one hand places one paw at a time. It is not a stamp in sPaws: those are the
//   document's, written by a command and counted; this one is a picture of the press in progress.
// ⚠Its database and view are compared and never dereferenced -- the press can outlive neither, but the drawing
//   side reads this on every draw, and a comparison is all it needs.
static bool16				sPreviewUp = kFalse;
static IDataBase*			sPreviewDB = nil;
static const IControlView*	sPreviewView = nil;
static KCMPawStamp			sPreview;

void KCMPawPreviewSet(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y, int32 colour,
                      const PMReal& angle, const IControlView* view)
{
	KCMMarkStateLock lock(KCMMarkStateMutex());
	sPreviewUp   = (db != nil && pageUID != kInvalidUID && view != nil) ? kTrue : kFalse;
	sPreviewDB   = db;
	sPreviewView = view;
	sPreview     = KCMPawStamp(pageUID, x, y, colour, PMString(), angle);
}

void KCMPawPreviewClear()
{
	KCMMarkStateLock lock(KCMMarkStateMutex());
	sPreviewUp   = kFalse;
	sPreviewDB   = nil;
	sPreviewView = nil;
}

bool16 KCMPawPreviewOn(IDataBase* db, UID pageUID, const IControlView* view, KCMPawStamp& out)
{
	KCMMarkStateLock lock(KCMMarkStateMutex());
	// ★By POINTER, with no fall-back on file identity, unlike the readers below: the preview belongs to the screen
	//   of the document being pressed, and a background thread's clone is exactly what must not draw it.
	if (!sPreviewUp || db != sPreviewDB || view != sPreviewView || pageUID != sPreview.fPageUID)
		return kFalse;
	out = sPreview;
	return kTrue;
}

bool16 KCMPawPreviewFor(IDataBase* db, const IControlView* view)
{
	KCMMarkStateLock lock(KCMMarkStateMutex());
	return (sPreviewUp && db == sPreviewDB && view == sPreviewView) ? kTrue : kFalse;
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
// The store as a whole, for KCMMarksSyncFromDocument (KCMPageMarksDoc.cpp)
//========================================================================================

void KCMPawStampGetForSave(IDataBase* db, std::vector<KCMPawStamp>& out)
{
	out.clear();
	if (db == nil)
		return;

	KCMMarkStateLock lock(KCMMarkStateMutex());
	// ⚠By pointer, deliberately: saving is a main-thread request about the document in front of
	//   the reader. The file-identity fallback exists for the background thread's clone, which
	//   never saves.
	KCMPawMap::const_iterator it = sPaws.find(db);
	if (it != sPaws.end())
		out = it->second;
}

void KCMPawStampReplaceAll(IDataBase* db, const std::vector<KCMPawStamp>& in)
{
	if (db == nil)
		return;

	KCMMarkStateLock lock(KCMMarkStateMutex());
	// REPLACE, not merge: the document is the answer, and merging would double every paw at the
	//   next sync.
	if (in.empty())
		sPaws.erase(db);		// keeps the promise that an entry which exists is not empty
	else
		sPaws[db] = in;
}

//========================================================================================
// Forgetting
//========================================================================================

int32 KCMPawStampClearDoc(IDataBase* db)
{
	if (db == nil)
		return 0;

	// ⚠THE STORE IS BROUGHT BACK TO WHAT THE DOCUMENT SAYS FIRST (KCMMarksSyncFromDocument). It goes on
	//   naming a page after the page is deleted, and the write below fails as a whole on a page that is
	//   gone - the paws stayed, and the status line said how many had come off (measured).
	KCMMarksSyncFromDocument(db, nil, nil);

	// Which pages carry a paw, and how many paws there are. ★By pointer, deliberately: "clear the
	//   document in front of me" is a main-thread request about one document, the same reasoning as rule 3.
	std::set<UID> pages;
	int32 paws = 0;
	{
		KCMMarkStateLock lock(KCMMarkStateMutex());
		KCMPawMap::const_iterator entry = sPaws.find(db);
		if (entry == sPaws.end())
			return 0;
		paws = (int32)entry->second.size();
		for (size_t i = 0; i < entry->second.size(); ++i)
			pages.insert(entry->second[i].fPageUID);
	}

	// Each of those pages as it stands (KCMPageMarksNow - its tick comes along), losing its paws.
	//   One command, so one undo step.
	std::vector<KCMPageMarks> wanted;
	for (std::set<UID>::const_iterator it = pages.begin(); it != pages.end(); ++it)
	{
		wanted.push_back(KCMPageMarksNow(db, *it));
		wanted.back().fPaws.clear();
	}

	return (KCMMarksWriteForReader(db, wanted, "Clear Cat Paws") == kSuccess) ? paws : -1;
}

int32 KCMPawStampClearPage(IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return 0;

	// How many are there to lose. ★Counted BEFORE the write, because afterwards nothing can say --
	//   the same reason the tick's own clear reads its page set first.
	std::vector<KCMPawStamp> onPage;
	KCMPawStampsOnPage(db, pageUID, onPage);
	if (onPage.empty())
		return 0;						// nothing here, and that is not a failure

	// The page as it stands (KCMPageMarksNow - its TICK comes along, which this gesture never means
	//   to take off), with no paws.
	std::vector<KCMPageMarks> wanted;
	wanted.push_back(KCMPageMarksNow(db, pageUID));
	wanted.back().fPaws.clear();

	if (KCMMarksWriteForReader(db, wanted, "Clear Cat Paws on Page") != kSuccess)
		return -1;						// none went (the sequence rolled it back); the door said so

	return (int32)onPage.size();
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
