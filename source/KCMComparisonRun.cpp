//========================================================================================
//
//  KCMComparisonRun.cpp
//
//  Starting and clearing a comparison, plus the two display settings that come with it: the
//  resolver (which two documents), the procedure that starts on those two, the Start/Stop
//  toggle, and the print-marks and opacity switches.
//
//  MODEL side: this drives the comparison itself. The callers (the flyout items, the
//  right-click on a book comparison row) stay in the UI, and everything here says what
//  happened by emitting a notification rather than by touching the panel.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISession.h"				// GetExecutionContextSession -- can be nil during shutdown, so the type is spelled out
#include "IApplication.h"			// QueryApplication
#include "IDocument.h"
#include "IDocumentList.h"
#include "IDataBase.h"
#include "PersistUtils.h"			// ::GetUIDRef
#include "PMString.h"

// Project includes:
#include "KCMComparisonRun.h"
#include "KCMCore.h"				// arm/disarm, running the comparison, the print-mark settings
#include "KCMID.h"				// kKCMMarksRebuiltMessage / kKCMMarksClearedMessage
#include "KCMModelNotify.h"	// KCMNotifyStatus / KCMNotify - the model tells the UI, it never calls it
#include "KCMDrawEventHandler.h"	// sSrcMarksOn / sOversetOn / sOversetDB
#include "KCMOversetApply.h"		// KCMApplyOversetForDoc -- re-apply overset on Start and on Stop

//----------------------------------------------------------------------------------------
// The resolver: which two documents to compare
//----------------------------------------------------------------------------------------

// The ONE place that resolves Target and Source: the active (front) document is the Target, and
// the first other open document is the Source. kTrue only when both were found; whichever could
// not be resolved comes back nil, so the caller can word its message from which is missing.
// Resolving in one place is the point: "can a comparison be started" is asked by the menu's grey
// state (KCMCanStartComparison) and by the command itself (KCMToggleStartStop), and written twice
// the two answers would drift ([[one-question-one-place]]).
static bool16 KCMResolveComparisonPair(IDocument*& outTarget, IDocument*& outSource);

// The first open document that is not `target` = the Source (the older version).
static IDocument* KCMFirstOtherDoc(IDocument* target)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return nil;
	const int32 n = docList->GetDocCount();
	for (int32 i = 0; i < n; ++i)
	{
		IDocument* d = docList->GetNthDoc(i);
		if (d != nil && d != target)
			return d;
	}
	return nil;
}

// The resolver declared above.
static bool16 KCMResolveComparisonPair(IDocument*& outTarget, IDocument*& outSource)
{
	outTarget = KCMActiveDoc();
	outSource = (outTarget != nil) ? KCMFirstOtherDoc(outTarget) : nil;
	return (outTarget != nil && outSource != nil) ? kTrue : kFalse;
}

// KCMCanStartComparison (declared in KCMComparisonRun.h) -- whether the flyout's Start may be
// enabled. Goes through the same resolver as the command, so the two cannot disagree.
bool16 KCMCanStartComparison()
{
	IDocument* target = nil;
	IDocument* source = nil;
	return KCMResolveComparisonPair(target, source);
}

//----------------------------------------------------------------------------------------
// The actions
//----------------------------------------------------------------------------------------

// KCMStopComparison (declared in KCMComparisonRun.h) -- end the comparison.
// Split out of the toggle for the same reason KCMStartComparisonFor is: **the procedure lives in
// one place** ([[one-question-one-place]]).
void KCMStopComparison()
{
	// The active document is only used for the redraw, and nil is fine: KCMDoClearMarks and
	// KCMDoDisarmMousePeek each remember the document the marks were actually drawn on (sDB, the
	// armed target) and redraw that, so clearing and disarming still work with no document open.
	IDataBase* db = KCMActiveDocDB();

	KCMDoClearMarks(db);
	KCMDoDisarmMousePeek(db);
	// Scrollbar map: the strips come off every window (Target and Source alike), and that is done
	// by the UI when it receives the kKCMMarksClearedMessage the KCMDoClearMarks above emits.
	// If Find Overset is on by itself, re-apply it to the overset document (sOversetDB) so the
	// Source window is not left with a comparison strip, AND so the overset set itself is scanned
	// again: with overset present, Start, then an edit that resolves it, then Stop used to leave
	// the pre-edit set standing and the plug-in went on reporting overset that was gone.
	// KCMApplyOversetForDoc re-scans sOversetDB and updates the thumbnails, the map's
	// Attach+Invalidate and Prev/Next together.
	if (KCMDrawEventHandler::sOversetOn)
		KCMApplyOversetForDoc(KCMDrawEventHandler::sOversetDB);
	KCMSayStatus("marks cleared");

	// **Cleared is emitted a second time, on purpose.** When KCMDoClearMarks emitted it, the
	// disarm three lines above had not run yet, so the armed state was still up and the panel
	// would have been rebuilt looking like a comparison in progress. Emitting again here rebuilds
	// the Target/Source names, the icon and the Prev/Next enabling from the state AFTER the stop.
	// No document travels with it (docA/docB are nil), and that is the SIGN that this notification
	// only asks for a redraw: the listener (KCMModelChangeObserver) uses it to skip both the
	// thumbnail purge and the removal of the scrollbar map's strips.
	// @warning skipping the strip removal matters here -- without it this notification tears off
	// the Find Overset strip that KCMApplyOversetForDoc has just put back, three lines above.
	KCMNotify(kKCMMarksClearedMessage);
}

// KCMStartComparisonFor (declared in KCMComparisonRun.h) -- start a comparison ON THESE TWO
// DOCUMENTS.
//
// **The resolver (which two) and the procedure (what to do) are kept apart.** There is no
// document-choosing here at all; it starts on whatever it is handed. The reason is that there are
// two callers:
//   1. KCMToggleStartStop, which resolves active = Target and another open document = Source;
//   2. "Start Change Marker" on a book comparison row, which opens that chapter's two files.
// Copying the procedure into each would let the two drift ([[one-question-one-place]]), and the
// procedure holds three decisions that all fail quietly when forgotten: do not arm on cancel, let
// the strips go onto both windows, re-apply overset.
void KCMStartComparisonFor(IDocument* target, IDocument* source)
{
	if (target == nil || source == nil)
		return;

	IDataBase* targetDB = ::GetUIDRef(target).GetDataBase();
	IDataBase* sourceDB = ::GetUIDRef(source).GetDataBase();

	PMString report;
	// **Start does not touch "Always Show Marks on Target / Source".** Setting them here would
	// fight the saved panel state, which is restored at start-up (KCMLoadPanelStateIfPresent from
	// KCMUIStartup::Startup): a Start that overwrites them means the saved choice is wiped by
	// every comparison, and saving it stops meaning anything. The default is the static initial
	// value (off); from there the reader's choice stands.
	// If the user CANCELS the comparison (the progress bar carries a Cancel on longer runs), do
	// not start: the marks have already been discarded inside KCMDoMarkChangesDoc, so leaving the
	// arm and the strips out puts everything back the way it was before the press, rather than
	// creating an armed state with no marks in it.
	if (KCMDoMarkChangesDoc(targetDB, sourceDB, report) == kSuccess)
	{
		KCMDoArmMousePeek(targetDB, sourceDB);
		// The scrollbar map's strips are injected by the UI when it receives the
		// kKCMMarksRebuiltMessage that KCMDoMarkChangesDoc emitted -- into BOTH windows (the strip
		// looks at each window's document and switches its source accordingly).
		// If Find Overset is still on, re-scan the comparison Target so overset is re-applied.
		// That does two things: (a) Prev/Next can walk "changes then overset" within one Target
		// document -- otherwise an overset set belonging to a different document (sOversetDB != sDB)
		// drops out of the walk silently; (b) overset that edits have added or removed since the
		// last scan is picked up, which a same-document Start would otherwise miss.
		if (KCMDrawEventHandler::sOversetOn)
			KCMApplyOversetForDoc(targetDB);
	}
	KCMNotifyStatus(report);

	// **Rebuilt is emitted again after the arm**, for the reason its mirror image is emitted again
	// in KCMStopComparison: when the comparison itself emitted, KCMDoArmMousePeek had not run, so
	// the panel would rebuild looking like nothing had started.
	// No document travels with it (docA/docB are nil) so the thumbnail purge is not repeated --
	// only the display is being brought up to date.
	KCMNotify(kKCMMarksRebuiltMessage);
}

void KCMToggleStartStop()
{
	const bool16 armed = KCMIsArmed() && (KCMArmedTargetDB() != nil);
	if (armed)
	{
		KCMStopComparison();
		return;
	}

	// Start: active (front) document = Target, another open document = Source.
	// The flyout's Start is grey unless two documents are there (KCMCanStartComparison goes
	// through the same resolver), so this normally cannot fail. It is the guard for the case
	// where a document is closed while the menu stands open.
	IDocument* target = nil;
	IDocument* source = nil;
	if (!KCMResolveComparisonPair(target, source))
	{
		// Name what is actually missing: if the target resolved, only the Source is absent.
		KCMSayStatus(target == nil ? "Target and source documents not found."
		                         : "Source document not found.");
		// This branch needs the notification too. It returns from inside the else, so it never
		// reaches the end of the function -- an early implementation refreshed the panel only at
		// the end and left this path without a redraw.
		// No document travels with it: the display is only being brought up to the current state.
		KCMNotify(kKCMMarksRebuiltMessage);
		return;
	}

	KCMStartComparisonFor(target, source);
}

// Put the state of the two mark settings on the status line. Both callers below change one of the
// pair and keep the other, so both have to say the same sentence.
// **Written out twice, the two copies drifted**: when the plug-in was renamed (2026-08-25) the
// prefix here was left at the old "kescm:" in every copy, and the reader saw a name that no longer
// exists on three of the flyout's toggles. Saying it in one place is also what makes it possible
// to change what is said.
static void KCMReportMarkSettings(bool16 printFlag, bool16 op25)
{
	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(op25 ? "kcm: marks opacity 25%" : "kcm: marks opacity 75%");
	report.Append(printFlag ? "; will print (and stay visible on screen)"
	                        : "; screen-only (won't print)");
	KCMNotifyStatus(report);
}

// KCMSetMarkOpacity25 (declared in KCMComparisonRun.h) -- set the frame opacity to 25% or 75%,
// from the flyout's kKCMPopupOpacity25ActionID / kKCMPopupOpacity75ActionID. The current print
// flag is kept. The radio-button look (a tick on the chosen item) is applied when the menu opens,
// by UpdateActionStates reading KCMGetMarkOpacity25.
void KCMSetMarkOpacity25(bool16 op25)
{
	const bool16 flag = KCMGetPrintMarks();	// keep the current print on/off
	KCMDoSetPrintMarks(flag, op25, KCMActiveDocDB());

	KCMReportMarkSettings(flag, op25);
}

// KCMSetMarkColor (declared in KCMComparisonRun.h) -- set the mark colour to red or cyan, from
// the flyout's kKCMPopupColorRedActionID / kKCMPopupColorCyanActionID. The tick on the chosen item
// is applied by UpdateActionStates reading KCMGetMarkColorCyan, the same way the opacity pair works.
// This replaced an automatic choice that switched to cyan over reddish ground, decided per pixel
// off the comparison raster. It went because the reader could not tell why a mark was the colour
// it was, and because **the Story mode cannot read the ground at all**, so the two modes would
// have disagreed about how the colour is chosen.
void KCMSetMarkColor(bool16 cyan)
{
	KCMDoSetMarkColor(cyan, KCMActiveDocDB());

	// Not KCMReportMarkSettings: this one reports the colour, which is not part of the print /
	// opacity pair. It carries the same prefix, and the prefix is the whole of what they share.
	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(cyan ? "kcm: mark colour cyan" : "kcm: mark colour red");
	KCMNotifyStatus(report);
}

// KCMTogglePrintMarks (declared in KCMComparisonRun.h) -- flip the print-marks flag, from the
// flyout's kKCMPopupPrintMarksActionID. The opacity choice is kept as it is. Only the status line
// is updated here; the check mark is applied when the menu opens, by UpdateActionStates reading
// KCMGetPrintMarks.
void KCMTogglePrintMarks()
{
	const bool16 newFlag = !KCMGetPrintMarks();
	const bool16 op25    = KCMGetMarkOpacity25();
	KCMDoSetPrintMarks(newFlag, op25, KCMActiveDocDB());

	KCMReportMarkSettings(newFlag, op25);
}

// End of KCMComparisonRun.cpp.
