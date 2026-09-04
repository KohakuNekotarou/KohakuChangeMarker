//========================================================================================
//
//  KCMToolButtonEH.cpp
//
//  The event handler of the panel's tool button -- the one button that carries BOTH of this
//  plug-in's tools. A CLICK chooses the tool whose face the button is wearing; HOLDING IT DOWN
//  raises a small menu of both tools, which is the toolbox's press-and-hold brought to the panel
//  (the user's request, 2026-09-04: "the toolbox manages it -- hold it down and you can pick
//  either tool").
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
//         "released", and no time between them that a listener can read.
//      2. ⚠**A button already showing selected raises no kTrueStateMessage at all.** Measured
//         2026-09-04: with the comparison tool active (so the button sat pressed-in), a second
//         press did nothing whatsoever -- no message, no switch.
//
//  ★★★AND THE MENU HAS TO GO UP WHILE THE BUTTON IS STILL DOWN. Raising it from the button-UP
//    was tried first and **nothing appeared** (measured: zero windows of class #32768 after the
//    press) -- a popup menu wants the mouse, and by then the mouse is gone. So a one-shot timer is
//    started on the way down and the menu is raised from its callback, with the button still held.
//    ⚠That is the one case [[avoid-timers-and-idle-tasks]] admits: a delay that is structurally
//      necessary, for which ICallbackTimer is the sanctioned tool.
//
//  ★What is NOT lost by replacing the stock handler: the rollover artwork (IID_IMOUSEROLLOVER),
//    the tooltip (IID_ITIP) and the pressed look (IID_ITRISTATECONTROLDATA, written by
//    KCMSyncToolButton) all live on other interfaces of the boss and are untouched.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "CEventHandler.h"		// the entry-level IEventHandler (every method answers kFalse)
#include "IEvent.h"				// GlobalWhere -- where the menu goes up
#include "IControlView.h"		// GetWidgetID -- which of the two faces was pressed
#include "ICallbackTimer.h"		// the one-shot delay that lets the flyout appear mid-press
#include "IIdleTask.h"			// kEndOfTime -- what a one-shot callback MUST return

// General includes:
#include "CreateObject.h"		// ::CreateObject -- the timer is made, not queried
#include "PMString.h"
#include "ShuksanID.h"			// kCallbackTimerBoss / IID_ICALLBACKTIMER
#include "WideString.h"			// the UTF-16 route from PMString to AppendMenuW
#ifdef WINDOWS
#include <windows.h>			// CreatePopupMenu / TrackPopupMenu -- the press-and-hold feel
#endif

// Project includes:
#include "KCMUIID.h"
#include "KCMUIShared.h"		// KCMToolButtonPressed / kKCMToolButtonHoldMs

//========================================================================================
// The press in progress.
//
//  ★File statics rather than data members: the button's two faces are TWO WIDGETS sharing one
//    rectangle, so a press that begins on one and ends on the other must still be one press. UI
//    code is main-thread only, so nothing here is locked.
//  ⚠sTimer is an owned reference (::CreateObject hands one over) and MUST be released -- a timer
//    left holding a raw function pointer into a plug-in that then unloads is a crash
//    (ICallbackTimer.h says so, and [[plugin-teardown-robustness]] repeats it). Every exit from a
//    press goes through KCMStopFlyoutTimer.
//========================================================================================
static ICallbackTimer*	sTimer = nil;
static SysPoint			sDownWhere;			// where the press began, in global coordinates
static bool16			sFlyoutShown = kFalse;	// did the timer fire and put the menu up?

static void KCMStopFlyoutTimer()
{
	if (sTimer != nil)
	{
		sTimer->StopTimer();
		sTimer->Release();
		sTimer = nil;
	}
}

//========================================================================================
// The flyout itself.
//
//  ★★★IT IS A WIN32 POPUP, NOT IMenuManager's (2026-09-04, the user's request: "with a menu you
//    have to click again -- I want what is selected to run when the mouse is RELEASED"). That is
//    the toolbox's feel, and it comes from ONE FLAG: TPM_LEFTBUTTON, raised while the button is
//    still down, makes the popup track the drag and choose whatever the pointer is over at the
//    release.
//  ⚠IMenuManager::HandlePopupMenu cannot be asked for it. Its arguments are a label, two points,
//    a flag about disabled items and a widget (IMenuManager.h:88-96) -- nothing about how the
//    press is tracked. It was tried first, and it needed a second click.
//  ⚠WINDOWS ONLY, and that is a decision rather than an oversight: KCM is a Windows product
//    (memory: the Mac build is off the table), and the panel already reaches for Win32 where the
//    SDK has no door -- the translucent panel does the same.
//========================================================================================
#ifdef WINDOWS
// One of this plug-in's own Win32 bitmaps, for a menu item's picture.
//  ★★THE MODULE IS FOUND FROM AN ADDRESS INSIDE IT, not from a file name. GetModuleHandleW(L"...")
//    would need the .pln's name spelled here, and that name is set by the vcxproj's TargetName --
//    a rename would leave this compiling, loading and silently showing no icons. An address
//    cannot go stale.
//  ⚠UNCHANGED_REFCOUNT: this must not pin the plug-in in memory.
static HBITMAP KCMLoadMenuBitmap(int32 rsrcID)
{
	HMODULE self = nil;
	if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                          reinterpret_cast<LPCWSTR>(&KCMLoadMenuBitmap), &self) || self == nil)
		return nil;

	// LR_CREATEDIBSECTION keeps the 32 bits as they are -- the alpha included, which is what
	// hbmpItem reads. Without it the bitmap is converted to the screen's format and the
	// transparency is lost.
	return (HBITMAP)::LoadImageW(self, MAKEINTRESOURCEW(rsrcID), IMAGE_BITMAP, 0, 0,
	                             LR_CREATEDIBSECTION);
}
#endif

static void KCMRaiseToolFlyout()
{
#ifdef WINDOWS
	HMENU menu = ::CreatePopupMenu();
	if (menu == nil)
		return;

	// ★The names are the tool names themselves, from the very string keys ITool::Init passes to
	//   SetName -- so this menu, the tooltip and the toolbox cannot disagree
	//   ([[one-question-one-place]]).
	PMString n1(kKCMToolStringKey);		n1.Translate();
	PMString n2(kKCMPawToolStringKey);	n2.Translate();
	WideString w1(n1);
	WideString w2(n2);

	// ★A tick marks the tool that is current, which is what the toolbox's own flyout shows.
	const bool16 pawNow = KCMIsPawToolActive();
	// ⚠The cast is sound HERE and only here: wchar_t is 16 bits on Windows, and this whole
	//   function is inside #ifdef WINDOWS. (On the Mac it is 32 and the same cast would read past
	//   the buffer -- the mistake KCMChangedPagesTSV.cpp records having made once.)
	::AppendMenuW(menu, MF_STRING | (pawNow ? MF_UNCHECKED : MF_CHECKED), 1,
	              reinterpret_cast<LPCWSTR>(w1.GrabUTF16Buffer(nil)));
	::AppendMenuW(menu, MF_STRING | (pawNow ? MF_CHECKED : MF_UNCHECKED), 2,
	              reinterpret_cast<LPCWSTR>(w2.GrabUTF16Buffer(nil)));

	// ★A picture beside each name, as the toolbox's own flyout has (the user's request). ⚠The
	//   bitmaps are owned HERE and deleted below: a menu does not take them over, and leaking one
	//   per press would be a handle leak that only shows after a long session.
	HBITMAP bmpTool = KCMLoadMenuBitmap(kKCMToolMenuBitmapID);
	HBITMAP bmpPaw  = KCMLoadMenuBitmap(kKCMPawToolMenuBitmapID);
	if (bmpTool != nil || bmpPaw != nil)
	{
		MENUITEMINFOW mii;
		::ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask  = MIIM_BITMAP;
		if (bmpTool != nil)
		{
			mii.hbmpItem = bmpTool;
			::SetMenuItemInfoW(menu, 1, FALSE, &mii);
		}
		if (bmpPaw != nil)
		{
			mii.hbmpItem = bmpPaw;
			::SetMenuItemInfoW(menu, 2, FALSE, &mii);
		}
	}

	// SysPoint is POINT on Windows (WSysType.h:56), so the press point goes straight through.
	HWND owner = ::WindowFromPoint(sDownWhere);
	if (owner == nil)
		owner = ::GetActiveWindow();

	// ★TPM_RETURNCMD: the choice comes back as the return value, so no menu message has to be
	//   routed anywhere. TPM_NONOTIFY keeps WM_COMMAND off the owner entirely.
	const int picked = ::TrackPopupMenu(menu,
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
		sDownWhere.x, sDownWhere.y, 0, owner, nil);

	::DestroyMenu(menu);
	if (bmpTool != nil) ::DeleteObject(bmpTool);
	if (bmpPaw  != nil) ::DeleteObject(bmpPaw);

	if (picked == 1)
		KCMToolButtonPressed(kFalse);
	else if (picked == 2)
		KCMToolButtonPressed(kTrue);
#endif
}

// The timer's callback: the button has been held long enough, so up goes the flyout.
// ⚠★★★IT MUST RETURN kEndOfTime. **Returning 0 means "call me again at once"**, not "done"
//   (IIdleTask.h) -- this plug-in froze InDesign that way once already, raising a sprite on every
//   idle tick. There is no second firing here by design.
static uint32 KCMToolFlyoutTimerFired(void* /*refPtr*/)
{
	sFlyoutShown = kTrue;
	KCMRaiseToolFlyout();		// ⚠returns only when the reader has let go of the button
	return IIdleTask::kEndOfTime;
}

/** The panel tool button's press-and-hold handler.

	It owns the whole press: down starts the clock, up decides what the press meant.
*/
class KCMToolButtonEH : public CEventHandler
{
public:
	KCMToolButtonEH(IPMUnknown* boss) : CEventHandler(boss) {}
	/** ⚠The timer is stopped here as well as on the way up: a panel closed mid-press would
		otherwise leave a timer pointing at a function in a plug-in that may unload. */
	virtual ~KCMToolButtonEH() { KCMStopFlyoutTimer(); }

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);
};

CREATE_PMINTERFACE(KCMToolButtonEH, kKCMToolButtonEHImpl)

bool16 KCMToolButtonEH::LButtonDn(IEvent* e)
{
	if (e == nil)
		return kFalse;

	KCMStopFlyoutTimer();			// a press that never got its release: start clean
	sFlyoutShown = kFalse;
	sDownWhere   = e->GlobalWhere();

	sTimer = (ICallbackTimer*)::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (sTimer != nil)
		sTimer->StartTimer(KCMToolFlyoutTimerFired, kKCMToolButtonHoldMs, this);

	// kTrue = handled. Nothing is decided yet: what the press MEANS depends on how long it lasts.
	return kTrue;
}

bool16 KCMToolButtonEH::LButtonUp(IEvent* e)
{
	KCMStopFlyoutTimer();

	// ★The menu is already up and owns the gesture from here: the reader picks an item, and that
	//   item does the choosing (KCMActionComponent -> KCMToolButtonPressed). Choosing a tool here
	//   as well would change the tool out from under the menu.
	if (sFlyoutShown)
	{
		sFlyoutShown = kFalse;
		return kTrue;
	}

	// A short press: choose the tool whose face is showing.
	InterfacePtr<IControlView> cv(this, UseDefaultIID());
	if (cv == nil)
		return kTrue;

	KCMToolButtonPressed(cv->GetWidgetID() == kKCMPawToolButtonWidgetID ? kTrue : kFalse);
	return kTrue;
}

// End, KCMToolButtonEH.cpp.
