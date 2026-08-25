//========================================================================================
//
//  KCMPeekGesture.h
//
//  What the mouse gesture means and what it starts: classifying the modifier combination
//  under the tool, beginning and ending the reveal, and the deferred UI cleanup that runs
//  once after a batch document close.
//
//  Split out of KCMPeek.cpp on 2026-08-13. Behaviour unchanged. UI side: it reads the
//  pressed state of the tool, which lives in the UI and is invisible to the model.
//
//  NOTE: the close handling here is the UI half only. Its model twin is
//  KCMHandleDocsClosed() in KCMPeek.cpp, which drops tracking state for documents that
//  are gone. Both listen to the same close notification for different purposes -- do not
//  merge them.
//
//========================================================================================

#ifndef __KCMPeekGesture_h__
#define __KCMPeekGesture_h__

#include "BaseType.h"

// What a modifier combination under the tool means.
// ★It belongs on the UI side although it was declared in KCMPeek.h first: the only files that
//   name it are this one, KCMPeekGesture.cpp and KCMTracker.cpp -- all three UI side. Keeping it
//   there would have the UI include a model header for nothing.
enum KCMGesture
{
	kKCMGestureNone = 0,	// anything with Ctrl (cmd), or Mac's Control ＝ unassigned, do nothing
	kKCMGestureReveal,	// no modifier: **invert the pressed window's marks while held**
	kKCMGesturePeek100,	// Shift: lay the OTHER version over the pressed window at 100%
	kKCMGesturePeek50,	// Shift+Alt (Mac: Shift+Option): the same at 50%
	kKCMGestureCmyk		// Alt alone (Mac: Option alone): sample the CMYK under the cursor
};

// Classify the modifiers. ★**The assignment is defined here and nowhere else**
// ([[one-question-one-place]]). Two places ask: KCMTracker.cpp's BeginTracking, to decide whether
// to fire CMYK before the base, and KCMTrackerRevealBegin. Everything downstream -- the temp-hide,
// the peeks, the Story press marks -- branches on the value that one call returned, so to change
// what a modifier does, change KCMClassifyGesture in KCMPeekGesture.cpp and nothing else.
// ★macCtrlDown (= IEvent::MacCtrlDown, always kFalse on Windows) falls to "unassigned": on macOS
//   Control-click is the standard secondary-button (context menu) gesture, so even if it were to
//   arrive as a left press, KCM must not take it away. Same reasoning as cmdDown (Mac's Command).
//   The parameter defaults to kFalse, so the Windows calls are unaffected.
KCMGesture	KCMClassifyGesture(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown = kFalse);

// The shared entry points for the tracker (the left button). While the KCM tool is active and the
// left button is held, the modifiers **as they stood at press time** pick what happens. Begin = the
// press (it is handed that modifier state), End = the release. Both are called from KCMTracker.cpp.
//   - no modifier  = **the pressed window's marks are inverted while held**: hidden ones come up,
//                    shown ones go away (so the bare page underneath can be checked)
//   - Shift        = lay the other version over the pressed window at 100%
//   - Shift+Alt    = the same at 50%
//   - Alt alone    = sample the raw CMYK at the click point in both versions, into the status line
//   - with Ctrl (cmd) = unassigned, nothing happens (re-comparing moved to the page context menu,
//                    and the panel operations to the flyout)
//   - Mac's Control   = unassigned (see macCtrlDown above)
// ★What the key names mean (IEvent absorbs the difference): OptionAltKeyDown = Alt on Windows /
//   Option on Mac, CmdKeyDown = Ctrl on Windows / Command on Mac. So "Alt" above is Option on Mac.
void			KCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown = kFalse);
void			KCMTrackerRevealEnd();

// Subscribe to the batch-close completion notification so the deferred UI cleanup (strip
// removal, thumbnail regeneration, panel refresh) runs once instead of once per document.
// Called at startup.
void			KCMAttachDocsClosedObserver();

// Forget that a press is showing anything. Split out on 2026-08-13 because the callers of the day
// lived on the model side and cannot see this file's statics.
//
// ⚠2026-08-19 (bug recheck B-U7): the one caller left is the UI's close handling (the
// comparison-docs-closed branch of KCMModelChangeObserver). Arm and disarm no longer call it, and
// that is not an oversight -- see the .cpp for why the three ways a press can end all funnel through
// KCMTrackerRevealEnd, so the flags cannot outlive a press.
void			KCMResetPeekGestureState();

// Is a batch close running right now? Reads the session flag the application's Links UI keeps
// (IID_IKFILESCLOSING); kFalse when that flag cannot be read, which restores the pre-2026-07-27
// behaviour of cleaning up once per document.
//
// The close sweep asks this to decide whether to clean the UI now or hand it to
// KCMDeferCloseUi below.
bool16			KCMBatchCloseInProgress();

// Hold the UI half of the close cleanup until the batch close finishes. The close sweep calls
// this when it has dropped its state but must not touch widgets yet.
void			KCMDeferCloseUi();

// Shutdown: drop the pending flag so nothing is left booked. Called from
// KCMUIStartup::Shutdown.
void			KCMPeekGestureShutdown();

#endif // __KCMPeekGesture_h__
