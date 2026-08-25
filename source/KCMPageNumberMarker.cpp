//========================================================================================
//
//  KCMPageNumberMarker.cpp
//
//  Finding the automatic page number (folio) markers, declared in KCMPageNumberMarker.h.
//
//  How one page is examined:
//   1. Walk the page's own flattened items (ISpread::GetItemsOnPage).
//   2. From the master spread applied to the page (IMasterPage::GetMasterSpreadUID), collect the
//      master items that are actually drawn on this spread (IMasterSpreadUtils::
//      AppendMasterPageItems, whose header says it returns the items "drawn on the given spread
//      and page bounds"). Items overridden on the page itself are therefore expected not to
//      appear -- that is the implication of "drawn", not something the header states outright.
//   3. For each item, get the text model through IMultiColumnTextFrame and scan its range
//      (TextStart .. TextStart+TextSpan) character by character for kTextChar_PageNumber (0x18).
//      A folio is stored as that character code; see KCMTextRangeHasPageNumberMarker below.
//   4. Take the found frame's rectangle (GetPathBoundingBox) into spread coordinates through the
//      item's own InnerToSpreadMatrix -- for a master-derived item, composed with the offset
//      matrix AppendMasterPageItems returns -- and then back into page inner coordinates with the
//      page's inverse matrix.
//   5. Union in the real ink extent of **the digits that are actually drawn**
//      (KCMRealNumberInkInSpread), so that glyphs which overhang the frame (the descender or
//      overshoot of a large folio) are inside the excluded area rather than being reported as a
//      difference.
//      A master's wax is composed from placeholder characters and not from the page's real digits,
//      so only the baseline, the font and the coordinate transform are borrowed from the master's
//      wax; the glyph shapes come from the real page number (GetPageString). With no wax, no font
//      or no string it falls back to the frame rectangle alone.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "IGeometry.h"
#include "IHierarchy.h"
#include "ISpread.h"
#include "IMasterPage.h"
#include "IMasterSpreadUtils.h"
#include "IMultiColumnTextFrame.h"
#include "ITextFrameColumn.h"
#include "IGraphicFrameData.h"	// a graphic frame (the parent of a text frame) -> its child MC text frame
#include "ITextModel.h"
#include "IWaxStrand.h"			// the character ink bounds (for overhanging glyphs); the same known-good pattern KESCL uses
#include "IWaxIterator.h"
#include "IWaxLine.h"			// GetToSpreadMatrix / QueryWaxGlyphIterator
#include "IWaxGlyphIterator.h"	// walking glyphs (the first glyph's placement matrix = the baseline)
#include "IWaxGlyphs.h"
#include "IWaxRun.h"				// GetWaxRun -> IWaxRenderData
#include "IWaxRenderData.h"		// run -> font and font matrix (to build the IFontInstance for GetGlyphBBox)
#include "IFontMgr.h"			// QueryFontInstance
#include "IPMFont.h"
#include "IFontInstance.h"		// AppendGlyphIDs / GetGlyphBBox (the glyph outline's bbox = the true tight bounds)
#include "K2Vector.h"
#include "TextID.h"				// kFrameListBoss / IID_IWAXSTRAND
#include "K2SmartPtr.h"			// K2::scoped_ptr (releasing the IWaxIterator)
#include "TextChar.h"			// kTextChar_PageNumber, the folio marker's character code (0x18)
#include "textiterator.h"
#include "TransformUtils.h"		// InnerToSpreadMatrix
#include "PMMatrix.h"
#include "PMRect.h"
#include "PMPoint.h"
#include "UIDList.h"
#include "Utils.h"

#include "IPageList.h"			// the real page number as a string (GetPageString)
#include "CTextEnum.h"			// Text::GlyphID

#include <algorithm>			// std::find, scanning for the folio marker
#include <map>					// the cache of excluded rectangles ((db, page) -> rectangles)
#include <utility>				// std::pair (that key)

#include "KCMPageNumberMarker.h"

// Default kFalse: a difference in the folio normally counts as a change like any other. Session
// only -- it is not saved into the document.
static bool16 sIgnorePageNumberMarker = kFalse;

bool16 KCMGetIgnorePageNumberMarker()
{
	return sIgnorePageNumberMarker;
}

void KCMSetIgnorePageNumberMarker(bool16 on)
{
	sIgnorePageNumberMarker = on;
	// Throw the cache of excluded rectangles away on every switch. The cache holds what the last
	// comparison measured, so the reader needs some way to force a re-measure after moving a folio
	// frame; switching this off and on is that way (a re-comparison updates it too, of course).
	KCMInvalidatePageNumberMarkerRects();
}

// Is there an automatic page-number (folio) marker in [start, start+span)?
// It is stored as the character code kTextChar_PageNumber (0x18) -- confirmed in a real document's
// IDML, whose Content read <?ACE 18?> = 0x18.
// kTextChar_CurrentPageNumber (0xE025) is NOT what to look for: that is a Find/Change-only
// representation and never appears in a document's character stream (TextChar.h:496, "used only by
// Find/Change, and are not stored in a document").
// 0x18 is a control code that does not occur in ordinary text, and the current, next and previous
// page numbers are ALL stored as this same 0x18 (which of them it is lives in the character
// attributes), so its presence alone identifies a folio.
static bool16 KCMTextRangeHasPageNumberMarker(ITextModel* textModel, TextIndex start, int32 span)
{
	if (textModel == nil || span <= 0)
		return kFalse;
	// TextIterator is a genuine STL bidirectional iterator (textiterator.h:115-119 declares
	// value_type = UTF32TextChar and iterator_category = std::bidirectional_iterator_tag), so the
	// scan is one std::find instead of a hand-written loop. Official precedent:
	// xmlmarkupinjector/XMLMrkSuiteTextCSB.cpp:447 runs std::find_first_of over the same iterator.
	TextIterator iter(textModel, start);
	TextIterator endIter(textModel, start + span);
	return std::find(iter, endIter, kTextChar_PageNumber) != endIter;
}

// Does itemUID's text frame contain a folio marker (0x18)? If so, hand back the text model and the
// range as well, which is what the ink calculation needs.
// What the item actually is depends on where it came from:
//   (a) a graphic frame (the spline that is a text frame's parent). **Master items arrive this
//       way**: AppendMasterPageItems defaults to kSkipChildren, so it returns the parent frame and
//       not the child that holds the text. IGraphicFrameData::QueryMCTextFrame() descends to the
//       child MC text frame and its text model.
//   (b) the item implements IMultiColumnTextFrame or ITextFrameColumn itself (read directly).
// If a text model comes out of either, it is scanned; if none does, kFalse.
static bool16 KCMQueryMarkerText(IDataBase* db, UID itemUID,
	InterfacePtr<ITextModel>& outModel, TextIndex& outStart, int32& outSpan)
{
	// (a) graphic frame -> child MC text frame
	InterfacePtr<IGraphicFrameData> gfd(db, itemUID, UseDefaultIID());
	if (gfd != nil)
	{
		InterfacePtr<IMultiColumnTextFrame> childMcf(gfd->QueryMCTextFrame());
		if (childMcf != nil)
		{
			InterfacePtr<ITextModel> textModel(childMcf->QueryTextModel());
			if (KCMTextRangeHasPageNumberMarker(textModel, childMcf->TextStart(), childMcf->TextSpan()))
			{
				outModel = textModel; outStart = childMcf->TextStart(); outSpan = childMcf->TextSpan();
				return kTrue;
			}
		}
	}
	// (b) the item is an MC text frame itself
	InterfacePtr<IMultiColumnTextFrame> mcf(db, itemUID, UseDefaultIID());
	if (mcf != nil)
	{
		InterfacePtr<ITextModel> textModel(mcf->QueryTextModel());
		if (KCMTextRangeHasPageNumberMarker(textModel, mcf->TextStart(), mcf->TextSpan()))
		{
			outModel = textModel; outStart = mcf->TextStart(); outSpan = mcf->TextSpan();
			return kTrue;
		}
	}
	// (b) the item is a single-column text frame column
	InterfacePtr<ITextFrameColumn> tfc(db, itemUID, UseDefaultIID());
	if (tfc != nil)
	{
		InterfacePtr<ITextModel> textModel(tfc->QueryTextModel());
		if (KCMTextRangeHasPageNumberMarker(textModel, tfc->TextStart(), tfc->TextSpan()))
		{
			outModel = textModel; outStart = tfc->TextStart(); outSpan = tfc->TextSpan();
			return kTrue;
		}
	}
	return kFalse;
}

// The ink extent of the digits **actually drawn** in a folio frame, in the coordinates of the
// spread that frame belongs to.
// A master's folio frame is composed from placeholder characters ("A" and the like), whose glyphs
// differ from the real digits printed on the page ("3"), so the overhang of the real page cannot be
// measured from the master's wax. Instead:
//   1. borrow from the master's wax only the baseline (the first glyph's placement matrix, gm), the
//      line-to-spread transform, and the font (an IFontInstance with the point size in it) -- all
//      of which are the same whatever the digits are;
//   2. take the string that really appears from IPageList::GetPageString(pageUID);
//   3. place each of its glyph bboxes (IFontInstance::GetGlyphBBox) on that baseline and union them.
// The result is exactly how far the real digits overflow, descenders and overshoot included.
// Horizontally everything is stacked at the first glyph's position, which is harmless: the caller
// unions this with the frame rectangle, and the frame covers the X extent.
// The coordinate system of the return value is the frame's own spread (for a master-derived item the
// caller composes the offset).
// Recompose is deliberately NOT called -- this can run inside a draw event, and recomposing there
// would re-enter. With no wax, no font or no string it returns an empty rect and the caller falls
// back naturally to the frame rectangle alone.
static PMRect KCMRealNumberInkInSpread(ITextModel* masterTextModel, TextIndex start,
	IDataBase* db, UID pageUID)
{
	PMRect result(0, 0, 0, 0);	// empty; the caller tests with IsEmpty
	if (masterTextModel == nil || db == nil || pageUID == kInvalidUID)
		return result;

	// ---- 1. borrow the baseline placement (gm), line->spread (lm) and font (fi) from the wax ----
	InterfacePtr<IWaxStrand> waxStrand((IWaxStrand*)masterTextModel->QueryStrand(kFrameListBoss, IID_IWAXSTRAND));
	if (waxStrand == nil)
		return result;
	// A READ-ONLY iterator: this walk changes no wax and applies nothing (it borrows the baseline,
	// the font and the transform). IWaxStrand.h:100-106 offers this one for exactly that -- "code
	// that does not change the wax and does not Apply", drawing among it -- and the product code
	// uses it (spellpanel/PrivateSpellingUtils.cpp:371,579).
	// The sample (SnpEstimateTextDepth.cpp:208) uses the ordinary one instead; where the two
	// disagree, follow the product (KBSGlyphScanEngine.cpp does the same).
	K2::scoped_ptr<const IWaxIterator> waxIter(waxStrand->NewReadOnlyWaxIterator());
	if (waxIter == nil)
		return result;
	const IWaxLine* line = waxIter->GetFirstWaxLine(start);
	if (line == nil)
		return result;	// not composed, or overset -> the frame rectangle alone
	// The strand can still hand out a line the composer has discarded, so test before touching it.
	// The product does the same (PrivateSpellingUtils.cpp:387-389, whose comment cites bug 538392 --
	// a real defect, not a precaution). We are MORE exposed than the product here, because we
	// deliberately do not recompose (see above).
	// @warning do not add the IsDamaged test that stands beside it there. That one is the drawing
	//   side's judgement ("it is going to be redrawn anyway"), and this use -- borrowing a
	//   baseline, a font and a transform -- works fine on a damaged line. Rejecting it would shrink
	//   the result to the frame rectangle, i.e. narrow the excluded area and cause MORE false
	//   differences.
	if (line->IsDestroyed())
		return result;

	const PMMatrix lineToSpread = line->GetToSpreadMatrix();

	// The first glyph's placement matrix (pen (0,0) = the line's start, on the baseline; measured
	// with gmXsc = 1.0, so nothing is rescaled) and its run's font (an IFontInstance with the size).
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return result;
	K2::scoped_ptr<IWaxGlyphIterator> git(line->QueryWaxGlyphIterator(kFalse));
	if (git == nil)
		return result;
	git->Reset();
	IWaxGlyphs* g = git->GetWaxGlyphsContainer();
	if (g == nil)
		return result;
	PMMatrix baselinePlacement;			// the first glyph's GetGlyphMatrix (line start, on the baseline)
	PMPoint pen(0, 0);
	git->GetGlyphMatrix(&baselinePlacement, &pen);
	IWaxRun* run = git->GetWaxRun();
	InterfacePtr<IWaxRenderData> rd(run, UseDefaultIID());
	if (rd == nil)
		return result;
	InterfacePtr<IPMFont> font(rd->QueryFont());
	if (font == nil)
		return result;
	const PMMatrix fontMatrix = rd->GetFontMatrix();
	InterfacePtr<IFontInstance> fontInst(fontMgr->QueryFontInstance(font, fontMatrix));
	if (fontInst == nil)
		return result;

	// ---- 2. the number string that really appears on the page ----
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
		return result;
	PMString numStr;
	// Called the same way the badge's "current number" is called (IPageList.h:141-146):
	//   3rd, bIncludeSectionName = kFalse ... the number ALONE. kTrue returns the "A:12" form,
	//     which is how the Pages panel spells it and not what is printed on the page. (With a
	//     section set to include its prefix the prefix really is printed, but even then there is no
	//     ":" separator, so kTrue would not match either.) This implementation stacks every glyph at
	//     the first position and unions the bboxes, so a stray character would only affect the
	//     vertical extent -- but measuring exactly the characters that are drawn is still closer.
	//   4th, bUseIntegerStyle = kFalse ... the section's own numbering style, so roman numerals and
	//     the rest look like what is printed.
	//   7th, bIncludePagesOfHiddenSpread = kFalse ... numbering that skips hidden spreads.
	//     **InDesign has TWO page numbers, and they differ**:
	//       (a) the Pages panel / the page-number field / the DOM's page.name /
	//           GetPageString(..., kTrue) ... counts pages on hidden spreads too (hiding one leaves
	//           the rest numbered as they were);
	//       (b) the folio composed onto the page / GetPageString(..., kFalse) ... skips hidden
	//           spreads (hide the first spread and the second page prints "1"; confirmed on screen).
	//     **kFalse is the right one here** because this function measures the ink of the digits
	//     that are really printed, so it has to count the way the real folio counts.
	//     @warning the TSV export, Prev/Next and Story Edits all use kTrue instead. Those NAME a
	//       page to a person and so must spell it the way the Pages panel does. **The asymmetry is
	//       deliberate: do not "make them consistent".**
	pageList->GetPageString(pageUID, &numStr, kFalse, kFalse, kDefaultPageType, kTrue, kFalse);
	const int32 nch = numStr.NumUTF16TextChars();
	if (nch <= 0)
		return result;
	const UTF16TextChar* buf = numStr.GrabUTF16Buffer(nil);
	if (buf == nil)
		return result;

	// ---- 3. place each glyph's bbox on the baseline and union them ----
	K2Vector<Text::GlyphID> gids;
	fontInst->AppendGlyphIDs(buf, nch, gids);
	for (int32 i = 0; i < (int32)gids.size(); ++i)
	{
		PMRect bbox = fontInst->GetGlyphBBox(gids[i]);	// size already applied, relative to the baseline (measured)
		if (bbox.IsEmpty())
			continue;								// notdef and the like
		baselinePlacement.Transform(&bbox);		// glyph space -> wax (line start, baseline)
		lineToSpread.Transform(&bbox);			// wax -> spread
		result.Union(bbox);
	}
	return result;
}

// Take itemUID's rectangle (a text frame, in its own inner coordinates, which are not the page's)
// into spread coordinates through itemToSpread (the item's own InnerToSpreadMatrix, composed with
// masterOffset for a master item) and then into page inner coordinates through spreadToPage.
static PMRect KCMItemRectToPageInner(IDataBase* db, UID itemUID,
	const PMMatrix& itemToSpread, const PMMatrix& spreadToPage)
{
	InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
	PMRect r;
	if (itemGeo == nil)
		return r;
	r = itemGeo->GetPathBoundingBox();
	itemToSpread.Transform(&r);
	spreadToPage.Transform(&r);
	return r;
}

//========================================================================================
// KCMAppendPageNumberMarkerRects (declared in KCMPageNumberMarker.h)
//   Collect pageRef's folio frame rectangles, in points with the page's top left as the origin.
//========================================================================================
void KCMAppendPageNumberMarkerRects(const UIDRef& pageRef, std::vector<PMRect>& outRects)
{
	IDataBase* db = pageRef.GetDataBase();
	const UID pageUID = pageRef.GetUID();
	if (db == nil || pageUID == kInvalidUID)
		return;

	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
	if (pageGeo == nil || pageHier == nil)
		return;

	const UID spreadUID = pageHier->GetSpreadUID();
	InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
	if (spread == nil)
		return;

	// This page's index within its spread, which GetItemsOnPage and AppendMasterPageItems both want.
	int32 pgPos = -1;
	{
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			if (spread->GetNthPageUID(p) == pageUID) { pgPos = p; break; }
	}
	if (pgPos < 0)
		return;

	const PMMatrix pageToSpread = ::InnerToSpreadMatrix(pageGeo);
	const PMMatrix spreadToPage = pageToSpread.Inverse();
	// The offset that moves the origin to the page's top left (the page inner bbox's Left/Top).
	const PMRect pageBoundsInPage = pageGeo->GetPathBoundingBox();
	const PMPoint origin(pageBoundsInPage.Left(), pageBoundsInPage.Top());

	// ---- 1. local items (the ones that really belong to this page) ----
	UIDList localItems(db);
	spread->GetItemsOnPage(pgPos, &localItems, kFalse /*exclude the page shape itself*/, kFalse, kTrue);
	const int32 nLocal = localItems.Length();
	for (int32 i = 0; i < nLocal; ++i)
	{
		const UID itemUID = localItems[i];
		InterfacePtr<ITextModel> markerModel;
		TextIndex mStart = 0; int32 mSpan = 0;
		if (!KCMQueryMarkerText(db, itemUID, markerModel, mStart, mSpan))
			continue;
		InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
		if (itemGeo == nil)
			continue;
		const PMMatrix itemToSpread = ::InnerToSpreadMatrix(itemGeo);
		PMRect r = KCMItemRectToPageInner(db, itemUID, itemToSpread, spreadToPage);
		// Union in the ink of the digits actually drawn, so glyphs overhanging the frame (a large
		// folio's descender or overshoot) are excluded too and their pixels are not reported as a
		// difference. A local item's wax belongs to the drawing spread itself, so spreadToPage
		// takes it straight to page inner coordinates.
		PMRect ink = KCMRealNumberInkInSpread(markerModel, mStart, db, pageUID);
		if (!ink.IsEmpty())
		{
			spreadToPage.Transform(&ink);
			r.Union(ink);
		}
		r.Left(r.Left() - origin.X());   r.Right(r.Right() - origin.X());
		r.Top(r.Top() - origin.Y());     r.Bottom(r.Bottom() - origin.Y());
		outRects.push_back(r);
	}

	// ---- 2. master-derived (un-overridden) items ----
	InterfacePtr<IMasterPage> masterPage(db, pageUID, UseDefaultIID());
	if (masterPage != nil && masterPage->IsValid())
	{
		PMRect pageBoundsInSpread = pageBoundsInPage;
		pageToSpread.Transform(&pageBoundsInSpread);

		UIDList onThesePages(db);
		onThesePages.Append(pageUID);
		PMRectCollection pageBoundsList;
		pageBoundsList.push_back(pageBoundsInSpread);

		UIDList masterItems(db);
		UIDList itemPages(db);
		PMMatrixCollection offsets;
		Utils<IMasterSpreadUtils>()->AppendMasterPageItems(db, spreadUID, onThesePages, pageBoundsList,
			masterItems, itemPages, offsets);

		const int32 nMaster = masterItems.Length();
		for (int32 i = 0; i < nMaster; ++i)
		{
			const UID itemUID = masterItems[i];
			InterfacePtr<ITextModel> markerModel;
			TextIndex mStart = 0; int32 mSpan = 0;
			if (!KCMQueryMarkerText(db, itemUID, markerModel, mStart, mSpan))
				continue;
			InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
			if (itemGeo == nil)
				continue;
			// Compose the master item's own inner->(master spread) with the offset
			// AppendMasterPageItems returns (master spread -> the spread being drawn).
			// m1 * m2 means "apply m1, then m2".
			const PMMatrix itemToMasterSpread = ::InnerToSpreadMatrix(itemGeo);
			const PMMatrix itemToDrawingSpread = itemToMasterSpread * offsets[i];
			PMRect r = KCMItemRectToPageInner(db, itemUID, itemToDrawingSpread, spreadToPage);
			// Union in the real digits' ink, as for a local item. The master frame's wax is
			// composed from placeholders, but KCMRealNumberInkInSpread borrows only the baseline,
			// the font and the transform from it and takes the glyphs from the real page number,
			// so it fits what is actually printed. The coordinates go master spread -> offsets[i]
			// (the drawing spread) -> spreadToPage, the same path the path rectangle takes.
			PMRect ink = KCMRealNumberInkInSpread(markerModel, mStart, db, pageUID);
			if (!ink.IsEmpty())
			{
				offsets[i].Transform(&ink);
				spreadToPage.Transform(&ink);
				r.Union(ink);
			}
			r.Left(r.Left() - origin.X());   r.Right(r.Right() - origin.X());
			r.Top(r.Top() - origin.Y());     r.Bottom(r.Bottom() - origin.Y());
			outRects.push_back(r);
		}
	}
}


//========================================================================================
// The cache of excluded rectangles (declared in KCMPageNumberMarker.h)
//   Measuring one page means enumerating every item on it, scanning every character of every text
//   frame, collecting the master page's items, walking the wax and taking glyph bboxes. That was
//   being done for every page on every draw event (to paint the green wash), so the result is
//   remembered under (db, page UID) and looked up instead.
//   The db pointer is for comparison only and is never dereferenced. A closed db's entries are
//   dropped by Invalidate.
//
// **WHY NO LOCK (KCMMarkStateLock) HERE.** The shape is exactly sEntries': a std::map read from
//   inside a draw event, which looks read-only but **inserts on a cache miss** (i.e. walks the
//   tree). It is still not needed, because **every reader and every writer is on the main thread**:
//     1. KCMDrawPageNumberMarkerFill (drawing) ... the caller gates on
//        `fillExcluded = !printing && ...`, so **the background thread (the asynchronous PDF
//        export) never arrives here at all** -- printing is true there. The green wash is a
//        SCREEN-ONLY diagnostic showing what the comparison excluded, and that decision doubles as
//        the thread boundary.
//     2. MakeEntry (the comparison) ... comparisons only ever run on the main thread.
//     3. **The book comparison** (two calls to KCMGetPageNumberMarkerRects in KCMBookCompare.cpp),
//        which opens one chapter pair at a time. Also main.
//        It passes refresh=kTrue (force a re-measure), and **that route needs it**: the key is
//        (IDataBase*, page UID), and a closed chapter's db address can be reused by the next
//        chapter ([[uidref-reuse-after-close]]).
//     4. KCMSetIgnorePageNumberMarker / KCMHandleDocsClosed / Shutdown (the discarding side) ...
//        all main.
//   So this is the same "the readers are all on the main thread, so it need not be guarded" as
//   DropAllOrig (whose reasoning is in KCMDrawEventHandler.h).
//   **What breaks that assumption**: painting the green wash into print or PDF. Do that and BOTH
//   KCMGetPageNumberMarkerRects and KCMInvalidatePageNumberMarkerRects need KCMMarkStateLock --
//   guarding only the discarding side is worthless, and is a mistake this plug-in has already made.
//   @warning the count of callers above is worth re-deriving with grep rather than trusting: it
//   once said "three callers" and had missed the book comparison. The conclusion (all main) did not
//   change, but **reading "all three are main" while one is uncounted is how a fourth caller on
//   another thread gets missed.**
//========================================================================================
typedef std::pair<IDataBase*, UID>              KCMMarkerRectKey;
typedef std::map<KCMMarkerRectKey, std::vector<PMRect> > KCMMarkerRectMap;
static KCMMarkerRectMap sMarkerRectCache;

const std::vector<PMRect>& KCMGetPageNumberMarkerRects(const UIDRef& pageRef, bool16 refresh)
{
	// The immutable empty returned for bad arguments (never cached -- do not remember an invalid key).
	static const std::vector<PMRect> kNoRects;

	IDataBase* const db      = pageRef.GetDataBase();
	const UID        pageUID = pageRef.GetUID();
	if (db == nil || pageUID == kInvalidUID)
		return kNoRects;

	const KCMMarkerRectKey key(db, pageUID);
	KCMMarkerRectMap::iterator it = sMarkerRectCache.find(key);
	if (it != sMarkerRectCache.end() && !refresh)
		return it->second;	// what was remembered (zero rectangles is remembered too -- that is the most expensive miss)

	std::vector<PMRect> rects;
	KCMAppendPageNumberMarkerRects(pageRef, rects);
	if (it == sMarkerRectCache.end())
		it = sMarkerRectCache.insert(std::make_pair(key, std::vector<PMRect>())).first;
	it->second.swap(rects);	// swap the contents in rather than copying (the old value dies with the local)
	return it->second;
}

void KCMInvalidatePageNumberMarkerRects()
{
	sMarkerRectCache.clear();
}
