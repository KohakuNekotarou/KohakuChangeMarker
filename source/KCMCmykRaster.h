//========================================================================================
//
//  KCMCmykRaster.h
//
//  How a CMYK raster is asked for when its NUMBERS are going to be read. The comparison
//  (KCMRasteriseForComparison), the book comparison and the Alt read all read them, and all three
//  have to ask alike.
//
//  **Asked for plainly, what a pixel holds depends on whether the spread holds any transparency at
//  all, anywhere on it** (measured, with SnapshotUtilsEx's kCsCMYK and its defaults):
//
//                          no transparency on the spread    transparency on the spread
//    placed CMYK image     through sRGB: 25/50/75/10        its own values
//                          reads C0 M21 Y46 K27
//    placed RGB image      split off sRGB                   converted by the document's profile
//    an overprint          seen                             not seen
//
//  What that did to the comparison: one frame with a drop shadow added to a page marked every
//  placed image on the spread as changed (on the other side of the fold too, where nothing had been
//  edited); a 1% difference in a placed image's yellow was rounded away; and on a page holding
//  transparency a change of overprint alone was not seen at all.
//
//  With the two below the table has one column: a placed CMYK image reads its own values, an
//  overprint is seen, and an RGB or Lab colour reads what the document itself converts it to (asked
//  of the DOM: 255/0/0 is C0 M96 Y95 K0; drawn plainly it read Y91). **Neither is enough alone**:
//  simulateOverprint alone leaves the images going through sRGB, kCMSProofingEnabled alone leaves
//  the overprint unseen beside transparency. Neither changed what a page costs to draw, measured
//  (a sample 2-3ms, or 7-9ms with transparency on the spread; a text-heavy page's comparison 220ms).
//  The window's own proof setup (View > Proof Setup) does not reach these rasters: with another
//  profile set there the numbers stayed the same.
//
//  @warning kCMSProofingEnabled has no comment in its header (ColorMgmtID.h) and no sample uses it;
//    what is written here is what it was measured to do. pVPAttrMap itself is documented
//    (SnapshotUtilsEx.h), with "various viewport attributes" for its values.
//
//========================================================================================
#ifndef __KCMCmykRaster_h__
#define __KCMCmykRaster_h__

#include "SnapshotUtilsEx.h"	// VPAttrMap
#include "ColorMgmtID.h"		// kCMSProofingEnabled

// The constructor's simulateOverprint, its last argument.
const bool8 kKCMCmykRasterSimulateOverprint = kTrue;

// What Draw's pVPAttrMap points at. A local of the function that draws: Draw takes a non-const
// pointer, and it only reads the map.
struct KCMCmykRasterAttrs
{
	VPAttrMap map;

	KCMCmykRasterAttrs() { map[ClassID(kCMSProofingEnabled)] = 1.0; }
};

#endif // __KCMCmykRaster_h__
