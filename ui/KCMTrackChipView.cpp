//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) UI - the colour square at the left of a Track mode AUTHOR row (2026-10-05).
//  ★WHY BY HAND: a stock static text draws one string in the theme's colour (KCMStoryBangView.cpp says the same).
//   The author's NAME follows the square in the same cell (the user, 2026-10-05) - the ID column, not the Story one.
//   The colour comes through IKCMStoryCellData::SetTrackLook, written on every apply (the recycling rule).
//   ★Drawn by drover, in DV_Draw (2026-10-24 - KCMPanelTextDraw.h's kKCMTextSmoothing says why).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IControlView.h"
#include "IInterfaceColors.h"		// RealAGMColor, the theme's text colours
#include "IInterfaceFonts.h"		// the palette font the name is written in

#include "DVAPublicIncludes.h"		// dvaui::drawbot - what DV_Draw is handed
#include "DVControlView.h"
#include "DVPublicUtilities.h"		// dv_utils::DVFillRect - the square
#include "ISession.h"				// GetExecutionContextSession
#include "ShuksanID.h"			// kPaletteWindowSystemScriptFontId
#include "WidgetDefs.h"			// kEllipsizeEnd

#include "IKCMStoryCellData.h"
#include "KCMUIID.h"
#include "KCMPanelTextDraw.h"		// KCMViewOrParentIsHilited, KCMDrawInBox, KCMEllipsize
#include "KCMTrackLabels.h"		// KCMTrackChipSide / the chip's offsets - shared with the column's fit

class KCMTrackChipView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMTrackChipView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMTrackChipView() {}

	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const;
};

CREATE_PERSIST_PMINTERFACE(KCMTrackChipView, kKCMTrackChipViewImpl)

void KCMTrackChipView::DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const
{
	InterfacePtr<IKCMStoryCellData> data(this, UseDefaultIID());
	if (drawbotP == nil || data == nil)
		return;
	bool16 on = kFalse;
	RealAGMColor colour(0.0, 0.0, 0.0);
	PMString unused;
	data->GetTrackLook(on, colour, unused);
	if (!on)
		return;		// a recycled widget waiting for its next apply draws nothing

	// The square, on whole pixels - the panel's bars' rule (KCMDrawCaret): a fill that straddles pixels is smoothed paler.
	const PMRect frame = this->GetInnerContentFrame();
	const PMReal side = KCMTrackChipSide(frame.Height());
	const PMReal left = ::Round(frame.Left() + PMReal(kKCMTrackChipLeft));
	const PMReal top = ::Round(frame.Top() + (frame.Height() - side) / PMReal(2.0));
	dv_utils::DVFillRect(drawbotP, colour, PMRect(left, top, left + side, top + side));

	// ★AND THE AUTHOR'S NAME AFTER IT (2026-10-05, the user: "after the colour, can the user name go there?") - in the
	//   ordinary text colour, the selected row's when this row is selected (the change row's text cell asks the same).
	//   The name comes through SetSegments' middle piece, written on every apply like the colour.
	PMString pre, name, post, ruby;
	int32 lines = 1, attrKind = 0;
	KCMStoryLayers layers;
	bool16 bar = kFalse;
	data->GetSegments(pre, name, post, ruby, lines, attrKind, layers, bar);
	if (name.IsEmpty())
		return;
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowSystemScriptFontId);
	const bool16 hilited = KCMViewOrParentIsHilited(this, kKCMHiliteParentSteps);
	RealAGMColor fg(0.0, 0.0, 0.0);
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
		colors->GetRealAGMColor(hilited ? kInterfaceHighLightText : kInterfaceTextColor, fg);
	const PMReal textX = frame.Left() + PMReal(kKCMTrackChipLeft) + side + PMReal(kKCMTrackChipGap);
	const PMReal room = frame.Right() - textX;
	if (room <= PMReal(0.0))
		return;
	const PMString shown = KCMEllipsize(room, name, fontInfo, kEllipsizeEnd);
	KCMDrawInBox(drawbotP, PMRect(textX, frame.Top(), frame.Right(), frame.Bottom()), shown, fontInfo, fg);
}

// End, KCMTrackChipView.cpp.
