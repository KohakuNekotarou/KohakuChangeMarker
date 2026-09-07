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
#include "IPatientUserPreference.h"	// how long the APPLICATION says a hold-to-reveal gesture waits
#include "IInterfaceColors.h"	// the interface colours (and class RealAGMColor) the menu is painted in
#include "ISession.h"			// GetExecutionContextSession (nil during teardown, so the type is spelled out)
#include "IWorkspace.h"			// the session workspace, where that preference lives

// General includes:
#include "CreateObject.h"		// ::CreateObject -- the timer is made, not queried
#include "PMString.h"
#include <vector>				// the icon is composited by hand (KCMDrawFlyoutIcon)
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
// ★Set by KCMToolButtonShutdown at the foot of this file: from there on no press arms a timer.
//   The other two timers in this plug-in keep the same flag (KCMPanelAlpha's sPanelAlphaShutdown,
//   KCMThumbIdleTask's sShutdown); the reasoning is with the function.
static bool16			sShutdown = kFalse;

// ★WHICH FACE THE BUTTON WAS WEARING WHEN THE PRESS LANDED -- read from the widget the press came
//   through, which is the same reading LButtonUp makes to decide what a short press chooses. The
//   flyout's tick uses it (see KCMRaiseToolFlyout), so the menu and the button cannot disagree.
static bool16			sDownFaceIsPaw = kFalse;

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

#ifdef WINDOWS
//========================================================================================
// THE FLYOUT IN INDESIGN'S COLOURS (2026-09-07, the user's request: "it is white now -- I want it
// to match InDesign's interface").
//
//  ★★A Win32 popup is drawn by the OS in the OS's colours, so against InDesign's dark interface it
//    arrives as a white rectangle. Three things make it match instead:
//      1. **the colours are asked of InDesign** -- IInterfaceColors on the session, the very route
//         the panel's scrollbar map already takes (KCMScrollMap.cpp) -- so all four brightness
//         themes are followed and not one colour is written down here;
//      2. **the items are owner-drawn**, because a menu's TEXT colour cannot be set any other way
//         (MENUINFO carries a background brush and nothing else, so the background alone would
//         leave black text on a dark ground);
//      3. **the menu is given an owner window of our own**, because WM_MEASUREITEM and WM_DRAWITEM
//         go to the OWNER -- which used to be InDesign's own window, whose procedure knows nothing
//         of them and would leave every item blank.
//
//  ⚠**THE OWNER IS CREATED AND DESTROYED AROUND THE MENU, class and all.** A window class whose
//    procedure lives in a plug-in that later unloads is the same shape of crash as a timer holding
//    a raw function pointer, which this file already guards against ([[plugin-teardown-robustness]]).
//  ★**EVERY STEP CAN FAIL BACK TO THE OLD MENU**: no owner window means no owner-draw, and the
//    items are appended as plain strings against InDesign's window exactly as before. A theme that
//    cannot be read falls back to a mid grey. The flyout never fails to appear over cosmetics.
//========================================================================================

/** One of InDesign's interface colours as a COLORREF, or `fallback` when it cannot be read -- no
	session during teardown, say. ⚠InterfacePtr(p, iid) accepts a nil pointer, so a gone session
	simply produces a nil interface here rather than a crash (the shape KCMScrollMap.cpp uses). */
static COLORREF KCMThemeColour(int32 which, COLORREF fallback)
{
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), IID_IINTERFACECOLORS);
	if (colors == nil)
		return fallback;

	RealAGMColor c;
	if (!colors->GetRealAGMColor(which, c))
		return fallback;

	const int r = (int)(ToDouble(c.red)   * 255.0 + 0.5);
	const int g = (int)(ToDouble(c.green) * 255.0 + 0.5);
	const int b = (int)(ToDouble(c.blue)  * 255.0 + 0.5);
	return RGB(r < 0 ? 0 : (r > 255 ? 255 : r),
	           g < 0 ? 0 : (g > 255 ? 255 : g),
	           b < 0 ? 0 : (b > 255 ? 255 : b));
}

/** What one owner-drawn item needs in order to draw itself. These live on KCMRaiseToolFlyout's
	stack for as long as the menu is up -- TrackPopupMenu does not return before then. */
struct KCMFlyoutItem
{
	const wchar_t*	fText;
	HBITMAP			fIcon;
	bool			fCurrent;		// the tool in use wears the tick
};

// The gutter that holds the tick, the gap after the icon, and the padding, in pixels.
static const int kKCMFlyoutTickWidth = 18;
static const int kKCMFlyoutGap       = 8;
static const int kKCMFlyoutPadY      = 5;
static const int kKCMFlyoutPadRight  = 20;

/** The font the system would have used for a menu, so the items are the size a reader expects.
	Owned by KCMRaiseToolFlyout for the life of one menu; the drawing procedure only reads it. */
static HFONT sFlyoutFont = nil;

static HFONT KCMFlyoutFont()
{
	NONCLIENTMETRICSW ncm;
	::ZeroMemory(&ncm, sizeof(ncm));
	ncm.cbSize = sizeof(ncm);
	if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
		return nil;
	return ::CreateFontIndirectW(&ncm.lfMenuFont);
}

static void KCMFlyoutIconSize(HBITMAP bmp, int& outW, int& outH)
{
	outW = outH = 0;
	BITMAP info;
	if (bmp != nil && ::GetObject(bmp, sizeof(info), &info) != 0)
	{
		outW = info.bmWidth;
		outH = info.bmHeight;
	}
}

/** Draw one of this plug-in's 32-bit bitmaps over a solid background, by hand.

	★★**NO AlphaBlend, AND THAT IS TO AVOID A NEW LIBRARY.** AlphaBlend lives in msimg32, which
	  this project does not link; adding it means editing build files that sit OUTSIDE the
	  repository ([[vcxproj-registration-not-build-dependency]]) or reaching for a #pragma. The
	  icons are 16 pixels square, so compositing them here costs nothing and depends on nothing.
	⚠**The resource is premultiplied** -- that is what a menu's hbmpItem expects and what these
	  bitmaps were made for -- so the mix is `src + back*(255-a)/255`, not `src*a + back*(1-a)`. */
static void KCMDrawFlyoutIcon(HDC dc, HBITMAP bmp, int x, int y, COLORREF back)
{
	BITMAP info;
	if (dc == nil || bmp == nil || ::GetObject(bmp, sizeof(info), &info) == 0)
		return;

	const int w = info.bmWidth;
	const int h = info.bmHeight;
	if (w <= 0 || h <= 0)
		return;

	BITMAPINFO bi;
	::ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth       = w;
	bi.bmiHeader.biHeight      = -h;			// top-down: row 0 is the top one
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	std::vector<BYTE> bits((size_t)w * (size_t)h * 4);
	if (::GetDIBits(dc, bmp, 0, (UINT)h, &bits[0], &bi, DIB_RGB_COLORS) == 0)
		return;

	const int br = GetRValue(back), bg = GetGValue(back), bb = GetBValue(back);
	for (size_t i = 0; i + 3 < bits.size(); i += 4)
	{
		const int a = bits[i + 3];
		if (a == 255)
			continue;
		const int inv = 255 - a;
		bits[i + 0] = (BYTE)(bits[i + 0] + bb * inv / 255);		// BGRA order
		bits[i + 1] = (BYTE)(bits[i + 1] + bg * inv / 255);
		bits[i + 2] = (BYTE)(bits[i + 2] + br * inv / 255);
		bits[i + 3] = 255;
	}
	::SetDIBitsToDevice(dc, x, y, (DWORD)w, (DWORD)h, 0, 0, 0, (UINT)h,
	                    &bits[0], &bi, DIB_RGB_COLORS);
}

/** The window procedure InDesign's window had before the menu went up, put back the moment it
	comes down. ⚠One menu at a time, on the main thread only -- the same assumption the file's other
	statics rest on. */
static WNDPROC sFlyoutPrevProc = nil;

/** Stands in front of the owner window's procedure for the life of one menu. It exists for exactly
	two messages -- the ones a menu sends about owner-drawn items -- and passes everything else on.

	⚠**WHY NOT A WINDOW OF OUR OWN.** That was built first and MEASURED not to work: a hidden 0x0
	  owner made the flyout stop appearing altogether (held 1400 ms, nothing on screen, where the
	  same press had shown the menu a minute earlier). A popup menu needs an owner that is really
	  there and in front; InDesign's window is, and borrowing its procedure for the length of one
	  synchronous call is the smaller change of the two. */
static LRESULT CALLBACK KCMFlyoutOwnerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_MEASUREITEM)
	{
		MEASUREITEMSTRUCT* const mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
		const KCMFlyoutItem* const item =
			(mis != nil) ? reinterpret_cast<const KCMFlyoutItem*>(mis->itemData) : nil;
		if (item == nil || item->fText == nil)
			return ::DefWindowProcW(hwnd, msg, wp, lp);

		int iconW = 0, iconH = 0;
		KCMFlyoutIconSize(item->fIcon, iconW, iconH);

		SIZE text = { 0, 0 };
		HDC dc = ::GetDC(nil);
		if (dc != nil)
		{
			HGDIOBJ old = (sFlyoutFont != nil) ? ::SelectObject(dc, sFlyoutFont) : nil;
			::GetTextExtentPoint32W(dc, item->fText, (int)::wcslen(item->fText), &text);
			if (old != nil)
				::SelectObject(dc, old);
			::ReleaseDC(nil, dc);
		}

		mis->itemWidth  = (UINT)(kKCMFlyoutTickWidth + iconW + kKCMFlyoutGap +
		                         text.cx + kKCMFlyoutPadRight);
		mis->itemHeight = (UINT)((iconH > text.cy ? iconH : text.cy) + kKCMFlyoutPadY * 2);
		return TRUE;
	}

	if (msg == WM_DRAWITEM)
	{
		DRAWITEMSTRUCT* const dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
		const KCMFlyoutItem* const item =
			(dis != nil) ? reinterpret_cast<const KCMFlyoutItem*>(dis->itemData) : nil;
		if (item == nil || dis->hDC == nil || item->fText == nil)
			return ::DefWindowProcW(hwnd, msg, wp, lp);

		const bool selected = (dis->itemState & ODS_SELECTED) != 0;
		// ★The fallbacks are the dark interface's own values, so a theme that cannot be read still
		//   produces a readable menu rather than the white one this replaced.
		const COLORREF back = selected ? KCMThemeColour(kInterfaceHighLight,     RGB( 70, 100, 140))
		                               : KCMThemeColour(kInterfacePaletteFill,   RGB( 50,  50,  50));
		const COLORREF fore = selected ? KCMThemeColour(kInterfaceHighLightText, RGB(255, 255, 255))
		                               : KCMThemeColour(kInterfaceTextColor,     RGB(215, 215, 215));

		HBRUSH backBrush = ::CreateSolidBrush(back);
		if (backBrush != nil)
		{
			::FillRect(dis->hDC, &dis->rcItem, backBrush);
			::DeleteObject(backBrush);
		}

		// The tick is DRAWN rather than borrowed: DrawFrameControl paints in the system's colours,
		// which is the one thing this menu is getting away from.
		if (item->fCurrent)
		{
			HPEN pen = ::CreatePen(PS_SOLID, 2, fore);
			if (pen != nil)
			{
				HGDIOBJ oldPen = ::SelectObject(dis->hDC, pen);
				const int cx = dis->rcItem.left + kKCMFlyoutTickWidth / 2;
				const int cy = (dis->rcItem.top + dis->rcItem.bottom) / 2;
				::MoveToEx(dis->hDC, cx - 4, cy, nil);
				::LineTo(dis->hDC, cx - 1, cy + 3);
				::LineTo(dis->hDC, cx + 4, cy - 4);
				::SelectObject(dis->hDC, oldPen);
				::DeleteObject(pen);
			}
		}

		int iconW = 0, iconH = 0;
		KCMFlyoutIconSize(item->fIcon, iconW, iconH);
		if (iconW > 0 && iconH > 0)
			KCMDrawFlyoutIcon(dis->hDC, item->fIcon,
			                  dis->rcItem.left + kKCMFlyoutTickWidth,
			                  (dis->rcItem.top + dis->rcItem.bottom - iconH) / 2, back);

		RECT textRect = dis->rcItem;
		textRect.left += kKCMFlyoutTickWidth + iconW + kKCMFlyoutGap;
		::SetBkMode(dis->hDC, TRANSPARENT);
		::SetTextColor(dis->hDC, fore);
		HGDIOBJ oldFont = (sFlyoutFont != nil) ? ::SelectObject(dis->hDC, sFlyoutFont) : nil;
		::DrawTextW(dis->hDC, item->fText, -1, &textRect,
		            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
		if (oldFont != nil)
			::SelectObject(dis->hDC, oldFont);
		return TRUE;
	}

	// ★Everything else belongs to the window we borrowed: this procedure is standing in front of
	//   InDesign's own for the life of one menu, and only two messages are ours.
	return (sFlyoutPrevProc != nil) ? ::CallWindowProcW(sFlyoutPrevProc, hwnd, msg, wp, lp)
	                                : ::DefWindowProcW(hwnd, msg, wp, lp);
}

/** Stands the procedure above in front of a window, and PUTS THE REAL ONE BACK however the caller
	leaves -- normally, or by an exception crossing TrackPopupMenu's modal loop.

	⚠★★**A WINDOW LEFT POINTING AT A PLUG-IN THAT THEN UNLOADS IS A CRASH**, which is the same rule
	  this file already keeps for its timer (ICallbackTimer.h). One statement at the end of the
	  happy path is not enough to keep it, so the restore is a destructor.
	⚠★★**IT REFUSES TO STACK.** A second press arriving while a menu is up would otherwise read
	  OUR procedure back as "the previous one" and install it permanently -- the window would then
	  be pointing at this plug-in with nothing behind it. When one is already in place this one
	  installs nothing and the caller builds the plain menu instead. */
struct KCMFlyoutSubclass
{
	explicit KCMFlyoutSubclass(HWND window) : fWindow(nil)
	{
		if (window == nil || sFlyoutPrevProc != nil)
			return;
		WNDPROC prev = (WNDPROC)::SetWindowLongPtrW(window, GWLP_WNDPROC,
		                                           (LONG_PTR)&KCMFlyoutOwnerProc);
		if (prev == nil)
			return;					// refused: the caller falls back to the plain menu
		sFlyoutPrevProc = prev;
		fWindow         = window;
	}

	~KCMFlyoutSubclass()
	{
		if (fWindow != nil)
		{
			::SetWindowLongPtrW(fWindow, GWLP_WNDPROC, (LONG_PTR)sFlyoutPrevProc);
			sFlyoutPrevProc = nil;
		}
	}

	bool Installed() const { return fWindow != nil; }

private:
	HWND fWindow;

	// Not copyable: two of these would put the procedure back twice.
	KCMFlyoutSubclass(const KCMFlyoutSubclass&);
	KCMFlyoutSubclass& operator=(const KCMFlyoutSubclass&);
};

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

	// ★★THE TICK MARKS THE FACE THE BUTTON IS WEARING -- which is to say, the tool a short press
	//   would give you. It is NOT "whichever of the two is active".
	//   ⚠★★★It used to be `KCMIsPawToolActive()` alone, written as a two-way choice:
	//       items[0].fCurrent = (pawNow == kFalse);   // the comparison tool
	//       items[1].fCurrent = (pawNow != kFalse);   // the stamp
	//     That reads "if the stamp is not active then the comparison tool must be current", and
	//     **when NEITHER is active it is simply false**: with the Type tool in hand the tick sat on
	//     the comparison tool, which the reader had not chosen and was not using (the user's report,
	//     2026-09-07: "the panel still wears the paw, but the menu's tick is on the other one").
	//   ★The face is the right answer because the face is already a decided thing: with neither
	//     tool active the button deliberately KEEPS the last tool used, the way a toolbox slot does
	//     (KCMSyncToolButtonViews says why). Ticking anything else makes the menu contradict the
	//     button it hangs from. And when one of the two IS active the face is that tool, so nothing
	//     about the ordinary case changes ([[one-question-one-place]]).
	const bool16 tickPaw = sDownFaceIsPaw;

	// ★A picture beside each name, as the toolbox's own flyout has (the user's request). ⚠The
	//   bitmaps are owned HERE and deleted below: a menu does not take them over, and leaking one
	//   per press would be a handle leak that only shows after a long session.
	HBITMAP bmpTool = KCMLoadMenuBitmap(kKCMToolMenuBitmapID);
	HBITMAP bmpPaw  = KCMLoadMenuBitmap(kKCMPawToolMenuBitmapID);

	// SysPoint is POINT on Windows (WSysType.h:56), so the press point goes straight through.
	HWND owner = ::WindowFromPoint(sDownWhere);
	if (owner == nil)
		owner = ::GetActiveWindow();
	if (owner == nil)
	{
		// ⚠TrackPopupMenu refuses a null owner, and refusing here is what keeps the menu handle
		//   from leaking on that path.
		::DestroyMenu(menu);
		if (bmpTool != nil) ::DeleteObject(bmpTool);
		if (bmpPaw  != nil) ::DeleteObject(bmpPaw);
		return;
	}

	// ***** BORROW THAT WINDOW'S PROCEDURE, WHICH IS WHAT MAKES THE COLOURS POSSIBLE. *****
	//   The two owner-draw messages go to the menu's OWNER, and the owner has to be a window that
	//   is really on screen and in front (a hidden one of our own was tried and the menu stopped
	//   appearing at all). So the owner stays InDesign's and this file stands in front of its
	//   procedure for the length of ONE SYNCHRONOUS CALL.
	//   ⚠When the swap is refused, `ownerDrawn` stays false and the plain menu is built instead --
	//   the flyout is never lost over its appearance.
	KCMFlyoutSubclass subclass(owner);
	const bool ownerDrawn = subclass.Installed();

	// ⚠The cast is sound HERE and only here: wchar_t is 16 bits on Windows, and this whole
	//   function is inside #ifdef WINDOWS. (On the Mac it is 32 and the same cast would read past
	//   the buffer -- the mistake KCMChangedPagesTSV.cpp records having made once.)
	const wchar_t* const text1 = reinterpret_cast<const wchar_t*>(w1.GrabUTF16Buffer(nil));
	const wchar_t* const text2 = reinterpret_cast<const wchar_t*>(w2.GrabUTF16Buffer(nil));

	KCMFlyoutItem items[2];
	items[0].fText = text1; items[0].fIcon = bmpTool; items[0].fCurrent = (tickPaw == kFalse);
	items[1].fText = text2; items[1].fIcon = bmpPaw;  items[1].fCurrent = (tickPaw != kFalse);

	HBRUSH menuBack = nil;
	if (ownerDrawn)
	{
		sFlyoutFont = KCMFlyoutFont();

		// MF_OWNERDRAW: the text and the picture both come from the item data below, so no string
		// and no hbmpItem is given to the menu at all.
		::AppendMenuW(menu, MF_OWNERDRAW, 1, reinterpret_cast<LPCWSTR>(&items[0]));
		::AppendMenuW(menu, MF_OWNERDRAW, 2, reinterpret_cast<LPCWSTR>(&items[1]));

		// ★The BACKGROUND BRUSH as well as the items: the menu paints a margin of its own around
		//   them, and an owner-drawn item cannot reach it. Without this the frame stays white.
		menuBack = ::CreateSolidBrush(KCMThemeColour(kInterfacePaletteFill, RGB(50, 50, 50)));
		if (menuBack != nil)
		{
			MENUINFO mi;
			::ZeroMemory(&mi, sizeof(mi));
			mi.cbSize  = sizeof(mi);
			mi.fMask   = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
			mi.hbrBack = menuBack;
			::SetMenuInfo(menu, &mi);
		}
	}
	else
	{
		// The menu as it was before the colours: the system draws it, in the system's colours.
		// ⚠The same source as the owner-drawn tick above: two answers to one question is what put
		//   the tick on the wrong tool in the first place.
		::AppendMenuW(menu, MF_STRING | (tickPaw ? MF_UNCHECKED : MF_CHECKED), 1, text1);
		::AppendMenuW(menu, MF_STRING | (tickPaw ? MF_CHECKED : MF_UNCHECKED), 2, text2);
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
	}

	// ★TPM_RETURNCMD: the choice comes back as the return value, so no menu message has to be
	//   routed anywhere. TPM_NONOTIFY keeps WM_COMMAND off the owner entirely.
	const int picked = ::TrackPopupMenu(menu,
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
		sDownWhere.x, sDownWhere.y, 0, owner, nil);

	::DestroyMenu(menu);
	if (bmpTool != nil) ::DeleteObject(bmpTool);
	if (bmpPaw  != nil) ::DeleteObject(bmpPaw);
	if (menuBack != nil) ::DeleteObject(menuBack);
	if (sFlyoutFont != nil)
	{
		::DeleteObject(sFlyoutFont);
		sFlyoutFont = nil;
	}
	// (The borrowed procedure is put back by ~KCMFlyoutSubclass, on every way out of this
	//  function -- see the class for why that is not left to a statement here.)

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
	// ⚠★★★THE TIMER IS LET GO HERE, BEFORE THE MENU. It cannot be left to LButtonUp, because
	//   **TrackPopupMenu captures the mouse and the release goes to the menu, not to this widget**
	//   -- so the button-up this handler was relying on may never arrive. The object would then sit
	//   here owned by nobody until the next press, and ICallbackTimer.h names exactly that as a
	//   crash: a timer holding a raw function pointer into a plug-in that unloads.
	// ★Dropped before rather than after the menu, because the menu runs a modal loop: anything
	//   wanting to stop the timer during it would find nothing to stop, which is the honest state.
	//   (The SDK's own user of ICallbackTimer likewise releases inside its callback.)
	ICallbackTimer* fired = sTimer;
	sTimer = nil;
	if (fired != nil)
	{
		fired->StopTimer();
		fired->Release();
	}

	sFlyoutShown = kTrue;
	KCMRaiseToolFlyout();		// ⚠returns only when the reader has let go of the button
	return IIdleTask::kEndOfTime;
}

/* How long the button must be held before the tool flyout opens.

   ★★**IT IS THE APPLICATION'S SETTING, NOT A NUMBER OF OURS** (2026-09-07). InDesign keeps one
     delay for hold-to-reveal gestures -- IPatientUserPreference on the session workspace, with
     named values (Off -1 / NoDelay 0 / Fast 330 / Standard 500 / Long 1000, and anything up to
     10000). A reader who has set the application to "long" and finds this one button opening on a
     schedule of its own is being told, in the only way an interface can say it, that this panel is
     not part of the application. The 400 ms this used to hold is now only the fallback.

   ⚠**OFF (-1) FALLS BACK TO OUR DEFAULT rather than switching the flyout off.** Off means the
     application's own hold-to-reveal behaviour is off; here the flyout is the ONLY way to reach the
     other tool FROM THIS BUTTON, so reading it literally would take a function away rather than
     change a timing. (The toolbox's own flyout is a separate road and is unaffected either way.)
   ★A missing interface -- no session during teardown, or a workspace without the preference --
     takes the same road, which is why the fallback is written once, here.

   ⚠**Zero is honoured but not passed on as 0.** ICallbackTimer's callback treats 0 as "call me
     again at once" and this file's own comment records freezing InDesign that way; one millisecond
     is the same gesture to a person and cannot be mistaken for that.
*/
static uint32 KCMToolFlyoutDelayMs()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IWorkspace> ws(session != nil ? session->QueryWorkspace() : nil);
	InterfacePtr<IPatientUserPreference> patient(ws, UseDefaultIID());
	if (patient == nil)
		return kKCMToolButtonHoldMs;

	const int32 ms = patient->GetPatientUserDelayTime();
	if (ms < 0)
		return kKCMToolButtonHoldMs;		// patient user mode off -- see above
	return (ms == 0) ? 1 : (uint32)ms;
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

	// The face under the press. Two widgets share this frame and only the shown one is enabled, so
	// the widget this handler is on IS the face the reader can see.
	InterfacePtr<IControlView> downView(this, UseDefaultIID());
	sDownFaceIsPaw = (downView != nil &&
	                  downView->GetWidgetID() == kKCMPawToolButtonWidgetID) ? kTrue : kFalse;

	// ⚠**Nothing is armed once the plug-in's Shutdown has run.** A widget can still be standing,
	//   and still deliver a press, after the UI's shutdown service has gone through -- and a timer
	//   built then would hold a raw function pointer into a plug-in that is unloading. The press is
	//   still handled: LButtonUp below chooses the tool whose face is showing, which is what a short
	//   press does anyway. Only the held-down flyout is lost, and only after Shutdown.
	if (!sShutdown)
	{
		sTimer = (ICallbackTimer*)::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER);
		if (sTimer != nil)
			sTimer->StartTimer(KCMToolFlyoutTimerFired, KCMToolFlyoutDelayMs(), this);
	}

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

/* KCMToolButtonShutdown (declared in KCMUIShared.h) -- the plug-in is going down.

   ★**THE SECOND HALF IS WHAT THIS ADDS.** Stopping the timer was already covered twice: every
     exit from a press goes through KCMStopFlyoutTimer, and this handler's destructor calls it as
     well, so a panel closed mid-press was already safe. What was missing is the REFUSAL TO ARM
     AGAIN -- the shape the plug-in's other two timers already have (KCMPanelAlpha's
     sPanelAlphaShutdown, KCMThumbIdleTask's sShutdown). The destructor only fires when the widget
     is destroyed, and nothing here decided what happens if the UI's shutdown service runs first.
   ⚠**No fault has been observed from its absence**, and the window was never large: the timer is
     armed only between a press and its release. It is written because a rule kept in two places
     out of three is not a rule -- and because the delay is no longer a number of ours: it is the
     application's, whose named settings reach 1000 ms and whose preference accepts up to 10000
     (KCMToolFlyoutDelayMs), so the window is not as short as it was when 400 was fixed here.
   ★Asked for by the spec map's RUN-61 ("the clean-up during a quit, checked again in the code").
*/
void KCMToolButtonShutdown()
{
	sShutdown = kTrue;
	KCMStopFlyoutTimer();
}

// End, KCMToolButtonEH.cpp.
