//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The three things every hand-drawn text widget in this panel has to agree about: how far the
//  CONTEXT around a change is faded toward the background, how that faded colour is worked out, and
//  (since 2026-08-22) where a RUBY READING sits over the characters it belongs to.
//
//  ★WHY THIS FILE EXISTS. Two widgets now draw "changed characters at full strength, the words
//  around them faded": the change ROW's text cell (KCMStoryCellView.cpp) and the panel's MESSAGE
//  AREA (KCMStatusTextView.cpp), which shows the other side of the same edit. If each kept its
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

#ifndef __KCMPanelTextDraw_h__
#define __KCMPanelTextDraw_h__

// Interface includes:
#include "IGraphicsPort.h"		// setrgbcolor / rectfill - the caret is filled, not written
#include "IInterfaceColors.h"	// RealAGMColor

// General includes:
#include "PMReal.h"
#include "PMString.h"			// the caret's placeholder

/** The two flags every draw and every measure passes, spelled out rather than left to a default.
	★★**THE DEFAULTS IN DrawStringUtils.h DISAGREE**: the draw calls default to kFalse, the measure
	and ellipsize calls to kTrue ⇒ taking them would **measure a string differently from how it is
	drawn**, which is precisely what these two widgets must not do.
	⚠'&' has to survive verbatim either way: one box shows **document text**, the other **full save
	  paths**, and a folder called "Q&A資料" lost its ampersand until the `.fr` cell this replaced
	  set the same flag (KCMUI.fr). */
const bool16 kKCMDontConvertAmpersand = kFalse;
const bool16 kKCMNoUnderline = kFalse;

/** How much of the theme's text colour the CONTEXT keeps. 0 = the background itself (invisible),
	1 = the full text colour (no fade at all).

	★The same 0.65 KBS settled on - half and half made the surrounding words harder to read than
	they needed to be, and the change still stands out at this weight (user's call there,
	there; "follow KBS" here). */
const double kKCMContextTextWeight = 0.65;

/** Linear blend of two RGB colours (t = 0 -> bg, t = 1 -> fg).
	RealAGMColor's components are PMReal, hence the ToDouble on the way back into its constructor. */
inline RealAGMColor KCMBlendColor(const RealAGMColor& bg, const RealAGMColor& fg, const PMReal& t)
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
		both of which is what real ruby does, and what the user asked for ("the position is what
		matters").

	★★SHARED FOR THE SAME REASON kKCMContextTextWeight IS. Two widgets now draw a reading over
	base text: the change ROW's cell (KCMStoryCellView.cpp) shows the newer version's reading, and
	the panel's MESSAGE AREA (KCMStatusTextView.cpp) shows the older one - which is the only place
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
inline PMReal KCMRubyX(const PMReal& baseX, const PMReal& baseW, const PMReal& rubyW,
						 const PMReal& leftLimit)
{
	const PMReal x = baseX + (baseW - rubyW) / PMReal(2.0);
	return (x < leftLimit) ? leftLimit : x;
}


/** ★**THE PLACE WORDS LEFT, OR WHERE THEY WENT IN** - the bar this panel draws when a change has
	a place on this side and no characters (2026-09-08, user's request).

	A DELETION's row shows the NEWER version, where the removed words simply are not there: the
	context closes up and nothing says WHERE. The mirror case is an INSERTION seen from the message
	area, which shows the OLDER version. Both are "a place, and nothing to show" - the same fact the
	marks on the page have always drawn as a caret (KCMStoryMarkBuild turns a zero-width range into
	KCMMarkRange::Caret). The panel was the one place without it.

	⚠**TEXT CHANGES ONLY** (user's call, the same day): a ruby or kenten change keeps its base
	 characters on both sides, so there is nothing missing to point at - those are left exactly as
	 they were.

	★**NOTHING IS ADDED TO ANY STRING.** The bar is DRAWN; the placeholder below only reserves the
	room, so nothing measured, selected or copied ever gains a character - the rule
	KCMStoryDiffRun's MarkUpBreaks states for its ¶ and ⚓.
	★★SHARED BY THE TWO WIDGETS for the reason this header exists: the change ROW draws one side and
	the MESSAGE AREA the other, and a bar that looked different in the two would read as two things.

	★**HOW WIDE IT IS DRAWN.**
	★**1.0, NOT 2.0** - shown at 2.0 first, the reader asked for it thinner, and confirmed 1.0 on
	screen ("it looks right now", 2026-09-08). Both numbers were seen before this one was settled.
	⚠**IT NEVER LANDS ON A LINE THAT CARRIES A READING, AND THAT IS TWO CONDITIONS MEETING RATHER
	 THAN ONE RULE.** The bar is drawn only for a TEXT change (attrKind == kKCMStoryAttrNone), and a
	 text change carries no reading - so the ruby pass in each widget, guarded by !ruby.IsEmpty(),
	 never runs on a line that has one.
	 ⇒ **If a bar is ever wanted for an ATTRIBUTE change, that guard stops being enough**: the ruby
	   pass looks for the run marked "these are the changed characters", and the bar's placeholder is
	   marked exactly that way - it has to be, so that the layout treats it as the change. */
const PMReal kKCMCaretWidth(1.0);

/** The room the bar stands in.
	★**ONE SPACE, NOT AN EMPTY STRING**: the message area wraps its text run by run, and an empty run
	is indistinguishable from "nothing left to place" there (KCMAnythingLeft). A space is carried
	through the wrap like any other text, and the bar is drawn over it instead of it. */
inline PMString KCMCaretPlaceholder()
{
	PMString s(" ");
	s.SetTranslatable(kFalse);
	return s;
}

/** Draw the bar, centred in the room the placeholder reserved.
	@param x the left edge of that room, @param roomW how wide it came out.
	@param top the top of the line's box and @param height its height - the bar spans the WHOLE of
	       it, which is what makes it read as "between these two characters" rather than as a
	       character of its own. */
inline void KCMDrawCaret(IGraphicsPort* gPort, const RealAGMColor& colour,
						 const PMReal& x, const PMReal& roomW,
						 const PMReal& top, const PMReal& height)
{
	if (gPort == nil || height <= PMReal(0.0))
		return;

	const PMReal left = x + (roomW - kKCMCaretWidth) / PMReal(2.0);
	gPort->setrgbcolor(colour.red, colour.green, colour.blue);
	gPort->rectfill(left, top, kKCMCaretWidth, height);
}
#endif // __KCMPanelTextDraw_h__

// End, KCMPanelTextDraw.h.
