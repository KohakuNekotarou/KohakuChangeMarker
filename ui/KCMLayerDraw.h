//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Draws a WARICHU or TATE-CHU-YOKO change in layers (2026-09-16, the user's drawings):
//
//      +       12      34          <- line 2
//          わり｜ちゅう｜のぶん       <- line 1
//          琥珀｜猫太郎              <- line 0
//
//  ★★ONE DRAWING, TWO WIDGETS - the change row's cell (KCMStoryCellView.cpp) and the panel's message
//  area (KCMStatusTextView.cpp), the same reason KCMKentenMark is shared: the two show the two sides
//  of one edit, and a second copy of where a line stands over the bar below it would be a second
//  thing to keep right ([[one-question-one-place]]).
//  ★WHAT GOES ON WHICH LINE IS NOT DECIDED HERE: the model cut the words (KCMStoryLayers.h). This file
//  only places them - each line centred over the bar it stands in, held inside the column.
//
//========================================================================================

#ifndef __KCMLayerDraw_h__
#define __KCMLayerDraw_h__

#include "PMReal.h"
#include "AGMGraphicsContext.h"		// measured and drawn text needs it
#include "IInterfaceColors.h"		// RealAGMColor
#include "IInterfaceFonts.h"		// InterfaceFontInfo

#include "KCMStoryLayers.h"

class IGraphicsPort;

/** Where the lines go. Index 0 is the BOTTOM line, matching KCMStoryLayers. */
struct KCMLayerCanvas
{
	PMReal			fLeft;
	PMReal			fRight;
	PMReal			fTop[3];		///< the top of each line's box - a bar spans it
	PMReal			fBaseline[3];	///< where text sits on each line
	PMReal			fLineHeight;
	RealAGMColor	fStrong;		///< the change, and every bar
	RealAGMColor	fFaded;			///< the context
};

/** Draw one side of a layered change. Does nothing when layers.fCount is below 2. */
void KCMDrawLayers(AGMGraphicsContext& gc, IGraphicsPort* gPort, const InterfaceFontInfo& font,
				   const KCMStoryLayers& layers, const KCMLayerCanvas& canvas);

#endif	// __KCMLayerDraw_h__

// End, KCMLayerDraw.h.
