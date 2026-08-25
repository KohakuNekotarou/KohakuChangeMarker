//========================================================================================
//
//  KCMTrackerRegister.cpp
//
//  Registers the KCM tool's tracker with the application. InstallTracker binds
//  (layout widget, KCM tool) -> KCM tracker, so while the KCM tool is active a left-button
//  press on the layout invokes KCMTracker (KCMTracker.cpp).
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

/** Installs KCM's tracker so it runs for the KCM tool on the layout widget. */
class KCMTrackerRegister : public CPMUnknown<ITrackerRegister>
{
public:
	KCMTrackerRegister(IPMUnknown* boss) : CPMUnknown<ITrackerRegister>(boss) {}

	/** Register the tracker(s) with the application tracker factory. */
	virtual void Register(ITrackerFactory* factory);
};

CREATE_PMINTERFACE(KCMTrackerRegister, kKCMTrackerRegisterImpl)

void KCMTrackerRegister::Register(ITrackerFactory* factory)
{
	factory->InstallTracker(kLayoutWidgetBoss, kKCMToolBoss, kKCMTrackerBoss);
}

// End, KCMTrackerRegister.cpp.
