//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: running it. See KESCMBookCompare.h for the contract.
//
//  The open/close machinery is ported from KBS (KBSBookScope::ReopenChapterDoc and its close),
//  which arrived at its present shape through several measured faults - each one is named at the
//  line it guards, because none of them is guessable from the API alone.
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
#include "ProgressBar.h"		// RangeProgressBar - the progress and the cancel (see KESCMCompareBooks)
#include "SDKFileHelper.h"		// GetPath - documents are compared by their path
#include "SnapshotUtilsEx.h"	// the rasteriser, on the SAME terms the document comparison uses

// Project includes:
#include "KESCMBookCompare.h"
#include "KESCMBookPair.h"			// KESCMBuildChapterPairing
#include "KESCMConstants.h"			// kKESCMResolution / kKESCMHiResMul / kKESCMCmykThr
#include "KESCMCore.h"				// KESCMCollectPageUIDs - the same page walk the document comparison uses
#include "KESCMDrawEventHandler.h"	// KESCMRasterizingGuard - do not draw marks into our own raster
#include "KESCMPageNumberMarker.h"	// KESCMGetPageNumberMarkerRects - the folio areas to skip

namespace
{

/** The last comparison, as the text app.kcmBookResult hands back. Module-level so it survives
    whatever happens to any panel, and so a script can read it with nothing on screen. */
PMString gBookResultText;

/** Does this open document live in that file?

    ***** A document's identity is its FILE. ***** Asked through IDataBase::GetSysFile. A document
    that has never been saved has no file and can never be the chapter being looked for.

    This check is not optional caution. KBS used to trust IBookUtils::IsSourceDocumentAlreadyOpen,
    which hands back an INDEX into the document list, and that put a DIFFERENT chapter's document
    in a chapter's place: measured 2026-08-04, 4 book replaces in 10 came back with a whole
    chapter's rows marked missing - silently, because the call had reported success. One string
    compare is the whole distance between that and a right answer. */
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

    outWeOpened says whether THIS call opened it. ***** A chapter the user already had open is used
    as it stands and never closed afterwards ***** - closing something somebody else opened would
    surprise them, and it is not ours to close. */
bool16 OpenChapter(const IDFile& file, UIDRef& outDocRef, bool16& outWeOpened, PMString& outWhy)
{
	outDocRef   = UIDRef::gNull;
	outWeOpened = kFalse;

	SDKFileHelper fileHelper(file);
	const PMString wantedPath = fileHelper.GetPath();
	if (wantedPath.empty())
	{
		outWhy = PMString("the chapter names no file");
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
	// ⚠ NOT IBookUtils::OpenOneDocument. That one takes no UI-suppression argument, so a chapter
	// that raises an alert on opening (missing font, missing link, saved by another version)
	// fails - and its caller sees only "could not open", with the chapter vanishing from the
	// result as if it had had nothing to report. KBS lost a whole chapter that way.
	//
	// ***** THIS OPEN IS ALLOWED TO FAIL, so what it raises must not leave this scope. ***** A
	// chapter that will not open gets its own row with a reason; an error left standing would
	// instead fail whatever command runs next. Preserve, then clear (ErrorUtils.h:115-117) - the
	// shape the SDK itself uses for this operation
	// (buttonui/actiondatapanels/gotoanchor/GoToAnchorPanelObserver.cpp:395-401).
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
		outWhy = PMString("could not be opened");
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}

	outWeOpened = kTrue;
	outDocRef   = docRef;
	return kTrue;
}

/** Close a chapter THIS run opened. Anything that was already open is left exactly as it was.
    kFalse means it is still standing (and the caller counts it, so the user is told). */
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

	// ***** kProcess closes NOW; kSchedule would not close until this whole run unwinds. *****
	// A run IS the current tick, so with kSchedule every chapter it had "handed back" would still
	// be open - and still locking its .indd - until the entire comparison was over (measured by
	// KBS on a four-chapter run, 2026-08-04). With one pair in flight that is the difference
	// between two open documents and all of them.
	//
	// ***** kProcess is only legal because this document has no window. ***** Closing a document
	// that HAS one with kProcess is a stated error - IDocFileHandler::Close's own implementation
	// asserts "Close() illegal with open document windows and cmdMode == kProcess". Everything
	// reaching this line was opened by OpenChapter with showInWindow=kFalse, so it is windowless
	// by construction. Keep that true if this is ever reused.
	//
	// ⚠ Do NOT "improve" this to IBookUtils::CloseDocumentsInBook: it takes no UI flag and no
	// command mode, closes immediately, and crashed KESCL in 2026-07-17 when called from a
	// notification.
	docFileHandler->Close(docRef, kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kProcess);
	return kTrue;
}

/** Finish composing a chapter before anything reads its pixels.

    ***** A document that has just been opened is NOT composed yet. ***** Rasterising it in that
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

/** Is x inside any of the exclusion rects covering this row? */
bool16 XInRowRects(int32 x, const std::vector<const Int32Rect*>& rects)
{
	for (size_t i = 0; i < rects.size(); ++i)
	{
		if (x >= rects[i]->left && x < rects[i]->right)
			return kTrue;
	}
	return kFalse;
}

/** Compare two rasterised pages.

    ***** outDiffers is set the moment ONE differing pixel is found, and the walk stops there.
    ***** The document comparison has to count every differing pixel because it draws a ring around
    them; this only has to answer yes or no, so the first pixel is the whole answer.

    kFailure means the two could NOT be compared (different page sizes, no pixels). That is
    reported as a failed chapter - never as "no change". */
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
	// changed. Same rects the document comparison uses, so the two agree about what a folio is.
	//
	// The rects are re-measured here (refresh=kTrue). Holding two references at once is safe only
	// because the cache is a std::map, whose inserts do not invalidate existing references - do not
	// swap it for an unordered_map or a vector without changing this to copy by value.
	std::vector<Int32Rect> excludeRects;
	{
		const std::vector<PMRect>& tRects = KESCMGetPageNumberMarkerRects(targetPage, kTrue);
		const std::vector<PMRect>& sRects = KESCMGetPageNumberMarkerRects(sourcePage, kTrue);
		const PMReal pxScale = hiRes / PMReal(72.0);		// pt -> comparison pixels
		for (int pass = 0; pass < 2; ++pass)				// 0 = target, 1 = source (same coordinates)
		{
			const std::vector<PMRect>& mrs = (pass == 0) ? tRects : sRects;
			for (size_t mi = 0; mi < mrs.size(); ++mi)
			{
				const PMRect& mr = mrs[mi];
				Int32Rect epr;
				epr.left   = ::ToInt32(::Round(mr.Left()   * pxScale));
				epr.top    = ::ToInt32(::Round(mr.Top()    * pxScale));
				epr.right  = ::ToInt32(::Round(mr.Right()  * pxScale));
				epr.bottom = ::ToInt32(::Round(mr.Bottom() * pxScale));
				excludeRects.push_back(epr);
			}
		}
	}

	// Two-stage sieve, as in the document comparison: the union bbox first, then only the rects
	// that actually cover this row. Outside the bbox the whole test is one comparison.
	int32 exTop = 0, exBottom = 0, exLeft = 0, exRight = 0;
	if (!excludeRects.empty())
	{
		exTop  = excludeRects[0].top;   exBottom = excludeRects[0].bottom;
		exLeft = excludeRects[0].left;  exRight  = excludeRects[0].right;
		for (size_t mi = 1; mi < excludeRects.size(); ++mi)
		{
			const Int32Rect& r = excludeRects[mi];
			if (r.top    < exTop)    exTop    = r.top;
			if (r.bottom > exBottom) exBottom = r.bottom;
			if (r.left   < exLeft)   exLeft   = r.left;
			if (r.right  > exRight)  exRight  = r.right;
		}
	}

	const int   nch      = 4;					// CMYK
	const int32 colorOff = 0;
	const int   thr      = kKESCMCmykThr;

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
			if (rowHasExclude && x >= exLeft && x < exRight && XInRowRects(x, rowRects))
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
				return kSuccess;				// ***** one pixel is the whole answer *****
			}
		}
	}

	return kSuccess;
}

/** Rasterise both pages and compare them. */
ErrorCode PageDiffers(const UIDRef& targetPage, const UIDRef& sourcePage, bool16& outDiffers)
{
	outDiffers = kFalse;

	// ***** THE RASTERISING TERMS ARE COPIED FROM THE DOCUMENT COMPARISON, UNCHANGED. *****
	// 144dpi (kKESCMResolution x kKESCMHiResMul) / CMYK / opaque / AA OFF / greek 0.0 /
	// fullResolutionGraphics = kFalse. Every one of them has a reason:
	//   - the two sides MUST match, or every edge becomes a difference
	//   - CMYK, because small CMYK differences vanish when rounded into RGB
	//   - AA off, so sub-pixel shifts do not show up as grey fringes
	//   - greek 0.0, or small type is drawn as a grey band with no glyphs and changes in it hide
	//   - fullResolutionGraphics = kFalse, because kTrue makes the document dirty
	//   - bDrawNonPrintingObjects = kFalse (2026-08-12; the default is kTrue), so that moving an item
	//     marked non-printing - a working note, an instruction to the printer - is NOT a change. The
	//     mark states that the PRINTED result changed. Found by reading guide vol1-09; the argument is
	//     documented at SnapshotUtilsEx.h:241-242, which also warns it does NOT affect non-printing
	//     LAYERS. Arguments 5-7 are the defaults spelled out, because the 8th cannot be reached without
	//     them: kXPHigh (lowering it would hide changes to shadows, feathers and blends), no abort
	//     callback (cancellation is checked at page boundaries, see KESCMCore.cpp), no viewport map
	const PMReal hiRes = kKESCMResolution * kKESCMHiResMul;

	SnapshotUtilsEx*  snapT  = nil;
	SnapshotUtilsEx*  snapS  = nil;
	AGMImageAccessor* accT   = nil;
	AGMImageAccessor* accS   = nil;
	ErrorCode         status = kFailure;

	// nothrow throughout, as in the document comparison: the ordinary new throws instead of
	// returning nil, and an exception crossing an event boundary crashes. With nothrow an OOM
	// costs this one page.
	snapT = new (std::nothrow) SnapshotUtilsEx(targetPage, 1.0, 1.0, hiRes, hiRes, 0.0,
	                                           SnapshotUtilsEx::kCsCMYK, kFalse);
	snapS = new (std::nothrow) SnapshotUtilsEx(sourcePage, 1.0, 1.0, hiRes, hiRes, 0.0,
	                                           SnapshotUtilsEx::kCsCMYK, kFalse);

	if (snapT != nil && snapS != nil)
	{
		ErrorCode drewT = kFailure;
		ErrorCode drewS = kFailure;
		{
			KESCMRasterizingGuard rg;	// a re-entrant draw event must not paint marks into our raster
			drewT = snapT->Draw(IShape::kPreviewMode, kFalse, 0.0, kFalse,
			                    SnapshotUtils::kXPHigh, nil, nil, kFalse);
			drewS = snapS->Draw(IShape::kPreviewMode, kFalse, 0.0, kFalse,
			                    SnapshotUtils::kXPHigh, nil, nil, kFalse);
		}

		accT = (drewT == kSuccess) ? snapT->CreateAGMImageAccessor() : nil;
		accS = (drewS == kSuccess) ? snapS->CreateAGMImageAccessor() : nil;

		if (accT != nil && accS != nil)
			status = ComparePages(accT, accS, targetPage, sourcePage, hiRes, outDiffers);
	}

	// ***** Released on EVERY path. ***** This function returns early the moment a difference is
	// found, which the document comparison never does - so its "delete at the end" shape would
	// leak here. Everything is allocated in one place and freed in one place instead.
	if (accS  != nil) delete accS;
	if (accT  != nil) delete accT;
	if (snapS != nil) delete snapS;
	if (snapT != nil) delete snapT;

	return status;
}

/** Decide one chapter: changed, unchanged, or could not be judged.

    Three stages, each of which can end the chapter early:
      (1) a different page count IS the answer, and costs no rasterising at all
      (2) within a page, the first differing pixel ends the page
      (3) the first differing page ends the CHAPTER - the rest is never opened
    So the only chapters read to the end are the unchanged ones.

    progress/baseTicks move the bar WITHIN this chapter (baseTicks is where this chapter's slice of
    the bar starts). outCancelled is set when the user pressed Cancel, and the return is then
    kKESCMChapterNotCompared - a chapter whose remaining pages were never read cannot be called
    unchanged, however many of its pages had already compared equal. */
KESCMChapterState CompareChapter(IDataBase* targetDB, IDataBase* sourceDB,
                                 CProgressBar& progress, int32 baseTicks,
                                 bool16& outCancelled, PMString& outWhy)
{
	outCancelled = kFalse;

	std::vector<UID> targetPages;
	std::vector<UID> sourcePages;
	KESCMCollectPageUIDs(targetDB, targetPages);	// the shared helper; not modified for this
	KESCMCollectPageUIDs(sourceDB, sourcePages);

	if (targetPages.empty() && sourcePages.empty())
	{
		outWhy = PMString("no pages");
		outWhy.SetTranslatable(kFalse);
		return kKESCMChapterFailed;
	}

	// (1)
	if (targetPages.size() != sourcePages.size())
		return kKESCMChapterChanged;

	const int32 pageCount = (int32)targetPages.size();

	for (size_t i = 0; i < targetPages.size(); ++i)
	{
		bool16 differs = kFalse;
		const ErrorCode err = PageDiffers(UIDRef(targetDB, targetPages[i]),
		                                  UIDRef(sourceDB, sourcePages[i]), differs);
		if (err != kSuccess)
		{
			// Could not be compared. Reported as a failure, NEVER as "no change".
			outWhy = PMString("page ");
			outWhy.AppendNumber(int32(i + 1));
			outWhy.Append(" could not be compared");
			outWhy.SetTranslatable(kFalse);
			return kKESCMChapterFailed;
		}
		if (differs)
			return kKESCMChapterChanged;			// (3)

		// This chapter's slice of the bar, divided by its pages. ★Multiply BEFORE dividing, or every
		// chapter with more pages than the span would sit at 0 until it finished. The arithmetic is
		// done in size_t (i is one), so the product cannot overflow on the 64-bit build this
		// plug-in ships as.
		progress.SetPosition(baseTicks + (int32)((i + 1) * (size_t)kKESCMChapterProgressSpan / (size_t)pageCount));

		// ***** The cancel is asked at the PAGE BOUNDARY. ***** WasCancelled runs events, so it must
		// not be called inside the rasterising or the pixel walk - the same rule the document
		// comparison states at its own Draw call ("cancellation is checked at page boundaries").
		// kFalse = do not raise the global error state; raising it would make whatever command runs
		// next fail as collateral ([[command-sequence-rollback-on-error]]).
		if (progress.WasCancelled(kFalse))
		{
			outCancelled = kTrue;
			return kKESCMChapterNotCompared;
		}
	}

	return kKESCMChapterNoChange;
}

}	// anonymous namespace

ErrorCode KESCMCompareBooks(IBook* target, IBook* source,
                            std::vector<KESCMChapterResult>& outChapters, PMString& outReport)
{
	KESCMBuildChapterPairing(target, source, outChapters);

	int32 leftOpen = 0;

	// ***** THE PROGRESS BAR. *****
	// RangeProgressBar rather than the TaskProgressBar the document comparison uses, because this
	// walk is not "N items, one step each": a chapter is a slice of the bar that the pages inside it
	// move through. The SDK has both and they are chosen by use (linksui counts files with
	// TaskProgressBar; textimportfilter counts bytes with RangeProgressBar+SetPosition).
	//
	// ★showImmediate = kTrue, unconditionally. The document comparison needs the
	// kKESCMProgressBarMinPages threshold because a two-page incremental recompare is instant; a
	// book comparison never is - every chapter is opened, composed and rasterised, which measured at
	// ~200 ms per chapter even on small test chapters (15 chapters ≈ 3 s, 2026-08-12). ⚠ kFalse does
	// NOT mean "appear if it takes a while"; it means the bar never appears at all (measured
	// 2026-07-27, when a 100-page comparison showed nothing).
	const int32 chapterCount = (int32)outChapters.size();
	PMString barTitle(chapterCount == 1 ? "Comparing 1 chapter..." : "Comparing books...");
	barTitle.SetTranslatable(kFalse);
	RangeProgressBar progress(barTitle, 0, chapterCount * kKESCMChapterProgressSpan, kTrue);

	// ⚠ Opening a chapter can raise a progress bar of its own. Suppressing it has to happen HERE,
	// before the loop - KBS had this call after its chapters were already open and it therefore
	// never affected the bar it was meant to suppress. This comparison wants no child bars at all
	// (nothing inside a chapter reports progress), so the blanket kTrue is right.
	progress.DisableChildProgressBars(kTrue);

	bool16 cancelled = kFalse;

	for (size_t i = 0; i < outChapters.size(); ++i)
	{
		KESCMChapterResult& chapter = outChapters[i];

		// ***** Asked BEFORE the chapter is opened. ***** Cancelling has to be able to stop the next
		// open, not just the next comparison - opening and composing a chapter is most of the cost.
		if (progress.WasCancelled(kFalse))
		{
			cancelled = kTrue;
			break;
		}

		// Where this chapter's slice of the bar begins, and what the bar says it is doing. Both are
		// set before the open so the name on screen is the chapter being waited for, not the last
		// one finished.
		const int32 baseTicks = (int32)i * kKESCMChapterProgressSpan;
		progress.SetPosition(baseTicks);
		progress.SetTaskText(chapter.fName);

		// Already answered by the pairing: no counterpart on the other side, or no file to open.
		if (chapter.fState != kKESCMChapterUnknown)
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
			if (!CloseChapter(sourceRef, sourceMine))
				++leftOpen;
			if (!CloseChapter(targetRef, targetMine))
				++leftOpen;

			chapter.fState = kKESCMChapterFailed;
			chapter.fWhy   = why;
			continue;
		}

		{
			IDataBase* targetDB = targetRef.GetDataBase();
			IDataBase* sourceDB = sourceRef.GetDataBase();
			if (targetDB == nil || sourceDB == nil)
			{
				chapter.fState = kKESCMChapterFailed;
				chapter.fWhy   = PMString("the opened chapter has no database");
				chapter.fWhy.SetTranslatable(kFalse);
			}
			else
			{
				// ***** "Do not dirty the document" is not a promise never to touch it - it is
				// "if it was clean going in, it is clean coming out". ***** Composing and
				// rasterising both touch it, so both chapters are wrapped. The guards are scoped
				// so they have already restored by the time the chapter is closed below - a
				// document closed while still marked modified would prompt, and with kSuppressUI
				// a prompt means silently losing what is in it.
				IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
				IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

				// Compose BEFORE anything reads pixels - see RecomposeChapter.
				RecomposeChapter(targetRef);
				RecomposeChapter(sourceRef);

				PMString why;
				bool16   chapterCancelled = kFalse;
				chapter.fState = CompareChapter(targetDB, sourceDB, progress, baseTicks,
				                                chapterCancelled, why);
				if (chapter.fState == kKESCMChapterFailed)
					chapter.fWhy = why;
				if (chapterCancelled)
					cancelled = kTrue;
			}
		}

		// ***** The chapters this run opened are closed even when it was cancelled. ***** Cancelling
		// stops the comparison, not the tidying up: a chapter left open would go on locking its
		// .indd with no window for the user to close it by.
		if (!CloseChapter(sourceRef, sourceMine))
			++leftOpen;
		if (!CloseChapter(targetRef, targetMine))
			++leftOpen;

		if (cancelled)
			break;
	}

	// ***** ASKED ONCE MORE, AFTER THE LOOP. *****
	// A cancel pressed while the LAST chapter was being compared is never seen by a test that only
	// runs at the top of the next iteration - there is no next iteration. KBS spent a day on the
	// symptom this produces ("cancel works on the first chapter but not the last", and never at all
	// in a one-chapter book). The work is already done by this point; what this decides is what the
	// run is REPORTED as, and a run the user stopped must not claim to have finished.
	if (!cancelled && progress.WasCancelled(kFalse))
		cancelled = kTrue;

	// ***** Chapters the cancel never reached get a word of their own. *****
	// kKESCMChapterUnknown is the internal "not judged yet" and must not reach the screen; leaving it
	// would print "Unknown", and calling it NoChange would assert something this run never checked.
	if (cancelled)
	{
		for (size_t i = 0; i < outChapters.size(); ++i)
		{
			if (outChapters[i].fState == kKESCMChapterUnknown)
				outChapters[i].fState = kKESCMChapterNotCompared;
		}
	}

	int32 changed = 0, unchanged = 0, added = 0, deleted = 0, failed = 0, notCompared = 0;
	for (size_t i = 0; i < outChapters.size(); ++i)
	{
		switch (outChapters[i].fState)
		{
			case kKESCMChapterChanged:		++changed;		break;
			case kKESCMChapterNoChange:		++unchanged;	break;
			case kKESCMChapterAdded:		++added;		break;
			case kKESCMChapterDeleted:		++deleted;		break;
			case kKESCMChapterFailed:		++failed;		break;
			case kKESCMChapterNotCompared:	++notCompared;	break;
			default:										break;
		}
	}

	// ***** The chapter COUNT is always stated, and it comes FIRST. ***** An empty list has to be
	// readable as "every chapter was compared and none changed" rather than "nothing could be
	// opened" - conflating those two is the fault that took a day to find in KBS.
	//
	// ***** ONLY NON-ZERO COUNTS ARE LISTED, AND THE LINE HAS NO PREAMBLE. *****
	// ⚠ This is not tidiness, it is a MEASURED defect (2026-08-12). The line lives in one status
	// widget that ellipsizes in the MIDDLE, and a cancelled run adds two more counts to it. The
	// earlier wording overflowed and came out as
	//     "book co...5 chapters, 0 changed, 0 unchanged, 1 added, 14 not compared - cancelled"
	// - which reads as FIVE chapters. Ellipsis in the middle of a number does not look like damage,
	// it looks like a smaller number, so an overflowing summary here does not merely lose detail: it
	// states a wrong figure. Middle-ellipsis was itself the fix for the previous overflow, where the
	// failed count fell off the end (段階2), so the answer cannot be to move the truncation around
	// again - the line has to be short enough not to truncate.
	//   - dropping "book compare: " saves 14 characters and loses nothing: the dialog is titled
	//     "Compare Books" and this is the only status line in it. It also restores the wording the
	//     design specified (§4-2: "12 chapters: 5 changed, 6 unchanged, 1 failed").
	//   - dropping zeroes saves the rest. The VERDICT counts sum to the chapter count, which is
	//     printed, so a reader can still tell "15 chapters: 15 unchanged" from a partial run.
	//     ⚠ "left open" is NOT one of those, and the sum does not hold once it appears: it counts
	//       DOCUMENTS this run could not close - up to two per chapter - so a run that failed to close
	//       both sides of one chapter reads "3 chapters: 3 unchanged, 2 left open", whose parts add up
	//       to 5. It is listed here anyway because it is rare and has to be said; the reading to keep
	//       is that the five VERDICTS before it account for every chapter.
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
		// ★Before "failed": with chapters left uncompared, "1 changed, 5 unchanged" describes a
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

	// ★Last, and set off by a dash rather than a comma - it qualifies the whole line instead of
	// adding another count to it. Same shape the Refresh path uses ("refreshed 5 (changed 2) -
	// cancelled"). ⚠ It can appear with no "not compared" at all, when the cancel arrived as the
	// last chapter finished: nothing was lost, but the user did press Cancel, and the report says
	// what happened rather than what would have been tidier to say.
	if (cancelled)
		outReport.Append(" - cancelled");
	outReport.SetTranslatable(kFalse);

	// The per-chapter read-out, built HERE from the same list the caller receives - so the summary
	// line and the detail can never disagree about what happened.
	gBookResultText.Clear();
	gBookResultText.SetTranslatable(kFalse);
	for (size_t i = 0; i < outChapters.size(); ++i)
	{
		const KESCMChapterResult& chapter = outChapters[i];
		if (i > 0)
			gBookResultText.Append("\n");
		gBookResultText.Append(chapter.fName);
		gBookResultText.Append("\t");
		gBookResultText.Append(KESCMChapterStateText(chapter.fState));
		if (chapter.fState == kKESCMChapterFailed && !chapter.fWhy.IsEmpty())
		{
			gBookResultText.Append("\t");
			gBookResultText.Append(chapter.fWhy);
		}
	}

	return kSuccess;
}

void KESCMGetBookResultText(PMString& out)
{
	out = gBookResultText;
}

// End, KESCMBookCompare.cpp.
