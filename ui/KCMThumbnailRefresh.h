//========================================================================================
//
//  KCMThumbnailRefresh.h
//
//  The isolated module that makes the Pages panel rebuild thumbnails it has ALREADY drawn, once
//  a comparison has run.
//
//  THE SMALLEST SET THAT WORKS is two steps, isolated on a live build:
//    - IImageCacheMgr::Purge invalidates the shared image cache. The Pages panel thumbnails do
//      live in that shared cache (an earlier note claiming they sat in a private one was wrong).
//      The unit of the purge is ONE PAGE UID, not the whole db -- purging the db turns out not to
//      invalidate thumbnails that already exist, and it makes the whole panel flash. The .cpp
//      says more.
//    - ForceRedraw then makes the panel rebuild them on the spot.
//  TWO THINGS THAT DO NOT WORK and were taken out: InvalidateSpreadWidget + UpdatePagesPanel
//  (bForcePurge), and IPendingUpdateController. The first is the documented API on the public
//  IPagesSubPanelController, and it still did nothing on its own -- Adobe's own code does not use
//  it anywhere in the SDK either. Whether it would work when combined with the purge is untested.
//  The full record of how this was narrowed down is in the memory note
//  pages-panel-thumbnail-refresh.
//
//========================================================================================

#ifndef __KCMThumbnailRefresh_h__
#define __KCMThumbnailRefresh_h__

#include "OMTypes.h"		// UID
#include <vector>
#include <set>

class IDataBase;
class IControlView;

// The IControlView of the Pages panel (kPagesPanelWidgetID) if it is showing; nil when it is
// hidden or cannot be obtained. One copy of the IPanelMgr -> GetVisiblePanel idiom, shared by the
// thumbnail redraw here and by KCMChangeNav's Pages-panel scrolling. Defined in the .cpp.
IControlView* KCMGetVisiblePagesPanel();

// Does the Pages panel's selection contain NO real page -- that is, are only [None] rows (the
// "no master" row) selected? When it is true, the page-directed items (Check / Register /
// Refresh) are left out.
//
// WHY THIS IS NEEDED (confirmed on a live build). A [None] row has no real page behind it, so
// ILayoutUIUtils::GetSelectedPages FALLS BACK TO THE CURRENT PAGE. From here that is
// indistinguishable from "that page was selected", and the visible result was a check mark
// appearing on page 1 while [None] was what the user had selected.
// @warning the third argument of GetSelectedPages (bCurrentPageOnly) does not prevent this.
//   Passing kFalse widens the fallback to every page of the current spread, which is worse.
//
// HOW IT IS TOLD APART: read the UID list the Pages panel itself keeps (IUIDListControlData, part
// of kPagesPanelWidgetBoss, implemented by kPagesPanelUIDListControlDataImpl). Selecting [None]
// puts a single kInvalidUID (0) in it -- measured, against page 1: 240 / page 3: 262 / master:
// 252. So the application does hold [None] as an invalid UID.
// @warning the IUIDListControlData methods carry a `___` suffix, Adobe's mark for internal use.
//   Read from them only.
// Selecting [None] together with a real page gives kFalse, so the real page still gets the
// action and is not caught up in this.
bool16 KCMPagesPanelSelectionHasNoRealPage();

// Try to rebuild this db's Pages panel thumbnails. The image cache purge is attempted even when
// the panel is hidden. Safe to call any number of times.
//   redrawNow (kTrue by default): pass kFalse to purge without the ForceRedraw. That is how a
//     caller updating both the Target and the Source batches them -- kFalse for the first, then
//     one redraw at the end. (Before that batching, a single operation could run up to nine
//     synchronous ForceRedraws.)
//
// This used to take an extraPages argument as well -- "page UIDs to purge on top of the changed
// set", meaning the pages that a re-pairing had dropped out of the old set. NEITHER caller ever
// passed anything, so it was a promise nobody used, and it is gone. That job now belongs to
// KCMPurgeAllPageThumbs (purge every page) and to KCMRefreshThumbnailsForPages, now that the
// notification carries the page set (see below).
// If mixing in the old set is ever wanted again, do NOT put the argument back: put the old set on
// the notification and hand it to KCMRefreshThumbnailsForPages -- the shape the sibling path
// already uses.
void KCMTryRefreshPagesPanelThumbnails(IDataBase* db, bool16 redrawNow = kTrue);

// "Which pages can carry a mark right now" moved to the model side: it touches no widget, and
// every caller was on the model side. From the UI the answer comes through the boundary facade
// IKCMMarkData::GetMarkablePageUIDs.

// Purge EVERY page of the db (ordinary and master) per UID, so the Pages panel rebuilds all of
// its thumbnails.
//
// WHY SO BLUNT AN ENTRY POINT EXISTS. The model no longer calls the UI directly; it posts a
// notification instead. For a full recomparison there is no way to put "the set that HAD frames
// before the comparison" on that notification -- the old set is lost in the act of recomparing,
// and purging only the new set leaves a stale frame on the thumbnail of a page whose frame has
// just gone (exactly the defect this .cpp was written to fix). Purging every page cannot miss
// anything; it is only slower, in proportion to the page count.
//
// @warning an earlier version of this note said the reason was that "a notification can carry
//   nothing but a ClassID". That is untrue: ISubject::Change carries it in the third argument,
//   changedBy (the shipping linksui/EditOriginalResumeObserver.cpp does exactly that). The page
//   flag path (Register / check mark) went back to per-UID once that was understood: the model
//   puts the toggled page set on KCMNotifyPages and KCMModelChangeObserver hands it to
//   KCMRefreshThumbnailsForPages below. Two more paths went back to per-UID with it -- the
//   partial recomparison (Refresh Page Comparison, which decides which pages it will touch before
//   it starts) and the overset scan (Find Overset, where the old set is still there after
//   sOversetPages.swap() and was simply being thrown away).
//
// WHAT STILL COMES HERE:
//   - a full recomparison (and Stop), where what is needed is the set from BEFORE the comparison
//     and the model would have to save it first (not done). NOTE that the partial recomparison is
//     no longer one of these.
//   - any notification that carries no page set at all (fPagesA == nil), as the fallback.
//
// This function only purges. It used to take a redrawNow argument, and every caller passed
// kFalse, because they all end with a single KCMForceRedrawPagesPanelNow of their own.
void KCMPurgeAllPageThumbs(IDataBase* db);

// Purge just the named page UIDs of this db, per UID, then redraw the Pages panel. Use it for
// pages that will not turn up in the changed set (sEntries / overflow) but must still be rebuilt
// -- a registration toggle above all. Un-registering removes the page from the registered set
// first, so the only way to clear its green "/" is this explicit purge. Does nothing when db is
// nil or pages is empty, and is safe to call any number of times. redrawNow means what it means
// on KCMTryRefreshPagesPanelThumbnails (kFalse = purge only, for batching).
// The std::set overload exists so that the page set a notification carries can be passed straight
// through: the purge itself is a template that does not care about the container, so the
// implementation is a one-line forward rather than a throwaway vector.
void KCMRefreshThumbnailsForPages(IDataBase* db, const std::vector<UID>& pages, bool16 redrawNow = kTrue);
void KCMRefreshThumbnailsForPages(IDataBase* db, const std::set<UID>& pages, bool16 redrawNow = kTrue);

// Redraw the Pages panel now, if it is showing: the trigger that rebuilds purged thumbnails.
// This is the public form, for a caller that batched its purges with redrawNow=kFalse and calls
// this once at the end.
void KCMForceRedrawPagesPanelNow();

#endif // __KCMThumbnailRefresh_h__
