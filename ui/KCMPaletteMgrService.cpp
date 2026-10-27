//========================================================================================
//
//  KCMPaletteMgrService.cpp
//
//  The palette manager's start and shut-down (IPaletteMgrService, kPaletteMgrService - 2026-10-27).
//
//  WHAT IT IS FOR. The panel visibility observer (KCMPanelAlpha.cpp) is attached to the panel manager's and the
//    application's subjects, and it has to come off again before this .pln does: while it is attached, those subjects
//    hold a pointer into code that is on its way out. Until today the only call that took it off was the UI half's own
//    Shutdown (KCMUIStartup) - and KFC, which has the same observer, measured that by then the session answers nil for
//    the application, so the detach found nothing to detach from: the subscriptions stood through the whole quit
//    (about 75 notifications on the way down - docs/ai-notes/kfc-block14-inspections-2026-10-20.md section 3, A14-1).
//    PaletteMgrAboutToShutdown - "called as Palette manager is shutting down just before the palettes are closed"
//    (IPaletteMgrService.h) - comes while the session, the active context and the panel manager are all still up;
//    InDesign's own Book panel hangs its start-up and shut-down off this service.
//  ⇒ The detach is called here first; the Shutdown call stays as the backstop (a second call finds nothing attached).
//    The attach side refuses while the application is going down (KCMPanelAlpha.cpp), so nothing put back after this
//    can outlive the observer.
//
//  A UI service: a provider in a UI plug-in is main-thread only by its base class's answer (CServiceProvider.h), so
//  GetThreadingPolicy is not written - as for every service of this plug-in.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CPMUnknown.h"
#include "CServiceProvider.h"
#include "IPaletteMgrService.h"
#include "WorkspaceID.h"			// kPaletteMgrService, IID_IPALETTEMGRSERVICE

#include "KCMUIID.h"
#include "KCMPanelAlpha.h"			// KCMDetachPanelVisibilityObserver
#include "KCMDiag.h"				// KCM_DIAG_LOG - which call took the observer off (test builds only)

/** Registers kKCMPaletteMgrServiceBoss for kPaletteMgrService. */
class KCMPaletteMgrServiceProvider : public CServiceProvider
{
public:
	KCMPaletteMgrServiceProvider(IPMUnknown* boss) : CServiceProvider(boss) {}
	virtual ~KCMPaletteMgrServiceProvider() {}

	virtual ServiceID GetServiceID() { return kPaletteMgrService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	// Per session: what the product's own provider of this service is registered as (KFC read it off the service
	// registry's dump - kBookPanelStartupShutdownBoss).
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// An internal name, not a key (PMString.h's form for "strings that are not keys").
	virtual void GetName(PMString* pName) { pName->SetCString("KCMPaletteMgrService", PMString::kEncodingASCII); }
};

CREATE_PMINTERFACE(KCMPaletteMgrServiceProvider, kKCMPaletteMgrServiceProviderImpl)

/** The two moments of the palette manager. Only the second is used: the observer is attached by the UI half's Startup
	and the panel's AutoAttach (KCMPanelAlpha.h says why both), which already come before the palettes are laid out. */
class KCMPaletteMgrService : public CPMUnknown<IPaletteMgrService>
{
public:
	KCMPaletteMgrService(IPMUnknown* boss) : CPMUnknown<IPaletteMgrService>(boss) {}
	virtual ~KCMPaletteMgrService() {}

	virtual void PaletteMgrStarted() {}
	virtual void PaletteMgrAboutToShutdown()
	{
		KCM_DIAG_LOG("PANELVIS the palette manager is about to close the palettes");
		KCMDetachPanelVisibilityObserver();
	}
};

CREATE_PMINTERFACE(KCMPaletteMgrService, kKCMPaletteMgrServiceImpl)

// End, KCMPaletteMgrService.cpp.
