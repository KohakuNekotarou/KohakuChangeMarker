//========================================================================================
//
//  KCMPeek.cpp
//
//  The implementation of the tool's peek (see KCMPeek.h): the peek state, laying the older version
//  over the current one, the partial re-comparison of selected pages, the startup/shutdown
//  service, and the arm/disarm/state accessors declared in KCMCore.h.
//
//  Three areas that belong to the UI live elsewhere:
//    - viewport synchronisation (Sync Layout Views / Align Other Views) -> KCMViewSync.cpp
//    - the Alt + left CMYK cursor                                       -> KCMCmykCursor.cpp
//    - gesture recognition and what is shown while held (RevealBegin/End) -> KCMPeekGesture.cpp
//  The state they own -- what is displayed while held, the CMYK, the sync caches -- is closed
//  inside those files. Reaching it from here means calling the entry points their headers publish;
//  the Shutdown, arm and disarm below are the worked examples.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// The object model:
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "ISession.h"

// The tool and the startup service:
#include "IStartupShutdownService.h"
#include "CPMUnknown.h"
#include "LayoutUIID.h"
#include "DocumentContextID.h"

// Geometry. IControlView.h, IPanorama.h and PMMatrix.h are deliberately absent: reading the view,
// its panorama and its zoom belongs to the caller (the UI), so nothing here uses them.
#include "PMPoint.h"
#include "PMReal.h"

// The progress bar of the partial re-comparison:
#include "ProgressBar.h"			// TaskProgressBar (progress and Cancel for a large Refresh)
#include "ErrorUtils.h"				// PMSetGlobalErrorCode (no error is carried past a cancellation)

#include <map>
#include <set>			// the touched pages, sent with the notification so thumbnails purge per UID
#include <vector>

// KCM's own headers:
#include "KCMID.h"
#include "KCMConstants.h"
#include "KCMDrawEventHandler.h"   // the engine's shared statics
#include "KCMCore.h"               // the arm/disarm/state declarations
#include "KCMComparisonRun.h"      // KCMForgetChosenDocsThatClosed -- the chosen Target/Source lose whichever document closed
#include "KCMExternalSource.h"     // KCMIsDbAlive (the lent Source counts as alive)
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it
// The UI's KCMViewLookup.h is deliberately absent. Resolving which view the mouse is over belongs
//   to the caller (the UI); this .cpp only peeks at the spread of the point it is given.
#include "KCMPageMap.h"            // KCMBuildPairing (the exclusion pairing) / KCMPageMapReadSelection / KCMPageMapSweepClosedDocs
#include "KCMPageCheck.h"          // KCMPageCheckClearAllDocs / KCMPageCheckSweepClosedDocs (clearing the ticks)
#include "KCMColorSampler.h"       // KCMSampleCmykEndDrag (the pairing cached while Alt + left is held; emptied at shutdown)
#include "KCMThreadSafety.h"       // KCMIsMainThread -- a background thread cannot tell whether a document is still open
#include "KCMPageNumberMarker.h"   // KCMInvalidatePageNumberMarkerRects (dropping the page-number exclusion rectangles)
// The UI's own headers are deliberately absent: the panel state, the translucency, the HUD, the
// CMYK cursor, the thumbnails, the scrollbar map, Prev/Next, the deferred rebuild, the sync caches
// and the peek's held-down state are all the UI's property. What used to be called directly from
// here now happens in KCMModelChangeObserver, which receives the notifications this file sends.
#include "KCMStoryList.h"          // KCMStoryList::ShutdownCleanup (letting go of the rows' PMStrings)
#include "KCMStoryMarker.h"        // KCMStoryMarker::Shutdown (the Story mode's marks are never drawn again)
#include "KCMBookCompare.h"        // KCMClearBookResultText (the book comparison's result text)
#include "KCMChangedPagesTSV.h"    // KCMClearExportMessage (the TSV export's message)
#include "KCMHideUnchanged.h"      // KCMResetHideUnchanged and the getters for the hidden documents
#include "KCMPeek.h"

//========================================================================================
// The tool's peek -- shared state and helpers.
//   While the tool's left button is held, the older version of the spread under the mouse is laid
//   opaquely over it, and releasing hides it again. The older document has to be armed first
//   through KCMDoArmMousePeek (KCMCore.h), which the panel's Start button calls. The tracker
//   entry points (KCMTrackerRevealBegin/End in KCMPeekGesture.cpp) read that armed state.
//========================================================================================
static IDataBase* sPeekTargetDB = nil;	// the document on display (newer). Checked for still being open before use.
static IDataBase* sPeekSourceDB = nil;	// the older document laid over it while peeking.
static bool16     sPeekArmed    = kFalse;

// The "base" on-screen opacity of the marks -- the value used while the tool's left button is not
//   held and the marks are simply visible. With printing marks on it is the opacity chosen in the
//   panel (25%/75%, so that screen and print look alike); with printing off it is 1.0.
//   Releasing the button puts sMarkScreenOpacity back to this.
PMReal KCMBaseScreenOpacity()
{
	// While printing marks is on, or while "Always Show Marks on Target" keeps the frames on
	// screen, the permanently visible frames are drawn at the panel's 25%/75% so that they match
	// what a press shows temporarily. With both off they are opaque.
	return (KCMDrawEventHandler::sPrintMarks || KCMDrawEventHandler::sTgtMarksOn)
	       ? KCMDrawEventHandler::SelectedMarkOpacity() : PMReal(1.0);
}

// Every paired page of the two documents as target UID -> source UID, **masters included**.
//
// The two rules differ -- ordinary pages pair by position, master spreads by name -- so they come
// from two separate functions, and both go into ONE map: page UIDs are unique within a document.
// (The view sync's KCMEnsureSyncPairing has the same shape.)
//
// @warning **leaving the masters out is a bug that hides.** Both callers below had it once, and
//   both times it looked like something else: the peek showed nothing at all on a master page,
//   and selecting a master and choosing Refresh found nothing in the pairing and silently
//   reported "no pages". **The comparison itself had learned to handle masters while the callers
//   of this pairing had not, so the frames appeared but nothing else worked on them.**
static void KCMBuildFullPairing(IDataBase* targetDB, IDataBase* sourceDB,
                                std::map<UID, UID>& outTargetToSource)
{
	std::vector<UID> pairT, pairS;
	KCMBuildPairing(targetDB, sourceDB, pairT, pairS);
	for (size_t k = 0; k < pairT.size(); ++k)
		outTargetToSource[pairT[k]] = pairS[k];

	std::vector<UID> mT, mS;
	KCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
	for (size_t k = 0; k < mT.size(); ++k)
		outTargetToSource[mT[k]] = mS[k];
}


// Lay the older version of the spread at **the given point** over the current one.
//   targetDB is the document on display (newer), sourceDB the older one laid over it.
//   A spread already in the cache is reused immediately; otherwise the old cache is thrown away
//   and that one spread is rasterised there and then (only ever one spread is held). On success
//   sShowOriginal goes up and the views are redrawn.
//   **The observations belong to the caller (the UI) and the policy stays here**: which view, at
//     what scale, and where the mouse is are all passed in, while translating that scale into a
//     dpi -- the 50% floor and the 16..300 dpi clamp below -- is decided here. The formula itself
//     did not change when the two were separated. The parameters are documented in KCMPeek.h.
void KCMPeekShowAt(IDataBase* targetDB, IDataBase* sourceDB,
                     const PMReal& mx, const PMReal& my,
                     const PMReal& viewScale, const PMReal& uiZoom,
                     UID viewSpreadUID)
{
	if (targetDB == nil || sourceDB == nil)
		return;

	// Turn the scale the caller measured (content -> window = zoom x device scale) into the
	// resolution that matches the screen one for one: dpi = 72 x scale. At 1:1 the result is as
	// sharp as it can be, one image pixel per screen pixel.
	PMReal curScale = abs(viewScale);
	if (curScale <= 0) curScale = 1.0;

	// **The floor at 50% UI zoom.** Below a UI zoom of 50% -- the magnification the user sees,
	// without the device scale -- the resolution stops falling and stays at what 50% would give.
	// At and above 50% the image stays 1:1 with the screen and crisp; below it the image is finer
	// than the screen and the downscaling blit (point sampling) coarsens it, which is the accepted
	// trade at 10% and the like. Putting the floor on the UI percentage keeps the boundary at the
	// 50% the user actually sees, whatever the device scale is. When the panorama could not be
	// read the caller passes uiZoom = 0, which skips this and leaves the scale at 1:1.
	PMReal effScale = curScale;
	if (uiZoom > 0)
	{
		const PMReal deviceScale = curScale / uiZoom;			// the display's device scale
		const PMReal flooredZoom = (uiZoom < PMReal(0.5)) ? PMReal(0.5) : uiZoom;	// floored at 50%
		effScale = flooredZoom * deviceScale;
	}

	PMReal peekDpi = PMReal(72.0) * effScale;
	if (peekDpi < 16.0)  peekDpi = 16.0;	// a safety floor against degenerate values; normally unreached
	if (peekDpi > 300.0) peekDpi = 300.0;	// a memory ceiling (300dpi A4 is about 35MB a page)

	// Which spread and page the point is on (and its flat index), through KCMFindPageUnderMouse.
	KCMPageHit hit;
	if (!KCMFindPageUnderMouse(targetDB, mx, my, hit, viewSpreadUID))
		return;

	const int32 np          = hit.numPages;
	InterfacePtr<ISpread> spread(targetDB, hit.spreadUID, UseDefaultIID());
	if (spread == nil)
		return;

	// **Skipping an unchanged spread**, and only in the Pixel mode. If this document has already
	// been compared (sDB == targetDB) and none of this spread's pages is in the changed entries
	// (sEntries), the older version is identical to the current one and there is nothing to lay
	// over it: the expensive rasterisation is skipped entirely. When no comparison has run
	// (sDB != targetDB) there is no way to tell, so the spread is rasterised as before.
	//
	// **The mode test is what makes this correct.** Without it, a Shift-peek in the Story mode did
	//   nothing at all -- not because the peek was broken, but because this always returned:
	//     - the Story mode also puts targetDB into sDB (Start goes through
	//       KCMDoMarkChangesDoc's full-comparison branch, allowIncremental being kFalse)
	//     - and that branch then empties toRaster, so **MakeEntry never runs** and sEntries is
	//       always empty
	//   The condition therefore held for every spread, and neither the rasterisation nor
	//   sShowOriginal ever happened. The drawing side (wantOrig in KCMDrawEventHandler) works in
	//   both modes, so **all that was missing was the picture**.
	// The reasoning behind the fix: this skip is an optimisation, not a specification, and it
	//   rests on one thing -- that sEntries is *the result of comparing pixels*. The Story mode has
	//   compared no pixels, so it has no grounds to say "this spread is unchanged", and it is
	//   therefore treated **exactly like a document that has not been compared**: rasterise.
	// @warning **read this before adding a third mode.** `== kKCMModePixel` is deliberate: it keeps
	//   the peek alive in any future mode that builds no sEntries, whereas `!= kKCMModeStory`
	//   would silently reintroduce the same bug there.
	if (KCMGetCompareMode() == kKCMModePixel && KCMDrawEventHandler::sDB == targetDB)
	{
		bool16 anyChanged = kFalse;
		for (int32 p = 0; p < np; ++p)
			if (KCMDrawEventHandler::sEntries.find(spread->GetNthPageUID(p)) !=
			    KCMDrawEventHandler::sEntries.end())
			{ anyChanged = kTrue; break; }
		if (!anyChanged)
			return;
	}

	// Is the whole spread already cached (same database, every page present in sOrigImages)? Then
	// it is reused as it stands.
	bool16 cached = (KCMDrawEventHandler::sOrigDB == targetDB);
	for (int32 p = 0; p < np && cached; ++p)
		if (KCMDrawEventHandler::sOrigImages.find(spread->GetNthPageUID(p)) ==
		    KCMDrawEventHandler::sOrigImages.end())
			cached = kFalse;
	// A changed zoom means the cached resolution no longer matches, so it is rebuilt. Within 2% it
	// is close enough to reuse.
	if (cached && KCMDrawEventHandler::sOrigScale > 0)
	{
		const PMReal d = abs(effScale - KCMDrawEventHandler::sOrigScale);
		if (d > KCMDrawEventHandler::sOrigScale * PMReal(0.02))
			cached = kFalse;
	}

	if (!cached)
	{
		// **Rasterising can trigger the lazy recomposition of an uncomposed story, and composing
		//   dirties the document.** If it was clean on the way in, it is clean on the way out.
		//   Every other rasterising route in KCM carries this guard -- Start (KCMCore.cpp), the
		//     partial re-comparison below, the book comparison, the overset scan -- and this one,
		//     which rasterises whole pages at up to 300dpi, was the exception. Never dirtying the
		//     model is central to KCM's design: merely peeking at a document must not end up
		//     asking the user to save it.
		//   The guards live inside this branch, so a cache hit -- the same spread peeked at again,
		//     which is the common case -- does not construct them.
		IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
		IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

		// New page -> old page comes from the exclusion pairing (registered pages, which have no
		// counterpart, left out and the rest matched in order). It is only needed in this branch,
		// the one that actually rasterises: rebuilding the pairing on every cache hit would be
		// wasted work, and cache hits are the common case.
		KCMDrawEventHandler::DropAllOrig();		// only one spread is peeked at; the rest goes
		KCMDrawEventHandler::sOrigDB = targetDB;
		KCMDrawEventHandler::sOrigScale = effScale;	// remembered so a later peek can tell whether to rebuild
		// The pairing is the same for every page of the spread, so it is built once before the loop.
		std::map<UID, UID> targetToSource;
		KCMBuildFullPairing(targetDB, sourceDB, targetToSource);
		for (int32 p = 0; p < np; ++p)
		{
			const UID tPageUID = spread->GetNthPageUID(p);
			std::map<UID, UID>::const_iterator mi = targetToSource.find(tPageUID);
			if (mi == targetToSource.end())
				continue;
			UIDRef tRef(targetDB, tPageUID);
			UIDRef sRef(sourceDB, mi->second);
			KCMDrawEventHandler::MakeOrigImage(tRef, sRef, peekDpi);	// a page that fails is simply not laid over
		}
	}

	KCMDrawEventHandler::sShowOriginal = kTrue;

	KCMInvalidateDB(targetDB);
}


// The shared core of the partial re-comparison: re-compare targetPages (page UIDs in targetDB) and
// update their rings. Each page's counterpart comes from the pairing -- ordinary pages by position
// with the registered ones left out, masters by name.
//   - every page is taken again with MakeEntry, so the difference reflects the current edit; a
//     page that is no longer different has its old ring removed
//   - the older-version image cache (sOrigImages) is stale and thrown away, to be rebuilt by the
//     next peek
//   - the ticks are pruned, and the layout, the scrollbar map and the Pages panel thumbnails are
//     brought up to date
//   outProcessed comes back with how many pages were really re-compared (pages skipped for having
//   no counterpart are not counted) and outChanged with how many of those differed. The return
//   says whether at least one page was processed.
//   A large enough selection brings up a progress bar with Cancel. On cancellation outCancelled is
//     set and **what has been updated so far is kept** -- unlike Start's comparison, nothing is
//     thrown away; the reasoning is in the loop below. The follow-up work (pruning the ticks,
//     redrawing, the thumbnails) happens even then.
static bool16 KCMRefreshComparisonCore(IDataBase* targetDB, IDataBase* sourceDB,
                                         const std::vector<UID>& targetPages,
                                         int32* outProcessed, int32* outChanged, bool16* outCancelled,
                                         int32* outFailed)
{
	if (outProcessed) *outProcessed = 0;
	if (outChanged)   *outChanged = 0;
	if (outCancelled) *outCancelled = kFalse;
	if (outFailed)    *outFailed = 0;
	if (targetDB == nil || sourceDB == nil || targetPages.empty())
		return kFalse;

	// Rasterising can trigger the lazy recomposition of an uncomposed story, and composing dirties
	//   the document. As on the Start route (KCMDoMarkChangesDoc): clean on the way in, clean on
	//   the way out.
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

	// Make sure the marks belong to this document. Only a wholesale swap when they were on another
	// one; normally the two already agree and nothing happens.
	if (KCMDrawEventHandler::sDB != nil && KCMDrawEventHandler::sDB != targetDB)
		KCMDrawEventHandler::DropAll();
	KCMDrawEventHandler::sDB = targetDB;

	// Build the exclusion pairing once, so that target -> source can be looked up.
	std::map<UID, UID> targetToSource;
	KCMBuildFullPairing(targetDB, sourceDB, targetToSource);

	// Re-compare the given pages and update their rings. The pages touched -- each target page and
	// its counterpart -- are collected so the Pages panel thumbnails can be purged per UID
	// afterwards. Both the changed and the unchanged go in: a page that is no longer different has
	// left sEntries, and its old ring has to be cleared just as surely.
	// **The pages to compare are settled first** (the progress bar needs the total, and pages with
	//   no counterpart drop out here) -- the same "decide the work, then do it" shape as Start's
	//   toRaster in KCMCore.cpp.
	std::vector<UID> toCompareT, toCompareS;
	toCompareT.reserve(targetPages.size());
	toCompareS.reserve(targetPages.size());
	for (size_t i = 0; i < targetPages.size(); ++i)
	{
		std::map<UID, UID>::const_iterator mi = targetToSource.find(targetPages[i]);
		if (mi == targetToSource.end())
			continue;	// registered pages and the like have no counterpart to compare against
		toCompareT.push_back(targetPages[i]);
		toCompareS.push_back(mi->second);
	}

	// Nothing eligible -- every selected page is registered, say -- returns here. That avoids a
	// progress bar with a total of zero, and skips the follow-up work below (pruning the ticks,
	// redrawing, the thumbnails), which is the "nothing happens" this used to do.
	if (toCompareT.empty())
		return kFalse;

	// The progress bar with its Cancel only appears for a large enough job. The threshold, and why
	//   KCM sets one of its own, are with kKCMProgressBarMinPages in KCMConstants.h.
	const int32 compareCount = (int32)toCompareT.size();
	const bool8 showBar = (compareCount >= kKCMProgressBarMinPages) ? kTrue : kFalse;
	PMString barTitle(compareCount == 1 ? "Refreshing 1 page..." : "Refreshing pages...");
	barTitle.SetTranslatable(kFalse);
	TaskProgressBar progress(barTitle, compareCount, showBar);
	progress.DisableChildProgressBars(kTrue);	// stop the rasterisation putting up bars of its own

	int32 changedCount = 0;
	int32 failedCount = 0;
	bool16 cancelled = kFalse;
	// The touched pages are **the pages whose picture this operation can change**. They travel on
	//   the notification at the end so the UI rebuilds those thumbnails and no others. They are
	//   sets rather than vectors because the ticks the prune removes are merged in below and must
	//   not appear twice.
	//   @warning **do not report the count from size()**: the set grows afterwards, so "how many
	//   pages were re-compared" is counted separately.
	int32 processedCount = 0;
	std::set<UID> touchedTargetPages, touchedSourcePages;
	for (size_t i = 0; i < toCompareT.size(); ++i)
	{
		PMString item("Page ");
		item.AppendNumber((int32)(i + 1));
		item.Append(" / ");
		item.AppendNumber(compareCount);
		item.SetTranslatable(kFalse);	// it holds numbers, so it is not a translatable string
		progress.DoTask(item);			// one step on; this is also where the previous one is marked done

		const UID tUID = toCompareT[i];
		const UID sUID = toCompareS[i];
		touchedTargetPages.insert(tUID);
		touchedSourcePages.insert(sUID);
		++processedCount;
		bool16 changed = kFalse;
		const ErrorCode mkErr = KCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tUID), UIDRef(sourceDB, sUID), changed);
		if (mkErr != kSuccess)
		{
			// **A page that could not be compared leaves its existing entry alone** (the causes are
			//   mismatched page sizes, a failed rasterisation, or being out of memory). Reading
			//   changed == kFalse as "no longer different" and calling DropOneEntry would let a
			//   temporary failure silently erase a correct ring. The old ring stays and the page is
			//   counted as failed for the status line.
			++failedCount;
			// **It is removed from the previous pairing, though.** The Start route does the same
			//   thing in the same place, and for the same reason: left in, the next incremental
			//   re-comparison decides "the pair is unchanged, reuse the previous result", never
			//   calls MakeEntry, and **the failure to update this page becomes permanent, wearing
			//   the appearance of "compared, no difference"**. Refresh exists to bring the selected
			//   pages up to date, so a page that failed is exactly the one that must be compared
			//   again at the next opportunity.
			//   @warning **only the previous pairing is touched**, never the entry. The two mean
			//     different things: the entry is the ring shown now, the previous pairing is what
			//     decides whether that ring may be reused.
			KCMDrawEventHandler::sPrevPairTargetToSource.erase(tUID);
		}
		else if (changed)
			++changedCount;
		else
		{
			// No longer different, so any ring left over from before has to go. The shared helper
			// clears the Source-side mapping (sSrcPageToTarget[sUID]) along with the entry, which
			// is why dropping goes through one function.
			KCMDrawEventHandler::DropOneEntry(tUID, sUID);
		}

		// Cancellation is tested at a safe point, with a page fully compared: WasCancelled pumps
		//   events, so it must not be called in the middle of a rasterisation. The kFalse argument
		//   means "do not set the global error state", which would otherwise make the commands that
		//   follow fail with it.
		// **Unlike Start's comparison, this stops and keeps what it has done.** The command updates
		//   only the selected pages to begin with, so stopping halfway leaves some pages updated
		//   and some still old -- the same state as running it on a narrower selection. It cannot
		//   produce what Start could: a whole document half compared and half not.
		if (progress.WasCancelled(kFalse))
		{
			cancelled = kTrue;
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// carry no error out of the cancellation
			break;
		}
	}
	if (outCancelled) *outCancelled = cancelled;

	// **A page that failed to rasterise must not leave an error behind either**, the same as the
	//   cancellation above.
	//   Of the two kinds of failure only the second can set one -- a page-size mismatch rasterises
	//   fine, while a failed SnapshotUtilsEx::Draw or an allocation failure happens inside the SDK
	//   where there is no way to find out -- so this errs towards the side that cannot be measured.
	//   Clearing it is safe because the failures are reported in full: they come back in outFailed
	//   and reach the status line as "failed=N", so nothing depends on the error state carrying
	//   them. Left set, it would take down the clean-up that follows (pruning the ticks, the work
	//   the UI does on the notification) and whatever command the caller issues next -- see
	//   ProcessCommand in CmdUtils.h on protective shutdown, and a command sequence would roll back
	//   entirely.
	if (failedCount > 0)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	// The reported count is how many pages actually reached MakeEntry or DropOneEntry: selected
	// pages that dropped out for having no counterpart are not counted, and a cancelled run reports
	// what it got through. That is what keeps "refreshed N" in the status line honest.
	if (outProcessed) *outProcessed = processedCount;
	if (processedCount == 0)
		return kFalse;

	// The older-version images are stale now; the next peek rebuilds them at the current zoom.
	KCMDrawEventHandler::DropAllOrig();

	// The ticks: a page that lost its ring in this partial re-comparison loses its tick with it --
	//   "the frame is gone, and the memory of having checked it goes with it".
	//   **This must run before the KCMInvalidateDB below**: untick after invalidating and the
	//   layout is redrawn with the old ticks still on it.
	// **The pages the prune unticked are collected and merged into the touched set.** Without them
	//   the per-UID purge misses those pages every time: losing a tick changes the thumbnail, but
	//   once the tick is gone the page is in none of the sets the current state can produce, so it
	//   **cannot be recovered afterwards** (see outUnchecked in KCMPageCheck.h).
	std::map<IDataBase*, std::set<UID> > uncheckedByDoc;
	KCMPageCheckPruneToMarked(&uncheckedByDoc);
	{
		std::map<IDataBase*, std::set<UID> >::const_iterator u = uncheckedByDoc.find(targetDB);
		if (u != uncheckedByDoc.end())
			touchedTargetPages.insert(u->second.begin(), u->second.end());
		u = uncheckedByDoc.find(sourceDB);
		if (u != uncheckedByDoc.end())
			touchedSourcePages.insert(u->second.begin(), u->second.end());
		// @warning **documents other than the target and the source are not collected** -- the
		//   notification carries at most two. Nothing can be in them today: ticks only go on the
		//   Target and the Source of a running comparison, and Stop clears them all
		//   (KCMPageCheck.h). Should a third document ever become tickable, this is where the
		//   pages would go missing.
	}

	KCMInvalidateDB(targetDB);
	// The Source's layout views are redrawn too: gaining or losing entries changes how its
	// permanently visible frames (Always Show Marks on Source) and its ticks look as well.
	if (sourceDB != targetDB)
		KCMInvalidateDB(sourceDB);

	// The scrollbar map, the Pages panel thumbnails and Prev/Next's position all belong to the UI,
	//   so this route -- which does not go through KCMDoMarkChangesDoc -- reports them in one
	//   notification too.
	//   **navReset is kFalse**: throwing away the traversal's anchor after re-comparing a few
	//     selected pages would send Prev/Next back to the start every time one page is refreshed.
	//     The document has not changed, so the anchor is still valid.
	//   **The touched pages travel with it**, which is what lets the UI purge per UID.
	//   **A full re-comparison cannot do this and this route can**: the full one would need the set
	//     of pages that carried a ring *before* it ran, and by notification time that is gone. Here
	//     the pages are decided before the loop (toCompareT/S), so the set is in hand from the start.
	//   @warning **anything missing from the set keeps a stale thumbnail** (purging every page
	//     could not miss, which is what it was doing before). The set is: (1) the pages
	//     re-compared, target and counterpart, including those whose ring disappeared for being
	//     unchanged again, and (2) the pages the prune unticked above. The older-version overlay
	//     (DropAllOrig) is deliberately not in it -- it is never drawn into a thumbnail, the
	//     drawing side rejecting it with `wantOrig && !isThumb` -- and neither the registrations
	//     (green "/") nor the overflow can change on this route.
	KCMNotifyDocsPages(kKCMMarksRebuiltMessage,
	                     targetDB, touchedTargetPages,
	                     sourceDB, touchedSourcePages,
	                     kFalse /*navReset*/);

	if (outChanged) *outChanged = changedCount;
	if (outFailed)  *outFailed = failedCount;
	return kTrue;
}

// The two documents of a RUNNING PIXEL comparison, or kFalse (leaving both out arguments nil).
//
// **The command and the test that greys the command out ask exactly this**, and asking it in one
// place is what keeps the menu's answer and the command's answer from drifting apart
// ([[one-question-one-place]]) -- the pair below is the same shape as KCMResolveComparisonPair,
// which exists for the same reason.
// @warning **the Story mode is excluded on purpose**: it rasterises no page, so "re-compare these
//   pages" has nothing to do there. It has a refresh of its own ("Refresh Story Comparison" on a
//   Story Edits row), so each mode has exactly one way to refresh and the two do not overlap.
static bool16 KCMQueryPixelComparePair(IDataBase*& outTarget, IDataBase*& outSource)
{
	outTarget = nil;
	outSource = nil;

	if (!KCMIsArmed())
		return kFalse;
	if (KCMGetCompareMode() == kKCMModeStory)
		return kFalse;

	outTarget = KCMArmedTargetDB();
	outSource = KCMArmedSourceDB();
	return (outTarget != nil && outSource != nil) ? kTrue : kFalse;
}


// Re-detect and update the comparison of the pages selected in the Pages panel -- the body behind
// the context-menu item "Refresh Page Comparison". It runs only while a comparison is armed and
// with the Target as the frontmost document.
// outPages is how many pages were really re-compared (pages with no counterpart are not counted)
// and outChanged how many of those differed. The return says whether at least one was processed.
bool16 KCMRefreshComparisonForSelectedPages(int32* outPages, int32* outChanged, bool16* outCancelled, int32* outFailed)
{
	if (outPages)     *outPages = 0;
	if (outChanged)   *outChanged = 0;
	if (outCancelled) *outCancelled = kFalse;
	if (outFailed)    *outFailed = 0;

	// @warning the command refuses on its own account, not only through the greyed-out menu:
	//   KCMRefreshComparisonAvailable has normally removed the item already, but **an ActionID can
	//   be given a keyboard shortcut**, so this side has entry points the menu's appearance does
	//   not govern.
	IDataBase* targetDB = nil;
	IDataBase* sourceDB = nil;
	if (!KCMQueryPixelComparePair(targetDB, sourceDB))
		return kFalse;

	// Read the Pages panel's selection through the reader Register and Check share
	// (KCMPageMap.cpp). Nothing happens unless the document that selection belongs to is the
	// Target; over the Source the menu item is not offered at all, which is what
	// KCMRefreshComparisonAvailable arranges.
	IDataBase* db = nil;
	std::vector<UID> selPages;
	// includeMasters=kTrue: master spreads are compared, so they can be re-compared too. It only
	//   means anything together with the master pairs KCMRefreshComparisonCore puts in its table.
	if (!KCMPageMapReadSelection(db, selPages, kTrue /*includeMasters*/) || db != targetDB)
		return kFalse;

	// The core is driven by Target pages, and the Target is the frontmost document here, so the
	// selection can be handed straight over.
	std::vector<UID> targetPages = selPages;

	int32 processed = 0, changed = 0, failed = 0;
	bool16 cancelled = kFalse;
	const bool16 ok = KCMRefreshComparisonCore(targetDB, sourceDB, targetPages, &processed, &changed, &cancelled, &failed);
	// The cancellation is reported whether the core succeeded or not. The two cannot actually
	//   coincide -- the core only returns kFalse for "nothing eligible", where cancelled is kFalse
	//   as well -- but reporting it without relying on that keeps the two independent.
	if (outCancelled) *outCancelled = cancelled;
	if (!ok)
		return kFalse;

	// (Prev/Next's position readout and the enabling of its buttons ride on the
	//  kKCMMarksRebuiltMessage that KCMRefreshComparisonCore above sends.)

	// The Story Edits list is rebuilt from here as well, and **it cannot be rebuilt for the
	//   selected pages alone**: one story can flow across pages that were re-compared and pages
	//   that were not, so there is no way to split it per page. Rebuilding the whole list is
	//   KCMRebuildStoryEdits' job.
	// @warning without this, the list kept showing its pre-edit state after a Refresh and only
	//   after a Refresh.
	KCMRebuildStoryEdits(targetDB, sourceDB);

	if (outPages)   *outPages = processed;
	if (outChanged) *outChanged = changed;
	if (outFailed)  *outFailed = failed;
	return kTrue;
}

// Whether the "Refresh Page Comparison" menu item should be enabled (for UpdateActionStates):
// kTrue while a comparison is armed, in the **Pixel mode**, with the Target frontmost. A context
// menu does not show disabled items, so over the Source or in the Story mode the item disappears
// altogether; the reasoning is in the branch below.
// It does not look at the selection: right-clicking a page normally selects it, and even with
// nothing selected the command safely does nothing and reports "no comparable pages".
// **The command itself decides from KCMPageMapReadSelection's database**, which comes from the
//   same KCMActiveDocDB(), so the two can never disagree about which document is meant. The only
//   difference between them is that one looks at the selection and the other does not. Keep them
//   on the same source ([[one-question-one-place]]).
bool16 KCMRefreshComparisonAvailable()
{
	// **Not offered in the Story mode** (KCMQueryPixelComparePair refuses there). What this item
	//   rebuilds is the result of comparing pixels, and the Story mode rasterises no page at all
	//   (KCMDoMarkChangesDoc empties toRaster). Pressing it would spend time redrawing the selected
	//   pages one by one and **change nothing on screen**, the drawing side holding the rings back
	//   with `drawRings = (mode != Story)`.
	// Disabling it **removes the item entirely**, a context menu showing no disabled items. That
	//   is the intent.
	IDataBase* targetDB = nil;
	IDataBase* sourceDB = nil;
	if (!KCMQueryPixelComparePair(targetDB, sourceDB))
		return kFalse;
	IDataBase* db = KCMActiveDocDB();
	return (db != nil && db == targetDB) ? kTrue : kFalse;
}

// The last line of defence: are the armed Target and Source still in the IDocumentList?
//   Normally the close responder (KCMHandleDocsClosed) has disarmed things long before, so this
//   never fails; it exists so that a responder that did not fire cannot let a released IDataBase
//   reach the sampler or the peek.
//   When one of them is gone, KCMHandleDocsClosed() performs the full clean-up that Stop would --
//   sPeek* cleared included -- and kFalse comes back.
bool16 KCMArmedDocsAlive()
{
	if (!sPeekArmed || sPeekTargetDB == nil || sPeekSourceDB == nil)
		return kFalse;
	ISession* session = GetExecutionContextSession();	// nil is possible during the shutdown sequence
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil ||
	    !KCMIsDbAlive(docList, sPeekTargetDB) ||
	    !KCMIsDbAlive(docList, sPeekSourceDB))
	{
		KCMHandleDocsClosed();
		return kFalse;
	}
	return kTrue;
}

//========================================================================================
// KCMPeekStartup
//   The application's startup/shutdown service. There is nothing to do at startup; it exists to
//   release what is held at shutdown -- the mark and older-version image buffers, the peek's armed
//   state, and the rest. The three files that own UI state clean themselves up through the
//   Shutdown entry points they publish, which this one calls.
//
// **This service only ever runs on the main thread.** KCM is a kModelPlugIn, and left alone the
//   guide's "Threading and startup/shutdown services" (vol1-07) applies:
//     "kCStartupShutdownProviderImpl derives its implementation of GetThreadingPolicy from
//      CServiceProvider. **If the startup/shutdown service boss resides in a model plug-in, the
//      service will be called on both main and background thread startup and shutdown.**"
//   Since Shutdown() below throws away the entire comparison state, that would mean **every PDF
//     export wiping out every mark**.
//   **The fix is in the resource, not here**: KCM.fr gives kKCMPeekStartupBoss the class
//     kCMainThreadStartupShutdownProviderImpl, with the reasoning next to it. Adobe's own
//     DiagnosticLog (DiagLogClass.fr) does exactly this -- a model plug-in whose startup/shutdown
//     service alone is pinned to the main thread, with a comment saying why.
//   @warning **do not instead guard Shutdown() with "do nothing on a background thread"** -- that
//     can also skip the real shutdown. A threading policy belongs in the service's declaration.
//   @warning **what the change did alter is the timing.** The old kLazyStartupShutdownProviderImpl
//     ran as an idle task after startup completed; this one does not, so **Startup() runs
//     earlier**. That is safe here because **Startup() is empty**. Check this assumption again
//     before giving Startup() work to do.
//========================================================================================
class KCMPeekStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KCMPeekStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	~KCMPeekStartup() {}

	virtual void Startup();
	virtual void Shutdown();
};

CREATE_PMINTERFACE(KCMPeekStartup, kKCMPeekStartupImpl)

void KCMPeekStartup::Startup()
{
	// **The model's startup does nothing.** Everything that used to happen here belongs to the UI
	//   -- restoring the panel state, subscribing to batch closes, following the panel's
	//   visibility -- and lives in KCMUIStartup.cpp. Splitting the plug-in in two is what revealed
	//   that the original startup work had been UI work all along.
	//   @warning **the method stays even while it is empty** (IStartupShutdownService's contract).
	//
	// (Registering the global page-item adornment was tried here and **taken out again**.
	//  @warning **this Startup is pinned to the main thread** (kCMainThreadStartupShutdownProviderImpl
	//    in the .fr), which it has to be because Shutdown() below throws the comparison state
	//    away -- but **an adornment registration does not cross threads**, so registering on the
	//    main thread alone means **nobody draws on a background thread**, which is where the UI's
	//    PDF export runs (measured).
	//  The registration therefore lives in a service that runs once per execution context,
	//    kKCMRingAdornmentStartupBoss, implemented at the end of KCMRingAdornment.cpp.
	//  **Why it could not share this boss is the design point**: what Shutdown() throws away
	//    decides the threading policy, so **work that throws away different things needs a boss of
	//    its own**.)
}

void KCMPeekStartup::Shutdown()
{
	// **This function only ever runs on the main thread.** KCM is a kModelPlugIn, so left alone the
	//   guide (vol1-07) has it called on every background thread's startup and shutdown too -- the
	//   DropAll below and everything after it would run, and **every PDF export would wipe out
	//   every mark**. The fix is in the .fr's declaration
	//   (kCMainThreadStartupShutdownProviderImpl); the reasoning is above the class.
	//   @warning **do not add a "do nothing on a background thread" guard here** -- that can also
	//     skip the real shutdown. A threading policy belongs in the service's declaration, which
	//     is what the guide provides one for.
	//
	// The UI's own clean-up -- the deferred thumbnail idle task, the pending batch close, the
	//   translucency subscription and its timer, the held-down HUD's font -- lives in
	//   KCMUIStartup.cpp, **in an order that matters and is kept over there**: unsubscribe first,
	//   then dismantle, so that no Update runs against code that is going away.
	// (Removing the adornment moved to kKCMRingAdornmentStartupBoss along with its registration.
	//  @warning **this Shutdown only runs on the main thread**, so removing it here would leave
	//    whatever was registered on a background execution context behind. Registering and
	//    removing belong to **one service with one threading policy**.)

	// Release the mark and older-version image buffers.
	KCMDrawEventHandler::DropAll();
	KCMDrawEventHandler::DropAllOrig();
	// **Every other static container is emptied on the same principle.** DropAll and DropAllOrig
	//   only touch the comparison's own state (sEntries, sOrigImages, the pairings, the overflow),
	//   which left the Find Overset sets, the registrations, the ticks and the Hide Unchanged
	//   record holding heap until the static destructors ran at plug-in unload. That has never
	//   caused trouble on Windows, but macOS unloads in a different order, so KCM's rule is that
	//   **no live buffer survives to static destruction** -- the same reason its file-static
	//   PMStrings are cleared.
	//   None of these dereferences a pointer; each only empties a container, which is safe at any
	//   point in the shutdown sequence.
	KCMDrawEventHandler::DropOverset();	// sOversetPages / sOversetLocs
	KCMPageMapClearAllDocs();				// the registrations (Added/Removed)
	KCMPageCheckClearAllDocs();			// the ticks
	KCMResetHideUnchanged(kFalse);		// the Hide Unchanged record (kFalse: the documents are not touched at all)
	KCMInvalidatePageNumberMarkerRects();	// the page-number exclusion rectangles
	// The hover -> other pairing cached while Alt + left is held (sDragCacheH2O in
	//   KCMColorSampler.cpp).
	//   @warning what used to empty it was the UI's KCMCmykShutdown -> EndColorDrag, **behind a nil
	//     check** (kUtilsBoss may already be gone during shutdown), so **it could simply not
	//     happen**. A model-side static is closed by the model's Shutdown; calling it twice is
	//     idempotent, since all it does is clear.
	KCMSampleCmykEndDrag();
	// The Story Edits list. **Unlike the others its rows hold PMStrings**, so forgetting it means
	//   static PMStrings being destructed at unload -- the very thing KBS recorded after forgetting
	//   it three times (see ShutdownCleanup in KBSResultTree.h). It touches no UI, only drops the
	//   rows, so it is safe during shutdown.
	KCMStoryList::ShutdownCleanup();
	// The Story mode's marks. **Until they moved into the model plug-in this clean-up had no
	//   caller at all**: while the marks lived in the UI there was no main-thread-only entry point
	//   to pair it with, and moving them here is what created the right doorway. All it does is
	//   raise the flag that says "draw nothing and redraw nothing from now on" and empty the sets
	//   -- safe during shutdown, that flag being exactly what keeps it from redrawing a closing
	//   document.
	// @warning **this must not go in an ordinary model-side startup/shutdown service**: it would be
	//   called on every background thread teardown, and the marks would vanish on every PDF export
	//   (the trap recorded at the top of this function). Here that restriction is already in force,
	//   so here is where such work belongs.
	KCMStoryMarker::Shutdown();
	// Two more statics holding PMStrings, each keeping the text of the last run until unload:
	//     - KCMClearBookResultText ... the book comparison's result (one line per chapter, which
	//       app.kcmBookResult returns)
	//     - KCMClearExportMessage  ... the TSV export's message, which contains a full path
	//   Both only clear, dereference nothing, and are idempotent, so they are safe in any order.
	KCMClearBookResultText();
	KCMClearExportMessage();
	// The panel's remembered status line (sSessionStatus) is the third of that shape.
	//   @warning what used to empty it was the UI's KCMUIStartup, through the Facade and behind a
	//     nil check, so **it did not happen once kUtilsBoss had gone**. Same conclusion as the CMYK
	//     cache above: **a model-side static is closed by the model's Shutdown**. The UI's call
	//     stays; calling it twice only clears twice.
	KCMClearSessionStatus();
	// The peek's armed state goes too. Left standing, a kAfterCloseDoc responder firing after
	// shutdown could have KCMHandleDocsClosed recompute comparisonDocClosed from a stale sPeek*.
	// The normal order -- documents close, then Shutdown -- should never allow that, so this is
	// defensive; the pointers are only assigned nil, never dereferenced.
	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;
	// The chosen Target/Source ("Set as Target" / "Set as Source") go with them, for exactly the
	// same reason: they are model-side statics that the same responder path reads
	// (KCMForgetChosenDocsThatClosed). They live in KCMComparisonRun.cpp, so the clearing is
	// reached through a function of its own -- as with the book result and the export message
	// above.
	// ⚠**Here and not in the close sweep's comparisonDocClosed branch**: that branch is the
	// comparison's own all-or-nothing clean-up, and a choice outlives a Stop by design.
	KCMClearChosenDocs();

	// (Dropping the sync caches, clearing the sync flags and the CMYK clean-up all live in the UI's
	//  KCMUIStartup.cpp, that state belonging to UI files.)

	// The old page-number badge's font cache goes for the same reason as everything above: nothing
	// live may reach static destruction.
	KCMReleaseOldNumFontCache();
}

//========================================================================================
// The arm, disarm and state accessors declared in KCMCore.h. They live here so that they can share
// the file-local peek state above.
//========================================================================================

void KCMDoArmMousePeek(IDataBase* targetDB, IDataBase* sourceDB)
{
	// A different pair means the cached older-version images are of the wrong document.
	if (sPeekSourceDB != sourceDB || sPeekTargetDB != targetDB)
		KCMDrawEventHandler::DropAllOrig();

	// Dropping the sync caches (KCMViewSync) and resetting the peek's gesture state
	//   (KCMPeekGesture) are not done here: both are UI state. Arming is immediately followed by
	//   KCMStartComparisonFor sending kKCMMarksRebuiltMessage, and the UI resets its own state on
	//   receiving that.
	sPeekTargetDB = targetDB;
	sPeekSourceDB = sourceDB;
	sPeekArmed = kTrue;
	KCMDrawEventHandler::sMarksTempHidden = kFalse;	// in case a held-down flag was left standing
	KCMDrawEventHandler::sSrcMarksPressed = kFalse;
	KCMDrawEventHandler::sMarksVisible = kFalse;	// back to hidden: while armed the frames still only show under a press
}

void KCMDoDisarmMousePeek(IDataBase* db)
{
	// Remember which document was armed before clearing it. The caller's db is whatever was active
	// when the command ran, which may be the Source or an unrelated third document, and the armed
	// target's frames still have to disappear at once -- it can be on screen at the same time in a
	// tiled layout.
	IDataBase* armedTargetDB = sPeekTargetDB;

	// As on the arming side, dropping the sync caches and clearing the gesture state are left to
	//   the UI: disarming is immediately followed by KCMStopComparison sending
	//   kKCMMarksClearedMessage.
	// (The lent Source's registration is NOT dropped here: it stays chosen after a Stop, as a
	//  chosen document does -- KCMExternalSource.h. The lender's Release is what ends it.)
	sPeekArmed = kFalse;
	sPeekTargetDB = nil;
	sPeekSourceDB = nil;
	KCMDrawEventHandler::sMarksTempHidden = kFalse;
	KCMDrawEventHandler::sSrcMarksPressed = kFalse;
	KCMDrawEventHandler::sMarksVisible = kFalse;
	KCMDrawEventHandler::DropAllOrig();	// clears sShowOriginal as well as freeing the images

	KCMInvalidateDB(armedTargetDB);
	if (db != armedTargetDB)
		KCMInvalidateDB(db);
}

// The state accessors the panel reads: an armed peek is what "a comparison is running" means.
bool16     KCMIsArmed()        { return sPeekArmed; }
IDataBase* KCMArmedTargetDB()  { return sPeekTargetDB; }
IDataBase* KCMArmedSourceDB()  { return sPeekSourceDB; }

// KCMIsComparedDoc (declared in KCMCore.h) -- the three above in the combination the per-page
// flags always want. It sits here because that is where the three it is built from live.
bool16 KCMIsComparedDoc(IDataBase* db)
{
	return KCMIsArmed() && (db == sPeekTargetDB || db == sPeekSourceDB);
}

//========================================================================================
// KCMHandleDocsClosed (declared in KCMCore.h)
//   Called right after documents close (from the kAfterCloseDoc responder). Every database KCM is
//   tracking -- the marks' sDB, the older version's sOrigDB, the armed target and source -- is
//   checked against the IDocumentList. The signal does not say which document closed, so this
//   liveness sweep works it out, the same way the drawing side does but without waiting for a draw.
//
//   **If any database involved in the comparison has closed, everything is cleared**, exactly as
//   the Stop button would, rather than patching up individual statics. Before that, only sDB
//   closing cleared the marks: closing the older document alone disarmed the peek and put the
//   button back to Start while the target stayed open with its frames still on it.
//
//   **A closed database pointer is only ever compared against FindDocByDataBase, never
//   dereferenced.** A closed document's IDataBase may already be freed, so nothing that would
//   dereference it (KCMDoDisarmMousePeek and its InvalidateViews, say) is called for it. The
//   counterpart may still be open -- in a tiled layout, for instance -- and that one has been
//   checked, so its views are invalidated to clear the frames at once.
//========================================================================================
void KCMHandleDocsClosed()
{
	// **A background thread does nothing here.**
	//
	//   This function throws away all of KCM's state on the reasoning that **a tracked database
	//   not in the document list has closed**.
	//   @warning **that reasoning is always wrong on a background thread**, which sees **a clone
	//     with a different pointer** (guide vol1-07), so the sDB the main thread recorded is **open
	//     and yet not in the list**.
	//   The damage was reproduced: exporting a PDF with Document.asynchronousExportFile() fires
	//     kAfterCloseDoc on the background thread when the export's clone is closed, this ran, and
	//     **every mark disappeared** ("marks cleared", with the menu items going grey).
	//
	//   **The guard is at this one entry point, not in each caller** ([[one-question-one-place]]).
	//     There are three callers -- KCMDocResponder for kAfterCloseDoc, the drawing event's
	//     safety net, and the peek's liveness check -- but "a background thread cannot tell whether
	//     a document is still open" is a property of this function, not of who calls it.
	//   **Nothing is missed**: documents close on the main thread, so kAfterCloseDoc always arrives
	//     there too. The background one is a different event entirely -- a clone being finished with.
	if (!KCMIsMainThread())
		return;

	// The session can be nil during the shutdown sequence. Without it there is no way to judge
	// liveness at all, so nothing is cleaned up here and Shutdown disposes of the state instead.
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	// The chosen Target/Source ("Set as Target" / "Set as Source"): whichever names the document
	// that has just closed is forgotten, and **the other one is left standing**.
	//   This is the one thing here that is NOT part of "clean up if a compared document closed".
	//   A choice can be made with nothing being compared at all, so it has to be swept on every
	//   close, not only when comparisonDocClosed comes out true below -- and it is swept
	//   per document, where the comparison's own clean-up is all-or-nothing (one of the two
	//   closing makes the whole comparison meaningless; one of the two choices closing leaves the
	//   other perfectly good).
	//   Memory only, so it is safe while quitting; the pointers are compared, never dereferenced.
	KCMForgetChosenDocsThatClosed(docList);

	// A document has closed, so neither the page structure nor any database pointer can be relied
	// on: the sync caches are dropped unconditionally. They belong to the UI (KCMViewSync), so what
	// actually drops them is the UI on receiving the notification at the end of this function --
	// and **the unconditional part is preserved by not filtering that notification on `changed`**.
	// The page-number exclusion rectangles go for the same reason: their keys contain database
	// pointers, so no entry of a closed document may remain. The surviving documents' entries are
	// simply measured once more on the next draw.
	KCMInvalidatePageNumberMarkerRects();

	// **While the application is quitting, no UI work is done at all** -- no widgets removed from
	// the strip, no InvalidateViews, no thumbnail idle task, no panel or status update -- and only
	// the state in memory is disposed of. The order in which windows and panels are torn down
	// during a quit is platform-dependent, and touching a widget mid-teardown is the classic shape
	// of a macOS-only crash on quit. A normal quit closes all documents first, while still running,
	// so the interactive close and the quit's close-all phase both still get the full clean-up.
	const bool16 quitting = KCMAppIsQuitting();

	// (Whether a batch close is in progress -- several documents closing in a row, where the UI
	//  clean-up is held back and run once at the end -- **is decided by the UI**.
	//  KCMBatchCloseInProgress() is UI state, and the model asking for it was the flow going the
	//  wrong way. The observer that receives the notification at the end decides whether to defer.
	//  What stays in the model is the quitting test above, which is a model question and also
	//  decides whether commands may be issued at all.)

	// Find Overset is independent of the comparison: if the document it scanned has closed, its
	//   state goes. The drawing side only ever compares sOversetDB by pointer and never
	//   dereferences it, but leaving a closed one in place means drawing crosses on the wrong
	//   document once its address is reused, so it is cleared here deliberately. Memory only, so it
	//   is safe while quitting, and no redraw is needed -- the crosses were never on any other
	//   document, and the closed one's window is gone.
	if (KCMDrawEventHandler::sOversetOn && KCMDrawEventHandler::sOversetDB != nil &&
	    !KCMIsDbAlive(docList, KCMDrawEventHandler::sOversetDB))
	{
		KCMDrawEventHandler::DropOverset();
	}

	bool16 changed = kFalse;

	// Has any database the comparison depends on closed -- the marks' sDB, the older version's
	// sOrigDB, the Source frames' sSrcDB, or the armed target and source? sSrcDB is in practice the
	// same document as sPeekSourceDB, but it is checked separately so that the answer does not
	// depend on anything still being armed.
	const bool16 comparisonDocClosed =
		(KCMDrawEventHandler::sDB     != nil && !KCMIsDbAlive(docList, KCMDrawEventHandler::sDB))     ||
		(KCMDrawEventHandler::sOrigDB != nil && !KCMIsDbAlive(docList, KCMDrawEventHandler::sOrigDB)) ||
		(KCMDrawEventHandler::sSrcDB  != nil && !KCMIsDbAlive(docList, KCMDrawEventHandler::sSrcDB))  ||
		(sPeekArmed &&
		 ((sPeekTargetDB != nil && !KCMIsDbAlive(docList, sPeekTargetDB)) ||
		  (sPeekSourceDB != nil && !KCMIsDbAlive(docList, sPeekSourceDB))));

	// The surviving databases are declared outside the block below because the notification at the
	//   end of the function carries them too.
	//   @warning **only pointers that passed FindDocByDataBase go in here**; a closed one never
	//   does. Whoever receives the notification dereferences them, and a closed IDataBase* has an
	//   address that gets reused.
	IDataBase* survivorTargetDB = nil;
	IDataBase* survivorOrigDB   = nil;
	IDataBase* survivorSrcDB    = nil;	// the document showing Source-side frames (Always Show Marks on Source)

	if (comparisonDocClosed)
	{
		// Record the databases that are still open before DropAll and DropAllOrig clear them. They
		// have passed the liveness check, so invalidating their views later is safe.
		if (KCMDrawEventHandler::sDB != nil && KCMIsDbAlive(docList, KCMDrawEventHandler::sDB))
			survivorTargetDB = KCMDrawEventHandler::sDB;
		if (KCMDrawEventHandler::sOrigDB != nil && KCMIsDbAlive(docList, KCMDrawEventHandler::sOrigDB))
			survivorOrigDB = KCMDrawEventHandler::sOrigDB;
		if (KCMDrawEventHandler::sSrcDB != nil && KCMIsDbAlive(docList, KCMDrawEventHandler::sSrcDB))
			survivorSrcDB = KCMDrawEventHandler::sSrcDB;
		if (sPeekArmed)
		{
			if (survivorTargetDB == nil && sPeekTargetDB != nil && KCMIsDbAlive(docList, sPeekTargetDB))
				survivorTargetDB = sPeekTargetDB;
			if (survivorOrigDB == nil && sPeekSourceDB != nil && KCMIsDbAlive(docList, sPeekSourceDB))
				survivorOrigDB = sPeekSourceDB;
		}

		// The full clean-up, exactly as the Stop button performs it.
		KCMDrawEventHandler::DropAll();		// sDB = nil, the mark entries destroyed
		KCMDrawEventHandler::DropAllOrig();	// sOrigDB = nil, the older-version overlay destroyed
		sPeekArmed     = kFalse;
		sPeekTargetDB  = nil;
		sPeekSourceDB  = nil;
		// (Clearing the peek's gesture state is UI state, so the UI does it on the notification.)
		KCMDrawEventHandler::sMarksTempHidden = kFalse;
		KCMDrawEventHandler::sSrcMarksPressed = kFalse;
		KCMDrawEventHandler::sMarksVisible = kFalse;
		// Stop clears the registrations, and closing one of the compared documents used to leave
		//   them standing. This is the same full clean-up, so they go here too: left behind, they
		//   stay on whichever of the two survived and creep into the pairing at the next Start.
		//   (Emptying a map, so nothing is dereferenced.)
		KCMPageMapClearAllDocs();
		KCMPageCheckClearAllDocs();	// the ticks, which only exist while a comparison runs
		// The Story Edits list goes as well: its rows hold story and page UIDs of the Target, which
		//   point at nothing once that document has closed. The panel's tree and heading are
		//   rebuilt from the real state by the UI, so throwing the state away is all that is needed
		//   here -- safe while quitting, being an empty of a vector with no dereference.
		// @warning **do not re-decide here whether what closed was one of the compared documents.**
		//   A closed document's UIDRef and IDataBase* cannot establish identity, their addresses
		//   being reused ([[uidref-reuse-after-close]]). comparisonDocClosed above already answered
		//   that through FindDocByDataBase, so this rides on its answer.
		KCMStoryList::Clear();
		// The traversal's anchor is forgotten too. Stop does that, and this "clean-up as Stop
		//   would" was the one route that did not: a closed document's page UID left as the anchor
		//   can happen to match a UID in the next document and start the traversal partway through.
		//   The anchor is UI state, so it travels as navReset on the notification at the end.
		changed = kTrue;

		// (The screen-side clean-up that used to be here -- removing the strip, or redrawing the red
		//  band when Find Overset is on by itself; redrawing the survivor; **deferring the thumbnail
		//  rebuild to the next idle** -- is all done by the UI on the
		//  kKCMComparisonDocsClosedMessage below.
		//  @warning the deferral is not an optimisation: when the Target is what closed and the
		//    survivor is about to become active, a ForceRedraw issued now happens mid-switch and
		//    the frames survive it.
		//  Whether to skip the work while quitting, and whether to defer during a batch close, are
		//  the UI's decisions too.)

		// Redrawing the surviving document's layout views, so its frames go at once, is the model's
		// job: this is the side that holds the drawing data. While quitting the windows are going
		// anyway, so nothing is touched.
		if (!quitting)
		{
			KCMSayStatus("marks cleared");	// the same message the Stop button reports

			KCMInvalidateDB(survivorTargetDB);
			if (survivorOrigDB != survivorTargetDB)
				KCMInvalidateDB(survivorOrigDB);
			if (survivorSrcDB != survivorTargetDB && survivorSrcDB != survivorOrigDB)
				KCMInvalidateDB(survivorSrcDB);
		}
	}

	// Clearing up after the "Hide Unchanged Spreads" toggle, on both sides. It is reset, and the
	// toggle goes off, when either document it hid spreads in has closed, or when the marks were
	// all cleared above -- comparisonDocClosed means the grounds for calling a spread "unchanged"
	// are gone. KCMResetHideUnchanged(kTrue) checks liveness itself, showing the surviving
	// document's spreads again and merely discarding the state of a closed one, so kTrue is always
	// the right argument here. An unrelated third document closing changes nothing.
	IDataBase* hideDB    = KCMGetHideUnchangedDB();
	IDataBase* hideSrcDB = KCMGetHideUnchangedSrcDB();
	if (hideDB != nil || hideSrcDB != nil)
	{
		const bool16 hideTargetClosed = (hideDB    != nil && !KCMIsDbAlive(docList, hideDB));
		const bool16 hideSourceClosed = (hideSrcDB != nil && !KCMIsDbAlive(docList, hideSrcDB));
		if (hideTargetClosed || hideSourceClosed || comparisonDocClosed)
		{
			// While quitting, kFalse discards the state without issuing the command that shows the
			// spreads again (kHideSpreadCmdBoss): pushing a model change through during teardown is
			// dangerous. A normal quit closes all documents while still running, so this branch is
			// only reached in abnormal cases.
			KCMResetHideUnchanged(quitting ? kFalse : kTrue);
			changed = kTrue;
		}
	}

	// The registrations of closed documents are dropped, state only and with no dereference. They
	// do not affect what the panel shows, so `changed` is deliberately not set.
	KCMPageMapSweepClosedDocs();
	KCMPageCheckSweepClosedDocs();	// and the ticks, the same way

	// All of the screen-side clean-up travels on **this one notification**.
	//
	// @warning **it is sent unconditionally, without consulting `changed`.** The view sync's page
	//   rectangles and pairing caches (KCMViewSync) are dropped whichever document closed -- this
	//   function used to call KCMInvalidateSyncCaches() unconditionally at its head. Matching the
	//   panel to the state is idempotent, so arriving here because an unrelated document closed
	//   does no harm.
	//
	// @warning **it is sent while quitting too.** "Touch no widget while quitting" and "hold the
	//   clean-up back during a batch close and run it once at the end" **are both UI concerns**,
	//   so the UI's observer decides them from KCMAppIsQuitting() and KCMBatchCloseInProgress().
	//   That is what keeps the flow one-way: KCMBatchCloseInProgress() is UI state, and **the model
	//   asking for it was the flow going backwards**.
	//
	// The payload, only when the comparison has ended: up to three **surviving** databases (the
	// Target, the older-version overlay, the Source frames). A closed one is never included, and
	// navReset is likewise only raised when the comparison has ended.
	KCMNotifyDocs(kKCMComparisonDocsClosedMessage,
	                survivorTargetDB, survivorOrigDB, survivorSrcDB,
	                comparisonDocClosed /*navReset*/);
}
