//========================================================================================
//
//  KCMPanelAlpha.cpp
//
//  The panel translucency toggles. EVERY WIN32 DEPENDENCY IS CONFINED TO THIS FILE.
//
//  HOW IT WORKS (established by measurement; read docs/ai-notes/win32-window-transparency.md
//  before changing any of it):
//    1. get the OWL.Palette window from the panel's WidgetID --
//       IPanelMgr::GetPanelFromWidgetID -> GetPaletteRefContainingPanel -> PaletteRef::GetOWLControl
//       An earlier version instead ran EnumWindows looking for cls == "OWL.Palette" and
//       title == the panel's display name, and THAT DOES NOT WORK FOR THE APPLICATION'S OWN
//       PANELS -- the window title changes with the UI language (the Pages panel is "Pages" in an
//       English UI). A WidgetID is a number.
//    2. GetAncestor(GA_ROOT) gives the top-level window it is in RIGHT NOW
//    3. if that is "indesign" (the main frame) it is expanded inside a dock -- do nothing
//    4. if it is "OWL.Dock" (floating) or "OWL.FrameDrawer" (the drawer that opens when its icon
//       is clicked), set the alpha with SetLayeredWindowAttributes
//
//  THE OWL.Dock HWND CHANGES when a panel is docked and then floated again (the old window is
//    destroyed and a new one built). CLOSING AND REOPENING A PANEL IS DIFFERENT: the same Dock
//    survives, alpha and all (measured from outside on Release 21.0.2.2, one step at a time; an
//    earlier note here claiming it changed was wrong). The OWL.Palette HWND survives both.
//    So the Dock's HWND is never cached -- it is looked up every time.
//  A Dock belongs to ONE panel (55-56 pairs sit invisible from startup, each naming its own
//    panel), so one panel's alpha can never reach another's.
//  InDesign ITSELF sets WS_EX_LAYERED on OWL.Dock (EXSTYLE=0x08080000). The style needs neither
//    adding nor removing -- AND REMOVING IT BREAKS THE APPLICATION'S OWN DRAWING (restoring means
//    alpha=255 and nothing else).
//  DO NOT PUT WS_EX_LAYERED ON THE CHILD WINDOW (OWL.Palette): it does not go translucent and the
//    colours break (measured).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// This plug-in:
#include "KCMPanelAlpha.h"
#include "KCMConstants.h"		// kKCMPanelAlphaValue / kKCMPanelAlphaReapplyTries / ...DelayMillis
#include "KCMUIID.h"			// kKCMPanelWidgetID (the WidgetID this aims at) and our own IIDs
									// and ImplIDs

// For the observer that subscribes to the panel's visibility changing:
#include "CObserver.h"
#include "ISubject.h"			// AttachObserver / IsAttached
#include "ISession.h"			// GetExecutionContextSession (can be nil during teardown)
#include "IApplication.h"		// QueryPanelManager
#include "IActiveContext.h"		// kActiveContextBoss, where the observer implementation lives
#include "IPanelMgr.h"			// IID_IPANELMGR (the subject subscribed to)
#include "AppUIID.h"			// kPaletteVisibilityChangedMessage (a public header)
#include "ShuksanID.h"			// kApplicationSuspendMsg (another application became frontmost)

// The window is rebuilt AFTER the notification, so a one-shot timer is used to let the events go
// round once and then apply again:
#include "ICallbackTimer.h"		// StartTimer / StopTimer (derived from IIdleTask, which is where kEndOfTime comes from)
#include "CreateObject.h"		// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)

// For going opaque again while the pointer is over the panel:
#include "CPMUnknown.h"			// the implementation base class
#include "IMouseRollOver.h"		// MouseEnter / MouseOver / MouseLeave

// EVERYTHING NEEDED TO GET A PANEL'S WINDOW (HWND) FROM THE SDK SIDE.
//   PaletteRef holds an HWND inside it: PaletteRef.h defines OWLControlRef as HWND, and
//   GetOWLControl() returns it. Going from IPanelMgr::GetPanelFromWidgetID (a numeric WidgetID) to
//   GetPaletteRefContainingPanel() gives the PaletteRef, and the window is in there.
//   => NEITHER TITLE MATCHING (which is translated) NOR EnumWindows (which walks every window) IS
//      NEEDED. An earlier audit concluded there was no route from the SDK to a panel's HWND (four
//      candidates all failed); this is the fifth, and every panel was reached with it on a live
//      build.
#include "IControlView.h"		// the panel GetPanelFromWidgetID returns
#include "PaletteRef.h"			// PaletteRef::GetOWLControl (= the HWND)
#include "PagesPanelID.h"		// kPagesPanelWidgetID -- what the Pages panel is aimed at.
									// (PaletteRefUtils.h was included here for a long time, with a
									//  comment about walking up the hierarchy, and this file never
									//  called any of it. It was measured and then removed -- the
									//  reason is on KCMQueryTranslucentTarget below.)

// windows.h goes AFTER the SDK headers, so that its macros do not collide with SDK names.
#ifdef WINDOWS
#include <windows.h>
#endif

//----------------------------------------------------------------------------------------
// The targets that can be made translucent.
//
//  1 = our own panel. 2 = THE APPLICATION'S PAGES PANEL (at the user's request).
//  Holding a target as a WidgetID -- a number -- is what makes the second one possible: window
//    titles change with the UI language, so they cannot be used for the application's own panels
//    ("Pages" in an English UI).
//  TO ADD ONE, add an enumerator here and its WidgetID to kKCMAlphaWidgetIDs. A toggle (a menu
//    item and a key for persistence) is needed per target either way.
//  (A toolbox target was added and taken out again the same day, at the user's decision. That it
//   worked with only those additions does show this shape does not care what the target is. The
//   window structure and where to aim were measured and kept in the memory note
//   translucent-toolbox-idea, if it is ever tried again.)
//
//  3 = THE BOOK COMPARISON DIALOG (at the user's request), the first target that is not a panel.
//    THE ONLY THING THIS SHAPE COULD NOT ABSORB WAS HOW TO FIND THE WINDOW, and that is confined
//    to a `which` branch in each of KCMQueryPaletteWindow and KCMQueryTranslucentTarget. Writing
//    the alpha, deciding to go opaque under the pointer, the delayed re-apply and following the
//    Win32 hook all work on it unchanged, line for line.
enum
{
	kKCMAlphaSelf       = 0,	// our own panel (Kohaku Change Marker)
	kKCMAlphaPages      = 1,	// the application's Pages panel
	kKCMAlphaBookDialog = 2,	// our own book comparison dialog (not a panel: the window comes from elsewhere)
	kKCMAlphaCount      = 3
};

// The toggles, held for the session; persisting them is KCMPanelState.cpp's job. Off by default.
// The state is kept on the Mac too -- it is only the applying that does nothing there.
static bool16 sTranslucentOn[kKCMAlphaCount] = { kFalse, kFalse, kFalse };

// Is any of them on? Shared by the decision to install or remove the Win32 hook and by the
// decision to carry on or stop the delayed re-apply -- counting the same thing in two places is
// how the two come to disagree.
static bool16 KCMAnyTranslucentOn()
{
	for (int32 i = 0; i < kKCMAlphaCount; ++i)
	{
		if (sTranslucentOn[i])
			return kTrue;
	}
	return kFalse;
}

#ifdef WINDOWS

// Is the pointer over the target window -- the top-level window the panel is in right now?
//
// THIS IS MEASURED EVERY TIME RATHER THAN HELD IN A FLAG. It used to be a static flag raised and
//   lowered by IMouseRollOver, which had two weaknesses; measuring makes both structurally
//   impossible:
//     (a) MouseLeave does NOT arrive when the panel is closed, docked, or another application is
//         switched to WITH THE POINTER STILL ON IT. Miss one and "the pointer is on it" sticks,
//         so the toggle can be on and nothing ever goes translucent.
//     (b) IMouseRollOver only sees THE PANEL'S OWN WIDGETS, so it does not react on the tab strip
//         (the one reading "Kohaku Change Marker") or the title strip (the << and x strip) -- which
//         is what prompted the user's request in the first place.
//
// The target window contains the tab strip, the title strip and the panel body alike, so this one
//   test covers "the pointer is somewhere on the panel" entirely. THAT THE TAB STRIP CANNOT BE
//   REACHED FROM THE SDK SIDE was settled on a live build: the panel widget's parent chain ends
//   after ONE step at kOWLHostedPanelWrapperBoss (QueryParent() == nil), and its bbox was
//   identical to the panel body's -- the chrome is outside the widget tree, on the OWL side.
//
// DO NOT DECIDE ON THE RECTANGLE ALONE (GetWindowRect + PtInRect): another window on top of it
//   would still count as "on it". The rectangle is the cheap rejection; the answer comes from
//   WindowFromPoint plus a GA_ROOT match.
//
// THE TEST IS THE ONE KBS USES (at the user's instruction): the pointer is on the panel when it is
//   INSIDE THE PANEL'S RECTANGLE and what is under it belongs to InDesign itself. With a menu
//   open, it stays opaque as long as the pointer is over the panel.
//   This function is shared by every target (the caller just passes that target's window), so our
//   own panel and the application's Pages panel behave identically.
//
//   THIS PLACE HAS CHANGED FIVE TIMES, and what settled it was:
//     the wobble was caused by AN INTERSECTION TEST, not by the "belongs to this process" test.
//     The user's report -- open the panel menu, pick the frame opacity item, and while walking its
//     submenu the opaque panel goes translucent -- happened like this:
//       the flyout itself overlaps the panel  -> intersects     -> opaque
//       its submenu opens off to the right    -> does not       -> translucent
//     It was deciding on THE MENU WINDOW'S POSITION, so it had to wobble with which way the menu
//     opened and how long it was (both of which vary with the position on screen).
//
//   WHAT IS TESTED NOW IS THE POINTER'S POSITION AND NOTHING ELSE:
//     1. outside the panel's rectangle -> not on it (wherever a menu may be is irrelevant)
//     2. inside it, with a window of our own (InDesign's) underneath -> on it
//   The rectangle does not move, so walking a menu and its submenu inside case 2 cannot change the
//   answer. Nothing to wobble.
//
//   @warning WHAT THIS DELIBERATELY ACCEPTS:
//     - while a menu extends OUTSIDE the panel's rectangle and the pointer is on one of those
//       outer items, the panel goes translucent. The test has exactly one reference -- the panel's
//       rectangle -- so what is seen matches why it happened.
//     - when ANOTHER InDesign WINDOW overlaps the panel, a pointer over that window counts as
//       being on the panel. It is hidden, so nothing looks wrong on screen, and moving the pointer
//       out of the rectangle puts it right on the next move. KBS has accepted the same trade.
static bool KCMClassIs(HWND h, const wchar_t* wanted);	// defined below (an exact match on the window class name)

static bool KCMCursorOverWindow(HWND target)
{
	if (target == nullptr)
		return false;

	POINT pt;
	if (!::GetCursorPos(&pt))
		return false;

	RECT rc;
	if (!::GetWindowRect(target, &rc))
		return false;
	if (!::PtInRect(&rc, pt))
		return false;		// outside the rectangle: certainly not on it (where most pointer movement ends)

	HWND under = ::WindowFromPoint(pt);
	if (under == nullptr)
		return false;

	const HWND root = ::GetAncestor(under, GA_ROOT);
	if (root == target)
		return true;		// the panel itself: the ordinary answer

	// Does it belong to InDesign as well? Then it is something the panel put up over itself -- a
	// flyout, its submenu, a context menu, a tooltip -- and the pointer has not left the panel.
	// ASK THE TOP-LEVEL WINDOW: a menu is not a child of the panel, it is an independent top-level
	// window owned by the application.
	DWORD pid = 0;
	::GetWindowThreadProcessId(root, &pid);
	return (pid == ::GetCurrentProcessId());
}

// The effective alpha: translucent only when that target's toggle is on AND the pointer is not
// over it. Kept in this one place, because both the applying side and the hook's test use it.
// WITH THE TOGGLE OFF IT DOES NOT EVEN LOOK AT THE POINTER (this feature runs only when it is on,
// which is the user's decision).
// Not built on the Mac at all, where there is nothing to apply -- so that it is not an unused
// function warning.
static uint8 KCMEffectiveAlpha(int32 which, HWND target)
{
	if (!sTranslucentOn[which])
		return 255;

	return KCMCursorOverWindow(target) ? 255 : kKCMPanelAlphaValue;
}

// Installing and removing the Win32 event hook (defined in the WINDOWS block below). It is
// only installed while a toggle is on.
static void KCMInstallWinEventHook();
static void KCMRemoveWinEventHook();
#endif

// Write one target's state. The public functions below are thin wrappers around this, so that
// the decision lives in one place and does not spread out as targets are added.
static void KCMSetTranslucentFor(int32 which, bool16 on)
{
	sTranslucentOn[which] = on;

#ifdef WINDOWS
	// "Expanded in a dock <-> floating" and "drawer -> floating" SEND NOT ONE SDK NOTIFICATION
	// (confirmed on both the Debug and the Release build). The only handle on them is Win32's
	// parent-change event, so a hook is installed. INSTALL IT WHEN ANY TARGET IS ON AND REMOVE IT
	// ONLY WHEN THEY ALL GO OFF -- otherwise turning one off would stop the other one following.
	if (KCMAnyTranslucentOn())
		KCMInstallWinEventHook();
	else
		KCMRemoveWinEventHook();
#endif
}

bool16 KCMGetPanelTranslucent()
{
	return sTranslucentOn[kKCMAlphaSelf];
}

void KCMSetPanelTranslucent(bool16 on)
{
	KCMSetTranslucentFor(kKCMAlphaSelf, on);
}

bool16 KCMGetPagesPanelTranslucent()
{
	return sTranslucentOn[kKCMAlphaPages];
}

void KCMSetPagesPanelTranslucent(bool16 on)
{
	KCMSetTranslucentFor(kKCMAlphaPages, on);
}

#ifdef WINDOWS

// GET A PANEL'S OWL.Palette WINDOW FROM THE SDK SIDE.
//
//  WHY IT WAS CHANGED: the older code walked every window looking for class == OWL.Palette and
//  title == the panel's display name. Our own panel has an English display name in every locale,
//  so that held -- but IT DOES NOT WORK FOR THE APPLICATION'S OWN PANELS: on one machine, one
//  panel, the title was measured as "Pages" in an English UI and its translation in a Japanese one
//  (a Debug build in English against a Release build in Japanese).
//
//  THE RIGHT ROUTE: PaletteRef holds an HWND inside it.
//    IPanelMgr::GetPanelFromWidgetID(WidgetID)      ... a WidgetID is a number, so no language
//      -> IPanelMgr::GetPaletteRefContainingPanel() ... the PaletteRef holding that panel
//         -> PaletteRef::GetOWLControl()            ... OWLControlRef is HWND (PaletteRef.h)
//
//  WHAT COMES BACK IS A CONTRACT, NOT AN OBSERVATION. IPanelMgr says of
//    GetPaletteRefContainingPanel that "for regular tabbed palettes, this should return an object
//    of type kTabPanelContainerType" -- so getting type=8 from the hierarchy below is what the
//    header promises, not something that varies by environment. (Which is why
//    KCMQueryPanelPaletteFromSDK does not check the class name of what it returns. The cache and
//    the hook check class names for a different reason: the OS recycles HWNDs.)
//
//  THE HIERARCHY, measured, identical for the Pages panel and ours, and matching what PaletteRef.h
//  describes:
//    type=8 kTabPanelContainerType = OWL.Palette   <- this is what comes back (see the contract)
//    type=7 kTabGroupType          = OWL.TabGroup
//    type=6 kTabPaneType           = OWL.TabPane
//    type=3 kDockType              = OWL.Dock      <- the window the alpha is written to
//  TURNING THAT INTO A TOP-LEVEL WINDOW IS STILL KCMQueryTranslucentTarget'S JOB (GetAncestor).
//    Handling the drawer (OWL.FrameDrawer) and the in-dock expansion (indesign) belongs there and
//    has a track record, so it is left alone. What changed here is only HOW THE OWL.Palette IS
//    FOUND.
//
//  (An earlier audit concluded there was no route from the SDK to a panel's HWND, four candidates
//   having failed. That was wrong; this is the fifth.)
static HWND KCMQueryPanelPaletteFromSDK(const WidgetID& panelWidgetID)
{
	// the session can be nil during teardown
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nullptr;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nullptr;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return nullptr;

	// GetPanelFromWidgetID does not AddRef (in IPanelMgr, "the caller must release" is said of
	// CreatePanel and of nothing else), so this does not Release either.
	IControlView* panel = panelMgr->GetPanelFromWidgetID(panelWidgetID);
	if (panel == nil)
		return nullptr;		// that panel has never been built

	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panel);
	if (!container.IsValid())
		return nullptr;

	HWND h = container.GetOWLControl();
	return (h != nullptr && ::IsWindow(h)) ? h : nullptr;
}

// Remember the OWL.Palette once it is found. Its HWND survives both closing and reopening a panel
//   and docking and undocking it (what changes is the parent OWL.Dock, and only when going from
//   docked to floating -- see the head of this file), so a cache is worth having.
//   WHY: the kPaletteVisibilityChangedMessage subscribed to below arrives SEVERAL TIMES for
//   opening a single document (measured). Asking the SDK on each of those is waste, and the Win32
//   hook below runs on every pointer movement -- SO THE NUMBER OF TIMES A WIN32 CALLBACK REACHES
//   INTO THE MODEL SHOULD BE KEPT AS LOW AS POSSIBLE. (KBS goes further for the same reason and
//   caches a negative too: KBSQueryFindChangeWindow's sFcLookedUp means "looked and found none;
//   do not look again until the window list changes".)
static HWND sPaletteWnd[kKCMAlphaCount] = { nullptr, nullptr, nullptr };

// What each target is aimed at. KEEP THIS IN THE SAME ORDER AS THE enum.
//  Held as a raw uint32 rather than a WidgetID: DECLARE_PMID produces a uint32, which can be used
//  in a static initialiser.
//  @warning THE BOOK DIALOG CANNOT BE LOOKED UP BY WidgetID. The panel manager knows about panels,
//    and a dialog is not one of them -- its window is handed over by KCMSetBookDialogWindow. The 0
//    is there to keep the table lined up and is never used.
static const uint32 kKCMAlphaWidgetIDs[kKCMAlphaCount] =
{
	kKCMPanelWidgetID,		// kKCMAlphaSelf = our own panel
	kPagesPanelWidgetID,		// kKCMAlphaPages = the application's Pages panel
	0							// kKCMAlphaBookDialog = never looked up (see above)
};

// The window title recorded when the dialog's window was registered, for checking against.
//   WHY IT IS NEEDED: THE OS HANDS AN HWND VALUE OUT TO ANOTHER WINDOW LATER. The dialog's window
//     is destroyed when it closes (IDialogMgr: "the dialog will take care of destructing itself
//     when the dialog is closed") while this side goes on holding the handle, so from the moment
//     it is recycled IsWindow answers truthfully -- about somebody else's window. Carrying on from
//     there means PUTTING WS_EX_LAYERED ON AN UNRELATED WINDOW AND WRITING 77 TO IT: the same
//     accident as "making another panel translucent", which really happened once with panels.
//   A panel proves the same thing with its class name ("OWL.Palette"). A dialog's class name is a
//     generic one the application uses everywhere and proves nothing, SO THE SPELLING RECORDED AT
//     REGISTRATION IS MATCHED INSTEAD (our dialog's title is a fixed string -- the one in the
//     .fr).
static wchar_t sBookDialogTitle[128] = { 0 };

// Is the window's title the one recorded? WITH NOTHING RECORDED THIS RETURNS kTrue -- there is
//   nothing to check against, so it falls back to IsWindow alone, as before. The record is only
//   missing when GetWindowTextW failed, and rejecting on that would mean our own dialog could
//   never be made translucent again.
static bool KCMTitleMatchesBookDialog(HWND h)
{
	if (h == nullptr)
		return false;
	if (sBookDialogTitle[0] == L'\0')
		return true;			// nothing recorded: no check (the earlier behaviour)
	wchar_t title[128] = { 0 };
	::GetWindowTextW(h, title, 128);
	return ::wcscmp(title, sBookDialogTitle) == 0;
}

// The panel window, from the cache where possible. THE OS RECYCLES HANDLES, so the class name is
//   checked as well as whether the window is alive -- which is still cheaper than asking the SDK
//   again.
//   (The older code checked the window title here too, to see whether it really was our panel.
//   Now that the lookup aims at a WidgetID, there is no spelling left to match.)
//
//   WHY THE CLASS NAME IS ENOUGH. This used to say that a window being rebuilt, or a handle being
//     recycled, always comes with either a kPaletteVisibilityChangedMessage or a Win32 hook event,
//     and that the cache is dropped at one of those. THE FIRST HALF IS NOT TRUE: nothing drops the
//     cache on a visibility notification (the Update below only re-applies, and returns at once
//     when everything is off). The only place that drops it is the Win32 hook, AND THAT HOOK IS
//     ONLY INSTALLED WHILE A TOGGLE IS ON.
//   The two checks here (alive + class name) cannot tell apart "destroyed, and the OS handed that
//     handle to ANOTHER PANEL'S OWL.Palette" -- both of those are a live OWL.Palette.
//     KBS therefore drops its cache unconditionally whenever a visibility notification arrives
//     (KBSForgetPaletteWindow), giving "a workspace change rebuilds the palettes" as the reason.
//   THAT WAS NOT COPIED HERE BECAUSE THE PREMISE DID NOT HOLD WHEN MEASURED (21.0.2.2, with a
//     temporary diagnostic build printing the cached HWND next to the one IPanelMgr gives now):
//       - closing and reopening our own panel                          -> unchanged
//       - switching workspaces (Essentials -> Advanced -> Essentials)  -> unchanged
//       - resetting the workspace                                      -> unchanged
//       - closing the application's Pages panel -> its OWL.Palette stayed ALIVE, title and all
//     => OWL.Palette WINDOWS ARE NOT DESTROYED DURING A SESSION. That is the same fact the head of
//     this file states from the other side ("55-56 pairs sit invisible from startup"), seen from
//     destruction rather than creation. With no recycling, there is no need for a place to drop it.
//   @warning IF A PATH IS EVER FOUND THAT DOES REBUILD A PALETTE, add the drop in the same place
//     KBS has it -- right after the visibility notification, before the toggles are looked at.
//     BUT IT CANNOT BE COPIED WHOLESALE HERE: the dialog's window cannot be looked up again (the
//     dialog only hands it over), so the only targets safe to drop are the ones the panel manager
//     can be asked about -- those with kKCMAlphaWidgetIDs[i] != 0.
static HWND KCMQueryPaletteWindow(int32 which)
{
	// THE DIALOG NAMES ITSELF. It is not on the panel manager, so the SDK route below cannot be
	// used; KCMBookDialog.cpp hands the window over as soon as it has one. All that is done here is
	// to ask whether that window is still alive and still THE one -- if either has gone, forget it
	// (the next open hands it over again; with no way to look it up, forgetting fails safe, the
	// same as being off).
	if (which == kKCMAlphaBookDialog)
	{
		if (sPaletteWnd[which] != nullptr &&
			(!::IsWindow(sPaletteWnd[which]) || !KCMTitleMatchesBookDialog(sPaletteWnd[which])))
		{
			sPaletteWnd[which] = nullptr;
		}
		return sPaletteWnd[which];
	}

	HWND cached = sPaletteWnd[which];
	if (cached != nullptr && ::IsWindow(cached) && KCMClassIs(cached, L"OWL.Palette"))
		return cached;

	sPaletteWnd[which] = KCMQueryPanelPaletteFromSDK(kKCMAlphaWidgetIDs[which]);
	return sPaletteWnd[which];
}

// Is the window's class name the expected one?
static bool KCMClassIs(HWND h, const wchar_t* wanted)
{
	if (h == nullptr)
		return false;
	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(h, cls, 64) == 0)
		return false;
	return ::wcscmp(cls, wanted) == 0;
}

// The top-level window the panel is in RIGHT NOW, but only when it is one that can be made
// translucent on its own. nullptr while it is expanded inside a dock (GA_ROOT being the main
// frame).
//
// GA_ROOT HAS THREE OUTCOMES (measured on the Pages panel in all three states; the OWL.Palette
// HWND is the same throughout):
//     "indesign"        expanded inside a dock, attached to the main window. EXSTYLE=0x00000100,
//                       i.e. no WS_EX_LAYERED -- and setting it would take the whole application
//                       with it, so it cannot be controlled on its own
//     "OWL.Dock"        floating. EXSTYLE=0x08080000
//     "OWL.FrameDrawer" the drawer that opens when its icon is clicked. EXSTYLE=0x08080000
//   InDesign itself sets WS_EX_LAYERED on the last two, so they are treated identically.
// @warning testing for "OWL.Dock" alone silently leaves the drawer out (which it did, until this
//   was measured).
//
// CAN THIS THREE-WAY TEST BE MOVED ONTO THE SDK (PaletteRefUtils)? MEASURED, AND THE ANSWER IS NO.
//   The motive was sound: class-name strings are undocumented OWL internals, and the window lookup
//   was moved onto WidgetIDs for the very same reason (titles are translated). There IS an official
//   way to ask whether a palette is floating, AND THIS PLUG-IN ITSELF USES IT ELSEWHERE
//   (PaletteRefUtils::IsPaletteFloating, in KCMStorySection.cpp; the shipping
//   linksui/LinksUIUtils.cpp uses it too).
//   => BOTH CANDIDATES FAILED WHEN MEASURED (moving a panel between floating, in-dock and drawer):
//     1. IsPaletteFloating RETURNED BOTH 0 AND 1 FOR A DRAWER -- 0 for a drawer opened from an
//        edge dock that had been iconised, 1 for one opened from an iconised floating dock
//        (a flotilla). What that API answers is "IS THE DOCK IT BELONGS TO FLOATING", not the
//        question this code has, which is "DOES THIS PANEL HAVE A TOP-LEVEL WINDOW THAT CAN BE
//        MADE TRANSLUCENT ON ITS OWN".
//     2. THE "OWL.FrameDrawer" HWND NEVER APPEARS IN THE PALETTE HIERARCHY AT ALL (measured;
//        walking to the root does not produce it, and the Dock names a different HWND). That is
//        the measured form of there being no PaletteRefType (there are twelve enumerators) for a
//        drawer. => THE SDK CANNOT RETURN THE VERY WINDOW THAT HAS TO BE WRITTEN TO.
//   THEREFORE GA_ROOT PLUS A CLASS-NAME MATCH IS THE ONLY THING THAT SEPARATES THESE THREE STATES
//   IN ONE PIECE. @warning TO WHOEVER AUDITS THIS NEXT: this looks like a leftover Win32 string
//   comparison, but it is here because the alternative was measured.
//   Two useful checks came out of the same measurement: THE OWL.Palette HWND WAS UNCHANGED across
//   all three states and six transitions (which is what the cache design rests on), and THE Dock
//   HWND DID CHANGE on being floated again (as the head of this file says). While floating -- and
//   only then -- the Dock's GetOWLControl() did equal GA_ROOT, so state 1 alone COULD be had from
//   the SDK; it is not taken, because states 2 and 3 cannot be had the same way.
static HWND KCMQueryTranslucentTarget(int32 which, HWND palette)
{
	if (palette == nullptr)
		return nullptr;

	// A DIALOG IS ITSELF A TOP-LEVEL WINDOW, so everything this function does for a panel --
	// walking GA_ROOT to find which dock it is in right now -- is simply not needed.
	// A side effect is that the panel's restriction, that it cannot be made translucent while
	// expanded inside a dock, does not apply either: a dialog always floats, so pressing the menu
	// always takes effect.
	if (which == kKCMAlphaBookDialog)
		return palette;

	HWND root = ::GetAncestor(palette, GA_ROOT);
	if (root == nullptr || root == palette)
		return nullptr;

	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(root, cls, 64) == 0)
		return nullptr;

	if (::wcscmp(cls, L"OWL.Dock") == 0 || ::wcscmp(cls, L"OWL.FrameDrawer") == 0)
		return root;

	return nullptr;		// "indesign" = the main frame = expanded inside a dock
}

#endif // WINDOWS

// Apply to one named target. THIS IS THE ONLY PLACE THAT WRITES AN ALPHA; the public functions
// below are thin wrappers.
static bool16 KCMApplyFor(int32 which)
{
#ifdef WINDOWS
	HWND palette = KCMQueryPaletteWindow(which);
	HWND target  = KCMQueryTranslucentTarget(which, palette);
	if (target == nullptr)
		return kFalse;		// no panel / expanded inside a dock / the dialog is closed -- do nothing

	// Opaque again while the pointer is over it (KCMEffectiveAlpha measures where it is now). The
	// tab strip and the title strip count as "over it" too, because the target window contains
	// them.
	const BYTE alpha = KCMEffectiveAlpha(which, target);

	// THE DIALOG NEEDS WS_EX_LAYERED SET BY US. A panel does not, because InDesign has already set
	// it on OWL.Dock / OWL.FrameDrawer -- that is a property of those windows, not of windows in
	// general. The dialog's window does not have it: measured, SetLayeredWindowAttributes failed
	// and GetLayeredWindowAttributes reported "not layered" (the first wall this feature hit).
	// @warning IT IS NOT REMOVED WHEN SWITCHING OFF: an alpha of 255 restores the look anyway, and
	//   removing and re-adding it invites a switch between per-pixel and uniform alpha drawing
	//   (the trap already hit with the shadow -- see the comment below).
	// ADDING A STYLE NEEDS NO SetWindowPos (measured by KBS and settled there). MSDN's SetWindowPos
	//   Remarks says that *certain window data* changed with SetWindowLong does not take effect
	//   until SetWindowPos(SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED) is called, BUT IT
	//   DOES NOT SAY WHICH data -- so it has to be measured, and adding WS_EX_LAYERED took effect
	//   without it (the SetLayeredWindowAttributes right after commits it).
	//   @warning REMOVING one is a different matter: the window has to be told the style is gone so
	//     that it redraws, which needs SetWindowPos + RedrawWindow. THE ASYMMETRY HAS A REASON.
	//     This function never removes one, so it needs neither. (KCMBookDialog.cpp, which changes
	//     the same dialog's frame style, is on the removing side and does use the four flags.)
	if (which == kKCMAlphaBookDialog)
	{
		const LONG_PTR ex = ::GetWindowLongPtr(target, GWL_EXSTYLE);
		if ((ex & WS_EX_LAYERED) == 0)
		{
			// DO NOT SET THE STYLE ON A WINDOW THAT IS NOT YET LAYERED JUST TO WRITE 255 TO IT. The
			// caller (KCMBookDialog.cpp) comes through here UNCONDITIONALLY on every open.
			// That unconditional call is right: THE TOGGLE MAY HAVE BEEN TURNED ON WHILE THE DIALOG
			//   WAS CLOSED, and a reopened window always starts opaque, so 77 has to be written
			//   again every time (measured, and it passes).
			// But in the other direction -- opened with the toggle off -- there is no value to
			//   restore: THE WINDOW IS NEW EVERY TIME (measured: three opens, three different
			//   HWNDs, not layered right after each reopen; what a cached dialog keeps is its
			//   contents, not the OS window). Setting WS_EX_LAYERED on a window the application
			//   built, and leaving it there, just to write 255, also goes against this feature only
			//   running when it is on (the user's decision).
			// WHAT IS TESTED IS NOT THE TOGGLE BUT "IS THIS WINDOW LAYERED" AND "IS 255 WHAT WE
			//   WANT" -- which is why this is not an argument for putting an off-guard in
			//   KCMApplyFor itself (that would kill the 255 restore path above).
			// It returns kTrue: the alpha did reach the window. This is also where a toggle that is
			//   ON with the pointer over the dialog ends up (255 being what is wanted), and the
			//   panels return kTrue in that case -- SO THE MENU IS WORDED THE SAME WAY FOR BOTH
			//   (kFalse is reserved for "the dialog is closed").
			if (alpha == 255)
				return kTrue;

			::SetWindowLongPtr(target, GWL_EXSTYLE, ex | WS_EX_LAYERED);
		}
	}

	// For a panel, InDesign set WS_EX_LAYERED long ago, so it is left alone (see the dialog branch
	// above).
	const BOOL ok = ::SetLayeredWindowAttributes(target, 0, alpha, LWA_ALPHA);

	// THE SHADOW (OWL.ShadowView) IS HANDLED ALONGSIDE. The shadow is a separate top-level window,
	// the Dock's owner, so making only the Dock translucent leaves the shadow opaque and that part
	// looks noticeably darker (reported: "it looks fine while dragging, but the shadow is dark once
	// you let go" -- no shadow is drawn while dragging, and it appears the moment the move is
	// committed). While translucent, the shadow is hidden along with it.
	//
	// DO NOT USE SetLayeredWindowAttributes ON THE SHADOW (established by breaking it on a live
	//   build). The shadow is drawn with per-pixel alpha through UpdateLayeredWindow (that is what
	//   makes it soft). In Win32, uniform alpha and per-pixel alpha are mutually exclusive: once
	//   the former has been set, going back to 255 does not restore per-pixel drawing, and turning
	//   the feature off leaves the shadow an unnatural block. Showing and hiding it touches no
	//   drawing mode and is safe.
	//   MSDN's Remarks bear the measurement out, and "IT DOES NOT COME BACK" is better put as "IT
	//   IS ONE-WAY FOR US": "once SetLayeredWindowAttributes has been called for a layered window,
	//   subsequent UpdateLayeredWindow calls will fail **until the layering style bit is cleared
	//   and set again**" -- so there IS an official way back (clear WS_EX_LAYERED and set it
	//   again).
	//   @warning THE DECISION IS UNCHANGED: the window belongs to InDesign, and the head of this
	//     file says that removing that style breaks the application's own drawing. That road is not
	//     taken. (A case of a measurement later being confirmed by the documented contract.)
	//   SW_SHOWNA shows it without activating: the shadow window is WS_EX_NOACTIVATE and must not
	//     be brought to the front.
	//   When a drawer's ("OWL.FrameDrawer") owner is not a ShadowView, the test below simply passes
	//     it by.
	//
	// SHOWING AND HIDING THE SHADOW IS DECIDED BY THE TOGGLE ALONE, NOT BY THE ALPHA. While the
	//   toggle is on, the shadow stays hidden even when the pointer has made the panel opaque.
	//   @warning WHY (a reported defect): a panel is dragged by its tab or title strip, and the
	//     pointer IS on the panel there, so deciding by the alpha would SHOW THE SHADOW MID-DRAG.
	//     InDesign does not draw a shadow while dragging (it draws one when the move is committed),
	//     so forcing one out leaves A SHADOW AT THE OLD POSITION THAT NEVER MOVES.
	//   A second benefit: the shadow no longer flickers on and off as the pointer crosses the
	//     panel's edge.
	const bool16 hideShadow = sTranslucentOn[which];
	HWND shadow = ::GetWindow(target, GW_OWNER);
	if (KCMClassIs(shadow, L"OWL.ShadowView"))
		::ShowWindow(shadow, hideShadow ? SW_HIDE : SW_SHOWNA);

	return ok ? kTrue : kFalse;
#else
	(void)which;
	return kFalse;		// the Mac has no way to do this, so it always reports "not applied"
#endif
}

bool16 KCMApplyPanelTranslucency()
{
	return KCMApplyFor(kKCMAlphaSelf);
}

bool16 KCMApplyPagesPanelTranslucency()
{
	return KCMApplyFor(kKCMAlphaPages);
}

bool16 KCMGetBookDialogTranslucent()
{
	return sTranslucentOn[kKCMAlphaBookDialog];
}

void KCMSetBookDialogTranslucent(bool16 on)
{
	KCMSetTranslucentFor(kKCMAlphaBookDialog, on);
}

bool16 KCMApplyBookDialogTranslucency()
{
	return KCMApplyFor(kKCMAlphaBookDialog);
}

// The dialog hands its window over. Called by KCMBookDialog.cpp as soon as it has one.
//  There is no "let go" call (nothing ever passes nil). Why nothing is needed on close is on the
//    declaration in the header, next to sBookDialogTitle.
//  NO ALPHA IS WRITTEN HERE: "remember the window" and "apply the current state" are separate
//    jobs, and the caller does the second one itself by calling KCMApplyBookDialogTranslucency
//    next (with the toggle off, nothing happening is the right outcome).
void KCMSetBookDialogWindow(void* sysWindow)
{
#ifdef WINDOWS
	HWND hwnd = static_cast<HWND>(sysWindow);
	sPaletteWnd[kKCMAlphaBookDialog] = hwnd;

	// The window's title is recorded at the same time. Every later check is a match against this
	// spelling and nothing else, SO NOTHING IS EVER WRITTEN TO ANY WINDOW BUT THE ONE HANDED OVER.
	// The reason is on the declaration of sBookDialogTitle.
	sBookDialogTitle[0] = L'\0';
	if (hwnd != nullptr && ::IsWindow(hwnd))
		::GetWindowTextW(hwnd, sBookDialogTitle, 128);
#else
	(void)sysWindow;
#endif
}

// Re-apply to every target. The notifications, the delayed re-apply and the Win32 hook all call
// this one, so that adding a target costs the code that has to follow along nothing.
//
// TARGETS THAT ARE OFF ARE SKIPPED. KCMApplyFor looks up a window even when a target is off, writes
//   alpha 255 and SW_SHOWNAs the shadow -- which never showed while there was a single target,
//   because the caller rejected "off" first, but started reaching the off targets once this became
//   "if any of them is on, do all of them".
//   @warning THE REAL DAMAGE SHOWS WHEN TWO TARGETS ARE PUT IN THE SAME FLOATING GROUP: they then
//     share one OWL.Dock as their GA_ROOT, so the moment after the on one writes 77, the off one
//     writes 255 over the same window and the translucency is cancelled out (and the shadow goes
//     SW_HIDE then SW_SHOWNA). Beyond that, it would show the off target's shadow uninvited -- and
//     catching that before a drag is committed is exactly the "shadow left behind at the old
//     position" that was fixed earlier.
//   WHY IT IS SAFE TO NARROW IT HERE: restoring 255 and bringing the shadow back at the moment a
//     toggle goes off is done by the flyout's own handlers, which apply their target by name.
//     Each of the translucency ActionIDs in KCMActionComponent.cpp -- for our panel, for the Pages
//     panel and for the book dialog -- calls that target's own Apply. This function exists to
//     RE-apply what is on, so it never needs to visit what is off.
//   @warning WHICH IS WHY KCMApplyFor ITSELF MUST NOT GET AN OFF-GUARD (it would kill that restore
//     path).
void KCMApplyAllPanelTranslucency()
{
	for (int32 i = 0; i < kKCMAlphaCount; ++i)
	{
		if (sTranslucentOn[i])
			KCMApplyFor(i);
	}
}

//========================================================================================
// The delayed re-apply -- not losing to the window being rebuilt
//
//   WHY IT IS NEEDED (measured): applying as soon as the observer below receives
//     kPaletteVisibilityChangedMessage is not enough, because InDesign can rebuild the top-level
//     window right after that, throwing the alpha away with it. The symptom is "going from the
//     icon back to floating comes out opaque -- but switching the menu off and on again works".
//     THE DIAGNOSTIC THAT SETTLED IT: reading back right after applying gave 128 and succeeded,
//     while measuring later from an external tool gave 255 -- and the HWND applied to was not the
//     window that existed at that point. So the value was not overwritten; THE WINDOW WAS
//     REPLACED.
//   THE REMEDY: as well as applying right after the notification, let the events go round once and
//     apply again to whatever GA_ROOT is by then. It chases the window for a few rounds while it
//     settles (kKCMPanelAlphaReapplyTries), and the count is what guarantees it stops.
//   @warning ICallbackTimer's callback is a raw function pointer that is NOT reference-counted, so
//     leaving a booking outstanding while this .pln goes down is a crash -- KCMShutdownPanelAlpha()
//     always stops and releases it.
//========================================================================================

#ifdef WINDOWS

static ICallbackTimer* sReapplyTimer = nil;
static int32           sReapplyLeft  = 0;			// rounds left (0 stops it; this is the runaway guard)
static bool            sPanelAlphaShutdown = false;	// KCMShutdownPanelAlpha has run (no timer is built after that)

static uint32 KCMReapplyTimerProc(void* refPtr);

// Called from wherever a notification arrives. Starts (or restarts) the chase until the window
// settles.
// THE "DO NOT STACK A BOOKING" GATE WAS TAKEN OUT (a self-review decision). Should the chain below
//   ever break part-way, that flag would stay raised and NEVER COME DOWN, leaving this function a
//   no-op for the rest of the session -- a failure mode where it can never be re-applied again.
//   ICallbackTimer holds one booking per instance, so calling StartTimer over a live booking only
//   replaces it. Re-arming unconditionally on every new notification works whatever the
//   implementation does underneath (and since the count goes back to kKCMPanelAlphaReapplyTries
//   each time, it debounces in effect).
static void KCMScheduleReapply()
{
	// NO RE-ARMING AFTER SHUTDOWN. A panel being destroyed during teardown sends the notification
	// (with the toggle still on) and arrives here. Building the timer again with CreateObject after
	// the tidy-up would recreate, from after the tidy-up, exactly the state the warning above
	// guards against: a raw function pointer booked while the .pln goes down.
	if (sPanelAlphaShutdown)
		return;

	sReapplyLeft = kKCMPanelAlphaReapplyTries;	// every notification puts the count back
	if (sReapplyLeft <= 0)
		return;		// a constant of 0 turns the delayed re-apply off altogether

	// The typed ::CreateObject2 is used rather than a C-style cast over ::CreateObject -- the form
	// CreateObject.h provides to return the face pointer already coerced to the right type, and the
	// one KCMThumbIdleTask.cpp uses elsewhere in this same plug-in.
	// @warning THE ONE-ARGUMENT FORM CreateObject2<ICallbackTimer>(kCallbackTimerBoss) CANNOT BE
	//   USED. It asks for FACE::kDefaultIID, and ICallbackTimer HAS NO kDefaultIID OF ITS OWN: it
	//   inherits its base IIdleTask's (IID_IIDLETASK). That would ask for IID_IIDLETASK and
	//   static_cast the result to ICallbackTimer* -- a broken type.
	//   THE TWO-ARGUMENT FORM, WITH THE IID SPELLED OUT, IS THE CORRECT ONE.
	if (sReapplyTimer == nil)
		sReapplyTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (sReapplyTimer == nil)
		return;

	sReapplyTimer->StartTimer(KCMReapplyTimerProc, kKCMPanelAlphaReapplyDelayMillis, nil);
}

static uint32 KCMReapplyTimerProc(void* /*refPtr*/)
{
	--sReapplyLeft;

	// DO NOT Release HERE: releasing itself while RunTask is running is self-destruction. Releasing
	// happens in one place, KCMShutdownPanelAlpha().
	// With more than one target, the chase stops when they are ALL off.
	if (!KCMAnyTranslucentOn())
	{
		sReapplyLeft = 0;		// everything is off: stop chasing
		return IIdleTask::kEndOfTime;
	}

	KCMApplyAllPanelTranslucency();

	// THE CHAIN IS DRIVEN BY THE RETURN VALUE. It used to call StartTimer again from in here and
	// then return kEndOfTime, which cancelled the very booking it had just made -- eight rounds
	// turned out to be two (measured). The return value IS the reschedule, so returning a delay is
	// how it carries on.
	// @warning ICallbackTimer's documented contract is one-shot ("register a one time only
	//   callback"), so this chain rides on an observation about the implementation. Even where it
	//   does not hold, the next notification re-arms KCMScheduleReapply unconditionally, so it can
	//   never end up never running again.
	if (sReapplyLeft > 0)
		return kKCMPanelAlphaReapplyDelayMillis;

	// The return value is IIdleTask::RunTask's reschedule. 0 MEANS "CALL ME AGAIN IMMEDIATELY", not
	// "stop" -- returning it by mistake locks InDesign up. To stop, return kEndOfTime.
	return IIdleTask::kEndOfTime;
}

//========================================================================================
// The Win32 event hook -- the only way to catch the transitions that send no SDK notification
//
//   ESTABLISHED BY MEASUREMENT (the same on both the Debug and the Release build):
//     - kPaletteVisibilityChangedMessage arrives, as its name says, only when VISIBILITY changes.
//       Opening, closing, iconising and opening a drawer all send it; TRANSITIONS THAT ONLY CHANGE
//       WHERE IT SITS (in-dock <-> floating, drawer -> floating) DO NOT.
//     - kDockedPaletteAreaChangedByUserMsg arrived in the 2025 release BUT NOT IN 2026 (and it
//       went to kAppBoss rather than kPanelManagerBoss).
//     - Riding a view recalculation (kFitInViewCmdBoss and the like) was considered too, but
//       pulling a panel out of a drawer does not change the dock's width, so nothing happens
//       there either.
//   -> The only handle left is the Win32 fact that OWL.Palette's parent is swapped.
//
//   Limited to our own process, with WINEVENT_OUTOFCONTEXT (no DLL injected into anyone else), so
//     the blast radius is closed.
//   Installed only while a toggle is on, and always removed on off and at shutdown (a missing
//     UnhookWinEvent is a resource leak).
//
//   @warning REENTRY IS WARNED ABOUT BY NAME IN THE DOCUMENTATION: "While a hook function processes
//     an event, additional events may be triggered, which may cause the hook function to
//     **reenter**". THIS FILE CAUSES ITS OWN REENTRY: the ShowWindow inside KCMApplyFor emits
//     EVENT_OBJECT_SHOW (0x8002) / HIDE (0x8003), both inside the 0x8002-0x800B range installed
//     below.
//     IT IS DELIBERATELY NOT "FIXED": this callback is idempotent -- it writes only when the state
//     differs from what is wanted (alphaOk && shadowOk continues at once) -- so the reentry
//     converges in one step. The harm the documentation warns of, events arriving out of order,
//     cannot appear because nothing here depends on their order.
//     => ADDING ANYTHING HERE THAT ACCUMULATES STATE BREAKS THAT PREMISE.
//========================================================================================

static HWINEVENTHOOK sWinEventHook = nullptr;

static void CALLBACK KCMWinEventProc(HWINEVENTHOOK /*hook*/, DWORD /*event*/, HWND /*hwnd*/,
									   LONG idObject, LONG idChild,
									   DWORD /*thread*/, DWORD /*time*/)
{
	// Only two kinds are of interest:
	//   1. events about a window itself (OBJID_WINDOW) = it was rebuilt or moved. The original
	//      purpose.
	//   2. the pointer moving (OBJID_CURSOR) = a signal that the mouse moved. Catching this is what
	//      makes it possible to go opaque again WHEN THE POINTER IS ON THE TAB STRIP OR THE TITLE
	//      STRIP, which are outside the widget tree and so out of the SDK's IMouseRollOver's reach.
	//      It adds neither a hook nor a periodic timer: it simply stops discarding something that
	//      already arrives here.
	//   @warning child elements (idChild != CHILDID_SELF) are rejected FOR WINDOW EVENTS ONLY. A
	//     cursor event can carry the cursor's state in idChild, and rejecting on that would lose
	//     them.
	const bool isWindowEvent = (idObject == OBJID_WINDOW && idChild == CHILDID_SELF);
	const bool isCursorEvent = (idObject == OBJID_CURSOR);
	if (!isWindowEvent && !isCursorEvent)
		return;

	// Each target is decided independently: one of them being docked must not stop the other from
	// being re-applied, hence the loop with continue.
	bool16 didApply = kFalse;

	for (int32 which = 0; which < kKCMAlphaCount; ++which)
	{
		// This feature only runs while a toggle is on. (With everything off the hook is not even
		// installed, but this also rejects anything still in flight as it is being removed.)
		if (!sTranslucentOn[which] || sPaletteWnd[which] == nullptr)
			continue;

		// EVEN A CACHED HANDLE IS CHECKED FOR STILL BEING THAT PANEL. The OS recycles HWNDs, so the
		// same value can be handed to another window after a panel is closed. Walking GA_ROOT
		// without checking would MAKE SOMEBODY ELSE'S PANEL TRANSLUCENT -- and since the hook is
		// not removed until the toggle goes off, events keep arriving here after a panel is closed
		// with it still on.
		// @warning THE SDK IS NOT ASKED AGAIN HERE: a stale entry is simply dropped and the loop
		//   moves on, leaving the lookup to the SDK notification or to Apply
		//   (KCMQueryPaletteWindow). This hook fires a great deal, so keeping each pass cheap comes
		//   first.
		if (!::IsWindow(sPaletteWnd[which]))
		{
			sPaletteWnd[which] = nullptr;	// stale (the panel was closed, say): the line above rejects it from now on
			continue;
		}

		// THE CLASS-NAME CHECK IS DONE FOR WINDOW EVENTS ONLY. Calling GetClassNameW on every pass
		// where the pointer merely moved is waste (those run 60-100 times a second).
		// @warning WHY THAT IS SAFE: for an HWND to be recycled onto another window, that window
		//   has to be created and SHOWN. Being shown is EVENT_OBJECT_SHOW, a window event, so the
		//   moment of any swap always passes through this check. A window that is never shown is
		//   rejected by KCMQueryTranslucentTarget as being neither "OWL.Dock" nor
		//   "OWL.FrameDrawer", so it never becomes a target either.
		//   (The older code checked the window title here as well, to see whether it was our panel
		//   by its spelling. Once the lookup aimed at a WidgetID there was no spelling left to
		//   match, so the class name is where it stops. Dropping it here means the next Apply looks
		//   the right window up from the SDK again.)
		// @warning A DIALOG CANNOT BE CHECKED BY CLASS NAME. Its window is not an "OWL.Palette",
		//   and its class name is a generic one the application uses everywhere -- putting it
		//   through here would drop it on every window event and the translucency would come
		//   undone at once. NOR CAN IT GO UNCHECKED: handles get recycled, so IsWindow alone could
		//   make somebody else's window translucent -- the same accident this line prevents for
		//   panels. So a dialog is matched against ITS RECORDED WINDOW TITLE
		//   (KCMTitleMatchesBookDialog), on window events only, at the same cost as a panel.
		// DOES NOT CHECKING ON CURSOR PASSES OPEN A ROUTE TO WRITING TO SOMEBODY ELSE'S WINDOW?
		//   No. All that is read below is an attribute (GetLayeredWindowAttributes); THE WRITING IS
		//   DONE BY KCMApplyFor, WHICH ALWAYS GOES THROUGH KCMQueryPaletteWindow and checks the
		//   class name (panel) or the title (dialog) again, right then. The check here only makes
		//   a stale entry go sooner -- the safety of the write does not rest on it.
		if (isWindowEvent)
		{
			const bool stillOurs = (which == kKCMAlphaBookDialog)
				? KCMTitleMatchesBookDialog(sPaletteWnd[which])
				: KCMClassIs(sPaletteWnd[which], L"OWL.Palette");
			if (!stillOurs)
			{
				sPaletteWnd[which] = nullptr;
				continue;
			}
		}

		// This once filtered on hwnd == sPaletteWnd, but NOT ONE PARENTCHANGE OR LOCATIONCHANGE
		// ADDRESSED TO AN OWL.Palette EVER ARRIVED (measured). The system does not always generate
		// those for a child window being moved.
		// -> So the sender is not looked at. When an event arrives, the target's current top-level
		//   window is looked up again and the alpha written if it has drifted. The test is a
		//   GetAncestor and an attribute read, and it continues at once when nothing has drifted,
		//   so the volume does no harm.
		HWND target = KCMQueryTranslucentTarget(which, sPaletteWnd[which]);
		if (target == nullptr)
			continue;				// expanded inside a dock: not a target

		// THIS RUNS MANY TIMES DURING A MOVE. Only go on to the real work WHEN THE STATE DIFFERS
		// FROM WHAT IS WANTED (measured: 1477 events produced exactly one write).

			// 1. is the alpha what is wanted? (with the pointer over it, opaque is what is wanted)
			// THE FAILURE ARM IS NOT BELT-AND-BRACES; IT IS THIS WINDOW'S DOCUMENTED STATE. MSDN's
			//   Remarks: "GetLayeredWindowAttributes can be called only if the application has
			//   previously called SetLayeredWindowAttributes on the window. The function will fail
			//   if the layered window was setup with UpdateLayeredWindow."
			//   For a panel, WS_EX_LAYERED was set by INDESIGN and not by us (see the head of this
			//   file) => UNTIL WE HAVE WRITTEN ONCE, THIS READ CAN LEGITIMATELY FAIL.
			// SO "CANNOT READ" MUST FALL ON THE SIDE OF "APPLY" and never be read as "already
			//   correct": the other way round leaves a rebuilt Dock opaque with the toggle on.
			// Once written, it reads back from then on -- so "one write in 1477 events" above is
			//   about the steady state.
		const BYTE want = KCMEffectiveAlpha(which, target);
		BYTE  cur = 0;
		DWORD key = 0, flags = 0;
		const bool16 alphaOk = (::GetLayeredWindowAttributes(target, &key, &cur, &flags) && cur == want) ? kTrue : kFalse;

			// 2. is the shadow (OWL.ShadowView) shown or hidden as wanted?
			//   Dragging a panel makes InDesign put the shadow back. Watching only the alpha leaves
			//   the state where THE SHADOW ALONE HAS RETURNED and that part looks dark (found on a
			//   live build).
		bool16 shadowOk = kTrue;
		HWND   shadow   = ::GetWindow(target, GW_OWNER);
		if (KCMClassIs(shadow, L"OWL.ShadowView"))
		{
			const bool16 visible = ::IsWindowVisible(shadow) ? kTrue : kFalse;
				// What is wanted is decided by THE TOGGLE, not by the alpha -- KEEP THIS IN STEP
				// WITH THE APPLYING SIDE ABOVE. Only targets that are on reach this point, so what
				// is wanted here is always "hidden".
			shadowOk = (visible == kFalse);
		}

		if (alphaOk && shadowOk)
			continue;				// both as wanted: nothing to do

		KCMApplyFor(which);
		didApply = kTrue;
	}

	// THE DELAYED RE-APPLY IS FOR WINDOW EVENTS ONLY. It exists to chase a window that gets rebuilt
	// with the alpha thrown away, so running it on a pass where only the pointer moved is pure
	// waste -- and since the pointer moves constantly, it would mean re-arming the whole chain over
	// and over.
	// One booking covers every target, because the timer re-applies to all of them.
	if (isWindowEvent && didApply)
		KCMScheduleReapply();
}

static void KCMInstallWinEventHook()
{
	// NOT INSTALLED AGAIN AFTER SHUTDOWN -- the same reason and the same shape as KCMScheduleReapply
	// above, which is where this was missing. KCMSetTranslucentFor installs "when any of them is
	// on", so a path that touches a toggle after KCMShutdownPanelAlpha has removed the hook would
	// leave THE OS HOLDING KCMWinEventProc, A RAW UNCOUNTED FUNCTION POINTER, WHILE THE .pln GOES
	// DOWN: precisely the state this file guards against several times over for ICallbackTimer.
	// @warning THE REMOVE SIDE IS DELIBERATELY NOT GUARDED. That direction takes things away, and
	//   blocking it would kill the restore path for switching a toggle off.
	// (Today's only caller is the menu being pressed, which never comes after Shutdown, so nothing
	//  has actually gone wrong. This is closing the asymmetry itself: two bookings of the same
	//  nature -- a timer and a hook -- of which only one was guarded.)
	if (sPanelAlphaShutdown)
		return;

	if (sWinEventHook != nullptr)
		return;		// already installed

	// The range is SHOW (0x8002) through LOCATIONCHANGE (0x800B). PARENTCHANGE (0x800F) alone was
	// installed at first AND NEVER FIRED ONCE (measured): OWL rearranges its windows by some means
	// other than SetParent. A panel does move, so LOCATIONCHANGE is certain to arrive, and the
	// range is taken out that far.
	// @warning this range fires a great deal for other windows too. The callback narrows it in two
	//   stages: it must be one of our panel windows, AND the alpha must differ from what is
	//   wanted.
	sWinEventHook = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE,
									  nullptr,						// no hook DLL (a function inside our own process)
									  KCMWinEventProc,
									  ::GetCurrentProcessId(), 0,	// our own process, every thread of it
									  WINEVENT_OUTOFCONTEXT);		// nothing injected
}

// FORGET THE HANDLE ONLY WHEN THE HOOK REALLY CAME OFF (ported from the shape KBS settled on).
//   It used to assign nullptr without looking at the return value. MSDN gives three ways
//     UnhookWinEvent can fail -- an invalid handle, one already removed, and BEING CALLED FROM A
//     THREAD OTHER THAN THE ONE THAT INSTALLED IT -- and in the third case THE HOOK IS STILL ALIVE.
//     Throwing the handle away then leaves no way to ever remove it: KCMShutdownPanelAlpha sees
//     nullptr, concludes it is done, and THE OS IS LEFT HOLDING KCMWinEventProc, A RAW UNCOUNTED
//     FUNCTION POINTER, WHILE THE .pln GOES DOWN -- the very state this file guards against twice
//     over for ICallbackTimer.
//   @warning "KEEPING THE HANDLE HAS NO SIDE EFFECT" WAS TOO STRONG. KCMInstallWinEventHook returns
//     at once on sWinEventHook != nullptr, so IF THE HOOK IS IN FACT GONE AND ONLY THE HANDLE
//     REMAINS, INSTALLING AGAIN IS A NO-OP FOR THE REST OF THE SESSION -- following a docking
//     change dies silently. MSDN's first two failure conditions are exactly that case.
//   THIS SHAPE (look at the return value; keep the handle on failure) is taken anyway BECAUSE THE
//     ONLY FAILURE THAT CAN REALISTICALLY HAPPEN IS THE THIRD ONE, where the hook IS alive, and
//     then Install being a no-op is the correct behaviour. The callers today are the menu being
//     pressed and Shutdown, both on the main thread.
//     => IF A PATH EVER CALLS THIS FROM ANOTHER THREAD, REVISIT IT.
//   No failure has ever actually happened; the point is not to swallow one.
static void KCMRemoveWinEventHook()
{
	if (sWinEventHook != nullptr)
	{
		if (::UnhookWinEvent(sWinEventHook))
			sWinEventHook = nullptr;
	}
}

void KCMShutdownPanelAlpha()
{
	// The backstop at plug-in shutdown. Nothing is dereferenced: it only stops and releases, which
	// is safe even during teardown.
	sPanelAlphaShutdown = true;		// from here on KCMScheduleReapply builds no timer (no re-arming)
	KCMRemoveWinEventHook();		// leaving the hook installed while the .pln goes down is dangerous

	for (int32 i = 0; i < kKCMAlphaCount; ++i)
		sPaletteWnd[i] = nullptr;	// let the remembered HWNDs go too (never hold a value the OS recycles)

	if (sReapplyTimer != nil)
	{
		sReapplyTimer->StopTimer();
		sReapplyTimer->Release();
		sReapplyTimer = nil;
	}
	sReapplyLeft = 0;
}

#else	// the Mac applies nothing, so it needs neither a booking nor a tidy-up

static void KCMScheduleReapply() {}
void        KCMShutdownPanelAlpha() {}

#endif // WINDOWS

//========================================================================================
// Following a panel being opened, closed or (un)docked
//
//   The translucency is put on THE TOP-LEVEL WINDOW OF THE MOMENT, so it is lost when that window
//     is rebuilt -- which happens on: (a) closing and reopening a panel, (b) docking or undocking
//     it, (c) pulling it out of its icon into a floating window, (d) clicking its icon to open the
//     drawer.
//     ALL FOUR ARRIVE AS THE SINGLE NOTIFICATION BELOW (measured with the Spy), so while a toggle
//     is on they are re-applied automatically.
//
//   WHICH NOTIFICATION IT IS was measured with the Debug build's Spy:
//       kPaletteVisibilityChangedMessage @ kPanelManagerBoss (IID_IPANELMGR)
//     It goes out on every docking change. The application's own listeners
//     (kLibraryPanelWindowObserverBoss / kBookPaletteWindowObserverBoss) take it on a plain
//     IID_IOBSERVER as well.
//     FOR EVERY ONE OF (a) TO (d), THE ORDER IS THE SAME (measured): the widgets are rebuilt (the
//       observers re-attach) and THEN this message arrives. So by the time Update runs the widgets
//       are already rebuilt, and it is safe to apply there.
//     @warning kDockedPaletteAreaChangedMsg, considered first, never arrived at all.
//     @warning kPanelChangedMessage (widgetid.h) is a different thing: CPanelControlData sends it
//       for "the child widget layout changed", which has nothing to do with a palette's
//       visibility (measured: it does not even arrive when a panel is opened or closed).
//     @warning THERE IS NO NOTIFICATION MEANING "A PANEL OPENED". Nothing mirrors the
//       kAboutToClosePaletteMsg sent just before closing; opening, docking and coming back from an
//       icon are all rolled into this one message.
//
//   The observer implementation is AddIn'd onto kActiveContextBoss in the .fr, alongside the layout
//     synchronisation observer and the batch-close observer -- the same proven arrangement.
//   IT IS DETACHED AT SHUTDOWN, by KCMDetachPanelVisibilityObserver, called BEFORE
//     KCMShutdownPanelAlpha (the full reason is on that function). NOT DETACHING IS THE MORE
//     DANGEROUS OPTION: while the subscription stands, what the session holds is a pointer INTO
//     THIS .pln, and destroying a panel during teardown really does send the notification, so
//     Update would run in code that is on its way out. KBS came to the same conclusion; this is
//     that fix, ported.
//     @warning THE OTHER TWO OBSERVERS (layout synchronisation and batch close) ARE STILL NOT
//       DETACHED. That is not two policies: this is the only one watching something that gets
//       destroyed part-way through teardown.
//========================================================================================

//========================================================================================
// Going opaque again while the pointer is over the panel (IMouseRollOver)
//
//   THE POINT: translucency is there so that what is underneath shows through and the panel is out
//     of the way -- but when it is to be read or used, opaque is better. The pointer arriving
//     lifts it; the pointer leaving puts it back.
//   HOW: IMouseRollOver (ui/IMouseRollOver.h) is the public interface for giving a widget
//     roll-over behaviour: MouseEnter / MouseOver / MouseLeave get called. It is AddIn'd as
//     IID_IMOUSEROLLOVER onto the panel boss (kKCMPanelWidgetBoss) in the .fr.
//   WHERE IT IS PUT: kKCMPanelWidgetBoss, the panel itself (derived from kPalettePanelWidgetBoss).
//     The Class block for kKCMPanelWidgetBoss in KCMUI.fr is what names it, and REACTING OVER THE
//     WHOLE PANEL AREA WAS CONFIRMED ON A LIVE BUILD (the record is in a comment in that same Class
//     block).
//     @warning FORGETTING THE FACTORY ENTRY (KCMUIFactoryList.h) MEANS IT IS SIMPLY NEVER CALLED,
//       WITH NO ERROR OF ANY KIND (CREATE_PMINTERFACE alone is not enough). If it stops working,
//       suspect that first.
//   FROM THE INVESTIGATION: going through a live boss registry dump (IObjectModel_RomanFS.txt) for
//     the bosses that really carry IID_IMOUSEROLLOVER, the application implements it on these
//     families (useful if this is ever moved elsewhere):
//       kRollOverIconButtonBoss and kin (icon buttons generally) -> kMouseRollOverImpl
//       kPanelWithRolloverWidgetBoss (the .fr type PanelWithRollOverWidget) -> kPanelMouseRollOverImpl
//       kClickableTextWidgetBoss and kin (link text) -> kHyperlinkRollOverImpl
//       kGIFPlayerWidgetBoss -> kGIFMouseRollOverImpl
//     => WHAT CALLS MouseEnter IS THE WIDGET'S OWN IMPLEMENTATION, so before moving this onto
//       another boss, check that there is something there to call it.
//   @warning ITS REACH ENDS AT THE PANEL'S OWN WIDGETS. It does not react over the title bar or the
//     tab strip (the OWL chrome), which do not pass mouse events to the app dispatcher.
//
//   WHETHER THE POINTER IS OVER IT IS NO LONGER HELD HERE: KCMCursorOverWindow() measures the
//     pointer's position every time it is called. Two reasons:
//       (a) as the warning above says, this is never called at all on the tab strip or the title
//           strip -- which is where the user actually reaches, so it cannot be left unreachable.
//           (A widget-tree dump on a live build settled that the chrome is outside the tree: the
//           parent chain ends at kOWLHostedPanelWrapperBoss.)
//       (b) MouseLeave has paths where it is missed, and with a flag "the pointer is on it" would
//           stick, breaking translucency for the rest of the session.
//     => This has been demoted to A SUPPLEMENTARY TRIGGER that only says "the mouse moved, apply
//       again". Holding no flag, missing an event on either side cannot leave the state wrong.
//========================================================================================

/** Re-applies the translucency as the pointer arrives on and leaves the panel. Holds no state of
    its own -- it is only a trigger. */
class KCMPanelRollOver : public CPMUnknown<IMouseRollOver>
{
public:
	KCMPanelRollOver(IPMUnknown* boss) : CPMUnknown<IMouseRollOver>(boss) {}
	~KCMPanelRollOver() {}

	virtual void	MouseEnter(const PMPoint& localMousePos);
	virtual void	MouseOver(const PMPoint& localMousePos);
	virtual void	MouseLeave();
	virtual bool8	IsMouseOver() const;
	virtual PMPoint	GetMouseOverPosition() const	{ return fLastPos; }

private:
	PMPoint	fLastPos;
};

CREATE_PMINTERFACE(KCMPanelRollOver, kKCMPanelRollOverImpl)

void KCMPanelRollOver::MouseEnter(const PMPoint& localMousePos)
{
	fLastPos = localMousePos;

	// Nothing to do while the toggle is off. This used to say "it gets rejected inside", AND IT
	// DID NOT: KCMApplyPanelTranslucency looks a window up even when off (asking the SDK again when
	// the cache is stale), writes alpha=255, and SW_SHOWNAs the shadow.
	// The judgement IsMouseOver() below had already made -- do not measure while off -- had simply
	// not reached this side (a disagreement inside one file). DO NOT MAKE PEOPLE WHO ARE NOT USING
	// THE FEATURE PAY FOR IT.
	// @warning restoring 255 at the moment the toggle goes off is guaranteed by the flyout's
	//   handler, which calls KCMApplyPanelTranslucency() explicitly
	//   (KCMActionComponent.cpp).
	if (!KCMGetPanelTranslucent())
		return;

	KCMApplyPanelTranslucency();		// measures, and goes opaque
}

void KCMPanelRollOver::MouseOver(const PMPoint& localMousePos)
{
	// Called on every movement. It only records the position and never writes to the window --
	// Enter has already made it opaque, so hammering SetLayeredWindowAttributes each time would
	// achieve nothing.
	fLastPos = localMousePos;
}

void KCMPanelRollOver::MouseLeave()
{
	if (!KCMGetPanelTranslucent())	// the same reason as MouseEnter above
		return;

	KCMApplyPanelTranslucency();		// measures, and goes back to translucent
}

bool8 KCMPanelRollOver::IsMouseOver() const
{
	// Holding no flag, this measures and answers on the spot.
	// @warning THE HEADER'S CONTRACT IS STRICTLY "as determined by the previous calls to
	//   MouseEnter/Over/Leave" (IMouseRollOver). Measuring gives a MORE accurate answer than that,
	//   but it is not the letter of the contract -- with the flag gone there is no other way to
	//   answer, and measuring is what a caller actually wants.
#ifdef WINDOWS
	// Do not measure while the toggle is off. This AddIn exists only for the translucency toggle,
	// and while it is off nobody uses this answer -- whereas KCMQueryPaletteWindow asks the SDK
	// (IPanelMgr) again whenever its cache is stale. DO NOT MAKE PEOPLE WHO ARE NOT USING THE
	// FEATURE PAY FOR IT (the same policy as the applying side, where KCMEffectiveAlpha does not
	// even look at the pointer while off).
	// This AddIn is on OUR OWN PANEL'S widget, so it only ever looks at our side. (Nothing can be
	// AddIn'd to the application's Pages panel; the pointer over that one is caught by the Win32
	// hook's OBJID_CURSOR.)
	if (!sTranslucentOn[kKCMAlphaSelf])
		return kFalse;
	return KCMCursorOverWindow(
		KCMQueryTranslucentTarget(kKCMAlphaSelf, KCMQueryPaletteWindow(kKCMAlphaSelf))) ? kTrue : kFalse;
#else
	return kFalse;
#endif
}

/** Re-applies the translucency when the panel's visibility changes. It subscribes to the panel
    manager's subject. */
class KCMPanelVisibilityObserver : public CObserver
{
public:
	KCMPanelVisibilityObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMPANELVISIBILITYOBSERVER) {}
	~KCMPanelVisibilityObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KCMPanelVisibilityObserver, kKCMPanelVisibilityObserverImpl)

void KCMPanelVisibilityObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	// TWO SUBJECTS ARE SUBSCRIBED TO (measured with the Debug build's Spy):
	//   1. kPanelManagerBoss / IID_IPANELMGR, kPaletteVisibilityChangedMessage
	//      = a panel opened or closed, came back from an icon, or opened its drawer. It arrives
	//      right after the widgets are rebuilt.
	//      As its name says, it is only about VISIBILITY -- transitions that change nothing but
	//      where it sits do not send it.
	//   2. kAppBoss / IID_IAPPLICATION, kDockedPaletteAreaChangedByUserMsg
	//      = dragging out of an in-dock expansion into a floating window.
	//      @warning IT ARRIVES IN THE 2025 RELEASE BUT NOT IN 2026 (measured), so nothing in 2026
	//        may depend on it. It is kept for running against 2025; in 2026 the Win32 hook above
	//        is what does this.
	//      @warning it goes to kAppBoss, NOT to kPanelManagerBoss. Believing for a long time that
	//        it never arrived came from subscribing to the wrong one.
	//   @warning kApplicationResumeMsg and kApplicationSuspendMsg come through kAppBoss /
	//     IID_IAPPLICATION as well, so theChange must always be checked.
	const bool16 isPaletteMsg = (protocol == IID_IPANELMGR    && theChange == kPaletteVisibilityChangedMessage);
	const bool16 isDockMsg    = (protocol == IID_IAPPLICATION && theChange == kDockedPaletteAreaChangedByUserMsg);
	// 3. the application went to the back (kApplicationSuspendMsg).
	//   @warning WITHOUT THIS: leaving the pointer on the tab strip or the title strip and moving
	//     the mouse out to another application means no more cursor events reach a hook limited to
	//     our own process, SO IT STAYS OPAQUE FOREVER. (Leaving the panel BODY is rescued by
	//     IMouseRollOver's MouseLeave, but the chrome is outside its reach in the first place.)
	//     Applying once here measures "not on it" and it goes translucent again.
	//   It writes a single alpha and touches neither the model nor the UI, SO IT IS SAFE TO CALL
	//     WHILE THE APPLICATION IS BEING DEACTIVATED (the guards collected in
	//     [[app-resume-and-safe-timing]] are for heavy automatic work and do not apply here).
	const bool16 isSuspendMsg = (protocol == IID_IAPPLICATION && theChange == kApplicationSuspendMsg);
	if (!isPaletteMsg && !isDockMsg && !isSuspendMsg)
		return;

	// Nothing to do while everything is off. This notification arrives SEVERAL TIMES for opening a
	// single document (measured), so people who are not using the feature are not made to run a
	// window lookup (re-applying is only needed while something is on).
	if (!KCMAnyTranslucentOn())
		return;

	KCMApplyAllPanelTranslucency();

	// The alpha written here is thrown away if InDesign rebuilds the window right afterwards
	// (measured). Let the events go round once and apply again to whatever window exists by then.
	// Going to the back (Suspend) changes no window, so it needs no chase.
	if (!isSuspendMsg)
		KCMScheduleReapply();
}

void KCMAttachPanelVisibilityObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;

	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKCMPANELVISIBILITYOBSERVER));
	if (obs == nil)
		return;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return;

	// The panel manager comes up part-way through the application's own startup sequence (there is
	// a kPanelMgrHasStartedMsg), so calling from a startup service can find it nil here.
	// GO ON TO THE SUBSCRIPTION BELOW EVEN WHEN IT IS (the same shape KBS settled on). Returning
	//   here would take the kAppBoss subscription down with it, LEAVING kApplicationSuspendMsg
	//   UNSUBSCRIBED -- and that is the only handle on "leave the pointer on the panel, switch to
	//   another application, and it stays opaque forever", which has nothing to do with the panel
	//   manager. One subject being absent is not a reason to give up on the other.
	//   (The palette subscription is picked up later, because the panel's AutoAttach
	//    (KCMPanelObserver.cpp) calls this again.)
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr != nil)
	{
		InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
		if (subject != nil &&
			!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKCMPANELVISIBILITYOBSERVER))
		{
			subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKCMPANELVISIBILITYOBSERVER);
		}
	}

	// The second subject: kAppBoss / IID_IAPPLICATION.
	//   "Expanded in a dock -> dragged out to floating" does not surface on the PanelMgr; in the
	//   2025 release it arrives here as kDockedPaletteAreaChangedByUserMsg. @warning IT DOES NOT
	//   ARRIVE IN 2026, where the Win32 hook is what does this. This is the fallback for running
	//   against 2025.
	InterfacePtr<ISubject> appSubject(app, IID_ISUBJECT);
	if (appSubject != nil &&
		!appSubject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKCMPANELVISIBILITYOBSERVER))
	{
		appSubject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKCMPANELVISIBILITYOBSERVER);
	}
}

// The mirror image of the above. Called at plug-in shutdown (KCMUIStartup::Shutdown), BEFORE
//   KCMShutdownPanelAlpha: stop the notifications first, then fold up the tools (the timer and the
//   Win32 hook).
//   WHY IT IS NEEDED: while the subscription stands, what the session holds is A POINTER INTO THIS
//     .pln. A panel destroyed part-way through teardown sends the notification, so Update would
//     run in code that is on its way out.
//     KBS added the same thing for the same reason (KBSDetachPanelVisibilityObserver) and it never
//     walked over here -- FIXES DO NOT WALK TO THEIR SIBLINGS BY THEMSELVES (this plug-in took the
//     ferror check and the re-arm guards from KBS; this is the same thing in the other direction).
//   Detach with THE SAME ATTACHMENT TYPE it was attached with: what went on as Regular comes off
//     as Regular.
//   IsAttached is asked before detaching for the same reason the attach side asks before
//     attaching: attach is called from two places (the startup service and the panel's
//     AutoAttach), so "is it really attached" is an honest question on both sides.
//   The panel manager can already be gone during teardown. Even when it is, the kAppBoss
//     subscription is independent and is not taken down with it (the same lesson the attach side
//     learned).
void KCMDetachPanelVisibilityObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;

	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKCMPANELVISIBILITYOBSERVER));
	if (obs == nil)
		return;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr != nil)
	{
		InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
		if (subject != nil &&
			subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKCMPANELVISIBILITYOBSERVER))
		{
			subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKCMPANELVISIBILITYOBSERVER);
		}
	}

	InterfacePtr<ISubject> appSubject(app, IID_ISUBJECT);
	if (appSubject != nil &&
		appSubject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKCMPANELVISIBILITYOBSERVER))
	{
		appSubject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKCMPANELVISIBILITYOBSERVER);
	}
}
