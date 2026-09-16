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

	const RealAGMColor& middleColour = (layers.fChanged == 1) ? canvas.fStrong : canvas.fFaded;

	/** Where one of line 1's bars landed, and which piece of line 2 stands over it. */
	struct Hole
	{
		size_t	fPiece;
		PMReal	fX;
	};
	std::vector<Hole> holes;

	// ★★THE CHANGED PIECE'S BAR SURVIVES A NARROW COLUMN (found by the re-check of 2026-09-16). The
	//   line used to be cut from its tail alone, so a tate-chu-yoko near the end of a long warichu lost
	//   its bar, and the piece that IS the change was never drawn - a row with a sign and nothing to
	//   show for it. ⇒ When the line does not fit and the change stands on line 2, the line gives way
	//   around that one bar the way line 0 does: the part before it loses its head, the part after
	//   it its tail, and the bars further out go with them (an ellipsis says so).
	const size_t changedPiece = (layers.fChangedPiece >= 0) ? static_cast<size_t>(layers.fChangedPiece) : 0;
	const bool16 keepChangedBar = (lineW > avail && layers.fChanged == 2 && layers.fChangedPiece >= 0
								   && changedPiece + 1 < layers.fMiddleParts.size()) ? kTrue : kFalse;
	if (keepChangedBar)
	{
		const char16_t kEllipsis[] = u"…";
		PMString ellipsis;
		ellipsis.SetXString(reinterpret_cast<const UTF16TextChar*>(kEllipsis), 1);
		ellipsis.SetTranslatable(kFalse);

		PMString before;
		before.SetTranslatable(kFalse);
		if (changedPiece > 0)
			before.Append(ellipsis);				// bars before this one are not drawn
		before.Append(layers.fMiddleParts[changedPiece]);

		PMString after(layers.fMiddleParts[changedPiece + 1]);
		after.SetTranslatable(kFalse);
		if (changedPiece + 2 < layers.fMiddleParts.size())
			after.Append(ellipsis);					// nor are the ones after it

		const PMReal rem = avail - barW;
		PMReal beforeW = Width(gc, font, before);
		if (beforeW > rem)
		{
			before = (rem > PMReal(0.0))
				? StringUtils::PMEllipsizeString(&gc, rem, before, font, kEllipsizeBeginning, nil, kKCMDontConvertAmpersand)
				: PMString();
			beforeW = Width(gc, font, before);
		}
		PMReal afterW = Width(gc, font, after);
		const PMReal afterBudget = rem - beforeW;
		if (afterW > afterBudget)
		{
			after = (afterBudget > PMReal(0.0))
				? StringUtils::PMEllipsizeString(&gc, afterBudget, after, font, kEllipsizeEnd, nil, kKCMDontConvertAmpersand)
				: PMString();
			afterW = Width(gc, font, after);
		}

		const PMReal shortW = beforeW + barW + afterW;
		PMReal lineX = holeX + barW / PMReal(2.0) - shortW / PMReal(2.0);
		if (lineX + shortW > right)
			lineX = right - shortW;
		if (lineX < left)
			lineX = left;

		x = lineX;
		drawText(before, x, 1, middleColour);
		x += beforeW;
		Hole kept = { changedPiece, x };
		holes.push_back(kept);
		drawBar(x, barW, 1);
		x += barW;
		drawText(after, x, 1, middleColour);
	}
	else
	{
		// Centred over the bar below, held inside the column.
		PMReal lineX = holeX + barW / PMReal(2.0) - lineW / PMReal(2.0);
		if (lineX + lineW > right)
			lineX = right - lineW;
		if (lineX < left)
			lineX = left;

		x = lineX;
		size_t barsSeen = 0;
		for (size_t s = 0; s < segments.size(); ++s)
		{
			const Segment& seg = segments[s];
			if (seg.fIsBar)
			{
				Hole hole = { barsSeen++, x };
				holes.push_back(hole);
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
	}

	// ---- line 2: one piece over each bar of line 1 -------------------------------------------
	if (layers.fCount < 3)
		return;

	PMReal nextFree = left;
	for (size_t h = 0; h < holes.size(); ++h)
	{
		const size_t k = holes[h].fPiece;
		const PMReal holeAt = holes[h].fX;
		if (k >= layers.fUpperPieces.size())
			continue;

		const bool16 isChange = (layers.fChanged == 2 && static_cast<int32>(k) == layers.fChangedPiece)
								? kTrue : kFalse;
		if (isChange && layers.fChangedPieceIsBar)
		{
			// ⚠HELD INSIDE THE COLUMN like every other bar (the re-check of 2026-09-16: this one alone
			//   was not, and a bar standing past the edge painted over the sign's column).
			if (holeAt + barW <= right)
				drawBar(holeAt, barW, 2);
			nextFree = holeAt + barW + kKCMLayerPieceGap;
			continue;
		}

		const PMString& piece = layers.fUpperPieces[k];
		PMReal pieceW = Width(gc, font, piece);
		PMReal px = holeAt + barW / PMReal(2.0) - pieceW / PMReal(2.0);
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
