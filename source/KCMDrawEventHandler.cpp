//========================================================================================
//
//  KCMDrawEventHandler.cpp
//
//  The difference overlay's drawing engine: the rings, the peek at the older version, the
//  rasterisation used for comparing (MakeEntry / MakeOrigImage) and the image helpers.
//  The shared state (the static members) is published in KCMDrawEventHandler.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Object model / drawing / rasterisation (the SDK headers the engine uses):
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IGeometry.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISpread.h"
#include "IShape.h"
#include "IDrwEvtHandler.h"
#include "IDrwEvtDispatcher.h"
#include "CServiceProvider.h"
#include "DocumentContextID.h"
#include "GraphicsID.h"
#include "GraphicsData.h"
#include "IGraphicsPort.h"
#include "AutoGSave.h"
#include "IControlView.h"
#include "IPanorama.h"
#include "IWidgetParent.h"
#include "ISession.h"
#include "PMMatrix.h"
#include "PMReal.h"
#include "PMString.h"			// what GetPageString writes into
// TransformUtils.h is deliberately NOT included: the eight uses of ::InnerToSpreadMatrix all moved
// to IGeometryFacade::GetItemBounds, so nothing here references it any more.
#include "IGeometryFacade.h"	// GetItemBounds -- a page's box in spread coordinates (worked example: snapshot/SnapTracker.cpp:621)
#include "SnapshotUtilsEx.h"
#include "AGMImageAccessor.h"
#include "GraphicsExternal.h"
#include "IXPUtils.h"
#include "IXPManager.h"				// GetDocumentBlendingSpace / ReleaseBlendingSpace (the PDF export's transparency group)
#include "IViewPortAttributes.h"		// asking the port for kPDFExportVPAttr / kPDFIsFlattenerTargetVPAttr
#include "PDFID.h"					// those two ViewPortAttr IDs (PDFID.h:1543-1544)


// For the original-page-number badge (Show Original Page Numbers):
#include "IPageList.h"			// GetPageString(..., bIncludePagesOfHiddenSpread) -- kTrue = the original number, kFalse = the number as it stands with spreads hidden
#include "IFontMgr.h"			// the default font (the same way framelabel's FrmLblAdornment.cpp does it)
#include "IPMFont.h"
#include "IFontInstance.h"		// MeasureWText (centring) / GetDescent

#include <algorithm>	// std::min -- the page's short side (four places used to spell out the same ternary)
#include <map>
#include <set>
#include <new>			// std::nothrow for the image buffers: MSVC's ordinary new throws rather than returning nil
#include <string.h>

// Project includes:
#include "KCMID.h"
#include "KCMCore.h"               // KCMHandleDocsClosed -- one place for the after-a-close clean-up
#include "KCMPageMap.h"            // KCMPageMapIsRegistered / KCMPageMapHasAnyRegistered (the added/removed pages)
#include "KCMPageCheck.h"          // KCMPageCheckIsChecked / KCMPageCheckHasAny (the "Check" ticks)
#include "KCMPageNumberMarker.h"   // KCMGetIgnorePageNumberMarker / KCMAppendPageNumberMarkerRects (the folio exclusion)
#include "KCMThreadSafety.h"       // KCMIsSameDoc (the background thread's cloned db) / KCMIsMainThread / the mark-state lock
// The press-time HUD moved to the UI's own drawing service (KCMUIDrawEvent.cpp): whether a button
// is held is the tool's state, which is the UI's and not visible from the model. **This file
// includes no UI header.**
#include "KCMDrawEventHandler.h"

std::map<UID, KCMOverlayEntry*> KCMDrawEventHandler::sEntries;
IDataBase* KCMDrawEventHandler::sDB = nil;
bool16 KCMDrawEventHandler::sMarksVisible = kFalse;	// default hidden; the marks appear while the tool's left button is held (the master toggle)
PMReal KCMDrawEventHandler::sMarkScreenOpacity = 1.0;	// default opaque. While the button is held, or while printing is on, this is the chosen 25%/75%
bool16 KCMDrawEventHandler::sPrintMarks = kFalse;	// default screen only (nothing goes into print or PDF)
bool16 KCMDrawEventHandler::sMarkOpacity25 = kTrue;	// default 25%, matching the panel's default radio. kFalse = 75%
bool16 KCMDrawEventHandler::sMarkColorCyan = kFalse;	// default red. kTrue = cyan
bool16 KCMDrawEventHandler::sShowOldNumbers = kFalse;	// default off (the flyout's "Show Original Page Numbers")
bool16 KCMDrawEventHandler::sMarksTempHidden = kFalse;	// with "Always Show Marks on Target" on, kTrue only while the tool's left button is held over the Target window
bool16 KCMDrawEventHandler::sSrcMarksPressed = kFalse;	// kTrue only while the button is held over the Source window. It records "pressed", not "hidden" -- the drawing XORs it with sSrcMarksOn (see the declaration)
bool16 KCMDrawEventHandler::sSrcMarksOn = kFalse;	// default off (the flyout's "Always Show Marks on Source"). Start does not touch it: the setting is saved in the panel state and restored at start-up, so a Start that overwrote it would wipe the reader's choice
bool16 KCMDrawEventHandler::sTgtMarksOn = kFalse;	// the Target counterpart. Screen only -- print and PDF are decided by sPrintMarks. Start does not touch it either
IDataBase* KCMDrawEventHandler::sSrcDB = nil;
std::map<UID, UID> KCMDrawEventHandler::sSrcPageToTarget;
std::map<UID, UID> KCMDrawEventHandler::sPrevPairTargetToSource;	// the last comparison's pairing, for the register toggle's differential re-comparison
std::set<UID> KCMDrawEventHandler::sOverflowT;					// the overflow ("/") pages, Target side
std::set<UID> KCMDrawEventHandler::sOverflowS;					// and Source side
IDataBase* KCMDrawEventHandler::sOverflowCacheDB = nil;			// the sDB those two were built for
IDataBase* KCMDrawEventHandler::sOverflowCacheSrcDB = nil;			// and the sSrcDB
// Thread-local. The initial value kFalse means "kFalse the first time any thread reads it".
// What breaks if this is a plain static is written at the declaration (KCMDrawEventHandler.h).
IDThreading::ThreadLocal<bool16> KCMDrawEventHandler::tl_Rasterizing(kFalse);	// kTrue only while WE are rasterising (re-entrancy guard)
bool16 KCMDrawEventHandler::sThumbExperiment = kTrue;	// kFalse restores the earlier behaviour (nothing drawn into thumbnails)
std::map<UID, KCMOrigImage*> KCMDrawEventHandler::sOrigImages;
IDataBase* KCMDrawEventHandler::sOrigDB = nil;
bool16 KCMDrawEventHandler::sShowOriginal = kFalse;	// default hidden
PMReal KCMDrawEventHandler::sOrigScale = 0.0;	// the zoom scale the pictures were rasterised at (0 = not set)
PMReal KCMDrawEventHandler::sPeekOpacity = 1.0;	// default opaque (Shift peek). Shift+Alt peek sets 0.5
bool16 KCMDrawEventHandler::sOversetOn = kFalse;	// the Find Overset toggle (default off)
IDataBase* KCMDrawEventHandler::sOversetDB = nil;	// the document scanned (identity only)
std::set<UID> KCMDrawEventHandler::sOversetPages;	// the page UIDs holding overset
std::vector<KCMOversetLoc> KCMDrawEventHandler::sOversetLocs;	// where each overset "+" goes (Prev/Next's stops)

// How far inside the page rectangle to clip, in points. Facing pages meet at the spine with no
// gap, so clipping exactly at the page rectangle puts the outermost pixel of a frame or slash on
// the shared line and draws a 1px line into the neighbouring (unchanged) page. Coming in about
// 1pt prevents that.
//
// **The width passed to rectclip can never go negative.** The four functions that use this
// (KCMDrawEntryOnPage / KCMDrawPageBorder / KCMDrawPageDiagonal / KCMDrawPageCrossOutlined) hand
// `pr.Width() - kKCMClipInset * 2.0` straight to rectclip, so a page narrower than 2.0pt would
// pass a negative width. Measured: **the minimum page width InDesign allows is 114.38-114.39pt**
// (about 1.5888 inch, 40.4mm) -- anything smaller assigned to documentPreferences.pageWidth is
// rejected with "the value is out of range" (30481), and zeroing the margins and columns does not
// move that floor. (The maximum is 15,551.996pt = 216.000 inch = 5486.4mm.)
// So even the smallest page is 57 times the inset, and **no guard is needed in those four
// functions** -- adding one only adds unreachable code.
static const PMReal kKCMClipInset = 1.0;

//========================================================================================
// The font cache for the original-page-number badge. The default font at a fixed size never
// changes, so it is fetched once instead of on every draw.
// It is NOT a file-static InterfacePtr: releasing at static destruction time happens after the
// object model is gone, which is unsafe. Raw pointers plus an explicit
// KCMReleaseOldNumFontCache at Shutdown instead.
//========================================================================================
static IPMFont*       sOldNumFont      = nil;
static IFontInstance* sOldNumFontInst  = nil;
static bool16         sOldNumFontTried = kFalse;	// so a failed fetch is not retried on every draw (once per session)

void KCMReleaseOldNumFontCache()
{
	if (sOldNumFontInst != nil) { sOldNumFontInst->Release(); sOldNumFontInst = nil; }
	if (sOldNumFont != nil)     { sOldNumFont->Release();     sOldNumFont = nil; }
	sOldNumFontTried = kFalse;
}

//========================================================================================
// The overflow cache (the "/" pages that were never compared). Built once when a comparison runs,
// rather than by calling KCMBuildPairing -- a walk of both documents' pages -- on every draw.
//========================================================================================
void KCMDrawEventHandler::RebuildOverflowCache()
{
	// **The walk happens outside the lock; only the swap is inside.**
	//   1. sOverflowT/S are **written by the main thread and counted by the background thread
	//      while it draws** (both loops in DrawSpreadMarks) -- exactly the condition
	//      KCMThreadSafety.h:76-81 says to guard. A bare clear() + insert() leaves a window where
	//      **the background thread counts while the main thread is walking the tree** (the same
	//      reason, and the same opponent, as sEntries).
	//   2. But KCMBuildPairing walks **every page of both documents**, and must not run with the
	//      lock held (same header, :88-89: do not hold the lock through a long operation). So the
	//      new sets are built locally and swapped in.
	// A useful side effect: a draw that arrives mid-rebuild sees **the previous sets rather than
	// empty ones** (with a bare clear the "/" marks flickered out). swap is O(1) and cannot throw.
	sOverflowCacheDB    = sDB;
	sOverflowCacheSrcDB = sSrcDB;
	std::set<UID> newT, newS;
	if (sDB != nil && sSrcDB != nil)
	{
		std::vector<UID> tp, sp, tov, sov;
		KCMBuildPairing(sDB, sSrcDB, tp, sp, &tov, &sov);
		newT.insert(tov.begin(), tov.end());
		newS.insert(sov.begin(), sov.end());
	}
	{
		KCMMarkStateLock lock(KCMMarkStateMutex());
		sOverflowT.swap(newT);
		sOverflowS.swap(newS);
	}
}

void KCMDrawEventHandler::EnsureOverflowCache()
{
	// Rebuild only when the (sDB, sSrcDB) the cache was built for differs from the current pair --
	// the guard for a document switch or a re-comparison that moved to another document. Start,
	// register-add and the Ignore toggle all call RebuildOverflowCache directly from
	// KCMDoMarkChangesDoc, so an ordinary draw of the same pair does nothing here and no full walk
	// happens per draw.
	// What is compared is **one static against another** (sOverflowCacheDB against sDB), so the
	// answer is the same on every thread.
	if (sOverflowCacheDB == sDB && sOverflowCacheSrcDB == sSrcDB)
		return;

	// **Never rebuild on a background thread.** RebuildOverflowCache writes four shared statics
	// (sOverflowT / sOverflowS / sOverflowCacheDB / sOverflowCacheSrcDB) and walks both documents
	// to do it. Running that from the background (the asynchronous PDF export) rebuilds the sets
	// while the main thread is reading them (guide vol1-07 L104, "They do share globals and
	// statics").
	// Reaching here on a background thread only happens when the main thread has not built them
	// yet, or right after the document pair changed; the cost is that the overflow "/" does not
	// appear (the marks themselves come from sEntries). **Not drawing is the safe side.**
	if (!KCMIsMainThread())
		return;

	RebuildOverflowCache();
}

void KCMDrawEventHandler::BuildRing(uint8* buf, int32 rb, int32 bpp, int32 wt, int32 ht,
	const uint8* dist, int32 radius)
{
	if (buf == nil || dist == nil || wt <= 0 || ht <= 0 || bpp < 3)
		return;
	if (radius < 1) radius = 1;
	const int32 colorOff = bpp - 3;
	const uint8 rad = (radius > 255) ? 255 : (uint8)radius;	// dist is uint8 clamped at 255; the radius ceiling is 200

	// The mark colour is the panel's choice, read **once, outside the loop** -- it is not a
	// per-pixel question.
	uint8 markR = 0, markG = 0, markB = 0;
	KCMDrawEventHandler::SelectedMarkColor(markR, markG, markB);
	// Nothing special is needed where a change touches the page edge: the frame band below paints
	// everything within radius of the edge unconditionally, so the edge is always filled and the
	// border never thins out.

	// One pass over the distance transform. The ring is 0 < dist <= radius, i.e. "a changed pixel
	// is within radius, and this pixel is not itself a changed one". This replaced a horizontal
	// dilation followed by a vertical one (two O(W*H) sliding windows), so a zoom step costs about
	// a third of what it did. The distance is chessboard, so the ring is square-cornered exactly
	// as before.
	for (int32 y = 0; y < ht; ++y)
	{
		uint8* rowB = buf + (size_t)y * rb;
		const uint8* drow = dist + (size_t)y * wt;
		for (int32 x = 0; x < wt; ++x)
		{
			uint8* pixT = rowB + (size_t)x * bpp;	// ARGB, alpha first
			uint8* px = pixT + colorOff;
			const uint8 d = drow[x];
			// The band around a changed pixel (distance <= radius).
			const bool16 ring = (d != 0 && d <= rad);
			// The border band inside the page edge: everything within radius pixels of the edge of
			// the buffer is painted as an outer frame, change or no change. Its thickness is the
			// same radius as the ring, which is recomputed at every zoom -- so it stays a constant
			// number of screen pixels. Colour and alpha are the ring's (the density is applied by
			// the blit's opacity).
			const bool16 frame = (x < radius || (wt - 1 - x) < radius ||
			                      y < radius || (ht - 1 - y) < radius);
			if (ring || frame)
			{
				// A ring or frame pixel, in the colour the reader chose (SelectedMarkColor).
				// The print side needs no change for this: it **reads this image's colour** and
				// sorts pixels into a red mask and a cyan one (see PassDef below), so a
				// single-coloured image prints in that single colour automatically.
				px[0] = markR; px[1] = markG; px[2] = markB;
				if (bpp >= 4) pixT[0] = kKCMRingAlpha;	// the ring pixels' own alpha (opaque); the visible density is applied with setopacity
			}
			else { px[0] = 255; px[1] = 255; px[2] = 255; if (bpp >= 4) pixT[0] = 0; }	// transparent
		}
	}
}


//========================================================================================
// Helper: the chessboard distance transform of the 0/1 difference mask into out (uint8, 0 = a
//   changed pixel, clamped at 255). Each pixel gets the distance to the nearest changed pixel
//   (max(|dx|, |dy|)), which reduces drawing the ring to a threshold test (0 < out <= radius).
//   Two-pass chamfer (forward and backward), 8-neighbour, all costs 1. out is the caller's (w*h).
//========================================================================================
static void KCMDistTransform(const uint8* mask, int32 wt, int32 ht, uint8* out)
{
	if (mask == nil || out == nil || wt <= 0 || ht <= 0)
		return;
	const size_t N = (size_t)wt * ht;
	for (size_t i = 0; i < N; ++i)
		out[i] = mask[i] ? 0 : (uint8)255;

	// Forward pass (top left to bottom right): +1 from the already-processed left, up, up-left and up-right.
	for (int32 y = 0; y < ht; ++y)
	{
		for (int32 x = 0; x < wt; ++x)
		{
			const size_t idx = (size_t)y * wt + x;
			if (out[idx] == 0) continue;
			int32 best = out[idx];
			if (x > 0)                    { int32 v = (int32)out[idx - 1]      + 1; if (v < best) best = v; }
			if (y > 0)                    { int32 v = (int32)out[idx - wt]     + 1; if (v < best) best = v; }
			if (y > 0 && x > 0)           { int32 v = (int32)out[idx - wt - 1] + 1; if (v < best) best = v; }
			if (y > 0 && x < wt - 1)      { int32 v = (int32)out[idx - wt + 1] + 1; if (v < best) best = v; }
			if (best > 255) best = 255;
			out[idx] = (uint8)best;
		}
	}
	// Backward pass (bottom right to top left): +1 from the right, down, down-right and down-left.
	for (int32 y = ht - 1; y >= 0; --y)
	{
		for (int32 x = wt - 1; x >= 0; --x)
		{
			const size_t idx = (size_t)y * wt + x;
			if (out[idx] == 0) continue;
			int32 best = out[idx];
			if (x < wt - 1)               { int32 v = (int32)out[idx + 1]      + 1; if (v < best) best = v; }
			if (y < ht - 1)               { int32 v = (int32)out[idx + wt]     + 1; if (v < best) best = v; }
			if (y < ht - 1 && x > 0)      { int32 v = (int32)out[idx + wt - 1] + 1; if (v < best) best = v; }
			if (y < ht - 1 && x < wt - 1) { int32 v = (int32)out[idx + wt + 1] + 1; if (v < best) best = v; }
			if (best > 255) best = 255;
			out[idx] = (uint8)best;
		}
	}
}


// Testing a pixel against the folio exclusion, in the innermost loop of the comparison.
// It is a two-stage sieve rather than a walk of every rectangle per pixel: the old form compared
// all four edges of every rectangle for each pixel, and at the 144dpi comparison resolution an A4
// page is about two million pixels, times the number of rectangles, on every page.
// Folio rectangles only ever occupy a thin band at the bottom (or top) of the page, so:
//   1. the union bbox of the page's rectangles rejects whole rows and columns cheaply;
//   2. at the top of each row, the rectangles that reach that y are collected once.
// Outside the band -- which is most rows -- the test is one "is the set empty".
// Looking at x within a collected row is KCMXInRowRects, which lives inline in
// KCMDrawEventHandler.h because the book comparison (KCMBookCompare.cpp) needs the identical test
// and used to hold a copy of the same four lines.

ErrorCode KCMDrawEventHandler::MakeEntry(const UIDRef& targetRef, const UIDRef& sourceRef, bool16& changed)
{
	changed = kFalse;
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// Two rasterisations, not three: an earlier version also rasterised the target at 72dpi, and
	// not one of those pixels was ever used, because BuildRing overwrites the whole buffer. The
	// low-resolution dimensions are derived from the high-resolution ones instead.
	// HIGH RESOLUTION, for detecting the difference: target and source at
	// kKCMResolution x kKCMHiResMul. Thin lines and sub-pixel shifts that averaging would erase
	// are counted at full weight, so nothing is missed.
	const PMReal hiRes = kKCMResolution * kKCMHiResMul;
	// The comparison is always four-channel CMYK, rasterised opaque (small CMYK differences are
	// rounded away by a conversion to RGB). The displayed ring is composited separately as ARGB,
	// so the comparison raster does not need an alpha channel (addTransparencyAlpha = kFalse).
	SnapshotUtilsEx* snapTH = new (std::nothrow) SnapshotUtilsEx(targetRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	if (snapTH == nil)
		return kFailure;
	// Rasterise with ANTI-ALIASING OFF (4th argument enableAntiAliasing = kFalse). It removes the
	//   grey halo at edges, and with it the banding that a sub-pixel shift would otherwise register
	//   as a difference. **Target and source must always use the same AA setting** -- with one of
	//   them anti-aliased, every edge in the document is a difference.
	// 2nd argument fullResolutionGraphics = kFalse (the default) is **deliberate**: kTrue triggers
	//   generation of placed images at full resolution, and that DIRTIES THE DOCUMENT. KCM's design
	//   rests on never modifying the model, so the comparison uses the proxies.
	// 3rd argument greekBelowPtSize = 0.0 (greeking disabled) is **deliberate**. Left at the
	//   default, small text is drawn as a grey band with no letterforms; target and source are
	//   greeked alike, so **a change in small text produces no difference at all**
	//   (SnapshotUtilsEx.h:224-225 -- the threshold is "point size multiplied by the scaling", and
	//   the header does not say what that scaling is, so whether it kills everything under 7pt or
	//   only under 3.5pt cannot be settled). KCM compares pixels, so it passes 0.0 and always gets
	//   letterforms; the price is a slightly slower rasterisation on pages full of small text.
	//   The official sample (snapshot/SnapTracker.cpp:318) leaves the default, but its purpose is
	//   a visual snapshot. **Target and source must use the same value.**
	// 8th argument bDrawNonPrintingObjects = kFalse is **deliberate** (the default is kTrue). Left
	//   at the default, moving a page item marked non-printing -- a working note, an annotation, a
	//   memo beside the crop marks -- makes the page count as changed, so **a mark appears although
	//   the printed result is identical**. KCM's marks mean "the printed result changed"
	//   (SnapshotUtilsEx.h:241-242).
	//   @warning as that header states, it does NOT affect a LAYER's non-printing setting.
	//   @warning arguments 5-7 are the defaults spelled out, only because the 8th cannot be reached
	//     otherwise: transparencyQuality = kXPHigh (do not lower it, or changes to shadows, feathers
	//     and blends stop being detected), abortCheck = nil (cancelling is checked at page
	//     boundaries, in KCMCore.cpp), pVPAttrMap = nil.
	//   **Target and source must use the same value.**
	ErrorCode drewTH;
	{
		KCMRasterizingGuard rg;	// a re-entrant draw during this Draw must not paint marks into our own raster
		drewTH = snapTH->Draw(IShape::kPreviewMode, kFalse, 0.0, kFalse,
		                      SnapshotUtils::kXPHigh, nil, nil, kFalse);
	}
	AGMImageAccessor* accTH = (drewTH == kSuccess) ? snapTH->CreateAGMImageAccessor() : nil;

	SnapshotUtilsEx* snapSH = new (std::nothrow) SnapshotUtilsEx(sourceRef, 1.0, 1.0, hiRes, hiRes, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	if (snapSH == nil)
	{
		if (accTH) delete accTH;
		delete snapTH;
		return kFailure;
	}
	ErrorCode drewSH;
	{
		KCMRasterizingGuard rg;
		// As above: greeking off, AA off, non-printing objects not drawn -- both sides identical.
		drewSH = snapSH->Draw(IShape::kPreviewMode, kFalse, 0.0, kFalse,
		                      SnapshotUtils::kXPHigh, nil, nil, kFalse);
	}
	AGMImageAccessor* accSH = (drewSH == kSuccess) ? snapSH->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (accTH != nil && accSH != nil)
	{
		// The high-resolution (comparison) dimensions and buffers.
		Int32Rect bth = accTH->GetBounds();
		Int32Rect bsh = accSH->GetBounds();
		const int32 wth = bth.right - bth.left, hth = bth.bottom - bth.top;
		const int32 wsh = bsh.right - bsh.left, hsh = bsh.bottom - bsh.top;
		const int32 rbTH = (int32)accTH->GetRowBytes();
		const int32 rbSH = (int32)accSH->GetRowBytes();
		const int32 bppH = (int32)accTH->GetBitsPerPixel() / 8;
		const uint8* ptH = accTH->GetBaseAddr();
		const uint8* psH = accSH->GetBaseAddr();

		// The low-resolution (stored, displayed) dimensions are derived from the high-resolution
		// ones. buf is our own ARGB buffer, with no row padding.
		int32 wl = ::ToInt32(::Round(PMReal(wth) / kKCMHiResMul));
		int32 hl = ::ToInt32(::Round(PMReal(hth) / kKCMHiResMul));
		if (wl < 1) wl = 1;
		if (hl < 1) hl = 1;
		const int32 bppL = 4;				// the displayed ring is always our own ARGB (4), independent of the comparison raster's channel count
		const int32 rbL = wl * bppL;		// our own buffer: no row padding

		if (ptH != nil && psH != nil &&
			wth == wsh && hth == hsh && rbTH == rbSH && rbTH > 0 &&
			bppH >= 4 && wl > 0 && hl > 0)
		{
			// **Every allocation in this function is new (std::nothrow)**: MSVC's ordinary new
			//   throws instead of returning nil, so without nothrow the nil tests below would do
			//   nothing and an out-of-memory would send an exception through an event boundary.
			//   With nothrow, running out of memory only means this page gets no mark.
			const size_t N = (size_t)wl * hl;
			uint8*  M     = new (std::nothrow) uint8[N];	// the stored low-resolution mask: the pooling result
			uint16* cntHi = new (std::nothrow) uint16[N];	// per low-resolution cell, how many high-resolution pixels changed (pooling scratch)
			if (M != nil && cntHi != nil)
			{
				memset(cntHi, 0, N * sizeof(uint16));

				// The folio exclusion. While the toggle is on, the rectangles of the frames holding
				// a "Current Page Number" marker are collected for BOTH pages (in points with the
				// page's top left as the origin), converted to the comparison resolution's pixels,
				// and the pixels inside them are skipped by the difference scan below -- otherwise
				// an identical design still differs because the two versions number differently.
				// Target and source are assumed to be the same page size, so both sets of
				// rectangles apply to the same (x, y) space.
				// The conversion to Int32Rect happens here: KCMPageNumberMarker.h deals in PMRect
				// only and does not depend on Int32Rect.
				std::vector<Int32Rect> excludeRects;
				if (KCMGetIgnorePageNumberMarker())
				{
					// A comparison always measures afresh (refresh=kTrue), which also updates the
					// cache -- so the green wash showing the excluded area draws exactly the
					// rectangles THIS comparison used.
					// @warning two references are held at once, so the cache must be a container
					//   whose existing elements are not invalidated by an insertion. It is a
					//   std::map (KCMPageNumberMarker.cpp's KCMMarkerRectMap), which guarantees
					//   that. **Switching it to unordered_map or vector would leave the first
					//   reference (tRects) dangling after the second fetch** -- change it and this
					//   has to copy by value or use one at a time.
					const std::vector<PMRect>& tRects = KCMGetPageNumberMarkerRects(targetRef, kTrue);
					const std::vector<PMRect>& sRects = KCMGetPageNumberMarkerRects(sourceRef, kTrue);
					const PMReal pxScale = hiRes / PMReal(72.0);	// points -> comparison-resolution pixels
					for (int pass = 0; pass < 2; ++pass)		// 0 = target, 1 = source (both into the same (x, y) space)
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

				// COMPARE AT HIGH RESOLUTION, SCATTER INTO LOW-RESOLUTION CELLS.
				// Each high-resolution pixel is tested (the largest raw per-channel difference) and,
				// if it changed, the counter of its low-resolution cell is incremented. The cell
				// mapping is by dimension ratio, so the two resolutions need not be integer multiples.
				// CMYK: four channels from offset 0. A pixel changed when the largest channel
				// difference exceeds the threshold (kKCMCmykThr).
				const int  nch       = 4;
				const int32 colorOffH = 0;
				const int  thr        = kKCMCmykThr;

				// Stage 1 of the exclusion sieve: the union bbox of the rectangles. After this, a
				// row outside its vertical range costs zero tests, and an x outside its horizontal
				// range costs two comparisons.
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
				std::vector<const Int32Rect*> rowRects;	// only the rectangles reaching this row (allocated outside the loop and reused)
				rowRects.reserve(excludeRects.size());
				for (int32 y = 0; y < hth; ++y)
				{
					const uint8* rowT = ptH + (size_t)y * rbTH;
					const uint8* rowS = psH + (size_t)y * rbTH;
					int32 yl = (int32)((int64)y * hl / hth);
					if (yl >= hl) yl = hl - 1;
					uint16* cntRow = cntHi + (size_t)yl * wl;

					// Stage 2: collect the rectangles reaching this row (outside the bbox's
					// vertical range it stays empty, and every test below is skipped).
					rowRects.clear();
					if (!excludeRects.empty() && y >= exTop && y < exBottom)
					{
						for (size_t mi = 0; mi < excludeRects.size(); ++mi)
							if (y >= excludeRects[mi].top && y < excludeRects[mi].bottom)
								rowRects.push_back(&excludeRects[mi]);
					}
					const bool16 rowHasExclude = rowRects.empty() ? kFalse : kTrue;

					for (int32 x = 0; x < wth; ++x)
					{
						if (rowHasExclude && x >= exLeft && x < exRight && KCMXInRowRects(x, rowRects))
							continue;	// inside the folio exclusion: not a difference
						const uint8* px = rowT + (size_t)x * bppH + colorOffH;
						const uint8* sx = rowS + (size_t)x * bppH + colorOffH;
						int cm = 0;
						for (int c = 0; c < nch; ++c)
						{
							const int d = (px[c] > sx[c]) ? px[c] - sx[c] : sx[c] - px[c];
							if (d > cm) cm = d;
						}
						if (cm > thr)
						{
							int32 xl = (int32)((int64)x * wl / wth);
							if (xl >= wl) xl = wl - 1;
							if (cntRow[xl] < 0xFFFF) ++cntRow[xl];
						}
					}
				}

				// MAX POOLING: a stored cell is 1 when at least min-count high-resolution pixels
				// inside it changed. At min = 1 nothing is missed; raising it survives edge noise
				// better.
				size_t diffCount = 0;
				for (size_t i = 0; i < N; ++i)
				{
					uint8 m = (cntHi[i] >= (uint16)kKCMPoolMinCount) ? 1 : 0;
					M[i] = m;
					if (m) ++diffCount;
				}
				delete[] cntHi; cntHi = nil;

				if (diffCount == 0)
				{
					// Nothing changed: no entry is created.
					delete[] M;
					status = kSuccess;	// success, with changed = false
				}
				else
				{
					// Build our own AGMImageRecord pointing at buf and detach from the snapshot.
					// The raster's pixels are not copied, because BuildRing writes every pixel of
					// buf below. Neither the SnapshotUtilsEx nor the accessor is kept, and
					// GetAGMImageRecord is not called -- which removes the crash-on-destroy that
					// holding an accessor causes.
					KCMOverlayEntry* e = new (std::nothrow) KCMOverlayEntry();
					if (e == nil)
					{
						// Out of memory: release what was allocated, destroy the snapshots, and give
						// up on this page (the same shape as MakeOrigImage's allocation failure).
						delete[] M;
						if (accSH)  delete accSH;
						if (snapSH) delete snapSH;
						if (accTH)  delete accTH;
						if (snapTH) delete snapTH;
						return kFailure;
					}
					e->w = wl;  e->h = hl;  e->rowBytes = rbL;  e->bpp = bppL;
					e->lastRadius = kKCMBaseRadius;
					// For Prev/Next's "how much of this page changed". The denominator is w * h, so
					// only the numerator is stored. This branch is only reached with diffCount != 0,
					// so it is always at least 1.
					e->changedCells = (int32)diffCount;
					// Build the distance transform from the mask once and keep it; every later
					// BuildRing works from it alone. The mask is freed as soon as dist exists, so
					// the resident memory does not grow (dist replaces mask).
					e->dist = new (std::nothrow) uint8[N];
					if (e->dist != nil)
						KCMDistTransform(M, wl, hl, e->dist);
					delete[] M;

					// Draw the first ring (at the base radius) straight into buf.
					e->buf = (e->dist != nil) ? new (std::nothrow) uint8[(size_t)rbL * hl] : nil;

					// **If either dist or buf could not be allocated, this page gets no mark at all.**
					//   Falling back ("clear buf to transparent when dist is missing", "let the
					//   drawing skip it when buf is missing") produces a state where **the entry is
					//   in sEntries but nothing is on screen**: changedCells is non-zero, so
					//   Prev/Next jumps to the page as changed and no frame is there -- it looks
					//   broken. Giving the page up on out-of-memory matches e == nil above and
					//   MakeOrigImage.
					if (e->buf == nil)
					{
						delete e;
						if (accSH)  delete accSH;
						if (snapSH) delete snapSH;
						if (accTH)  delete accTH;
						if (snapTH) delete snapTH;
						return kFailure;
					}
					BuildRing(e->buf, rbL, bppL, wl, hl, e->dist, kKCMBaseRadius);
					// **The int16 casts here cannot overflow.** AGMImageRecord.bounds is int16
					//   (up to 32,767) and wl/hl are pixel counts at the STORED resolution,
					//   kKCMResolution = 36dpi. The largest page InDesign allows is
					//   **216.000 inch (15,551.996pt, 5486.4mm)**, so at most **216 x 36 = 7,776 px**
					//   -- 24% of the ceiling.
					//   @warning MakeOrigImage below DOES have an explicit `b.right <= 32767` guard,
					//     and the asymmetry is correct: that one works at kKCMOrigResolution = 72dpi
					//     and the peek passes a dpi derived from the current zoom, so
					//     **216 inch x 300dpi = 64,800 really does overflow**.
					//   The comparison resolution (144dpi) reaches 31,104 px in wth/hth, but those
					//     stay int32 and are never narrowed to int16.
					e->rec.bounds.xMin = 0;             e->rec.bounds.yMin = 0;
					e->rec.bounds.xMax = (int16)wl;     e->rec.bounds.yMax = (int16)hl;
					e->rec.baseAddr     = e->buf;
					e->rec.byteWidth    = rbL;
					// ARGB (alpha first). Without the HasAlpha flag the transparent pixels are drawn
					// as opaque white. ARGB is the default order, so no SwapAlpha is needed (RGBA
					// would need | kColorSpaceSwapAlpha).
					e->rec.colorSpace   = (int16)(kRGBColorSpace | kColorSpaceHasAlpha);
					e->rec.bitsPerPixel = (int16)(bppL * 8);
					e->rec.decodeArray  = nil;
					e->rec.colorTab.numColors = 0;  e->rec.colorTab.theColors = nil;

					// Replace any existing entry.
					// **The replacement deletes the old entry**, so the lock is taken to keep a
					//   drawing background thread from reading that pointer (the same reason DropAll
					//   takes it).
					//   @warning the lock covers only the interval that touches the collections.
					//     The rasterisation and the ring generation above are outside it.
					UID key = targetRef.GetUID();
					{
						KCMMarkStateLock lock(KCMMarkStateMutex());
						std::map<UID, KCMOverlayEntry*>::iterator old = sEntries.find(key);
						if (old != sEntries.end()) { delete old->second; sEntries.erase(old); }
						sEntries[key] = e;

						// The Source-side mapping ("Always Show Marks on Source") is recorded here,
						// in the same place the entry is registered, so any route that reaches
						// MakeEntry keeps the two in step. It is cleaned up by DropAll, together
						// with the entries.
						// sSrcPageToTarget is **inserted into by the main thread and searched by the
						// background thread while it draws** -- the condition KCMThreadSafety.h:76-81
						// says to guard, and the same one sEntries has.
						//
						// @warning **the lock is protecting sSrcPageToTarget (a std::map), not
						//   sSrcDB.** Assigning one pointer needs no lock: the reader sees the old
						//   value or the new one, and neither breaks the drawing (old = the Source
						//   frames do not appear, new = they do). sSrcDB is inside this scope only
						//   because it is written in the same place. Reading this as "sSrcDB is a
						//   variable that needs the lock" has already led to KCMCore.cpp's
						//   `sSrcDB = sourceDB;` being misdiagnosed as a missing lock once.
						sSrcDB = sourceRef.GetDataBase();
						sSrcPageToTarget[sourceRef.GetUID()] = key;
					}

					// dist and buf belong to the entry now (the mask was freed once dist existed).
					// The snapshots are destroyed by the clean-up below.
					changed = kTrue;
					status = kSuccess;
				}
			}
			else
			{
				if (M)     delete[] M;
				if (cntHi) delete[] cntHi;
			}
		}
	}

	// Clean-up: destroy both snapshots and accessors.
	if (accSH)  delete accSH;
	if (snapSH) delete snapSH;
	if (accTH)  delete accTH;
	if (snapTH) delete snapTH;
	return status;
}


ErrorCode KCMDrawEventHandler::MakeOrigImage(const UIDRef& targetRef, const UIDRef& sourceRef, const PMReal& resolution)
{
	if (targetRef.GetDataBase() == nil || targetRef.GetUID() == kInvalidUID)
		return kFailure;
	if (sourceRef.GetDataBase() == nil || sourceRef.GetUID() == kInvalidUID)
		return kFailure;

	// Rasterise the source (the older side) opaquely at `resolution` dpi
	// (addTransparencyAlpha = kFalse draws the page opaque, which is what an overlay wants).
	// Only one offscreen exists at a time: the pixels are copied into our own buffer and it is
	// destroyed immediately below.
	// **The arguments are left at their defaults (greeking 7.0, AA on, non-printing objects drawn),
	//   deliberately UNLIKE the comparison raster.** The comparison asks "did the printed result
	//   change" and therefore does not draw non-printing objects; this image is meant to **show the
	//   older version as it looked**, and something that was visible on screen going missing is not
	//   a reproduction of "how it was".
	//   @warning so a non-printing object that moved can differ in the peek image while no mark
	//     appears. The asymmetry is intended; matching them means setting both to kFalse.
	SnapshotUtilsEx* snap = new (std::nothrow) SnapshotUtilsEx(sourceRef, 1.0, 1.0, resolution, resolution, 0.0, SnapshotUtilsEx::kCsRGB, kFalse);
	if (snap == nil)
		return kFailure;	// nothrow: out of memory only costs this page its picture (as in MakeEntry)
	ErrorCode drew;
	{
		KCMRasterizingGuard rg;	// a re-entrant draw during this Draw must not paint marks into our own raster
		drew = snap->Draw(IShape::kPreviewMode);
	}
	AGMImageAccessor* acc = (drew == kSuccess) ? snap->CreateAGMImageAccessor() : nil;

	ErrorCode status = kFailure;
	if (acc != nil)
	{
		Int32Rect b = acc->GetBounds();
		const int32 w = b.right - b.left, h = b.bottom - b.top;
		const int32 rb = (int32)acc->GetRowBytes();
		const int32 bpp = (int32)acc->GetBitsPerPixel() / 8;
		const uint8* p = acc->GetBaseAddr();
		// AGMImageRecord.bounds is int16, so an enormous page at 300dpi (over 32,767px, about
		// 109 inch) is rejected rather than wrapping.
		// **This guard really is reachable**: the largest page InDesign allows is
		//   **216.000 inch (15,551.996pt, 5486.4mm)**, which at 300dpi is 64,800 px -- nearly twice
		//   the ceiling. MakeEntry has no such guard and does not need one (36dpi, 7,776 px at most;
		//   see the comment at its e->rec.bounds).
		if (p != nil && w > 0 && h > 0 && rb > 0 && bpp >= 3 && b.right <= 32767 && b.bottom <= 32767)
		{
			// nothrow: a 300dpi large-format page (about 140MB of buffer for A2) is the likeliest
			// place to actually run out of memory. The early return below frees the partial state.
			KCMOrigImage* o = new (std::nothrow) KCMOrigImage();
			uint8* obuf = (o != nil) ? new (std::nothrow) uint8[(size_t)rb * h] : nil;
			if (o == nil || obuf == nil)
			{
				// allocation failed: free any partial state and bail (same safety as MakeEntry)
				if (obuf) delete[] obuf;
				if (o)    delete o;
				if (acc)  delete acc;
				if (snap) delete snap;
				return kFailure;
			}
			o->buf = obuf;
			o->w = w;  o->h = h;  o->rowBytes = rb;  o->bpp = bpp;
			memcpy(o->buf, p, (size_t)rb * h);
			// Guarantee opacity: with ARGB (alpha first), set every alpha to 255 so nothing shows
			// through the overlay.
			// A grid of about 8x8 samples is checked first, and if they are all already 255 the
			//   O(W*H) pass is skipped entirely -- which it is whenever the raster came back opaque
			//   (addTransparencyAlpha = kFalse). One non-255 sample and every pixel is set, as before
			//   (self-correcting: either path is right).
			if (bpp >= 4)
			{
				bool16 alreadyOpaque = kTrue;
				const int32 sy = (h > 8) ? h / 8 : 1;
				const int32 sx = (w > 8) ? w / 8 : 1;
				for (int32 y = 0; y < h && alreadyOpaque; y += sy)
				{
					const uint8* row = o->buf + (size_t)y * rb;
					for (int32 x = 0; x < w; x += sx)
						if (row[(size_t)x * bpp] != 255) { alreadyOpaque = kFalse; break; }
				}
				if (!alreadyOpaque)
				{
					for (int32 y = 0; y < h; ++y)
					{
						uint8* row = o->buf + (size_t)y * rb;
						for (int32 x = 0; x < w; ++x)
							row[(size_t)x * bpp] = 255;
					}
				}
			}
			o->rec.bounds.xMin = (int16)b.left;   o->rec.bounds.yMin = (int16)b.top;
			o->rec.bounds.xMax = (int16)b.right;  o->rec.bounds.yMax = (int16)b.bottom;
			o->rec.baseAddr     = o->buf;
			o->rec.byteWidth    = rb;
			o->rec.colorSpace   = (int16)((bpp >= 4) ? (kRGBColorSpace | kColorSpaceHasAlpha) : kRGBColorSpace);
			o->rec.bitsPerPixel = (int16)acc->GetBitsPerPixel();
			o->rec.decodeArray  = nil;
			o->rec.colorTab.numColors = 0;  o->rec.colorTab.theColors = nil;

			// Replace any existing picture.
			UID key = targetRef.GetUID();
			std::map<UID, KCMOrigImage*>::iterator old = sOrigImages.find(key);
			if (old != sOrigImages.end()) { delete old->second; sOrigImages.erase(old); }
			sOrigImages[key] = o;
			status = kSuccess;
		}
	}

	if (acc)  delete acc;
	if (snap) delete snap;
	return status;
}


//========================================================================================
// Drawing the ring for print and PDF. On screen an image() blit is enough (it honours the pixels'
// alpha), but the print flattener does NOT honour a blitted image's partial alpha and the frame
// comes out opaque. So, in the same way the transparencyeffect sample does it, the ring's SHAPE
// becomes a grey ALPHA SERVER and a solid vector fill is drawn through it with setopacity, which
// the transparency engine does honour.
// The red and the cyan pixels are filled in two passes, each through its own grey mask.
// The caller has already applied the translate and scale, so user space is image pixels.
//   e->buf is ARGB (alpha first, then R, G, B).
//========================================================================================


//========================================================================================
// **The colour a mark is drawn in depends on the output: print and PDF are painted in CMYK.**
//
//   WHY: **PDF/X-1a does not allow RGB.** Left in RGB, exporting to [PDF/X-1a:2001 (Japan)]
//     produces this warning in the background task and **a valid PDF that is NOT PDF/X-1a
//     compliant**: "One of the placed images cannot display colours as CMYK colours. Non-CMYK
//     colours do not comply with the PDF/X-1a standard."
//     For print submission that is a real defect: a file believed to be X-1a is not.
//   It also removed an inconsistency: **KCM compares in CMYK** (the core of its design) while the
//     marks alone were RGB.
//   The screen stays RGB. (The ring itself is a blit of an ARGB image and never comes through here
//     at all.)
//   The conversion is the standard formula (k = 1-max(r,g,b), c = (max-r)/max ...). KCM's colours
//     are near-primary, so they map cleanly: red (255,0,0) -> C0 M100 Y100 K0, cyan (0,255,255) ->
//     C100 M0 Y0 K0, green (0,200,0) -> C100 M0 Y100 K22, white -> all zero, black -> K100.
//========================================================================================
// Not static, because the Story mode's marker shares it (declared in KCMDrawEventHandler.h).
// "Screen is RGB, print is CMYK" written in two places would drift ([[one-question-one-place]]).
void KCMSetOutputColor(IGraphicsPort* gPort, uint8 r, uint8 g, uint8 b, bool16 useCMYK)
{
	if (!useCMYK)
	{
		gPort->setrgbcolor(r / PMReal(255.0), g / PMReal(255.0), b / PMReal(255.0));
		return;
	}
	const PMReal rf = r / PMReal(255.0), gf = g / PMReal(255.0), bf = b / PMReal(255.0);
	PMReal mx = rf;  if (gf > mx) mx = gf;  if (bf > mx) mx = bf;
	if (mx <= PMReal(0.0001))
	{
		gPort->setcmykcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0), PMReal(1.0));	// black
		return;
	}
	gPort->setcmykcolor((mx - rf) / mx, (mx - gf) / mx, (mx - bf) / mx, PMReal(1.0) - mx);
}


static void KCMDrawRingForPrint(IGraphicsPort* gPort, IViewPortAttributes* vpAttr, IDataBase* db,
	KCMOverlayEntry* e)
{
	if (gPort == nil || e == nil || e->buf == nil || e->w <= 0 || e->h <= 0 || e->bpp < 4)
		return;
	// The transparency utilities (used to create and release the alpha server). They are always
	// present in a running application, but as the transparencyeffect sample does, do nothing if
	// they cannot be obtained. This one instance is reused below.
	Utils<IXPUtils> xpUtils;
	if (!xpUtils)
		return;

	//========================================================================================
	// **The extra initialisation that only a PDF export needs.**
	//
	//   Print and PDF export treat transparency differently:
	//     - **print** ... always goes through the transparency FLATTENER (a printer does not
	//       understand transparency, so InDesign resolves the overlaps into opaque shapes and
	//       rasters first) -- the caller has nothing to do;
	//     - **PDF export** ... maps onto the transparency PDF itself provides (/Group, /SMask, /ca)
	//       -- so **without opening that structure (a transparency group) there is nowhere for
	//       something semi-transparent to be written.**
	//
	//   The worked example is `transparencyeffect/TranFxAdornment.cpp:392-407`, where Adobe writes
	//     "**Extra initialisation is required when drawing to a PDF port.**" and wraps the fill in
	//     `starttransparencygroup(bounds, xpManager->GetDocumentBlendingSpace(), ...)` only when
	//     `isPDFExport && !isPDFFlattenerExport`.
	//
	//   Measured: exported at [High Quality Print] (PDF 1.4), the PDF gained **/Group=14, /SMask=6,
	//     /ca=6 and 8,623 bytes**, and the frames were visibly semi-transparent. **The same works
	//     at PDF 1.3**, confirmed on screen.
	//     @warning a byte-size analysis is not evidence about a picture: reasoning from "only 221
	//       bytes more than without marks, so it must be a solid fill" contradicted what was
	//       actually on screen. **Do not judge the image from the file size.**
	//
	//   **Only THIS function needs the transparency group** -- the one that paints through an alpha
	//     server. The other marks that reach print and PDF from the same draw -- the registered and
	//     overflow "/" (KCMDrawPageDiagonal), the tick (KCMDrawPageCheck) and the page border
	//     (KCMDrawPageBorder) -- use `setopacity` plus a vector fill and open no group, **and that
	//     is correct**:
	//       - at PDF **1.4**, a page carrying only an overflow "/" exported at 25% and at 75%
	//         carries **`/ca 0.25` `/CA 0.25`** (and 0.75) straight through -- semi-transparent with
	//         no group at all;
	//       - at PDF **1.3** the same page comes out **`/ca 1.0`**, i.e. opaque (1.3 cannot express
	//         it). @warning it does NOT turn into a solid block -- the "/" keeps its shape.
	//         **A solid block is a symptom peculiar to the alpha server** and never happens to the
	//         "/", the tick or the border.
	//     So the dividing line is **whether a mask (an alpha server) is used**, not whether a
	//       transparency group was opened.
	//========================================================================================
	bool16 needTransparencyGroup = kFalse;
	if (vpAttr != nil)
	{
		const bool32 isPDFFlattenerExport = vpAttr->GetAttr(kPDFIsFlattenerTargetVPAttr, kFalse);
		const bool32 isPDFExport          = vpAttr->GetAttr(kPDFExportVPAttr, kFalse);
		needTransparencyGroup = (isPDFExport && !isPDFFlattenerExport) ? kTrue : kFalse;
	}
	// The transparency manager answers for **the document's** blending space, so it needs the db
	// (nil cannot be resolved -- IXPUtils.h:72-73). It is not queried unless this is a PDF export.
	InterfacePtr<IXPManager> xpManager(needTransparencyGroup && db != nil
		? xpUtils->QueryXPManager(db) : nil);
	if (xpManager == nil)
		needTransparencyGroup = kFalse;	// cannot be resolved: draw as the print route does
	const int32 w = e->w, h = e->h, rb = e->rowBytes, bpp = e->bpp;
	const size_t N = (size_t)w * h;

	// Build two grey masks out of e->buf (ARGB): red ring pixels = 255, cyan ring pixels = 255.
	// **The ring image is single-coloured now** (the panel's "Mark colour" chooses red or cyan);
	// it once mixed both, when the colour adapted to reddish ground per pixel. **Splitting into two
	// masks still works exactly right**: only the chosen colour's mask has anything in it, and the
	// other is skipped by the `if (!passes[p].any) continue;` below. That is why the print side
	// needed no change when the colour became a choice.
	uint8* maskR = new (std::nothrow) uint8[N];	// nothrow, so the nil test right below means something (failure just means no frame)
	uint8* maskB = new (std::nothrow) uint8[N];
	if (maskR == nil || maskB == nil) { if (maskR) delete[] maskR; if (maskB) delete[] maskB; return; }
	// Count whether each mask has **any** pixel at all; the reason is at the continue below.
	bool16 anyR = kFalse, anyB = kFalse;
	for (int32 y = 0; y < h; ++y)
	{
		const uint8* row = e->buf + (size_t)y * rb;
		for (int32 x = 0; x < w; ++x)
		{
			const uint8* px  = row + (size_t)x * bpp;	// [alpha, R, G, B]
			const size_t idx = (size_t)y * w + x;
			if (px[0] != 0)								// a ring pixel (alpha != 0)
			{
				const bool16 blue = (px[3] > px[1]);	// B > R = cyan (the panel's cyan is selected)
				maskR[idx] = blue ? 0 : 255;
				maskB[idx] = blue ? 255 : 0;
				if (blue) anyB = kTrue; else anyR = kTrue;
			}
			else { maskR[idx] = 0; maskB[idx] = 0; }
		}
	}

	// The ring's opacity is the panel's 25%/75% choice -- the same SelectedMarkOpacity the screen uses.
	const PMReal op = KCMDrawEventHandler::SelectedMarkOpacity();
	// A known limitation: on a page that contains transparency, the frames drawn here are
	// rasterised by the flattener and the CMYK conversion sinks the colour slightly (the frames look
	// denser on pages with transparent images). Specifying the colour in CMYK does not fix it (the
	// cause is drawing through the transparency machinery, not the colour values), and making it an
	// opaque vector would lose the 25% see-through, so it stands as it is.
	// The second pass paints **cyan**, matching the screen: BuildRing paints kKCMRingAlt* =
	// cyan (0,255,255), and this used to paint pure blue (0,0,255) -- screen and print disagreed.
	// The test (`B > R`) is true for both, so it never showed up in behaviour.
	struct PassDef { uint8* buf; uint8 r, g, b; bool16 any; };
	PassDef passes[2] = {
		{ maskR, kKCMRingR,    kKCMRingG,    kKCMRingB,    anyR },	// red
		{ maskB, kKCMRingAltR, kKCMRingAltG, kKCMRingAltB, anyB }		// cyan
	};

	for (int p = 0; p < 2; ++p)
	{
		// **Skip an empty mask -- this is what caused "the page comes out solid blue".**
		//   Handing an all-zero mask to the alpha server means **the mask does not take, and the
		//   rectpath below is filled as it stands** -- a solid block in that pass's colour.
		//   Since the ring image became single-coloured, **one of the two masks is empty every
		//   time**, so this guard is not an edge case but the normal path.
		//   The vector implementation that preceded the alpha server had the same guard
		//   (`if (anyRun) fill();`) and **only the alpha server version lacked it** -- one of two
		//   implementations guarding something ([[verify-claims-in-comments]]: "N places guard it"
		//   means opening all N).
		//   It also removes a pointless fill on the print route, which comes through the same code.
		if (!passes[p].any)
			continue;

		// A grey (8bpp, no alpha) AGMImageRecord pointing at the mask. An alpha server requires a
		// grey colour space.
		AGMImageRecord mrec;
		mrec.bounds.xMin = 0;            mrec.bounds.yMin = 0;
		mrec.bounds.xMax = (int16)w;     mrec.bounds.yMax = (int16)h;
		mrec.baseAddr     = passes[p].buf;
		mrec.byteWidth    = w;								// 1 byte per pixel, no row padding
		mrec.colorSpace   = (int16)kGrayColorSpace;
		mrec.bitsPerPixel = 8;
		mrec.decodeArray  = nil;
		mrec.colorTab.numColors = 0;     mrec.colorTab.theColors = nil;

		PMMatrix idm;										// identity: user space is image pixels, so pixel (x,y) -> user (x,y)
		AGMPaint* alphaPaint = xpUtils->CreateImagePaintServer(&mrec, &idm, 0, nil);
		if (alphaPaint != nil)
		{
			AutoGSave ag(gPort);
			gPort->SetAlphaServer(alphaPaint, kTrue, PMMatrix());	// the shape is the ring pixels, per pixel

			// For a PDF export only, open a transparency group in the document's blending space.
			//   The order is the official one: **SetAlphaServer -> starttransparencygroup -> fill ->
			//   endtransparencygroup** (TranFxAdornment.cpp:390-425).
			//   `GetDocumentBlendingSpace` **takes the port**: IXPManager.h:51-55 says "in general
			//   **the port is needed** so we can inspect the proofing configuration", so gPort is
			//   passed (the sample omits it; follow the header).
			bool16 startedGroup = kFalse;
			if (needTransparencyGroup)
			{
				AGMColorSpace* blendingSpace = xpManager->GetDocumentBlendingSpace(gPort);
				gPort->starttransparencygroup(
					PMRect(PMReal(0.0), PMReal(0.0), PMReal(w), PMReal(h)),
					blendingSpace, kFalse /*isolation*/, kFalse /*knockout*/);
				startedGroup = kTrue;
				xpManager->ReleaseBlendingSpace(blendingSpace);
			}

			gPort->setopacity(op, kFalse);							// semi-transparent (the transparency engine honours it)
			// Painted in CMYK, this function being for print and PDF only (PDF/X-1a does not allow
			//   RGB -- see the head of KCMSetOutputColor).
			//   @warning **this function may only be reached where the flattener will run** (the
			//     caller, KCMDrawEntryOnPage, decides). On a page with no transparency at all the
			//     alpha server's mask is never resolved and the result is **a solid block**.
			KCMSetOutputColor(gPort, passes[p].r, passes[p].g, passes[p].b, kTrue /*CMYK*/);
			gPort->newpath();
			gPort->rectpath(PMReal(0.0), PMReal(0.0), PMReal(w), PMReal(h));	// user space is image pixels (the caller translated and scaled)
			gPort->fill();
			if (startedGroup)
				gPort->endtransparencygroup();
			xpUtils->ReleasePaintServer(alphaPaint);
		}
	}

	delete[] maskR;
	delete[] maskB;
}


// KCMDrawEntryOnPage's drawing mode. The thickness rule and the drawing method differ by context:
//   Screen: the radius adapts to the zoom (to about kKCMRingTargetPx on screen), drawn with an
//           image() blit at the chosen opacity (25%/75%)
//   Print:  the radius is the one 100% display would give (sxr = 1.0), drawn through an alpha
//           server with a vector fill (the flattener does not honour a blit's partial alpha).
//           The opacity is taken inside KCMDrawRingForPrint.
//   (There is no separate mode for the Pages panel's thumbnails: those are generated with no view,
//   in kPreviewMode, and the thumbnail branches of the loops below draw a border and a "/" instead
//   of the ring image.)
enum { kKCMDrawModeScreen = 0, kKCMDrawModePrint = 1 };

//========================================================================================
// Drawing one page's ring. It fits e's ring image to db/pageUID's page rectangle, recomputing the
//   ring thickness (BuildRing) as needed. Called for the Target side and for the Source side
//   ("Always Show Marks on Source"): the Source side lays the Target's ring image over the Source
//   page, which is correct because the comparison pairs flattened page numbers, so the position
//   and shape are the same (a different page size is stretched to fit).
//   screenOpacity is used by the Screen mode's blit only; Print takes SelectedMarkOpacity inside
//   KCMDrawRingForPrint.
//   With Target and Source shown at different zooms, e->lastRadius ping-pongs and BuildRing runs
//   again each way; the ring image is stored at 36dpi and the buffer is small, so it does not
//   matter in practice.
//========================================================================================
static void KCMDrawEntryOnPage(IGraphicsPort* gPort, IViewPortAttributes* vpAttr,
	KCMOverlayEntry* e, IDataBase* db, UID pageUID,
	const PMReal& sxr, int32 drawMode, const PMReal& screenOpacity)
{
	if (e == nil || e->buf == nil)
		return;

	const int32 iw = e->w, ih = e->h;
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (iw <= 0 || ih <= 0 || pageGeo == nil)
		return;

	// COORDINATES: the drawing port is in spread coordinates, so the page's box is taken in spread
	// coordinates and the image is fitted to it.
	// The official Facade replaced a hand-built GetPathBoundingBox + ::InnerToSpreadMatrix +
	//   Transform. Worked example: snapshot/SnapTracker.cpp:621. (Line :616 of the same function
	//   takes the same page in PasteboardCoordinates -- **calling it twice with different coordinate
	//   systems is the official shape**. IGeometryFacade.h:209 lists only Pasteboard, Parent and
	//   Inner, which is an omission: the product's CPageItemAdaptiveTransform.cpp:197,362 and the
	//   public lib's CPathCreationTracker.cpp:300 call it with SpreadCoordinates.)
	//   @warning keep the IGeometry Query and its nil test -- whether this UID has geometry at all
	//     is not the Facade's guarantee (the worked example does the same in the same order).
	//     Pass UIDRef(db, pageUID) so the Facade does not cost an extra Query.
	//   @warning pass Geometry::PathBounds(). The worked example uses OuterStrokeBounds, but
	//     PathBounds is what matches GetPathBoundingBox.
	PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// RING THICKNESS: work out the dilation radius (in image pixels) for this mode and redraw if it
	// differs from last time.
	if (e->dist != nil)
	{
		int32 R = -1;	// -1 = this mode cannot decide a radius; draw with the buffer as it is
		if (sxr > 0)
		{
			// Screen and print: derive the radius that gives about kKCMRingTargetPx on screen, from
			// this page's real size and the current zoom (print fixes sxr at 1.0). Zoomed in, it
			// sticks at the floor (2) and stops being recomputed.
			PMReal denom = (pr.Width() / PMReal(iw)) * sxr;		// screen px per image px
			if (denom > PMReal(0.0001))
			{
				R = ::ToInt32(::Round(kKCMRingTargetPx / denom));
				if (R < 2) R = 2;								// at least 2px (4px after quantisation)
				if (R > 200) R = 200;							// and a ceiling on the dilation
				// Quantised in steps of 4px rather than 2, which roughly halves how often R changes
				// while zooming (and so how often BuildRing runs). The cost is slightly coarser
				// steps in thickness. The minimum becomes 4; 200 stays 200.
				R = ((R + 2) / 4) * 4;							// 4px quantisation
			}
		}
		else if (drawMode != kKCMDrawModePrint)
		{
			// A thumbnail (no view, so no sxr). The zoom formula cannot be used, so the frame is a
			// fixed fraction of the image width, and thick, because a thumbnail is tiny:
			// radius = image width / kKCMThumbRingDivisor.
			R = iw / kKCMThumbRingDivisor;
			if (R < 4) R = 4;
			if (R > 200) R = 200;
			R = ((R + 2) / 4) * 4;								// 4px quantisation, as on screen
		}
		if (R > 0 && R != e->lastRadius)
		{
			KCMDrawEventHandler::BuildRing(e->buf, e->rowBytes, e->bpp, e->w, e->h, e->dist, R);
			e->lastRadius = R;
		}
	}

	// Blit the ring image. The translate and scale live inside this gsave only.
	{
		AutoGSave ag(gPort);
		// Confine the drawing to just inside this page's rectangle (clipping in spread
		// coordinates). Facing pages meet at the spine with no gap, so the page rectangle's edge IS
		// the neighbour's edge; clipping at pr exactly puts the frame's outermost pixel on that
		// shared line and draws a 1px line into the neighbouring (unchanged) page. Coming in about
		// 1pt keeps the frame off it (the frame then sits 1px inside the page edge, which looks the
		// same).
		gPort->rectclip(pr.Left()   + kKCMClipInset, pr.Top()    + kKCMClipInset,
		                pr.Width()  - kKCMClipInset * 2.0, pr.Height() - kKCMClipInset * 2.0);
		gPort->translate(pr.Left(), pr.Top());				// to the page's top left
		gPort->scale(pr.Width() / iw, pr.Height() / ih);	// image pixels -> fitted to the page rectangle
		// **Print and PDF export go through the same path** (KCMDrawRingForPrint: an alpha server
		//   with a solid vector fill), and the extra initialisation only PDF needs -- the
		//   transparency group -- is handled inside it.
		//   Screen is an image() blit, which honours the pixels' alpha (measured), at the chosen
		//   opacity (25%/75%).
		if (drawMode == kKCMDrawModePrint)
		{
			// **How a page with NO transparency used to fail, and why there is no branch for it.**
			//   Measured: on a page containing a transparent illustration, the alpha server's mask
			//     resolves and the frame is semi-transparent; on a page with no transparency at all,
			//     **the framed page came out as a solid red block**.
			//   The cause is **whether the flattener runs**: a page with transparency is flattened
			//     before the PDF is written, and the alpha server's fill is resolved along that
			//     path. With no transparency there is no flattener offscreen, the mask is never
			//     resolved, and only the rectangle underneath remains.
			//   **The fix was not here.** Whether the flattener runs is decided solely by
			//     `IXPManager`'s list of page items that carry transparency, and an adornment is not
			//     an item, so it was never in that list. Putting a representative item on the list
			//     for the duration of the export fixed the whole thing (the record is in
			//     KCMRingAdornment.h; the code is section 5 of KCMRingAdornment.cpp).
			//   @warning **do not reintroduce a "fall back to a vector fill when there is no
			//     transparency" branch.** `kPDFIsFlattenerTargetVPAttr` answers "is this port a
			//     flattener target", which is **0 at PDF 1.4 and later too** (1.4 carries
			//     transparency natively and needs no flattener), so branching on it would drop
			//     genuine semi-transparency at 1.4 down to an approximation. There is no
			//     approximation left to fall back to, and none is needed.
			//   What remains unsolved is only "a spread with no page items at all" (nothing to
			//     declare the transparency on).
			KCMDrawRingForPrint(gPort, vpAttr, db, e);
		}
		else
		{
			// Screen (and thumbnails): an image() blit, which honours the pixels' alpha (measured).
			// A thumbnail (sxr <= 0) is drawn fully opaque -- at that size 25%/75% sinks out of sight.
			const PMReal blitOpacity = (sxr <= 0) ? PMReal(1.0) : screenOpacity;
			gPort->setopacity(blitOpacity, kFalse);
			gPort->image(&e->rec, PMMatrix(), 0);		// blit our own record (which points at buf)
		}
	}
}


//========================================================================================
// A coloured border around the whole page. Used to mark a changed page in the Pages panel's
// thumbnails, where the difference ring is too small to read and is replaced by a page border.
// A vector fill with setopacity, so it composites correctly.
// Thickness: the page's short side / kKCMThumbBorderDivisor (a fixed ratio, the zoom formula being
// unavailable). Opacity: kKCMThumbMarkOpacity (0.75, slightly see-through).
//
// **THIS IS FOR THE PAGES PANEL'S THUMBNAILS ONLY**, and the argument list says so. It once took
//   sxr, drawMode and screenOpacity and branched three ways ("screen and print adapt to the zoom",
//   "print is CMYK", "screen uses screenOpacity") -- but **all three callers call it from an
//   `isThumb` branch**: the Find Overset thumbnail (wantOversetThumb implies isThumb), the Source
//   loop's `if (isThumb)`, and the Target loop's. `isThumb` is by definition **!printing and
//   GetView() == nil** (so sxr stays 0), which means **the sxr > 0 branch and the Print branch were
//   both unreachable**.
//   That showed up as a real defect in the arguments: the callers passed **two different
//     opacities** (`SelectedMarkOpacity()` and `screenMarkOp`) and **neither was ever used** (the
//     thumbnail branch picks kKCMThumbMarkOpacity). One question with two answers, in a form that
//     could never show up in behaviour -- the next person to touch it would be left wondering why
//     the opacity does nothing.
//   @warning if a border is ever wanted in the layout view, do NOT put the arguments back: copy the
//     shape of KCMDrawPageDiagonal, which really does get sxr > 0 and printing.
//========================================================================================
static void KCMDrawPageBorder(IGraphicsPort* gPort, IDataBase* db, UID pageUID,
	uint8 cr, uint8 cg, uint8 cb)
{
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// COORDINATES: as in KCMDrawEntryOnPage, the page's box comes from the Facade already in spread
	// coordinates (the reasoning is there).
	PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// THICKNESS: a fixed fraction of the page's short side (the border's own divisor). A thumbnail
	// has no view, so the zoom formula is unavailable.
	const PMReal minDim = std::min(pr.Width(), pr.Height());
	PMReal w = minDim / PMReal(kKCMThumbBorderDivisor);
	// @warning the next two are **unreachable at the current minimum page size** (measured: the
	//   smallest page is 114.39pt; with w = minDim/6, w < 0.5 needs minDim < 3pt and exceeding maxW
	//   needs minDim < 1.5pt). **They stay as insurance against InDesign changing that floor** --
	//   unlike the argument list above, this is a defence against a value arriving from outside, so
	//   being unreachable today is not a reason to remove it.
	const PMReal maxW = minDim / PMReal(2.0) - PMReal(0.5);
	if (w > maxW) w = maxW;
	if (w < PMReal(0.5))
		return;	// the page is too small for the border to be anything but a smear

	// The equivalent of the clip: start about 1pt in, so nothing reaches the spine's shared line.
	const PMReal L = pr.Left()   + kKCMClipInset, R = pr.Right()  - kKCMClipInset;
	const PMReal T = pr.Top()    + kKCMClipInset, B = pr.Bottom() - kKCMClipInset;
	if (R <= L || B <= T)
		return;	// as above (the measured minimum page size is at kKCMClipInset's definition)

	const PMReal opacity = kKCMThumbMarkOpacity;

	AutoGSave ag(gPort);
	// **A thumbnail port draws no fill unless a valid clip rectangle is set first** (the image blit
	// in KCMDrawEntryOnPage and the stroke in KCMDrawPageDiagonal likewise draw after a rectclip;
	// without it no border appears at all).
	// Clipped about 1pt in, so nothing reaches the spine's shared line (the same inset as L/R/T/B).
	// The filled bars are inside that, so nothing is cut off.
	gPort->rectclip(pr.Left()   + kKCMClipInset, pr.Top()    + kKCMClipInset,
	                pr.Width()  - kKCMClipInset * 2.0, pr.Height() - kKCMClipInset * 2.0);
	gPort->setopacity(opacity, kFalse);
	// RGB always: **generating a thumbnail is neither printing nor exporting**, so CMYK is not
	//   needed here (why CMYK is needed at all is at the head of KCMSetOutputColor).
	KCMSetOutputColor(gPort, cr, cg, cb, kFalse /*RGB*/);
	gPort->rectfill(L,     T,     R - L, w);					// top
	gPort->rectfill(L,     B - w, R - L, w);					// bottom
	gPort->rectfill(L,     T + w, w,     (B - T) - w * PMReal(2.0));	// left
	gPort->rectfill(R - w, T + w, w,     (B - T) - w * PMReal(2.0));	// right
}


//========================================================================================
// The wash over the folio exclusion area (a diagnostic).
//   While the exclusion toggle (KCMGetIgnorePageNumberMarker) is on, the folio frames of pageUID
//   are painted a semi-transparent green so the area being left out of the comparison is visible.
//   The rectangles come from KCMAppendPageNumberMarkerRects in points with the page's top left as
//   the origin, so -- like the other marks -- the page's inner bbox is taken into spread (drawing
//   port) coordinates and the rectangles are offset from its top left. Pages are assumed
//   axis-aligned, as everywhere else in the comparison and the ring drawing.
//   A vector fill with setopacity, so it composites correctly on screen and in print
//   (KCMDrawPageBorder's reasoning).
//========================================================================================
static void KCMDrawPageNumberMarkerFill(IGraphicsPort* gPort, IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return;

	// Through the cache (refresh=kFalse). This runs for every page on every draw event -- continuously
	//   while scrolling -- so it must not measure again. If a comparison (MakeEntry) put a value in,
	//   that is what gets drawn, i.e. "the area THIS comparison excluded". Only when the toggle is
	//   switched on after a comparison does this measure once and remember.
	const std::vector<PMRect>& markerRects = KCMGetPageNumberMarkerRects(UIDRef(db, pageUID), kFalse);
	if (markerRects.empty())
		return;

	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// The page's box from the Facade, in spread coordinates (the reasoning is in KCMDrawEntryOnPage).
	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	AutoGSave ag(gPort);
	gPort->setopacity(kKCMExcludeFillOpacity, kFalse);
	gPort->setrgbcolor(kKCMExcludeFillR / PMReal(255.0), kKCMExcludeFillG / PMReal(255.0), kKCMExcludeFillB / PMReal(255.0));
	for (size_t i = 0; i < markerRects.size(); ++i)
	{
		const PMRect& mr = markerRects[i];			// in points, from the page's top left
		if (mr.Width() > 0 && mr.Height() > 0)
			gPort->rectfill(pr.Left() + mr.Left(), pr.Top() + mr.Top(), mr.Width(), mr.Height());
	}
}


//========================================================================================
// A bottom-left to top-right diagonal ("/") across the page, in a given colour. Two uses:
//   - a registered page (one with no partner, "Added"/"Removed") -> a green "/" (it used to be a
//     green BORDER; the slash matches the red overflow one, so "this page has no partner" reads the
//     same either way);
//   - a page that overflowed the page-count difference and was never compared, and is not
//     registered either -> a red "/" (the same red as the change marks).
// A vector line needing no raster, so setopacity composites it correctly on screen, in print and in
// thumbnails alike. Thickness and opacity follow KCMDrawPageBorder's rules (a thumbnail uses the
// fixed ratio and kKCMThumbMarkOpacity, slightly see-through).
//========================================================================================
static void KCMDrawPageDiagonal(IGraphicsPort* gPort, IDataBase* db, UID pageUID,
	const PMReal& sxr, int32 drawMode, const PMReal& screenOpacity,
	uint8 cr, uint8 cg, uint8 cb)
{
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// The page's box from the Facade, in spread coordinates (the reasoning is in KCMDrawEntryOnPage).
	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// Thickness: screen and print adapt to the zoom; a thumbnail (sxr <= 0) uses a fixed fraction of
	// the page's short side (the slash's own divisor).
	const PMReal minDim = std::min(pr.Width(), pr.Height());
	PMReal w = (sxr > 0) ? (kKCMRingTargetPx / sxr) : (minDim / PMReal(kKCMThumbDiagDivisor));
	const PMReal maxW = minDim / PMReal(2.0);
	if (w > maxW) w = maxW;
	if (w < PMReal(0.5))
		return;

	const PMReal opacity = (sxr <= 0) ? kKCMThumbMarkOpacity
		: ((drawMode == kKCMDrawModePrint) ? KCMDrawEventHandler::SelectedMarkOpacity() : screenOpacity);

	AutoGSave ag(gPort);
	// Clipped about 1pt in, like the other marks, so nothing reaches the spine's shared line.
	gPort->rectclip(pr.Left()   + kKCMClipInset, pr.Top()    + kKCMClipInset,
	                pr.Width()  - kKCMClipInset * 2.0, pr.Height() - kKCMClipInset * 2.0);
	gPort->setopacity(opacity, kFalse);
	// Print and PDF are painted in CMYK (PDF/X-1a does not allow RGB -- see KCMSetOutputColor).
	KCMSetOutputColor(gPort, cr, cg, cb, (drawMode == kKCMDrawModePrint) ? kTrue : kFalse);
	gPort->setlinewidth(w);
	gPort->newpath();
	gPort->moveto(pr.Left(),  pr.Bottom());	// bottom left
	gPort->lineto(pr.Right(), pr.Top());		// to top right, making the "/"
	gPort->stroke();
}


//========================================================================================
// A large "+" across the page, drawn as a red line with a white halo (Pages panel thumbnails
// only). Used by the flyout's "Find Overset" to make the pages holding overset stand out in the
// Pages panel. **It is never drawn on the canvas** (the reader's decision).
//   The thick white line is stroked first and the slightly thinner red one over it, giving red with
//   a white edge. Vector lines, so they survive a tiny thumbnail (the thickness is a fixed fraction
//   of the page's short side, as the "/" is).
//   Thumbnails are generated with no view (sxr = 0), which is why the thickness cannot come from
//   the zoom.
//========================================================================================
static void KCMDrawPageCrossOutlined(IGraphicsPort* gPort, IDataBase* db, UID pageUID)
{
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// The page's box from the Facade, in spread coordinates (the reasoning is in KCMDrawEntryOnPage).
	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	// The red line's thickness is the page's short side over its own divisor (thicker than the "/").
	// The white halo is stroked wider so it shows on both sides.
	const PMReal minDim = std::min(pr.Width(), pr.Height());
	PMReal redW = minDim / PMReal(kKCMOversetCrossWidthDivisor);
	const PMReal maxW = minDim / PMReal(3.0);
	if (redW > maxW) redW = maxW;
	if (redW < PMReal(0.5))
		return;
	const PMReal whiteW = redW * PMReal(2.2);	// the halo, about 0.6 * redW proud on each side

	const PMReal cx = (pr.Left() + pr.Right()) / PMReal(2.0);	// the page's centre X
	const PMReal cy = (pr.Top()  + pr.Bottom()) / PMReal(2.0);	// and centre Y
	// Both arms are the same length: half is the short side times kKCMOversetCrossHalfRatio, so the
	// horizontal arm spans twice that -- 40% of the short side each way.
	const PMReal half = minDim * PMReal(kKCMOversetCrossHalfRatio);

	AutoGSave ag(gPort);
	// Clipped about 1pt in, like the other marks, so nothing reaches the spine's shared line.
	gPort->rectclip(pr.Left()   + kKCMClipInset, pr.Top()    + kKCMClipInset,
	                pr.Width()  - kKCMClipInset * 2.0, pr.Height() - kKCMClipInset * 2.0);
	gPort->setopacity(kKCMOversetCrossOpacity, kFalse);	// opaque, so it stays crisp

	// 1) the white halo (the thick line) first.
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->setlinewidth(whiteW);
	gPort->newpath();
	gPort->moveto(cx - half, cy);   gPort->lineto(cx + half, cy);	// horizontal, centred, 2*half long
	gPort->moveto(cx, cy - half);   gPort->lineto(cx, cy + half);	// vertical, the same length
	gPort->stroke();

	// 2) the red body (the thinner line) over it.
	gPort->setrgbcolor(kKCMRingR / PMReal(255.0), kKCMRingG / PMReal(255.0), kKCMRingB / PMReal(255.0));
	gPort->setlinewidth(redW);
	gPort->newpath();
	gPort->moveto(cx - half, cy);   gPort->lineto(cx + half, cy);	// horizontal, centred, 2*half long
	gPort->moveto(cx, cy - half);   gPort->lineto(cx, cy + half);	// vertical, the same length
	gPort->stroke();
}


//========================================================================================
// A tick drawn with vector lines at the centre of a page, in a given colour, for pages marked with
// "Check". Two destinations, selected by layoutStyle:
//   kFalse = the Pages panel's thumbnail: size = short side * 0.52, thickness = the same fixed
//     ratio the "/" uses, opacity = kKCMThumbMarkOpacity.
//   kTrue  = the layout view and print: size = short side * kKCMCheckLayoutSizeRatio (much larger),
//     thickness = the tick's size * kKCMCheckLayoutStrokeRatio (proportional to the page, so zoom
//     and print stay similar), opacity = the screenOpacity handed in (the caller passes
//     SelectedMarkOpacity, the 25%/75% choice; print is the same value).
// A tick CHARACTER (U+2713 and the like) is not used, because whether it appears depends on the
//   font: two strokes are drawn instead (left, down to the valley, up to the right).
//========================================================================================
static void KCMDrawPageCheck(IGraphicsPort* gPort, IDataBase* db, UID pageUID,
	const PMReal& sxr, int32 drawMode, const PMReal& screenOpacity,
	uint8 cr, uint8 cg, uint8 cb, bool16 layoutStyle = kFalse)
{
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return;

	// The page's box from the Facade, in spread coordinates (the reasoning is in KCMDrawEntryOnPage).
	const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

	const PMReal minDim = std::min(pr.Width(), pr.Height());
	// The tick's overall size, as a fraction of the short side: much larger for the layout view.
	const PMReal s = minDim * (layoutStyle ? kKCMCheckLayoutSizeRatio : PMReal(0.52));
	// Thickness: the layout version scales with the tick (similar at any zoom and in print); the
	// thumbnail uses the same fixed ratio as the "/".
	// (The thumbnail route never has sxr > 0 -- isThumb means a viewless generation, so sxr is 0 --
	//  but the formula is left as it is.)
	PMReal w = layoutStyle ? (s * kKCMCheckLayoutStrokeRatio)
		: ((sxr > 0) ? (kKCMRingTargetPx / sxr) : (minDim / PMReal(kKCMThumbDiagDivisor)));
	const PMReal maxW = minDim / PMReal(3.0);
	if (w > maxW) w = maxW;
	if (w < PMReal(0.5))
		return;

	// Opacity: the layout version uses screenOpacity on screen (SelectedMarkOpacity is what gets
	// passed) and SelectedMarkOpacity in print -- the same value, so screen and print match. The
	// thumbnail is fixed at kKCMThumbMarkOpacity.
	const PMReal opacity = layoutStyle
		? ((drawMode == kKCMDrawModePrint) ? KCMDrawEventHandler::SelectedMarkOpacity() : screenOpacity)
		: ((sxr <= 0) ? kKCMThumbMarkOpacity
			: ((drawMode == kKCMDrawModePrint) ? KCMDrawEventHandler::SelectedMarkOpacity() : screenOpacity));

	// Built around the page's centre, at fixed fractions of the short side. Page coordinates have
	// Top < Bottom (Y downwards).
	const PMReal cx = (pr.Left() + pr.Right()) / PMReal(2.0);
	const PMReal cy = (pr.Top()  + pr.Bottom()) / PMReal(2.0);
	const PMReal lx = cx - s * PMReal(0.40), ly = cy - s * PMReal(0.02);	// the left end (slightly high)
	const PMReal vx = cx - s * PMReal(0.10), vy = cy + s * PMReal(0.32);	// the valley (lowest point)
	const PMReal rx = cx + s * PMReal(0.48), ry = cy - s * PMReal(0.40);	// the top right (highest point)

	AutoGSave ag(gPort);
	// **Drawing into a Pages panel thumbnail requires the rectclip** (found by measurement).
	//
	//   The symptom: ticking or unticking a page with "Check" **left the page icon unchanged**,
	//   while pages that also carried another mark (a frame, a registered "/") updated correctly.
	//   The two draw with the same opacity and the same line-width formula, and **the only
	//   difference was the rectclip**. Adding it fixed the icon.
	//
	//   **A clip is both a restriction on where you draw and a declaration that you are touching
	//     this rectangle.** Without the declaration the Pages panel has no reason to redraw that
	//     page icon. Pages that happened to look right were pages where a "/" or a frame had been
	//     drawn WITH a rectclip on the same spread.
	//   @warning the mechanism itself is unconfirmed (how the application tracks its invalid
	//     regions is not public). What is certain is the measured correspondence: no rectclip means
	//     no redraw, a rectclip means a redraw. **Do not remove this line on reasoning alone.**
	//   Things that did NOT work, and were removed again: a second ForceRedraw after the Purge, and
	//     an Invalidate before the ForceRedraw. This one line was what worked.
	//
	//   @warning the layout-view version (layoutStyle) draws a tick across most of the page's short
	//     side, so clipping would cut its ends off. It is not clipped, and it has always appeared
	//     correctly.
	if (!layoutStyle)
		gPort->rectclip(pr.Left()   + kKCMClipInset, pr.Top()    + kKCMClipInset,
		                pr.Width()  - kKCMClipInset * 2.0, pr.Height() - kKCMClipInset * 2.0);
	gPort->setopacity(opacity, kFalse);
	// Print and PDF are painted in CMYK (PDF/X-1a does not allow RGB -- see KCMSetOutputColor).
	KCMSetOutputColor(gPort, cr, cg, cb, (drawMode == kKCMDrawModePrint) ? kTrue : kFalse);
	gPort->setlinewidth(w);
	gPort->newpath();
	gPort->moveto(lx, ly);
	gPort->lineto(vx, vy);
	gPort->lineto(rx, ry);
	gPort->stroke();
}


bool16 KCMDrawEventHandler::DrawSpreadMarks(DrawEventData* ded)
{
	if (ded == nil || ded->gd == nil)
		return kFalse;
	// Do not draw during OUR OWN rasterisation (MakeEntry's comparison snapshot, MakeOrigImage's
	// picture of the older version): that would be self-reference, with the marks appearing in the
	// snapshot. This used to be rejected by the kPreviewMode bit, which also caught PDF export --
	// **the same bit, 4096** -- so an explicit re-entrancy flag (tl_Rasterizing) replaced it.
	// **That collision is Adobe's deliberate design, not an accident**, so the values will not
	//   diverge later and this replacement is permanently correct (do not go back to kPreviewMode).
	//   kPreviewMode is a draw flag (IShape.h:89) and kPDFExportMode an iterate flag (IShape.h:159)
	//   -- **different enums**, both 4096. IShape.h:144 says why:
	//     "Due to lack up type checking for IShape enums, this must match kPrinting above
	//      since they are unfortunately used interchangeably in the codebase."
	//   (kIteratePrinting = 512 matches kPrinting = 512 for the same reason.)
	//   So IShape's flags are passed across enums by design: check constants in OTHER enums for
	//   collisions too.
	// The flag is thread-local, so only THIS thread being mid-rasterisation rejects the draw. As a
	//   plain static, the main thread's comparison rasterisation also suppressed the background
	//   thread's PDF export.
	if (KCMDrawEventHandler::tl_Rasterizing.Get())
		return kFalse;
	// Is this a printing context (kPrinting = 512)? While printing, whether the marks appear is
	// decided by sPrintMarks. It is never set for ordinary screen drawing.
	// **kPrinting is set for printing OR PDF export** (the official sample
	//   basicdrwevthandler/BscDEHDrwEvtHandler.cpp:278-282 says " Printing or PDF Output"), and a
	//   draw event's output really does reach a File > Export > PDF (measured, using the
	//   application's built-in watermark as a probe:
	//   docs/ai-notes/draw-event-pdf-export-experiment-2026-08-12.md).
	//   So read this as "printing and PDF export alike".
	// **This handler is called on background threads too.** KCM's model half is a `kModelPlugIn`
	//   and draw events are delivered to background threads (measured), which is what makes the
	//   marks reach an exported PDF at all.
	//   @warning what remains is that **the statics are shared but the db is not**: a background
	//     thread is handed **a clone with a different pointer**, so a document's identity must never
	//     be tested by pointer (KCMIsSameDoc, KCMThreadSafety.h).
	//   To measure where a given draw is going, the probe is app.ktDrawProbe (KT/KTDrawProbe.cpp).
	// Self-reference is handled by tl_Rasterizing above, so kPreviewMode is not consulted here.
	const bool16 printing = (ded->flags & IShape::kPrinting) != 0;

	// Detect the Pages panel's thumbnail generation: no view, kPreviewMode, not printing
	// (the diagnostic log shows flags = 0x1800 = kPreviewMode | kDrawFrameEdge). While
	// sThumbExperiment is on, wantMarks below is forced on so that frames reach thumbnails as well
	// (normally nothing is drawn with sPrintMarks and sMarksVisible off).
	// In a thumbnail the loops below take their isThumb branch and call KCMDrawPageBorder (a frame)
	// and KCMDrawPageDiagonal (a "/") instead of the difference ring image, at a fixed ratio
	// thickness that survives the size. The opacity is kKCMThumbMarkOpacity (0.75).
	const bool16 isThumb = sThumbExperiment && !printing &&
		ded->gd->GetView() == nil && (ded->flags & IShape::kPreviewMode) != 0;	// gd was nil-checked at the top of the function

	// **Overprint preview (OPP) is NOT suppressed.** It used to be treated as "a print simulation"
	// (by reading kSepPrvOPPEnabledVPAttr) and suppressed like printing, but OPP is a working mode
	// on screen: the frames held under the tool's left button, the Shift / Shift+Alt peek and the
	// badge all stay visible in it. Only real printing (kPrinting) suppresses, so "print the frames"
	// being off still keeps them out of the printed result.
	// The Source-side frames ("Always Show Marks on Source") are shown at ALL times while the
	// toggle is on -- not hidden by OPP, and always printed (independently of the Target side's
	// sPrintMarks). Whether this draw really is a Source document's spread is decided once the db
	// is known; here it is only "could it be drawn".
	// Even with sEntries empty there may be registered ("Added"/"Removed") pages, or pages that
	// overflowed the pairing because of a page-count difference, so the walk continues in order to
	// draw their green and red slashes. The overflow sets come from the cache
	// (sOverflowT/sOverflowS); EnsureOverflowCache rebuilds them only when the pair changed, so an
	// ordinary draw walks no documents.
	EnsureOverflowCache();
	// **This calculation is inside the lock.** It reads `sEntries` and `sOverflowT/sOverflowS`, all
	//   of which the drawing loops below read under the lock and the main thread writes under it
	//   (DropAll's delete + clear, MakeEntry's insert, the swap). The background thread (the
	//   asynchronous PDF export) always comes through this line, which is the very condition
	//   KCMThreadSafety.h describes.
	//   @warning **reading only empty() is still reading**: "it is a cheap read" is not a reason
	//     (guarding one side only is worthless, and here it was the WRITERS that were guarded).
	//   The cost is effectively zero: KCMPageMapHasAnyRegistered and KCMPageCheckHasAny take the
	//     same mutex themselves, and it is **recursive, so the nesting does not deadlock** (it
	//     saves re-acquisitions, if anything). The scope is six lines, so "do not hold the lock
	//     through a long operation" holds too.
	bool16 anyMarkableContent = kFalse;
	{
		KCMMarkStateLock gateLock(KCMMarkStateMutex());
		anyMarkableContent = !sEntries.empty() ||
			(sDB    != nil && KCMPageMapHasAnyRegistered(sDB)) ||
			(sSrcDB != nil && KCMPageMapHasAnyRegistered(sSrcDB)) ||
			(sDB    != nil && KCMPageCheckHasAny(sDB)) ||		// the "Check" ticks (so a thumbnail redraw is triggered)
			(sSrcDB != nil && KCMPageCheckHasAny(sSrcDB)) ||
			(!sOverflowT.empty() || !sOverflowS.empty());
	}
	// **While the button is held, the marks in that window are the other way round.**
	//   On the Source side that is **one expression**: the toggle (sSrcMarksOn) XOR the press
	//   (sSrcMarksPressed).
	//     - toggle off ... no frames, so pressing over the Source window shows them
	//     - toggle on  ... frames, so pressing over the Source window hides them
	//   @warning a flag raised only while the toggle is ON gives "pressing with the toggle off does
	//     nothing at all", which contradicts the rule as three places in this plug-in state it
	//     ("Pixel/Story, Target/Source alike").
	//   @warning **the Target side cannot take this shape**: its "show" is sMarksVisible, which
	//     other routes (the peek) also raise, which is why it needs the two separate flags
	//     (wantMarks and alwaysScreen) below.
	// @warning it must not affect print or PDF: the Source frames are always printed, so the press
	//   is gated on !printing and a printing context reads sSrcMarksOn alone.
	// A press over any other window does not raise sSrcMarksPressed (KCMPeekGesture.cpp decides).
	const bool16 srcPressed = (sSrcMarksPressed && !printing) ? kTrue : kFalse;
	// **In the Pages panel's thumbnails the Source frames are always shown**, symmetrically with
	//   the Target side (whose wantMarks below forces them with `|| isThumb`). The Pages panel is
	//   where changed pages are surveyed, so there is no reason for the two documents to behave
	//   differently there.
	//   The press XOR does not apply to thumbnails either, which also removes the oddity of the
	//     survey's frames inverting when a thumbnail is regenerated mid-press (the Target side was
	//     never exposed to it, thanks to isThumb).
	// @warning it is written `(a != 0) != (b != 0)`: bool16 is an integer type, and a bare != gives
	//   the wrong answer for a true value that is not kTrue.
	const bool16 srcWanted = printing ? sSrcMarksOn
	                       : (isThumb ? kTrue
	                                  : ((((sSrcMarksOn != 0) != (srcPressed != 0))) ? kTrue : kFalse));
	const bool16 wantSrcMarks = srcWanted && sSrcDB != nil && anyMarkableContent;
	// When printing with "print the frames" off, none of the Target-side overlay is drawn.
	// The Source-side frames are always printed, so if wantSrcMarks is alive the walk continues and
	// only the Target part is dropped by the want flags below.
	const bool16 suppressForPrint = printing && !sPrintMarks;
	if (suppressForPrint && !wantSrcMarks)
		return kFalse;
	// Settle what this draw could possibly paint from the state flags alone, and return at once if
	// the answer is nothing.
	//   - the marks (rings and frames): only with printing on, or the display on while the tool's
	//     left button is held, and only when there are entries
	//   - the peek at the older version: screen only (never printed)
	// Otherwise the whole preamble (resolving the spread, the liveness sweep, the zoom matrix, the
	//   panorama lookup, the mouse position, the visible-area transform) ran before the final branch
	//   decided "marks are hidden" and threw it away. The commonest state by far is "started, marks
	//   hidden" (the default being to show them only while the button is held), so rejecting here
	//   makes ordinary editing and scrolling cost almost nothing. The liveness sweep also becomes
	//   insurance taken only when something is actually going to be drawn (the main route for
	//   handling a close is still KCMDocResponder).
	// "Always Show Marks on Target" (sTgtMarksOn): while on, the frames are shown at all times on
	//   SCREEN, except while the tool's left button is held (sMarksTempHidden), which hides them.
	//   Screen only: print and PDF are decided independently by sPrintMarks, so alwaysScreen is
	//   inside a !printing gate and never affects a printing context.
	// **The rule is one sentence: while the button is held, everything is the other way round.**
	//     - toggle off ... no frames, so they appear while held (sMarksVisible, the reveal)
	//     - toggle on  ... frames, so they hide while held (the !sMarksTempHidden in this line)
	// @warning it must not affect print or PDF -- hence the !printing gate. The Target side's output
	//   is decided by sPrintMarks alone, asymmetrically with the Source side's sSrcMarksOn, and that
	//   asymmetry is deliberate.
	const bool16 alwaysScreen = sTgtMarksOn && !sMarksTempHidden && !printing;
	const bool16 wantMarks = !suppressForPrint && (sPrintMarks || sMarksVisible || alwaysScreen || isThumb) && anyMarkableContent;
	// **The Story mode draws no comparison ring** (nothing from sEntries). A story diff creates no
	//   entries, so sEntries is empty and the find() below would always miss -- but rather than rely
	//   on "there is nothing, so nothing is drawn", the mode is consulted and it stops explicitly.
	//   (This is the read KCMCore.cpp's sCompareMode declares itself subject to: one enum, read on
	//    any thread, written only by a menu action on the main thread.)
	// @warning **only the RING stops.** The registered pages' green "/", the overflow red "/", the
	//   ticks, the peek and the original-folio badge all come from **the page pairing**, which the
	//   Story mode has too -- KCMDoMarkChangesDoc builds the pairing and the overflow cache in both
	//   modes and says so. **So this function must not return at its entry** (that would take the
	//   peek, the ticks and the badge with it).
	const bool16 drawRings = (KCMGetCompareMode() != kKCMModeStory);
	const bool16 wantOrig  = !suppressForPrint && !printing && sShowOriginal && !sOrigImages.empty();
	// The layout-view version of the "Check" tick. On screen it is shown **at all times**,
	// completely independently of the frame toggles and the tool's left button. It reaches print and
	// PDF only with sPrintMarks (Print comparison marks) on, for the Target and Source alike. The
	// tick sets only exist on the armed Target/Source (sDB/sSrcDB) and are cleared by Stop, so
	// testing those two databases is enough.
	// Thumbnails (isThumb) are drawn by their own block below and are not included here.
	const bool16 wantChecks = !isThumb && (!printing || sPrintMarks) &&
		((sDB != nil && KCMPageCheckHasAny(sDB)) || (sSrcDB != nil && KCMPageCheckHasAny(sSrcDB)));
	// Find Overset's "+": completely independent of the comparison and the ticks. **It is never
	// drawn on the canvas** -- only into the Pages panel's thumbnails (isThumb), as red with a white
	// halo. A scan having run (sOversetOn) with a non-empty set makes it a candidate; whether this
	// particular spread gets it is decided in the drawing block (db == sOversetDB).
	const bool16 wantOversetThumb = isThumb && sOversetOn && sOversetDB != nil && !sOversetPages.empty();
	// The original-page-number badge: the toggle on, and the frames visible (printing on, or the
	// tool's left button held) -- the same visibility rule wantMarks uses. In a printing context
	// suppressForPrint leaves it alive only with sPrintMarks on, so it prints only when the marks
	// do. Whether a page's number has actually shifted is decided per page later (unshifted pages
	// draw nothing).
	const bool16 wantOldNums = !suppressForPrint && sShowOldNumbers && (sPrintMarks || sMarksVisible || alwaysScreen);
	// Registered pages (Added/Removed, the green "/") are drawn **only while a comparison is
	// running**. There used to be a separate route that drew them without one, so registering from
	// the right-click menu produced a green "/" with nothing started; it was removed (and
	// registering itself now requires an armed comparison). So the green "/" is drawn only by the
	// Target and Source loops below, both of which imply an armed comparison.

	if (!wantMarks && !wantOrig && !wantOldNums && !wantSrcMarks && !wantChecks && !wantOversetThumb)
		return kFalse;

	GraphicsData* gd = ded->gd;
	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;
	// The attributes to ask the port what kind of output this is, used to decide whether the extra
	//   initialisation a PDF export needs (the transparency group) applies -- see the head of
	//   KCMDrawRingForPrint.
	//   @warning **it can be nil**; the official sample tests for nil and bails after its ASSERT
	//     (TranFxAdornment.cpp:267-270). The receiving side reads nil as "draw as for print", so it
	//     can be passed straight through.
	IViewPortAttributes* vpAttr = gd->GetViewPortAttributes();

	// The Pages panel's thumbnail generation (an offscreen draw with no view, in kPreviewMode).
	// Nothing was drawn there originally, because a thumbnail already on screen could not be
	// regenerated and some would be left showing an old frame while others showed a new one.
	// With sThumbExperiment off (so isThumb is false) this branch restores that behaviour exactly.
	if (!printing && gd->GetView() == nil && (ded->flags & IShape::kPreviewMode) != 0)
	{
		if (!isThumb)
			return kFalse;	// experiment off: draw nothing into thumbnails
		// Experiment on: carry on and draw the frames into the thumbnail.
	}

	// changedBy is the spread being drawn.
	InterfacePtr<ISpread> spread(ded->changedBy, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	IDataBase* db = ::GetDataBase(ded->changedBy);
	if (db == nil)
		return kFalse;


	// **Three answers measured here. Nobody needs to measure them again.**
	//
	//   MAIN  db=…23FB4A80  sDB=…23FB4A80  entries=2 firstUID=258 class=1295
	//   *BG*  db=…295BE390  sDB=…23FB4A80  entries=2 firstUID=258 class=1295
	//
	//   1. **draw events are delivered to background threads too** (being a kModelPlugIn works);
	//   2. **the db handed over is a clone with a different pointer**, so it never equals sDB
	//      (guide vol1-07 L93);
	//   3. **UIDs survive the cloning** -- looking up sEntries' key (258) in the background
	//      thread's db returns a page of the same ClassID (1295).
	//
	// So **identity can be asked by UID and file rather than by db pointer**, which is the same
	//   conclusion [[uidref-reuse-after-close]] reaches (a closed document's pointer matching a
	//   different document because the address was reused): one fix answers both.

	// If a document whose marks are being held has been closed, discard them. A draw only fires for
	//   open documents, so sDB/sOrigDB can be checked for liveness here.
	//   With no marks (sDB == nil and sOrigDB == nil) nothing is asked at all, so it costs nothing.
	//   This used to call DropAll/DropAllOrig on their own, removing the marks while leaving the
	//   peek armed and the panel showing "started" -- the frames went and the button still said
	//   Stop. Normally the close responder (KCMHandleDocsClosed) gets there first and this branch is
	//   never reached, but insurance that is kept has to do the full Stop-equivalent clean-up, so it
	//   goes through the same function.
	//   sOversetDB is included: with Find Overset used on its own (nothing armed) and its document
	//   closed, a missed responder would leave a stale sOversetPages that could mark the thumbnails
	//   of a NEW document whose address was reused.
	//   **The guard is main-thread only, and that fixed a reproduced defect.** The insurance rests
	//     on "a draw only comes for open documents", i.e. **not in the document list means closed**
	//     -- and **that reasoning does not hold on a background thread**, which sees **a cloned
	//     database** (guide vol1-07 L93), so `FindDocByDataBase(sDB)` **returns nil for a document
	//     that is open**. The result was that every asynchronous PDF export ran
	//     `KCMHandleDocsClosed()` (a full Stop-equivalent clean-up) and **the marks all disappeared**
	//     (reproduced with `Document.asynchronousExportFile()`; "marks cleared" appears).
	//   The block was put at **KCMHandleDocsClosed's own entrance** (it has three callers, and "a
	//     background thread cannot judge whether a document is alive" is a property of the function
	//     -- [[one-question-one-place]]), so this site is left plain.
	//   @warning "do nothing on a background thread" is right **because that function discards
	//     state**. Stopping the DRAWING side on a background thread would defeat the whole point of
	//     making the marks reach an exported PDF.
	if (sDB != nil || sOrigDB != nil || sSrcDB != nil || sOversetDB != nil)
	{
		ISession* session = GetExecutionContextSession();	// can be nil during shutdown
		InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList != nil &&
		    ((sDB != nil && docList->FindDocByDataBase(sDB) == nil) ||
		     (sOrigDB != nil && docList->FindDocByDataBase(sOrigDB) == nil) ||
		     (sSrcDB != nil && docList->FindDocByDataBase(sSrcDB) == nil) ||
		     (sOversetDB != nil && docList->FindDocByDataBase(sOversetDB) == nil)))
			KCMHandleDocsClosed();
	}

	// The screen scale (the zoom), fetched once. Non-nil only for screen drawing.
	// **This nil check IS the multithreading conformance.** Being handed an `IControlView*` by the
	//   framework in a drawing signature is normal for model-side code (the SDK's
	//   `FrmLblAdornment.cpp` and `TranFxAdornment.cpp` do the same, and both are kModelPlugIn).
	//   @warning it is also the exact place guide vol1-07 L101 means when it says "It is critical
	//     that you write model code that expects to be able to receive nil pointers" -- **printing
	//     and PDF export have no window, so it arrives nil**. This code was written expecting that
	//     (sxr simply stays 0). **Write the same guard whenever this shape is written again.**
	PMReal sxr = 0.0;
	IControlView* zview = gd->GetView();
	if (zview != nil)
	{
		PMMatrix toWin = zview->GetContentToWindowMatrix();	// content -> window (screen px) at the current zoom
		sxr = abs(toWin.GetXScale());	// the scale can be negative (PMReal's abs, from PMReal.h)
	}

	// The drawing mode (thumbnail generation returned earlier, so it cannot arrive here).
	int32 drawMode = printing ? kKCMDrawModePrint : kKCMDrawModeScreen;

	// For print and PDF, fix the appearance at "100% display" (no zoom tracking). A printing port
	// has no view, so sxr is 0; giving it an effective 1.0 (100%, device scale 1) makes the ring
	// thickness formula produce exactly what 100% on screen produces, and the downstream
	// zoom-adaptive formula can be reused unchanged.
	if (printing)
		sxr = 1.0;

	// The "Check" tick, thumbnail version: a blue tick at the centre of a ticked page's thumbnail.
	//   Completely independent of the other marks (the rings, the slashes, the Source toggle): this
	//   spread's db may be the Target or the Source, and if that db has ticks they are drawn. It
	//   comes before the Target/Source loops below and is not subject to their gates. The layout
	//   view and print version is the wantChecks block further down.
	//   Armed only (the tick sets are cleared by Stop, so they are empty when nothing is armed --
	//   the arm test is insurance).
	if (isThumb && KCMIsArmed() && KCMPageCheckHasAny(db))
	{
		const int32 npChk = spread->GetNumPages();
		for (int32 i = 0; i < npChk; ++i)
		{
			const UID puid = spread->GetNthPageUID(i);
			if (KCMPageCheckIsChecked(db, puid))
				KCMDrawPageCheck(gPort, db, puid, sxr, drawMode, kKCMThumbMarkOpacity,
					kKCMCheckR, kKCMCheckG, kKCMCheckB);
		}
	}

	// Find Overset's marks, thumbnail version. Only while generating the Pages panel thumbnails of
	//   the document that was scanned (sOversetDB), the pages holding overset (sOversetPages) get
	//   (a) the same red border a changed page gets (KCMDrawPageBorder) and (b) a red "+" with a
	//   white halo at the centre.
	//   The border was once removed in favour of the "+" alone and then brought back: the cross by
	//   itself is hard to spot, and visibility beat distinguishability. **Nothing is drawn on the
	//   canvas.** It is independent of the comparison and the ticks, so it appears while sOversetOn
	//   even with nothing armed (an overset check has a result whether or not documents are being
	//   compared).
	if (wantOversetThumb && db == sOversetDB)
	{
		const int32 npx = spread->GetNumPages();
		for (int32 i = 0; i < npx; ++i)
		{
			const UID puid = spread->GetNthPageUID(i);
			if (sOversetPages.count(puid) > 0)
			{
				KCMDrawPageBorder(gPort, db, puid,
					kKCMRingR, kKCMRingG, kKCMRingB);	// the same red border a change gets, for visibility
				KCMDrawPageCrossOutlined(gPort, db, puid);	// and the red-with-white-halo "+" at the centre
			}
		}
	}

	// Is the spread being drawn the one currently being peeked at? The peek covers only the single
	// spread under the mouse (whose pages are in sOrigImages). That spread should show the older
	// version cleanly, so its marks are not drawn; every other spread keeps its marks as usual.
	// The peek is not applied to thumbnail generation: a thumbnail regenerated while the button is
	// held would cache the older version's image and go on showing it after the button came up.
	bool16 peekingThisSpread = kFalse;
	if (wantOrig && !isThumb && sOrigDB != nil && db == sOrigDB)
	{
		const int32 npChk = spread->GetNumPages();
		for (int32 i = 0; i < npChk; ++i)
			if (sOrigImages.find(spread->GetNthPageUID(i)) != sOrigImages.end())
			{ peekingThisSpread = kTrue; break; }
	}

	// The peek itself, independent of the marks (sEntries): each page of the peeked spread gets the
	// older version's picture blitted opaquely across its whole rectangle.
	if (peekingThisSpread)
	{
		const int32 npo = spread->GetNumPages();
		for (int32 i = 0; i < npo; ++i)
		{
			const UID pageUID = spread->GetNthPageUID(i);
			std::map<UID, KCMOrigImage*>::iterator it = sOrigImages.find(pageUID);
			if (it == sOrigImages.end())
				continue;
			KCMOrigImage* o = it->second;
			if (o == nil || o->buf == nil || o->w <= 0 || o->h <= 0)
				continue;
			InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
			if (pageGeo == nil)
				continue;
			// The page's box from the Facade, in spread coordinates (see KCMDrawEntryOnPage).
			const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
				UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());
			AutoGSave ag(gPort);
			gPort->setopacity(sPeekOpacity, kFalse);		// Shift peek = 1.0 (opaque), Shift+Alt peek = 0.5
			gPort->translate(pr.Left(), pr.Top());
			gPort->scale(pr.Width() / o->w, pr.Height() / o->h);	// fit the older picture to the page rectangle
			gPort->image(&o->rec, PMMatrix(), 0);			// and lay it over at sPeekOpacity
		}
	}

	// The "Check" tick, layout view and print version: a blue tick drawn **large** (the short side
	//   times kKCMCheckLayoutSizeRatio) at the centre of a ticked page. Target or Source alike -- if
	//   this spread's db has ticks, they are drawn, independently of the frame toggles and the
	//   tool's left button (so on screen they are always visible). Print and PDF are already gated
	//   by wantChecks on sPrintMarks. The opacity is the panel's 25%/75% choice
	//   (SelectedMarkOpacity), the same on screen and in print.
	//   It comes right after the peek so that the tick sits on top of the peek's opaque picture and
	//   stays visible, and before the Source/Target loops because the Source loop ends in a return.
	if (wantChecks && KCMIsArmed() && KCMPageCheckHasAny(db))
	{
		const int32 npc = spread->GetNumPages();
		for (int32 i = 0; i < npc; ++i)
		{
			const UID puid = spread->GetNthPageUID(i);
			if (KCMPageCheckIsChecked(db, puid))
				KCMDrawPageCheck(gPort, db, puid, sxr, drawMode, SelectedMarkOpacity(),
					kKCMCheckR, kKCMCheckG, kKCMCheckB, kTrue /*layoutStyle*/);
		}
	}

	// The original-page-number badge (Show Original Page Numbers). On a page whose "current page
	// number" marker has SHIFTED because spreads are hidden, the number it had before the hiding is
	// drawn at the bottom centre (on screen, and in print and PDF too).
	// Independent of the marks (sEntries): this db need not be one being compared (with nothing
	// hidden the original and current numbers agree and nothing is drawn). GetPageString's last
	// argument bIncludePagesOfHiddenSpread selects between them -- kTrue counts hidden pages (the
	// original number), kFalse skips them (the number the marker shows now).
	// The format is the NUMBER ALONE (bIncludeSectionName = kFalse, no "A:" prefix) in the section's
	// own numbering style (bUseIntegerStyle = kFalse), so it matches the real folio. The characters
	// are drawn framelabel-style (selectfont + show), at a size independent of the zoom
	// (fontSize = the target px / sxr; printing fixes sxr at 1.0, giving real points).
	// It is drawn as black text with a white halo, and the badge's overall opacity follows the
	// 25%/75% choice.
	if (wantOldNums && sxr > 0)
	{
		InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
		// The font and instance come from the cache at the top of this file, fetched once.
		// **Only the main thread performs that first fetch.** This is the one place that writes the
		//   three shared statics (sOldNumFontTried / sOldNumFont / sOldNumFontInst), and a
		//   background thread arriving at the same time would **issue QueryFont twice and lose one
		//   of the pointers** (a leak).
		//   @warning on a background thread with nothing fetched yet, numFont stays nil and the
		//     `numFont != nil` below simply skips the badge. The badge is an off-by-default toggle,
		//     and with it on the screen drawing (main thread) fills the cache first, so "the badge
		//     is missing from the PDF only" needs it never to have been shown on screen.
		if (!sOldNumFontTried && KCMIsMainThread())
		{
			sOldNumFontTried = kTrue;
			// InterfacePtr(p, iid) accepts p == nil (InterfacePtr.h:459 -- QueryInterface_ tests for
			//   it), so a session that is nil during shutdown just leaves fontMgr nil here. An
			//   explicit guard is only needed for a **direct method call** such as
			//   session->QueryApplication().
			InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
			if (fontMgr != nil)
			{
				sOldNumFont = fontMgr->QueryFont(fontMgr->GetDefaultFontName());
				if (sOldNumFont != nil)
				{
					// The size is fixed at the equivalent of 50% document zoom, so the instance
					//   (font x matrix) never changes and can be cached.
					const PMReal cacheSize = kKCMOldNumFontPx / kKCMOldNumFixedZoom;
					PMMatrix fontMatrix(cacheSize, 0.0, 0.0, cacheSize, 0.0, 0.0);
					sOldNumFontInst = fontMgr->QueryFontInstance(sOldNumFont, fontMatrix);
				}
			}
		}
		IPMFont*       numFont  = sOldNumFont;
		IFontInstance* fontInst = sOldNumFontInst;	// nil is fine: the branches below fall back
		if (pageList != nil && numFont != nil)
		{
			// The size is fixed at the equivalent of 50% document zoom: dividing by that constant
			//   rather than by sxr (the effective screen/print scale) is what keeps the badge the
			//   same size relative to the page at any zoom and in print.
			const PMReal fontSize = kKCMOldNumFontPx / kKCMOldNumFixedZoom;
			const PMReal margin   = kKCMOldNumMarginPx / kKCMOldNumFixedZoom;

			const int32 npn = spread->GetNumPages();

			// Decide ONCE, from the first page, whether this spread can contain a shifted number.
			//   A shift depends on there being a hidden spread BEFORE this one, so every page of the
			//   spread is under the same condition: if the first page agrees, either the hiding does
			//   not reach it or it is inside a section that fixes the numbering, and the following
			//   pages continue from it and agree too.
			//   In an ordinary document with nothing hidden this always holds, and without it every
			//   draw called GetPageString twice for every page and threw both away -- the whole time
			//   the badge was on.
			//   When it HAS shifted, every page is walked as before (the per-page test inside the
			//   loop stays, so a page where a section starts mid-spread and removes the shift is
			//   still skipped correctly).
			bool16 spreadMayShift = kFalse;
			if (npn > 0)
			{
				const UID probeUID = spread->GetNthPageUID(0);
				PMString probeOrig, probeCur;
				pageList->GetPageString(probeUID, &probeOrig, kFalse, kFalse, kDefaultPageType, kTrue, kTrue);
				pageList->GetPageString(probeUID, &probeCur,  kFalse, kFalse, kDefaultPageType, kTrue, kFalse);
				spreadMayShift = (probeOrig != probeCur);
			}

			for (int32 i = 0; spreadMayShift && i < npn; ++i)
			{
				const UID pageUID = spread->GetNthPageUID(i);
				PMString orig, cur;
				// 3rd argument bIncludeSectionName = kFalse: the number alone, with no section
				// prefix ("A:" and the like). Both numbers are fetched with the same settings, so
				// the comparison is not thrown off.
				pageList->GetPageString(pageUID, &orig, kFalse, kFalse, kDefaultPageType, kTrue, kTrue);	// the original (counting hidden pages)
				pageList->GetPageString(pageUID, &cur,  kFalse, kFalse, kDefaultPageType, kTrue, kFalse);	// the current (skipping them)
				if (orig == cur)
					continue;	// not shifted: no hidden spread before this page

				InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
				if (pageGeo == nil)
					continue;
				// The page's box from the Facade, in spread coordinates (see KCMDrawEntryOnPage).
				const PMRect pr = Utils<Facade::IGeometryFacade>()->GetItemBounds(
					UIDRef(db, pageUID), Transform::SpreadCoordinates(), Geometry::PathBounds());

				PMReal textW = 0.0;
				if (fontInst != nil)
					fontInst->MeasureWText(orig, textW);
				const PMReal ascent  = (fontInst != nil) ? fontInst->GetAscent()  : (fontSize * PMReal(0.8));
				const PMReal descent = (fontInst != nil) ? fontInst->GetDescent() : (fontSize * PMReal(0.2));
				const PMReal tx = (pr.Left() + pr.Right()) / 2 - textW / 2;	// centred at the bottom
				const PMReal ty = pr.Bottom() - margin - descent;			// the baseline (margin + descent up from the bottom)

				const int32 nch = orig.NumUTF16TextChars();
				const UTF16TextChar* buf16 = orig.GrabUTF16Buffer(nil);

				AutoGSave ag(gPort);
				// The badge (a white halo and black text, no background) is bundled into one
				// transparency group, and SelectedMarkOpacity (the frames' 25%/75%, the same on
				// screen and in print) is applied once, to the group's compositing.
				// starttransparencygroup inherits the GState as it stands (i.e. the setopacity just
				// made) for the group's compositing and resets alpha to 1.0 INSIDE the group
				// (IGraphicsPort.h), so everything within can be drawn opaque and the density does
				// not change where the halo overlaps the body (with a bare setopacity the
				// overlapping pixels come out denser). cs = nil is a non-isolated group, which uses
				// the parent's colour space.
				const PMReal pad = fontSize * kKCMOldNumPadEm;	// padding of the group's bbox (wide enough for the halo)
				const PMRect badgeRect(tx - pad, ty - ascent - pad, tx + textW + pad, ty + descent + pad);
				gPort->setopacity(SelectedMarkOpacity(), kFalse);	// the group's overall opacity
				gPort->starttransparencygroup(badgeRect, nil, kFalse /*non-isolated*/, kFalse /*no knockout*/);

				gPort->selectfont(numFont, fontSize);
				// The white halo (shown eight times, offset around the centre), then the body.
				// It reads on light and dark ground alike (the same method the cursor's tick halo uses).
				const PMReal halo = fontSize * kKCMOldNumHaloEm;
				// Print and PDF are CMYK (white = zero in every channel); PDF/X-1a does not allow RGB.
				KCMSetOutputColor(gPort, 255, 255, 255, printing);	// the halo
				for (int32 dy = -1; dy <= 1; ++dy)
					for (int32 dx = -1; dx <= 1; ++dx)
						if (dx != 0 || dy != 0)
							gPort->show(tx + halo * dx, ty + halo * dy, nch, buf16);
				// The body, in whatever the constants say (black by default).
				// Print and PDF are CMYK here too.
				//   @warning the constants are PMReal (0.0-1.0), so they are converted to 0..255
				//     before being passed -- written this way so that changing a constant is
				//     followed automatically rather than by editing a literal. Black gives K100.
				KCMSetOutputColor(gPort,
					(uint8)::ToInt32(::Round(kKCMOldNumR * PMReal(255.0))),
					(uint8)::ToInt32(::Round(kKCMOldNumG * PMReal(255.0))),
					(uint8)::ToInt32(::Round(kKCMOldNumB * PMReal(255.0))), printing);
				gPort->show(tx, ty, nch, buf16);

				gPort->endtransparencygroup();
			}
		}
	}

	// The Source document's rings ("Always Show Marks on Source"): when the current spread belongs
	// to the Source document, the same ring image is laid over the Source page through the mapping
	// (Source page UID -> Target page UID).
	// While the toggle is on they are shown at all times, regardless of the tool's left button. The
	// opacity is fixed at the panel's 25%/75% choice (SelectedMarkOpacity), and a printing context
	// reaches here through suppressForPrint (the print route uses the same SelectedMarkOpacity, so
	// screen and print match; OPP is not suppressed at all).
	// The same db as the Target (an unexpected self-comparison) is left to the Target drawing below,
	// so nothing is drawn twice.
	// **Identity is asked with KCMIsSameDoc, not a pointer comparison**: the background thread (the
	//   asynchronous PDF export) is handed **a cloned db**, so `db == sSrcDB` is always false there
	//   and the Source frames never reached an export. On the main thread an identical pointer
	//   settles it immediately, so nothing about the old behaviour changes.
	// @warning **the peeked spread gets no frames.** The Target side always had this (its ring
	//   drawing returns early on peekingThisSpread); the Source side lacked the same guard and the
	//   frames sat on top of the older version's picture.
	//   Why it was invisible until now: the Source frames used to appear only with the toggle ON,
	//   and with it on a press raised the "temp hidden" flag and they were hidden anyway. **Making
	//   the press show them with the toggle OFF (the XOR) is what first created a route for them to
	//   appear during a peek** -- because peeking is a press too.
	//   **Making a feature symmetric exposes the guard the asymmetric side never had.**
	if (wantSrcMarks && !peekingThisSpread && KCMIsSameDoc(db, sSrcDB) && !KCMIsSameDoc(db, sDB))
	{
		// The lock is taken for the same reason as the Target loop below, and held until the return.
		KCMMarkStateLock srcMarkLock(KCMMarkStateMutex());
		const int32 nps = spread->GetNumPages();
		// The green wash is a **screen-only diagnostic** showing what the comparison excluded, so it
		//   never goes into print or PDF (!printing) -- unlike the rings, it covers the design
		//   underneath.
		//   The Source side alone used to be on the route that ignores sPrintMarks (its frames are
		//   always printed), so the green appeared in a Source print even with "Print comparison
		//   marks" off. Target and Source are both screen-only now.
		const bool16 fillExcluded = !printing && KCMGetIgnorePageNumberMarker();	// the folio wash: toggle on, screen only
		for (int32 i = 0; i < nps; ++i)
		{
			const UID srcPageUID = spread->GetNthPageUID(i);
			// Registered and overflow are both needed by the if/else chain and by the wash, so each
			// is asked once per page and reused.
			const bool16 isRegistered = KCMPageMapIsRegistered(db, srcPageUID);
			const bool16 isOverflow   = (sOverflowS.count(srcPageUID) > 0);
			std::map<UID, UID>::iterator mp = sSrcPageToTarget.find(srcPageUID);
			if (mp != sSrcPageToTarget.end())
			{
				std::map<UID, KCMOverlayEntry*>::iterator it = sEntries.find(mp->second);
				if (drawRings && it != sEntries.end())	// no rings in the Story mode (drawRings above)
				{
					if (isThumb)
					{
						// **A thumbnail's border follows the panel's "Mark colour" too.**
						//   @warning this is the **only changed-page display that does not use the
						//     ring image** (at thumbnail size a solid border is drawn instead of
						//     shrinking the image), which is how it was left behind at red when the
						//     colour became a choice -- **cyan on the canvas, red in the Pages
						//     panel**.
						//   The reason for offering cyan at all ("red marks are lost against red
						//     ground") applies most at thumbnail size, where everything is harder to
						//     tell apart.
						uint8 mr = 0, mg = 0, mb = 0;
						SelectedMarkColor(mr, mg, mb);
						KCMDrawPageBorder(gPort, db, srcPageUID, mr, mg, mb);
					}
					else
						KCMDrawEntryOnPage(gPort, vpAttr, it->second, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity());
				}
			}
			else if (isRegistered)
			{
				// A Source page that is not in the mapping (so not being compared). Registered
				// ("Removed") pages get the green "/".
				KCMDrawPageDiagonal(gPort, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity(), kKCMAddedBorderR, kKCMAddedBorderG, kKCMAddedBorderB);
			}
			else if (isOverflow)
			{
				// Not registered either: a page that overflowed the page-count difference. The red
				// slash says it was never compared.
				// @warning **this red does NOT follow the "Mark colour" choice, deliberately** -- it
				//   says something different ("not compared", not "changed"). It stands with the
				//   registered pages' green "/" as **a mark whose meaning does not change with the
				//   mark colour**. With cyan chosen, changed (cyan) and uncompared (red) are even
				//   easier to tell apart.
				KCMDrawPageDiagonal(gPort, db, srcPageUID, sxr, drawMode, SelectedMarkOpacity(), kKCMRingR, kKCMRingG, kKCMRingB);
			}
			// With the toggle on, the green wash goes only over the pages actually being compared
			// (in the mapping -- neither registered-Removed nor overflow). It is drawn independently
			// of the if/else above, because a page with no entry (nothing changed) needs it too.
			// Removed and overflow pages are never pixel-compared at all, so "folio exclusion" means
			// nothing for them.
			if (!isThumb && fillExcluded && !isRegistered && !isOverflow)
				KCMDrawPageNumberMarkerFill(gPort, db, srcPageUID);
		}
		return kFalse;	// a Source document carries no Target-side overlay: done
	}

	// The change overlay (the rings), only when the marked document is this spread's db.
	// Nothing is drawn while the master toggle (sMarksVisible) is off, or while this spread is being
	// peeked at -- the data is kept, so it comes straight back. Other spreads keep their marks.
	// With print marks (sPrintMarks) on they are always drawn, regardless of the tool's left button
	// (WYSIWYG on screen, and into print and PDF).
	// **`db != sDB` was the whole of "the marks do not appear in an exported PDF".** On a background
	//   thread it is always true (a cloned pointer), so not one Target-side mark was drawn and an
	//   asynchronously exported PDF was **byte-for-byte identical** to one exported with Print
	//   comparison marks off. Identity is asked by file instead.
	//   The loop below looks entries up **by page UID**, so once this line passes, the rest works on
	//   a background thread unchanged (UIDs surviving the cloning was measured -- see above).
	if (peekingThisSpread || !wantMarks || sDB == nil || !KCMIsSameDoc(db, sDB))
		return kFalse;

	// The effective opacity of the on-screen marks. sMarkScreenOpacity always holds the effective
	// value, set by:
	//   - while the tool's left button is held = the chosen opacity (the panel's 25%/75%)
	//   - otherwise = the base value, KCMBaseScreenOpacity() (the chosen opacity with printing on,
	//     1.0 with it off)
	// Releasing restores the base value. The printing route does not use this: KCMDrawRingForPrint
	// takes SelectedMarkOpacity directly.
	const PMReal screenMarkOp = sMarkScreenOpacity;

	// For each page of this spread, draw its entry if it has one (the drawing itself is shared with
	// KCMDrawEntryOnPage).
	// **From here on the mark state is read, so the lock is taken.** It protects two things:
	//   1. an element of sEntries being deleted by DropAll or MakeEntry while it is being read;
	//   2. KCMDrawEntryOnPage rewriting e->buf through BuildRing, so that the main thread (a radius
	//      from the screen zoom) and a background thread (a radius from the fixed sxr = 1.0 of
	//      print) **fight over the same buffer**.
	// @warning the lock covers the rest of the function, i.e. the whole drawing loop. Drawing takes
	//   a few milliseconds, so the wait is not a practical problem.
	KCMMarkStateLock markLock(KCMMarkStateMutex());
	const int32 np = spread->GetNumPages();
	// The green wash is a **screen-only diagnostic** (see the Source loop above for the full reason).
	const bool16 fillExcluded = !printing && KCMGetIgnorePageNumberMarker();	// the folio wash: toggle on, screen only
	for (int32 i = 0; i < np; ++i)
	{
		const UID pageUID = spread->GetNthPageUID(i);
		// Registered and overflow are both needed by the if/else chain and by the wash, so each is
		// asked once per page and reused.
		const bool16 isRegistered = KCMPageMapIsRegistered(db, pageUID);
		const bool16 isOverflow   = (sOverflowT.count(pageUID) > 0);
		std::map<UID, KCMOverlayEntry*>::iterator it = sEntries.find(pageUID);
		if (drawRings && it != sEntries.end())	// no rings in the Story mode (drawRings above)
		{
			if (isThumb)
			{
				// A thumbnail's border follows the panel's "Mark colour" as well (the reason is at
				// the Source side above: this is the only changed-page display that does not use
				// the ring image, so it was left behind when the colour became a choice).
				uint8 mr = 0, mg = 0, mb = 0;
				SelectedMarkColor(mr, mg, mb);
				KCMDrawPageBorder(gPort, db, pageUID, mr, mg, mb);
			}
			else
				KCMDrawEntryOnPage(gPort, vpAttr, it->second, db, pageUID, sxr, drawMode, screenMarkOp);
		}
		else if (isRegistered)
		{
			// A Target page with no comparison entry (so not being compared). Registered ("Added")
			// pages get the green "/".
			KCMDrawPageDiagonal(gPort, db, pageUID, sxr, drawMode, screenMarkOp, kKCMAddedBorderR, kKCMAddedBorderG, kKCMAddedBorderB);
		}
		else if (isOverflow)
		{
			// Not registered either: a page that overflowed the page-count difference. The red slash
			// says it was never compared.
			// @warning this red does not follow the "Mark colour" choice, deliberately (the full
			//   reason is at the Source side above).
			KCMDrawPageDiagonal(gPort, db, pageUID, sxr, drawMode, screenMarkOp, kKCMRingR, kKCMRingG, kKCMRingB);
		}
		// With the toggle on, the green wash goes only over the pages actually being compared
		// (neither registered-Added nor overflow). Drawn independently of the if/else, because a
		// page with no entry needs it too. Added and overflow pages are never pixel-compared, so
		// folio exclusion means nothing for them.
		if (!isThumb && fillExcluded && !isRegistered && !isOverflow)
			KCMDrawPageNumberMarkerFill(gPort, db, pageUID);
	}

	return kFalse;	// let other handlers and the rest of the drawing continue
}
