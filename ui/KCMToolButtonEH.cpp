//========================================================================================
//
//  KCMToolButtonEH.cpp
//
//  The event handler of the panel's tool button -- the one button that carries BOTH of this
//  plug-in's tools. A CLICK chooses the tool whose face the button is wearing; HOLDING IT DOWN
//  swaps to the other one, which is the toolbox's press-and-hold brought to the panel (the user's
//  request, 2026-09-04: "the toolbox manages it -- hold it down and you can pick either tool").
//
//  ***** WHY THIS EXISTS AT ALL, AND WHY IT REPLACES A STOCK IMPLEMENTATION *****
//
//  ⚠★★kRollOverIconButtonBoss already has an IID_IEVENTHANDLER: kAssociatedActionEventHandlerImpl
//    (measured in the boss dump, IObjectModel_RomanFS.txt). Naming this one on the boss REPLACES
//    it -- the same shape of change that once cost this plug-in its OK button, when a stock
//    IID_IOBSERVER was replaced without asking what the stock one did. So it was asked first, and
//    the answer is: what goes away is the stock press handling, and the press is handled here in
//    full instead.
//
//  ★★THE STATE MESSAGES COULD NOT DO THIS JOB, which is what sent the work here:
//      1. They cannot tell a HOLD from a CLICK. There is one message for "pressed" and one for
//         "released", and no time between them that a listener can read -- IEvent::GetTime can,
//         and only an event handler sees an IEvent.
//      2. ⚠**A button already showing selected raises no kTrueStateMessage at all.** Measured
//         2026-09-04: with the comparison tool active (so the button sat pressed-in), a second
//         press did nothing whatsoever -- no message, no switch. That is the whole reason the
//         first attempt at "press again to swap tools" looked dead.
//
//  ★What is NOT lost: the rollover artwork (IID_IMOUSEROLLOVER), the tooltip (IID_ITIP) and the
//    pressed look (IID_ITRISTATECONTROLDATA, written by KCMSyncToolButton) all live on other
//    interfaces of the boss and are untouched.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "CEventHandler.h"		// the entry-level IEventHandler (every method answers kFalse)
#include "IEvent.h"				// GetTime -- how long the button was held
#include "IControlView.h"		// GetWidgetID -- which of the two faces was pressed

// Project includes:
#include "KCMUIID.h"
#include "KCMUIShared.h"		// KCMToolButtonPressed / kKCMToolButtonHoldMs

/** The panel tool button's press-and-hold handler.

	It owns the whole press: down is remembered, up decides. Everything else the button does is on
	other interfaces of the boss and is left alone.
*/
class KCMToolButtonEH : public CEventHandler
{
public:
	KCMToolButtonEH(IPMUnknown* boss) : CEventHandler(boss) {}
	virtual ~KCMToolButtonEH() {}

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);

private:
	// When the button went down. ★A file static is right here and not a data member: the two faces
	//   of the button are two widgets, so a press that starts on one and is released on the other
	//   (they occupy the same rectangle, and a swap can happen in between) would otherwise lose its
	//   start time. UI code is main-thread only, so no lock is involved.
	// ⚠fDownValid guards the case of an up arriving with no down before it -- a press begun before
	//   the panel was built, or one the window manager swallowed.
	static PMReal	sDownTime;
	static bool16	sDownValid;
};

PMReal KCMToolButtonEH::sDownTime = PMReal(0.0);
bool16 KCMToolButtonEH::sDownValid = kFalse;

CREATE_PMINTERFACE(KCMToolButtonEH, kKCMToolButtonEHImpl)

bool16 KCMToolButtonEH::LButtonDn(IEvent* e)
{
	if (e == nil)
		return kFalse;

	sDownTime  = e->GetTime();
	sDownValid = kTrue;

	// kTrue = handled. Nothing is decided yet: what the press MEANS depends on how long it lasts,
	// and that is only known at the release.
	return kTrue;
}

bool16 KCMToolButtonEH::LButtonUp(IEvent* e)
{
	if (e == nil)
		return kFalse;

	// ⚠GetTime is documented as a DWORD of milliseconds, so it rolls over about every 47 days
	//   (IEvent.h:144). A press straddling the roll-over gives a negative difference, which is read
	//   as a short click -- the harmless way round.
	// ⚠A negative value means NO BUTTON-DOWN REACHED THIS HANDLER, which is a different fault from
	//   "the press was short" and has to be told apart from it.
	const PMReal heldRaw = sDownValid ? (e->GetTime() - sDownTime) : PMReal(-1.0);
	sDownValid = kFalse;

	InterfacePtr<IControlView> cv(this, UseDefaultIID());
	if (cv == nil)
		return kTrue;

	// ★It reports the MEASUREMENT and lets the caller decide. The threshold then lives in one place
	//   (KCMToolButtonPressed), and this class stays a thing that measures rather than a second
	//   opinion about what "held" means.
	const bool16 pawFace = (cv->GetWidgetID() == kKCMPawToolButtonWidgetID);
	KCMToolButtonPressed(pawFace, heldRaw);
	return kTrue;
}

// End, KCMToolButtonEH.cpp.
