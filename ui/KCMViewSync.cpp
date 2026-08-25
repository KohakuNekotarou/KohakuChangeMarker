//========================================================================================
//
//  KCMViewSync.cpp
//
//  Layout view synchronisation: the engine itself, the caches on its hot path, the flyout toggle
//  (Sync Layout Views), the one-shot action (Align Other Views to Active), and the observer that
//  subscribes to the panoramas.
//
//  ★The armed state (sPeekArmed / sPeekTargetDB / sPeekSourceDB) stays on the model side in
//    KCMPeek.cpp, so it is read from here **through IKCMCompareFacade** (IsArmed /
//    GetArmedTargetDB / GetArmedSourceDB).
//    ⚠**Free functions of the same name still exist on the model side** -- KCMCore.h declares
//    KCMIsArmed and KCMPeek.cpp defines it among the panel state accessors -- so never write that
//    this file calls them: it would read as the UI calling into the model ＝ the one-way dependency
//    the whole split rests on being broken.
//
//  UI side: it works on IControlView and IPanorama, which a model plug-in must not depend on.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// The object model:
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IGeometry.h"
#include "IDocument.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "ISession.h"
#include "ILayoutViewUtils.h"		// GetAllLayoutViews -- both panes of a split window come back
#include "ILayoutUIUtils.h"			// MakeZoomCmd (kZoomToCmdBoss) -- the zoom half of a viewport copy
#include "IPasteboardUtils.h"		// QuerySpread / QueryNearestSpread -- the official route to the page at a view's centre
#include "CmdUtils.h"				// ProcessCommand -- running the zoom command
#include "ICommand.h"

// For the layout view synchronisation (Sync Layout Views):
#include "CObserver.h"				// the base of the sync observer
#include "ISubject.h"				// AttachObserver / DetachObserver / IsAttached
#include "IActiveContext.h"			// IID_IACTIVECONTEXT / ContextInfo -- noticing a document switch
#include "widgetid.h"				// IID_IPANORAMA / kScrollToMessage / kScrollByMessage / kScaleToMessage / kScaleByMessage

// Geometry and views:
#include "IControlView.h"
#include "IPanorama.h"
#include "PMPoint.h"
#include "PMRect.h"					// a page's pasteboard rectangle (the add/remove correction)
#include "PMReal.h"
#include "IGeometryFacade.h"		// GetItemBounds -- a page rectangle in pasteboard coordinates (the product does the same in SnapTracker.cpp:610-616)
#include "IHierarchy.h"				// GetSpreadUID -- the spread the destination page sits on

#include "ErrorUtils.h"				// PMSetGlobalErrorCode / GlobalErrorStatePreserver -- a failed zoom is neither carried forward nor let out

#include <algorithm>			// std::find -- the linear search behind the master-page test
#include <map>
#include <vector>
#include <chrono>				// steady_clock -- the caches' TTL

// The plug-in's own headers:
#include "KCMUIID.h"
#include "Utils.h"                   // Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"     // the armed state
#include "KCMViewLookup.h"         // KCMFindDocDbForView / KCMQueryPanorama
#include "IKCMMarkData.h"          // GetPagePairing / GetMasterPagePairing -- the exclusion pairing
#include "KCMChangeNav.h"          // KCMEnsureViewShowsSpread -- put the destination view on the partner spread
#include "KCMViewSync.h"

//========================================================================================
// The viewport sync engine (shared)
//   It copies the model view's "what is on screen" -- the effective zoom (GetXScaleFactor(kTrue),
//   monitor PPI included, the same dimension as kZoomToCmdBoss's scaleFactor) plus the content
//   coordinate at the centre of the frame -- onto the layout views of other documents. Views of the
//   **same** document, the split sibling included, are never destinations.
//   Both the one-shot action and the automatic "Sync Layout Views" toggle go through this one
//   function.
//   ★★**Which documents are destinations depends on whether a comparison is armed, and there are
//   two modes** -- the full statement is on KCMSyncOtherDocViewportsTo itself:
//     - armed     ... Target <-> Source only, with the page add/remove correction. A third
//                     document as the model is refused at the top of the function.
//     - not armed ... every other open document follows the one that was operated, with no
//                     correction (the user's decision).
//   ⚠Do not write "syncing only happens while armed" here: that was true until the second mode was
//   added, and it makes the whole of that branch read as dead code.
//========================================================================================

// The re-entry guard. Copying raises kScaleTo / kScrollTo notifications in the destination views,
// which the sync observer would pick up and copy back (a ping-pong that never ends). kTrue for the
// duration of the copy loop only.
static bool16 sLayoutSyncBroadcasting = kFalse;

// (KCMFindDocDbForView is **declared in KCMViewLookup.h** -- see the include above. ⚠Not in
//  KCMCore.h: that header is model side and cannot be included from ui/ at all.)

//========================================================================================
// ★The short-lived caches on the sync hot path
//
//   Sync Layout Views is notified **every time any view scrolls or zooms**, which during a scroll
//   drag is dozens of times a second. Without these caches each of those notifications redid
//     - the walk of every page in the document (ISpreadList -> ISpread -> GetNthPageUID)
//     - an IGeometry query and an InnerToPasteboardMatrix per page (matrix work proportional to the
//       page count)
//     - a rebuild of the exclusion pairing (KCMBuildPairing = walk both documents, test each page
//       for registration)
//   so the cost per notification grew with the document, and the longer the document the heavier
//   the following. None of it changes while a scroll is in progress, so it is remembered briefly
//   and reused.
//
//   Invalidation has two halves:
//     (1) explicit ... KCMInvalidateSyncCaches() (arm/disarm, the toggle going off, a document
//         closing, shutdown)
//     (2) time     ... kKCMSyncCacheTtlMs (250ms) and the generation lapses. This is what follows a
//         page being added or removed, or a spread being hidden or shown again -- stop scrolling
//         for a quarter of a second and it is rebuilt.
//
//   ★Using a stale cache cannot break anything: the only thing that can be wrong is **the
//     following view's scroll position**, and the next notification or the next 250ms puts it
//     right. The db pointers are compared, never dereferenced.
//========================================================================================
static const long long kKCMSyncCacheTtlMs = 250;

// One document's "page UIDs and their pasteboard rectangles". pages and rects run in step.
// A page whose geometry could not be read gets an empty rectangle (width and height 0), which the
// tests below drop of their own accord.
// ★pages is in two parts -- **the ordinary pages (in spread order) followed by the master pages (in
//   master-spread order)**, the boundary being normalCount. The masters are in the same table so
//   that the sync can look a master page's rectangle up at all.
//   ⚠**The nearest-page search must not use the second part**: a master spread lives in its own
//   coordinate space centred on the origin, so its rectangles can overlap the ordinary pages', and
//   mixing them in lets a master come out "nearest" while an ordinary spread is on screen.
struct KCMPageRectCache
{
	IDataBase*          db;
	std::vector<UID>    pages;
	std::vector<PMRect> rects;
	size_t              normalCount;	// pages[0..normalCount) = ordinary pages / [normalCount..) = master pages
	KCMPageRectCache() : db(nil), normalCount(0) {}
};
// Two slots are enough: while armed, the sync only ever runs between Target and Source.
static KCMPageRectCache sPageRectCache[2];

// The exclusion pairing (the registered pages dropped, the rest matched in order) as maps both
// ways. They belong to the armed (Target, Source) pair.
static std::map<UID, UID> sSyncPairT2S;
static std::map<UID, UID> sSyncPairS2T;
static IDataBase* sSyncPairTargetDB = nil;
static IDataBase* sSyncPairSourceDB = nil;
static bool16     sSyncPairBuilt    = kFalse;

// The model view's state as of the last copy. Where the same state is notified again, the whole
// copy is skipped (see Update below). The pointer is only ever compared, never dereferenced.
static IPanorama* sLastSrcPano      = nil;
static PMReal     sLastSrcZoom      = 0.0;
static PBPMPoint  sLastSrcCenter;
static bool16     sHaveLastSrcState = kFalse;

static bool16                                  sSyncCacheValid = kFalse;
static std::chrono::steady_clock::time_point   sSyncCacheStamp;

// Throw the whole sync cache away (declared in KCMViewSync.h).
void KCMInvalidateSyncCaches()
{
	sSyncCacheValid = kFalse;
	for (int i = 0; i < 2; ++i)
	{
		sPageRectCache[i].db = nil;
		sPageRectCache[i].pages.clear();
		sPageRectCache[i].rects.clear();
	}
	sSyncPairT2S.clear();
	sSyncPairS2T.clear();
	sSyncPairTargetDB = nil;
	sSyncPairSourceDB = nil;
	sSyncPairBuilt    = kFalse;
	sLastSrcPano      = nil;
	sHaveLastSrcState = kFalse;
	// (There is no view -> db hint to forget any more: that hint went with KCMFindDocDbForView's
	//  fallback, and it now asks the view itself every time.)
}

// Called at the entrance of one notification. Past the TTL the cache is thrown away and a new
// generation starts.
static void KCMSyncCacheBeginTick()
{
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (sSyncCacheValid)
	{
		const long long ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - sSyncCacheStamp).count();
		if (ms < kKCMSyncCacheTtlMs)
			return;			// still current
		KCMInvalidateSyncCaches();
	}
	sSyncCacheValid = kTrue;	// begin a new generation
	sSyncCacheStamp = now;
}

// The page-rectangle table for db, built on first use. nil for a nil db.
static const KCMPageRectCache* KCMGetPageRects(IDataBase* db);

//----------------------------------------------------------------------------------------
// The pasteboard rectangle of page pageUID in db. That is the same space as a panorama's content
// coordinates (PBPMPoint). Pages do not rotate, so transforming two corners of the inner bbox and
// taking the min/max is enough.
//----------------------------------------------------------------------------------------
static bool16 KCMPagePasteboardRectRaw(IDataBase* db, UID pageUID, PMRect& outPB)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IGeometry> geo(db, pageUID, UseDefaultIID());
	if (geo == nil)
		return kFalse;
	// ★Getting a page rectangle in pasteboard coordinates is the facade's job, and the product does
	//   exactly this **to a page** in snapshot/SnapTracker.cpp:610-616.
	//   ★The nil test above and the min/max below stay: the facade guarantees neither "this thing has
	//   geometry" nor "the rectangle comes back normalised" (the hand-written version happened to do
	//   both). This route measures once and caches, so going through the facade costs the sync hot
	//   path nothing.
	const PMRect pb = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		::GetUIDRef(geo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
	const PMReal l = (pb.Left() < pb.Right()) ? pb.Left() : pb.Right();
	const PMReal r = (pb.Left() < pb.Right()) ? pb.Right() : pb.Left();
	const PMReal t = (pb.Top()  < pb.Bottom()) ? pb.Top()  : pb.Bottom();
	const PMReal b = (pb.Top()  < pb.Bottom()) ? pb.Bottom() : pb.Top();
	outPB = PMRect(l, t, r, b);
	return kTrue;
}

//----------------------------------------------------------------------------------------
// The page-rectangle table for db, from the cache, measuring every page once where it is not built
// yet.
// ★This is what takes the heavy part off the sync hot path: without it, every notification queried
//   IGeometry and InnerToPasteboardMatrix for every page again. There are only two slots because
//   while armed the sync runs between Target and Source alone; anything else evicts the older slot
//   and rebuilds ＝ at worst, the work it used to do anyway.
//----------------------------------------------------------------------------------------
static const KCMPageRectCache* KCMGetPageRects(IDataBase* db)
{
	if (db == nil)
		return nil;
	for (int i = 0; i < 2; ++i)
		if (sPageRectCache[i].db == db)
			return &sPageRectCache[i];

	// Take a free slot; where both are taken, rebuild slot 0.
	const int slot = (sPageRectCache[0].db == nil) ? 0 : ((sPageRectCache[1].db == nil) ? 1 : 0);
	KCMPageRectCache& c = sPageRectCache[slot];
	c.db = db;
	c.pages.clear();
	c.rects.clear();
	// ★Two calls in a row, so the interface is queried once and kept (Utils.h:74-80 -- the same shape
	// as KCMEnsureSyncPairing below).
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	marks->GetAllPageUIDs(db, c.pages);
	c.normalCount = c.pages.size();
	marks->GetMasterPageUIDs(db, c.pages);	// ★the masters go on the end (the boundary is normalCount)
	c.rects.resize(c.pages.size());
	for (size_t i = 0; i < c.pages.size(); ++i)
	{
		if (!KCMPagePasteboardRectRaw(db, c.pages[i], c.rects[i]))
			c.rects[i] = PMRect(0, 0, 0, 0);	// no geometry ＝ an empty rectangle, which the tests below drop
	}
	return &c;
}

//----------------------------------------------------------------------------------------
// The pasteboard rectangle of page pageUID in db, through the cache. This is what the sync path
// uses. An empty rectangle (a page whose geometry could not be read) answers kFalse, which the
// callers treat as "could not get it".
//----------------------------------------------------------------------------------------
static bool16 KCMPagePasteboardRect(IDataBase* db, UID pageUID, PMRect& outPB)
{
	if (pageUID == kInvalidUID)
		return kFalse;
	const KCMPageRectCache* c = KCMGetPageRects(db);
	if (c == nil)
		return kFalse;
	for (size_t i = 0; i < c->pages.size(); ++i)
	{
		if (c->pages[i] != pageUID)
			continue;
		const PMRect& r = c->rects[i];
		if (r.Right() <= r.Left() && r.Bottom() <= r.Top())
			return kFalse;	// empty ＝ a page whose geometry could not be read
		outPB = r;
		return kTrue;
	}
	return kFalse;
}

//----------------------------------------------------------------------------------------
// The UID of the page in db that contains the pasteboard point pb; where none does, the page whose
// centre is nearest (the view centre is in the gap between pages, or out on the pasteboard).
// kInvalidUID where the document has no pages. The rectangles come from the cache, so no
// notification measures every page again.
//----------------------------------------------------------------------------------------
static UID KCMFindPageAtPasteboard(IDataBase* db, const PBPMPoint& pb)
{
	const KCMPageRectCache* c = KCMGetPageRects(db);
	if (c == nil)
		return kInvalidUID;
	UID best = kInvalidUID;
	PMReal bestDist2(0);
	bool16 haveBest = kFalse;
	// ★Ordinary pages only. A master spread lives in its own coordinate space centred on the origin,
	//   so its rectangles can overlap the ordinary pages'; mixing them in produces "an ordinary page
	//   is on screen but the nearest is a master". **Whether a master page is being pointed at cannot
	//   be decided from the point** -- it follows from which spread is on screen -- so that decision
	//   is left to the caller (KCMCorrectedCenterForDoc), which gets it from the SDK.
	for (size_t i = 0; i < c->normalCount; ++i)
	{
		const PMRect& r = c->rects[i];
		if (r.Right() <= r.Left() && r.Bottom() <= r.Top())
			continue;	// empty ＝ a page whose geometry could not be read
		// ★Containment is PMRect::PointIn (PMRect.h:250) ＝ the official one. The product hit-tests
		//   pages with it in snapshot/SnapTracker.cpp:600 and :617, **immediately after the :610-616
		//   that KCMPagePasteboardRectRaw above quotes for the rectangle itself**.
		//   ⚠PointIn is a plain closed-interval comparison, so it answers kFalse for every point of a
		//   box that is not normalised. The rectangles in this table are: they come from
		//   KCMPagePasteboardRectRaw, which takes the min/max. ⇒ no Normalize() is needed here.
		if (r.PointIn(pb))
			return c->pages[i];	// a page that contains it settles the answer
		const PMReal cx = (r.Left() + r.Right()) / PMReal(2.0);
		const PMReal cy = (r.Top()  + r.Bottom()) / PMReal(2.0);
		const PMReal dx = pb.X() - cx, dy = pb.Y() - cy;
		const PMReal d2 = dx * dx + dy * dy;
		if (!haveBest || d2 < bestDist2) { bestDist2 = d2; best = c->pages[i]; haveBest = kTrue; }
	}
	return best;
}

//----------------------------------------------------------------------------------------
// Build the exclusion pairing as maps both ways, once per generation.
// ★Going through KCMMapTargetToSource / KCMMapSourceToTarget instead would rebuild the whole
//   KCMBuildPairing on every call -- walk both documents' pages, test each for registration, then
//   search linearly. The sync is notified dozens of times a second, so that was a fixed cost
//   proportional to the page count. It is built once per generation (250ms, or an explicit
//   invalidation) and read afterwards with the map's O(log n) lookup.
//----------------------------------------------------------------------------------------
static void KCMEnsureSyncPairing(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (sSyncPairBuilt && sSyncPairTargetDB == targetDB && sSyncPairSourceDB == sourceDB)
		return;
	sSyncPairT2S.clear();
	sSyncPairS2T.clear();
	sSyncPairTargetDB = targetDB;
	sSyncPairSourceDB = sourceDB;
	sSyncPairBuilt    = kTrue;	// remember that it was built even where the pairing is empty (every page registered, say)
	if (targetDB == nil || sourceDB == nil)
		return;
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	std::vector<UID> pairT, pairS;
	marks->GetPagePairing(targetDB, sourceDB, pairT, pairS);
	for (size_t i = 0; i < pairT.size(); ++i)
	{
		sSyncPairT2S[pairT[i]] = pairS[i];
		sSyncPairS2T[pairS[i]] = pairT[i];
	}

	// ★The master spreads' pairing goes into the same table. A page UID is unique within a document,
	//   so it can share one map with the ordinary pages, and that is what lets a window showing a
	//   master find the partner's master page ＝ Sync Layout Views and Align Other Views work on
	//   masters too.
	//   ★It goes through the same two-part scheme the comparison uses (KCMCore.cpp): ordinary pages
	//   matched in order, masters matched by name.
	std::vector<UID> mT, mS;
	marks->GetMasterPagePairing(targetDB, sourceDB, mT, mS);
	for (size_t i = 0; i < mT.size(); ++i)
	{
		sSyncPairT2S[mT[i]] = mS[i];
		sSyncPairS2T[mS[i]] = mT[i];
	}
}

// Is pageUID (in db) a page of a master spread? It is decided from where the UID sits in the
// rectangle cache.
// ★IMasterSpreadList is not queried again because this is asked from the sync hot path: the cache
//   has certainly been built within the same notification (the rectangles come from it too), so
//   this costs nothing extra.
static bool16 KCMIsMasterPage(IDataBase* db, UID pageUID)
{
	if (pageUID == kInvalidUID)
		return kFalse;
	const KCMPageRectCache* c = KCMGetPageRects(db);
	if (c == nil)
		return kFalse;
	// The linear search is written with std::find, the way the product writes it
	// (conditionaltextui/ConditionalTextUIFacade.cpp:335, buttonui/actionpanel/
	// BehaviorTreeObserver.cpp:587, and plenty more).
	return std::find(c->pages.begin() + static_cast<std::ptrdiff_t>(c->normalCount),
	                 c->pages.end(), pageUID) != c->pages.end();
}

//----------------------------------------------------------------------------------------
// ★Ask the SDK which page is at the centre of a view -- the official route.
//   (1) the spread, from the view and the point = IPasteboardUtils::QuerySpread(view, pt)
//       (IPasteboardUtils.h:83). It answers nil for a point inside no spread at all, which drops
//       to QueryNearestSpread (:106).
//   (2) the nearest page within that spread = ISpread::QueryNearestPage(pt, &index) (ISpread.h:195)
//   The product does the same in CPathCreationTracker.cpp:278.
//   ★This hands "measure every page's rectangle, test containment, then find the nearest" to the
//   SDK. kInvalidUID where nothing is found ＝ the caller drops back to its own search.
//----------------------------------------------------------------------------------------
static UID KCMQueryViewCenterPage(IControlView* srcView, const PBPMPoint& center)
{
	if (srcView == nil)
		return kInvalidUID;

	InterfacePtr<ISpread> hitSpread(Utils<IPasteboardUtils>()->QuerySpread(srcView, center));
	ISpread* rawNear = (hitSpread == nil)
		? Utils<IPasteboardUtils>()->QueryNearestSpread(srcView, center)	// out on the empty pasteboard
		: nil;
	InterfacePtr<ISpread> nearSpread(rawNear);
	ISpread* spread = (hitSpread != nil) ? (ISpread*)hitSpread : (ISpread*)nearSpread;
	if (spread == nil)
		return kInvalidUID;

	int32 pageIndex = -1;
	InterfacePtr<IGeometry> pageGeo(spread->QueryNearestPage(center, &pageIndex));
	if (pageGeo == nil || pageIndex < 0 || pageIndex >= spread->GetNumPages())
		return kInvalidUID;
	return spread->GetNthPageUID(pageIndex);
}

//----------------------------------------------------------------------------------------
// The add/remove correction. It takes the page at the centre of the model view (in srcDocDb),
// maps it to the partner page through the comparison pairing -- the same pairing
// KCMMapTargetToSource / KCMMapSourceToTarget use: the registered pages are dropped and the rest
// matched in order, so **the right partner comes back even where an insertion has shifted every
// number after it** -- and converts the coordinate onto that partner page, keeping the offset from
// the page centre. That is what makes the two compared pages show the same place.
//   ★outSkip: kTrue where the model's centre is on a page with no partner (an Added / Removed
//     registration, or an overflow page). The caller then does **not** sync that destination
//     document, leaving the following view where it is -- rather than letting it jump to a raw
//     coordinate over a page that has no counterpart (the user's decision).
//   Where the correction cannot be made but it is not a skip either (not armed / a third document
//   outside the armed pair / the centre is off every page / the geometry could not be read),
//   srcCenter comes back unchanged ＝ it falls back to the plain copy, with outSkip = kFalse.
//----------------------------------------------------------------------------------------
static PBPMPoint KCMCorrectedCenterForDoc(IControlView* srcView, IDataBase* srcDocDb, IDataBase* dstDb,
                                            const PBPMPoint& srcCenter, bool16& outSkip, UID& outDstPage)
{
	outSkip = kFalse;
	outDstPage = kInvalidUID;	// the caller uses this to put the destination page's spread on screen

	// ★The arm state is asked nine times in this one function, so the interface is queried once and
	//  kept -- the practice Utils.h:74-80 states outright ("if you want to use a utility interface
	//  in several places, it is more efficient to get the interface once").
	//  ⚠**Count calls, not lines**: several lines here ask two or three times.
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (!compare->IsArmed() || compare->GetArmedTargetDB() == nil || compare->GetArmedSourceDB() == nil)
		return srcCenter;	// not armed: copy the viewport as it stands

	// Which way the pairing runs. A third document outside the armed pair is not corrected at all.
	const bool16 t2s = (srcDocDb == compare->GetArmedTargetDB() && dstDb == compare->GetArmedSourceDB());
	const bool16 s2t = (srcDocDb == compare->GetArmedSourceDB() && dstDb == compare->GetArmedTargetDB());
	if (!t2s && !s2t)
		return srcCenter;

	// Build the table the partner page is looked up in. Registered (Added / Removed) and overflow
	// pages have no partner -> that destination is skipped. ★The table comes from the cache; it
	// behaves exactly as KCMMapTargetToSource / KCMMapSourceToTarget do, "not in the table = no
	// partner" included.
	KCMEnsureSyncPairing(compare->GetArmedTargetDB(), compare->GetArmedSourceDB());
	const std::map<UID, UID>& pairTable = t2s ? sSyncPairT2S : sSyncPairS2T;

	// The model page ＝ the page at the centre of the view in srcDocDb.
	// ★The official route (KCMQueryViewCenterPage) is asked first.
	// ★★It answers "which page of the document" broadly, master-spread pages included. **The masters
	//   are in the pairing table too** (KCMEnsureSyncPairing adds the by-name matching), so a window
	//   showing a master can find the partner's master page.
	//   ⚠**A master page must NOT fall through to the search below**: that search only looks at
	//   ordinary pages, so it would seize on some entirely unrelated ordinary page as "nearest" and
	//   send the other window somewhere else. Where the partner has no such master, that is the same
	//   "no partner" an Added page has ＝ leaving the following view alone is the right answer.
	//   ⚠Pages of a **hidden** spread ARE in the table (flattening does not look at the hide flag),
	//   so this net does not catch them: with Sync and Hide Unchanged both on, it can sync to the
	//   partner of a hidden page. That was equally true of the hand-written search this replaced (it
	//   walks the same flat list), so it is existing behaviour, not something introduced here. To
	//   close it, add a hide test (IID_IHIDESPREADBOOLDATA) **on the reading side**
	//   -- ★KCMCollectPageUIDs itself is shared with the comparison pairing and must not change.
	//   ⚠KCMCollectPageUIDs was rewritten from a double ISpreadList loop onto **IPageList**, and
	//     **that it still includes hidden spreads' pages was measured** (with Hide Unchanged on, the
	//     whole UID sequence matched) ＝ the assumption here is unchanged.
	//   (A registered Added / Removed page is "not in the table" as well and does fall through to the
	//    search, but the search returns the same page, so the answer does not change -- it simply
	//    goes on to the skip test below. The only cost is searching twice in a rare case.)
	UID srcPage = KCMQueryViewCenterPage(srcView, srcCenter);
	const bool16 srcIsMaster = KCMIsMasterPage(srcDocDb, srcPage);
	std::map<UID, UID>::const_iterator pairIt = pairTable.find(srcPage);	// end for kInvalidUID
	if (pairIt == pairTable.end() && !srcIsMaster)
	{
		srcPage = KCMFindPageAtPasteboard(srcDocDb, srcCenter);
		if (srcPage == kInvalidUID)
			return srcCenter;	// the document has no pages at all, say: copy as it stands
		pairIt = pairTable.find(srcPage);
	}
	if (pairIt == pairTable.end() || pairIt->second == kInvalidUID)
	{
		outSkip = kTrue;	// ★no partner (Added, or a master with no counterpart): leave the following view alone
		return srcCenter;
	}
	const UID dstPage = pairIt->second;
	outDstPage = dstPage;

	// Move it onto the partner page, keeping the offset from the page centre.
	PMRect srcRect, dstRect;
	if (!KCMPagePasteboardRect(srcDocDb, srcPage, srcRect) ||
	    !KCMPagePasteboardRect(dstDb,    dstPage, dstRect))
		return srcCenter;	// the geometry could not be read: copy as it stands (NOT a skip)
	const PMReal srcCX = (srcRect.Left() + srcRect.Right()) / PMReal(2.0);
	const PMReal srcCY = (srcRect.Top()  + srcRect.Bottom()) / PMReal(2.0);
	const PMReal dstCX = (dstRect.Left() + dstRect.Right()) / PMReal(2.0);
	const PMReal dstCY = (dstRect.Top()  + dstRect.Bottom()) / PMReal(2.0);
	return PBPMPoint(dstCX + (srcCenter.X() - srcCX), dstCY + (srcCenter.Y() - srcCY));
}

// With applyPageOffset = kTrue, the add/remove correction above is applied to the centre copied to
// each destination document. The automatic "Sync Layout Views" passes kTrue as a matter of design:
// while a comparison is armed, the paired pages line up exactly. Outside the pair the correction
// falls back to a plain copy inside the function.
// ★srcView is the model view ＝ the one that was operated. It is what lets the correction ask the
//   SDK which page is at the view's centre (KCMQueryViewCenterPage). nil works too: it drops to the
//   search by rectangle.
//
// ★★★The return value is "**was there any destination view at all**". **Only this function can
//   answer "did anything get aligned"**, so it does. It used to be void, and its one caller that
//   needed the answer (KCMAlignOtherViewsToActiveNow) carried **a look-ahead copied by hand from
//   this function's guards**, with a comment promising to keep the two in step ---- and **it had
//   missed one exit (the dstDbs.empty() below)**, so with only one document open it reported
//   "aligned" (measured on a live build). ⇒ the decision is in one place
//   ([[one-question-one-place]]). ⚠kFalse means "**there was nowhere to copy to**", not "it failed".
//   The two automatic callers (Update, and the first alignment when the toggle goes on) ignore it.
static bool16 KCMSyncOtherDocViewportsTo(IControlView* srcView, IPanorama* srcPano, IDataBase* srcDocDb,
                                           bool16 applyPageOffset = kFalse)
{
	if (srcPano == nil)
		return kFalse;

	// The cache generation is bumped here as well. Coming through Update it has been done already and
	// this is a no-op, but Align Other Views and the first alignment when Sync goes on call this
	// function directly, so the TTL has to apply here too.
	KCMSyncCacheBeginTick();

	// ★★**There are two modes, and this is where they are told apart** (the section header above
	// says the same -- keep the two in step):
	//   (A) a comparison is armed ... Target <-> Source only, with the add/remove correction. Where
	//       the model view belongs to a third document, nothing is synced at all.
	//   (B) not armed             ... every other open document follows the one that was operated,
	//       **with no correction** (applyPageOffset is forced to kFalse).
	//       ★It does not additionally require the KCM tool to be active. It used to, to avoid
	//       syncing by accident, but by the time control reaches this function the Sync toggle
	//       (sLayoutSyncOn) is certainly on, and that is condition enough (the user's decision).
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());	// ★queried once and kept (Utils.h:74-80)
	const bool16 armed = (compare->IsArmed() && compare->GetArmedTargetDB() != nil && compare->GetArmedSourceDB() != nil);
	bool16 stopBroadSync = kFalse;
	if (armed)
	{
		if (srcDocDb != compare->GetArmedTargetDB() && srcDocDb != compare->GetArmedSourceDB())
			return kFalse;	// the model view is a third document ＝ sync nothing
	}
	else
	{
		stopBroadSync   = kTrue;
		applyPageOffset = kFalse;	// ★no add/remove correction in this mode (the user's decision)
	}

	// Read what the model view has on screen. The zoom is the effective scale (kTrue ＝ monitor PPI
	// included), which is the same dimension as kZoomToCmdBoss's scaleFactor, so reading and writing
	// are symmetric.
	const PMReal  srcZoom   = srcPano->GetXScaleFactor(kTrue);
	const PBPMPoint srcCenter(srcPano->GetContentLocationAtFrameCenter());

	ISession* session = GetExecutionContextSession();	// can be nil while the application is quitting
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return kFalse;

	// ★Settle the destination documents first.
	//   armed (A)     ... the guard above has pinned the model view to the Target or the Source, so
	//                     there is exactly one destination: the other half of the pair. Walking
	//                     IDocumentList end to end and building a ::GetUIDRef(doc).GetDataBase() per
	//                     document to find it would run dozens of times a second during a scroll, so
	//                     the partner is named directly and checked for life once.
	//   not armed (B) ... every document but the model's is a destination, so they are enumerated.
	std::vector<IDataBase*> dstDbs;
	dstDbs.reserve(2);
	if (!stopBroadSync)
	{
		IDataBase* dstDb = (srcDocDb == compare->GetArmedTargetDB()) ? compare->GetArmedSourceDB() : compare->GetArmedTargetDB();
		// Only check that the partner is still open -- the condition the full docList walk used to
		// assure silently. ★The db is passed to FindDocByDataBase and compared as a pointer, never
		// dereferenced, which is KCM's rule for a db that may have closed.
		if (dstDb != nil && dstDb != srcDocDb && docList->FindDocByDataBase(dstDb) != nil)
			dstDbs.push_back(dstDb);
	}
	else
	{
		const int32 docCount = docList->GetDocCount();
		for (int32 d = 0; d < docCount; ++d)
		{
			IDocument* doc = docList->GetNthDoc(d);
			if (doc == nil)
				continue;
			IDataBase* db = ::GetUIDRef(doc).GetDataBase();
			// ★The model's own document is excluded entirely, its split sibling included (the
			// user asked for "the other documents only").
			if (db == srcDocDb)
				continue;
			dstDbs.push_back(db);
		}
	}
	if (dstDbs.empty())
		return kFalse;	// ★Nowhere to copy to. It returns **before** the re-entry guard is raised.
	                	//   This is the exit for "only one document is open", "the partner has closed"
	                	//   and "Target and Source are the same document" ＝ the kFalse the caller
	                	//   used to be given no way of hearing about.

	// ★No zoom failure leaves this function. The copy loop below swallows a failed zoom and carries
	//   on, but cleaning up with ErrorUtils::PMSetGlobalErrorCode(kSuccess) alone would **also wipe
	//   an error that was already standing when this function was entered**. The official one is
	//   GlobalErrorStatePreserver (ErrorUtils.h:118).
	//   ★It is declared **before** broadcastGuard: the later declaration is destroyed first, so
	//   restoring the error state ends up outermost.
	GlobalErrorStatePreserver syncErrorState;

	// ★One zoom command is built per destination view, so the helper is queried once and kept
	//   (Utils.h:74-80). Building Utils<ILayoutUIUtils>() inside the loop would run a QueryInterface
	//   and a Release per view, on a path taken dozens of times a second.
	InterfacePtr<ILayoutUIUtils> layoutUIUtils(Utils<ILayoutUIUtils>().QueryUtilInterface());

	// The re-entry guard is raised with RAII, so that a ProcessCommand in the copy loop throwing
	// cannot leave the flag up ＝ cannot disable the sync for the rest of the session.
	struct KCMSyncBroadcastGuard
	{
		KCMSyncBroadcastGuard()  { sLayoutSyncBroadcasting = kTrue; }	// notifications from here on are our own, and the observer ignores them
		~KCMSyncBroadcastGuard() { sLayoutSyncBroadcasting = kFalse; }
	} broadcastGuard;

	for (size_t di = 0; di < dstDbs.size(); ++di)
	{
		IDataBase* db = dstDbs[di];

		// The centre coordinate copied to this destination document. With applyPageOffset it goes
		// through the add/remove correction (mapped to the paired page, keeping the offset within the
		// page). Where the model's centre is on a page with no partner, skip ＝ this destination is
		// not synced and its view stays where it is. Where the correction cannot be made but it is
		// not a skip, srcCenter comes back unchanged and the viewport is copied as it stands.
		PBPMPoint dstCenter = srcCenter;
		UID dstPage = kInvalidUID;
		if (applyPageOffset)
		{
			bool16 skipThisDoc = kFalse;
			dstCenter = KCMCorrectedCenterForDoc(srcView, srcDocDb, db, srcCenter, skipThisDoc, dstPage);
			if (skipThisDoc)
				continue;	// ★the model's centre is on a page with no partner ＝ this document is not synced
		}

		// ★The spread the destination page sits on. Each view below is checked against it. On the
		//   uncorrected path it stays kInvalidUID and nothing but the scroll happens, as before.
		UID dstSpread = kInvalidUID;
		if (dstPage != kInvalidUID)
		{
			InterfacePtr<IHierarchy> pageHier(db, dstPage, UseDefaultIID());
			if (pageHier != nil)
				dstSpread = pageHier->GetSpreadUID();
		}

		K2Vector<IControlView*> views;
		Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);

		for (int32 vi = 0; vi < (int32)views.size(); ++vi)
		{
			IControlView* view = views[vi];
			if (view == nil)
				continue;

			InterfacePtr<IPanorama> pano(KCMQueryPanorama(view));
			if (pano == nil)
				continue;

			// ★★Put the destination page's spread on this view **first**. A scroll (IPanorama) can only
			//   move within the coordinate space of the spread currently on screen, so it never reaches
			//   another one -- **a master spread above all, which is a separate space centred on the
			//   origin** -- and lands on empty pasteboard instead ([[scroll-needs-spread-switch]]).
			//   ★The test is **"is it a different spread"**, not "is it a master": that is how the
			//   product writes it (SnapTracker.cpp:224 has no special case), and it goes through the
			//   same function Prev/Next uses ([[one-question-one-place]]). Where the spread is already
			//   on screen it does nothing, so scrolling within one ordinary spread runs no command at
			//   all.
			if (dstSpread != kInvalidUID)
				KCMEnsureViewShowsSpread(view, db, dstSpread);

			// A view that already matches the model is left alone. Notifications arrive very frequently
			// during a scroll drag or a zoom, and re-scrolling an already-matching view (with a forced
			// redraw) on every one of them is wasted work.
			// ★The tolerance on the centre is about 1.5 screen pixels **converted through the zoom**.
			// Scroll positions are quantised to screen pixels, so a small fixed tolerance in points
			// would read as "does not match" forever at low magnifications. A difference inside this
			// band is not visible.
			const PMReal zoomDiff = abs(pano->GetXScaleFactor(kTrue) - srcZoom);
			const bool16 zoomMatched = (zoomDiff <= PMReal(0.0001));
			if (zoomMatched)
			{
				const PMPoint curCenter = pano->GetContentLocationAtFrameCenter();
				const PMReal dx = abs(curCenter.X() - dstCenter.X());
				const PMReal dy = abs(curCenter.Y() - dstCenter.Y());
				const PMReal tol = (srcZoom > PMReal(0.0001)) ? (PMReal(1.5) / srcZoom) : PMReal(1.0);
				if (dx <= tol && dy <= tol)
					continue;	// both the position and the magnification already match
			}

			// Bring the magnification to the model's effective scale, through the same official command
			// the UI's zoom field uses (kZoomToCmdBoss). The default arguments zoom about the view
			// centre.
			// ★Calling ILayoutViewUtils::ZoomLayoutViews directly does not work on another document's
			//   views (measured) -- and in any case **the header itself says "Internal use only"**
			//   (ILayoutViewUtils.h:71; GatherSpreadRects and TransformPointToNewSpread are marked the
			//   same way) ＝ it is not an API for a third party to call, so whether it works or not it
			//   is not something to move towards.
			// ★★**The zoom goes through a Command while the scroll below drives IPanorama directly, and
			//   the asymmetry has a reason**: the only official scroll command is
			//   ILayoutUIUtils::MakeScrollToSpreadCmd (:252), whose destination is limited to the
			//   spread's centre or the previous centre offset -- **an arbitrary content coordinate
			//   cannot be asked for**. A scroll position is view state rather than model state, so not
			//   putting it through a Command is defensible in itself.
			if (!zoomMatched)
			{
				InterfacePtr<ICommand> zoomCmd(layoutUIUtils->MakeZoomCmd(view, srcZoom));
				if (zoomCmd == nil || CmdUtils::ProcessCommand(zoomCmd) != kSuccess)
				{
					// ★Matching the zoom is a convenience of the synchronised display, and the scroll may
					//   carry on without it. But an error state left standing takes the commands that
					//   follow down with it ([[command-sequence-rollback-on-error]]), so it is cleared
					//   and the loop goes on -- the same practice as the zoom in KCMChangeNav.cpp.
					ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				}
			}
			// Put the model's visible centre -- or, when corrected, the matching coordinate on the
			// partner page -- at the centre of this view: at the same magnification, the same thing is
			// on screen in the same place. It runs **after** the zoom command, so the centring is done
			// at the new magnification.
			// ★forceRedraw is kFalse: with notifications arriving this often, invalidating and letting
			// the OS fold the repaints into its own cycle is much cheaper than a synchronous redraw
			// each time. The scroll position itself is updated synchronously, so the match test above
			// still reads the right value.
			pano->ScrollContentLocationToFrameCenter(dstCenter, kFalse /*forceRedraw*/);
		}
	}
	// (broadcastGuard's destructor lowers sLayoutSyncBroadcasting)
	return kTrue;	// there was somewhere to copy to ＝ including where every view already matched and none was touched, which is also "aligned"
}

//========================================================================================
// The layout view synchronisation (the flyout's checked "Sync Layout Views" toggle)
//   While it is on, every layout view of every document has its IPanorama subject subscribed to,
//   and whenever one of them scrolls or zooms, KCMSyncOtherDocViewportsTo copies that viewport onto
//   the other documents' views -- automatically, live. It also subscribes to the ActiveContext (a
//   document switch) so that the views of a newly opened document are picked up.
//   It is completely independent of a comparison being started: it can be turned on by itself.
//
//   ★It was written from the user's own earlier KES plug-in, KESLayoutScrollObserver. ⚠That file is
//   not in this repository anywhere -- it can be named, but not pointed at. Three things were done
//   differently:
//   (1) What is subscribed to widened from "only the original pane of the active document's first
//       presentation (where it matches QueryFrontView)" to **every layout view of every document**,
//       the new pane of a split window and second and later windows included. Whichever window is
//       moved becomes the model, and QueryFrontView's active-tracking drift (measured in a split
//       window) cannot affect it.
//   (2) Subscribing to many views cannot ping-pong, because the re-entry guard
//       (sLayoutSyncBroadcasting) makes the observer ignore the notifications the copy itself
//       raises. The model plug-in avoided that by subscribing to a single view.
//   (3) The engine is shared with the one-shot action (kZoomToCmdBoss plus reading and writing the
//       effective scale symmetrically).
//========================================================================================

static bool16 sLayoutSyncOn = kFalse;			// the toggle, kept for the session only

// Get the sync observer itself off the ActiveContext boss (+1 ref, so the caller takes it in an
// InterfacePtr).
// ★The arrangement: the .fr AddIn lodges it on kActiveContextBoss under IID_IKCMLAYOUTSYNCOBSERVER
// (the same as the plug-in it was written from).
// ★★**Why it has to be lodged there rather than created standalone.** AttachObserver's fourth
// argument, asObserver, is contractually "the interface id this observer implementation **actually
// occupies on that boss**" (ISubject.h, plus the fact that IChangeManager keys a dependency by
// (subject, observer, observerIID, interestedIn) and expects to look it up again by boss + IID;
// CSubject.h and IChangeManager.h exist only in the docs HTML). Putting the implementation on a
// standalone boss under IID_IOBSERVER (CreateObject2) and passing IID_IKCMLAYOUTSYNCOBSERVER --
// which that boss does not carry -- as asObserver **means Update is never delivered**, silently.
static IObserver* KCMQueryLayoutSyncObserver()
{
	// The session can be nil while the application is quitting.
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return nil;
	return (IObserver*)ctx->QueryInterface(IID_IKCMLAYOUTSYNCOBSERVER);
}

// Subscribe to every layout view, skipping the ones already subscribed to. Called when the toggle
// goes on and on every document switch while it is on, so a newly opened document or a newly
// appeared window is never missed.
static void KCMLayoutSyncAttachAllPanoramas()
{
	InterfacePtr<IObserver> obs(KCMQueryLayoutSyncObserver());
	if (obs == nil)
		return;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, nil);	// db = nil ＝ every layout view of every document
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<ISubject> subject(views[i], UseDefaultIID());
		if (subject == nil)
			continue;
		if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKCMLAYOUTSYNCOBSERVER))
			subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKCMLAYOUTSYNCOBSERVER);
	}
}

// Unsubscribe from every layout view (when the toggle goes off). A view that has already closed
// does not appear in GetAllLayoutViews, and its subscription went with it, so unsubscribing from
// the surviving ones is enough.
static void KCMLayoutSyncDetachAllPanoramas()
{
	InterfacePtr<IObserver> obs(KCMQueryLayoutSyncObserver());
	if (obs == nil)
		return;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, nil);
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<ISubject> subject(views[i], UseDefaultIID());
		if (subject == nil)
			continue;
		if (subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKCMLAYOUTSYNCOBSERVER))
			subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IPANORAMA, IID_IKCMLAYOUTSYNCOBSERVER);
	}
}

// Subscribe to, or unsubscribe from, the ActiveContext -- where a document switch is announced.
// There is one ActiveContext per session and it persists, so once each on and off is enough.
static void KCMLayoutSyncAttachContext(bool16 attach)
{
	ISession* session = GetExecutionContextSession();	// can be nil while the application is quitting
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKCMLAYOUTSYNCOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<ISubject> subject(ctx, UseDefaultIID());
	if (subject == nil)
		return;
	const bool16 attached = subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKCMLAYOUTSYNCOBSERVER);
	if (attach && !attached)
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKCMLAYOUTSYNCOBSERVER);
	else if (!attach && attached)
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IACTIVECONTEXT, IID_IKCMLAYOUTSYNCOBSERVER);
}

/** The layout view sync observer. The .fr AddIn lodges it on kActiveContextBoss under
    IID_IKCMLAYOUTSYNCOBSERVER (see KCMQueryLayoutSyncObserver above for why that matters). */
class KCMLayoutSyncObserver : public CObserver
{
public:
	// ★The second argument is "the IID this implementation actually occupies on the boss"
	//   (CObserver.h:55; what it is kept in is fAttachIID at :81 of the same header).
	//   The .fr AddIn lodges it under IID_IKCMLAYOUTSYNCOBSERVER and the Attach uses that IID, so the
	//   same one is passed here and what it says about itself matches what it is. The product does
	//   the same (layerpanel/CLayoutLayerListObserver.cpp:112). ⚠Left at the default,
	//   GetAttachIID() would answer IID_IOBSERVER and the two would disagree.
	KCMLayoutSyncObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMLAYOUTSYNCOBSERVER) {}
	~KCMLayoutSyncObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KCMLayoutSyncObserver, kKCMLayoutSyncObserverImpl)

void KCMLayoutSyncObserver::Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy)
{
	if (!sLayoutSyncOn || sLayoutSyncBroadcasting)
		return;	// off, or one of our own notifications from inside a copy

	// Re-subscribe on a document switch (IID_IDOCUMENT) or an active-view switch (IID_ICONTROLVIEW);
	// only views not yet subscribed to are added. ★Watching the **view** switch as well as the
	// document one is what makes a new window of the same document, or the new pane of a split
	// window, opened while the toggle is on, get subscribed the moment it is clicked into ＝ that
	// pane can be the model too.
	// ★Reading changedBy as a ContextInfo* and asking Key() what changed is the official form. The
	//   product writes the same two lines in linksui/LinksUIPanelTreeObserver.cpp:215-217 and in
	//   buttonui/actiondatapanels/gotourl/GoToURLPanelObserver.cpp:222-224.
	if (protocol.Get() == IID_IACTIVECONTEXT)
	{
		IActiveContext::ContextInfo* info = (IActiveContext::ContextInfo*)changedBy;
		if (info != nil && (info->Key() == IID_IDOCUMENT || info->Key() == IID_ICONTROLVIEW))
			KCMLayoutSyncAttachAllPanoramas();
		return;
	}

	if (protocol.Get() != IID_IPANORAMA)
		return;
	if (theChange != kScrollToMessage && theChange != kScrollByMessage &&
	    theChange != kScaleToMessage  && theChange != kScaleByMessage)
		return;

	// The panorama that sent the notification ＝ the model. theSubject is the layout view boss's
	// subject, so IPanorama and IControlView can both be had from it.
	//
	// ★★**Not using changedBy is deliberate.** A panorama notification carries a
	//   **PanoramaUpdateParams\*** in changedBy (IPanorama.h:352-356 says so), but what that carries
	//   is **how far it moved** (fOffset / fXScaleFactor) -- a delta, there to support live and
	//   hardware scrolling -- whereas what is wanted here is the absolute **where it is looking now**.
	//   ⚠And :358-359 of the same header admits that **the offset's sign is inverted when
	//   kScrollToMessage is broadcast, and observers have to allow for it** ＝ taking the delta means
	//   walking into that.
	//   ∴ the right thing is to take the panorama off the subject and read the current values again
	//   (the GetXScaleFactor and the visible centre below).
	InterfacePtr<IPanorama> srcPano(theSubject, UseDefaultIID());
	if (srcPano == nil)
		return;

	// ★Drop a notification that reports the same state as the last one. A single scroll or zoom can
	//   send several in a row (kScrollTo and kScrollBy, say), and running the whole copy -- resolving
	//   the page pairing, walking every destination view -- for each of them is wasted. Where the
	//   model's (panorama, effective zoom, visible centre) is exactly what was copied last time,
	//   nothing has changed and it returns at once. The test costs two reads of the panorama ＝ the
	//   cheapest sieve there is.
	//   ★Nothing is missed by it: if only a destination view moved, that view sends its own
	//     notification and is handled as the model then (model and destination are not fixed roles).
	//   ★sLastSrcPano is only ever compared, never dereferenced. Should another view reuse the same
	//     address, nothing is dropped unless the zoom and the centre match as well, and the 250ms TTL
	//     clears it anyway.
	KCMSyncCacheBeginTick();	// past the TTL this throws the whole cache away, the last state included
	const PMReal    curZoom = srcPano->GetXScaleFactor(kTrue);
	const PBPMPoint curCenter(srcPano->GetContentLocationAtFrameCenter());
	if (sHaveLastSrcState && sLastSrcPano == (IPanorama*)srcPano)
	{
		const PMReal dz  = abs(curZoom - sLastSrcZoom);
		const PMReal dcx = abs(curCenter.X() - sLastSrcCenter.X());
		const PMReal dcy = abs(curCenter.Y() - sLastSrcCenter.Y());
		if (dz <= PMReal(0.0) && dcx <= PMReal(0.0) && dcy <= PMReal(0.0))
			return;	// the model has not moved at all since the last copy
	}

	InterfacePtr<IControlView> srcView(theSubject, UseDefaultIID());
	if (srcView == nil)
		return;
	IDataBase* srcDocDb = KCMFindDocDbForView(srcView);
	if (srcDocDb == nil)
		return;	// its document cannot be identified (it is closing, say) ＝ do not sync

	// From here it really does copy, so this state is recorded as "the state last copied".
	sLastSrcPano      = (IPanorama*)srcPano;
	sLastSrcZoom      = curZoom;
	sLastSrcCenter    = curCenter;
	sHaveLastSrcState = kTrue;

	// ★The live sync applies the add/remove correction as well, by design: while a comparison is
	// armed the paired pages line up exactly. Not armed, or outside the pair, the function falls back
	// to a plain copy.
	KCMSyncOtherDocViewportsTo(srcView, srcPano, srcDocDb, kTrue /*applyPageOffset*/);
}

// KCMGetLayoutSync / KCMSetLayoutSync (declared in KCMViewSync.h) -- the flyout toggle itself.
bool16 KCMGetLayoutSync()
{
	return sLayoutSyncOn;
}

void KCMSetLayoutSync(bool16 on)
{
	if ((on && sLayoutSyncOn) || (!on && !sLayoutSyncOn))
		return;

	KCMInvalidateSyncCaches();	// on or off, the next sync starts from a fresh measurement

	if (on)
	{
		// The observer is AddIn-ed on kActiveContextBoss by the .fr. Where it cannot be had, the toggle
		// does not go on at all.
		InterfacePtr<IObserver> obs(KCMQueryLayoutSyncObserver());
		if (obs == nil)
			return;
		sLayoutSyncOn = kTrue;
		KCMLayoutSyncAttachContext(kTrue);
		KCMLayoutSyncAttachAllPanoramas();

		// Align once the moment it goes on, taking the frontmost layout view as the model. After that
		// it is notification-driven.
		InterfacePtr<IControlView> front(Utils<ILayoutUIUtils>()->QueryFrontView());
		if (front != nil)
		{
			InterfacePtr<IPanorama> pano(KCMQueryPanorama(front));
			IDataBase* db = KCMFindDocDbForView(front);
			if (pano != nil && db != nil)
				KCMSyncOtherDocViewportsTo(front, pano, db, kTrue /*applyPageOffset: the first alignment is corrected too*/);
		}
	}
	else
	{
		sLayoutSyncOn = kFalse;
		KCMLayoutSyncDetachAllPanoramas();
		KCMLayoutSyncAttachContext(kFalse);
		// The observer belongs to kActiveContextBoss (AddIn), so there is no lifetime to manage here.
	}
}

// KCMAlignOtherViewsToActiveNow (declared in KCMViewSync.h) -- the flyout / shortcut action.
// It aligns the other documents' views to the active (frontmost) layout view's position and
// magnification, once. It is independent of the Sync Layout Views toggle: with the toggle off,
// pressing it still aligns once (and the first alignment when the toggle goes on takes the same
// model view and calls the same engine). It passes applyPageOffset = kTrue, so the engine's own
// guards decide what that means -- armed, the add/remove correction runs between Target and Source;
// not armed, the engine forces the correction to kFalse and copies plainly to every other document.
bool16 KCMAlignOtherViewsToActiveNow()
{
	// An explicit action, so it always aligns from a fresh measurement whatever the cache holds.
	KCMInvalidateSyncCaches();

	InterfacePtr<IControlView> front(Utils<ILayoutUIUtils>()->QueryFrontView());
	if (front == nil)
		return kFalse;
	InterfacePtr<IPanorama> pano(KCMQueryPanorama(front));
	IDataBase* db = KCMFindDocDbForView(front);
	if (pano == nil || db == nil)
		return kFalse;
	// ★★★**"Did anything get aligned" is the engine's answer**, and it is returned unchanged.
	//   There used to be a **look-ahead guard copied by hand from the engine** here ("while armed, if
	//   the frontmost view belongs to a third document the engine does nothing"), with a comment
	//   promising to keep the two in the same condition.
	//   ⇒ the engine had another exit it had not copied -- **there was nowhere to copy to** -- so
	//     with only one document open this reported "Aligned other views to the active view."
	//     (measured: the same wording for one document as for two). **Hold the same decision in two
	//     places and they drift** ([[one-question-one-place]]).
	return KCMSyncOtherDocViewportsTo(front, pano, db, kTrue /*applyPageOffset: corrected only while armed; the engine forces kFalse otherwise*/);
}

// KCMViewSyncShutdown (declared in KCMViewSync.h) -- the teardown clean-up.
void KCMViewSyncShutdown()
{
	// The teardown here **lowers the flag and nothing else**; any notification still in flight is
	// then ignored by the guard at the top of Update.
	// ★★**KCMSetLayoutSync(kFalse) must NOT be called here.** That route **walks every open view with
	// GetAllLayoutViews**, and while the application is quitting those windows and views are being
	// taken apart -- dereferencing them crashed **every time** (measured: quit with the toggle on and
	// it went down without fail).
	// Leaving the subscriptions is safe: a dependency disappears from IChangeManager when the
	// subject's or the observer's boss is destroyed, so nothing has to be detached explicitly. The
	// plug-in this was written from detached nothing at shutdown either, without incident.
	//
	// ⚠★★**It is GetAllLayoutViews that is dangerous here, not GetActiveContext().** Touching
	//   GetActiveContext() during teardown is fine -- KCMDetachModelChangeObserver and
	//   KCMDetachPanelVisibilityObserver both do exactly that at shutdown and pass the
	//   teardown-safety checks, quitting straight after ten queued exports included.
	// ⇒ ★**Listing two dangerous things side by side makes an operation that uses only one of them
	//   look forbidden as well.** That is precisely what happened: this pair was read in
	//   KCMPeekGesture.cpp as the wider rule "detaching is itself what crashes", and it stood there
	//   for three days as the reason not to detach that file's observer.
	sLayoutSyncOn = kFalse;
}
