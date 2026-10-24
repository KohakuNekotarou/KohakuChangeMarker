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
//  ★★THE SHAPES ARE PAINTED BY AGM, THE TEXT AROUND THEM BY DROVER (2026-10-24, the DV_Draw port -
//  KCMPanelTextDraw.h's kKCMTextSmoothing says why the text moved). The drawbot's public tools fill a
//  rectangle and stroke a polyline and nothing more (DVPublicUtilities.h), so a circle, a filled
//  triangle or a sesame dot still needs the port's curves. ⇒ A view plans its marks (PlanOverRun),
//  has them painted by an AGM pass FIRST (KCMMarkPassView, below) and then draws its text.
//
//========================================================================================

#ifndef __KCMKentenMark_h__
#define __KCMKentenMark_h__

#include "PMTypes.h"		// bool16
#include "PMReal.h"

#include "PMString.h"

#include "DVAPublicIncludes.h"		// dvaui::drawbot - a custom mark is WRITTEN, by drover
#include "DVControlView.h"			// KCMMarkPassView's base
#include "IInterfaceColors.h"		// RealAGMColor - a plan carries its colour
#include "IInterfaceFonts.h"		// InterfaceFontInfo - the palette font a caller already holds

#include <vector>

class IGraphicsPort;

namespace KCMKentenMark
{
	/** kTrue when this value names a mark Draw can actually paint.

		@param kindValue the kind as the reader put it in KCMAttrSpan::fValue - the SDK's own
		 spelling ("Bullseye", "BlackSesameDot"), or "Custom:<set>:<code>" for a user glyph.
		@return kFalse for Custom and for any kind this build does not know, both of which the
		 caller shows as text. **A kind that cannot be drawn is never nothing** - the characters
		 still carry a mark, and saying so in words beats drawing something that is not it. */
	bool16 CanDraw(const PMString& kindValue);

	/** The GLYPH a custom kenten uses, or an empty string when this is not a custom one.

		★A CUSTOM MARK IS SHOWN AS ITSELF. There is no shape this file could know for it - the user
		picked the character - so the character is what goes over the base text, drawn as text where
		the other ten are drawn as paths. The reader then sees the mark they chose, in the place the
		mark belongs.
		@warning **NEVER SHOW THE WHOLE VALUE.** It reads "Custom:X", and the panel was seen showing
		 a reader "Custom:0:8251" before this existed. The value is for the comparison; this is for
		 the eye. */
	PMString CustomCharacter(const PMString& kindValue);

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

	/** The marks of one changed run, worked out once and drawn by two hands (2026-10-24): the shapes PAINTED by AGM
		(PaintShapes, from KCMMarkPassView's pass), a custom mark WRITTEN by drover (WriteGlyphs). */
	struct Marks
	{
		int32				fShape;		///< what every mark is painted as (KCMKentenMark.cpp's Shape); 0 - nothing to paint
		PMString			fGlyph;		///< a custom mark's character - written, not painted; empty otherwise
		PMReal				fGlyphW;	///< how wide that character comes out
		std::vector<PMReal>	fCentreX;	///< one per mark that fits, left to right
		PMReal				fCentreY;
		PMReal				fSize;
		PMReal				fRight;		///< the right edge the plan was held inside
		RealAGMColor		fColour;

		Marks() : fShape(0), fGlyphW(0.0), fCentreY(0.0), fSize(0.0), fRight(0.0), fColour(0.0, 0.0, 0.0) {}

		/** kTrue when there is a shape to paint - the only case a view needs its AGM pass for. */
		bool16 HasShapes() const { return (fShape != 0 && !fCentreX.empty()) ? kTrue : kFalse; }
	};

	/** The whole upper line for one changed run: ONE MARK PER CHARACTER, centred over each.

		★★THE TWO PLACES THAT SHOW A KENTEN CALL THIS SAME FUNCTION - the change row's cell and the
		panel's message area (user's call, 2026-09-01: "use the same code where it can be the
		same"). They draw the same thing about the same document, so a second copy of this
		arithmetic would be a second thing to keep right, and the day they drifted apart the panel
		would be showing one answer in two shapes ([[one-question-one-place]]).

		★ONE PER CHARACTER IS WHAT KENTEN IS. A single mark standing for a whole span would
		misdescribe every span longer than one character - which is the difference between an
		emphasis mark and a ruby.

		@param runLeft/runWidth where the changed characters actually landed, which the caller only
		 knows after laying its line out.
		@param charCount how many characters are under that width - **the characters that were
		 DRAWN**, not the span's length in the document: a caller that cut its text to fit would
		 otherwise draw more marks than there are characters beneath them.
		@param lineHeight how tall the upper line is. **SIZE ONLY** - markCentreY says where.
		@param markCentreY the y the drawn marks are centred on.
		 ⚠**THE CALLER WORKS THIS OUT, AND THAT IS THE POINT.** It was computed here as lineHeight/2,
		 which is right in a cell whose whole height IS the two lines and wrong in the message area,
		 where the upper line can be the fourth line down - the marks landed on the box's heading
		 (seen 2026-09-01: "Source T●●").
		@param rightEdge marks that would cross it are dropped rather than clipped.
		@param out [out] the plan - nothing is drawn here. A shape plan is painted by KCMMarkPassView's
		 pass, a custom mark written by WriteGlyphs; a custom mark is centred top to bottom in its
		 line's box there, as the text around it is (it was written on a baseline until 2026-10-24).
		@return kFalse when this value is neither a known kind nor a custom glyph - **then the
		 caller shows the name instead**, which is still true and still readable. kTrue otherwise,
		 including when there was simply nothing to draw over (a removed kenten has no characters
		 on the newer side, exactly as a removed ruby has no reading). */
	bool16 PlanOverRun(const InterfaceFontInfo& font, const PMString& kindValue,
					   const PMReal& runLeft, const PMReal& runWidth, int32 charCount,
					   const PMReal& lineHeight, const PMReal& markCentreY,
					   const PMReal& rightEdge, const RealAGMColor& colour, Marks& out);

	/** Paint a plan's shapes on an AGM port - KCMMarkPassView's pass is the one caller. */
	void PaintShapes(IGraphicsPort* gPort, const Marks& marks);

	/** Write a custom mark's character over each base character, by drover, centred top to bottom in the
		upper line's box (lineTop..lineBottom). A plan without a glyph writes nothing. */
	void WriteGlyphs(dvaui::drawbot::Drawbot* drawbotP, const InterfaceFontInfo& font, const Marks& marks,
					 const PMReal& lineTop, const PMReal& lineBottom);
}

/** A cell that draws its text by drover and has its kenten SHAPES painted by AGM first (2026-10-24) - the change row's
	text cell and the panel's message area, the two places a kenten is shown.

	★HOW: DVControlView's own DV_Draw sets up an offscreen AGM port, calls Draw on it and blits it back over the view
	(DVAbstractControlView.h). PaintMarks runs exactly that, with Draw answering by painting the plan - the product's
	shape for "the base draws, then more is drawn on top" (open/components/layerpanel/DVLayerElementView.cpp calls
	inherited::DV_Draw first). ⚠**A caller runs it BEFORE it draws any text**: the pass blits the offscreen picture back
	over the whole view, so whatever was drawn before it could be painted over.
	⚠Draw outside the pass is the base's - never a stale plan. */
class KCMMarkPassView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMMarkPassView(IPMUnknown* boss) : inherited(boss), fInMarkPass(kFalse) {}
	virtual ~KCMMarkPassView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);

protected:
	/** Paint `marks`' shapes now (nothing when it has none). From DV_Draw, before any text. */
	void PaintMarks(dvaui::drawbot::Drawbot* drawbotP, const KCMKentenMark::Marks& marks) const;

private:
	mutable KCMKentenMark::Marks	fMarks;			// the plan Draw paints - set by PaintMarks for its pass only
	mutable bool16					fInMarkPass;
};

#endif	// __KCMKentenMark_h__

// End, KCMKentenMark.h.
