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
#include "KCMPairChoice.h"		// which two: the chosen pair, the resolver, and realising a file end
#include "KCMCore.h"				// arm/disarm, running the comparison, the print-mark settings
#include "KCMID.h"				// kKCMMarksRebuiltMessage / kKCMMarksClearedMessage
#include "KCMModelNotify.h"	// KCMNotifyStatus / KCMNotify - the model tells the UI, it never calls it
#include "KCMDrawEventHandler.h"	// sSrcMarksOn / sOversetOn / sOversetDB
#include "KCMPeek.h"				// KCMArmedDocsAlive -- Refresh must not hand a closed document to the comparison
#include "KCMRingAdornment.h"		// KCMRevalidateItemXPList -- the transparency-list insurance, at Start and at Stop
#include "KCMThreadSafety.h"		// KCMIsSameDoc -- the one place this plug-in asks whether two dbs are one document
#include "KCMExternalSource.h"	// the lent Source: registered and chosen by KCMStartComparisonWithSourceDB, forgotten by the lender's Release
#include "KCMOrigin.h"			// the origin (Task Start): the third kind of Source, chosen by KCMChooseOriginPair
#include "KCMOriginCompare.h"	// KCMOriginStart / KCMOriginRefresh / KCMOriginArmed / KCMOriginOnStop - the origin's Start and Refresh

// KCMCanStartComparison (declared in KCMComparisonRun.h) -- whether the flyout's Start may be
// enabled. Goes through the same resolver as the command, so the two cannot disagree.
bool16 KCMCanStartComparison()
{
	// ⚠**THIS RUNS EVERY TIME THE FLYOUT IS OPENED**, which is why resolving may not open
	// anything: a file end stays a file here and is turned into a document only by the Start
	// (KCMPairChoice.h explains the two stages).
	KCMPairEnd target, source;
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
	// **The transparency-list insurance, on the pair being stopped** (KCMRingAdornment.h says what
	//   it is for). Asked through KCMIsDocDBOpen first: an armed db whose document has since closed
	//   is a pointer nothing may dereference, and Stop is reachable in exactly that state.
	{
		IDataBase* const armedTarget = KCMArmedTargetDB();
		IDataBase* const armedSource = KCMArmedSourceDB();
		if (KCMIsDocDBOpen(armedTarget))
			KCMRevalidateItemXPList(armedTarget);
		if (KCMIsDocDBOpen(armedSource))
			KCMRevalidateItemXPList(armedSource);
	}

	// The active document is only used for the redraw, and nil is fine: KCMDoClearMarks and
	// KCMDoDisarmMousePeek each remember the document the marks were actually drawn on (sDB, the
	// armed target) and redraw that, so clearing and disarming still work with no document open.
	IDataBase* db = KCMActiveDocDB();

	KCMDoClearMarks(db);
	KCMDoDisarmMousePeek(db);
	KCMOriginOnStop();			// Task Start: the armed-origin flag goes, and the peek document with it
	// Scrollbar map: the strips come off every window (Target and Source alike), and that is done
	// by the UI when it receives the kKCMMarksClearedMessage the KCMDoClearMarks above emits.
	// If Find Overset is on by itself, re-apply it to the overset document (sOversetDB) so the
	// Source window is not left with a comparison strip, AND so the overset set itself is scanned
	// (Stop used to re-apply the Find Overset scan here. That feature went on 2026-09-08.)
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

// The procedure itself, on two DATABASES -- start a comparison ON THESE TWO.
//
// **The resolver (which two) and the procedure (what to do) are kept apart.** There is no
// document-choosing here at all; it starts on whatever it is handed. The reason is that there are
// three callers:
//   1. KCMToggleStartStop, which resolves active = Target and another open document = Source;
//   2. "Start Change Marker" on a book comparison row, which opens that chapter's two files;
//   3. KCMStartComparisonWithSourceDB, where the Source is a database another plug-in lent
//      (2026-09-02) -- which is why this takes databases and KCMStartComparisonFor only unwraps.
// Copying the procedure into each would let the three drift ([[one-question-one-place]]), and the
// procedure holds three decisions that all fail quietly when forgotten: do not arm on cancel, let
// the strips go onto both windows, re-apply overset.
// @return kTrue when the comparison ran and the pair is armed; kFalse when it did not (a CANCEL
//   from the progress bar, or a failure). ★**THE ANSWER MATTERS ONLY TO A CALLER THAT WAS ALREADY
//   ARMED** - KCMRefreshComparison. For the other two, "not armed" is where they started, so the
//   kFalse case leaves exactly the state they began in and there is nothing to undo.
bool16 KCMStartComparisonOn(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (targetDB == nil || sourceDB == nil)
		return kFalse;

	// **The transparency-list insurance, on both documents, before anything is drawn**
	//   (KCMRingAdornment.h says what it is for). Both are live here -- the resolvers above only
	//   hand over open documents or the lent Source -- so no liveness test is needed.
	KCMRevalidateItemXPList(targetDB);
	KCMRevalidateItemXPList(sourceDB);

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
	const bool16 compared = (KCMDoMarkChangesDoc(targetDB, sourceDB, report) == kSuccess) ? kTrue : kFalse;
	if (compared)
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
		// (Start used to re-bind the Find Overset scan to the Target here -- gone 2026-09-08.)
	}
	KCMNotifyStatus(report);

	// **Rebuilt is emitted again after the arm**, for the reason its mirror image is emitted again
	// in KCMStopComparison: when the comparison itself emitted, KCMDoArmMousePeek had not run, so
	// the panel would rebuild looking like nothing had started.
	// No document travels with it (docA/docB are nil) so the thumbnail purge is not repeated --
	// only the display is being brought up to date.
	KCMNotify(kKCMMarksRebuiltMessage);

	return compared;
}

// KCMRefreshComparison (declared in KCMComparisonRun.h) -- compare the SAME pair again.
//
// It is three lines because the procedure is KCMStartComparisonOn's and this only supplies the
// documents. See the header for why nothing is stopped first, and why the comparison is the full
// one rather than the incremental one.
void KCMRefreshComparison()
{
	// Task Start: an armed origin pair has no Source database; its own Refresh rehydrates one.
	if (KCMOriginArmed())
	{
		KCMOriginRefresh();
		return;
	}

	IDataBase* const targetDB = KCMArmedTargetDB();
	IDataBase* const sourceDB = KCMArmedSourceDB();
	if (targetDB == nil || sourceDB == nil)
		return;					// nothing is armed - the menu item is greyed for this

	// ⚠**ASKED, NOT ASSUMED.** A document that has been closed since the comparison started leaves
	//   its IDataBase* behind in the armed state (that is the whole reason KCMArmedDocsAlive
	//   exists), and handing a dead one to the comparison is how a stale pointer gets dereferenced.
	if (!KCMArmedDocsAlive())
		return;

	if (KCMStartComparisonOn(targetDB, sourceDB))
		return;

	// ⚠★★★**A CANCELLED REFRESH HAS TO STOP THE COMPARISON, AND THAT IS NOT TIDINESS.**
	//   KCMDoMarkChangesDoc discards the marks BEFORE it can know it will be cancelled, and it
	//   returns kFailure precisely so that the Start route does not arm - its own comment says
	//   why: "always returning kSuccess would leave it armed after a cancel, with the menu stuck
	//   on Stop". **The Start route is safe because it was not armed to begin with.** A refresh
	//   IS armed, so doing nothing here leaves exactly the state that return value exists to
	//   prevent: the menu reading Stop, the panel naming two documents, and not one mark on
	//   screen. Stopping puts the reader somewhere they can act from - the same place a cancelled
	//   Start leaves them.
	KCMStopComparison();

	// KCMStopComparison ends with "marks cleared", which is true and says nothing about WHY. The
	//   reader pressed Cancel and needs to know that the comparison went with it - otherwise the
	//   only visible difference between "I cancelled a refresh" and "I pressed Stop" is one the
	//   panel does not show.
	KCMSayStatus("refresh cancelled - comparison stopped");
}

// KCMStartComparisonFor (declared in KCMComparisonRun.h) -- the two-DOCUMENT entry. It only
// unwraps; the procedure is KCMStartComparisonOn above.
void KCMStartComparisonFor(IDocument* target, IDocument* source)
{
	if (target == nil || source == nil)
		return;
	KCMStartComparisonOn(::GetUIDRef(target).GetDataBase(), ::GetUIDRef(source).GetDataBase());
}

// KCMStartComparisonWithSourceDB (declared in KCMComparisonRun.h) -- the lent Source's entry.
void KCMStartComparisonWithSourceDB(IDocument* target, IDataBase* sourceDB, const PMString& sourceLabel)
{
	if (target == nil || sourceDB == nil)
		return;

	IDataBase* const targetDB = ::GetUIDRef(target).GetDataBase();
	// KCMIsSameDoc is deliberately NOT asked here: the lent database is a clone of this very
	// document and names the same file, so by file the two ARE one document -- which is the
	// whole point. Only the pointer can say "you handed me the Target itself".
	if (targetDB == sourceDB)
	{
		KCMSayStatus("Target and Source are the same database.");
		KCMNotify(kKCMMarksRebuiltMessage);
		return;
	}

	// **Stop first**, through the ordinary Stop, so that the previous pair's marks and strips come
	// off on the same path every other Stop takes before the new pair is chosen and drawn. (The
	// registration is not touched by a Stop; it is REPLACED two lines below.)
	if (KCMIsArmed() && KCMArmedTargetDB() != nil)
		KCMStopComparison();

	// Registered BEFORE the run: the rasteriser's liveness checks (KCMIsDbAlive) have to say
	// "yes" about this database while its pages are being drawn.
	KCMRegisterExternalSource(sourceDB, sourceLabel);

	// ★★CHOSEN AS WELL AS STARTED (the user's ask, 2026-09-02: "keep Target and Source after a
	//  Stop, as a chosen pair is kept"). This is "Set as Target" + "Set as Source" + Start in one:
	//  the panel keeps naming both after a Stop, and the flyout's own Start compares against the
	//  same copy again, until the lender releases it. A cancel inside the run leaves the choice
	//  standing too -- the copy is still there, and Start is the way to try again.
	//   The lent database replaces the origin as the Source, as a real document does in
	//   KCMSetChosenSourceToActive - and the origin is released with the choice; left standing,
	//   the resolver would have gone on preferring it over the pair chosen here. KCMChooseDBPair
	//   is where all of that happens now (KCMPairChoice.h holds every slot).
	KCMChooseDBPair(targetDB, sourceDB);

	KCMStartComparisonOn(targetDB, sourceDB);
}

// KCMReleaseExternalSource (declared in KCMComparisonRun.h) -- the lender is deleting it.
void KCMReleaseExternalSource(IDataBase* sourceDB)
{
	if (sourceDB == nil || !KCMIsExternalSource(sourceDB))
		return;			// not ours to stop: the lender frees many databases and calls this for each

	// The ordinary Stop when it is being drawn from: marks off, peek disarmed, strips removed by
	// the UI on the notification. The database is still valid at this moment (the lender calls
	// before its delete), so the redraws inside are safe.
	const bool16 wasArmed = KCMIsArmed() && KCMArmedSourceDB() == sourceDB;
	if (wasArmed)
		KCMStopComparison();

	// The choice goes with it -- the panel's Source: line must not go on naming a copy that no
	// longer exists -- and so does the registration, here and nowhere else on the lender's side.
	KCMForgetChosenSourceIfDB(sourceDB);
	KCMForgetExternalSource();

	KCMSayStatus(wasArmed ? "Stopped: the task-start copy used as Source was released."
	                      : "The task-start copy chosen as Source was released.");
	KCMNotify(kKCMMarksClearedMessage);
}

void KCMToggleStartStop()
{
	const bool16 armed = KCMIsArmed() && (KCMArmedTargetDB() != nil);
	if (armed)
	{
		KCMStopComparison();
		return;
	}

	// Task Start: the chosen Source is the origin. Its own procedure rehydrates and compares; the
	// pair never reaches the resolver's same-document test below (the Source is not a database).
	if (KCMChosenSourceIsOrigin())
	{
		KCMOriginStart();		// its own words on the status line
		return;
	}

	// Start: active (front) document = Target, another open document = Source.
	// The flyout's Start is grey unless two documents are there (KCMCanStartComparison goes
	// through the same resolver), so this normally cannot fail. It is the guard for the case
	// where a document is closed while the menu stands open.
	KCMPairEnd targetEnd;
	KCMPairEnd sourceEnd;
	if (!KCMResolveComparisonPair(targetEnd, sourceEnd))
	{
		// Name what is actually missing: if the target resolved, only the Source is absent.
		KCMSayStatus(targetEnd.IsEmpty() ? "Target and source documents not found."
		                           : "Source document not found.");
		// This branch needs the notification too. It returns from inside the else, so it never
		// reaches the end of the function -- an early implementation refreshed the panel only at
		// the end and left this path without a redraw.
		// No document travels with it: the display is only being brought up to the current state.
		KCMNotify(kKCMMarksRebuiltMessage);
		return;
	}

	// ★★★**THE ONE PLACE A DOCUMENT IS OPENED FOR A COMPARISON** (2026-09-21). A chosen end may be
	// a FILE - a Task Start saves a copy of the document and names that file without opening it -
	// and here is where it becomes a database: the document already open on that file when there
	// is one, otherwise the file opened in a window.
	// ⚠**AFTER the resolver and never inside it.** The resolver is what the flyout's grey state
	//  rests on, and it is asked every time the menu is opened; opening a document from there
	//  would mean opening the flyout opened a document (KCMPairChoice.h).
	// ⚠**AND BEFORE THE SAME-DOCUMENT TEST BELOW**, which needs two databases to compare.
	IDataBase* targetDB = nil;
	IDataBase* sourceDB = nil;
	PMString whyNotRealised;
	if (!KCMRealisePairEnd(targetEnd, targetDB, whyNotRealised)
		|| !KCMRealisePairEnd(sourceEnd, sourceDB, whyNotRealised))
	{
		KCMNotifyStatus(whyNotRealised);
		KCMNotify(kKCMMarksRebuiltMessage);
		return;
	}

	// **One document cannot be compared against itself**, and with "Set as Target" / "Set as
	// Source" it can now be asked for: both items take the active document, so pressing them one
	// after the other without switching documents chooses the same one twice. That is deliberately
	// ALLOWED as a choice -- the panel shows the same name on both lines, which is the reader
	// seeing what they have asked for -- and refused here, at the Start.
	//
	// @warning **this is not folded into KCMCanStartComparison**, which would grey the Start out
	// instead. A greyed item says "not now" and names no reason; the reader who has just chosen
	// the same document twice needs to be told which of the two to change. So the item stays live
	// and pressing it answers (user's instruction: "rejected at the start, with a message on the
	// panel"). The automatic rule cannot produce this case -- KCMFirstOtherDoc excludes the
	// Target -- so it only ever arises from a choice, and the message can say so.
	//
	// ★**Asked of KCMIsSameDoc, which is where this plug-in answers "are these two one document".**
	// Everywhere else the question is put to it and never to `==` (the drawing side says so in as
	// many words: "KCMIsSameDoc, NOT ==", KCMStoryMarkBuild.cpp). Comparing the IDocument* the
	// resolver handed back would have been a second way of asking, and would have rested on the
	// two routes into a document -- GetContextDocument and FindDocByDataBase -- giving out one
	// pointer for one document, which nothing here has established.
	//   ★**Its background-thread half does not come into it here** (a clone db is a different
	//   pointer naming the same file): this runs from a menu, so both sides are the main thread's
	//   own databases and the answer is settled by the pointer test at its head. Going through it
	//   anyway is what keeps the one question in one place -- and what stops the next reader from
	//   having to work out whether this spot is the exception.
	//
	// ⚠**The message names both ways out**, because this case is reached from two different
	// mistakes and their remedies are not the same. Choosing one document for both is the obvious
	// one. **The commoner one is choosing only a Source and pressing Start without switching
	// documents**: "Set as Source" takes the ACTIVE document, so the Target, left unchosen,
	// resolves to that very document. The way out of that one is to bring the other document to
	// the front -- setting something is what the reader has already done.
	//   ★**And a lent Source is never "the same document"** (KCMIsSameDoc answers by pointer for
	//   it): the copy IS a copy of the Target, and comparing the two is the whole request.
	if (KCMIsSameDoc(targetDB, sourceDB))
	{
		KCMSayStatus("Target and Source are the same document. Bring another document to the front, or set one of them to another document.");
		// As in the branch above: this one returns without reaching the end of the function, so it
		// asks for the panel refresh itself. No document travels with it -- nothing has changed but
		// what the status line says.
		KCMNotify(kKCMMarksRebuiltMessage);
		return;
	}

	KCMStartComparisonOn(targetDB, sourceDB);
}

// Put the state of the two mark settings on the status line. Both callers below change one of the
// pair and keep the other, so both have to say the same sentence.
// **Written out twice, the two copies drifted**: when the plug-in was renamed (2026-08-25) the
// prefix here was left at the old "kescm:" in every copy, and the reader saw a name that no longer
// exists on three of the flyout's toggles. Saying it in one place is also what makes it possible
// to change what is said.
// ★**THE PREFIX IS GONE** (2026-09-10, the user's call: take "KCM:" off the panel's messages).
//   It said whose message it was, and the box it is drawn in belongs to this panel already -- the
//   reader is looking at Kohaku Change Marker while they read it. This finishes the removal begun
//   on 2026-08-25, when the same prefix came off the Pages panel's context items (KCMID.h).
static void KCMReportMarkSettings(bool16 printFlag, bool16 op25)
{
	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(op25 ? "Marks opacity 25%" : "Marks opacity 75%");
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
	// opacity pair. ⚠**They used to share a prefix, and that was all they shared** -- the prefix
	// came off both on 2026-09-10, so what is left in common is only the shape of the sentence.
	PMString report;
	report.SetTranslatable(kFalse);
	report.Append(cyan ? "Mark colour cyan" : "Mark colour red");
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
