//========================================================================================
//
//  KESCMTrackerRegister.cpp
//
//  Registers the KESCM tool's tracker with the application. InstallTracker binds
//  (layout widget, KESCM tool) -> KESCM tracker, so while the KESCM tool is active a left-button
//  press on the layout invokes KESCMTracker (KESCMTracker.cpp).
//
//  ITrackerRegister. The boss also advertises IID_IK2SERVICEPROVIDER via the SDK's
//  kCTrackerRegisterProviderImpl so the application discovers this registrar at startup
//  (same pattern as the snapshot / dynamicdocumentsui samples).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ITrackerRegister.h"
#include "ITrackerFactory.h"

#include "CPMUnknown.h"
#include "LayoutUIID.h"		// kLayoutWidgetBoss

#include "KCMUIID.h"

/** Installs KESCM's tracker so it runs for the KESCM tool on the layout widget. */
class KESCMTrackerRegister : public CPMUnknown<ITrackerRegister>
{
public:
	KESCMTrackerRegister(IPMUnknown* boss) : CPMUnknown<ITrackerRegister>(boss) {}

	/** Register the tracker(s) with the application tracker factory. */
	virtual void Register(ITrackerFactory* factory);
};

CREATE_PMINTERFACE(KESCMTrackerRegister, kKESCMTrackerRegisterImpl)

void KESCMTrackerRegister::Register(ITrackerFactory* factory)
{
	factory->InstallTracker(kLayoutWidgetBoss, kKESCMToolBoss, kKESCMTrackerBoss);
}

// End, KESCMTrackerRegister.cpp.
