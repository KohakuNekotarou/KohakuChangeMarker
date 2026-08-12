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
#include "IControlView.h"		// Enable / Disable - the Compare button
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
#include "KESCMBookPair.h"		// KESCMResolveBookPair / KESCMBookDisplayName
#include "KESCMBookTree.h"		// KESCMBookTreeRebuild - the list, redrawn when the dialog opens
#include "KESCMID.h"

// *windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names.
//  (The same order KESCMPanelAlpha.cpp uses.)
#ifdef WINDOWS
#include <windows.h>
#endif

/** The dialog's controller.

    There is nothing to fill in or apply yet - the Target/Source labels, the Compare button and the
    chapter list all arrive in the next tasks. The class exists now because the boss names this
    implementation, and a boss naming an implementation that is not registered simply fails to
    produce the interface (silently - which is the whole difficulty of that class of bug). */
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
	KESCMBookDialogUpdateTargets(panelData);

	// ★The list is rebuilt from the module's rows on every open, so that what is on screen is
	//   always what this plug-in actually holds. Measured 2026-08-11: kCacheDialog does keep the
	//   rows on screen across close and reopen, so this is not what makes reopening work - it is
	//   what keeps the two from ever disagreeing if the cached dialog is dropped. Rebuilding an
	//   already-correct list of a few rows costs nothing.
	KESCMBookTreeRebuild(panelData);
}

//----------------------------------------------------------------------------------------
// Filling the dialog in
//----------------------------------------------------------------------------------------

namespace
{

/** The chapter list's rows. ★Module scope, not a member of the dialog: the dialog window is
	created and destroyed as it is opened and closed, and results that survive only as long as the
	window would make closing it the same as discarding them. */
std::vector<KESCMChapterResult> gDialogRows;

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

}	// anonymous namespace

void KESCMBookDialogUpdateTargets(IPanelControlData* panelData)
{
	if (panelData == nil)
		return;

	IBook* target = nil;
	IBook* source = nil;
	const bool16 resolved = KESCMResolveBookPair(target, source);

	PMString targetLine("Target: ");
	PMString sourceLine("Source: ");
	if (resolved)
	{
		targetLine.Append(KESCMBookDisplayName(target));
		sourceLine.Append(KESCMBookDisplayName(source));
	}
	else
	{
		// ***** Say WHICH half is missing, and never guess. ***** The front tab is deliberately
		// not allowed to fall back to InDesign's active book (see KESCMBookPair.h), so when it
		// cannot be identified the honest thing is to say so and refuse to run.
		targetLine.Append("(no book tab in front)");
		sourceLine.Append("(no second open book)");
	}
	targetLine.SetTranslatable(kFalse);
	sourceLine.SetTranslatable(kFalse);

	SetLine(panelData, kKESCMBookTargetTextWidgetID, targetLine);
	SetLine(panelData, kKESCMBookSourceTextWidgetID, sourceLine);

	// Compare is only pressable when there is something to compare.
	IControlView* compareButton = panelData->FindWidget(kKESCMBookCompareButtonWidgetID);
	if (compareButton != nil)
	{
		if (resolved)
			compareButton->Enable();
		else
			compareButton->Disable();
	}
}

void KESCMBookDialogSetStatus(IPanelControlData* panelData, const PMString& message)
{
	SetLine(panelData, kKESCMBookStatusTextWidgetID, message);
}

const std::vector<KESCMChapterResult>& KESCMBookDialogRows()
{
	return gDialogRows;
}

void KESCMBookDialogSetRows(const std::vector<KESCMChapterResult>& rows)
{
	gDialogRows = rows;
}

void KESCMBookDialogController::ApplyDialogFields(IActiveContext* context, const WidgetID& widgetId)
{
	CDialogController::ApplyDialogFields(context, widgetId);
}

namespace
{

/** Put a MINIMIZE BOX on the dialog's own window, so a comparison that took real time to compute can
    be pushed out of the way instead of closed.

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
    a dialog of our own can simply have the button (the user's call, 2026-08-12). */
void KESCMPutMinimizeBoxOnDialog(IDialog* dialog)
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

	// AFTER Open, not before: the platform window does not exist until the dialog is opened.
	KESCMPutMinimizeBoxOnDialog(dialog);
}

// End, KESCMBookDialog.cpp.
