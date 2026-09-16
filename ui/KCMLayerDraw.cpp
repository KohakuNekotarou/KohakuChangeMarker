//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMLayerDraw.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IGraphicsPort.h"

// General includes:
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB / PMMeasureString / PMEllipsizeString
#include "WidgetDefs.h"			// kEllipsizeBeginning / kEllipsizeEnd

#include <vector>

// Project includes:
#include "KCMLayerDraw.h"
#include "KCMPanelTextDraw.h"	// KCMDrawCaret / KCMCaretPlaceholder / the two draw flags

namespace
{

/** How far apart two pieces of line 2 are kept when centring them would make them touch. */
const PMReal kKCMLayerPieceGap(4.0);

PMReal Width(AGMGraphicsContext& gc, const InterfaceFontInfo& font, const PMString& s)
{
	return s.IsEmpty() ? PMReal(0.0)
					   : StringUtils::PMMeasureString(&gc, s, font, kKCMDontConvertAmpersand).X();
}

/** One bar or one run of text on a line, in the order it is drawn. */
struct Segment
{
	PMString	fText;
	bool16		fIsBar;
	PMReal		fWidth;
};

}	// anonymous namespace

void KCMDrawLayers(AGMGraphicsContext& gc, IGraphicsPort* gPort, const InterfaceFontInfo& font,
				   const KCMStoryLayers& layers, const KCMLayerCanvas& canvas)
{
	if (gPort == nil || layers.fCount < 2)
		return;

	const PMReal left = canvas.fLeft;
	const PMReal right = canvas.fRight;
	const PMReal avail = right - left;
	if (avail <= PMReal(0.0))
		return;

	const PMReal barW = Width(gc, font, KCMCaretPlaceholder());

	auto drawText = [&](const PMString& s, const PMReal& x, int32 line, const RealAGMColor& colour)
	{
		if (!s.IsEmpty())
			StringUtils::PMDrawStringRGB(&gc, PMPoint(x, canvas.fBaseline[line]), s, font, colour,
										 kKCMDontConvertAmpersand, kKCMNoUnderline);
	};
	// ⚠A pixel clear at each end of its line, so a bar over a bar reads as two - "the two bars in the
	//   same place", not one tall line.
	auto drawBar = [&](const PMReal& x, const PMReal& roomW, int32 line)
	{
		KCMDrawCaret(gPort, canvas.fStrong, x, roomW, canvas.fTop[line] + PMReal(1.0),
					 canvas.fLineHeight - PMReal(2.0));
	};

	// ---- line 0: the paragraph, with its one bar -----------------------------------------------
	// ★THE CHANGE'S BAR SURVIVES A NARROW COLUMN, the rule the change row already keeps: the
	//   context gives way, each side losing the end that faces away from the bar.
	PMString pre = layers.fBottomPre;
	PMString post = layers.fBottomPost;
	PMReal preW = Width(gc, font, pre);
	PMReal postW = Width(gc, font, post);
	if (preW + barW + postW > avail)
	{
		const PMReal rem = avail - barW;
		pre = (rem > PMReal(0.0) && !pre.IsEmpty())
			? StringUtils::PMEllipsizeString(&gc, rem, pre, font, kEllipsizeBeginning, nil, kKCMDontConvertAmpersand)
			: PMString();
		preW = Width(gc, font, pre);
		const PMReal postBudget = rem - preW;
		post = (postBudget > PMReal(0.0) && !post.IsEmpty())
			? StringUtils::PMEllipsizeString(&gc, postBudget, post, font, kEllipsizeEnd, nil, kKCMDontConvertAmpersand)
			: PMString();
		postW = Width(gc, font, post);
	}
	const RealAGMColor& bottomColour = (layers.fChanged == 0) ? canvas.fStrong : canvas.fFaded;
	PMReal x = left;
	drawText(pre, x, 0, bottomColour);
	x += preW;
	const PMReal holeX = x;
	drawBar(holeX, barW, 0);
	x += barW;
	drawText(post, x, 0, bottomColour);

	// ---- line 1: one piece over that bar, with bars of its own ------------------------------
	if (layers.fMiddleIsBar || layers.fMiddleParts.empty())
	{
		drawBar(holeX, barW, 1);
		return;
	}

	std::vector<Segment> segments;
	PMReal lineW(0.0);
	for (size_t k = 0; k < layers.fMiddleParts.size(); ++k)
	{
		if (k > 0)
		{
			Segment bar = { PMString(), kTrue, barW };
			segments.push_back(bar);
			lineW += barW;
		}
		Segment part = { layers.fMiddleParts[k], kFalse, Width(gc, font, layers.fMiddleParts[k]) };
		segments.push_back(part);
		lineW += part.fWidth;
	}

	// Centred over the bar below, held inside the column.
	PMReal lineX = holeX + barW / PMReal(2.0) - lineW / PMReal(2.0);
	if (lineX + lineW > right)
		lineX = right - lineW;
	if (lineX < left)
		lineX = left;

	const RealAGMColor& middleColour = (layers.fChanged == 1) ? canvas.fStrong : canvas.fFaded;
	std::vector<PMReal> holes;		// where line 1's bars landed - line 2 stands over them
	x = lineX;
	for (size_t s = 0; s < segments.size(); ++s)
	{
		const Segment& seg = segments[s];
		if (seg.fIsBar)
		{
			holes.push_back(x);
			if (x + barW <= right)
				drawBar(x, barW, 1);
			x += barW;
			continue;
		}
		PMString shown = seg.fText;
		PMReal shownW = seg.fWidth;
		if (x + shownW > right)
		{
			const PMReal room = right - x;
			if (room <= PMReal(0.0))
				continue;
			// Cut the TAIL: the words at the head are the ones nearest the bar below.
			shown = StringUtils::PMEllipsizeString(&gc, room, seg.fText, font, kEllipsizeEnd, nil, kKCMDontConvertAmpersand);
			shownW = Width(gc, font, shown);
		}
		drawText(shown, x, 1, middleColour);
		x += shownW;
	}

	// ---- line 2: one piece over each bar of line 1 -------------------------------------------
	if (layers.fCount < 3)
		return;

	PMReal nextFree = left;
	for (size_t k = 0; k < layers.fUpperPieces.size() && k < holes.size(); ++k)
	{
		const bool16 isChange = (layers.fChanged == 2 && static_cast<int32>(k) == layers.fChangedPiece)
								? kTrue : kFalse;
		if (isChange && layers.fChangedPieceIsBar)
		{
			drawBar(holes[k], barW, 2);
			nextFree = holes[k] + barW + kKCMLayerPieceGap;
			continue;
		}

		const PMString& piece = layers.fUpperPieces[k];
		PMReal pieceW = Width(gc, font, piece);
		PMReal px = holes[k] + barW / PMReal(2.0) - pieceW / PMReal(2.0);
		if (px < nextFree)
			px = nextFree;				// pieces over neighbouring bars must not run into each other
		if (px < left)
			px = left;

		PMString shown = piece;
		if (px + pieceW > right)
		{
			const PMReal room = right - px;
			if (room <= PMReal(0.0))
				break;
			shown = StringUtils::PMEllipsizeString(&gc, room, piece, font, kEllipsizeEnd, nil, kKCMDontConvertAmpersand);
			pieceW = Width(gc, font, shown);
		}
		drawText(shown, px, 2, isChange ? canvas.fStrong : canvas.fFaded);
		nextFree = px + pieceW + kKCMLayerPieceGap;
	}
}

// End, KCMLayerDraw.cpp.
