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

// (2026-09-13, the user's second look: "a little more winding, the stance a little wider, and a
//  few more stamps" - 7 -> 10 paws, the stance 0.32 -> 0.55, and a wave along the trail.)
const int32  kPawCount = 10;		// paws on the trail
const PMReal kPawSize  = 44.0;		// one paw's size (the outlines are in units of it)
const PMReal kPawSideways = 0.55;	// a left / right paw's offset from the trail's line, in paw sizes
const double kPawWaves = 1.5;		// how many times the trail winds from side to side over its length
const PMReal kPawWaveAmp = 1.1;		// how far it winds, in paw sizes

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

	// The trail: from a point above the foot's labels at the lower left, up and to the right,
	// through the middle of the page, winding from side to side on its way. The paws are spread
	// evenly along it, alternating left / right of the line (the stance) on top of the winding.
	// (10pt lower than first drawn - the user's ask, 2026-09-13: "the whole trail 10px down".)
	const PMReal kDown = 10.0;
	const PMReal x0 = page.Left() + page.Width() * 0.14;
	const PMReal y0 = page.Bottom() - page.Height() * 0.22 + kDown;
	const PMReal x1 = page.Right() - page.Width() * 0.14;
	const PMReal y1 = page.Top() + page.Height() * 0.16 + kDown;
	const double dx = ::ToDouble(x1 - x0), dy = ::ToDouble(y1 - y0);
	const double len = std::sqrt(dx * dx + dy * dy);
	if (len <= 0.0)
		return;
	const double ux = dx / len, uy = dy / len;			// along the trail
	const double nx = -uy, ny = ux;						// across it
	const double kPi = 3.14159265358979323846;

	for (int32 i = 0; i < kPawCount; ++i)
	{
		const double t = (i + 0.5) / kPawCount;				// 0..1 along the trail
		const double along = t * len;
		// The winding: a sine across the trail, and each paw turned to face the way the trail
		// runs THERE (the sine's slope), so the cat walks the curve rather than the chord.
		const double wave = std::sin(t * kPawWaves * 2.0 * kPi) * ::ToDouble(kPawSize * kPawWaveAmp);
		const double slope = std::cos(t * kPawWaves * 2.0 * kPi) * ::ToDouble(kPawSize * kPawWaveAmp) * (kPawWaves * 2.0 * kPi) / len;
		const double stance = (i % 2 == 0 ? -1.0 : 1.0) * ::ToDouble(kPawSize * kPawSideways);
		const double side = wave + stance;
		const PMReal cx = x0 + PMReal(ux * along + nx * side);
		const PMReal cy = y0 + PMReal(uy * along + ny * side);
		// A paw's toes point to -y in the table; turning by (heading + 90 degrees) points them
		// along the trail (check: (0,-1) turned by heading + pi/2 is (cos heading, sin heading)).
		const double heading = std::atan2(uy + ny * slope, ux + nx * slope);
		MakePaw(helper, reportDB, layer, cx, cy, heading + kPi / 2.0, (i % 2 == 0) ? red : blue, none);
	}
}

// End, KCMReportPaws.cpp.
