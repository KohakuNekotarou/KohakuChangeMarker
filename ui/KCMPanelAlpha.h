//========================================================================================
//
//  KCMPanelAlpha.h
//
//  The flyout toggles "Translucent Panel" / "Translucent Pages Panel", and what they do.
//
//  WINDOWS ONLY. The alpha is put on the panel's window with Win32's
//    SetLayeredWindowAttributes. On the Mac the functions below still exist, but
//    KCMApplyPanelTranslucency does nothing.
//  IT ONLY WORKS WHILE THE PANEL IS FLOATING. A docked panel is a child window of the main frame
//    and cannot be made translucent on its own; then nothing happens and only the flag is set.
//
//  THERE ARE THREE TARGETS, each with a toggle of its own:
//     1. our own panel
//     2. the application's Pages panel
//     3. our own book comparison dialog
//    The implementation is one shared path in KCMPanelAlpha.cpp. The first two are found by
//    WidgetID -- a number, so it does not depend on the window title, which changes with the UI
//    language. The third one is found differently; see its own block below.
//
//  The whole record of what was measured is in docs/ai-notes/win32-window-transparency.md and in
//  the memory note win32-window-alpha-transparency.
//
//========================================================================================

#ifndef __KCMPanelAlpha_h__
#define __KCMPanelAlpha_h__

#include "BaseType.h"

// The toggle as it stands (off by default).
bool16	KCMGetPanelTranslucent();

// Set the toggle. THIS ONLY UPDATES THE FLAG and does not touch any window -- restoring the
// saved settings at startup happens before the panel exists, which is why setting and applying
// are separate.
void	KCMSetPanelTranslucent(bool16 on);

// Put the current flag onto the panel's window.
//  - does nothing (and is not an error) when the panel cannot be found or is docked
//  - the callers are:
//      the menu being pressed = kKCMPopupTranslucentPanelActionID in KCMActionComponent.cpp
//      the pointer arriving and leaving = KCMPanelRollOver::MouseEnter / MouseLeave in
//        KCMPanelAlpha.cpp (both of which return without calling when the toggle is off)
//  - returns kTrue when the alpha really reached a window; kFalse when there is no panel, it is
//    docked, or this is the Mac. (The menu uses that to word its status line as either "it took
//    effect" or "it is docked, so it does nothing".)
bool16	KCMApplyPanelTranslucency();

//----------------------------------------------------------------------------------------
// For the application's Pages panel. Same meaning and same implementation as the three above;
// only the target differs.
//  IT IS NOT OUR PANEL, so no IMouseRollOver can be put on it. Going opaque under the pointer
//    rests entirely on the Win32 hook (OBJID_CURSOR) for this one.
//----------------------------------------------------------------------------------------
bool16	KCMGetPagesPanelTranslucent();
void	KCMSetPagesPanelTranslucent(bool16 on);
bool16	KCMApplyPagesPanelTranslucency();

//----------------------------------------------------------------------------------------
// For the book comparison dialog (added at the user's request that the dialog be translucent
// too).
//
//  ONLY THE WAY THE WINDOW IS FOUND DIFFERS from the two above. Those ask the application's panel
//    manager by WidgetID; a dialog is not a panel, so there is no such route -- instead THE DIALOG
//    HANDS ITS WINDOW OVER (KCMBookDialog.cpp calls KCMSetBookDialogWindow as soon as it has one).
//  The other difference: a dialog IS ITSELF A TOP-LEVEL WINDOW, so none of the panel's "which dock
//    is it in right now" resolution is needed -- and neither is the restriction that it does
//    nothing while docked.
//  @warning THE WINDOW ONLY EXISTS WHILE THE DIALOG IS OPEN. Turning the toggle on while it is
//    closed does nothing at the time; it takes effect the next time the dialog opens (which
//    applies it, every time it opens).
//  That unconditional apply on every open IS correct -- not because a cached dialog would hand
//    back the window with its old alpha, but because THE WINDOW IS NEW EVERY TIME AND ALWAYS
//    STARTS OPAQUE (measured: three opens gave three different HWNDs), so when the toggle is on
//    the alpha has to be written again each time.
//  On top of that, A WINDOW THAT HAS NEVER BEEN MADE TRANSLUCENT IS LEFT ALONE: when it is not
//    layered and the wanted alpha is 255, nothing is done.
//----------------------------------------------------------------------------------------
bool16	KCMGetBookDialogTranslucent();
void	KCMSetBookDialogTranslucent(bool16 on);
bool16	KCMApplyBookDialogTranslucency();

// Hand the book comparison dialog's window over. The only caller is KCMBookDialog.cpp, on every
// open.
//  There is no "forget" call, and none is needed. It is not that the window dies (the handle value
//  gets handed out to other windows), but that THE WINDOW'S TITLE IS RECORDED ALONGSIDE IT and
//  checked against every later use -- a recycled handle is dropped before anything is written to
//  it. The calling side says the same thing ("there is no matching 'forget' call to write" in
//  KCMBookDialog.cpp). Passing nil is still safe.
// The HWND is taken as a void* to keep it out of this header: KCMPanelAlpha.h includes nothing but
// BaseType.h, and pulling windows.h in here would reach every other .cpp. The cast is done in the
// implementation.
void	KCMSetBookDialogWindow(void* sysWindow);

// (A fourth target, the toolbox, was added and then taken out again the same day, at the user's
//  decision: adding one target was all it took to make it work, but it changes the look of the
//  application's own UI, and that does not belong in KCM. The measurements are kept in the memory
//  note translucent-toolbox-idea.)

// Re-apply to every target. For places -- the panel's visibility changing, and the like -- where
// the caller should not have to know which targets there are (adding one then costs the callers
// nothing).
void	KCMApplyAllPanelTranslucency();

// Start listening for the panel's visibility changing: opened, closed, docked, undocked.
// There are two callers and it is safe to call any number of times: KCMUIStartup::Startup, and the
// panel's AutoAttach (KCMPanelObserver.cpp). THE SECOND ONE IS NOT BELT-AND-BRACES -- the panel
// manager comes up part-way through the application's own startup sequence, so at Startup it can
// still be nil, and that subscription is picked up on the AutoAttach pass instead. Every
// subscription asks IsAttached first, so calling twice does not attach twice.
// How it works: the kPaletteVisibilityChangedMessage that goes to the IID_IPANELMGR subject of
// kPanelManagerBoss (identified with the Debug build's Spy; it goes out on every docking change).
// A second subject, kAppBoss / IID_IAPPLICATION, is subscribed to as well -- see the comment on
// the function itself.
void	KCMAttachPanelVisibilityObserver();

// Undo every subscription the above made. Called at plug-in shutdown (KCMUIStartup::Shutdown),
// BEFORE KCMShutdownPanelAlpha: stop the notifications first, then fold up the timer and the hook.
// WHY IT IS NEEDED: while the subscription stands, what the session holds is a pointer INTO THIS
// .pln, and destroying a panel during shutdown really does send the notification -- so Update
// would run in code that is on its way out. KBS added the same pair for the same reason; this is
// that fix, coming the other way.
void	KCMDetachPanelVisibilityObserver();

// (There is deliberately no "forget the hover state" call. Whether the pointer is over the panel
//  is not held in a flag: it is measured through Win32 every time KCMApplyPanelTranslucency runs,
//  so there is no state that can be left behind, and the flag cannot get stuck on.)

// Tidy up the one-shot timer used for the delayed re-apply. Called at plug-in shutdown
// (KCMUIStartup::Shutdown). ICallbackTimer's callback is a raw function pointer that is NOT
// reference-counted, so leaving a booking outstanding while this .pln goes down is a crash.
// Defined in KCMPanelAlpha.cpp (empty on the Mac).
void	KCMShutdownPanelAlpha();

#endif // __KCMPanelAlpha_h__
