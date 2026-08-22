//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The three things every hand-drawn text widget in this panel has to agree about: how far the
//  CONTEXT around a change is faded toward the background, how that faded colour is worked out, and
//  (since 2026-08-22) where a RUBY READING sits over the characters it belongs to.
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

/** Where a READING starts, given where its base characters were actually drawn (2026-08-22).

	★RUBY IS CENTRED ON WHAT IT BELONGS TO. A short reading sits in the middle of the word rather
	than at its left edge, and a reading wider than its base characters overhangs on both sides -
	both of which is what real ruby does, and what the user asked for ("位置が重要").

	★★SHARED FOR THE SAME REASON kKESCMContextTextWeight IS. Two widgets now draw a reading over
	base text: the change ROW's cell (KESCMStoryCellView.cpp) shows the newer version's reading, and
	the panel's MESSAGE AREA (KESCMStatusTextView.cpp) shows the older one - which is the only place
	a reading that was REMOVED can be seen at all. Two copies of this rule would drift apart the
	first time one was adjusted ([[one-question-one-place]]), and adjusting it is exactly what is
	expected: the position is the part the user will look at first.

	⚠WHAT IS NOT SHARED is what happens when the reading is too wide for what remains: the row's
	cell has one line and one right edge, while the message area has wrapped lines, so each ends it
	in its own terms. Only the starting point is one question.

	@param baseX where the base characters were drawn.
	@param baseW how wide they came out. May be 0 - a change with nothing on this side - and then
		the reading simply starts at baseX.
	@param rubyW how wide the reading is.
	@param leftLimit the left edge of the box. The reading may overhang its base characters but
		never the COLUMN: past this it would paint over a neighbouring cell.
	@return the x to draw the reading at. */
inline PMReal KESCMRubyX(const PMReal& baseX, const PMReal& baseW, const PMReal& rubyW,
						 const PMReal& leftLimit)
{
	const PMReal x = baseX + (baseW - rubyW) / PMReal(2.0);
	return (x < leftLimit) ? leftLimit : x;
}

#endif // __KESCMPanelTextDraw_h__

// End, KESCMPanelTextDraw.h.
