//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Paints the emphasis marks (kenten) shown on the upper line of a change row. The reasons for
//  drawing them rather than writing the characters that look like them are in KCMKentenMark.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IGraphicsPort.h"		// newpath / moveto / curveto / fill / stroke / gsave

// General includes:
#include "K2SmartPtr.h"		// K2::scoped_ptr - GetItem hands back a string this has to free
// ⚠RealAGMColor IS DECLARED IN IInterfaceColors.h, not in any AGM header - the name says otherwise
//   and costs a build to find out (measured 2026-09-01: AGMGraphicsContext.h leaves it incomplete,
//   and the error that follows names setrgbcolor rather than the type).
#include "IInterfaceColors.h"	// class RealAGMColor (:38) - .red / .green / .blue
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB / PMMeasureString - a custom mark is WRITTEN

// Project includes:
#include "KCMPanelTextDraw.h"	// kKCMDontConvertAmpersand / kKCMNoUnderline - shared with both drawers
#include "KCMKentenMark.h"

namespace {

/** The constant that turns four cubic beziers into a circle.

	★WRITTEN OUT RATHER THAN TAKEN FROM THE SDK, because the SDK does not offer it: IGraphicsPort
	has newpath / moveto / lineto / curveto and no arc, oval or ellipse of any kind, and a search of
	the whole SDK for a circle drawn with curveto finds **nothing** - there is no house style here to
	follow. So this is the standard value, 4/3*(sqrt(2)-1), which is what every drawing library uses
	for the same job.
*/
const PMReal kKCMCircleK(0.5522847498);

/** Lays down an ellipse centred on (cx, cy). A circle is the case where rx == ry.

	@warning THE PATH IS LEFT ON THE PORT, not painted. The caller chooses fill or stroke - which is
	 the whole difference between a black mark and a white one. */
void EllipsePath(IGraphicsPort* gPort, const PMReal& cx, const PMReal& cy,
				 const PMReal& rx, const PMReal& ry)
{
	const PMReal kx = rx * kKCMCircleK;
	const PMReal ky = ry * kKCMCircleK;

	gPort->newpath();
	gPort->moveto(cx + rx, cy);
	gPort->curveto(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
	gPort->curveto(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
	gPort->curveto(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
	gPort->curveto(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
	gPort->closepath();
}

/** A triangle standing on its base, pointing up.

	⚠**UP IS MINUS Y HERE.** This is a screen port, so y grows downward; the apex is above the
	 centre and therefore at the smaller y. Getting this backwards draws a mark that is right in
	 every measurable way and upside down on screen. */
void TrianglePath(IGraphicsPort* gPort, const PMReal& cx, const PMReal& cy, const PMReal& r)
{
	// Slightly wider than tall, which is how a solid triangle is drawn in type and what keeps it from reading as
	// a spike beside the round marks.
	const PMReal halfW = r;
	const PMReal halfH = r * PMReal(0.88);

	gPort->newpath();
	gPort->moveto(cx, cy - halfH);
	gPort->lineto(cx + halfW, cy + halfH);
	gPort->lineto(cx - halfW, cy + halfH);
	gPort->closepath();
}

/** The radius a HOLLOW mark is drawn at, so that it ends up the same size as a filled one.

	A stroke straddles its path, half in and half out, so an outlined shape of the same nominal
	radius reads slightly larger. Pulling it in by half a line width cancels that.

	⚠**WITH A FLOOR, AND THAT IS NOT DEFENSIVE PADDING.** The mark shrinks to fit the character it
	 stands over (KCMStoryCellView divides the changed run by its character count), while the line
	 width has a floor of its own so that a hollow mark stays visible - so at small sizes the
	 subtraction really can go negative. A negative radius does not draw nothing; it draws the
	 curve inside out.
*/
PMReal StrokeRadius(const PMReal& r, const PMReal& lineWidth)
{
	const PMReal pulled = r - (lineWidth / PMReal(2.0));
	return (pulled < PMReal(0.25)) ? PMReal(0.25) : pulled;
}

/** One point of a shape, rotated about (cx, cy) by an angle given as its cosine and sine. */
PMPoint TurnAbout(const PMReal& x, const PMReal& y, const PMReal& c, const PMReal& s,
				  const PMReal& cx, const PMReal& cy)
{
	return PMPoint(cx + (x * c) - (y * s), cy + (x * s) + (y * c));
}

/** A sesame dot: the shape a brush leaves, not an oval.

	★DRAWN FROM THE REAL ONE (user's capture, 2026-09-01: "make the sesame and white sesame look
	more like what InDesign shows"). InDesign's own mark is a TEARDROP LYING OVER - thin at the top
	left, swelling to the bottom right - and an upright oval, which is what stood here first, reads
	as a full stop rather than as a sesame dot. The white one is the same outline unfilled, which is
	how the capture shows it too.

	⚠THE TILT IS BAKED IN AS A COSINE AND A SINE rather than computed: the angle never varies, so a
	 call to cos/sin per mark would buy nothing, and PMReal does not take them directly anyway. The
	 pair below is -30 degrees, which is the lean measured off the capture.
*/
void SesamePath(IGraphicsPort* gPort, const PMReal& cx, const PMReal& cy,
				const PMReal& rx, const PMReal& ry)
{
	const PMReal kCos(0.8660254);		// cos(-30 deg)
	const PMReal kSin(-0.5);			// sin(-30 deg) - negative leans the tip to the LEFT and up

	// The upright teardrop's points, each turned by that angle about the centre. The tip is at -ry,
	// and up is minus y on a screen port. ⚠Everything stays PMReal - no conversion to double.
	const PMPoint tip   = TurnAbout(PMReal(0.0),        -ry,                 kCos, kSin, cx, cy);
	// The right flank, swelling away from the tip and round into the belly.
	const PMPoint r1    = TurnAbout(rx * PMReal(0.75),  -ry * PMReal(0.45),  kCos, kSin, cx, cy);
	const PMPoint r2    = TurnAbout(rx,                  ry * PMReal(0.25),  kCos, kSin, cx, cy);
	const PMPoint belly = TurnAbout(rx * PMReal(0.45),   ry * PMReal(0.90),  kCos, kSin, cx, cy);
	const PMPoint b1    = TurnAbout(-rx * PMReal(0.20),  ry,                 kCos, kSin, cx, cy);
	const PMPoint b2    = TurnAbout(-rx * PMReal(0.80),  ry * PMReal(0.55),  kCos, kSin, cx, cy);
	const PMPoint l1    = TurnAbout(-rx * PMReal(0.55), -ry * PMReal(0.10),  kCos, kSin, cx, cy);
	const PMPoint l2    = TurnAbout(-rx * PMReal(0.22), -ry * PMReal(0.60),  kCos, kSin, cx, cy);

	gPort->newpath();
	gPort->moveto(tip.X(), tip.Y());
	gPort->curveto(r1.X(), r1.Y(), r2.X(), r2.Y(), belly.X(), belly.Y());
	gPort->curveto(b1.X(), b1.Y(), b2.X(), b2.Y(), l1.X(), l1.Y());
	gPort->curveto(l2.X(), l2.Y(), tip.X(), tip.Y(), tip.X(), tip.Y());
	gPort->closepath();
}

/** Everything this file can draw, spelled the way the reader spells it (KCMTextRead's
	KentenKindName, which copies the SDK's own table). */
bool16 IsKnownKind(const PMString& kind)
{
	return (kind == "BlackSesameDot"  || kind == "WhiteSesameDot"   ||
			kind == "Fisheye"         || kind == "BlackCircle"      ||
			kind == "SmallBlackCircle"|| kind == "Bullseye"         ||
			kind == "BlackTriangle"   || kind == "WhiteTriangle"    ||
			kind == "WhiteCircle"     || kind == "SmallWhiteCircle") ? kTrue : kFalse;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
bool16 KCMKentenMark::CanDraw(const PMString& kindValue)
{
	return IsKnownKind(kindValue);
}

//----------------------------------------------------------------------------------------
bool16 KCMKentenMark::DrawOverRun(AGMGraphicsContext& gc, IGraphicsPort* gPort,
								  const InterfaceFontInfo& font, const PMString& kindValue,
								  const PMReal& runLeft, const PMReal& runWidth, int32 charCount,
								  const PMReal& lineHeight, const PMReal& markCentreY,
								  const PMReal& baselineY,
								  const PMReal& rightEdge, const RealAGMColor& colour)
{
	const bool16 asShape = CanDraw(kindValue);

	// A custom mark is the glyph the reader chose, so it is WRITTEN where the others are DRAWN.
	PMString glyph;
	if (!asShape)
		glyph = CustomCharacter(kindValue);

	if (!asShape && glyph.IsEmpty())
		return kFalse;			// neither - the caller writes the kind's name instead

	// ★NOTHING TO DRAW OVER IS STILL AN ANSWER. A kenten that was REMOVED leaves no characters on
	//   the newer side, and the row is drawn showing that version - the same decision ruby made
	//   (KCMStoryCellView: "nothing is drawn for a ruby that was taken away"). Returning kTrue
	//   keeps the caller from falling back to writing the name, which would put a word where the
	//   reader is looking for a mark.
	if (charCount <= 0 || runWidth <= PMReal(0.0) || gPort == nil)
		return kTrue;

	// The width one character got, which is what a mark has to fit in. CJK is even enough for this
	// to land the mark over its character; measuring every character on its own would cost a call
	// per mark to correct a few pixels nobody reads to that precision.
	const PMReal perChar = runWidth / PMReal(charCount);

	PMReal markSize = lineHeight * PMReal(0.70);
	if (perChar < markSize)
		markSize = perChar;		// a narrow column shrinks the marks rather than overlapping them


	const PMReal glyphW = glyph.IsEmpty()
						  ? PMReal(0.0)
						  : StringUtils::PMMeasureString(&gc, glyph, font,
														 kKCMDontConvertAmpersand).X();

	for (int32 i = 0; i < charCount; ++i)
	{
		const PMReal cx = runLeft + perChar * (PMReal(i) + PMReal(0.5));
		if (cx + (markSize / PMReal(2.0)) > rightEdge)
			break;				// ran out of room - the rest are simply not shown

		if (asShape)
			Draw(gPort, kindValue, cx, markCentreY, markSize, colour);
		else
			StringUtils::PMDrawStringRGB(&gc, PMPoint(cx - (glyphW / PMReal(2.0)), baselineY),
										 glyph, font, colour,
										 kKCMDontConvertAmpersand, kKCMNoUnderline);
	}

	return kTrue;
}

//----------------------------------------------------------------------------------------
PMString KCMKentenMark::CustomCharacter(const PMString& kindValue)
{
	PMString out;
	out.SetTranslatable(kFalse);

	// ★SPLIT ON THE COLON, do not count characters off the front: the day the reader writes
	//   something longer than "Custom", counting would quietly take the wrong slice while splitting
	//   still finds the two halves.
	const PMString colon(":");

	K2::scoped_ptr<PMString> head(kindValue.GetItem(colon, 1));
	if (head.get() == nil || !(*head == "Custom"))
		return out;

	// ⚠A CUSTOM MARK CAN ITSELF BE A COLON, and then this comes back empty. That is the right
	//   answer rather than a case to handle: the caller falls back to writing the kind's name,
	//   which is still true and still readable.
	K2::scoped_ptr<PMString> glyph(kindValue.GetItem(colon, 2));
	if (glyph.get() != nil)
	{
		out = *glyph;
		out.SetTranslatable(kFalse);
	}

	return out;
}

//----------------------------------------------------------------------------------------
void KCMKentenMark::Draw(IGraphicsPort* gPort, const PMString& kindValue,
						 const PMReal& centreX, const PMReal& centreY, const PMReal& size,
						 const RealAGMColor& colour)
{
	if (gPort == nil || size <= PMReal(0.0) || !IsKnownKind(kindValue))
		return;

	// ★THE PORT IS HANDED BACK AS IT WAS. This runs in the middle of a cell that draws text before
	//   and after it, and a colour or a line width left behind would repaint that text.
	gPort->gsave();

	gPort->setrgbcolor(colour.red, colour.green, colour.blue);

	// Thin enough that a white mark reads as an outline rather than a blob at these sizes, with a
	// floor so it never disappears on a low-resolution screen.
	PMReal lineWidth = size * PMReal(0.10);
	if (lineWidth < PMReal(0.75))
		lineWidth = PMReal(0.75);
	gPort->setlinewidth(lineWidth);

	// ⚠THE STROKE IS DRAWN ON THE PATH, HALF IN AND HALF OUT, so a hollow mark of the same nominal
	//   radius reads slightly larger than a filled one. The radii below already allow for it: the
	//   outlined kinds are pulled in by half a line width so that black and white marks of the same
	//   kind occupy the same box on screen.
	const PMReal r = size * PMReal(0.40);
	const PMReal rStroke = StrokeRadius(r, lineWidth);

	if (kindValue == "BlackCircle")
	{
		EllipsePath(gPort, centreX, centreY, r, r);
		gPort->fill();
	}
	else if (kindValue == "WhiteCircle")
	{
		EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
		gPort->stroke();
	}
	else if (kindValue == "SmallBlackCircle")
	{
		const PMReal rs = size * PMReal(0.24);
		EllipsePath(gPort, centreX, centreY, rs, rs);
		gPort->fill();
	}
	else if (kindValue == "SmallWhiteCircle")
	{
		const PMReal rs = StrokeRadius(size * PMReal(0.24), lineWidth);
		EllipsePath(gPort, centreX, centreY, rs, rs);
		gPort->stroke();
	}
	else if (kindValue == "Fisheye")
	{
		// A ring with a SOLID DOT inside, and **the dot is large** - measured off the user's
		// capture of InDesign's own mark (2026-09-01): the black centre is about half the outer
		// diameter, with a clear white gap left around it. A small dot reads as a bullseye instead,
		// which is the one mark this must not be confused with.
		EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
		gPort->stroke();
		const PMReal inner = size * PMReal(0.22);
		EllipsePath(gPort, centreX, centreY, inner, inner);
		gPort->fill();
	}
	else if (kindValue == "Bullseye")
	{
		// Bullseye - two rings, nothing filled. ⚠THIS IS THE ONE THAT MUST NOT BE CONFUSED WITH
		//   Fisheye above: they differ only in whether the inner shape is filled, and that is
		//   exactly the difference a reader is looking at the panel to see.
		EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
		gPort->stroke();
		const PMReal inner = size * PMReal(0.20);
		EllipsePath(gPort, centreX, centreY, inner, inner);
		gPort->stroke();
	}
	else if (kindValue == "BlackTriangle")
	{
		TrianglePath(gPort, centreX, centreY, r);
		gPort->fill();
	}
	else if (kindValue == "WhiteTriangle")
	{
		TrianglePath(gPort, centreX, centreY, rStroke);
		gPort->stroke();
	}
	else if (kindValue == "BlackSesameDot" || kindValue == "WhiteSesameDot")
	{
		// A LEANING TEARDROP, matched to InDesign's own mark - see SesamePath. It was an upright
		// oval until the user put a capture of the real one beside it.
		const PMReal rx = size * PMReal(0.20);
		const PMReal ry = size * PMReal(0.38);
		if (kindValue == "BlackSesameDot")
		{
			SesamePath(gPort, centreX, centreY, rx, ry);
			gPort->fill();
		}
		else
		{
			SesamePath(gPort, centreX, centreY,
					   StrokeRadius(rx, lineWidth), StrokeRadius(ry, lineWidth));
			gPort->stroke();
		}
	}

	gPort->grestore();
}

// End, KCMKentenMark.cpp.
