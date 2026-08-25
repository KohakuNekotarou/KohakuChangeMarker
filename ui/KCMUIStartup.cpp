//========================================================================================
//
//  KCMUIStartup.cpp
//
//  The UI half's startup / shutdown service. Its counterpart is KCMPeekStartup in KCMPeek.cpp,
//  and **the two together cover what used to be one**.
//
//  Everything here touches a widget, a window, a cursor or a subscription, so none of it could
//  live in the model plug-in.
//  ★★Put the other way round: **the original startup work was UI work in its entirety** --
//    separating the two left the model's Startup empty.
//
//  ⚠★★**THE ORDER MATTERS ON THE WAY OUT**: stop the subscriptions first, then take down what
//    they subscribe to. While an observer is attached, what the session holds is **a pointer
//    into this .pln**, and destroying the panel during teardown really does raise a
//    notification ---- that is, **Update runs inside code that is going away**. So
//    KCMDetachPanelVisibilityObserver() always comes before KCMShutdownPanelAlpha().
//    (Ported from KBS. Do not reorder.)
//
//  ⚠★**A consequence of there being two services**: **the relative order of the model half’s
//    Shutdown and this one is not guaranteed** (they are separate IStartupShutdownService
//    providers, and which the application calls first is undefined). Checked line by line at
//    the split: the UI's work (stopping subscriptions, returning fonts, dropping caches) and the
//    model's (emptying its containers) have no ordering between them. ★If either ever starts
//    reading the other’s state that premise breaks -- put them back into one service, or make
//    the order explicit.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CPMUnknown.h"
#include "IStartupShutdownService.h"

#include "KCMUIID.h"
#include "KCMPanelState.h"		// KCMLoadPanelStateIfPresent (restore the saved panel settings)
#include "KCMPanelTitle.h"		// KCMPanelTitle::Restore (put the tab name back on the way out)
#include "KCMPanelAlpha.h"		// subscribe / unsubscribe the translucency toggle, and its clean-up
#include "KCMTrackerHud.h"		// KCMTrackerHudShutdown (return the on-press HUD’s font)
#include "KCMPeekGesture.h"		// KCMAttachDocsClosedObserver / KCMPeekGestureShutdown
#include "KCMThumbIdleTask.h"		// KCMShutdownThumbIdleTask (release the deferred thumbnail idle task)
#include "KCMViewSync.h"			// KCMInvalidateSyncCaches / KCMViewSyncShutdown
#include "KCMCmykCursor.h"		// KCMCmykShutdown (the cursor strings and a font reference)
#include "KCMBookDialog.h"		// KCMBookDialogShutdown (the book comparison result: rows, two paths, summary)
#include "KCMUIShared.h"			// KCMAttachModelChangeObserver / KCMDetachModelChangeObserver
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// ClearSessionStatus (drop the remembered status line)
									// ★**The model side is what keeps it** so that app.kcmStatus can answer
									//   with the panel closed. A model-side free function cannot be linked
									//   from another .pln, which is why this goes through the facade.
									// ⚠★★**Dropping it is the MODEL half’s responsibility.** The older note
									//   here argued that the UI had to do it, because a model plug-in’s
									//   startup/shutdown service is called on **every background thread’s**
									//   startup and shutdown too (guide vol1-07 L245-253), so clearing it
									//   over there would wipe the status line on every PDF export.
									//   **That ground was closed off in the .fr**: kKCMPeekStartupBoss is
									//   declared kCMainThreadStartupShutdownProviderImpl, so the model’s
									//   Shutdown only ever runs on the main thread, and it now calls
									//   KCMClearSessionStatus() itself.
									//   ⇒ This call **stays** (Clear is idempotent) but it is neither the
									//     main one nor the only one.
									//   ⚠That is exactly why the nil check matters: during teardown kUtilsBoss
									//     can already be gone, so **this call may be skipped** -- and the
									//     model side closes it anyway.

class KCMUIStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KCMUIStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	~KCMUIStartup() {}

	virtual void Startup();
	virtual void Shutdown();
};

CREATE_PMINTERFACE(KCMUIStartup, kKCMUIStartupImpl)

void KCMUIStartup::Startup()
{
	// ★Read the saved panel settings (a private JSON) here, at startup, rather than when the panel
	// is first opened. Syncing runs even while stopped if its toggle is ON, so with the older
	// timing a user who had saved it ON lost the syncing between launch and opening the panel.
	// Reading at startup closes that window (with nothing saved, the defaults stand).
	// Every toggle restores into an engine-side flag or subscription and depends on neither the
	// panel nor a document, which is what makes startup a safe moment (KCMDoSetPrintMarks only
	// sets a flag with db=nil; ScrollMap / IgnoreMarker / Always Show Marks on Target and Source
	// are plain assignments).
	// An internal once-per-session guard keeps the existing call from the panel’s AutoAttach a
	// no-op -- insurance in case the order of the startup services ever changes.
	KCMLoadPanelStateIfPresent();

	// Subscribe to "a batch close has finished" (kPendingDocumentsClosedMsg). From here on, closing
	// several documents in a row folds the UI clean-up into one pass after the last of them
	// (KCMPeekGesture.cpp).
	KCMAttachDocsClosedObserver();

	// Subscribe to panel visibility changes (kPaletteVisibilityChangedMessage). With "Translucent
	// Panel" ON, the translucency survives reopening the panel and switching between docked and
	// floating -- the OWL.Dock window it is applied to is rebuilt each time. KCMPanelAlpha.cpp.
	KCMAttachPanelVisibilityObserver();

	// ★Subscribe to the model’s notifications (kKCM*Message). With this not connected, the model’s
	//   work never reaches the screen -- and it fails **with no error and no warning, as "nothing
	//   happens"**, so check it in the running application. KCMModelChangeObserver.cpp.
	KCMAttachModelChangeObserver();
}

void KCMUIStartup::Shutdown()
{
	// ★★Put the tab name back. **First of all**, while the UI is still standing (KBS puts
	//   KBSPanelTitle::Restore() at the head of its own Shutdown for the same reason).
	//   ⚠Without it the palette label persists in the workspace, so **a name with "- Pixel" on it
	//     stays there even after this plug-in is removed**.
	KCMPanelTitle::Restore();

	// release the deferred thumbnail idle task (RemoveTask first if one is queued)
	KCMShutdownThumbIdleTask();
	// drop the pending batch close as well: nothing can flush after this, but no state is left behind
	KCMPeekGestureShutdown();
	// ★Stop listening to the model as well. **Before the panel is taken down** -- the same reason
	//   as KCMDetachPanelVisibilityObserver below (Update must not run inside code that is going
	//   away).
	KCMDetachModelChangeObserver();
	// ★Stop the subscription first. While it is attached, what the session holds is **a pointer
	//   into this .pln**, and destroying the panel during teardown really does raise a
	//   notification ---- Update would run inside code that is going away. Only then take down the
	//   tools it uses (a timer and a Win32 hook) on the line below.
	//   ★Ported from the pair KBS introduced; KCM was the side that lacked it.
	KCMDetachPanelVisibilityObserver();
	// the delayed re-apply timer of the panel translucency goes the same way (again, leave no raw
	// function pointer behind)
	KCMShutdownPanelAlpha();
	// return the font reference the on-press HUD holds; the path where the application quits
	// mid-press is cleaned up too
	KCMTrackerHudShutdown();

	// the sync page-rectangle table, the correspondence table and the last state
	KCMInvalidateSyncCaches();
	// the rest of the layout view syncing (it only lowers a state flag; the reason is on the
	// implementation side, KCMViewSync.cpp)
	KCMViewSyncShutdown();

	// ★Empty the file-static PMStrings so that the static destructors at plug-in unload find
	// nothing to do. Windows has never shown a fault from leaving them, but the Mac unloads in a
	// different order, so no heap buffer is carried that far. This one is the CMYK side (the
	// cursor strings and the font / document pointer held during a press).
	KCMCmykShutdown();
	// ★★The four things the book comparison dialog keeps (the chapter rows, the Target and Source
	//   paths, the summary). **These were the UI-side statics missing from this list** -- the rows
	//   (`std::vector<KCMChapterResult>`) hold PMStrings, so a session that ran one comparison
	//   carried them to unload.
	//   ⚠They came to light **the day after the model half found two of the same shape missing from
	//     its own list** ＝ "fix one and do not look for its siblings".
	//   It touches no widget and no document, so it is safe wherever in the teardown it is reached.
	KCMBookDialogShutdown();
	// The remembered status line (kept on the model side) goes the same way. ⚠**With a nil check**:
	// during teardown kUtilsBoss can already be gone (the same reason as the nil check on
	// KCMCmykShutdown's EndColorDrag -- neighbouring lines of one shutdown, treated alike).
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare != nil)
		compare->ClearSessionStatus();
}

// End, KCMUIStartup.cpp.
