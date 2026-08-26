//========================================================================================
//
//  KCMThumbnailRefresh.cpp
//
//  Makes the Pages panel rebuild thumbnails it has ALREADY drawn, once a comparison has run
//  (KCMThumbnailRefresh.h).
//
//  THE SMALLEST SET THAT WORKS is two steps, both isolated on a live build:
//    - IImageCacheMgr::Purge(...) invalidates the shared image cache. The Pages panel thumbnails
//      are IN that shared cache: leave this out and they stay as they were, with no frame on them.
//    - ForceRedraw then redraws the panel (and both sub-panels) so the thumbnails are rebuilt on
//      the spot. Leave this out and the purge has no visible effect until something else redraws.
//
//  ---------------------------------------------------------------------------------------------
//  WHY THE PURGE IS PER PAGE AND NOT PER DOCUMENT (measured).
//    Purge(db) throws away every image the document has cached, so thumbnails of unchanged pages
//    are rebuilt too and the whole Pages panel flashes. Purging only the changed pages (the keys
//    of sEntries / sSrcPageToTarget) leaves the rest of the cache alive, and the pages that do get
//    rebuilt are the ones whose frame had to be redrawn anyway.
//
//    THE CACHE KEY IS THE PAGE UID (measured with a probe: purging a page UID frees bytes,
//    purging a spread UID frees none). So there is no need to purge at spread level.
//
//    THE SET MUST INCLUDE THE OVERFLOW PAGES ("/" slash marks), not just the changed ring
//    (sEntries). They are a separate set, and leaving them out means an overflow page's thumbnail
//    is never purged and its slash does not appear until something else redraws it.
//
//    STOP AND CLOSE NEED A FALLBACK. By then DropAll has run and the db is no longer the compared
//    one, so no changed set can be obtained. The obvious fallback -- a single Purge(db) -- was
//    measured NOT to invalidate existing thumbnails (the frames stayed), so instead every page is
//    enumerated and purged one UID at a time: the proven per-UID purge, applied to all of them.
//    With DropAll already done there is no frame data left, so what gets rebuilt is clean. This is
//    a terminating operation, so a momentary refresh of the whole panel is acceptable here (the
//    flash that the START path avoids is a different case).
//  ---------------------------------------------------------------------------------------------
//
//  Note that drawing the frame INTO a thumbnail is the drawing engine's job
//  (KCMDrawEventHandler::sThumbExperiment, on by default). Everything here only triggers the
//  rebuild of what is already on screen, and is safe to call any number of times.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"			// GetExecutionContextSession (can be nil during teardown, so the type is spelled out)
#include "IApplication.h"
#include "IPanelMgr.h"
#include "IPanelControlData.h"
#include "IControlView.h"
#include "IDataBase.h"
#include "IWorkspace.h"
#include "IImageCacheMgr.h"
#include "IUIDListControlData.h"	// the selection UID list the Pages panel itself keeps (how [None] is told apart)

#include "PagesPanelID.h"		// kPagesPanelWidgetID / kLayoutPagesSubPanelWidgetID / kMasterPagesSubPanelWidgetID
#include "ImageID.h"			// IID_IIMAGECACHEMGR
#include "Utils.h"

#include <set>
#include <vector>

#include "KCMThumbnailRefresh.h"
#include "IKCMMarkData.h"			// GetAllPageUIDs / GetMasterPageUIDs / GetMarkablePageUIDs

// Redraw one sub-panel (the Layout one or the Master one).
static void KCMForceRedrawSubPanel(IPanelControlData* pcd, const WidgetID& subPanelWID)
{
	if (pcd == nil)
		return;
	IControlView* subView = pcd->FindWidget(subPanelWID);
	if (subView != nil)
		subView->ForceRedraw(nil, kTrue);
}

// "Which pages can carry a mark right now" lives on the model side. It touches no widget, and
// its callers were all on the model side too -- keeping it here was what made those files include
// a UI header. From the UI it is reached through the boundary facade
// IKCMMarkData::GetMarkablePageUIDs, which is what the purge below calls. (The model-side
// header itself is not visible from ui/ at all.)

// Purge the given page UIDs one at a time (invalidating the shared image cache). The workspace
// comes from the effective session. Callers hand over both sets and vectors, so the body is a
// template over an iterator range rather than a conversion into one container -- the purge needs
// neither an order nor duplicates removed.
template <class InputIt>
static void KCMPurgePageThumbsRange(IDataBase* db, InputIt first, InputIt last)
{
	if (db == nil || first == last)
		return;
	// the session can be nil during teardown (this purge also runs from the deferred idle task)
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IWorkspace> ws(session != nil ? session->QueryWorkspace() : nil);
	InterfacePtr<IImageCacheMgr> cacheMgr(ws, IID_IIMAGECACHEMGR);
	if (cacheMgr == nil)
		return;
	for (; first != last; ++first)
	{
		UIDRef pageRef(db, *first);
		cacheMgr->Purge(pageRef, IImageCacheMgr::kWildCard);
	}
}

static void KCMPurgePageThumbs(IDataBase* db, const std::set<UID>& pages)
{
	KCMPurgePageThumbsRange(db, pages.begin(), pages.end());
}

static void KCMPurgePageThumbs(IDataBase* db, const std::vector<UID>& pages)
{
	KCMPurgePageThumbsRange(db, pages.begin(), pages.end());
}

// The Pages panel if it is showing (declared in KCMThumbnailRefresh.h; shared with ChangeNav's
// companion scrolling).
IControlView* KCMGetVisiblePagesPanel()
{
	ISession* session = GetExecutionContextSession();	// can be nil during teardown
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (app == nil)
		return nil;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return nil;
	return panelMgr->GetVisiblePanel(kPagesPanelWidgetID);
}

// Does the Pages panel selection hold no real page (only [None] rows)? The reason and the
// measurements are on the declaration.
bool16 KCMPagesPanelSelectionHasNoRealPage()
{
	IControlView* panel = KCMGetVisiblePagesPanel();
	if (panel == nil)
		return kFalse;		// no panel, so this test cannot be made: leave the decision where it was
	InterfacePtr<IUIDListControlData> uidList(static_cast<IPMUnknown*>(panel), IID_IUIDLISTCONTROLDATA);
	if (uidList == nil)
		return kFalse;		// cannot be read, so do not get in the way

	const int32 n = uidList->Length___();
	if (n <= 0)
		return kFalse;		// empty means "nothing selected": leave that to the existing test, do not disable anything

	for (int32 i = 0; i < n; ++i)
	{
		if (uidList->GetUID___(i) != kInvalidUID)
			return kFalse;	// one real page is enough: let the action apply to that one
	}
	return kTrue;			// every entry is kInvalidUID: only [None] rows are selected
}

// Redraw the Pages panel where it is showing, so the purged thumbnails are rebuilt on the spot.
// While it is hidden there is nothing to touch (the thumbnails are built naturally next time it
// is opened). Public so that a caller batching its purges can call it once at the end.
void KCMForceRedrawPagesPanelNow()
{
	IControlView* panel = KCMGetVisiblePagesPanel();
	if (panel == nil)
		return;
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	KCMForceRedrawSubPanel(pcd, kLayoutPagesSubPanelWidgetID);
	KCMForceRedrawSubPanel(pcd, kMasterPagesSubPanelWidgetID);
	panel->ForceRedraw(nil, kTrue);
}

void KCMTryRefreshPagesPanelThumbnails(IDataBase* db, bool16 redrawNow)
{
	if (db == nil)
		return;

	std::set<UID> changedPages;
	if (Utils<IKCMMarkData>()->GetMarkablePageUIDs(db, changedPages))
	{
		// -- a db under comparison (START / recomparison) --
		// Purge only the changed pages, plus the overflow ("/") pages, plus the registered ones,
		// by page UID. Thumbnails of unchanged pages keep their cache entry and do not flash.
		// (Page UID is the cache key; measured with a probe.) An empty set -- nothing changed --
		// purges nothing and only takes the ForceRedraw below, which is harmless.
		// The old set from before a recomparison is NOT merged in here: neither caller ever passed
		// one. Looking after the old set is KCMPurgeAllPageThumbs's and
		// KCMRefreshThumbnailsForPages's job; the header says why.
		KCMPurgePageThumbs(db, changedPages);
	}
	else
	{
		// -- after Stop or a close (DropAll has run and this db is no longer the compared one) --
		// Enumerate every page and purge one UID at a time. A single Purge(db) was measured NOT to
		// invalidate existing thumbnails (the frames stayed), so the proven per-UID purge is
		// applied to all of them. With DropAll done there is no frame data left, so what gets
		// rebuilt is clean and the frames go. A momentary refresh of the whole panel is acceptable
		// for a terminating operation.
		//
		// This is delegated to KCMPurgeAllPageThumbs below rather than done here.
		// @warning THE EARLIER CODE CALLED ONLY GetAllPageUIDs AND LEFT MASTER PAGES UNPURGED --
		//   while KCMPurgeAllPageThumbs, in this same file, already said in its own comment that
		//   purging one without the other leaves a stale frame on the master's thumbnail, and
		//   purged both. Master pages do carry marks (KCMBuildMasterPairing pairs them).
		//   Measured both ways with a diagnostic build that printed the purge count and a control
		//   build put back to the old shape: in a document of four ordinary pages and one master,
		//   closing the Target purged 4 the old way and 5 this way -- exactly the one master page.
		//   So "purge every page" is defined in ONE place now.
		// This branch is still live: the deferred purge after a close (KCMThumbIdleTask) comes
		// through here. The redraw is the caller's, via if (redrawNow) below --
		// KCMPurgeAllPageThumbs only purges.
		KCMPurgeAllPageThumbs(db);
	}

	if (redrawNow)
		KCMForceRedrawPagesPanelNow();
}

void KCMPurgeAllPageThumbs(IDataBase* db)
{
	if (db == nil)
		return;

	// Ordinary pages AND master pages. Marks do appear on masters (KCMBuildMasterPairing pairs
	// them and KCMDoMarkChangesDoc takes them in), so purging one without the other leaves a stale
	// frame on the master's thumbnail. GetMasterPageUIDs does not clear its out parameter, by
	// contract, so it can append to what is already there.
	// The facade is queried into an InterfacePtr because it is asked twice in a row (Utils.h says
	// to do that rather than pay for a query per call).
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	std::vector<UID> allPages;
	marks->GetAllPageUIDs(db, allPages);
	marks->GetMasterPageUIDs(db, allPages);
	KCMPurgePageThumbs(db, allPages);
	// No redraw here. Every caller has the shape "purge both documents, then one
	// KCMForceRedrawPagesPanelNow at the end", so drawing here would draw twice.
}

void KCMRefreshThumbnailsForPages(IDataBase* db, const std::vector<UID>& pages, bool16 redrawNow)
{
	if (db == nil || pages.empty())
		return;
	// Purge the toggled pages explicitly, per UID. Un-registering removes the page from the
	// registered set FIRST, so it can no longer be reached through
	// IKCMMarkData::GetMarkablePageUIDs above -- purging it here is what clears its green "/".
	// Registering purges the same page symmetrically, so that the swap from a red "/" (overflow)
	// to a green one (registered) shows at once.
	KCMPurgePageThumbs(db, pages);
	if (redrawNow)
		KCMForceRedrawPagesPanelNow();
}

// The std::set overload. The page set a notification (KCMNotifyPages) carries is a set, so it is
// passed straight through rather than copied into another container -- the purge itself does not
// care (KCMPurgePageThumbsRange).
void KCMRefreshThumbnailsForPages(IDataBase* db, const std::set<UID>& pages, bool16 redrawNow)
{
	if (db == nil || pages.empty())
		return;
	KCMPurgePageThumbs(db, pages);
	if (redrawNow)
		KCMForceRedrawPagesPanelNow();
}

// End of KCMThumbnailRefresh.cpp
