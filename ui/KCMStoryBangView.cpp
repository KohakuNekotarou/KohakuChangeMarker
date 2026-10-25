//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) UI - the Δ cell of a "!" row: a red "!" drawn by hand
//
//  WHAT THIS IS. A story an import could not fill - wholly, or one change of it - gets a row at the
//  top of Story Edits with a red "!" in the Δ column (2026-09-19, the user's ask: "what could not
//  be imported - a red ! in the Δ column"). This is that cell.
//
//  ★WHY BY HAND. A stock static text draws its whole string in ONE colour, the theme's text colour,
//  and there is no per-widget colour in the SDK's static text. The same reason the change row's
//  text cell is drawn by hand (KCMStoryCellView.cpp); this one is the smallest possible version of
//  it - one character, one colour, centred - and carries no data interface, because it says the
//  same thing every time it is drawn. Which row it is, the row beside it says.
//
//  ★THE RED IS NOT THE THEME'S. Every other colour in these rows comes from IInterfaceColors so that
//  a dark UI flips it; a warning mark is the one thing that must NOT flip - red on light and red
//  on dark are both red. It is a fixed colour, chosen to be readable on both (0.85, 0.10, 0.10).
//
//  ★AND THE "=" OF A ROW APPLY THIS CHANGE PUT IN (2026-10-24 evening, the import-as-Source road - the author:
//  "when a change goes in and shows =, make the = red"): the same cell with "=" for its mark (KCMStoryAppliedSignView
//  below, kKCMStoryAppliedSignCellBoss). ★★AMBER SINCE 2026-10-24 NIGHT (the author: "make the = of the Word import
//  amber") - the plug-in's own colour (Kohaku); the "!" stays red: it is a warning, and an "=" is not one. Not one
//  amber: KCMAppliedAmber says why the theme decides the shade.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IInterfaceColors.h"	// RealAGMColor
#include "IInterfaceFonts.h"	// the palette window font, the one the neighbouring cells declare

// General includes:
#include "DVAPublicIncludes.h"	// dvaui::drawbot - what DV_Draw is handed
#include "DVControlView.h"
#include "ISession.h"			// GetExecutionContextSession
#include "ShuksanID.h"			// kPaletteWindowSystemScriptFontId

// Project includes:
#include "KCMUIID.h"
#include "KCMPanelTextDraw.h"	// KCMDrawnWidth / KCMDrawInBox - drawn by drover as every hand-drawn text here is (2026-10-24)

namespace
{

/** The mark, as UTF-16 so that the same file could draw a non-ASCII one without a code page in the
	way (the change row's "≠" is set the same way). */
const char16_t kKCMBangMark[] = u"!";
/** The applied row's mark (2026-10-24 evening). */
const char16_t kKCMAppliedMark[] = u"=";

/** The "!" cell's colour. Fixed on purpose - see the file comment. */
const RealAGMColor kKCMBangRed(0.85, 0.10, 0.10);

/** The "=" cell's colour - amber (2026-10-24 night), in the shade the theme can carry. ★ONE AMBER DOES NOT READ ON BOTH:
	a true amber (1.0, 0.75, 0) against the Bright theme's grey 240 is about 1.5:1 (WCAG's contrast ratio - an "=" two
	strokes thin all but vanishes), so the Bright theme gets a deep amber (about 3.5:1 there) and the darker themes the
	true one - measured on their greys 83 and 50: 4.7:1 and 7.8:1 (work/note-scripts/2026-10-24-kcm-list/
	verify-amber-title.ps1). Which theme is the theme's own threshold, dv_utils::IsUIInterfaceBright. */
RealAGMColor KCMAppliedAmber()
{
	return dv_utils::IsUIInterfaceBright() ? RealAGMColor(0.70, 0.44, 0.00) : RealAGMColor(1.00, 0.75, 0.00);
}

/** One mark, in `colour`, centred in `view`'s inner frame - the whole of what both cells draw. */
void DrawMark(const DVControlView* view, dvaui::drawbot::Drawbot* drawbotP, const char16_t* utf16Mark,
			  const RealAGMColor& colour)
{
	if (drawbotP == nil)
		return;

	// The same font every cell of these rows declares (KCMStoryCellView.cpp says why a hand-drawn
	// cell has to name it in code: a generic panel widget carries no font field).
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowSystemScriptFontId);

	PMString mark;
	mark.SetXString(reinterpret_cast<const UTF16TextChar*>(utf16Mark), 1);
	mark.SetTranslatable(kFalse);

	// ★CENTRED, like every other sign in the Δ column (KCMApplyListColumnWidths sets the stock
	//   cells to kAlignCenter for the reason it states there: right-aligned, a sign touches the
	//   column after it). Nothing is painted behind it - the row draws its own background and its
	//   selection fill, and this cell adds the mark on top, exactly as the stock cell would.
	const PMRect frame = view->GetInnerContentFrame();
	const PMReal width = KCMDrawnWidth(mark, fontInfo);
	const PMReal x = frame.Left() + (frame.Width() - width) / PMReal(2.0);
	KCMDrawInBox(drawbotP, PMRect(x, frame.Top(), frame.Right(), frame.Bottom()), mark, fontInfo, colour);
}

}	// namespace

/** Implements IControlView: draws a red "!" centred in the cell. */
class KCMStoryBangView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMStoryBangView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMStoryBangView() {}

	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const { DrawMark(this, drawbotP, kKCMBangMark, kKCMBangRed); }
};

CREATE_PERSIST_PMINTERFACE(KCMStoryBangView, kKCMStoryBangViewImpl)

/** Implements IControlView: draws an amber "=" centred in the cell (2026-10-24 evening - a row Apply This Change put in;
	red until the same night). */
class KCMStoryAppliedSignView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMStoryAppliedSignView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMStoryAppliedSignView() {}

	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const { DrawMark(this, drawbotP, kKCMAppliedMark, KCMAppliedAmber()); }
};

CREATE_PERSIST_PMINTERFACE(KCMStoryAppliedSignView, kKCMStoryAppliedSignViewImpl)

// End, KCMStoryBangView.cpp.
