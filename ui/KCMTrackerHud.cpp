//========================================================================================
//
//  KCMTrackerHud.cpp
//
//  While the left button is held, the **top-left** of the view that was pressed says what that
//    window is to the comparison (Target / Source / Not in comparison / Not comparing). What it is
//    for, how it came about, and why a draw event CAN paint in a corner, are at the top of
//    KCMTrackerHud.h.
//  ⚠This line said "top-right" for a while. The implementation puts it top-LEFT
//    (kKCMTrackerHudLeftPx below is measured from boundsPb.Left()), and KCMTrackerHud.h and
//    KCMTracker.cpp both said left ---- **one line of this file was the only one that disagreed**.
//    The old sprite version was top-left as well.
//
//  What this file holds is two things: "is the button down" and "which view". The comparison state
//  is asked of the model side, so no state is kept twice. The drawing is called by
//  **KCMUIDrawEventHandler::HandleDrawEvent (KCMUIDrawEvent.cpp)** on two routes (in front of the
//  band, and behind on the canvas).
//  ⚠It used to say "KCMDrawEventHandler::HandleDrawEvent". That is the **model side's** mark
//    drawing, and the HUD moved here with the model/UI split ---- the model side said so itself
//    ("this file does not include KCMTrackerHud.h") while this side still claimed otherwise.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IControlView.h"		// GetContentToWindowMatrix (the zoom) / GetBBox / WindowToContentTransform
#include "IGraphicsPort.h"		// rectfill / selectfont / show / the transparency group
#include "IFontMgr.h"			// QueryFont / QueryFontInstance
#include "IPMFont.h"
#include "IFontInstance.h"		// MeasureWText / GetAscent / GetDescent (the size of the plate behind the text)
#include "ISession.h"			// GetExecutionContextSession
#include "AutoGSave.h"
#include "WideString.h"			// the UTF16 handed to show
#include "PMMatrix.h"
#include "PMRect.h"

#include "Utils.h"				// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// the armed state, asked across the boundary
#include "KCMViewLookup.h"	// KCMFindDocDbForView
#include "KCMTrackerHud.h"

//----------------------------------------------------------------------------------------
// The appearance. Every value is in SCREEN pixels: dividing by the real zoom converts them to
// content units, so the HUD looks the same at any zoom.
//----------------------------------------------------------------------------------------
// ★All of them are the values of the old sprite HUD that was removed (user's instruction: "the
//   position and so on the same as the previous HUD, up in the top-left"). The original is in the
//   git history. The white plate and the black text are the same too.
static const PMReal kKCMTrackerHudTextPx     = 20.0;		// the size of the text
static const PMReal kKCMTrackerHudPadXPx     = 8.0;		// the plate's padding, left and right
static const PMReal kKCMTrackerHudPadTopPx   = 4.0;		// the plate's padding above
static const PMReal kKCMTrackerHudPadBotPx   = 4.0;		// the plate's padding below
static const PMReal kKCMTrackerHudOpacity    = 0.6;		// how far the plate AND the text together are faded (1.0 = opaque)
static const PMReal kKCMTrackerHudLeftPx     = 20.0;		// the inset from the view's left edge (★it sits top-left, so the left is the reference)
static const PMReal kKCMTrackerHudBaselinePx = 40.0;		// from the view's top edge to the text baseline

//----------------------------------------------------------------------------------------
// The state held only while the button is down
//----------------------------------------------------------------------------------------
static bool16        sActive = kFalse;	// kTrue only while the left button is held
static IControlView* sView   = nil;		// the layout view that was pressed (borrowed; only ever compared)

// The font (owned here). All four wordings are ASCII, so the default font is enough ---- the old HUD
// showed a document name and therefore needed a three-step "pick a font that has these characters"
// (gPort's show uses the glyphs of one font and has no fallback of the kind the OS does, so Japanese
// came out as boxes). Fixed Latin text cannot hit that.
// ★The user's decision that **the other document's name is not shown** is what keeps it that way.
static IPMFont*       sFont         = nil;
static IFontInstance* sFontInst     = nil;
static PMReal         sFontInstSize = 0.0;

static void KCMTrackerHudReleaseFont()
{
	if (sFontInst != nil)
	{
		sFontInst->Release();
		sFontInst = nil;
	}
	sFontInstSize = 0.0;
	if (sFont != nil)
	{
		sFont->Release();
		sFont = nil;
	}
}

/** The default font, cached. It is owned here ＝ the caller does not Release it. */
static IPMFont* KCMTrackerHudQueryFont()
{
	if (sFont != nil)
		return sFont;

	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return nil;
	sFont = fontMgr->QueryFont(fontMgr->GetDefaultFontName());
	return sFont;
}

/** The instance used to measure and draw at that size, cached. Owned here.
	Built the way the old page-number badge in KCMDrawEventHandler.cpp built its own: a matrix with the
	size on the diagonal.
	★The size changes with the zoom, so it is rebuilt when it changes. */
static IFontInstance* KCMTrackerHudQueryFontInstance(IPMFont* font, const PMReal& size)
{
	if (font == nil || size <= 0)
		return nil;
	if (sFontInst != nil && sFontInstSize == size)
		return sFontInst;

	if (sFontInst != nil)
	{
		sFontInst->Release();
		sFontInst = nil;
	}
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
	if (fontMgr == nil)
		return nil;
	const PMMatrix fontMatrix(size, 0.0, 0.0, size, 0.0, 0.0);
	sFontInst     = fontMgr->QueryFontInstance(font, fontMatrix);
	sFontInstSize = size;
	return sFontInst;
}

/** Build the one line the HUD shows. Four wordings, by the pressed window and the comparison state:
	  comparing + the Target window  -> "Target"              ... this window is the Target (the newer)
	  comparing + the Source window  -> "Source"              ... this window is the Source (the older)
	  comparing + anything else      -> "Not in comparison"   ... this document is not being compared
	  stopped                        -> "Not comparing"       ... nothing is being compared at all
	★It says only what the pressed window IS. The other document's name is not shown (user's
	  instruction): the role of the window under the hand, at a glance, is worth more than a HUD that
	  stretches with a long file name.
	★"Nothing appears" is never used to mean something (it cannot be told from being broken). One of
	  the four always appears.
	★The text is English, fixed, and always passed through SetTranslatable(kFalse) so that it cannot
	  be taken for a translation key (a UI literal really has turned into a built-in translation in
	  this plug-in).
	★The wordings and the test are inherited unchanged from KCMBuildHudText of the old sprite HUD. */
static PMString KCMTrackerHudLabel(IControlView* view)
{
	PMString out;

	if (!Utils<IKCMCompareFacade>()->IsArmed())
		out = PMString("Not comparing");
	else
	{
		IDataBase* const db = KCMFindDocDbForView(view);	// the pressed window's document (only ever compared as a pointer)
		if (db != nil && db == Utils<IKCMCompareFacade>()->GetArmedTargetDB())
			out = PMString("Target");
		else if (db != nil && db == Utils<IKCMCompareFacade>()->GetArmedSourceDB())
			out = PMString("Source");
		else
			out = PMString("Not in comparison");
	}

	out.SetTranslatable(kFalse);
	return out;
}

/** Ask for a repaint of the pressed window's document.
	★★Why it is needed (reported from the running application: "it does not say Source over the
	  source", "it does not say Not while stopped"): the HUD is drawn on a draw event ＝ it depends on
	  **somebody causing a repaint**. What causes one on a press is the reveal, and that **only runs
	  over the Target window** (KCMTrackerRevealBegin in KCMPeekGesture.cpp returns early on
	  `KCMMouseIsOverTarget()`).
	  ∴ over the Source window, while stopped, and over a third document, the occasion to draw never
	  came and the HUD was never drawn at all. It asks for the repaint that shows it and the one that
	  clears it **itself**, riding on no other feature's.
	★It is done per DOCUMENT (KCMInvalidateDB = Utils<ILayoutUtils>()->InvalidateViews) so that it
	  takes the same road KCM uses to show and hide the frames. The HUD is drawn only in the pressed
	  window (the view test in KCMTrackerHudWantsDraw), so repainting the document's other views
	  changes nothing on screen. It happens twice - press and release - so the cost does not matter.
	★Over the Target window it coincides with the reveal's repaint, but InDesign coalesces invalid
	  regions and draws once. */
static void KCMTrackerHudInvalidate(IControlView* view)
{
	if (view != nil)
		Utils<IKCMCompareFacade>()->InvalidateDB(KCMFindDocDbForView(view));
}

void KCMTrackerHudBegin(IControlView* view)
{
	sActive = (view != nil);
	sView   = view;
	KCMTrackerHudInvalidate(view);	// ask for the repaint that shows it (see the comment above)
}

void KCMTrackerHudEnd()
{
	IControlView* const view = sView;	// kept before it is cleared: the repaint that erases the HUD needs it
	sActive = kFalse;
	sView   = nil;
	// ★The order matters: lower the flag first, then ask (the other way round, this very repaint
	//   draws the HUD once more).
	KCMTrackerHudInvalidate(view);
	// The font may be kept (the next press uses it as it is). Only Shutdown returns it.
}

bool16 KCMTrackerHudWantsDraw(IControlView* view)
{
	// ★Only in the window that was pressed. A draw with a nil view (a Pages panel thumbnail being
	// generated, for instance) is out of scope as well.
	return (sActive && view != nil && view == sView) ? kTrue : kFalse;
}

void KCMTrackerHudDraw(IGraphicsPort* gPort, IControlView* view, const PMPoint& spreadOffset)
{
	if (gPort == nil || view == nil)
		return;

	// The real zoom, used to convert the screen-pixel values into content units. A negative scale is
	// possible, hence the absolute value.
	PMReal sx = 1.0, sy = 1.0;
	{
		const PMMatrix toWindow = view->GetContentToWindowMatrix();
		sx = abs(toWindow.GetXScale());
		sy = abs(toWindow.GetYScale());
	}
	if (sx == 0 || sy == 0)
		return;

	// The view's visible area in pasteboard coordinates (the same steps the old toast took: take the
	// bbox in window coordinates and transform it to content).
	// ★This is what "the corner of the view" really is. Scroll or zoom as you like, this rectangle is
	//   always "what can be seen right now".
	PMRect boundsPb = view->GetBBox();
	view->WindowToContentTransform(&boundsPb);

	const PMString labelStr = KCMTrackerHudLabel(view);
	WideString     label(labelStr);
	IPMFont*       font = KCMTrackerHudQueryFont();
	if (font == nil)
		return;

	const PMReal fontSize = PMReal(kKCMTrackerHudTextPx) / sy;

	// The plate is sized by measuring (character count times a fixed width is wrong often enough).
	// Where the measurement cannot be had, it falls back to that estimate.
	PMReal textW   = 0.0;
	PMReal ascent  = fontSize * PMReal(0.8);
	PMReal descent = fontSize * PMReal(0.2);
	IFontInstance* inst = KCMTrackerHudQueryFontInstance(font, fontSize);
	if (inst != nil)
	{
		inst->MeasureWText(label, textW);
		ascent  = inst->GetAscent();
		descent = inst->GetDescent();
	}
	if (textW <= 0)
		textW = fontSize * PMReal(0.6) * PMReal(label.CharCount());

	// Top-left (where the old HUD sat). show puts the left end of the baseline at (x,y), so the left
	// inset IS tx.
	// ★Brought into this port's coordinates by subtracting spreadOffset from the pasteboard value (a
	//   translation and nothing more).
	const PMReal tx = boundsPb.Left() + PMReal(kKCMTrackerHudLeftPx) / sx - spreadOffset.X();
	const PMReal ty = boundsPb.Top()  + PMReal(kKCMTrackerHudBaselinePx) / sy - spreadOffset.Y();

	// The plate's rectangle, which is also the extent of the transparency group. A PMRect is (left,
	// top, right, bottom).
	const PMRect hudRect(tx - PMReal(kKCMTrackerHudPadXPx) / sx,
	                     ty - ascent - PMReal(kKCMTrackerHudPadTopPx) / sy,
	                     tx + textW + PMReal(kKCMTrackerHudPadXPx) / sx,
	                     ty + descent + PMReal(kKCMTrackerHudPadBotPx) / sy);

	// ★The plate and the text are bound into a transparency group and the opacity is applied **once,
	//   to the group**. Applying setopacity to each of them separately makes the pixels where the text
	//   overlaps the plate darker than the rest (the same practice as the old page-number badge and the
	//   old HUD).
	AutoGSave ag(gPort);
	gPort->setopacity(PMReal(kKCMTrackerHudOpacity), kFalse);
	gPort->starttransparencygroup(hudRect, nil, kFalse /*non-isolated*/, kFalse /*no knockout*/);

	// (1) The plate: solid white. rectfill takes (left, top, width, height).
	gPort->newpath();
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->rectfill(hudRect.Left(), hudRect.Top(), hudRect.Width(), hudRect.Height());
	gPort->newpath();

	// (2) The text: black. The plate is what makes it readable, so it needs no rim and no blending.
	gPort->setrgbcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0));
	gPort->selectfont(font, fontSize);
	gPort->show(tx, ty, label.NumUTF16TextChars(), label.GrabUTF16Buffer(nil), IGraphicsPort::kFillText);
	gPort->newpath();

	gPort->endtransparencygroup();
}

void KCMTrackerHudShutdown()
{
	sActive = kFalse;
	sView   = nil;
	KCMTrackerHudReleaseFont();	// always return the font reference before the .pln goes down
}

// End, KCMTrackerHud.cpp.
