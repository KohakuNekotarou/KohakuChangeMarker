//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) UI - the colour square at the left of a Track mode AUTHOR row (2026-10-05).
//  ★WHY BY HAND: a stock static text draws one string in the theme's colour (KCMStoryBangView.cpp says the same).
//   The colour comes through IKCMStoryCellData::SetTrackLook, written on every apply (the recycling rule).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IControlView.h"
#include "IGraphicsPort.h"
#include "IInterfaceColors.h"		// RealAGMColor

#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "DVControlView.h"

#include "IKCMStoryCellData.h"
#include "KCMUIID.h"

class KCMTrackChipView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMTrackChipView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMTrackChipView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KCMTrackChipView, kKCMTrackChipViewImpl)

void KCMTrackChipView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	InterfacePtr<IKCMStoryCellData> data(this, UseDefaultIID());
	if (gPort == nil || data == nil)
		return;
	bool16 on = kFalse;
	RealAGMColor colour(0.0, 0.0, 0.0);
	PMString unused;
	data->GetTrackLook(on, colour, unused);
	if (!on)
		return;		// a recycled widget waiting for its next apply draws nothing

	AutoGSave gSave(gPort);
	const PMRect frame = this->GetInnerContentFrame();
	const PMReal side = (frame.Height() > PMReal(6.0)) ? frame.Height() - PMReal(6.0) : frame.Height();
	const PMReal top = frame.Top() + (frame.Height() - side) / PMReal(2.0);
	gPort->setrgbcolor(colour.red, colour.green, colour.blue);
	gPort->rectfill(frame.Left() + PMReal(2.0), top, side, side);
}

// End, KCMTrackChipView.cpp.
