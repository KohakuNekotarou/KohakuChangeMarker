//========================================================================================
//
//  KCMConstants.h
//
//  Tuning constants for ChangeMarker (KCM). Read by the drawing engine, peek, the book
//  comparison and the color sampler - ALL OF THEM ON THE MODEL SIDE. Nothing under ui/ includes
//  this header (2026-08-30; the four constants that were read only from over there moved to the
//  files that read them - see the note at the end).
//
//  Namespace-scope const has internal linkage in C++ whether or not `static` is written, so each
//  TU gets its own copy and there is no ODR issue either way. The `static` below is redundant,
//  not load-bearing; it is kept because removing it from thirty declarations would change
//  nothing. (This header used to credit `static` with that property, which was wrong.)
//
//========================================================================================
#ifndef __KCMConstants_h__
#define __KCMConstants_h__

#include "BaseType.h"
#include "PMReal.h"

static const PMReal kKCMRingTargetPx = 8.0;	// Ring thickness in screen px. Stays constant at any zoom.

// ---- Thumbnail marks (Pages panel) -----------------------------------------------------
// The Pages panel builds its thumbnails without a view (sxr == 0), so the zoom-based formula
// cannot be used and sizes are fixed ratios of the image or of the page instead. Thumbnails
// are tiny, so favour visibility: a smaller divisor means a thicker mark.
static const int32 kKCMThumbRingDivisor = 8;	// Ring radius = image width / this. 8 is about 12.5% of the width; 6 for thicker, 10-12 for thinner.
// The border and the slash need different divisors even though they want the same weight:
// a border is spread over four edges and reads thinner than a single stroke.
static const int32 kKCMThumbBorderDivisor = 6;	// Changed-page border: page short side / this (about 16.7%).
static const int32 kKCMThumbDiagDivisor = 10;	// "/" slash (green = registered, red = overset): short side / this (10%).
static const PMReal kKCMThumbMarkOpacity = 0.75;	// Border and "/" on thumbnails. Slightly see-through, same weight as the 75% radio.

// ---- Find Overset "+" (Pages panel thumbnails only, never on the canvas) ----------------
static const PMReal kKCMOversetCrossOpacity = 1.0;	// Opaque, so the red stroke and its white halo stay crisp. Denser than the border and "/" (0.75).
static const PMReal kKCMOversetCrossHalfRatio = 0.20;	// Half-length of each arm = page short side * this. Both arms are the same length, so the horizontal one spans 40% of the width.
static const int32 kKCMOversetCrossWidthDivisor = 8;	// Red stroke = short side / this, thicker than the "/" divisor. The white halo is drawn at 2.2x that width. KCMDrawEventHandler clamps the stroke at short side / 3, so lowering this past 3 has no further effect.

static const uint8 kKCMRingAlpha = 255;	// Alpha of the ring pixels themselves (0..255). Always opaque; the visible density is applied afterwards with setopacity.
// Ring opacity, picked by the panel radio "Marks opacity 25% / 75%". The choice applies
// everywhere the marks show: tool left-hold, print-marks-on, and print/PDF output. They all
// go through KCMDrawEventHandler::SelectedMarkOpacity.
static const PMReal kKCMMarkOpacity25 = 0.25;
static const PMReal kKCMMarkOpacity75 = 0.75;
// Change detection: both sides are always rasterized as CMYK and compared channel by channel;
// a cell counts as changed as soon as one of the four channels differs by more than this.
// 0 = catch any difference (one CMYK unit). Comparing in CMYK is the point - small CMYK
// differences are rounded away by a conversion to RGB, and the user thinks in CMYK numbers.
// Raise to 1-2 if redraw jitter in images or effects starts producing noise.
static const int   kKCMCmykThr = 0;
static const int32 kKCMBaseRadius = 4;	// Initial ring radius in image px; the draw code recomputes it from the zoom.
// Resolution (dpi) the ring images and masks are stored at. Low on purpose: one changed A4
// page costs about 0.77MB here against about 3MB at 72dpi. The price is that a mask cell
// covers 2pt instead of 1pt, so ring outlines are coarser when zoomed in and in print.
static const PMReal kKCMResolution = 36.0;
// Missed-change guard: compare at a higher resolution than we store, then max-pool the result
// down. Thin lines and sub-cell shifts that averaging would erase are kept at full weight.
// Comparison resolution = kKCMResolution * kKCMHiResMul = 144dpi.
static const PMReal kKCMHiResMul    = 4.0;
static const int32  kKCMPoolMinCount = 1;	// Pooling: a stored cell counts as changed when this many high-res pixels inside it changed. 1 = most sensitive (picks up edge noise); higher survives noise better, at a slightly higher risk of missing a change.

// Minimum number of pages before a comparison shows a TaskProgressBar, counted against the
// pages actually about to be rasterized.
// The threshold has to be ours: TaskProgressBar's showImmediate = kFalse (the default) does
// not mean "appear once this takes a while", it means "never appear" - a 100 page comparison
// showed no bar at all. Every place in the product that does show one passes kTrue.
// Callers: the full/partial comparison in KCMCore.cpp, and Refresh for the pages selected in
// the Pages panel (KCMPeek.cpp).
static const int32  kKCMProgressBarMinPages = 10;

// Ticks handed to one chapter on the book-comparison progress bar (KCMBookCompare.cpp).
// With the chapter count alone as the denominator the needle freezes for the whole of a 100
// page chapter and Cancel does not take effect until that chapter ends. Giving each chapter a
// span lets the bar advance inside a chapter, which also makes Cancel feel immediate.
// The value itself carries no meaning - the denominator is chapters * span, so only the ratio
// matters. 1000 means "as long as a chapter stays under 1000 pages, every page moves the bar
// by at least one tick".
static const int32  kKCMChapterProgressSpan = 1000;

// Ring color: red by default. Over pixels that are themselves reddish a translucent red ring
// disappears into the page, so it switches to cyan per pixel. Cyan is the complement of red
// (180 degrees) and bright (luma about 0.79), which gives maximum contrast in both hue and
// value. Pure blue was rejected: too dark, thin strokes sink into the page.
static const uint8 kKCMRingR = 255, kKCMRingG = 0,   kKCMRingB = 0;		// normal
static const uint8 kKCMRingAltR = 0,   kKCMRingAltG = 255, kKCMRingAltB = 255;	// over reddish artwork

// Border of registered pages, the ones with no counterpart ("Added" / "Removed"). Fixed green
// so it cannot be mistaken for a change mark (red/cyan). No background-dependent switch here:
// such a page has no raster difference, so there is nothing to judge the background from.
static const uint8 kKCMAddedBorderR = 0, kKCMAddedBorderG = 200, kKCMAddedBorderB = 0;

// Color of the checkmark drawn on pages marked with "Check", the same blue in the Pages panel
// thumbnail and in the middle of the page in the layout view. Blue keeps it distinct from the
// green "/" (registered) and the red "/" (overset).
// The mark is two strokes (moveto/lineto/stroke), not a font glyph, so it does not depend on
// the font, the OS or the locale.
static const uint8 kKCMCheckR = 30, kKCMCheckG = 110, kKCMCheckB = 235;
// Layout-view checkmark, drawn large in the middle of the page. Both ratios are relative to
// the page, so the mark stays similar under zoom and in print - unlike the ring, which is
// pinned to screen px. Opacity follows the panel's 25%/75% choice (SelectedMarkOpacity).
// The thumbnail version has its own size ratio (0.52), hardcoded in KCMDrawPageCheck.
static const PMReal kKCMCheckLayoutSizeRatio   = 0.80;	// checkmark size, as a fraction of the page short side
static const PMReal kKCMCheckLayoutStrokeRatio = 0.12;	// stroke width, as a fraction of the checkmark size

// The cat-paw stamp: the reader's own "I have looked at this spot" mark, placed by the stamp tool
// at a point on the page. Sized the same way as the tick above -- a fraction of the page's short
// side -- so it stays similar under zoom and in print, and small, because a page may carry many.
// ★★THE HIT BOX FOR LIFTING A PAW IS DERIVED FROM THIS ONE VALUE (KCMPawHalfSizeForPage), so
//   what the reader can see is exactly what they can lift. Writing the size in two places would
//   let the picture and the target drift apart, and the drift would only show as "sometimes the
//   paw will not come off" ([[one-question-one-place]]).
// ⚠The colour lives here too rather than in the drawing file, for the same reason the tick's does.
static const PMReal kKCMPawSizeRatio = 0.05;	// paw size, as a fraction of the page short side
static const uint8  kKCMPawR = 240, kKCMPawG = 120, kKCMPawB = 165;	// pink: not the red ring (a change), the green "/" (registered) or the blue tick

// Alt while pressing places a BIGGER paw (user's choice, 2026-09-04) -- for the spot that matters
// more than the others, which is what a reader reaches for once they have put down a few.
// ★It is a multiplier of the size above, not a size of its own, so the two cannot drift apart:
//   change kKCMPawSizeRatio and both paws follow. Each stamp remembers its own multiplier, and
//   both the drawing and the hit box read it, so a big paw is lifted by pressing anywhere on the
//   big paw.
// ★★TEN, the user's own number (it was 1.6 for an hour and they asked for ten). 10 x 0.05 = HALF
//   THE PAGE'S SHORT SIDE, so this is not "a slightly larger paw" but a mark that claims the page
//   -- which is the point: it is for the one spot that matters, not for the fifth of thirty.
//   ⚠A big paw placed near an edge runs off the page. Nothing breaks -- the drawing is clipped to
//     the page rectangle (KCMDrawPawStamps calls rectclip) -- but the part outside is not drawn,
//     and the hit box is NOT clipped, so it can still be lifted by pressing the part that shows.
static const PMReal kKCMPawBigScale = 10.0;	// Alt + press, as a multiple of the ordinary paw

static const PMReal kKCMPawNormalScale = 1.0;	// a plain press

// Fill that shows which areas are excluded from the comparison as page-number regions. While
// the exclusion toggle is on, every excluded rectangle is painted in translucent green so the
// excluded area can be seen, thin enough that the page number underneath still shows through.
// It is a vector rectangle plus setopacity, so it composites correctly on screen and in print.
static const uint8  kKCMExcludeFillR = 0, kKCMExcludeFillG = 200, kKCMExcludeFillB = 0;
static const PMReal kKCMExcludeFillOpacity = 0.35;	// opacity of the excluded-area fill (0..1)

// Click-point CMYK sampling. Only a tiny area around the click is rasterized, at high dpi and
// in CMYK, and the raw value (0..255) of the center pixel is read from the new and the old
// document. Anti-aliasing is off, so the intermediate colors along vector edges are not sampled.
static const PMReal kKCMSampleDpi    = 300.0;	// raster resolution (dpi) of the sample
static const PMReal kKCMSampleHalfPt = 1.0;	// half-width (pt) of the sampled area. At 300dpi that is about 2pt square (8px), of which the center pixel is read.

// Original page number badge (flyout "Show Original Page Numbers"). Hiding spreads makes
// InDesign renumber the current-page markers across the gap, so the number a page had before
// hiding is drawn at the bottom center of the page, under the same visibility rules as the ring
// (print-marks-on, or tool left-hold). With print marks on it goes to print and PDF as well.
// The size is pinned to the equivalent of 50% document zoom, so the badge keeps one size
// relative to the page whatever the zoom or the output. No section prefix, the number only, and
// no background plate - white halo plus black text. Overall opacity follows the panel's "Marks
// opacity 25% / 75%" choice (the same SelectedMarkOpacity as the ring), so screen and print agree.
static const PMReal kKCMOldNumFontPx    = 42.0;	// text size reference (px); the real size is this / kKCMOldNumFixedZoom
static const PMReal kKCMOldNumMarginPx  = 6.0;	// gap between the bottom of the page and the bottom of the text, same reference
static const PMReal kKCMOldNumFixedZoom = 0.5;	// fixed zoom (50% document) used in place of sxr, which is what makes the badge zoom/print independent
// Black text; together with the white halo it stays readable over both light and dark artwork.
static const PMReal kKCMOldNumR = 0.0, kKCMOldNumG = 0.0, kKCMOldNumB = 0.0;
// Halo: the glyphs are drawn in white first, offset in 8 directions, to outline the text.
static const PMReal kKCMOldNumHaloEm = 0.06;	// halo thickness (em)
static const PMReal kKCMOldNumPadEm  = 0.20;	// padding of the transparency group bbox (em), wide enough for the halo to fit

// ⚠ THE UI HALF'S TUNING CONSTANTS ARE NOT HERE. The translucent panel's alpha and its delayed
// re-apply (kKCMPanelAlphaValue / ...ReapplyTries / ...ReapplyDelayMillis) and the CMYK cursor's
// settle wait (kKCMCursorSettleMillis) lived in this header until 2026-08-30. The API re-audit of
// M1 measured the readers: no file under source/ used any of the four, and each was read by
// exactly one file under ui/. They now sit at the top of those files (KCMPanelAlpha.cpp and
// KCMTracker.cpp), which is the shape the rest of the UI half already used for a file-local
// tuning value (KCMScrollMap.cpp, KCMThumbIdleTask.cpp, KCMTrackerHud.cpp) and the shape the
// product code uses (linksui/LinksUIUtils.cpp).
// ⇒ **No file under ui/ includes this header any more.**

#endif // __KCMConstants_h__
