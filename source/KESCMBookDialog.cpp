//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: the dialog. See KESCMBookDialog.h for why it is modeless and fixed-size.
//
//  Shape copied from KESCL's Jump Offset dialog (KESCLActionComponent.cpp:384-417), which is the
//  closest working example in reach - same author, same stock kDialogBoss, also fixed-size. The
//  one thing changed is the dialog type: kModeless instead of kMovableModal.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IControlView.h"		// what FindWidget hands back - the line being written to
#include "IDialog.h"
#include "IDialogMgr.h"
#include "IPanelControlData.h"	// FindWidget - reaching the dialog's own widgets
#include "ISession.h"
#include "ITextControlData.h"	// SetString - the Target/Source lines and the status area
#include "IWindow.h"			// GetSysWindow - the platform window behind the dialog (minimize box)

// General includes:
#include "CDialogController.h"
#include "CoreResTypes.h"		// kViewRsrcType - the dialog's RsrcSpec
#include "LocaleSetting.h"		// the dialog's RsrcSpec locale
#include "RsrcSpec.h"

// Project includes:
#include "KESCMBookDialog.h"
#include "KESCMBookTree.h"		// KESCMBookTreeRebuild - the list, redrawn when the dialog opens
#include "KESCMID.h"

// *windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names.
//  (The same order KESCMPanelAlpha.cpp uses.)
#ifdef WINDOWS
#include <windows.h>
#endif

//----------------------------------------------------------------------------------------
// Filling the dialog in
//----------------------------------------------------------------------------------------

namespace
{

/** Everything the dialog shows, held for it. ★Module scope, not members of the dialog: the dialog
	window is created and destroyed as it is opened and closed, and results that survive only as
	long as the window would make closing it the same as discarding them.
	★The four are written together (KESCMBookDialogSetResult) because they describe ONE run - see
	  the header for why the paths are stored rather than resolved again at paint time. */
std::vector<KESCMChapterResult> gDialogRows;
PMString gTargetPath;
PMString gSourcePath;
PMString gSummary;

/** Put text into one of the dialog's static text widgets. Does nothing if it is not there. */
void SetLine(IPanelControlData* panelData, const WidgetID& widgetID, const PMString& text)
{
	if (panelData == nil)
		return;

	IControlView* view = panelData->FindWidget(widgetID);
	if (view == nil)
		return;

	InterfacePtr<ITextControlData> textData(view, UseDefaultIID());
	if (textData != nil)
		textData->SetString(text);		// SetString invalidates by itself - no Invalidate() here
}

/** Paint the three text lines from what the last run stored. Called when the dialog opens - which,
	since 2026-08-12, is the only time it can need painting: the dialog no longer runs anything, so
	nothing can change underneath it while it is open. */
void KESCMBookDialogPaintResult(IPanelControlData* panelData)
{
	if (panelData == nil)
		return;

	// ★The labels stay English (the plug-in's UI language rule); only the paths vary.
	PMString targetLine("Target: ");
	targetLine.Append(gTargetPath);
	targetLine.SetTranslatable(kFalse);

	PMString sourceLine("Source: ");
	sourceLine.Append(gSourcePath);
	sourceLine.SetTranslatable(kFalse);

	SetLine(panelData, kKESCMBookTargetTextWidgetID, targetLine);
	SetLine(panelData, kKESCMBookSourceTextWidgetID, sourceLine);

	// ★Only when there is one. The resource carries "Ready" for the case where the dialog is
	//   somehow opened before any run - overwriting it with an empty line would replace a sentence
	//   that explains itself with one that says nothing.
	if (!gSummary.IsEmpty())
		SetLine(panelData, kKESCMBookStatusTextWidgetID, gSummary);
}

}	// anonymous namespace

void KESCMBookDialogSetResult(const PMString& targetPath, const PMString& sourcePath,
                              const PMString& summary,
                              const std::vector<KESCMChapterResult>& rows)
{
	gTargetPath = targetPath;
	gSourcePath = sourcePath;
	gSummary = summary;
	gDialogRows = rows;
}

const std::vector<KESCMChapterResult>& KESCMBookDialogRows()
{
	return gDialogRows;
}

//----------------------------------------------------------------------------------------
// The dialog's controller
//----------------------------------------------------------------------------------------

/** The dialog's controller: it paints what the last run left behind, and applies nothing.

    ***** There is nothing to apply. ***** Since 2026-08-12 this dialog has no controls at all - the
    comparison is started from the flyout menu and confirmed in an alert (KESCMBookRun.cpp), and
    what is left here is a report. ApplyDialogFields is still overridden, and still calls its base,
    because the close box goes through the dialog's own machinery either way.

    It is declared BELOW the helpers above on purpose: InitializeDialogFields calls one of them, and
    a file-static has to be seen before it is used. */
class KESCMBookDialogController : public CDialogController
{
public:
	KESCMBookDialogController(IPMUnknown* boss) : CDialogController(boss) {}

	virtual void InitializeDialogFields(IActiveContext* context);
	virtual void ApplyDialogFields(IActiveContext* context, const WidgetID& widgetId);
};

CREATE_PMINTERFACE(KESCMBookDialogController, kKESCMBookDialogControllerImpl)

void KESCMBookDialogController::InitializeDialogFields(IActiveContext* context)
{
	CDialogController::InitializeDialogFields(context);

	InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());
	KESCMBookDialogPaintResult(panelData);

	// ★The list is rebuilt from the module's rows on every open, so that what is on screen is
	//   always what this plug-in actually holds. Measured 2026-08-11: kCacheDialog does keep the
	//   rows on screen across close and reopen, so this is not what makes reopening work - it is
	//   what keeps the two from ever disagreeing if the cached dialog is dropped. Rebuilding an
	//   already-correct list of a few rows costs nothing.
	KESCMBookTreeRebuild(panelData);
}

void KESCMBookDialogController::ApplyDialogFields(IActiveContext* context, const WidgetID& widgetId)
{
	CDialogController::ApplyDialogFields(context, widgetId);
}

namespace
{

/** Prepare the dialog's own platform window: give it a MINIMIZE BOX, and bring it back when it is
    sitting minimized.

    The minimize box is so that a comparison that took real time to compute can be pushed out of the
    way instead of closed. The restore is the other half of that, and is described at the bottom.

    *****WHY THIS NEEDS WIN32.***** The SDK cannot do it, and says so: a window's decorations are
    fixed when it is created (IWindow::InitWindow), a modeless dialog's standard controls are
    kCloseWindowControl ALONE (IWindow.h:125), and IWindow::SetWindowPolicy states that for an
    EXISTING window "only ... kSideTitlebarControl" can be changed (IWindow.h:405).

    *****IT TAKES TWO BITS, AND THE OBVIOUS ONE IS NOT ENOUGH.***** WS_MINIMIZEBOX on its own changes
    nothing that can be seen; ***taking WS_EX_TOOLWINDOW OFF is what makes the button appear***,
    because Windows does not draw minimize or maximize on a tool-window frame. WS_EX_APPWINDOW is
    added as well so the minimised dialog lands on the TASKBAR - without it Windows leaves a 160x28
    title-bar stub at the bottom left of the screen (the old owned-popup behaviour).
    All of it measured on the real application, 2026-08-12; the full record, including the three
    faults this cost to find, is memory/window-minimize-presentation-system.md.

    *****NOTHING IS PUT BACK, AND THAT IS THE POINT OF DOING IT HERE.***** KBS does the same thing to
    InDesign's OWN Find/Change dialog and has to hold a record of what it changed, undo it when its
    toggle goes off, and undo it again at shutdown - because that window belongs to somebody else.
    This one is OURS: it is created by KESCMOpenBookDialog and destroyed when the dialog closes, so
    there is no window to hand back and no state to keep. That is also why there is no menu toggle:
    a dialog of our own can simply have the button (the user's call, 2026-08-12).

    *****AND IT HAS TO BE RESTORED - A FAULT THE MINIMIZE BOX ITSELF INTRODUCED.***** Until there was
    a minimize box there was no way to leave this dialog in a state that reopening could not recover
    from. Now there is, and reopening does not recover from it: see the end of the function. */
void KESCMPrepareBookDialogWindow(IDialog* dialog)
{
#ifdef WINDOWS
	if (dialog == nil)
		return;

	// *The IWindow sits on the same boss as the dialog, so this is a plain QueryInterface - no
	//  walking of the application's window list, which is what KBS has to do to find a window that
	//  is not its own.
	InterfacePtr<IWindow> window(dialog, IID_IWINDOW);
	if (window == nil)
		return;

	HWND hwnd = (HWND)window->GetSysWindow();
	if (hwnd == nullptr || !::IsWindow(hwnd))
		return;		// no platform window yet - nothing to decorate

	// *Idempotent: the dialog is created with kCacheDialog, so reopening it can hand back a window
	//  that already carries these bits. Setting them again costs nothing.
	::SetWindowLongPtr(hwnd, GWL_STYLE,
		::GetWindowLongPtr(hwnd, GWL_STYLE) | WS_MINIMIZEBOX);
	::SetWindowLongPtr(hwnd, GWL_EXSTYLE,
		(::GetWindowLongPtr(hwnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);

	// *SWP_NOACTIVATE is ours, so re-framing cannot re-order anything; the other four are the
	//  combination Microsoft's SetWindowPos Remarks prescribe for making a SetWindowLongPtr style
	//  change take effect.
	::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	::RedrawWindow(hwnd, nullptr, nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);

	// *****A MINIMIZED DIALOG MUST BE BROUGHT BACK, OR THE MENU ITEM LOOKS DEAD.*****
	// IDialogMgr hands back the EXISTING dialog instead of making a second one - "If
	// allowMultipleCopies is false and the dialog is already open, the existing dialog is returned"
	// (IDialogMgr.h:66-67), and a modeless dialog is single-copy regardless (:67) - and Open() on an
	// already-open window does NOT un-minimize it.
	// ⚠ MEASURED 2026-08-12: minimize the dialog, then choose "Compare Books" from the flyout again -
	//   the menu item is ENABLED, invoking it reports success, and IsIconic stays true with the window
	//   parked at -32000,-32000 as a 160x28 stub. Nothing appears. An enabled item that does nothing
	//   is the worst shape for this: a greyed one at least says why.
	// ★Neither SetWindowPos(SWP_FRAMECHANGED) above nor Open() itself restores it - only this does
	//   (both measured the same day).
	if (::IsIconic(hwnd))
		::ShowWindow(hwnd, SW_RESTORE);
#endif
}

}	// anonymous namespace

void KESCMOpenBookDialog()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return;

	InterfacePtr<IApplication> application(session->QueryApplication());
	if (application == nil)
		return;

	InterfacePtr<IDialogMgr> dialogMgr(application, UseDefaultIID());
	if (dialogMgr == nil)
		return;

	RsrcSpec dialogSpec
	(
		LocaleSetting::GetLocale(),		// Locale index
		kKESCMPluginID,					// This plug-in
		kViewRsrcType,
		kKESCMBookDialogRsrcID,			// The dialog's view resource
		kTrue							// Initially visible
	);

	// ***** kModeless / kDontAllowUserResize / kCacheDialog - each for its own reason: *****
	//   - kModeless is the point of the whole thing (see the header)
	//   - kDontAllowUserResize because a modeless dialog's panel does NOT follow its window: a
	//     resize handle would move the frame and leave the contents behind
	//   - kCacheDialog so that closing and reopening keeps the contents and the position. That
	//     matters here more than in most dialogs: the results took real time to compute, and
	//     closing the window must not be the same as throwing them away
	// kDontAllowMultipleCopies is belt and braces - IDialogMgr already allows only one copy of a
	// modeless dialog (IDialogMgr.h:67) - but it states the intent rather than relying on it.
	IDialog* dialog = dialogMgr->CreateNewDialog(dialogSpec, IDialog::kModeless,
		IDialogMgr::kDontAllowMultipleCopies, IDialogMgr::kCacheDialog,
		IDialogMgr::kDontAllowUserResize);
	if (dialog == nil)
		return;

	// ***** doWait = kFalse. ***** Open() waits by default (IDialog.h:59-65), which would make a
	// modeless dialog behave like a modal one - the exact thing this dialog exists not to do.
	dialog->Open(nil, kFalse);

	// AFTER Open, not before: the platform window does not exist until the dialog is opened. ★And on
	// EVERY open, not just the first - this call is also what brings the dialog back when the last
	// thing the user did to it was minimize it.
	KESCMPrepareBookDialogWindow(dialog);
}

// End, KESCMBookDialog.cpp.
