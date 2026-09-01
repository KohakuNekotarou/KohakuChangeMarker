//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Draws one emphasis mark (kenten) as a SHAPE, for the upper line of a change row.
//
//  ★★★WHY A SHAPE AND NOT A CHARACTER (user's call, 2026-09-01: "if it is possible I want to draw
//  the mark itself - there are similar ones in text too, ○ and ● and so on... is there a double
//  circle?"). There are indeed characters that look like these marks - ● ○ ◎ ◉ ▲ △ - and drawing
//  them would be one line instead of this file. Two things decided against it:
//
//    1. ⚠**A NON-ASCII LITERAL THAT EXISTS IN CP932 IS THE DANGEROUS KIND.** One that does not
//       exist fails the build and is fixed in a minute; one that does compiles cleanly and comes
//       out as the wrong character at RUN time, on the user's screen, in a panel nobody rebuilds
//       for months. ● ○ ◎ ▲ △ are all in CP932. (This plug-in has met that exact fault before -
//       see the memory note behind [[cpp-japanese-needs-bom]].)
//    2. **THE SESAME DOTS HAVE NO SAFE CHARACTER AT ALL.** U+FE45/FE46 are the real ones and the
//       UI font is not required to carry them, so those two kinds would have shown as a blank or a
//       tofu box - and they are the DEFAULT kind in InDesign, the one a reader meets first.
//
//  Drawing them removes both problems at once, and it also lets every kind be the same size and sit
//  on the same centre, which a font would not promise.
//
//  ⚠**CUSTOM IS NOT DRAWN HERE.** A custom kenten is whatever glyph the user picked, so there is no
//  shape this file could know; CanDraw answers kFalse for it and the caller writes its name instead.
//
//========================================================================================

#ifndef __KCMKentenMark_h__
#define __KCMKentenMark_h__

#include "PMTypes.h"		// bool16
#include "PMReal.h"

#include "PMString.h"

class IGraphicsPort;
class RealAGMColor;

namespace KCMKentenMark
{
	/** kTrue when this value names a mark Draw can actually paint.

		@param kindValue the kind as the reader put it in KCMAttrSpan::fValue - the SDK's own
		 spelling ("Bullseye", "BlackSesameDot"), or "Custom:<set>:<code>" for a user glyph.
		@return kFalse for Custom and for any kind this build does not know, both of which the
		 caller shows as text. **A kind that cannot be drawn is never nothing** - the characters
		 still carry a mark, and saying so in words beats drawing something that is not it. */
	bool16 CanDraw(const PMString& kindValue);

	/** Paints one mark, centred, inside a box `size` across.

		@param centreX/centreY where the middle of the mark goes, in the port's coordinates.
		@param size the width AND height available - the mark is drawn to fit it, so every kind
		 comes out the same size whatever shape it is.
		@param colour asked of the theme by the caller, never hardcoded here: the panel has to keep
		 working in a light UI and a dark one.
		@warning the port's colour, line width and path are all left as they were found (gsave /
		 grestore around the whole of it). A cell draws text before and after this call. */
	void Draw(IGraphicsPort* gPort, const PMString& kindValue,
			  const PMReal& centreX, const PMReal& centreY, const PMReal& size,
			  const RealAGMColor& colour);
}

#endif	// __KCMKentenMark_h__

// End, KCMKentenMark.h.
