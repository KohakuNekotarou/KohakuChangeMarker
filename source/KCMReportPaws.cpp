//========================================================================================
//
//  KCMReportPaws.cpp -- see the header.
//
//  Recipes: SDKLayoutHelper::CreateSplineGraphic (a closed path from PMPathPoints),
//  codesnippets/SnpManipulateSwatch.cpp (a colour swatch through ISwatchUtils::CreateNewSwatch
//  on a temporary rendering object), basicdragdrop/BscDNDDragSource.cpp (the fill / stroke
//  rendering commands of IGraphicAttributeUtils).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cmath>
#include <ctime>					// the seed of the walk
#include <windows.h>				// GetTickCount64 - the other half of the seed

#include "IColorData.h"
#include "ICommand.h"
#include "IDataBase.h"
#include "IGraphicAttributeUtils.h"	// CreateFillRenderingCommand / CreateStrokeRenderingCommand
#include "IGraphicStateUtils.h"		// CreateTemporaryRenderObject
#include "IInkData.h"
#include "IRenderingObject.h"
#include "ISwatchList.h"				// GetNoneSwatchUID - no stroke
#include "ISwatchUtils.h"				// CreateNewSwatch / QuerySwatchList
#include "CmdUtils.h"
#include "ColorSystemID.h"				// kPMColorBoss
#include "GraphicTypes.h"				// kPMCsCalRGB / ColorArray
#include "PMPathPoint.h"
#include "SDKLayoutHelper.h"
#include "UIDList.h"
#include "Utils.h"

#include "KCMReportPaws.h"
#include "KCMConstants.h"				// kKCMPawColourRed / kKCMPawColourBlue
#include "KCMDrawEventHandler.h"		// kKCMPawOutlines / KCMPawColours - the tool's own shape and shades

namespace
{

// (2026-09-13, the user's looks: "a little more winding, the stance a little wider, a few more
//  stamps" - and then "some randomness: diagonal, vertical, the other diagonal, downwards". The
//  count, the winding and the stance are drawn by Dice each time now; only the size is fixed.)
const PMReal kPawSize  = 44.0;		// one paw's size (the outlines are in units of it)

/** A small deterministic generator (a 32-bit LCG), seeded from the clock by the caller. Enough
    for a cat's walk; nothing here needs more than "different every time". */
class Dice
{
public:
	explicit Dice(uint32 seed) : fState(seed ? seed : 0x9E3779B9u) {}
	/** 0.0 .. 1.0 (never quite 1.0). */
	double Unit()
	{
		fState = fState * 1664525u + 1013904223u;
		return (fState >> 8) / 16777216.0;
	}
	/** 0 .. n-1. */
	int32 Below(int32 n) { return static_cast<int32>(Unit() * n) % n; }
private:
	uint32 fState;
};

/** An RGB swatch in the report document, at the paw's FILL shade for `colour`. kInvalidUID
    when the swatch could not be made. */
UID MakePawSwatch(IDataBase* db, int32 colour, const char* name)
{
	uint8 inkR = 0, inkG = 0, inkB = 0, fillR = 0, fillG = 0, fillB = 0;
	KCMPawColours(colour, inkR, inkG, inkB, fillR, fillG, fillB);

	Utils<IGraphicStateUtils> gsUtils;
	Utils<ISwatchUtils> swatchUtils;
	if (!gsUtils || !swatchUtils)
		return kInvalidUID;
	InterfacePtr<IRenderingObject> render(gsUtils->CreateTemporaryRenderObject(kPMColorBoss));
	InterfacePtr<IColorData> colorData(render, UseDefaultIID());
	InterfacePtr<IInkData> inkData(render, UseDefaultIID());
	if (render == nil || colorData == nil)
		return kInvalidUID;
	ColorArray rgb;		// 0.0 .. 1.0 (SnpApplyTextStyleAttributes says so in as many words)
	rgb.push_back(PMReal(fillR) / 255.0);
	rgb.push_back(PMReal(fillG) / 255.0);
	rgb.push_back(PMReal(fillB) / 255.0);
	colorData->SetColorData(kPMCsCalRGB, rgb);
	PMString swatchName(name);
	swatchName.SetTranslatable(kFalse);
	render->SetSwatchName(swatchName);
	render->SetVisibility(kTrue);
	render->SetCanDelete(kTrue);
	if (inkData != nil)
		inkData->SetInkType(IInkData::kProcessInk);	// an undefined ink type asserts at close (the snippet's warning)
	return swatchUtils->CreateNewSwatch(kPMColorBoss, render, db, kFalse);
}

/** One outline as a closed path of `kKCMPawPoints` PMPathPoints - a Catmull-Rom spline through
    the table's points, turned by `angle` and scaled to `size` about (cx, cy). The points come
    out in the coordinates the caller gives for the centre. */
void PawOutlinePath(const double outline[kKCMPawPoints][2], const PMReal& cx, const PMReal& cy,
					const PMReal& size, double angle, PMPathPointList& out, PMRect& outBounds)
{
	const double c = std::cos(angle), s = std::sin(angle);
	PMPoint p[kKCMPawPoints];
	for (int32 i = 0; i < kKCMPawPoints; ++i)
	{
		const double x = outline[i][0], y = outline[i][1];
		p[i] = PMPoint(cx + PMReal(x * c - y * s) * size, cy + PMReal(x * s + y * c) * size);
	}
	// The tangent at a point is (next - previous) / 6: the standard Catmull-Rom to Bezier
	// conversion, the same one the drawing uses (KCMDrawEventHandler.cpp, KCMPawShapePath).
	const PMReal kSixth = PMReal(1.0) / PMReal(6.0);
	out.clear();
	outBounds = PMRect(p[0], p[0]);
	for (int32 i = 0; i < kKCMPawPoints; ++i)
	{
		const int32 i0 = (i - 1 + kKCMPawPoints) % kKCMPawPoints;
		const int32 i2 = (i + 1) % kKCMPawPoints;
		const PMPoint tangent((p[i2].X() - p[i0].X()) * kSixth, (p[i2].Y() - p[i0].Y()) * kSixth);
		const PMPoint left (p[i].X() - tangent.X(), p[i].Y() - tangent.Y());
		const PMPoint right(p[i].X() + tangent.X(), p[i].Y() + tangent.Y());
		out.push_back(PMPathPoint(left, p[i], right));
		outBounds.Union(PMRect(p[i], p[i]));
	}
	// A little more than the anchors, so the control points' curves stay inside the bounds the
	// helper scales to (it scales by the ratio of the two rectangles, which must not be zero).
	outBounds.Inset(-size * 0.05, -size * 0.05);
}

/** One paw at (cx, cy), facing `angle`, filled with `swatch`, no stroke. */
void MakePaw(SDKLayoutHelper& helper, IDataBase* db, const UIDRef& layer, const PMReal& cx, const PMReal& cy,
			 double angle, UID swatch, UID noneSwatch)
{
	UIDList items(db);
	for (int32 k = 0; k < kKCMPawOutlineCount; ++k)
	{
		PMPathPointList path;
		PMRect bounds;
		PawOutlinePath(kKCMPawOutlines[k], cx, cy, kPawSize, angle, path, bounds);
		// The helper maps inner bounds onto parent bounds; the path is already in the parent's
		// (spread) coordinates, so the two rectangles are the same and the map is the identity
		// once the path is expressed relative to the bounds' origin.
		for (PMPathPointList::iterator it = path.begin(); it != path.end(); ++it)
			it->TransformPoints(PMMatrix(1, 0, 0, 1, -bounds.Left(), -bounds.Top()));
		const PMRect inner(0, 0, bounds.Width(), bounds.Height());
		const UIDRef spline = helper.CreateSplineGraphic(layer, bounds, inner, path, kTrue);
		if (spline != UIDRef::gNull)
			items.Append(spline.GetUID());
	}
	if (items.Length() == 0)
		return;
	Utils<IGraphicAttributeUtils> attrs;
	if (!attrs)
		return;
	if (swatch != kInvalidUID)
	{
		InterfacePtr<ICommand> fill(attrs->CreateFillRenderingCommand(swatch, &items, kTrue, kTrue));
		if (fill != nil)
			CmdUtils::ProcessCommand(fill);
	}
	if (noneSwatch != kInvalidUID)
	{
		InterfacePtr<ICommand> stroke(attrs->CreateStrokeRenderingCommand(noneSwatch, &items, kTrue, kTrue));
		if (stroke != nil)
			CmdUtils::ProcessCommand(stroke);
	}
}

}	// namespace

void KCMReportDrawPawTrail(IDataBase* reportDB, const UIDRef& layer, const PMRect& page)
{
	if (reportDB == nil || layer == UIDRef::gNull)
		return;
	SDKLayoutHelper helper;

	const UID red  = MakePawSwatch(reportDB, kKCMPawColourRed,  "KCM Paw Red");
	const UID blue = MakePawSwatch(reportDB, kKCMPawColourBlue, "KCM Paw Blue");
	UID none = kInvalidUID;
	{
		Utils<ISwatchUtils> swatchUtils;
		InterfacePtr<ISwatchList> swatches(swatchUtils ? swatchUtils->QuerySwatchList(reportDB) : nil);
		if (swatches != nil)
			none = swatches->GetNoneSwatchUID();
	}

	// THE TRAIL IS DIFFERENT EVERY TIME (the user's ask, 2026-09-13: "some randomness - diagonal,
	// vertical, the other diagonal, downwards..." - "the playful part"). A small generator seeded
	// from the clock picks the two ends of the walk anywhere on the page (the trail is laid down
	// BEFORE the words, so it may run under them - the user's call), far enough apart to be a
	// walk; then how many paws, how much the trail winds and how wide the stance is. Everything
	// else - the paw itself, the two colours alternating, the toes facing the way the cat goes -
	// stays as it was.
	Dice dice(static_cast<uint32>(::time(nil)) ^ static_cast<uint32>(::GetTickCount64()));
	const PMRect area(page.Left() + page.Width() * 0.06, page.Top() + page.Height() * 0.06,
					  page.Right() - page.Width() * 0.06, page.Bottom() - page.Height() * 0.06);
	PMReal x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	double dx = 0, dy = 0, len = 0;
	const double kMinWalk = ::ToDouble(area.Height()) * 0.9;		// at least most of the free height
	for (int32 attempt = 0; attempt < 32; ++attempt)
	{
		x0 = area.Left() + area.Width()  * dice.Unit();
		y0 = area.Top()  + area.Height() * dice.Unit();
		x1 = area.Left() + area.Width()  * dice.Unit();
		y1 = area.Top()  + area.Height() * dice.Unit();
		dx = ::ToDouble(x1 - x0);
		dy = ::ToDouble(y1 - y0);
		len = std::sqrt(dx * dx + dy * dy);
		if (len >= kMinWalk)
			break;
	}
	if (len <= 0.0)
		return;
	const int32  pawCount = 8 + dice.Below(5);							// 8 .. 12 paws
	const double waves    = 0.8 + 1.4 * dice.Unit();						// how many times it winds
	const double waveAmp  = ::ToDouble(kPawSize) * (0.5 + 0.9 * dice.Unit());	// how far
	const double phase    = dice.Unit() * 2.0 * 3.14159265358979323846;		// where the winding starts
	const double stanceW  = ::ToDouble(kPawSize) * (0.4 + 0.3 * dice.Unit());	// the stance
	const bool16 redFirst = (dice.Below(2) == 0) ? kTrue : kFalse;

	const double ux = dx / len, uy = dy / len;			// along the trail
	const double nx = -uy, ny = ux;						// across it
	const double kPi = 3.14159265358979323846;

	for (int32 i = 0; i < pawCount; ++i)
	{
		const double t = (i + 0.5) / pawCount;				// 0..1 along the trail
		const double along = t * len;
		// The winding: a sine across the trail, and each paw turned to face the way the trail
		// runs THERE (the sine's slope), so the cat walks the curve rather than the chord.
		const double wave = std::sin(phase + t * waves * 2.0 * kPi) * waveAmp;
		const double slope = std::cos(phase + t * waves * 2.0 * kPi) * waveAmp * (waves * 2.0 * kPi) / len;
		const double stance = (i % 2 == 0 ? -1.0 : 1.0) * stanceW;
		const double side = wave + stance;
		const PMReal cx = x0 + PMReal(ux * along + nx * side);
		const PMReal cy = y0 + PMReal(uy * along + ny * side);
		// A paw's toes point to -y in the table; turning by (heading + 90 degrees) points them
		// along the trail (check: (0,-1) turned by heading + pi/2 is (cos heading, sin heading)).
		const double heading = std::atan2(uy + ny * slope, ux + nx * slope);
		const bool16 evenIsRed = redFirst;
		MakePaw(helper, reportDB, layer, cx, cy, heading + kPi / 2.0, ((i % 2 == 0) == (evenIsRed != kFalse)) ? red : blue, none);
	}
}

// End, KCMReportPaws.cpp.
