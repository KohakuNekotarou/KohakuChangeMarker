//========================================================================================
//
//  KCMColorSampler.cpp
//
//  The implementation of the click-point CMYK sampling (see KCMColorSampler.h).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "IGeometry.h"
#include "IShape.h"
#include "TransformUtils.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"
#include "PMString.h"
#include "SnapshotUtilsEx.h"
#include "AGMImageAccessor.h"

#include "KCMConstants.h"
#include "KCMDrawEventHandler.h"   // KCMRasterizingGuard / KCMDrawEventHandler::tl_Rasterizing
#include "KCMCore.h"               // KCMFindPageUnderMouse
// This file deliberately does not include the UI's KCMViewLookup.h. Resolving which view the
//   mouse is over belongs to the caller (the UI); all this .cpp answers is "what colour is the
//   point I was given".
//   @warning **the third of those lookups must not be dropped along the way**: comparing
//   KCMFindDocDbForView(view) against hoverDB is what stops a different window's coordinates
//   being read as hoverDB's page coordinates, and it now lives in the callers (see the note in
//   KCMColorSampler.h).
#include "KCMPageMap.h"            // KCMMapTargetToSource / KCMBuildPairing (the exclusion pairing)
#include "KCMColorSampler.h"

#include <map>
#include <new>						// std::nothrow (allocating the SnapshotUtilsEx)

//----------------------------------------------------------------------------------------
// The hover -> other page mapping cached while Alt + left is held down (see KCMColorSampler.h).
// The page structure cannot change under a held button, so the whole-document KCMBuildPairing
// runs once instead of on every sample (up to 20 a second). Outside a press this is inactive and
// a one-off sample builds the mapping as before.
// **The direction is fixed when the button goes down**: press in the Target window and it maps
//   target -> source, press in the Source window and it maps source -> target. The reference
//   window cannot change mid-press, because the hovered document is pinned at press time.
//----------------------------------------------------------------------------------------
static bool16              sDragCacheActive  = kFalse;
static IDataBase*          sDragCacheHoverDB = nil;	// compared against, never dereferenced
static IDataBase*          sDragCacheOtherDB = nil;
static std::map<UID, UID>  sDragCacheH2O;			// hover page -> other page

void KCMSampleCmykBeginDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget)
{
	sDragCacheH2O.clear();
	sDragCacheHoverDB = hoverDB;
	sDragCacheOtherDB = otherDB;
	sDragCacheActive  = kFalse;
	if (hoverDB == nil || otherDB == nil)
		return;

	// KCMBuildPairing takes its arguments in target/source order, so the two are sorted by which
	// side is hovered and the resulting pairs stored the hover -> other way round.
	IDataBase* const targetDB = hoverIsTarget ? hoverDB : otherDB;
	IDataBase* const sourceDB = hoverIsTarget ? otherDB : hoverDB;
	std::vector<UID> pairT, pairS;
	KCMBuildPairing(targetDB, sourceDB, pairT, pairS);
	for (size_t k = 0; k < pairT.size(); ++k)
	{
		if (hoverIsTarget) sDragCacheH2O[pairT[k]] = pairS[k];
		else               sDragCacheH2O[pairS[k]] = pairT[k];
	}
	// **The master spread pairs go into the same cache.** @warning leave them out and the master
	//   pages report their CMYK on a single click but stop reporting it as soon as the button is
	//   held -- **behaviour that changes only while dragging**, because a live cache never reaches
	//   KCMMapTargetToSource.
	{
		std::vector<UID> mT, mS;
		KCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
		for (size_t k = 0; k < mT.size(); ++k)
		{
			if (hoverIsTarget) sDragCacheH2O[mT[k]] = mS[k];
			else               sDragCacheH2O[mS[k]] = mT[k];
		}
	}
	sDragCacheActive = kTrue;
}

void KCMSampleCmykEndDrag()
{
	sDragCacheActive  = kFalse;
	sDragCacheHoverDB = nil;
	sDragCacheOtherDB = nil;
	sDragCacheH2O.clear();
}

// Rasterise a tiny area of pageRef's page around spreadPt (that page's spread coordinates) in
// CMYK at high dpi, and read the raw C, M, Y, K of the centre pixel (0..255) into out[4]. The
// accessor and the snapshot are destroyed immediately: holding neither across a draw is what
// keeps their destruction from crashing. kTrue on success.
static bool16 KCMReadCmykPixel(const UIDRef& pageRef, const PMPoint& spreadPt, uint8 out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0;
	if (pageRef.GetDataBase() == nil || pageRef.GetUID() == kInvalidUID)
		return kFalse;

	// **Rasterising can trigger the lazy recomposition of an uncomposed story, and composing
	//   dirties the document.** If it was clean on the way in, it is clean on the way out. (One
	//   guard per database: this function is called once for the hovered document and once for
	//   its counterpart.)
	//   @warning **proxy drawing (fullRes=kFalse) does not make this unnecessary** -- that is a
	//     different question. What fullRes suppresses is generating placed images at full
	//     resolution; what dirties a document here is composition. Start, the partial
	//     re-comparison, the book comparison and the overset scan all carry the same guard for
	//     the same reason.
	//   The cost is saving and restoring a flag, which is nothing even at 20 samples a second.
	IDataBase::SaveRestoreModifiedState dirtyGuard(pageRef.GetDataBase());

	// The tiny rectangle around the point, in spread coordinates. boundsToSpreadMatrix is the
	// identity because these already are spread coordinates.
	const PMReal hp = kKCMSampleHalfPt;
	PMRect clip(spreadPt.X() - hp, spreadPt.Y() - hp, spreadPt.X() + hp, spreadPt.Y() + hp);

	SnapshotUtilsEx* snap = new (std::nothrow) SnapshotUtilsEx(clip, PMMatrix(), pageRef, 1.0, 1.0,
		kKCMSampleDpi, 72.0, 0.0, SnapshotUtilsEx::kCsCMYK, kFalse);
	if (snap == nil)
		return kFalse;	// nothrow: out of memory costs one sample, nothing more (as in KCMDrawEventHandler)
	// Proxy drawing (fullRes=kFalse), the same as the frame comparison uses, so no placed image is
	// generated at full resolution. Dirtying is guarded separately, by the SaveRestoreModifiedState
	// above.
	// **greek is 0.0, disabled.** At the default 7.0, text below that point size is drawn as a
	//   grey bar with no glyph shapes in it (SnapshotUtilsEx.h:224-225), so clicking on small text
	//   would read the colour of the bar rather than the colour of the text -- and since the same
	//   bar appears on both sides, the two values agree and the mistake is invisible. Reading
	//   pixels therefore wants greeking off, the same judgement the comparison rasterisation
	//   (MakeEntry) makes. It costs only a slightly slower rasterisation of a 2pt square.
	//   @warning **the greek threshold is multiplied by the scale** ("its point size multiplied by
	//     the scaling is less than the greek below value"). At the 300dpi used here that is 4.17x,
	//     so even the default would only have greeked text below about 1.68pt: the danger above is
	//     narrower than it reads.
	//     **It is decisive in the comparison rasterisation instead**, which runs at 36dpi (0.5x):
	//     the default greeks everything below 14pt, which is the body text of most documents. The
	//     same 0.0 matters far more there.
	// **Non-printing objects are left switched on** (the eighth argument, bDrawNonPrintingObjects,
	//   defaults to kTrue). This deliberately differs from the comparison rasterisation, which
	//   passes kFalse: that one asks "did the printed result change", while this one answers "what
	//   colour is the point the user clicked on", so anything visible on screen should be picked
	//   up whether it prints or not.
	ErrorCode drew;
	{
		KCMRasterizingGuard rg;	// RAII: no marks are drawn into this Draw by re-entry
		drew = snap->Draw(IShape::kPreviewMode, kFalse /*fullRes*/, 0.0 /*greek off*/, kFalse /*AA off*/);
	}
	AGMImageAccessor* acc = (drew == kSuccess) ? snap->CreateAGMImageAccessor() : nil;

	bool16 ok = kFalse;
	if (acc != nil)
	{
		Int32Rect b = acc->GetBounds();
		const int32 w = b.right - b.left, h = b.bottom - b.top;
		const int32 rb = (int32)acc->GetRowBytes();
		const int32 bpp = (int32)acc->GetBitsPerPixel() / 8;
		const uint8* base = acc->GetBaseAddr();
		if (base != nil && w > 0 && h > 0 && rb > 0 && bpp >= 4)
		{
			const int32 cx = w / 2, cy = h / 2;	// the centre pixel is the clicked point
			const uint8* px = base + (size_t)cy * rb + (size_t)cx * bpp;
			out[0] = px[0]; out[1] = px[1]; out[2] = px[2]; out[3] = px[3];	// C, M, Y, K from offset 0
			ok = kTrue;
		}
		delete acc;
	}
	delete snap;
	return ok;
}

// Append a value as exactly three digits, zero-padded (the callers pass CMYK percentages, 0..100).
// This is what lines the Target's and the Source's C/M/Y/K up vertically. AppendNumber does not
// zero-pad, hence the digit-by-digit output; anything out of range is clamped to 0..999.
static void KCMAppend3(PMString& s, int32 v)
{
	if (v < 0)   v = 0;
	if (v > 999) v = 999;
	s.AppendNumber(v / 100);
	s.AppendNumber((v / 10) % 10);
	s.AppendNumber(v % 10);
}

// Convert a CMYK raster's 8-bit value (0..255) to the percentage CMYK is really expressed in
// (0..100), rounded: 255 -> 100, 0 -> 0, 128 -> 50. (v*100+127)/255 does the rounding.
static int32 KCMByteToPct(uint8 v)
{
	return ((int32)v * 100 + 127) / 255;
}

// Append "C000 M000 Y000 K000" (three zero-padded digits each, 0..100%). For the cursor, where the
// heading row is drawn separately as graphics, so leaving the letters off each value keeps the
// text narrow.
static void KCMAppendCmyk(PMString& s, const uint8 c[4])
{
	KCMAppend3(s, KCMByteToPct(c[0]));
	s.Append(" "); KCMAppend3(s, KCMByteToPct(c[1]));
	s.Append(" "); KCMAppend3(s, KCMByteToPct(c[2]));
	s.Append(" "); KCMAppend3(s, KCMByteToPct(c[3]));
}

// Append "C 000 M 000 Y 000 K 000", with the heading letter attached to each value. For the panel.
// **The panel's status line is a proportional font** (kPaletteWindowFontId; the SDK offers no
// monospaced alternative), so a separate heading row could never line up with a row of numbers --
// letters and digits are different widths. Instead of aligning by coordinates the way the cursor
// does (KCMDrawColumns), each value carries its own letter, which removes the need to align at all.
static void KCMAppendCmykLabeled(PMString& s, const uint8 c[4])
{
	s.Append("C"); KCMAppend3(s, KCMByteToPct(c[0]));
	s.Append(" M"); KCMAppend3(s, KCMByteToPct(c[1]));
	s.Append(" Y"); KCMAppend3(s, KCMByteToPct(c[2]));
	s.Append(" K"); KCMAppend3(s, KCMByteToPct(c[3]));
}

// Alt + left click with the tool: sample the raw CMYK at **the given point** on the hovered
// document and on its counterpart, and build "...000 t\n...000 s" into outCursor and outPanel
// (three zero-padded digits per value, the hovered side always first). kTrue on success.
//   The hovered page's counterpart is resolved through the pairing. The point is transformed back
//   into inner (page) coordinates, then forward into each document's spread coordinates, and a
//   tiny area of each page is rasterised -- which assumes the two versions share their geometry.
//
// **This samples the point it is given, not "wherever the mouse is".** Three steps that used to
//   live here -- find the view under the mouse, check that its document is hoverDB, read the
//   mouse's content coordinates -- **belong to the caller (the UI)**: a question about windows has
//   no answer without windows, and a UI plug-in's boss is invisible (nil) on a background thread.
// @warning **the second of those, the window identity check, moved rather than disappeared.**
//   Without it, dragging from the window that was pressed into another one reads that window's
//   coordinates as hoverDB's page coordinates. Both callers in KCMCmykCursor.cpp apply it before
//   arriving here.
bool16 KCMSampleCmykAt(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
                         const PMReal& mx, const PMReal& my,
                         UID viewSpreadUID,
                         PMString& outPanel, PMString& outCursor)
{
	if (hoverDB == nil)
		return kFalse;
	// otherDB == nil is a solo pick with no counterpart: read the hovered side only and report one
	// unlabelled line of CMYK. That is the case when no comparison is running, and also when one
	// is but the mouse is over some third document that has nothing to do with it.
	const bool16 solo = (otherDB == nil);

	// Which page the point is on (and its flat index), through the shared KCMFindPageUnderMouse.
	KCMPageHit hit;
	if (!KCMFindPageUnderMouse(hoverDB, mx, my, hit, viewSpreadUID))
		return kFalse;

	const UID hPageUID = hit.hitPageUID;

	// Clicked point (pasteboard) -> inner (page) coordinates -> the hovered document's spread
	// coordinates. The same path in solo mode and in comparison mode.
	InterfacePtr<IGeometry> hGeo(hoverDB, hPageUID, UseDefaultIID());
	if (hGeo == nil)
		return kFalse;
	PMMatrix mPB = ::InnerToPasteboardMatrix(hGeo);
	if (mPB.IsSingular())
		return kFalse;
	PMPoint inner(mx, my);
	mPB.Inverse().Transform(&inner);
	PMPoint hSpreadPt(inner.X(), inner.Y());
	::InnerToSpreadMatrix(hGeo).Transform(&hSpreadPt);

	uint8 cH[4];
	if (!KCMReadCmykPixel(UIDRef(hoverDB, hPageUID), hSpreadPt, cH))
		return kFalse;

	// ---- Solo mode: one line, the hovered side's CMYK. The cursor's KCMSplitTwoLines skips an
	//      empty second line by itself, so handing it one line gives the "C M Y K" heading plus
	//      that line. ----
	if (solo)
	{
		outCursor.SetTranslatable(kFalse);
		KCMAppendCmyk(outCursor, cH);			// "C.. M.. Y.. K.." with no t/s: one document only
		outPanel.SetTranslatable(kFalse);
		KCMAppendCmykLabeled(outPanel, cH);	// "C .. M .. Y .. K .." with no t/s
		return kTrue;
	}

	// ---- Comparison mode (over either window while a comparison runs): resolve the hovered page's
	//      counterpart through the pairing and read that too. While dragging, the cache built at
	//      press time answers instead. A page that is not in the pairing -- registered, or past the
	//      end -- has no counterpart and no value. ----
	UID oPageUID;
	if (sDragCacheActive && hoverDB == sDragCacheHoverDB && otherDB == sDragCacheOtherDB)
	{
		std::map<UID, UID>::const_iterator it = sDragCacheH2O.find(hPageUID);
		if (it == sDragCacheH2O.end())
			return kFalse;
		oPageUID = it->second;
	}
	else
	{
		// No cache: a one-off sample outside a press. The pairing always takes its arguments in
		// (targetDB, sourceDB) order.
		const bool16 mapped = hoverIsTarget
			? KCMMapTargetToSource(hoverDB, otherDB, hPageUID, oPageUID)
			: KCMMapSourceToTarget(otherDB, hoverDB, hPageUID, oPageUID);
		if (!mapped)
			return kFalse;
	}
	InterfacePtr<IGeometry> oGeo(otherDB, oPageUID, UseDefaultIID());
	if (oGeo == nil)
		return kFalse;
	PMPoint oSpreadPt(inner.X(), inner.Y());
	::InnerToSpreadMatrix(oGeo).Transform(&oSpreadPt);

	uint8 cX[4];
	if (!KCMReadCmykPixel(UIDRef(otherDB, oPageUID), oSpreadPt, cX))
		return kFalse;

	// The suffixes: first line the hovered side, second line its counterpart. Pressing in the
	// Target window gives "t" then "s", pressing in the Source window "s" then "t" -- the hovered
	// window's side goes on top.
	const char* const hoverLabel = hoverIsTarget ? " t" : " s";
	const char* const otherLabel = hoverIsTarget ? " s" : " t";

	// Every value is converted from the raster's 8 bits (0..255) to the percentage CMYK is really
	// expressed in, and zero-padded to three digits so the columns line up.
	// outCursor = the two rows of numbers only; the bitmap cursor draws the "C M Y K" heading itself.
	outCursor.SetTranslatable(kFalse);
	KCMAppendCmyk(outCursor, cH); outCursor.Append(hoverLabel);
	outCursor.AppendW(UTF32TextChar(0x0A));	// newline: on to the second line
	KCMAppendCmyk(outCursor, cX); outCursor.Append(otherLabel);

	// outPanel = each value with its own heading letter (KCMAppendCmykLabeled), plus the t/s.
	// **A separate heading row was tried and abandoned**: the panel's status area uses a
	// proportional font (kPaletteWindowFontId), so columns cannot be made to line up. Attaching the
	// letter to each value cannot come apart, whatever the font's widths are.
	outPanel.SetTranslatable(kFalse);
	KCMAppendCmykLabeled(outPanel, cH); outPanel.Append(hoverLabel);
	outPanel.AppendW(UTF32TextChar(0x0A));
	KCMAppendCmykLabeled(outPanel, cX); outPanel.Append(otherLabel);
	return kTrue;
}
