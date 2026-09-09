//========================================================================================
//
//  KCMBookCompare.cpp
//
//  Book comparison: running it. See KCMBookCompare.h for the contract.
//
//  The open/close machinery is ported from KBS (KBSBookScope::ReopenChapterDoc and its close),
//  which arrived at its present shape through several measured faults -- each one is named at
//  the line it guards, because none of them is guessable from the API alone.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// GetSysFile - a document's identity is its FILE, never its UID
#include "IDocFileHandler.h"	// Close / CanClose - the windowless, UI-suppressed close
#include "IDocument.h"
#include "IDocumentCommands.h"	// Open by file, windowless
#include "IDocumentList.h"		// FindDoc - is this chapter already open?
#include "IDocumentUtils.h"		// QueryDocFileHandler
#include "IGlobalRecompose.h"	// ForceRecompositionToComplete - see RecomposeChapter
#include "IOpenFileCmdData.h"	// kOpenDefault / kUseLockFile
#include "IShape.h"				// kPreviewMode - the draw mode the comparison rasterises in
#include "ISession.h"

// General includes:
#include "AGMImageAccessor.h"	// the pixels a snapshot produced
#include "ErrorUtils.h"			// GlobalErrorStatePreserver - an open or a close that is allowed to
								// fail must not poison the caller's next command
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "ProgressBar.h"		// RangeProgressBar - the progress and the cancel (see KCMCompareBooks)
#include "SDKFileHelper.h"		// GetPath - documents are compared by their path
#include "SnapshotUtilsEx.h"	// the rasteriser, on the SAME terms the document comparison uses
#include "K2SmartPtr.h"			// K2::scoped_ptr -- the snapshots and accessors below, freed on every exit

// Project includes:
#include "KCMBookChapterModes.h"	// the story and resources judgements - the other two of the three
#include "KCMBookCompare.h"
#include "KCMBookPair.h"			// KCMBuildChapterPairing
#include "KCMConstants.h"			// kKCMResolution / kKCMHiResMul / kKCMCmykThr / kKCMChapterProgressSpan
#include "KCMCore.h"				// KCMCollectPageUIDs - the same page walk the document comparison uses
#include "KCMDrawEventHandler.h"	// KCMRasterizingGuard - do not draw marks into our own raster
#include "KCMPageNumberMarker.h"	// KCMGetPageNumberMarkerRects - the folio areas to skip

namespace
{

/** The last comparison, as the text app.kcmBookResult hands back. Module-level so it survives
    whatever happens to any panel, and so a script can read it with nothing on screen. */
PMString gBookResultText;

/** Does this open document live in that file?

    **A document's identity is its FILE.** Asked through IDataBase::GetSysFile. A document
    that has never been saved has no file and can never be the chapter being looked for.

    This check is not optional caution. KBS used to trust
    IBookUtils::IsSourceDocumentAlreadyOpen, which hands back an INDEX into the document list,
    and that put a DIFFERENT chapter's document in a chapter's place: measured, 4 book replaces
    in 10 came back with a whole chapter's rows marked missing -- silently, because the call had
    reported success. One string compare is the whole distance between that and a right
    answer. */
bool16 DocumentLivesInFile(IDocument* doc, const PMString& wantedPath)
{
	if (doc == nil)
		return kFalse;

	IDataBase* db = ::GetDataBase(doc);
	if (db == nil)
		return kFalse;

	const IDFile* docFile = db->GetSysFile();
	if (docFile == nil)
		return kFalse;

	SDKFileHelper helper(*docFile);
	return (helper.GetPath() == wantedPath) ? kTrue : kFalse;
}

/** Open a chapter windowless, or rebind to it when it is already open.

    outWeOpened says whether THIS call opened it. **A chapter the user already had open is used
    as it stands and never closed afterwards** -- closing something somebody else opened would
    surprise them, and it is not ours to close.

    @warning **"as it stands" includes edits that are not saved yet.** A chapter open on screen
    is compared in the state it is on screen, while its counterpart is read off the disk -- so a
    book comparison can report a chapter as changed for an edit that only exists in memory.
    That is the right answer for "does this chapter differ from the other book right now", and
    the wrong one for "do the two books on disk differ"; nothing here can tell which the user
    meant. Saving the chapters first makes the two questions the same. */
bool16 OpenChapter(const IDFile& file, UIDRef& outDocRef, bool16& outWeOpened, PMString& outWhy)
{
	outDocRef   = UIDRef::gNull;
	outWeOpened = kFalse;

	SDKFileHelper fileHelper(file);
	const PMString wantedPath = fileHelper.GetPath();
	if (wantedPath.empty())
	{
		// **THE VERDICT IS NOT REPEATED IN THE REASON.** (The user's call: "it already says
		// Failed, so the row does not have to say the open failed too".) Every fWhy in this
		// plug-in is shown NEXT TO the word "Failed" -- in the dialog's own state column, and as
		// the second field of app.kcmBookResult -- so a reason that begins "could not be..." spends
		// the row's width saying what is already on it. What is left here is the part only this
		// line knows. See KCMBookResult.h's fWhy for the rule and for what it costs when it is
		// broken.
		outWhy = PMString("no file");
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}

	// Open already - by the user, or by an earlier step of this same run?
	// Asked through the session's own lookup by file, and then CHECKED against the file asked for
	// (see DocumentLivesInFile). Never report failure from here: "not already open" is the
	// ordinary case, and the open below handles it.
	{
		ISession* session = GetExecutionContextSession();
		InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
		if (docList != nil)
		{
			IDocument* openDoc = docList->FindDoc(file);
			if (DocumentLivesInFile(openDoc, wantedPath))
			{
				outDocRef = ::GetUIDRef(openDoc);
				return kTrue;		// outWeOpened stays kFalse - not ours to close
			}
		}
	}

	// The windowless, UI-suppressed open, by FILE (the book itself may be closed).
	//
	// @warning **not IBookUtils::OpenOneDocument.** That one takes no UI-suppression argument, so
	// a chapter that raises an alert on opening (missing font, missing link, saved by another
	// version) fails -- and its caller sees only "could not open", with the chapter vanishing from
	// the result as if it had had nothing to report. KBS lost a whole chapter that way.
	//
	// **This open is allowed to fail, so what it raises must not leave this scope.** A chapter
	// that will not open gets its own row with a reason; an error left standing would instead fail
	// whatever command runs next: preserve (GlobalErrorStatePreserver), then clear.
	// The preserver around a document open that may fail is the SDK's own shape
	// (GoToAnchorPanelObserver::InitializeDocumentChoice). @warning **the clear is not**: that
	// example does not reset the code, it reads PMGetGlobalErrorCode() afterwards and so quietly
	// assumes the state was clean going in. This one does not assume it, because a book comparison
	// runs after a whole flyout command of ours.
	UIDRef    docRef;
	ErrorCode err = kFailure;
	{
		GlobalErrorStatePreserver openErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		err = Utils<IDocumentCommands>()->Open(&docRef, file, kSuppressUI,
			IOpenFileCmdData::kOpenDefault, IOpenFileCmdData::kUseLockFile, kFalse /*showInWindow*/);
	}

	if (err != kSuccess || docRef == UIDRef::gNull)
	{
		// **THE FALLBACK, AND THE ONLY REASON THAT STILL NAMES THE FAILURE.** The caller replaces
		// this with the BOOK's own word for the chapter ("missing", and the rest of
		// KCMChapterStatusText's vocabulary) whenever the book has one -- that word is the thing
		// worth reading, and it says the open failed by implication. This sentence is what is left
		// when the book answers nothing at all, and then it is all the row would otherwise have
		// (the user's call: keep a word for the empty case rather than an empty reason).
		outWhy = PMString("could not open");
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}

	outWeOpened = kTrue;
	outDocRef   = docRef;
	return kTrue;
}

/** Close a chapter THIS run opened. Anything that was already open is left exactly as it was.

    kFalse means the close was NOT ATTEMPTED -- no file handler, or CanClose said no -- and the
    caller counts those into the report's "left open", so the user is told.

    @warning **kTrue means "asked, without being refused up front", not "verified closed"**:
    IDocFileHandler::Close returns void and whatever it raises is swallowed by the preserver
    below, so a close that fails inside the command reads here as a success. The count can
    therefore only under-report. Making it exact would mean asking the document list for the
    file again after the close; it has not been needed, because the refusals seen in practice
    are the ones CanClose already answers. */
bool16 CloseChapter(const UIDRef& docRef, bool16 weOpened)
{
	if (!weOpened || docRef == UIDRef::gNull)
		return kTrue;		// nothing of ours to close

	// The close is allowed to fail too, and for the same reason as the open: this runs BETWEEN
	// chapters, so anything left standing here would fail the next chapter's open.
	GlobalErrorStatePreserver closeErrorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
	if (docFileHandler == nil)
		return kFalse;
	if (!docFileHandler->CanClose(docRef))
		return kFalse;

	// **kProcess closes NOW; kSchedule would not close until this whole run unwinds.** A run IS
	// the current tick, so with kSchedule every chapter it had "handed back" would still be open
	// -- and still locking its .indd -- until the entire comparison was over (measured by KBS on a
	// four-chapter run). With one pair in flight that is the difference between two open documents
	// and all of them.
	//
	// **kProcess is only legal because this document has no window.** Closing a document that HAS
	// one with kProcess is a stated error: the one implementation of the interface in the SDK's
	// sources asserts on exactly that -- "Close() illegal with open document windows and cmdMode ==
	// kProcess" (InCopyDocFileHandler::Close, in open/components/incopyfileactions).
	// @warning IDocFileHandler.h itself says nothing about it and defaults cmdMode to kSchedule,
	// so the rule is read off an implementation, not off the contract -- that assert text appears
	// once in the whole SDK. Everything reaching this line was opened by OpenChapter with
	// showInWindow=kFalse, so it is windowless by construction. Keep that true if this is ever
	// reused.
	//
	// @warning do NOT "improve" this to IBookUtils::CloseDocumentsInBook: it takes no UI flag and
	// no command mode, closes immediately, and crashed KESCL when called from a notification.
	docFileHandler->Close(docRef, kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kProcess);
	return kTrue;
}

/** Close both sides of a chapter, counting the ones that refused into ioLeftOpen.

    Source first, then target -- the order both call sites used. The reason to have one function
    is not the four lines: it is that **counting the refusal is the easy half to forget**, and
    the report's "left open" is the only thing that tells the user a document is still holding
    its .indd open with no window to close it by. */
void CloseChapterPair(const UIDRef& targetRef, bool16 targetMine,
					   const UIDRef& sourceRef, bool16 sourceMine, int32& ioLeftOpen)
{
	if (!CloseChapter(sourceRef, sourceMine))
		++ioLeftOpen;
	if (!CloseChapter(targetRef, targetMine))
		++ioLeftOpen;
}

/** Finish composing a chapter before anything reads its pixels.

    **A document that has just been opened is NOT composed yet.** Rasterising it in that
    state paints composition in progress, and two chapters with identical content then come out
    different. The document comparison never hit this because it only ever rasterises documents the
    user has open on screen, which are composed by the time anyone asks.

    KESHR (the hash plug-in) does the same thing before its own draw, with the comment that without
    it "identical content gave different hashes" - so this is measured behaviour, not caution.

    Note that MakeEntry deliberately does NOT do this: it can be reached from inside a draw event,
    where recomposing would re-enter. This path is a menu command, so it is safe here. */
void RecomposeChapter(const UIDRef& docRef)
{
	InterfacePtr<IDocument> doc(docRef, UseDefaultIID());
	if (doc == nil)
		return;

	InterfacePtr<IGlobalRecompose> recompose(doc, IID_IGLOBALRECOMPOSE);
	if (recompose != nil)
		recompose->ForceRecompositionToComplete();
}

/* The folio-exclusion row test (KCMXInRowRects) is NOT duplicated here: the single definition
   lives in KCMDrawEventHandler.h as an inline function, so both comparisons share it and both
   still inline it in their per-pixel loop. */

/** Compare two rasterised pages.

    **outDiffers is set the moment ONE differing pixel is found, and the walk stops there.**
    The document comparison has to count every differing pixel because it draws a ring around
    them; this only has to answer yes or no, so the first pixel is the whole answer.

    kFailure means the two could NOT be compared (different page sizes, no pixels). That is
    reported as a failed chapter -- never as "no change". */
ErrorCode ComparePages(AGMImageAccessor* accT, AGMImageAccessor* accS,
                       const UIDRef& targetPage, const UIDRef& sourcePage,
                       const PMReal& hiRes, bool16& outDiffers)
{
	outDiffers = kFalse;

	const Int32Rect bt = accT->GetBounds();
	const Int32Rect bs = accS->GetBounds();
	const int32 wt  = bt.right - bt.left, ht = bt.bottom - bt.top;
	const int32 ws  = bs.right - bs.left, hs = bs.bottom - bs.top;
	const int32 rbT = (int32)accT->GetRowBytes();
	const int32 rbS = (int32)accS->GetRowBytes();
	const int32 bpp = (int32)accT->GetBitsPerPixel() / 8;
	const uint8* pt = accT->GetBaseAddr();
	const uint8* ps = accS->GetBaseAddr();

	if (pt == nil || ps == nil || wt != ws || ht != hs || rbT != rbS || rbT <= 0 || bpp < 4)
		return kFailure;

	// The folio (automatic page number) areas, ALWAYS skipped for a book comparison: inserting one
	// chapter shifts every folio after it, and without this every page from there on would read as
	// changed. The document comparison asks its own toggle instead; the sieve itself is shared
	// (KCMDrawEventHandler.h), so the two can never disagree about what a folio is.
	//
	// The rects are re-measured here (refresh=kTrue). Holding two references at once is safe only
	// because the cache is a std::map, whose inserts do not invalidate existing references -- do not
	// swap it for an unordered_map or a vector without changing this to copy by value.
	std::vector<Int32Rect> excludeRects;
	{
		const std::vector<PMRect>& tRects = KCMGetPageNumberMarkerRects(targetPage, kTrue);
		const std::vector<PMRect>& sRects = KCMGetPageNumberMarkerRects(sourcePage, kTrue);
		KCMCollectFolioExcludeRects(tRects, sRects, hiRes, excludeRects);
	}

	// Two-stage sieve, as in the document comparison: the union bbox first, then only the rects
	// that actually cover this row. Outside the bbox the whole test is one comparison.
	int32 exTop = 0, exBottom = 0, exLeft = 0, exRight = 0;
	KCMFolioExcludeBBox(excludeRects, exTop, exBottom, exLeft, exRight);

	const int   nch      = 4;					// CMYK
	const int32 colorOff = 0;
	const int   thr      = kKCMCmykThr;

	std::vector<const Int32Rect*> rowRects;
	rowRects.reserve(excludeRects.size());

	for (int32 y = 0; y < ht; ++y)
	{
		const uint8* rowT = pt + (size_t)y * rbT;
		const uint8* rowS = ps + (size_t)y * rbT;

		rowRects.clear();
		if (!excludeRects.empty() && y >= exTop && y < exBottom)
		{
			for (size_t mi = 0; mi < excludeRects.size(); ++mi)
			{
				if (y >= excludeRects[mi].top && y < excludeRects[mi].bottom)
					rowRects.push_back(&excludeRects[mi]);
			}
		}
		const bool16 rowHasExclude = rowRects.empty() ? kFalse : kTrue;

		for (int32 x = 0; x < wt; ++x)
		{
			if (rowHasExclude && x >= exLeft && x < exRight && KCMXInRowRects(x, rowRects))
				continue;						// inside a folio: not a difference

			const uint8* px = rowT + (size_t)x * bpp + colorOff;
			const uint8* sx = rowS + (size_t)x * bpp + colorOff;
			int cm = 0;
			for (int c = 0; c < nch; ++c)
			{
				const int d = (px[c] > sx[c]) ? px[c] - sx[c] : sx[c] - px[c];
				if (d > cm)
					cm = d;
			}
			if (cm > thr)
			{
				outDiffers = kTrue;
				return kSuccess;				// **one pixel is the whole answer**
			}
		}
	}

	return kSuccess;
}

/** Rasterise both pages and compare them. */
ErrorCode PageDiffers(const UIDRef& targetPage, const UIDRef& sourcePage, bool16& outDiffers)
{
	outDiffers = kFalse;

	// **THE RASTERISING TERMS ARE COPIED FROM THE DOCUMENT COMPARISON, UNCHANGED.**
	// 144dpi (kKCMResolution x kKCMHiResMul) / CMYK / opaque / AA OFF / greek 0.0 /
	// fullResolutionGraphics = kFalse. Every one of them has a reason:
	//   - the two sides MUST match, or every edge becomes a difference
	//   - CMYK, because small CMYK differences vanish when rounded into RGB
	//   - AA off, so sub-pixel shifts do not show up as grey fringes
	//   - greek 0.0, or small type is drawn as a grey band with no glyphs and changes in it hide
	//   - fullResolutionGraphics = kFalse, because kTrue makes the document dirty
	//   - bDrawNonPrintingObjects = kFalse (the default is kTrue), so that moving an item marked
	//     non-printing -- a working note, an instruction to the printer -- is NOT a change. The
	//     mark states that the PRINTED result changed. The argument is documented on
	//     SnapshotUtilsEx::Draw, which also warns that it does NOT affect non-printing LAYERS.
	//     Arguments 5-7 are the defaults spelled out, because the 8th cannot be reached without
	//     them: kXPHigh (lowering it would hide changes to shadows, feathers and blends), no abort
	//     callback (cancellation is checked at page boundaries, see KCMCore.cpp), and no viewport
	//     attribute map (pVPAttrMap, SnapshotUtilsEx.h:251), so nothing about baseline grids,
	//     layers or the flattener is overridden for the snapshot.
	const PMReal hiRes = kKCMResolution * kKCMHiResMul;

	// nothrow throughout, as in the document comparison: the ordinary new throws instead of
	// returning nil, and an exception crossing an event boundary crashes. With nothrow an OOM
	// costs this one page. **K2::scoped_ptr, not std::vector or any throwing container** -- see
	// KCMDrawEventHandler.cpp's MakeEntry for why nothrow is a requirement here and not a habit.
	// @warning **the declaration order below is the release order.** scoped_ptrs go down in the
	//   reverse of declaration, so snapshots-then-accessors gives accS, accT, snapS, snapT: each
	//   accessor before the snapshot it came from, which is what the four hand-written deletes at
	//   the end of this function used to spell out.
	K2::scoped_ptr<SnapshotUtilsEx>  snapT(new (std::nothrow) SnapshotUtilsEx(targetPage, 1.0, 1.0, hiRes, hiRes, 0.0,
	                                                                         SnapshotUtilsEx::kCsCMYK, kFalse));
	K2::scoped_ptr<SnapshotUtilsEx>  snapS(new (std::nothrow) SnapshotUtilsEx(sourcePage, 1.0, 1.0, hiRes, hiRes, 0.0,
	                                                                         SnapshotUtilsEx::kCsCMYK, kFalse));
	K2::scoped_ptr<AGMImageAccessor> accT;
	K2::scoped_ptr<AGMImageAccessor> accS;
	ErrorCode                        status = kFailure;

	if (snapT.get() != nil && snapS.get() != nil)
	{
		ErrorCode drewT = kFailure;
		ErrorCode drewS = kFailure;
		{
			KCMRasterizingGuard rg;	// a re-entrant draw event must not paint marks into our raster
			drewT = snapT->Draw(IShape::kPreviewMode, kFalse, 0.0, kFalse,
			                    SnapshotUtils::kXPHigh, nil, nil, kFalse);
			drewS = snapS->Draw(IShape::kPreviewMode, kFalse, 0.0, kFalse,
			                    SnapshotUtils::kXPHigh, nil, nil, kFalse);
		}

		accT.reset((drewT == kSuccess) ? snapT->CreateAGMImageAccessor() : nil);
		accS.reset((drewS == kSuccess) ? snapS->CreateAGMImageAccessor() : nil);

		if (accT.get() != nil && accS.get() != nil)
			status = ComparePages(accT.get(), accS.get(), targetPage, sourcePage, hiRes, outDiffers);
	}

	// **Released on EVERY path**, which is why they are scoped_ptrs: this function returns early the
	// moment a difference is found, and a "delete at the end" shape would leak on that path. The
	// release order is the declaration order reversed -- see the note where they are declared.
	return status;
}

/** The PIXEL judgement for one chapter: do the two chapters' pages differ?

    ⚠**IT NO LONGER DECIDES THE CHAPTER.** Until 2026-09-10 this returned the chapter's verdict,
    because the pixels were the only thing looked at. They are now one of three, and the chapter's
    verdict is derived from all three by the caller -- so what comes back is THIS MODE's answer.

    Three stages, each of which can end the walk early:
      (1) a different page count IS the answer, and costs no rasterising at all
      (2) within a page, the first differing pixel ends the page
      (3) the first differing page ends the walk - the rest is never opened
    So the only chapters read to the end are the ones whose pixels agree.

    progress/baseTicks move the bar WITHIN this chapter's PIXEL PHASE (baseTicks is where the
    chapter's slice of the bar starts, and the phase owns kKCMBookPixelPhaseTicks of it).
    outCancelled is set when the user pressed Cancel, and the answer is then unjudged - a walk whose
    remaining pages were never read cannot be called unchanged, however many pages had already
    compared equal. ⚠**The caller then files nothing for this mode**: unjudged is what the walk
    honestly is, but a cancel must not reach the Change column as a '?' (2026-09-10 - the reasoning
    is at the call site, and outWhy is left empty for the same reason). */
KCMBookModeVerdict ComparePixels(IDataBase* targetDB, IDataBase* sourceDB,
                                 CProgressBar& progress, int32 baseTicks,
                                 bool16& outCancelled, PMString& outWhy)
{
	outCancelled = kFalse;

	std::vector<UID> targetPages;
	std::vector<UID> sourcePages;
	KCMCollectPageUIDs(targetDB, targetPages);	// the shared helper; not modified for this
	KCMCollectPageUIDs(sourceDB, sourcePages);

	if (targetPages.empty() && sourcePages.empty())
	{
		// ★THE MODE IS NAMED IN THE REASON, since 2026-09-10. There are three of them now, and a
		//   reason that does not say which one produced it sends the reader to the wrong half of
		//   the comparison. It is still not the VERDICT repeated (see OpenChapter): "Failed" is on
		//   the row already, "Pixel" is what only this line knows.
		outWhy = PMString("Pixel: no pages");
		outWhy.SetTranslatable(kFalse);
		return kKCMBookVerdictUnjudged;
	}

	// (1)
	if (targetPages.size() != sourcePages.size())
		return kKCMBookVerdictChanged;

	const int32 pageCount = (int32)targetPages.size();

	for (size_t i = 0; i < targetPages.size(); ++i)
	{
		bool16 differs = kFalse;
		const ErrorCode err = PageDiffers(UIDRef(targetDB, targetPages[i]),
		                                  UIDRef(sourceDB, sourcePages[i]), differs);
		if (err != kSuccess)
		{
			// Could not be compared. Reported as a failure, NEVER as "no change".
			// **The page NUMBER is the whole reason**; "could not be compared" was the verdict said
			//   twice (see the note in OpenChapter). @warning this is the reason that most needed the
			//   room: it carries a NUMBER, and a number that loses its middle to an ellipsis does not
			//   look damaged -- it looks like a different number (memory
			//   ellipsis-in-status-line-breaks-numbers).
			outWhy = PMString("Pixel: page ");
			outWhy.AppendNumber(int32(i + 1));
			outWhy.SetTranslatable(kFalse);
			return kKCMBookVerdictUnjudged;
		}
		if (differs)
			return kKCMBookVerdictChanged;		// (3)

		// This chapter's slice of the bar, divided by its pages. **Multiply BEFORE dividing**, or
		// every chapter with more pages than the span would sit at 0 until it finished. The
		// arithmetic is done in size_t (i is one), so the product cannot overflow on the 64-bit build
		// this plug-in ships as.
		// ⚠**THE PHASE'S SHARE, NOT THE WHOLE CHAPTER'S** (2026-09-10). The pixels used to own the
		//   chapter's entire span because they were the whole comparison; now they own
		//   kKCMBookPixelPhaseTicks of it and the other two phases have the rest.
		progress.SetPosition(baseTicks + (int32)((i + 1) * (size_t)kKCMBookPixelPhaseTicks / (size_t)pageCount));

		// **The cancel is asked at the PAGE BOUNDARY.** WasCancelled runs events, so it must not be
		// called inside the rasterising or the pixel walk -- the same rule the document comparison
		// states at its own Draw call ("cancellation is checked at page boundaries"). kFalse = do not
		// raise the global error state; raising it would make whatever command runs next fail as
		// collateral ([[command-sequence-rollback-on-error]]).
		if (progress.WasCancelled(kFalse))
		{
			// ⚠**NO REASON IS WRITTEN HERE, DELIBERATELY** (2026-09-10). The caller does not file
			//   this mode at all when a cancel is what ended it -- a cancel is not an unjudged mode
			//   (the reasoning is at the call site) -- so a reason built here would be dropped
			//   unread, and a reader of this function would be told it produces something it does
			//   not. What says the run was stopped is outCancelled, and the summary line's
			//   "- cancelled".
			outCancelled = kTrue;
			return kKCMBookVerdictUnjudged;
		}
	}

	return kKCMBookVerdictUnchanged;
}

/** What the progress bar says while one phase of one chapter runs.

    ★THE CHAPTER'S NAME COMES FIRST and the phase after it: the chapter is what the reader is
    waiting for, and the phase is why it is taking as long as it is (the resources phase is two
    whole-document exports). BK-36's rule is unchanged - the name is the chapter being WAITED FOR,
    set before the work rather than after it. */
PMString ChapterPhaseText(const PMString& chapterName, const char* phase)
{
	PMString out(chapterName);
	out.Append(" - ");
	out.Append(phase);
	out.SetTranslatable(kFalse);
	return out;
}

/** File one mode's answer into the chapter.

    ⚠**THE REASON IS KEPT ONLY FOR THE FIRST MODE THAT COULD NOT BE JUDGED.** fWhy is one string,
    shown in one cell beside a file name, with about 38 characters to live in (KCMBookResult.h's
    fWhy, measured 2026-08-19). Two reasons in it would produce exactly the row that note is about:
    a file name that does not exist, and neither reason readable at either end. Nothing is lost by
    keeping one - the Change column names EVERY unjudged mode with a '?', so the row says which
    modes could not be judged, and fWhy says why the first of them could not. */
void RecordVerdict(KCMChapterResult& chapter, uint32 modeBit, KCMBookModeVerdict verdict,
                   const PMString& why)
{
	if (verdict == kKCMBookVerdictChanged)
	{
		chapter.fChangedModes |= modeBit;
	}
	else if (verdict == kKCMBookVerdictUnjudged)
	{
		chapter.fUnjudgedModes |= modeBit;
		if (chapter.fWhy.IsEmpty())
			chapter.fWhy = why;
	}
}

/** The chapter's verdict, derived from the three modes.

    ★DERIVED, NOT STORED. fState is not a fourth thing to keep in step with the two bit fields; it
    is a reading of them, taken once, here (KCMBookResult.h's fChangedModes says the same).

      any mode changed              -> Changed     (even when another could not be judged: a
                                                    difference that WAS found is not made less true
                                                    by one that was not)
      nothing changed, cancelled    -> NotCompared
      nothing changed, some unjudged-> Failed
      nothing changed, all judged   -> NoChange

    ⚠**CANCELLED IS ASKED AFTER Changed AND BEFORE Failed**, and both halves of that matter. After
    Changed, because a difference found before the Cancel is a real answer and the run reporting it
    is not claiming to have finished. Before Failed, because a mode the Cancel stopped did not
    FAIL - and calling it a failure would send the reader looking for a broken chapter. */
KCMChapterState KCMChapterStateFromModes(const KCMChapterResult& chapter, bool16 cancelled)
{
	if (chapter.fChangedModes != 0)
		return kKCMChapterChanged;
	if (cancelled)
		return kKCMChapterNotCompared;
	if (chapter.fUnjudgedModes != 0)
		return kKCMChapterFailed;
	return kKCMChapterNoChange;
}

}	// anonymous namespace

ErrorCode KCMCompareBooks(IBook* target, IBook* source,
                            std::vector<KCMChapterResult>& outChapters, PMString& outReport)
{
	KCMBuildChapterPairing(target, source, outChapters);

	int32 leftOpen = 0;

	// **THE PROGRESS BAR.**
	// RangeProgressBar rather than the TaskProgressBar the document comparison uses, because this
	// walk is not "N items, one step each": a chapter is a slice of the bar that the pages inside
	// it move through. The SDK has both and they are chosen by use (linksui counts files with
	// TaskProgressBar; textimportfilter counts bytes with RangeProgressBar + SetPosition).
	//
	// **showImmediate = kTrue, unconditionally.** The document comparison needs the
	// kKCMProgressBarDelayMs delay because a two-page incremental recompare is instant; a
	// book comparison never is -- every chapter is opened, composed and rasterised, which measured
	// at ~200 ms per chapter even on small test chapters (15 chapters ~ 3 s).
	// @warning kFalse does NOT mean "appear if it takes a while"; it means the bar never appears
	// at all (measured, when a 100-page comparison showed nothing).
	const int32 chapterCount = (int32)outChapters.size();
	PMString barTitle(chapterCount == 1 ? "Comparing 1 chapter..." : "Comparing books...");
	barTitle.SetTranslatable(kFalse);
	RangeProgressBar progress(barTitle, 0, chapterCount * kKCMChapterProgressSpan, kTrue);

	// @warning **opening a chapter can raise a progress bar of its own.** Suppressing it has to
	// happen HERE, before the loop -- KBS had this call after its chapters were already open and
	// it therefore never affected the bar it was meant to suppress. This comparison wants no child
	// bars at all (nothing inside a chapter reports progress), so the blanket kTrue is right.
	progress.DisableChildProgressBars(kTrue);

	bool16 cancelled = kFalse;

	for (size_t i = 0; i < outChapters.size(); ++i)
	{
		KCMChapterResult& chapter = outChapters[i];

		// **Asked BEFORE the chapter is opened.** Cancelling has to be able to stop the next open,
		// not just the next comparison -- opening and composing a chapter is most of the cost.
		if (progress.WasCancelled(kFalse))
		{
			cancelled = kTrue;
			break;
		}

		// Where this chapter's slice of the bar begins, and what the bar says it is doing. Both are
		// set before the open so the name on screen is the chapter being waited for, not the last
		// one finished.
		const int32 baseTicks = (int32)i * kKCMChapterProgressSpan;
		progress.SetPosition(baseTicks);
		progress.SetTaskText(chapter.fName);

		// Already answered by the pairing: no counterpart on the other side, or no file to open.
		if (chapter.fState != kKCMChapterUnknown)
			continue;

		UIDRef   targetRef;
		UIDRef   sourceRef;
		bool16   targetMine = kFalse;
		bool16   sourceMine = kFalse;
		PMString why;

		if (!OpenChapter(chapter.fTargetFile, targetRef, targetMine, why) ||
		    !OpenChapter(chapter.fSourceFile, sourceRef, sourceMine, why))
		{
			// Whichever side did open has to be put back before moving on. (When the first open
			// failed the second never ran, and closing a null UIDRef is a no-op.)
			CloseChapterPair(targetRef, targetMine, sourceRef, sourceMine, leftOpen);

			// **THE BOOK KNOWS WHY, SO ASK IT.** On its own "could not open" is the same sentence for
			// a chapter that was deleted, one another user has open, and one saved by a newer version.
			// GetBookContentStatus tells those apart (see KCMChapterStatusText), and it is asked ONLY
			// here, on the failing path, so a run where everything opens pays nothing for it. Same call
			// KBS makes for its skipped chapters (KBSBookScope::OpenChapterDoc).
			//
			// Which side failed is readable from the refs: the two opens above are short-circuited and
			// each clears its own out-ref first, so a null target means the target's open is the one
			// that never reached a document. Only chapters present in BOTH books get this far (the
			// others were answered by the pairing), so the index is valid in either book.
			//
			// **It REPLACES the reason rather than being appended to it.** The book's word is a BETTER
			// answer to the same question, not an extra one: "could not open (missing)" and "missing"
			// say the same thing, and the row has room for one of them. What appending cost when it was
			// measured -- a row that named a file which does not exist -- is written out at
			// KCMBookResult.h's fWhy, which is where the rule for every reason string lives.
			const PMString statusWord =
				KCMChapterStatusText((targetRef == UIDRef::gNull) ? target : source, (int32)i);
			if (!statusWord.IsEmpty())
				why = statusWord;

			chapter.fState = kKCMChapterFailed;
			chapter.fWhy   = why;
			continue;
		}

		{
			IDataBase* targetDB = targetRef.GetDataBase();
			IDataBase* sourceDB = sourceRef.GetDataBase();
			if (targetDB == nil || sourceDB == nil)
			{
				chapter.fState = kKCMChapterFailed;
				chapter.fWhy   = PMString("no database");		// the verdict is not repeated - see OpenChapter
				chapter.fWhy.SetTranslatable(kFalse);
			}
			else
			{
				// **"Do not dirty the document" is not a promise never to touch it** -- it is "if it
				// was clean going in, it is clean coming out". Composing and rasterising both touch
				// it, so both chapters are wrapped. The guards are scoped so they have already
				// restored by the time the chapter is closed below -- a document closed while still
				// marked modified would prompt, and with kSuppressUI a prompt means silently losing
				// what is in it.
				IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
				IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

				// Compose BEFORE anything reads pixels - see RecomposeChapter.
				RecomposeChapter(targetRef);
				RecomposeChapter(sourceRef);

				// ***** THREE JUDGEMENTS, IN THE ORDER THE USER ASKED FOR (2026-09-10). *****
				//
				// ★★**NO EARLY EXIT BETWEEN MODES**, and that is the cost of the feature rather
				//   than an oversight: the Change column names EVERY mode that found something, so
				//   a chapter whose pixels already differ still has to be asked the other two.
				//   Inside a mode every early exit is still there - the first differing pixel, the
				//   first differing story, the first differing definition.
				// ★**THE PAIR IS OPENED ONCE FOR ALL THREE.** Walking the book three times would
				//   open, compose and close every chapter three times, and opening is most of the
				//   cost (BK-35: ~0.2s a chapter even on small ones).
				PMString why;
				bool16   chapterCancelled = kFalse;

				progress.SetTaskText(ChapterPhaseText(chapter.fName, "Pixel"));
				const KCMBookModeVerdict pixelVerdict =
					ComparePixels(targetDB, sourceDB, progress, baseTicks, chapterCancelled, why);

				// ⚠★★★**A CANCEL IS NOT AN UNJUDGED MODE** (2026-09-10, the user's call). The
				//   Change column's '?' means "this was looked at and could not be judged" -- a page
				//   that would not rasterise, an export that came back short -- so a reader who sees
				//   one goes hunting for a broken chapter. A mode the reader THEMSELF stopped is
				//   none of that. It would also contradict the cell beside it: the chapter's verdict
				//   is NotCompared, whose whole meaning is that nothing was looked at, and the spec
				//   map has the Change column empty on exactly those rows (BK-46).
				//   ⇒ The cancel travels in chapterCancelled alone; this mode is simply not filed.
				//   ★Pixel is the only phase that can end this way. The other two are asked about
				//     the cancel AFTER they have answered, so what they file is a real answer.
				if (!chapterCancelled)
					RecordVerdict(chapter, kKCMBookModePixel, pixelVerdict, why);

				// ⚠**A CANCEL STOPS THE PHASES THAT HAVE NOT RUN.** Running the other two after the
				//   person asked to stop would make Cancel take about as long as finishing -- and
				//   the resources phase, the one that would still be to come, is the expensive one.
				//   ★**It does not reach back over the phases that HAVE run**: what they filed is a
				//     real answer, and after the last of them there is nothing left to stop, which
				//     is why the ask at the end of the Resources block sets the RUN's flag and not
				//     this chapter's (2026-09-10).
				if (!chapterCancelled)
				{
					progress.SetPosition(baseTicks + kKCMBookPixelPhaseTicks);
					progress.SetTaskText(ChapterPhaseText(chapter.fName, "Story"));
					RecordVerdict(chapter, kKCMBookModeStory,
					              KCMJudgeChapterStory(targetDB, sourceDB, why), why);

					// Asked BETWEEN phases only. WasCancelled runs events, so it must not be called
					// inside the rasterising, inside the pixel walk, or inside ExportINX - none of
					// which has a safe point in the middle.
					if (progress.WasCancelled(kFalse))
						chapterCancelled = kTrue;
				}

				if (!chapterCancelled)
				{
					progress.SetPosition(baseTicks + kKCMBookPixelPhaseTicks + kKCMBookStoryPhaseTicks);
					progress.SetTaskText(ChapterPhaseText(chapter.fName, "Resources"));
					RecordVerdict(chapter, kKCMBookModeResources,
					              KCMJudgeChapterResources(targetDB, sourceDB, why), why);

					// ⚠★★★**THE CHAPTER IS FINISHED, SO THIS CANCEL DOES NOT TOUCH ITS VERDICT**
					//   (2026-09-10). Resources is the LAST of the three phases, so by the time this
					//   is asked every mode has answered. Calling the chapter NotCompared here would
					//   deny an answer that was fully arrived at -- BK-38's rule read backwards,
					//   because "could not be processed" and "processed, and nothing had changed"
					//   must not share a word in EITHER direction.
					//   ★What this cancel stops is the RUN: no next chapter is opened. That is the
					//     same shape as the ask after the loop, which says so in as many words
					//     ("nothing was lost, but the user did press Cancel").
					//   ⚠**It was `chapterCancelled` until 2026-09-10**, which took the verdict away
					//     from a chapter all three modes had just finished judging. Pressing Cancel
					//     during the Resources phase -- the phase that owns most of a chapter's time
					//     -- was enough to produce it.
					if (progress.WasCancelled(kFalse))
						cancelled = kTrue;
				}

				// ★THE CHAPTER'S VERDICT IS DERIVED from the three modes, never stored twice - see
				//   KCMBookResult.h's fChangedModes.
				chapter.fState = KCMChapterStateFromModes(chapter, chapterCancelled);
				if (chapterCancelled)
					cancelled = kTrue;
			}
		}

		// **The chapters this run opened are closed even when it was cancelled.** Cancelling stops
		// the comparison, not the tidying up: a chapter left open would go on locking its .indd with
		// no window for the user to close it by.
		CloseChapterPair(targetRef, targetMine, sourceRef, sourceMine, leftOpen);

		if (cancelled)
			break;
	}

	// **ASKED ONCE MORE, AFTER THE LOOP.**
	// A cancel pressed while the LAST chapter was being compared is never seen by a test that only
	// runs at the top of the next iteration -- there is no next iteration. KBS spent a day on the
	// symptom this produces ("cancel works on the first chapter but not the last", and never at
	// all in a one-chapter book). The work is already done by this point; what this decides is
	// what the run is REPORTED as, and a run the user stopped must not claim to have finished.
	if (!cancelled && progress.WasCancelled(kFalse))
		cancelled = kTrue;

	// **Chapters the cancel never reached get a word of their own.**
	// kKCMChapterUnknown is the internal "not judged yet" and must not reach the screen; leaving
	// it would print "Unknown", and calling it NoChange would assert something this run never
	// checked.
	if (cancelled)
	{
		for (size_t i = 0; i < outChapters.size(); ++i)
		{
			if (outChapters[i].fState == kKCMChapterUnknown)
				outChapters[i].fState = kKCMChapterNotCompared;
		}
	}

	int32 changed = 0, unchanged = 0, added = 0, deleted = 0, failed = 0, notCompared = 0;
	for (size_t i = 0; i < outChapters.size(); ++i)
	{
		switch (outChapters[i].fState)
		{
			case kKCMChapterChanged:		++changed;		break;
			case kKCMChapterNoChange:		++unchanged;	break;
			case kKCMChapterAdded:		++added;		break;
			case kKCMChapterDeleted:		++deleted;		break;
			case kKCMChapterFailed:		++failed;		break;
			case kKCMChapterNotCompared:	++notCompared;	break;
			default:										break;
		}
	}

	// **The chapter COUNT is always stated, and it comes FIRST.** An empty list has to be
	// readable as "every chapter was compared and none changed" rather than "nothing could be
	// opened" -- conflating those two is the fault that took a day to find in KBS.
	//
	// **ONLY NON-ZERO COUNTS ARE LISTED, AND THE LINE HAS NO PREAMBLE.**
	// @warning this is not tidiness, it is a MEASURED defect. The line lives in one status widget
	// that ellipsizes in the MIDDLE, and a cancelled run adds two more counts to it. The earlier
	// wording overflowed and came out as
	//     "book co...5 chapters, 0 changed, 0 unchanged, 1 added, 14 not compared - cancelled"
	// -- which reads as FIVE chapters. Ellipsis in the middle of a number does not look like
	// damage, it looks like a smaller number, so an overflowing summary here does not merely lose
	// detail: it states a wrong figure. Middle-ellipsis was itself the fix for the previous
	// overflow, where the failed count fell off the end, so the answer cannot be to move
	// the truncation around again -- the line has to be short enough not to truncate.
	//   - dropping "book compare: " saves 14 characters and loses nothing: the dialog is titled
	//     "Compare Books" and this is the only status line in it. It also restores the wording the
	//     design specified ("12 chapters: 5 changed, 6 unchanged, 1 failed").
	//   - dropping zeroes saves the rest. The VERDICT counts sum to the chapter count, which is
	//     printed, so a reader can still tell "15 chapters: 15 unchanged" from a partial run.
	//     @warning "left open" is NOT one of those, and the sum does not hold once it appears: it
	//       counts DOCUMENTS this run could not close -- up to two per chapter -- so a run that
	//       failed to close both sides of one chapter reads "3 chapters: 3 unchanged, 2 left
	//       open", whose parts add up to 5. It is listed here anyway because it is rare and has to
	//       be said; the reading to keep is that the five VERDICTS before it account for every
	//       chapter.
	const int32 total = (int32)outChapters.size();
	outReport = PMString();
	outReport.AppendNumber(total);
	outReport.Append(total == 1 ? " chapter" : " chapters");

	struct { int32 count; const char* word; } parts[] =
	{
		{ changed,     " changed"     },
		{ unchanged,   " unchanged"   },
		{ added,       " added"       },
		{ deleted,     " deleted"     },
		// Before "failed": with chapters left uncompared, "1 changed, 5 unchanged" describes a
		// part of the book rather than the book.
		{ notCompared, " not compared" },
		{ failed,      " failed"      },
		{ leftOpen,    " left open"   },
	};
	bool16 firstPart = kTrue;
	for (size_t p = 0; p < sizeof(parts) / sizeof(parts[0]); ++p)
	{
		if (parts[p].count <= 0)
			continue;
		outReport.Append(firstPart ? ": " : ", ");	// the colon arrives with the first count, so a
		outReport.AppendNumber(parts[p].count);		// book with no chapters ends at "0 chapters"
		outReport.Append(parts[p].word);
		firstPart = kFalse;
	}

	// Last, and set off by a dash rather than a comma -- it qualifies the whole line instead of
	// adding another count to it. Same shape the Refresh path uses ("refreshed 5 (changed 2) -
	// cancelled"). @warning it can appear with no "not compared" at all, when the cancel arrived
	// as the last chapter finished: nothing was lost, but the user did press Cancel, and the
	// report says what happened rather than what would have been tidier to say.
	if (cancelled)
		outReport.Append(" - cancelled");
	outReport.SetTranslatable(kFalse);

	// The per-chapter read-out, built HERE from the same list the caller receives - so the summary
	// line and the detail can never disagree about what happened.
	//
	// ★★**FOUR COLUMNS, ALWAYS, since 2026-09-10**: name, verdict, modes, reason. It used to be
	//   "name, verdict [, reason]" -- three fields on a Failed row and two on every other one -- so
	//   the third field meant a different thing depending on the row. With the modes to report as
	//   well that becomes unreadable, and reading by position is the only thing a tab-separated
	//   line offers. Empty fields where there is nothing to say.
	// ★**THE REASON IS NO LONGER LIMITED TO Failed ROWS.** A cancelled chapter has one, and so does
	//   a Changed chapter one of whose modes could not be judged - both of which used to have a
	//   reason recorded and no way to read it.
	// ⚠**Nothing was broken by the move, and that was counted rather than assumed** (2026-09-10):
	//   all five scripts that touch app.kcmBookResult print the string whole and cut no fields
	//   (work/kescm-booktest/r3-verify.ps1, r3-failed-row.ps1, r3-measure.ps1, dlg-tools.ps1, and
	//   work/kescm-selftest/task9/s1-book.jsx). The VERDICT is still field 2, which is the one a
	//   reader of those outputs actually looks at.
	gBookResultText.Clear();
	gBookResultText.SetTranslatable(kFalse);
	for (size_t i = 0; i < outChapters.size(); ++i)
	{
		const KCMChapterResult& chapter = outChapters[i];
		if (i > 0)
			gBookResultText.Append("\n");
		gBookResultText.Append(chapter.fName);
		gBookResultText.Append("\t");
		gBookResultText.Append(KCMChapterStateText(chapter.fState));
		gBookResultText.Append("\t");

		// SetUTF8String because the modes arrive as std::string (KCMBookModeNames.h has no SDK type
		// in it, which is what lets it be checked outside InDesign). Every character it produces is
		// ASCII, so nothing is at risk here - but naming the encoding at both ends is the habit
		// that stopped a Japanese font name being mangled in the Resources list.
		PMString modes;
		modes.SetUTF8String(KCMBookModesString(chapter.fChangedModes, chapter.fUnjudgedModes));
		modes.SetTranslatable(kFalse);
		gBookResultText.Append(modes);

		gBookResultText.Append("\t");
		if (!chapter.fWhy.IsEmpty())
			gBookResultText.Append(chapter.fWhy);
	}

	return kSuccess;
}

void KCMGetBookResultText(PMString& out)
{
	out = gBookResultText;
}

void KCMClearBookResultText()
{
	gBookResultText.Clear();
}

// End, KCMBookCompare.cpp.
