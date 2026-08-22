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
//  ★★AND, SINCE 2026-08-22, A RUBY CHANGE IS DRAWN ON TWO LINES - the reading above the characters
//  it belongs to, at the same size, the way ruby is actually set (user: "文字のサイズは同じで、位置を
//  漢字の文字の上に、実際のルビの様に" / "位置が重要"). The cell divides its own height in half for
//  those rows; the row is built tall enough for that by KESCMStoryTreeWidgetMgr, which asks the same
//  question this cell is told the answer to.
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
	KESCMStoryCellData(IPMUnknown* boss)
		: CPMUnknown<IKESCMStoryCellData>(boss), fTwoLines(kFalse) {}
	virtual ~KESCMStoryCellData() {}

	virtual void SetSegments(const PMString& pre, const PMString& mid, const PMString& post,
							 const PMString& ruby, bool16 twoLines)
	{
		// ★Not translation keys. This is text out of a document, and a short common word can
		//   otherwise be looked up in the string tables and come back as something else entirely
		//   (memory menu-string-translation-traps). The model already says so on its side; saying
		//   it again here costs nothing and means the cell is safe whoever writes to it.
		fPre = pre;   fPre.SetTranslatable(kFalse);
		fMid = mid;   fMid.SetTranslatable(kFalse);
		fPost = post; fPost.SetTranslatable(kFalse);
		fRuby = ruby; fRuby.SetTranslatable(kFalse);
		fTwoLines = twoLines;
	}

	virtual void GetSegments(PMString& outPre, PMString& outMid, PMString& outPost,
							 PMString& outRuby, bool16& outTwoLines) const
	{
		outPre = fPre;
		outMid = fMid;
		outPost = fPost;
		outRuby = fRuby;
		outTwoLines = fTwoLines;
	}

private:
	PMString fPre;
	PMString fMid;
	PMString fPost;
	PMString fRuby;
	bool16   fTwoLines;
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

	PMString pre, mid, post, ruby;
	bool16 twoLines = kFalse;
	data->GetSegments(pre, mid, post, ruby, twoLines);

	// ★NOTHING IS PAINTED BEHIND THE TEXT. The row widget draws the row's background and its
	//   selection fill; this cell adds the words on top, exactly as the stock cell it replaced did.
	//   An empty row is therefore a no-op rather than a blank rectangle - which is what a recycled
	//   widget waiting for its next apply has to look like.
	// ⚠The reading counts as something to draw: a recycled widget that kept only a ruby would
	//   otherwise paint it over the row it has become.
	if (pre.IsEmpty() && mid.IsEmpty() && post.IsEmpty() && ruby.IsEmpty())
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

	// ★★A RUBY CHANGE IS DRAWN ON TWO LINES, AND THEY ARE THE TWO HALVES OF THE CELL (2026-08-22,
	//   user's request: "ルビ以外を薄くして、本当にルビが付いているような見た目に" / "ちいさくなくても
	//   いいです、文字のサイズは同じで、位置を 漢字の文字の上に、実際のルビの様に" / "位置が重要").
	//   The base text keeps the LOWER half and the reading stands in the upper one, over the
	//   characters it belongs to - which is where a reader of Japanese expects to find it.
	//
	// ★HOW THE TWO BASELINES ARE WORKED OUT. GetViewYPosition answers "the baseline for a box this
	//   tall", and the product's own drawing hands it a height that is NOT the widget's when it
	//   wants a row inside a taller view (MSOStateDDLElementView.cpp:216,236 passes a constant row
	//   height). So a half-height box is asked for once and used twice: as it stands for the upper
	//   line, and pushed down by that same half for the lower one. Both lines are then centred in
	//   their own half, which is what keeps the base text sitting where the eye expects it.
	// ⚠frame.Top() is not added, here or below - the one-line case has always drawn at these
	//   coordinates and draws correctly, so the inner content frame starts at 0.
	const PMReal lineHeight = twoLines ? (frame.Height() / PMReal(2.0)) : frame.Height();
	const PMReal upperY = Utils<IWidgetUtils>()->GetViewYPosition(&gc, fontInfo, lineHeight);
	const PMReal y = twoLines ? (upperY + lineHeight) : upperY;

	const PMReal leftEdge = frame.Left();
	const PMReal rightEdge = frame.Right();
	PMReal x = leftEdge;

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

	// ★WHERE THE CHANGED CHARACTERS ACTUALLY LANDED. The reading has to stand over THEM, and where
	//   they land is not known until the line has been laid out: all three branches below place the
	//   change at a different x, because how much leading context fitted decides it. So the change
	//   is drawn through here, which records the span it occupied for the ruby pass at the end.
	PMReal drawnMidX = leftEdge;
	PMReal drawnMidW = PMReal(0.0);

	auto drawChange = [&](const PMString& s)
	{
		drawnMidX = x;
		drawRun(s, kChangeColor);
		drawnMidW = x - drawnMidX;
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
		drawChange(mid);
		drawRun(post, kContextColor);
	}
	else if (midW >= availWidth)
	{
		// The change alone overflows the cell: ellipsize the change itself (tail) and drop the
		// context. Nothing is lost that the reader could have used - the context is only there to
		// place a change that is too short to place itself.
		const PMString m = StringUtils::PMEllipsizeString(&gc, availWidth, mid, fontInfo, kEllipsizeEnd, nil, kDontConvertAmpersand);
		drawChange(m);
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
		drawChange(mid);
		drawRun(postCut, kContextColor);
	}

	// ---- the upper line: the reading, over the characters it belongs to -------------------
	//
	// ★NOTHING IS DRAWN FOR A RUBY THAT WAS TAKEN AWAY, and that is the decision rather than an
	//   oversight (user's call, 2026-08-22). The row shows the NEWER version, where there is no
	//   reading any more, so an empty upper line is what that version actually looks like; the
	//   reading that was removed is read in the panel's message area, which shows the other side.
	//   The row is still laid out on two lines - see the widget manager - so the base text does not
	//   jump half a row against the rows above and below it.
	if (twoLines && !ruby.IsEmpty())
	{
		const PMReal rubyW = StringUtils::PMMeasureString(&gc, ruby, fontInfo, kDontConvertAmpersand).X();

		// ★CENTRED ON THE BASE CHARACTERS, and worked out by the rule the message area uses too
		//   (KESCMPanelTextDraw.h) - that box draws the OLDER version's reading over the same kind
		//   of base text, and the position is the part the reader will judge first.
		const PMReal rubyX = KESCMRubyX(drawnMidX, drawnMidW, rubyW, leftEdge);

		PMString shown = ruby;
		if (rubyX + rubyW > rightEdge)
		{
			const PMReal room = rightEdge - rubyX;
			if (room <= PMReal(0.0))
				return;
			// Cut the TAIL: a reading is read from its head, and the head is what identifies it.
			shown = StringUtils::PMEllipsizeString(&gc, room, ruby, fontInfo, kEllipsizeEnd, nil, kDontConvertAmpersand);
		}

		// ★FULL STRENGTH, like the changed characters below it - the reading IS the change on
		//   these rows. What stays faded is the context on the lower line, which is what "ルビ以外
		//   を薄く" asks for.
		StringUtils::PMDrawStringRGB(&gc, PMPoint(rubyX, upperY), shown, fontInfo, kChangeColor,
									 kDontConvertAmpersand, kNoUnderline);
	}
}

// End, KESCMStoryCellView.cpp.
