//========================================================================================
//
//  KCMConstants.h
//
//  Tuning constants for ChangeMarker (KCM), read by BOTH halves: on the model side the drawing
//  engine, peek, the book comparison, the colour sampler, the progress bars and the paws; on the UI
//  side the paw tool (ui/KCMPawTracker.cpp - KCMPawColour) and the tracked-change labels
//  (ui/KCMTrackLabels.cpp - the ring colours). So a change here is built into both .pln files -
//  build both. A value only the UI half reads does not belong here (see the note at the end).
//
//  Namespace-scope const has internal linkage in C++ whether or not `static` is written, so each
//  TU gets its own copy and there is no ODR issue either way. The `static` below is redundant,
//  not load-bearing; it is kept because removing it would change nothing.
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
static const int32 kKCMThumbDiagDivisor = 10;	// "/" slash (green = registered, red = not compared): short side / this (10%).
static const PMReal kKCMThumbMarkOpacity = 0.75;	// Border and "/" on thumbnails. Slightly see-through, same weight as the 75% radio.

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
// (The PDF export check's own raster and counting - RGB, anti-aliased, 160 / 6 and patches of 8 - went with the PDF
//  comparison. Measured values and why: docs/ai-notes/kcm-pdf-export-check-live-2026-10-14.md.)

// How long a job runs before its progress bar (with Cancel) appears, in milliseconds - every
// mode alike (the author's call: three seconds). A page count cannot decide it (the Story mode
// rasterises nothing), and the SDK's own showImmediate = kFalse brought no bar up on a 100-page
// comparison (KCMProgressBar.h says how far that measurement goes). The waiting is done by
// KCMDeferredProgressBar (KCMProgressBar.h), whose default this is: every deferred bar but the
// Before/After report's, which passes 0. The book comparison's bar is a KCMProgressBar, up at once.
static const int32  kKCMProgressBarDelayMs = 3000;

// Ticks handed to one chapter on the book-comparison progress bar (KCMBookCompare.cpp).
// With the chapter count alone as the denominator the needle freezes for the whole of a 100
// page chapter and Cancel does not take effect until that chapter ends. Giving each chapter a
// span lets the bar advance inside a chapter, which also makes Cancel feel immediate.
// The value itself carries no meaning - the denominator is chapters * span, so only the ratio
// matters. With the pixel judgement's share below (kKCMBookPixelPhaseTicks of it), every page of a
// chapter of up to that many pages moves the bar by at least one tick.
static const int32  kKCMChapterProgressSpan = 1000;

// How one chapter's span is divided between the three judgements. Two numbers for
// three: the resources judgement, the last, has the rest of kKCMChapterProgressSpan - the next
// chapter's slice begins at the next span (KCMCompareBooks). So the one sum to keep is that the two
// leave it some: past the span the needle would run into the next chapter's slice and go back when
// that chapter starts (the static_assert below).
// THE SHARES ARE THE MEASURED COSTS, NOT EQUAL THIRDS. The pixels are the only phase that can
//   report from inside itself (page by page), and they are most of the time on an unchanged
//   chapter. The story judgement reads change counters, composes nothing and is over at once. The
//   resources judgement is two whole-document XML exports, 200-2400ms. Equal thirds would leave
//   the needle sitting still through the two phases that take the time and jumping through the one
//   that does not, which is worse than no bar: it would say the work is elsewhere.
static const int32  kKCMBookPixelPhaseTicks = 700;
static const int32  kKCMBookStoryPhaseTicks = 50;
static_assert(kKCMBookPixelPhaseTicks + kKCMBookStoryPhaseTicks < kKCMChapterProgressSpan,
			  "the pixel and story shares leave the resources judgement some of a chapter's span");

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
// green "/" (registered) and the red "/" (not compared).
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
// THE HIT BOX FOR LIFTING A PAW IS DERIVED FROM THIS ONE VALUE (KCMPawHalfSizeForPage), so
//   what the reader can see is exactly what they can lift. Writing the size in two places would
//   let the picture and the target drift apart, and the drift would only show as "sometimes the
//   paw will not come off" (memory one-question-one-place).
// ⚠The colour lives here too rather than in the drawing file, for the same reason the tick's does.
static const PMReal kKCMPawSizeRatio = 0.05;	// paw size, as a fraction of the page short side

// TWO COLOURS, ONE SIZE (the author's decision).
//   ⚠The modifier keys once changed the SIZE, and the author replaced the whole idea after putting
//   a big one on a real page: **a bigger paw is the same mark drawn larger, while a different colour
//   is a different KIND of mark.** Every paw is the ordinary size, and Shift+Alt swaps between:
//       red    the default, what a plain press puts down until the colour is swapped
//       blue
//
//  **BOTH COLOURS ARE BORROWED FROM KOHAKU INDESIGN MCP, AND THAT IS THE POINT** (the author).
//    The two plug-ins are twins that mark the same documents for the same reader
//    (memory kcm-kidmcp-story-diff-twins), and over there the two hands are told apart by colour
//    alone: **Claude's own marks are red, the person's blue pencil is blue.** A paw carries the
//    same reading here, so the shades are the same numbers and not merely similar ones:
//      red  = KIDMCPMarkDraw.cpp's kClaudeRed  (255, 59, 48)  = #FF3B30
//      blue = KIDMCPSettings.h's fPencilColor  ( 30, 91, 255) = #1E5BFF
//    ⚠**Changing either one here alone breaks the pairing silently** -- nothing in a build can see
//     that the twin moved. If one changes, change both, or the reading is gone.
static const uint8 kKCMPawRedR  = 255, kKCMPawRedG  =  59, kKCMPawRedB  =  48;	// the default
static const uint8 kKCMPawBlueR =  30, kKCMPawBlueG =  91, kKCMPawBlueB = 255;

// **THE SHAPE AND THE WORD ARE NOT THE SAME SHADE** (the author: "the paw part only,
//   pink -- and the blue one a blue with white mixed in; the text stays as it is").
//   The paw is a big solid blob and the word is a few thin strokes, so one colour cannot serve
//   both: at full strength the blob shouts over the page, and softened, the word stops being
//   readable. ⇒ **The paw is the colour mixed with white; the word is the colour itself.**
//   ⚠This is why the drawing carries TWO colours and passes the second one down to the word --
//     dropping to one would look like a simplification and would undo the decision.
// 0 = the colour as it is, 1 = white.
//   0.50 -> red (255,157,151), blue (142,173,255)   -- the first cut, "make it paler"
//   0.65 -> red (255,196,192), blue (178,198,255)   -- where it stands
// ⚠**The WORD does not follow this**, and that is the point of having two shades: paling the paw
//   further makes the note beside it MORE readable, not less, because the contrast between them
//   grows. There is no number here that trades one against the other.
static const PMReal kKCMPawFillWhiteMix = 0.65;

// **AND THE PAW GETS A WHITE OUTLINE TOO** (the author, after seeing the note's:
//   "can the cat paw illustration have a white edge as well?"). Same reason as the word's -- a
//   softened pink over a dark photograph is a smudge until something separates it from the
//   background -- and the same trick: stroke white first, fill over it.
//   ⚠**The path has to be BUILT TWICE**, because a stroke consumes it: five subpaths, stroke, five
//     subpaths again, fill. Stroking and filling one path in one pass would leave the white lines
//     the pad and the toes make where they overlap INSIDE the shape, which is the seam the single
//     merged path exists to hide.
// ⚠**RAISING THIS PUSHES THE PICTURE PAST THE TARGET.** The outline is centred on the paw's edge,
//   so half of it falls OUTSIDE, and the hit box for lifting is a square of +/- half the paw's size
//   (KCMPawHalfSizeForPage). The outlines themselves reach 0.486 of the size, so the sums are:
//       0.10 -> the rim ends at 1.07 x the hit box     0.16 -> 1.11 x
//   ⇒ a thin band of what can be SEEN cannot be lifted, and KCMPawStamp.h's promise that "what can
//     be seen is exactly what can be lifted" is about the paw, not its rim. Known and accepted;
//     if it ever bites, the fix is to widen the hit box, not to thin the rim.
static const PMReal kKCMPawHaloRatio = 0.16;	// the outline's width, as a fraction of the paw's size

// Which of the two a stamp was placed in.
// An enum rather than a stored colour: the shades themselves live above, in one place, so
//   changing one never means going near the saved data (memory one-question-one-place).
//
// ⚠**0, 1 AND 2 ARE RETIRED AND MUST NOT BE REUSED.** They were pink, cyan and green, and they
//   are IN THE READER'S DOCUMENTS -- in the pages' script labels. Giving a new colour an old
//   number would silently repaint every paw saved with the old three, and nothing would report
//   it. The new values therefore start at 3, and KCMPawColourFromStored (KCMPawStamp.h) is the one
//   place that turns an old number into a new one.
enum KCMPawColour
{
	kKCMPawColourBlue	= 3,
	kKCMPawColourRed	= 4
};

// The word an Alt press puts beside a paw (the author's request).
//
// **18pt, and BOLD THE WAY KOHAKU INDESIGN MCP IS BOLD** -- which is not a bold FACE but a faux
//   bold: the glyphs are filled and then stroked at this fraction of the point size, so it does
//   not depend on the substitute font having a bold face at all (KIDMCPMarkDraw.cpp's kBoldStroke,
//   the same 0.04). KCM asks the font manager for the default font exactly as the original page
//   numbers do, and that font is whatever the system gives -- so the same care applies.
// ⚠The size is in POINTS ON THE PAGE, so it follows the zoom, like the paw itself. (The original
//   page numbers are the other kind, fixed on screen; a caption belongs to the page, not the view.)
static const PMReal kKCMPawTextPt         = 18.0;
static const PMReal kKCMPawTextBoldStroke = 0.0;	// line width as a fraction of the size; 0 = no faux bold
// ⚠**0 ON PURPOSE** (the author: "try taking the weight off the text once"). At 0.04 -- Kohaku
//   InDesign MCP's own faux bold -- the note came out heavier than the page it sits on. Putting the
//   weight back is this one number; the drawing skips the stroke pass while it is zero, so nothing
//   else has to change either way.
static const PMReal kKCMPawTextGapRatio   = 0.35;	// gap between the paw's edge and the word, as a fraction of the paw's size
// What is DRAWN. ⚠**The label always keeps the whole string** -- these two cut the picture, never
// the note, so a reader who typed more than fits still has all of it in the document and in the
// Script Label panel.
static const int32  kKCMPawTextMaxChars   = 60;		// per line
static const int32  kKCMPawTextMaxLines   = 6;		// the box takes about four; a few more are drawn
static const PMReal kKCMPawTextLineRatio  = 1.25;	// line spacing, as a multiple of the point size

// **A WHITE OUTLINE ROUND THE LETTERS, NOT A BOX BEHIND THEM** (the author, replacing
//   the ground: "drop the white background -- red letters with a white edge, the edge opaque, draw
//   the edge and then the letters over it").
//   A caption goes wherever the reader pressed, which is over the page's own text as often as not,
//   and coloured letters on top of black letters are neither. **A box hid the page as well as the
//   words** -- and what is under the note is the thing the note is ABOUT. An outline lifts the
//   letters off whatever is behind them while hiding almost none of it.
//   This is the trick the original-page-number badge already uses on this page, which is why it
//     is a known quantity here rather than an experiment.
//   ⚠**Order is the whole of it**: every outline is stroked FIRST, and the letters are filled over
//     the lot. Drawing one line's outline after another line's letters would eat into them.
static const PMReal kKCMPawTextHaloRatio  = 0.16;	// the outline's width, as a fraction of the point size

// (No paw on the Pages panel thumbnail - the author dropped the idea after trying it.
//  KCMDrawEventHandler.cpp keeps the note on what it cost and what it taught.)

// Fill that shows which areas are excluded from the comparison as page-number regions. While
// the exclusion toggle is on, every excluded rectangle is painted in translucent green so the
// excluded area can be seen, thin enough that the page number underneath still shows through.
// It is a vector rectangle plus setopacity, so it composites correctly on screen and in print.
static const uint8  kKCMExcludeFillR = 0, kKCMExcludeFillG = 200, kKCMExcludeFillB = 0;
static const PMReal kKCMExcludeFillOpacity = 0.25;	// opacity of the excluded-area fill (0..1). Lowered from 0.35 at the author's request (spec map MK-19): it sits over the design, so it has to be readable through.

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

// Story ID labels (flyout "Show Story IDs"). "StoryID:<the story's UID>", written ABOVE
// every page item that carries a story - outside the item, sitting on its top edge at the left (the
// author's asks, in order: the number only, outside the frame so it never lies over the text; twice
// the size; then three quarters of that, with "StoryID:" in front). Two stories on one item (a frame
// whose path carries text as well) are two such labels on the one line. The same default font as
// the badge above, black with a white outline (the paw's note's method: stroke pass first, fill
// pass second). On screen the size is a fixed number of pixels, whatever the zoom (the size in
// points is this / the view's scale, the way the rings pick their radius); in print and PDF, where
// there is no view, it is a fixed size in points. Opaque - no transparency group, so nothing here
// touches the flattener.
static const char* const kKCMStoryIdPrefix = "StoryID:";	// in front of every number (the author's ask)
static const PMReal kKCMStoryIdFontPx    = 16.5;	// on screen, in screen pixels (11 -> 22 -> 75% of 22, the author's asks)
static const PMReal kKCMStoryIdPrintPt   = 10.5;	// in print and PDF, in points (the same steps)
static const PMReal kKCMStoryIdPadRatio  = 0.25;	// the gap between the item's top edge and the bottom of the digits, as a fraction of the size
static const PMReal kKCMStoryIdHaloRatio = 0.14;	// the white outline's stroke width, as a fraction of the size
// How far the label reaches beyond the item's own box, in SCREEN PIXELS: the band above it (one
// line plus the gap) and how far right it can run (two "StoryID:12345" and the space between). This
// is what the adornment adds to the item's painted bounds, so the label is repainted with the
// item: a label drawn outside the box would otherwise be left behind when the item moves.
static const PMReal kKCMStoryIdBandAbovePx = 26.0;	// kKCMStoryIdFontPx * (1 + pad) with room for the halo
static const PMReal kKCMStoryIdReachRightPx = 260.0;

// ⚠ THE UI HALF'S OWN TUNING CONSTANTS ARE NOT HERE. A value only the UI half reads sits at the top
// of the file that reads it - the translucent panel's alpha and its delayed re-apply
// (kKCMPanelAlphaValue / ...ReapplyTries / ...ReapplyDelayMillis) in ui/KCMPanelAlpha.cpp, as
// KCMScrollMap.cpp, KCMThumbIdleTask.cpp and KCMTrackerHud.cpp keep theirs - the shape the product
// code uses (linksui/LinksUIUtils.cpp). What stays here is read by the model half, and the head of
// this file says which of it the UI half reads too.

#endif // __KCMConstants_h__
