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
//  ★AND THE "=" OF A ROW APPLY THIS CHANGE PUT IN (2026-10-24 evening, the import-as-Source trial - the author:
//  "when a change goes in and shows =, make the = red"): the same cell with "=" for its mark (KCMStoryAppliedSignView
//  below, kKCMStoryAppliedSignCellBoss), in the same red, for the same reasons.
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

/** The one colour these cells ever draw in. Fixed on purpose - see the file comment. */
const RealAGMColor kKCMBangRed(0.85, 0.10, 0.10);

/** One mark, red, centred in `view`'s inner frame - the whole of what both cells draw. */
void DrawRedMark(const DVControlView* view, dvaui::drawbot::Drawbot* drawbotP, const char16_t* utf16Mark)
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
	KCMDrawInBox(drawbotP, PMRect(x, frame.Top(), frame.Right(), frame.Bottom()), mark, fontInfo, kKCMBangRed);
}

}	// namespace

/** Implements IControlView: draws a red "!" centred in the cell. */
class KCMStoryBangView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMStoryBangView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMStoryBangView() {}

	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const { DrawRedMark(this, drawbotP, kKCMBangMark); }
};

CREATE_PERSIST_PMINTERFACE(KCMStoryBangView, kKCMStoryBangViewImpl)

/** Implements IControlView: draws a red "=" centred in the cell (2026-10-24 evening - a row Apply This Change put in). */
class KCMStoryAppliedSignView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMStoryAppliedSignView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMStoryAppliedSignView() {}

	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const { DrawRedMark(this, drawbotP, kKCMAppliedMark); }
};

CREATE_PERSIST_PMINTERFACE(KCMStoryAppliedSignView, kKCMStoryAppliedSignViewImpl)

// End, KCMStoryBangView.cpp.
