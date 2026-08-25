//========================================================================================
//
//  KCMTracker.cpp
//
//  A capturing tracker for the KCM tool. While the tool is active, a LEFT-button press
//  captures the mouse and reveals the KCM comparison marks for as long as the button is held;
//  releasing hides them again. Modifier keys held at press time pick the variant:
//    - no modifier  = the marks of that window are INVERTED for as long as the button is held
//                     (they appear if they were hidden, and hide if they were shown)
//    - Shift        = peek: the old version laid over the new at 100%
//    - Shift+Alt    = the same peek at 50%
//    - Alt          = sample the raw CMYK under the click (and draw it on the cursor)
//  All variants fire immediately on press: there is no hold-to-arm delay.
//
//  Pattern copied from open/components/dynamicdocumentsui AnimationUIButtonTriggerTracker
//  (the SDK's real capturing tracker): override BeginTracking/EndTracking but ALWAYS call the
//  base CTracker::BeginTracking/EndTracking, return the base's result, and NEVER touch
//  DisableUpdates/EnableUpdates. A companion CTrackerEventHandler (IID_IEVENTHANDLER on the same
//  boss) forwards the button-up during capture to EndTracking.
//  ⚠ONE DELIBERATE DEPARTURE FROM THAT PATTERN, and it is the last clause: this tracker DOES
//    override DisableUpdates/EnableUpdates - as no-ops. The reason is written at those two methods
//    below (the base's suppression is exactly what silences KCM's InvalidateViews-based reveal).
//
//  ★The press-time HUD - the one line saying "Target" / "Source" / "Not in comparison" /
//    "Not comparing" in the **top-left** of the pressed layout view - is **in service**. This file
//    calls KCMTrackerHudBegin / End; what draws it is the draw-event route (KCMTrackerHud.cpp /
//    KCMTrackerHud.h).
//    ⚠What was removed was the **old sprite version** (a sprite drawing layer, its own font
//      selection, a one-shot timer). It was rebuilt on the draw event so that it rides the very
//      path the frames ride and therefore **appears together with them** (the old one lagged behind
//      the press).
//    ⚠This header said "removed" for ten days while the very same file went on calling the HUD.
//      The account is in docs/ai-notes/kescm-tracker-hud.md; what it must do is at the top of
//      KCMTrackerHud.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTracker.h"
#include "CTrackerEventHandler.h"
#include "IEvent.h"
#include "AutoBusyCursor.h"	// keeps the command layer's busy cursor out of the pre-press sampling

#include "CursorSpec.h"		// CursorSpec / GetPlugIn() (the custom cursor of Alt + left, CMYK)
#include "ISession.h"		// GetExecutionContextSession, on the way to ICursorMgr
#include "IApplication.h"	// QueryApplication, on the way to ICursorMgr
#include "ICursorMgr.h"		// Hide/Show, to cover the one frame it takes to install a cursor
							// (ClearCache is no longer called - see InstallCmykCursor)

#include "KCMUIID.h"
#include "KCMConstants.h"	// kKCMCursorSettleMillis (the settle wait after the install)
#include "KCMPeekGesture.h"	// KCMClassifyGesture / KCMTrackerRevealBegin / KCMTrackerRevealEnd
#include "KCMCmykCursor.h"	// the CMYK cursor entry points (HasPending / CursorProc / UpdateCmykDrag)
#include "KCMTrackerHud.h"	// says Target/Source top-left while the button is held (drawn on the draw event)

#include <chrono>			// milliseconds, for that settle wait
#include <thread>			// std::this_thread::sleep_for, likewise (the same on Windows and Mac)

/** Guarantees the ICursorMgr::Hide() -> Show() pair by scope.
	The install of a cursor takes one frame in which rubbish can be seen, so it is bracketed by
	Hide/Show (how that was pinned down is at kKCMCursorSettleMillis in KCMConstants.h).
	★Miss the Show and the cursor stays invisible for good, so it is left to the scope rather than
	written out.
	With mgr == nil it does nothing - nothing needs covering (a gesture other than CMYK, or no value
	sampled). */
struct KCMCursorHideGuard
{
	explicit KCMCursorHideGuard(ICursorMgr* mgr) : fMgr(mgr)
	{
		if (fMgr != nil)
			fMgr->Hide();
	}
	~KCMCursorHideGuard()
	{
		if (fMgr == nil)
			return;
		// The settle wait after the install. 0 = no wait, which is what it is set to; it stays here as
		// the knob to turn if the rubbish frames come back.
		if (kKCMCursorSettleMillis > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(kKCMCursorSettleMillis));
		fMgr->Show();
	}

private:
	ICursorMgr* fMgr;	// borrowed - the caller's InterfacePtr holds it for the whole scope. Not AddRef'd
};

//____________________________________________________________________________________
//	Tracker event handler: forwards events (notably the button-up) to the tracker while
//	capturing. A bare subclass of CTrackerEventHandler is enough - the base already forwards
//	LButtonUp -> ITracker::EndTracking, MouseDrag -> ContinueTracking, etc.
//____________________________________________________________________________________
class KCMTrackerEH : public CTrackerEventHandler
{
public:
	KCMTrackerEH(IPMUnknown* boss) : CTrackerEventHandler(boss) {}
	virtual ~KCMTrackerEH() {}
};

CREATE_PMINTERFACE(KCMTrackerEH, kKCMTrackerEHImpl)

//____________________________________________________________________________________
//	The KCM tool's tracker. Reveals the marks while the left button is held.
//____________________________________________________________________________________
class KCMTracker : public CTracker
{
public:
	KCMTracker(IPMUnknown* boss) : CTracker(boss), fCmykCursorFlip(kFalse)
	{
		fWantsToAutoScroll = kFalse;		// no autoscroll while holding (same as the animation sample)
	}
	virtual ~KCMTracker() {}

	/** Do NOT suppress document-view updates while tracking. CTracker::BeginTracking calls
		DisableUpdates()->DisableUpdateAllDocumentViews(), which is exactly what silences KCM's
		InvalidateViews-based mark reveal. By no-op'ing BOTH DisableUpdates and EnableUpdates the
		global suppression counter is left untouched, so views stay live during the hold and
		InvalidateViews works (the marks can show). */
	virtual void DisableUpdates() {}
	virtual void EnableUpdates()  {}

	/** Kill the continuous tracking timers. CTracker::WantTimer returns kTrue for kMouseTrackerBoss,
		which drives HandleContinueTracking/ContinueTracking on a repeating idle even when the mouse
		is steady. With live views that would re-run the heavy KCM mark compositing every tick and
		freeze the UI. We only need a static reveal, so refuse every timer. Mouse-up still ends
		tracking via the CTrackerEventHandler, not the timer, so this is safe.
		The blanket kFalse is not a shortcut: CTracker::BeginTracking asks for three timers
		(kPatientUserBoss / kMouseTrackerBoss / kDynamicPauseTimerBoss - the three WantTimer() calls in
		CTracker::BeginTracking) but the base's own CTracker::WantTimer only ever answers kTrue for
		kMouseTrackerBoss, so refusing all three behaves exactly like refusing that one - the other two
		were already off. (Both were quoted by line number until the 2026-08-19 bug recheck; the second
		one was off by a couple of lines, so they are named instead.) */
	virtual bool16 WantTimer(ClassID /*trackerTimerBoss*/) { return kFalse; }

	/** Mouse down. Engage on a left-button press only (middle/right keep their normal handling, e.g.
		the context menu). Call the base to do the real tracking setup, then reveal immediately by the
		modifier keys held at press time: nothing = invert this window's marks, Shift = peek 100%,
		Shift+Alt = peek 50%, Alt = CMYK. Every one of them fires on the press, with no hold delay.
		Return the base's result so the tracking lifecycle stays intact. */
	virtual bool16 BeginTracking(IEvent* theEvent)
	{
		if (theEvent == nil)
			return kFalse;
		// ★Only a LEFT press engages; middle and right are left to their usual handling (the context
		//   menu and so on).
		//   ⚠It is deliberately not restricted to kLButtonDn: press, release, press again inside the
		//   system double-click time and the second one arrives as **kDoubleClick**, which IEvent
		//   documents as "double click on **any** mouse button". LButtonDn() - "the left button was
		//   pressed when this event was generated" - is what narrows it back to the left.
		//   The product tracker this file was copied from (AnimationUIButtonTriggerTracker) has no event
		//   type filter at all.
		const IEvent::EventType evType = theEvent->GetType();
		const bool16 leftPress =
			(evType == IEvent::kLButtonDn) ||
			(evType == IEvent::kDoubleClick && theEvent->LButtonDn());
		if (!leftPress)
			return kFalse;

		// Classifying the gesture happens in KCMClassifyGesture and nowhere else - **no independent
		// modifier-key test is written anywhere** (KCMPeekGesture.h).
		// ★MacCtrlDown() is passed as well: Control+click on macOS is the system's standard secondary
		//   button gesture, so KCMClassifyGesture answers "unassigned" for it. On Windows it is always
		//   kFalse.
		const bool16 shiftDown = theEvent->ShiftKeyDown();
		const bool16 altDown   = theEvent->OptionAltKeyDown();
		const bool16 cmdDown   = theEvent->CmdKeyDown();
		const bool16 macCtrl   = theEvent->MacCtrlDown();
		const bool16 cmykGesture =
			(KCMClassifyGesture(shiftDown, altDown, cmdDown, macCtrl) == kKCMGestureCmyk);

		// ★The cure for the "one rubbish frame" on press. The point of it: get the expensive work OUT of
		//   the hidden stretch, and wait after the install.
		//   On an Alt-alone press (the colour compare) the cursor changes several times over: (1) the base
		//   takes its modal cursor (the checkmark is set again) -> (2) the expensive CMYK sampling (the
		//   page correspondence table is built and two tiny rasterizations are run) -> (3) the CMYK
		//   information cursor is installed. A hardware cursor is composited by the OS independently of
		//   what the application is doing, so whatever is in the buffer for the instant before the
		//   finished picture is installed reaches the screen ---- intermittent rubbish (reported as: a
		//   flash before the numbers appear, not on every press).
		//   ★Measured: moving (2) out of the hidden stretch so that (1) -> (3) took about a millisecond,
		//     WITHOUT Hide/Show, did not get rid of it. ＝ The cause is not "switching takes time" but
		//     **the one frame the install itself owns**, and there is no means but covering it. Covering
		//     (2) as well, the way it used to, spends nearly the whole hidden stretch on that computation
		//     and the cursor visibly disappears (also reported).
		//   -> As it stands: (2) is finished before anything is hidden (computed with the checkmark cursor
		//     still up), and what is hidden is only (1) + (3) + the settle wait. The wait is
		//     kKCMCursorSettleMillis (KCMConstants.h); raise it if rubbish is ever seen again.
		//   ★AutoBusyCursor(kFalse) keeps the command layer's automatic busy cursor from cutting in
		//     during the sampling ---- the same thing the base does in InitializeModalCursor, brought
		//     forward. Leaving the scope puts it back.
		if (cmykGesture)
		{
			AutoBusyCursor noBusyCursorWhileSampling(kFalse);
			KCMTrackerRevealBegin(shiftDown, altDown, cmdDown, macCtrl);
		}

		// session can be nil during shutdown, so only the direct QueryApplication call is guarded
		// (InterfacePtr(p, iid) itself permits p == nil). The whole plug-in is written this way.
		// ★Hiding happens only where a CMYK cursor is really going up ＝ where a value was sampled
		//   (Pending). Hide without one and the cursor merely blinks with no CMYK cursor to show for it
		//   (learnt the hard way; the single test is Pending).
		ISession* session = GetExecutionContextSession();
		InterfacePtr<IApplication> theApp(session != nil ? session->QueryApplication() : nil);
		InterfacePtr<ICursorMgr> cursorMgr(theApp, UseDefaultIID());
		const bool16 hideDuringSwitch =
			(cmykGesture && cursorMgr != nil && KCMTrackerHasPendingCmykCursor());

		bool16 result = kFalse;
		{
			// ★The Hide/Show pair is left to KCMCursorHideGuard. The Show runs at the end of this block,
			//   which is where the explicit one used to be, so nothing behaves differently ---- and an
			//   early return added here later can no longer leave the cursor invisible.
			KCMCursorHideGuard cursorHide(hideDuringSwitch ? (ICursorMgr*)cursorMgr : nil);

			result = CTracker::BeginTracking(theEvent);
			if (result)
			{
				// ★Raise the flag that says "show the press-time HUD". ★It must be raised **before**
				//   KCMTrackerRevealBegin: Begin asks for a repaint itself (KCMTrackerHudInvalidate), so
				//   with the flag already up that single repaint carries the frames AND the HUD ＝ **they
				//   appear together** (which is what the old sprite version, lagging behind the frames,
				//   did not do).
				//   ⚠This once said the HUD "rides on the InvalidateViews that reveal calls". That only
				//     runs over the **Target** window (KCMTrackerRevealBegin in KCMPeekGesture.cpp returns
				//     early when `KCMMouseIsOverTarget()` is false), so over the Source window, while
				//     stopped, and over a third document the HUD was never drawn at all. It asks for its
				//     own repaint now (the full reason is at the Invalidate in KCMTrackerHud.cpp).
				//   What draws it is **KCMUIDrawEvent.cpp on the UI side**, on two routes (in front of the
				//   band, and behind on the canvas).
				KCMTrackerHudBegin(fControlView);

				// Everything but CMYK (the reveal and the peeks) fires after the base, as it always has.
				// CMYK was dealt with above.
				if (!cmykGesture)
					KCMTrackerRevealBegin(shiftDown, altDown, cmdDown, macCtrl);

				// Where Alt + left (the colour compare) sampled a value, the CMYK is drawn on the cursor
				// itself: the modal cursor CTracker set up in BeginTracking is replaced with a custom
				// bitmap cursor (CTracker puts the original back when tracking ends). A kTrue - dynamic -
				// spec is NOT used, because the uninitialized buffer shows for an instant as it is set
				// (see InstallCmykCursor). Updating the numbers during a drag is ContinueTracking's job:
				// it re-installs through InstallCmykCursor when the value changes.
				if (KCMTrackerHasPendingCmykCursor())
					this->InstallCmykCursor();
			}
		}	// <- cursorHide's destructor Shows here (if it hid anything)

		if (!result && cmykGesture)
		{
			// The base refused to track, so **EndTracking never comes**. What the sampling run above is
			// holding - the press-time font, the page correspondence cache, the document of a lone pick,
			// the status line - has to be returned here.
			KCMTrackerRevealEnd();
		}
		return result;
	}

	/** Mouse drag. CTrackerEventHandler forwards MouseDrag here. With WantTimer = kFalse nothing
		timer-driven calls it, so it arrives only when the mouse actually moved. During Alt + left (the
		colour compare) the CMYK under the current position is sampled again (throttled) and the cursor
		is redrawn where the value changed ＝ the numbers can be picked up by dragging (asked for by the
		user). For any other gesture (reveal / peek) it does nothing beyond the base. */
	virtual void ContinueTracking(const PBPMPoint& where, bool16 mouseDidMove)
	{
		CTracker::ContinueTracking(where, mouseDidMove);
		// During Alt + left: sample the CMYK at the current position again (KCMTrackerUpdateCmykDrag
		// throttles that to one in 50ms) and re-install the kFalse cursor to redraw **only** where the
		// value changed (＝ where it answers kTrue).
		// A dynamic cursor (kTrue) is not used: the uninitialized buffer shows for an instant as it is
		// set, which was the real cause of the rubbish on the first press. Re-installing a kFalse one
		// draws in the callback and shows the finished buffer, so a drag update cannot show rubbish
		// either.
		if (mouseDidMove && KCMTrackerHasPendingCmykCursor() && KCMTrackerUpdateCmykDrag())
			this->InstallCmykCursor();
	}

	/** Mouse up. Call the base first, then hide the marks. */
	virtual bool16 EndTracking(IEvent* theEvent)
	{
		bool16 result = CTracker::EndTracking(theEvent);
		// ★Lower the HUD's flag **before** KCMTrackerRevealEnd: End asks for a repaint itself, so with
		//   the flag already down that one repaint clears the HUD along with the frames. The order
		//   mirrors BeginTracking's.
		KCMTrackerHudEnd();
		KCMTrackerRevealEnd();
		return result;
	}

	/** Put the reveal back where tracking was aborted (a menu was chosen, say). The same clean-up
		EndTracking does ＝ an interruption during the hold cannot leave the frames up. */
	virtual void AbortTracking(IEvent* theEvent)
	{
		CTracker::AbortTracking(theEvent);
		KCMTrackerHudEnd();			// as in EndTracking: an abort must not leave the HUD behind either
		KCMTrackerRevealEnd();
	}

private:
	bool16 fCmykCursorFlip;		// which way the CMYK cursor's CursorID alternation currently stands
								// (kFalse = 1021 next, kTrue = 1022 next)

	/** Install the CMYK information cursor with a kFalse (synchronously drawn) spec. Both the first
		one (BeginTracking) and the re-install when the value changes mid-drag (ContinueTracking) come
		here. Why no rubbish is seen, and the two guards that make the re-install actually take:
		- kFalse = the cursor manager runs the callback synchronously and builds the cursor from the
		  finished buffer **before** showing it (proven rubbish-free by the checkmark cursor). kTrue -
		  dynamic - shows first and calls back afterwards, so the uninitialized buffer is briefly
		  visible; it is not used.
		- Alternating the CursorID (1021 <-> 1022) = the spec is guaranteed to differ from the one before
		  it, so a redraw happens even where re-installing the same spec would be treated as a no-op.
		  HOTC is (10,18) for both IDs, so the cursor does not shift. It is called mid-drag only when
		  the value changed, and behind a 50ms throttle (about twenty times a second at most).
		★ICursorMgr::ClearCache() used to be called every time as well, as insurance against "the cache
		  keyed by CursorID reuses the picture of an old number". It was removed after confirming in the
		  running application that no old picture is reused without it (alternating IDs and a kFalse
		  synchronous spec are enough). The mix-up that had been measured earlier really came of the
		  checkmark and the CMYK cursor **sharing one CursorID**, which separating 1020 from 1021/1022
		  cured; ClearCache was never what was needed.
		  ※Should a machine ever show "the number from two picks ago" mid-drag, bring ClearCache back
		  here. */
	void InstallCmykCursor()
	{
		fCmykCursorFlip = !fCmykCursorFlip;
		CursorSpec spec(GetPlugIn()->GetPluginID(), IDFile(),
		                fCmykCursorFlip ? kKCMCmykCursorResID : kKCMCmykCursor2ResID,
		                KCMTrackerCmykCursorProc(), kFalse /*drawn synchronously = no rubbish*/);
		this->ChangeModalCursor(spec);
	}
};

CREATE_PMINTERFACE(KCMTracker, kKCMTrackerImpl)

// End, KCMTracker.cpp.
