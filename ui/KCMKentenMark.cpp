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
// ⚠RealAGMColor IS DECLARED IN IInterfaceColors.h, not in any AGM header - the name says otherwise
//   and costs a build to find out (measured 2026-09-01: AGMGraphicsContext.h leaves it incomplete,
//   and the error that follows names setrgbcolor rather than the type).
#include "IInterfaceColors.h"	// class RealAGMColor (:38) - .red / .green / .blue

// Project includes:
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
	const PMReal rStroke = r - (lineWidth / PMReal(2.0));

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
		const PMReal rs = size * PMReal(0.24) - (lineWidth / PMReal(2.0));
		EllipsePath(gPort, centreX, centreY, rs, rs);
		gPort->stroke();
	}
	else if (kindValue == "Fisheye")
	{
		// Fisheye - an outlined ring with a solid dot inside it.
		EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
		gPort->stroke();
		const PMReal inner = size * PMReal(0.18);
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
		// Sesame dot - a small tall oval. ⚠**IT IS NOT TILTED**, and the real one is: a sesame dot
		//   leans, which at this size (a few pixels) would cost the shape its readability while
		//   adding a rotation to a path that has no other reason to carry one. What has to be
		//   distinguishable here is sesame-from-circle and black-from-white, and an upright oval
		//   does both. The name is beside it in the message area for anyone who needs certainty.
		const PMReal rx = size * PMReal(0.16);
		const PMReal ry = size * PMReal(0.30);
		if (kindValue == "BlackSesameDot")
		{
			EllipsePath(gPort, centreX, centreY, rx, ry);
			gPort->fill();
		}
		else
		{
			EllipsePath(gPort, centreX, centreY,
						rx - (lineWidth / PMReal(2.0)), ry - (lineWidth / PMReal(2.0)));
			gPort->stroke();
		}
	}

	gPort->grestore();
}

// End, KCMKentenMark.cpp.
