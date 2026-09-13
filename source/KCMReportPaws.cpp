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

const int32  kPawCount = 7;			// paws on the trail
const PMReal kPawSize  = 44.0;		// one paw's size (the outlines are in units of it)
const PMReal kPawStride = 0.55;		// how far the trail advances per paw, as a fraction of the page's height
const PMReal kPawSideways = 0.32;	// a left / right paw's offset from the trail's line, in paw sizes

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
	// through the middle of the page. Each paw steps forward by a fixed share of the page's
	// height, and alternates left / right of the line.
	const PMReal x0 = page.Left() + page.Width() * 0.18;
	const PMReal y0 = page.Bottom() - page.Height() * 0.22;
	const PMReal x1 = page.Right() - page.Width() * 0.18;
	const PMReal y1 = page.Top() + page.Height() * 0.16;
	const double dx = ::ToDouble(x1 - x0), dy = ::ToDouble(y1 - y0);
	const double len = std::sqrt(dx * dx + dy * dy);
	if (len <= 0.0)
		return;
	const double ux = dx / len, uy = dy / len;			// along the trail
	const double nx = -uy, ny = ux;						// across it
	// A paw's toes point to -y in the table; turning by (heading + 90 degrees) points them along
	// the trail (check: (0,-1) turned by heading + pi/2 is (cos heading, sin heading)).
	const double angle = std::atan2(dy, dx) + 3.14159265358979323846 / 2.0;

	const PMReal step = page.Height() * kPawStride / PMReal(kPawCount);
	for (int32 i = 0; i < kPawCount; ++i)
	{
		const double along = ::ToDouble(step) * i + ::ToDouble(kPawSize) * 0.5;
		const double side = (i % 2 == 0 ? -1.0 : 1.0) * ::ToDouble(kPawSize * kPawSideways);
		const PMReal cx = x0 + PMReal(ux * along + nx * side);
		const PMReal cy = y0 + PMReal(uy * along + ny * side);
		MakePaw(helper, reportDB, layer, cx, cy, angle, (i % 2 == 0) ? red : blue, none);
	}
}

// End, KCMReportPaws.cpp.
