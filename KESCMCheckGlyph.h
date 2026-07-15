//========================================================================================
//
//  KESCMCheckGlyph.h
//
//  Shared drawing of the KESCM check mark (✓) into an IGraphicsPort: a white halo stroke
//  under a black body, at fixed logical (1x px) coordinates whose vertex (10,18) is the
//  cursor hotspot. Used by two callback-drawn cursors that must show the identical shape:
//    - the tool's always-on ✓ cursor        (KESCMCursorProvider.cpp)
//    - the Alt+left CMYK readout cursor that keeps the ✓ on top (KESCMPeek.cpp)
//  The caller owns the buffer clear and setopacity; this only strokes the glyph.
//
//  Header-only inline (no .cpp / no build-system change): both callers already include
//  IGraphicsPort.h, and inlining avoids an ODR clash across the two translation units.
//
//========================================================================================
#ifndef __KESCMCheckGlyph_h__
#define __KESCMCheckGlyph_h__

#include "IGraphicsPort.h"
#include "PMReal.h"

/** Stroke the ✓ into gPort (halo stroke under a thinner body stroke). Coordinates match the
	.fr HOTC for kKESCMCheckCursorResID so the bend sits on the cursor hotspot / click point.
	Two color schemes (2026-07-15, user-specified):
	  - active   (default): white halo + black body — over the armed Target where the tool works.
	  - inactive (inverted): black halo + white body ("白の塗りに黒の縁") — everywhere else,
	    meaning "the tool does nothing here". A gray body was tried first but was hard to tell apart.
	@param bodyGray body stroke gray level (0.0 = black default, 1.0 = white for inactive)
	@param haloGray halo stroke gray level (1.0 = white default, 0.0 = black for inactive) */
inline void KESCMDrawCheckGlyph(IGraphicsPort* gPort,
                                const PMReal& bodyGray = PMReal(0.0),
                                const PMReal& haloGray = PMReal(1.0))
{
	if (gPort == nil)
		return;

	const PMReal ax( 5.0), ay(12.0);	// short arm tip (upper-left)
	const PMReal bx(10.0), by(18.0);	// vertex (bend) = hotspot / click point
	const PMReal cx(20.0), cy( 5.0);	// long arm tip (upper-right)

	gPort->setlinecap(1);	// round cap
	gPort->setlinejoin(1);	// round join

	gPort->setrgbcolor(haloGray, haloGray, haloGray);	// halo (white default: readable on any background)
	gPort->setlinewidth(PMReal(3.5));
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

#endif // __KESCMCheckGlyph_h__
