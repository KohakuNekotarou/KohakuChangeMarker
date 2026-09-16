//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  KCMStoryLayers.h - the LINES a warichu or tate-chu-yoko change is drawn on, as the strings the
//  panel draws (2026-09-16, the user's drawings):
//
//      +       12      34          <- line 2: one piece over each bar of line 1
//          わり｜ちゅう｜のぶん       <- line 1: one piece over the bar of line 0
//          琥珀｜猫太郎              <- line 0: the paragraph, with ONE bar
//
//  ★WHY THE MODEL HANDS OVER STRINGS AND NOT RANGES. Which characters go on which line is worked
//   out from the paragraph's spans (KCMParaText::PlanLayers), and the UI half has neither the
//   paragraph nor the spans. It gets the words already cut, the same way every change row gets its
//   pre / mid / post (KCMStoryList.h) - so nothing on the UI side decides what a line says.
//  ★WHY A FILE OF ITS OWN: both halves read it (the model fills it, the change row's cell and the
//   message area draw it) and it needs PMString, which KCMStoryKinds.h deliberately does not.
//
//========================================================================================

#ifndef __KCMStoryLayers_h__
#define __KCMStoryLayers_h__

#include "BaseType.h"
#include "PMString.h"

#include <vector>

/** One side of a layered change, line by line. Line 0 is the bottom. */
struct KCMStoryLayers
{
	/** How many lines: 0 when the change is not a layered one, else 2 or 3. */
	int32		fCount;

	/** The layer that IS the change - 1 or 2. The row puts its sign (+ - ≠) on its line and draws
		its words at full strength; the other lines are context. ⚠A number of the LAYER, not of the
		line drawn: see fShowsText. */
	int32		fChanged;

	/** Line 0: the words before and after its one bar, cut to the row's excerpt with the ellipses
		the row's own pieces carry. */
	PMString	fBottomPre;
	PMString	fBottomPost;

	/** kTrue when line 1 is a bar alone - the mark is not on this side and nothing stands around it
		(the user: "the two bars in the same place"). fMiddleParts is then empty. */
	bool16		fMiddleIsBar;

	/** Line 1, split at its bars: one more part than there are bars. */
	std::vector<PMString>	fMiddleParts;

	/** Line 2: one piece per bar of line 1, in order - empty when line 2 is not there. */
	std::vector<PMString>	fUpperPieces;

	/** Which piece of line 2 is the change; -1 when the change is line 1. */
	int32		fChangedPiece;

	/** That piece is a bar: the mark is not on this side (its characters are hidden below it). */
	bool16		fChangedPieceIsBar;

	/** ★Whether line 0 - the paragraph - is drawn (2026-09-16, the user: a tate-chu-yoko changing
		inside a warichu is "the warichu's line and the tate-chu-yoko's", the text left out, because
		the ID column already says 割注). kFalse then: fCount is 2, the bottom line DRAWN is line 1,
		and fBottomPre / fBottomPost are empty.
		⚠**fChanged KEEPS ITS NUMBERING** (1 = the middle layer, 2 = the upper) either way; the line
		 a thing is DRAWN on is its number less one when this is kFalse. KCMLayerDraw and the row's
		 sign placement both make that step. */
	bool16		fShowsText;

	KCMStoryLayers() : fCount(0), fChanged(0), fMiddleIsBar(kFalse), fChangedPiece(-1),
					   fChangedPieceIsBar(kFalse), fShowsText(kTrue) {}
};

#endif // __KCMStoryLayers_h__

// End, KCMStoryLayers.h.
