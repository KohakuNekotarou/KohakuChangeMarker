//========================================================================================
//
//  KESCMTracker.cpp
//
//  CONFIRMATORY BUILD (2026-07-13): a "proper" capturing tracker for the KESCM tool, written
//  the way the SDK samples do it, to test two things:
//    (1) does it avoid the freeze/crash of the earlier attempt? (the earlier attempt overrode
//        DisableUpdates()/EnableUpdates() to no-ops, which no SDK sample does),
//    (2) does the reveal (KESCM marks) show while the left button is held, given the base
//        CTracker disables document-view updates during tracking?
//
//  Pattern copied from open/components/dynamicdocumentsui AnimationUIButtonTriggerTracker
//  (the SDK's real capturing tracker): override BeginTracking/EndTracking but ALWAYS call the
//  base CTracker::BeginTracking/EndTracking, return the base's result, and NEVER touch
//  DisableUpdates/EnableUpdates. A companion CTrackerEventHandler (IID_IEVENTHANDLER on the same
//  boss) forwards the button-up during capture to EndTracking.
//
//  While this tool is active, a left-button press captures and reveals the marks; releasing
//  hides them. The middle-button gestures are untouched (both input methods coexist).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTracker.h"
#include "CTrackerEventHandler.h"
#include "IEvent.h"

#include "KESCMID.h"
#include "KESCMPeek.h"		// KESCMTrackerRevealBegin / KESCMTrackerRevealEnd

//____________________________________________________________________________________
//	Tracker event handler: forwards events (notably the button-up) to the tracker while
//	capturing. A bare subclass of CTrackerEventHandler is enough - the base already forwards
//	LButtonUp -> ITracker::EndTracking, MouseDrag -> ContinueTracking, etc.
//____________________________________________________________________________________
class KESCMTrackerEH : public CTrackerEventHandler
{
public:
	KESCMTrackerEH(IPMUnknown* boss) : CTrackerEventHandler(boss) {}
	virtual ~KESCMTrackerEH() {}
};

CREATE_PMINTERFACE(KESCMTrackerEH, kKESCMTrackerEHImpl)

//____________________________________________________________________________________
//	The KESCM tool's tracker. Reveals the marks while the left button is held.
//____________________________________________________________________________________
class KESCMTracker : public CTracker
{
public:
	KESCMTracker(IPMUnknown* boss) : CTracker(boss)
	{
		fWantsToAutoScroll = kFalse;		// no autoscroll while holding (same as the animation sample)
	}
	virtual ~KESCMTracker() {}

	/** EXPERIMENT (2026-07-13): do NOT suppress document-view updates while tracking.
		CTracker::BeginTracking calls DisableUpdates()->DisableUpdateAllDocumentViews(), which is
		exactly what silences KESCM's InvalidateViews-based mark reveal. By no-op'ing BOTH
		DisableUpdates and EnableUpdates the global suppression counter is left untouched, so views
		stay live during the hold and InvalidateViews works (the marks can show). */
	virtual void DisableUpdates() {}
	virtual void EnableUpdates()  {}

	/** Kill the continuous tracking timers. CTracker::WantTimer returns kTrue for kMouseTrackerBoss,
		which drives HandleContinueTracking/ContinueTracking on a repeating idle even when the mouse
		is steady. With live views that would re-run the heavy KESCM mark compositing every tick and
		freeze the UI. We only need a static reveal, so refuse every timer. Mouse-up still ends
		tracking via the CTrackerEventHandler, not the timer, so this is safe. */
	virtual bool16 WantTimer(ClassID /*trackerTimerBoss*/) { return kFalse; }

	/** Mouse down. Engage only on a plain left-button press (leave right/middle to their normal
		handling, e.g. the context menu). Call the base to do the real tracking setup, then reveal
		the marks. Return the base's result so the tracking lifecycle stays intact. */
	virtual bool16 BeginTracking(IEvent* theEvent)
	{
		if (theEvent == nil || theEvent->GetType() != IEvent::kLButtonDn)
			return kFalse;

		bool16 result = CTracker::BeginTracking(theEvent);
		if (result)
			KESCMTrackerRevealBegin();
		return result;
	}

	/** Mouse up. Call the base first, then hide the marks. */
	virtual bool16 EndTracking(IEvent* theEvent)
	{
		bool16 result = CTracker::EndTracking(theEvent);
		KESCMTrackerRevealEnd();
		return result;
	}
};

CREATE_PMINTERFACE(KESCMTracker, kKESCMTrackerImpl)

// End, KESCMTracker.cpp.
