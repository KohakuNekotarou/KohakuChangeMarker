//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The two things every hand-drawn text widget in this panel has to agree about: how far the
//  CONTEXT around a change is faded toward the background, and how that faded colour is worked out.
//
//  ★WHY THIS FILE EXISTS. Two widgets now draw "changed characters at full strength, the words
//  around them faded": the change ROW's text cell (KESCMStoryCellView.cpp) and the panel's MESSAGE
//  AREA (KESCMStatusTextView.cpp), which shows the other side of the same edit. If each kept its
//  own 0.65 and its own blend, the two would answer the same question in two places and would
//  drift apart the first time one of them was tuned ([[one-question-one-place]]).
//
//  ★HEADER-ONLY ON PURPOSE. What is shared is one constant and one pure function of three numbers
//  - no state, no SDK object to hold. A .cpp for that would buy a translation unit and nothing else.
//  ⚠What is NOT shared is the colour LOOKUP: the row cell asks whether its row is hilited and
//    switches both colours to the selection pair, and the message area is never hilited, so the two
//    ask IInterfaceColors different questions. Sharing the answer to a question they do not both
//    ask is how a helper starts growing flags.
//
//========================================================================================

#ifndef __KESCMPanelTextDraw_h__
#define __KESCMPanelTextDraw_h__

// Interface includes:
#include "IInterfaceColors.h"	// RealAGMColor

// General includes:
#include "PMReal.h"

/** How much of the theme's text colour the CONTEXT keeps. 0 = the background itself (invisible),
	1 = the full text colour (no fade at all).

	★The same 0.65 KBS settled on - half and half made the surrounding words harder to read than
	they needed to be, and the change still stands out at this weight (user's call there,
	2026-08-02; "KBS を参考に" here, 2026-08-20). */
const double kKESCMContextTextWeight = 0.65;

/** Linear blend of two RGB colours (t = 0 -> bg, t = 1 -> fg).
	RealAGMColor's components are PMReal, hence the ToDouble on the way back into its constructor. */
inline RealAGMColor KESCMBlendColor(const RealAGMColor& bg, const RealAGMColor& fg, const PMReal& t)
{
	const PMReal u = PMReal(1.0) - t;
	return RealAGMColor(
		ToDouble(bg.red   * u + fg.red   * t),
		ToDouble(bg.green * u + fg.green * t),
		ToDouble(bg.blue  * u + fg.blue  * t));
}

#endif // __KESCMPanelTextDraw_h__

// End, KESCMPanelTextDraw.h.
