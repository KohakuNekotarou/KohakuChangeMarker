//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: the dialog. See KESCMBookDialog.h for why it is modeless and fixed-size.
//
//  Shape copied from KESCL's Jump Offset dialog (KESCLActionComponent.cpp:384-417), which is the
//  closest working example in reach - same author, same stock kDialogBoss, also fixed-size.
//  ⚠ "The one thing changed is the dialog type" stood here until 2026-08-18 (bug recheck B-U5).
//    THREE of CreateNewDialog's five arguments differ, and each difference is deliberate - they are
//    what a modeless report needs and a modal prompt does not:
//        kModeless                 (KESCL: kMovableModal)      - the point of the whole thing
//        kDontAllowMultipleCopies  (KESCL: kAllowMultipleCopies)
//        kCacheDialog              (KESCL: kDontCacheDialog)   - the results took time to compute
//    …and so does the OPEN that follows it, which is not one of those five:
//        Open(nil, kFalse)         (KESCL: Open())             - do not take over the event loop
//    What the two share is the RsrcSpec, built the same way, AND kDontAllowUserResize - the fifth
//    argument, which both pass for reasons of their own (KESCL: "two edit boxes and a button row
//    have nothing to gain"; here: a modeless dialog's panel does not follow its window - see the
//    header). ★The reasons are on each argument at the call.
//    ⚠ The correction above said "FOUR of the five" and "only the RsrcSpec is built the same way"
//      from 2026-08-18 until later the same day (bug recheck B-U5, second pass): it counted the
//      Open() call as one of CreateNewDialog's arguments and then reported the fifth argument, which
//      is IDENTICAL in both, as a difference. Fixing a stale sentence is where a new miscount gets
//      in - the same shape as the counts B6 / B10 / B-U5 found, this time in a correction.
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
#include "KESCMBookOpen.h"		// KESCMBookSetMenuRow - the stashed row index, dropped when the rows change
#include "KESCMBookTree.h"		// KESCMBookTreeRebuild - the list, redrawn when the dialog opens
#include "KCMUIID.h"
#include "KESCMPanelAlpha.h"	// KESCMSetBookDialogWindow / KESCMApplyBookDialogTranslucency
#include "KESCMPathDisplay.h"	// KESCMPathForDisplay - the "/" separators, shared with the panel

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

/** Paint the three text lines from what the last run stored.

	⚠"NOTHING CAN CHANGE UNDERNEATH IT WHILE IT IS OPEN" STOOD HERE, AND IT WAS WRONG. Written on
	2026-08-12 (the day the dialog lost its Compare button) on the grounds that the dialog no longer
	RUNS anything - which is true, and beside the point: this dialog is MODELESS, so the flyout's
	"Compare Books" can be chosen while it stands there, and that replaces all four of the values
	above. Measured 2026-08-18 (bug recheck B-U5); the repaint that fixes it, and the measurement,
	are in KESCMOpenBookDialog. */
void KESCMBookDialogPaintResult(IPanelControlData* panelData)
{
	if (panelData == nil)
		return;

	// ★The labels stay English (the plug-in's UI language rule); only the paths vary.
	//
	// ***** THE WHOLE PATH GOES IN; THE WIDGET SHORTENS IT. ***** (2026-08-15, user: "make the paths
	// match the way the panel puts them out".) The panel's Target:/Source: lines hand their widget
	// the complete path and let kEllipsizeBeginning cut the front off, and these two now do the same.
	//
	// ⚠THE OLD COMMENT HERE WAS WRONG, AND IT IS WORTH KNOWING WHY. It said the C++ shortening was
	//   what fixed the dialog's width and that kEVEAlignFill had been tried and did nothing - while
	//   KCMUI.fr, about the same day and the same widget, said Fill was the fix. Removing the
	//   shortening settled it: the dialog went to 593px, so neither claim was the whole story.
	// ★What actually holds the width is kKESCMBookPathTextWidgetBoss (KCMUI.fr): EVE asks a widget's
	//   IID_IEVEINFO for its size, and that boss answers "the size the resource wrote"
	//   (kFixedSizeEVEInfoImpl). Without it, the width in a .fr is only a MINIMUM - the guide says so
	//   in as many words ("we treat the width in the .fr file as a minimum width", Using EVE,
	//   Example 2) - and Fill cannot help because Fill takes the PARENT's width, and the parent is
	//   itself grown by the child asking for more. With it, the dialog measured 400px carrying a
	//   66-character path, and the ellipsis does the shortening the string used to do.
	PMString targetLine("Target: ");
	targetLine.Append(KESCMPathForDisplay(gTargetPath));
	targetLine.SetTranslatable(kFalse);

	PMString sourceLine("Source: ");
	sourceLine.Append(KESCMPathForDisplay(gSourcePath));
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

	// ***** UNCHANGED CHAPTERS ARE NOT LISTED. ***** (User, 2026-08-13: "in the comparison dialog's
	// row part, do not show the NoChange ones".) The list exists to answer "which chapters changed?",
	// and a run over a long book is mostly rows that say nothing happened.
	//
	// ★THE COUNT STILL COMES FROM ALL OF THEM. gSummary above is built by the comparison over the
	//   full set ("3 chapters: 1 changed, 2 unchanged"), so what is filtered out here is still
	//   accounted for one line above the list. That is what keeps an empty list readable: it means
	//   "nothing changed", and the summary says so in numbers - never "nothing could be opened"
	//   (the reason the status line states the chapter count at all - see KCMUI.fr).
	// ★EVERY OTHER STATE STAYS, including NotCompared and Failed: they are chapters with no answer
	//   yet, which is the opposite of "no change" and must not disappear with it
	//   (KESCMBookResult.h:42-49 is the same distinction, written out).
	// ★app.kcmBookResult IS NOT AFFECTED - it is built from gBookResultText in KESCMBookCompare.cpp
	//   over the full set, so scripts and tests still see every chapter.
	//
	// ***** THE STASHED ROW INDEX GOES WITH THEM. ***** The right-click menu records which row it was
	// popped over (KESCMBookOpen.cpp's gMenuRow) because the action it raises is handed no widget
	// context - and that index means "row N of the list as it stood THEN". The rows below are about
	// to be replaced, so keeping it would let an index taken from one comparison name a chapter of
	// the next one.
	// ⚠ MEASURED 2026-08-18 (bug recheck B-U5, second pass), and it was a real bug: with the dialog
	//   showing one row (ch2) that row was right-clicked, ch1 was then made to differ as well, the
	//   comparison was re-run - and firing kKESCMBookRowStartActionID by ID reported enabled=true and
	//   opened BOTH SIDES OF ch1, a chapter nobody had right-clicked. The range check in RowAt does
	//   not catch this: 0 is a perfectly valid row in the new list, just a different chapter.
	// ★THE ROUTE THAT REACHES IT WITHOUT A RIGHT CLICK is a script firing the action by ActionID
	//   (app.menuActions.itemByID(...).invoke()); a keyboard shortcut cannot, because the action is
	//   declared kSDKDefInvisibleInKBSCEditorFlag and so never appears in the shortcut editor.
	// ★THIS IS KBS'S RULE, WRITTEN DOWN IN KBS AND MISSING HERE. KBSResultModel::Clear() ends with
	//   gContextMenuChapter = kNoContextMenuChapter for exactly this reason, and KBSResultModel.h
	//   states it as a requirement on anything that drops rows: "an index taken from one result set
	//   can never name a chapter of the next one … Anything that ever drops chapters WITHOUT the
	//   aftermath flag has to reset this the way Clear() does." KESCMBookOpen.h claimed this file's
	//   feature "works exactly this way" as KBS - it did, apart from the one line that puts it back.
	KESCMBookSetMenuRow(-1);

	gDialogRows.clear();
	for (size_t i = 0; i < rows.size(); ++i)
	{
		if (rows[i].fState != kKESCMChapterNoChange)
			gDialogRows.push_back(rows[i]);
	}
}

const std::vector<KESCMChapterResult>& KESCMBookDialogRows()
{
	return gDialogRows;
}

void KESCMBookDialogShutdown()
{
	// ***** ALL FOUR, BECAUSE ALL FOUR HOLD HEAP. ***** The three PMStrings, and the rows - whose
	// KESCMChapterResult carries PMStrings of its own - are emptied so that the static destructors
	// at DLL unload find nothing left to do. Windows has never shown a fault from leaving them
	// behind, but the Mac unloads in a different order, which is why this plug-in empties every
	// static string it keeps (2026-07-15 teardown hardening).
	// ★ASSIGNED a fresh vector rather than clear()ed: clear() keeps the storage, and with it the
	//   rows' PMString buffers - the one thing this call exists to release. Same line and same
	//   reason as KESCMStoryList::ShutdownCleanup.
	// ⚠ADDED 2026-08-18 (bug recheck B-U5). The model half's Shutdown lists every static of its own
	//   that holds a PMString, and B8 had just found two missing from that list; these four were
	//   the same omission on this side - nothing in KESCMUIStartup::Shutdown named them, so a
	//   session that ran one book comparison carried its rows and both paths to unload.
	// Touches no widget and no document, so it is safe wherever in the teardown it is reached.
	gDialogRows = std::vector<KESCMChapterResult>();
	gTargetPath.Clear();
	gSourcePath.Clear();
	gSummary.Clear();
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

	// ***** TELL THE TRANSLUCENCY CODE WHERE THIS WINDOW IS, THEN LET IT PAINT. ***** (User,
	// 2026-08-13: "make it so the dialog can be translucent too".) The two panel toggles find their
	// window by asking the panel manager for a WidgetID; a dialog is not a panel and is not in there,
	// so this is the only place that knows the handle.
	// ★BOTH CALLS, AND IN THIS ORDER, ON EVERY OPEN. Registering is not applying: the toggle can be
	//   switched while the dialog is closed, and the window that comes back knows nothing about it.
	// ⚠**MEASURED 2026-08-17 (B-U9), correcting what stood here.** This used to say "a cached dialog
	//   (kCacheDialog) comes back with whatever alpha it had last time", and that is NOT what happens:
	//   opening the dialog three times produced THREE DIFFERENT HWNDs (0x63FADC, 0x220CB6, 0x460854),
	//   and a reopened window read back as NOT LAYERED AT ALL. **kCacheDialog keeps the dialog's
	//   CONTENTS (the rows - see InitializeDialogFields), not the platform window.**
	//   ★The file already said so 60 lines up: "it is created by KESCMOpenBookDialog and destroyed
	//     when the dialog closes" - two statements in one file, and the false one was the one being
	//     used as a reason. The remaining reason is the one that matters anyway: **with the toggle ON,
	//     a brand-new window starts opaque, so 77 has to be written on every open** (measured: reopen
	//     with the toggle ON comes up at 77).
	// ★Nothing is needed on close, but NOT because the handle stops being valid on its own: a closed
	//   window's HANDLE VALUE gets handed out again to some other window, and IsWindow says yes to
	//   that one too. What makes closing safe is that KESCMSetBookDialogWindow also records THIS
	//   window's title and every later use is checked against it (KESCMPanelAlpha.cpp, 2026-08-13) -
	//   so a recycled handle is dropped rather than painted on. Registering it here is what feeds
	//   that check; there is no matching "forget" call to write.
	KESCMSetBookDialogWindow(hwnd);
	KESCMApplyBookDialogTranslucency();
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
		kKCMUIPluginID,					// This plug-in
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

	// ***** PAINT IT HERE, BECAUSE A REOPEN NEVER SEES InitializeDialogFields. *****
	// ⚠MEASURED 2026-08-18 (bug recheck B-U5), and this was a real bug. With the dialog already on
	//   screen, a second comparison left it showing the FIRST one: IDialogMgr hands back the
	//   existing dialog (IDialogMgr.h:66-67) and Open() does reach it - the window moved - but the
	//   controller's InitializeDialogFields is NOT called a second time. So KESCMBookDialogSetResult
	//   replaced the module's rows and paths underneath a window that went on drawing the old ones.
	//   Both directions were run:
	//     - target and source swapped  -> the dialog still read "Target: new / Source: old"
	//     - one more chapter changed (rows 1 -> 2) -> it still said "1 changed" with one row, AND
	//       DOUBLE-CLICKING THAT ROW OPENED ch1 WHILE THE ROW READ "ch2.indd"
	// ★THE SECOND ONE IS WHY THIS MATTERS. Row indices index KESCMBookDialogRows(), which had
	//   already moved on, so a click meant something other than what it read - the same fault shape
	//   as the one the header describes being fixed on 2026-08-12 (labels describing one run while
	//   the rows described another). Storing the paths made the two agree; nothing made them get
	//   REDRAWN.
	// ★THE SHAPE IS THE PRODUCT'S OWN: spellpanel opens its modeless dialog and then reaches inside
	//   it (SpellMenuComponent.cpp:161-168), and IPanelControlData comes straight off the dialog
	//   boss - the SDK writes this exact line in five places, the framework included
	//   (CDialogController.cpp:474 / SpellMenuComponent.cpp:212 :253 /
	//   BscSlDlgActionComponent.cpp:209 / XDocBkUIActionComponent.cpp:209).
	// ★InitializeDialogFields KEEPS its own copy of these two calls, and that is not the same
	//   question asked in two places: that one is the framework building the dialog for the first
	//   time (and it also runs when the cached dialog has been dropped), this one is the reopen.
	//   Neither decides anything - both paint whatever the module holds at that instant - so the
	//   first open simply repaints a few rows once more.
	InterfacePtr<IPanelControlData> panelData(dialog, UseDefaultIID());
	KESCMBookDialogPaintResult(panelData);
	KESCMBookTreeRebuild(panelData);

	// AFTER Open, not before: the platform window does not exist until the dialog is opened. ★And on
	// EVERY open, not just the first - this call is also what brings the dialog back when the last
	// thing the user did to it was minimize it.
	KESCMPrepareBookDialogWindow(dialog);
}

// End, KESCMBookDialog.cpp.
