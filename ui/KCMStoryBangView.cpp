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
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IGraphicsPort.h"
#include "IInterfaceColors.h"	// RealAGMColor
#include "IInterfaceFonts.h"	// the palette window font, the one the neighbouring cells declare
#include "IWidgetUtils.h"		// GetViewYPosition - the baseline for a box this tall

// General includes:
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "DVControlView.h"
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB / PMMeasureString
#include "ISession.h"			// GetExecutionContextSession
#include "ShuksanID.h"			// kPaletteWindowSystemScriptFontId
#include "Utils.h"

// Project includes:
#include "KCMUIID.h"
#include "KCMPanelTextDraw.h"	// kKCMDontConvertAmpersand / kKCMNoUnderline - the same two flags every hand-drawn text here passes

namespace
{

/** The mark, as UTF-16 so that the same file could draw a non-ASCII one without a code page in the
	way (the change row's "≠" is set the same way). */
const char16_t kKCMBangMark[] = u"!";

/** The one colour this cell ever draws in. Fixed on purpose - see the file comment. */
const RealAGMColor kKCMBangRed(0.85, 0.10, 0.10);

}	// namespace

/** Implements IControlView: draws a red "!" centred in the cell. */
class KCMStoryBangView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMStoryBangView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMStoryBangView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KCMStoryBangView, kKCMStoryBangViewImpl)

void KCMStoryBangView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);

	// The same font every cell of these rows declares (KCMStoryCellView.cpp says why a hand-drawn
	// cell has to name it in code: a generic panel widget carries no font field).
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowSystemScriptFontId);

	PMString mark;
	mark.SetXString(reinterpret_cast<const UTF16TextChar*>(kKCMBangMark), 1);
	mark.SetTranslatable(kFalse);

	// ★CENTRED, like every other sign in the Δ column (KCMApplyListColumnWidths sets the stock
	//   cells to kAlignCenter for the reason it states there: right-aligned, a sign touches the
	//   column after it). Nothing is painted behind it - the row draws its own background and its
	//   selection fill, and this cell adds the mark on top, exactly as the stock cell would.
	const PMRect frame = this->GetInnerContentFrame();
	const PMReal width = StringUtils::PMMeasureString(&gc, mark, fontInfo, kKCMDontConvertAmpersand).X();
	const PMReal x = frame.Left() + (frame.Width() - width) / PMReal(2.0);
	const PMReal y = Utils<IWidgetUtils>()->GetViewYPosition(&gc, fontInfo, frame.Height());

	StringUtils::PMDrawStringRGB(&gc, PMPoint(x, y), mark, fontInfo, kKCMBangRed,
								 kKCMDontConvertAmpersand, kKCMNoUnderline);
}

// End, KCMStoryBangView.cpp.
