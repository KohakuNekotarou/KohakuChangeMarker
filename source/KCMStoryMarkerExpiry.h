//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  One-shot timer that takes the jump marker back off the screen shortly after it appears.
//  Driven by KCMStoryMarker: ShowFlash arms it, ClearFlash disarms it. Nothing else should
//  call it -- the standing marks have no clock at all.
//
//  **THIS PLUG-IN'S ONLY IDLE TASK BESIDES THE THUMBNAIL ONE**, and the justified exception to
//  "avoid timers" ([[avoid-timers-and-idle-tasks]]): a mark has to expire on WALL-CLOCK time,
//  which nothing else here needs.
//
//  @warning **WHY NOT ICallbackTimer**, which this plug-in uses elsewhere. Its callback is a
//   plain function pointer that nothing reference-counts -- its own header opens with six
//   "Danger!"s saying the supplying plug-in must not be unloaded while that pointer is in the
//   timer. A CIdleTask is an interface on a boss: it can be Released at shutdown and takes part
//   in teardown like everything else. Ported from KBS's KBSMarkerExpiryIdleTask, which reached
//   the same conclusion the same way (and which in turn took it from KESCL).
//
//========================================================================================

#ifndef __KCMStoryMarkerExpiry_h__
#define __KCMStoryMarkerExpiry_h__

/** Jump-marker expiry timer. Go through KCMStoryMarker::ShowFlash / ClearFlash rather than
    calling these directly, so that the mark and the countdown cannot disagree. */
namespace KCMStoryMarkerExpiry
{
	/** (Re)start the countdown to clearing the mark. Called every time a mark is shown, so an
	    already-running countdown starts over: each jump shows its mark for the full time. */
	void Start();

	/** Cancel the countdown. Safe to call when it is not running. */
	void Stop();

	/** Release the task for good (application shutdown). After this, Start() is a no-op. */
	void Shutdown();
}

#endif // __KCMStoryMarkerExpiry_h__

// End, KCMStoryMarkerExpiry.h.
