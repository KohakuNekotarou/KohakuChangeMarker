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
//  ★★AND HOW THE TEXT IS DRAWN AT ALL (2026-10-24, brought over from KFC): by drover, in DV_Draw - see
//  kKCMTextSmoothing for why, and the drawing and measuring calls below for the one way every widget
//  here does it.
//
//========================================================================================

#ifndef __KCMPanelTextDraw_h__
#define __KCMPanelTextDraw_h__

// Interface includes:
#include "IControlView.h"		// IsHilited - KCMViewOrParentIsHilited
#include "IWidgetParent.h"		// QueryParentFor - a cell -> the row that carries the hilite
#include "IInterfaceColors.h"	// RealAGMColor

// General includes:
#include "DVAPublicIncludes.h"	// dvaui::drawbot - what every widget here draws its text with (DV_Draw)
#include "DVPublicUtilities.h"	// dv_utils::DVFillRect / FontInfoGetDVAFontMetrics (source/open/includes/widgets)
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB (drawbot) / PMMeasureStringDroverized / PMEllipsizeString
#include "PMReal.h"
#include "PMRect.h"
#include "PMString.h"			// the caret's placeholder
#include "WidgetDefs.h"			// EllipsizeStyle, kAlignLeft

/** The two flags every draw and every measure passes, spelled out rather than left to a default.
	★★**THE DEFAULTS IN DrawStringUtils.h DISAGREE**: the draw calls default to kFalse, the measure
	and ellipsize calls to kTrue ⇒ taking them would **measure a string differently from how it is
	drawn**, which is precisely what these two widgets must not do.
	⚠'&' has to survive verbatim either way: one box shows **document text**, the other **full save
	  paths**, and a folder called "Q&A資料" lost its ampersand until the `.fr` cell this replaced
	  set the same flag (KCMUI.fr). */
const bool16 kKCMDontConvertAmpersand = kFalse;
const bool16 kKCMNoUnderline = kFalse;

/** ★★DRAWN BY DROVER, AS INDESIGN'S OWN ROWS ARE (2026-10-24, brought over from KFC - memory
	panel-text-draw-in-dv-draw-cleartype). Every widget here draws its text in DV_Draw, with DrawStringUtils.h's drawbot
	overloads below - the shipping code's way (open/components/dynamicdocumentsui/TimingPanelTreeDDTarget.cpp).
	⛔NOT IN Draw WITH AN AGM GRAPHICS CONTEXT: Draw is handed an OFFSCREEN AGM port that DVAbstractControlView blits
	back onto the panel (DVAbstractControlView.h), and text drawn there comes out in black and white - KFC photographed
	it on the Bright theme beside the stock rows' ClearType (none of its inked pixels coloured, 70% of theirs), and its
	author found it "blurred, hard to read".
	⚠What still needs AGM is a SHAPE with a curve or a fill: the drawbot's public tools are rectangles and polylines
	(DVPublicUtilities.h), so the kenten marks are painted by an AGM pass (KCMMarkPassView, KCMKentenMark.h).
	This is the policy every drawbot text draw here is handed. */
const dv_utils::AntiAliasPolicy kKCMTextSmoothing = dv_utils::kAntiAliasPolicy_Med;

/** How wide `s` comes out as drawn here - drover's measure (the draws are drover's), with the flag the draws pass; 0
	for nothing. The one measure of every widget here that lays text out. */
inline PMReal KCMDrawnWidth(const PMString& s, const InterfaceFontInfo& font)
{
	if (s.IsEmpty())
		return PMReal(0.0);
	return StringUtils::PMMeasureStringDroverized(s, font, kKCMDontConvertAmpersand).X();
}

/** One run in a line's box, from its left edge, centred top to bottom by the draw itself - how a ONE-LINE cell draws
	(KFC's hit row: the same baseline as InDesign's own rows beside it). */
inline void KCMDrawInBox(dvaui::drawbot::Drawbot* drawbotP, const PMRect& box, const PMString& s,
						 const InterfaceFontInfo& font, const RealAGMColor& colour)
{
	if (drawbotP == nil || s.IsEmpty())
		return;
	StringUtils::PMDrawStringRGB(drawbotP, box, kAlignLeft, s, font, colour, kKCMDontConvertAmpersand,
								 kTrue /*centerVertically*/, kKCMNoUnderline, kKCMTextSmoothing);
}

/** One run on an explicit BASELINE - how a box whose lines step by the font's advance draws them (the message area).
	ON A WHOLE PIXEL: an ascent is not one (12.7 on a Japanese UI), and a baseline between two rows of pixels is drawn
	across both (KFC's message area). */
inline void KCMDrawAtBaseline(dvaui::drawbot::Drawbot* drawbotP, const PMReal& x, const PMReal& baseline,
							  const PMString& s, const InterfaceFontInfo& font, const RealAGMColor& colour)
{
	if (drawbotP == nil || s.IsEmpty())
		return;
	StringUtils::PMDrawStringRGB(drawbotP, PMPoint(x, ::Round(baseline)), kAlignLeft, s, font, colour,
								 kKCMDontConvertAmpersand, kKCMNoUnderline, kKCMTextSmoothing);
}

/** The font's own line, as drover answers it (drover draws it): the ascent, the descent and the ADVANCE - ascent +
	descent + leading, 18.0 on a Japanese UI where a measured "Ag" answers 19.0 (KCMStatusTextView.cpp has the
	measurement). The product's way of asking (TimingPanelTreeDDTarget.cpp).
	@return kFalse when drover refuses the font - then nothing could be laid out in it either. */
inline bool16 KCMFontLine(const InterfaceFontInfo& font, PMReal& outAdvance, PMReal& outAscent, PMReal& outDescent)
{
	float size = 0.0f, ascent = 0.0f, descent = 0.0f, leading = 0.0f;
	if (!dv_utils::FontInfoGetDVAFontMetrics(font, &size, &ascent, &descent, &leading))
		return kFalse;
	outAdvance = PMReal(ascent + descent + leading);
	outAscent = PMReal(ascent);
	outDescent = PMReal(descent);
	return (outAdvance > PMReal(0.0)) ? kTrue : kFalse;
}

/** Where text sits when it is centred top to bottom in a box, as a NUMBER - for the draws that need one (a line struck
	through, the layers' lines), which IWidgetUtils::GetViewYPosition answered from an AGM context DV_Draw has not. The
	ink (ascent + descent) centred in the box, on a whole pixel.
	⚠The one-line runs themselves go through KCMDrawInBox, which centres them by the draw's own rule; this is the same
	idea worked out by hand and may land a pixel away from it. A font drover refuses has no ascent to centre: the
	box's three-quarter line stands in. */
inline PMReal KCMCentredBaseline(const InterfaceFontInfo& font, const PMReal& top, const PMReal& height)
{
	PMReal advance(0.0), ascent(0.0), descent(0.0);
	if (!KCMFontLine(font, advance, ascent, descent))
		return ::Round(top + height * PMReal(0.75));
	return ::Round(top + (height - (ascent + descent)) / PMReal(2.0) + ascent);
}

/** StringUtils::PMEllipsizeString, never handed a width at which it does not come back (2026-10-24). Every KCM
	ellipsize goes through here.

	★★★THE SDK CALL NEVER RETURNS for a width a little wider than "...". Its loop (DV_WidgetBin.dll 21.0, read from
	  the disassembly): if the string does not fit, room = width - the width of "..."; cut characters and measure
	  again WHILE what is left is wider than room. Cut down to nothing, what is left is the empty string - which
	  measures 1, not 0, in the palette font. So for a room above 0 and below that 1, nothing can ever be wide
	  enough to stop it. Measured the same day: "" = 1, "..." = 15, and a capped copy of the loop swept over two
	  row strings in 0.25 px steps never ended at 15.25 - 15.75 and nowhere else. It froze InDesign in the
	  import-as-Source trial: the Track look (KCMStoryCellView.cpp) handed it 15.5 for a row's context - reproduced
	  by narrowing the panel to 377 px, a pixel at a time. Any width worked out by subtraction can land there.
	⇒ Below "..." plus the empty string (plus half a pixel) nothing but the dots can fit anyway: the answer is the
	  dots when they fit and nothing when they do not - what the SDK itself answers on either side of the band.
	Measured the way the SDK measures inside (the context-free PMMeasureString, convertAmpersand on for the dots and
	the empty string), so the band tested is the one its loop meets. @see memory pmellipsizestring-dead-band
	★THE GC-LESS CALL (2026-10-24, the DV_Draw port): DV_Draw has no graphics context to hand it, and the one with a
	context uses this one inside anyway (the same reading of DV_WidgetBin.dll). Whether `str` fits is asked of
	KCMDrawnWidth - how it comes out as drawn.
	⚠THE CUT IS MARKED "do not translate" AGAIN (KFC's finding): PMEllipsizeString hands back a NEW string, a PMString
	starts out translatable, and the draw translates what it is given - so document text that spells a key of the string
	table would come out as that key's translation. */
inline PMString KCMEllipsize(const PMReal& width, const PMString& str, const InterfaceFontInfo& fontInfo,
							 EllipsizeStyle where)
{
	if (str.IsEmpty() || KCMDrawnWidth(str, fontInfo) <= width)
		return str;
	PMString dots("...", PMString::kDontTranslateDuringCall);
	dots.Translate();
	const PMReal dotsW = StringUtils::PMMeasureString(dots, fontInfo, kTrue).X();
	const PMReal emptyW = StringUtils::PMMeasureString(PMString(), fontInfo, kTrue).X();
	if (width < dotsW + emptyW + PMReal(0.5))
	{
		PMString shown;
		if (width >= dotsW)
			shown = dots;
		shown.SetTranslatable(kFalse);
		return shown;
	}
	PMString cut = StringUtils::PMEllipsizeString(width, str, fontInfo, where, nil, kKCMDontConvertAmpersand);
	cut.SetTranslatable(kFalse);
	return cut;
}

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

	⚠**TEXT CHANGES** (user's call, the same day): a ruby or kenten change keeps its base
	 characters on both sides, so there is nothing missing to point at.
	★★**AND SINCE 2026-09-16 A MARK THAT IS NOT ON ONE SIDE** (the user's rule: a removed ruby is a
	 bar over the kanji, kenten and notes the same) - drawn on the UPPER line over the base
	 characters, and on the lower line too when that side has no characters for it to stand on.

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
inline void KCMDrawCaret(dvaui::drawbot::Drawbot* drawbotP, const RealAGMColor& colour,
						 const PMReal& x, const PMReal& roomW,
						 const PMReal& top, const PMReal& height)
{
	if (drawbotP == nil || height <= PMReal(0.0))
		return;

	// ★★ON WHOLE PIXELS (2026-09-16, the user: "are the bars all the same width? they look
	//   different"). They were: 1.0 wide everywhere. But centring put the left edge at x.5 as often as
	//   at a whole number, and a one-pixel fill that straddles two pixels is smoothed into two paler
	//   ones - so the same bar read as a thin dark line in one row and a wide grey one in the next.
	//   Rounding the left edge (and the top, for the same reason at the ends) makes every bar the
	//   same crisp pixel. ⚠At a UI scale above 100% a view unit is more than one pixel, so this
	//   cannot promise one device pixel there - only that every bar is drawn the same way.
	const PMReal left = ::Round(x + (roomW - kKCMCaretWidth) / PMReal(2.0));
	const PMReal y = ::Round(top);
	dv_utils::DVFillRect(drawbotP, colour, PMRect(left, y, left + kKCMCaretWidth, y + height));
}
/** True if this view, or a widget above it, is drawn hilited - i.e. this cell belongs to the row the user has
	selected. The tree applies the hilite to the ROW widget (CTreeViewWidgetMgr::ApplyNodeIDToWidget does it, "for
	hilite selection"), and a cell is one of that row's children, so a cell that only asked itself would never see the
	selection. ★Shared by the change row's text cell and the Track author row's colour square (moved here from
	KCMStoryCellView.cpp on 2026-10-05). */
const int32 kKCMHiliteParentSteps = 3;	// cell -> row is one step; the rest only keep it working behind a wrapper (KBS's same constant)

inline bool16 KCMViewOrParentIsHilited(const IControlView* view, int32 stepsLeft)
{
	if (view == nil)
		return kFalse;
	if (view->IsHilited())
		return kTrue;
	if (stepsLeft <= 0)
		return kFalse;
	InterfacePtr<IWidgetParent> parent(view, UseDefaultIID());
	if (parent == nil)
		return kFalse;
	InterfacePtr<IControlView> parentView((IControlView*)parent->QueryParentFor(IID_ICONTROLVIEW));
	return KCMViewOrParentIsHilited(parentView, stepsLeft - 1);
}

#endif // __KCMPanelTextDraw_h__

// End, KCMPanelTextDraw.h.
