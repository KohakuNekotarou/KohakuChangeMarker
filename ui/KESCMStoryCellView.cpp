//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The text cell of a CHANGE row: it draws the changed characters at the theme's text colour and
//  fades the words on either side of them toward the panel's background (user's request,
//  2026-08-20: "変更されたところ以外は薄い色にして欲しい、KBSを参考に").
//
//  ★A STOCK STATIC TEXT CANNOT DO THIS: it holds one string and draws it in one colour. So the
//  cell is a DVControlView that paints three runs left to right - context, change, context - the
//  same recipe KBS's hit rows use (KBSColorTextView.cpp), and the one customdatalinkui proves for
//  a tree cell drawn by hand. Everything here that has a colour asks the THEME for it, so the
//  panel keeps working in a light UI and a dark one without a single hardcoded value.
//
//  ★WHERE THE THREE PIECES COME FROM: the model splits them (KESCMStoryDiffRun's Slice) and hands
//  them across the boundary on IKESCMStoryEditsFacade::Change. The split cannot be made here - the
//  boundary between context and change is a code point index into text that has already been cut
//  at both ends, and PMString counts UTF-16.
//
//  ★ONLY THE CHANGE ROWS USE THIS. A story row keeps its stock cells: its text is the story's
//  opening words, none of which is a change, so there is nothing there to tell apart (user's call,
//  2026-08-20). The pixel mode's rows are the same rows and are equally untouched.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"		// IsHilited - is this cell's row the selected one?
#include "IGraphicsPort.h"
#include "IInterfaceColors.h"	// RealAGMColor, InterfaceColor indices
#include "IInterfaceFonts.h"	// the palette window font
#include "IWidgetParent.h"		// QueryParentFor - this cell -> the row widget that carries the hilite

// General includes:
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "DVControlView.h"
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB / PMMeasureString / PMEllipsizeString
#include "ISession.h"			// GetExecutionContextSession
#include "IWidgetUtils.h"		// GetViewYPosition
#include "ShuksanID.h"			// kPaletteWindowSystemScriptFontId
#include "Utils.h"
#include "WidgetDefs.h"			// EllipsizeStyle (kEllipsizeBeginning / kEllipsizeEnd)

// Project includes:
#include "IKESCMStoryCellData.h"
#include "KCMUIID.h"
#include "KESCMPanelTextDraw.h"	// kKESCMContextTextWeight, KESCMBlendColor - shared with the message area

namespace
{

// How far up the widget chain to look for the hilite. One step is all this list needs (cell ->
// row); the extra steps only keep it working if the row ever gains another wrapper. Same constant,
// and the same reason, as KBS's kKBSHiliteParentSteps.
const int32 kKESCMHiliteParentSteps = 3;

// ★kKESCMContextTextWeight (how far the context fades) and KESCMBlendColor (how) MOVED OUT on
// 2026-08-20, to KESCMPanelTextDraw.h. The panel's message area draws the other side of the same
// edit the same way, and two copies of "0.65" would drift apart the first time one was tuned
// ([[one-question-one-place]]). ⚠What did NOT move is the colour lookup below: this cell asks
// whether its ROW is hilited and switches both colours; the message area is never hilited.

/* KESCMViewOrParentIsHilited
   True if this view, or a widget above it, is drawn hilited - i.e. this cell belongs to the row the
   user has selected. The tree applies the hilite to the ROW widget (the base
   CTreeViewWidgetMgr::ApplyNodeIDToWidget does it, "for hilite selection"), and this cell is one of
   that row's children, so a cell that only asked itself would never see the selection.
*/
bool16 KESCMViewOrParentIsHilited(IControlView* view, int32 stepsLeft)
{
	if (view == nil)
		return kFalse;
	if (view->IsHilited())
		return kTrue;
	if (stepsLeft <= 0)
		return kFalse;

	InterfacePtr<IWidgetParent> parent(view, UseDefaultIID());
	if (parent == nil)
		return kFalse;
	InterfacePtr<IControlView> parentView((IControlView*)parent->QueryParentFor(IID_ICONTROLVIEW));
	return KESCMViewOrParentIsHilited(parentView, stepsLeft - 1);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KESCMStoryCellData - the three strings the cell draws
//----------------------------------------------------------------------------------------

/** Non-persistent holder for one change row's three text pieces, aggregated on the cell's boss
	beside the view. Written by the widget manager on every apply, read by Draw. */
class KESCMStoryCellData : public CPMUnknown<IKESCMStoryCellData>
{
public:
	KESCMStoryCellData(IPMUnknown* boss) : CPMUnknown<IKESCMStoryCellData>(boss) {}
	virtual ~KESCMStoryCellData() {}

	virtual void SetSegments(const PMString& pre, const PMString& mid, const PMString& post)
	{
		// ★Not translation keys. This is text out of a document, and a short common word can
		//   otherwise be looked up in the string tables and come back as something else entirely
		//   (memory menu-string-translation-traps). The model already says so on its side; saying
		//   it again here costs nothing and means the cell is safe whoever writes to it.
		fPre = pre;   fPre.SetTranslatable(kFalse);
		fMid = mid;   fMid.SetTranslatable(kFalse);
		fPost = post; fPost.SetTranslatable(kFalse);
	}

	virtual void GetSegments(PMString& outPre, PMString& outMid, PMString& outPost) const
	{
		outPre = fPre;
		outMid = fMid;
		outPost = fPost;
	}

private:
	PMString fPre;
	PMString fMid;
	PMString fPost;
};

CREATE_PMINTERFACE(KESCMStoryCellData, kKESCMStoryCellDataImpl)

//----------------------------------------------------------------------------------------
// KESCMStoryCellView - the self-drawing cell
//----------------------------------------------------------------------------------------

/** Implements IControlView: draws one change with its context faded around it. */
class KESCMStoryCellView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KESCMStoryCellView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KESCMStoryCellView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KESCMStoryCellView, kKESCMStoryCellViewImpl)

void KESCMStoryCellView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);

	InterfacePtr<IKESCMStoryCellData> data(this, UseDefaultIID());
	if (data == nil)
		return;

	PMString pre, mid, post;
	data->GetSegments(pre, mid, post);

	// ★NOTHING IS PAINTED BEHIND THE TEXT. The row widget draws the row's background and its
	//   selection fill; this cell adds the words on top, exactly as the stock cell it replaced did.
	//   An empty row is therefore a no-op rather than a blank rectangle - which is what a recycled
	//   widget waiting for its next apply has to look like.
	if (pre.IsEmpty() && mid.IsEmpty() && post.IsEmpty())
		return;

	// The palette window's SYSTEM SCRIPT font - the one every other cell of these two rows declares
	// in the .fr (KESCMStoryRowCellWidget names kPaletteWindowSystemScriptFontId for both its
	// normal and its hilite font), and the one the shipping panels reach for whenever a widget has
	// to show text that came out of a document: the layer panel stamps it on the layer-name cell,
	// the spell panel's misspelled-word box asks for the dialog-window counterpart. A change row is
	// exactly that case - it draws the document's own text, in whatever script it is written in.
	// ⚠A hand-drawn cell has no FontID field to read: its boss is a generic panel widget, which
	//   carries no IUIFontSpec, so the font has to be named here in code. Which is why it is named
	//   as the same ID the neighbouring cells declare rather than as "the palette font".
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowSystemScriptFontId);

	const PMRect frame = this->GetInnerContentFrame();
	const PMReal y = Utils<IWidgetUtils>()->GetViewYPosition(&gc, fontInfo, frame.Height());
	const PMReal rightEdge = frame.Right();
	PMReal x = frame.Left();

	// Is this cell's row the selected one? A hand-drawn cell has to answer that itself: a stock
	// static text is handed four colours in the .fr and lets the framework pick, but drawing by
	// hand means the two hilite colours go unused unless they are asked for here. The app's own
	// drawing makes the same switch (CRenderingObjectDrawer::DrawRenderObjectUIName).
	const bool16 isHilited = KESCMViewOrParentIsHilited(this, kKESCMHiliteParentSteps);

	// Colours, entirely from the current theme:
	//   * bg = what this row is painted on - the panel's fill, or the selection fill while the row
	//          is selected
	//   * fg = the theme's text colour for that background (black in a light UI, ~0.8 gray in a
	//          dark one; it flips with the theme, so nothing vanishes when the UI brightness does)
	// ★Both have to move together: the context is faded TOWARD bg, so leaving bg as the panel fill
	//   on a selected row would fade the context toward a colour that is not behind it any more.
	RealAGMColor bg(0.5, 0.5, 0.5), fg(0.0, 0.0, 0.0);	// sane fallbacks if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
	{
		colors->GetRealAGMColor(isHilited ? kInterfaceHighLight : kInterfacePaletteFill, bg);
		colors->GetRealAGMColor(isHilited ? kInterfaceHighLightText : kInterfaceTextColor, fg);
	}
	const RealAGMColor kChangeColor = fg;
	const RealAGMColor kContextColor = KESCMBlendColor(bg, fg, PMReal(kKESCMContextTextWeight));

	// Named rather than passed as bare kFalse, the way the app's own drawing code writes it. Every
	// call below spells both out instead of letting the defaults apply, because the defaults in
	// DrawStringUtils.h DISAGREE with each other: the draw calls default to kFalse but the measure
	// and ellipsize calls default to kTrue, so taking the defaults would measure a string
	// differently from how it is drawn. ⚠'&' has to survive verbatim in any case - this is
	// document text, not a menu label, and the .fr cell it replaced set Convert ampersands kFalse
	// for the same reason.
	const bool16 kDontConvertAmpersand = kFalse;
	const bool16 kNoUnderline = kFalse;

	const PMReal availWidth = rightEdge - x;
	if (availWidth <= PMReal(0.0))
		return;

	// Draw one run at the running x and advance past it (an empty run is a no-op).
	auto drawRun = [&](const PMString& s, const RealAGMColor& c)
	{
		if (s.IsEmpty())
			return;
		StringUtils::PMDrawStringRGB(&gc, PMPoint(x, y), s, fontInfo, c, kDontConvertAmpersand, kNoUnderline);
		x += StringUtils::PMMeasureString(&gc, s, fontInfo, kDontConvertAmpersand).X();
	};

	const PMReal preW  = pre.IsEmpty()  ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, pre,  fontInfo, kDontConvertAmpersand).X();
	const PMReal midW  = mid.IsEmpty()  ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, mid,  fontInfo, kDontConvertAmpersand).X();
	const PMReal postW = post.IsEmpty() ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, post, fontInfo, kDontConvertAmpersand).X();

	// ★THE CHANGE IS WHAT SURVIVES A NARROW PANEL. The stock cell this replaced ellipsized in the
	//   MIDDLE, which is right for a story's opening words and wrong here - the middle is exactly
	//   where the change is. So the context gives way instead, each side losing the end that faces
	//   away from the change: the leading context loses its HEAD, the trailing context its TAIL.
	//   Same three branches as KBS's hit rows.
	if (preW + midW + postW <= availWidth)
	{
		drawRun(pre, kContextColor);
		drawRun(mid, kChangeColor);
		drawRun(post, kContextColor);
	}
	else if (midW >= availWidth)
	{
		// The change alone overflows the cell: ellipsize the change itself (tail) and drop the
		// context. Nothing is lost that the reader could have used - the context is only there to
		// place a change that is too short to place itself.
		const PMString m = StringUtils::PMEllipsizeString(&gc, availWidth, mid, fontInfo, kEllipsizeEnd, nil, kDontConvertAmpersand);
		drawRun(m, kChangeColor);
	}
	else
	{
		// The change fits but the whole line does not: keep the change whole and show as much
		// context as fits around it. Leading context is served first, so the run-up to the change
		// is preferred over what follows it.
		const PMReal rem = availWidth - midW;
		PMString preCut = pre;
		if (!pre.IsEmpty())
			preCut = StringUtils::PMEllipsizeString(&gc, rem, pre, fontInfo, kEllipsizeBeginning, nil, kDontConvertAmpersand);
		const PMReal preCutW = preCut.IsEmpty() ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, preCut, fontInfo, kDontConvertAmpersand).X();

		const PMReal postBudget = rem - preCutW;
		PMString postCut;
		if (!post.IsEmpty() && postBudget > PMReal(0.0))
			postCut = StringUtils::PMEllipsizeString(&gc, postBudget, post, fontInfo, kEllipsizeEnd, nil, kDontConvertAmpersand);

		drawRun(preCut, kContextColor);
		drawRun(mid, kChangeColor);
		drawRun(postCut, kContextColor);
	}
}

// End, KESCMStoryCellView.cpp.
