//========================================================================================
//
//  KCMCheckGlyph.h
//
//  Shared drawing of the KCM check mark (✓) into an IGraphicsPort: a white halo stroke
//  under a black body, at fixed logical (1x px) coordinates whose vertex (10,18) is the
//  cursor hotspot. Used by the Alt+left CMYK readout cursor, which keeps the ✓ on top of the
//  numbers (KCMCmykCursor.cpp - it was KCMPeek.cpp until the 2026-08-13 split; corrected
//  2026-08-17, audit B-U6). The caller owns the buffer clear and setopacity; this only strokes
//  the glyph.
//
//  ★★**Change the shape here and the PNGs have to be regenerated.** The always-on check cursor of
//  the tool stopped using a drawing callback (to cure the rubbish seen on press) and became PNG
//  resources (KCM_Check_10_18.png / KCM_CheckOff_10_18.png, with @2x and @3to2x), so **the artwork
//  now has two sources: this function and those files**. The PNGs are generated with the same
//  geometry as this function (vertices, stroke widths, round caps) by
//  work/kescm-make-check-cursor.ps1 (its argument is the output folder).
//  Put the results in source/sdksamples/KCM/ui/ and **touch KCMUI.fr before building** ---- replacing
//  a PNG alone does not make ODFRC run, and the old image stays linked in (a known trap).
//
//  Header-only inline (no .cpp / no build-system change): the caller already includes
//  IGraphicsPort.h, and inlining keeps that true if a second translation unit ever picks it up again.
//  ⚠This paragraph used to say "**both callers** ... across the **two** translation units". **There
//    has been exactly one caller** (KCMCmykCursor.cpp) since the day the check cursor became a PNG
//    resource and stopped having a callback. **Fourteen lines further down the same file** said "the
//    caller today is ... exactly one (counted by grepping)", and **the person who counted did not fix
//    this paragraph** ---- the nearer the sibling, the more likely it is to be left.
//
//========================================================================================
#ifndef __KCMCheckGlyph_h__
#define __KCMCheckGlyph_h__

#include "IGraphicsPort.h"
#include "PMReal.h"
#include "ICursorUtils.h"	// QueryGraphicsPortForBitmap(KCMCursorBitmapFinish)
#include "Utils.h"
#include <cstring>			// std::memset (the transparent clear in KCMCursorBitmapBegin)

//----------------------------------------------------------------------------------------
// The preamble shared by cursor-bitmap callbacks (about sixteen duplicated lines from two callbacks,
// gathered here).
// ★**There is exactly one caller today, KCMCmykCursor.cpp** (counted by grepping in full).
//   Of the two that existed when this was gathered, one - the check cursor
//   (KCMCursorProvider.cpp) - **became a PNG resource and stopped having a callback at all**, and
//   the other moved from KCMPeek.cpp to KCMCmykCursor.cpp with the model/UI split ---- that is,
//   **neither of the two named here is what it was**.
//   ⚠It is not un-gathered even so: these two steps (Begin / Finish) ARE the procedure for a cursor
//     bitmap - clear to transparent, settle the size, obtain the AGM port - and they are the way in
//     for the next dynamic cursor.
// The order: Begin (clear the whole allocation to transparent and answer the logical maximum size)
//       -> the caller decides the logical size -> Finish (clamp it, set the output size, hand back
//       the AGM port).
//----------------------------------------------------------------------------------------

/** 1) Clear the whole allocated buffer to transparent (ARGB = 0) and answer the logical maximum
	size (in 1x px). QueryGraphicsPortForBitmap does not erase what is already there, so this is what
	keeps rubbish out of the pixels nothing draws into.
	allocW/allocH = the real size the caller allocated (twice the logical one when hiRes). */
inline void KCMCursorBitmapBegin(uchar* buffer, uint32 allocW, uint32 allocH, bool16 hiRes,
                                   uint32& outMaxLogW, uint32& outMaxLogH)
{
	const uint32 scale = hiRes ? 2u : 1u;
	outMaxLogW = allocW / scale;
	outMaxLogH = allocH / scale;
	std::memset(buffer, 0, (size_t)allocW * (size_t)allocH * 4u);
}

/** 2) Clamp the logical size to the maximum, settle *width / *height / *hasAlpha, and hand back the
	AGM port (already AddRef'd - the caller receives it into an InterfacePtr; nil on failure). Drawing
	is done in logical coordinates (1x px) and the port absorbs the hiRes scale. */
inline IGraphicsPort* KCMCursorBitmapFinish(uchar* buffer, uint32* width, uint32* height, bool16* hasAlpha,
                                              bool16 hiRes, uint32 logW, uint32 logH,
                                              uint32 maxLogW, uint32 maxLogH)
{
	if (logW > maxLogW) logW = maxLogW;
	if (logH > maxLogH) logH = maxLogH;
	const uint32 scale = hiRes ? 2u : 1u;
	const uint32 actW = logW * scale;
	const uint32 actH = logH * scale;
	*width    = actW;
	*height   = actH;
	*hasAlpha = kTrue;
	return Utils<ICursorUtils>()->QueryGraphicsPortForBitmap(buffer, actW, actH, kTrue /*hasAlpha*/, hiRes);
}

/** Stroke the ✓ into gPort (halo stroke under a thinner body stroke). Coordinates match the
	.fr HOTC for kKCMCheckCursorResID so the bend sits on the cursor hotspot / click point.
	Two color schemes (2026-07-15, user-specified):
	  - active   (default): white halo + black body — over the armed Target where the tool works.
	  - inactive (inverted): black halo + white body (a white fill with a black rim) - everywhere else,
	    meaning "the tool does nothing here". A gray body was tried first but was hard to tell apart.
	@param bodyGray body stroke gray level (0.0 = black default, 1.0 = white for inactive)
	@param haloGray halo stroke gray level (1.0 = white default, 0.0 = black for inactive)
	@param haloWidth halo (rim) stroke width. Default 4.2 gives a ~0.9px visible rim over the
	       2.4px body (★thickened from 3.5, which showed as 0.55px, at the user's request. It must
	       stay equal to the active width in work/kescm-make-check-cursor.ps1, which generates the
	       PNGs). The inactive
	       (black-rimmed) cursor passes a slightly larger value because a dark rim reads thinner
	       than a light one at the same width (irradiation illusion), so it needs to be a touch
	       wider to look the same thickness (user report 2026-07-15). */
inline void KCMDrawCheckGlyph(IGraphicsPort* gPort,
                                const PMReal& bodyGray = PMReal(0.0),
                                const PMReal& haloGray = PMReal(1.0),
                                const PMReal& haloWidth = PMReal(4.2))
{
	if (gPort == nil)
		return;

	const PMReal ax( 5.0), ay(12.0);	// short arm tip (upper-left)
	const PMReal bx(10.0), by(18.0);	// vertex (bend) = hotspot / click point
	const PMReal cx(20.0), cy( 5.0);	// long arm tip (upper-right)

	gPort->setlinecap(1);	// round cap
	gPort->setlinejoin(1);	// round join

	gPort->setrgbcolor(haloGray, haloGray, haloGray);	// halo (white default: readable on any background)
	gPort->setlinewidth(haloWidth);
	gPort->newpath();
	gPort->moveto(ax, ay);
	gPort->lineto(bx, by);
	gPort->lineto(cx, cy);
	gPort->stroke();

	gPort->setrgbcolor(bodyGray, bodyGray, bodyGray);	// body (black default / white when inactive)
	gPort->setlinewidth(PMReal(2.4));
	gPort->newpath();
	gPort->moveto(ax, ay);
	gPort->lineto(bx, by);
	gPort->lineto(cx, cy);
	gPort->stroke();
}

#endif // __KCMCheckGlyph_h__
