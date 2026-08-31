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
#include "KCMThreadSafety.h"		// KCMIsSameDoc -- the one place this plug-in asks whether two dbs are one document

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

//----------------------------------------------------------------------------------------
// The chosen Target and Source (the flyout's "Set as Target" / "Set as Source")
//
// **Databases, not documents, and never dereferenced.** The pointer is only ever handed to
// IDocumentList::FindDocByDataBase, which is how the rest of this plug-in asks whether a
// database is still open (KCMArmedDocsAlive, KCMHandleDocsClosed). A closed document's
// IDataBase may already be freed and its address reused, so a raw IDocument* held across a
// close would be worse, not better ([[uidref-reuse-after-close]]).
//
// **The address-reuse window is closed at the other end**: kAfterCloseDoc runs
// KCMForgetChosenDocsThatClosed the moment a document goes, so a stale pointer does not
// survive long enough for a newly opened document to be given its address. The liveness test
// inside KCMLiveChosenDoc below is the second line, not the first.
//----------------------------------------------------------------------------------------

static IDataBase* sChosenTargetDB = nil;
static IDataBase* sChosenSourceDB = nil;

// The document `db` names, or nil when it is not (or no longer) an open document.
// Takes the list rather than fetching it so that the close sweep, which already holds one, can
// use the same test.
static IDocument* KCMLiveChosenDoc(IDataBase* db, IDocumentList* docList)
{
	if (db == nil || docList == nil)
		return nil;
	return docList->FindDocByDataBase(db);
}

// The same test for callers that have no list in hand. nil during the shutdown sequence, which
// is the right answer: with no session there is no way to judge liveness at all.
static IDocument* KCMLiveChosenDoc(IDataBase* db)
{
	if (db == nil)
		return nil;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	return KCMLiveChosenDoc(db, docList);
}

// KCMChosenTargetDB / KCMChosenSourceDB (declared in KCMComparisonRun.h).
IDataBase* KCMChosenTargetDB()	{ return (KCMLiveChosenDoc(sChosenTargetDB) != nil) ? sChosenTargetDB : nil; }
IDataBase* KCMChosenSourceDB()	{ return (KCMLiveChosenDoc(sChosenSourceDB) != nil) ? sChosenSourceDB : nil; }

// KCMSetChosenTargetToActive / KCMSetChosenSourceToActive (declared in KCMComparisonRun.h).
//
// **The active document is resolved here, on the model side.** The flyout item that calls this
// has no business naming a document -- IActiveContext::GetContextDocument is what "the active
// document" means in this plug-in (KCMActiveDoc), and asking it in one place is what keeps the
// menu and the comparison agreeing about which document that is
// ([[document-activation-is-presentation]] -- GetNthDoc(0) and GetFrontDocument each mean
// something else).
//   **Asked through KCMActiveDocDB, not KCMActiveDoc plus a GetUIDRef written out here**: that
//   pair IS KCMActiveDocDB (KCMCore.cpp), and it is what the UI's UpdateActionStates already goes
//   through the facade to reach (GetActiveDocDB) when it decides whether to grey these two items.
//   Spelled out a second time, the greying and the setting would be two answers to one question.
//
// **Setting the same document as both is allowed.** The reader may well want to point at one
// document twice while working out which is which; what refuses is the Start
// (KCMToggleStartStop), where a comparison of a document against itself is meaningless.
bool16 KCMSetChosenTargetToActive()
{
	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
		return kFalse;			// the flyout greys the item in this case; this guards a document closing while the menu stands open
	sChosenTargetDB = db;
	return kTrue;
}

bool16 KCMSetChosenSourceToActive()
{
	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
		return kFalse;
	sChosenSourceDB = db;
	return kTrue;
}

// KCMForgetChosenDocsThatClosed (declared in KCMComparisonRun.h) -- the close sweep's half of
// the rule. **Each choice is judged on its own**, so closing one of the two documents leaves the
// other one chosen: that is the whole point of stating the pair rather than inferring it. The
// pointers are compared, never dereferenced.
void KCMForgetChosenDocsThatClosed(IDocumentList* docList)
{
	if (docList == nil)
		return;					// no way to judge liveness; the choices stay, and KCMLiveChosenDoc still guards every read
	if (sChosenTargetDB != nil && docList->FindDocByDataBase(sChosenTargetDB) == nil)
		sChosenTargetDB = nil;
	if (sChosenSourceDB != nil && docList->FindDocByDataBase(sChosenSourceDB) == nil)
		sChosenSourceDB = nil;
}

// KCMClearChosenDocs (declared in KCMComparisonRun.h) -- the model's Shutdown drops both, in the
// same slot and for the same reason as the peek's armed state (KCMPeekStartup::Shutdown): left
// standing, a kAfterCloseDoc responder arriving after shutdown reaches
// KCMForgetChosenDocsThatClosed and weighs a stale pointer against the live document list. The
// normal order -- documents close, then Shutdown -- should never allow that, so this is
// defensive. Assignment only, nothing dereferenced, and idempotent, so it is safe at any point in
// the shutdown sequence.
void KCMClearChosenDocs()
{
	sChosenTargetDB = nil;
	sChosenSourceDB = nil;
}

// The first open document that is not `target` = the Source (the older version).
//
// ★**"First" is IDocumentList's order, which is the order the documents were OPENED -- and it is
//   NOT the order scripting reports.** app.documents is most-recently-active first, so a test
//   written against the DOM predicts the wrong Source. Measured 2026-08-31: with the DOM listing
//   third / new / old and `third` chosen as the Target, this returned `old` -- the one opened
//   earliest of the remaining two, where the DOM's own "first other" would have been `new`.
//   [[document-activation-is-presentation]] is the same trap for "which document is in front";
//   this is its ordering half.
//
// ⚠**`d != target` is a pointer comparison on purpose, and KCMIsSameDoc is deliberately NOT used
//   here.** That function answers "are these two databases one document" -- the question for a
//   pair that reached the caller by two different roads (KCMToggleStartStop, where a clone
//   database is possible). Here both sides come off the SAME IDocumentList within one call, so
//   the question is not identity but "skip this element", and one document has one IDocument*.
//   Measured the same day: a Target chosen through FindDocByDataBase was correctly skipped by the
//   pointer GetNthDoc handed back. ⇒ Two comparisons, two questions; do not fold them into one.
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
//
// **A chosen document wins; an unchosen one falls to the old rule.** Choosing neither leaves
// the behaviour exactly as it was before the two flyout items existed, and choosing one leaves
// the other to be worked out -- "Set as Source" on the older version, then Start from whichever
// document is in front, is a perfectly good way to work.
//
// @warning **the automatic Source is still "the first document that is not the Target"**, and
// the Target may now be a document that is not in front. That is what makes the pair right:
// picking "not the active document" instead would let the chosen Target be handed to itself as
// the Source the moment the reader brought a third document forward.
//
// **Whether the two come out the same is not decided here.** This answers "which two", and the
// menu's grey state rests on it; "are they the same document" is a different question with a
// different answer (a message, not a grey item), and it is asked once, in KCMToggleStartStop
// ([[one-question-one-place]] is kept by having each question in one place, not by folding two
// questions into one function).
static bool16 KCMResolveComparisonPair(IDocument*& outTarget, IDocument*& outSource)
{
	outTarget = KCMLiveChosenDoc(sChosenTargetDB);
	if (outTarget == nil)
		outTarget = KCMActiveDoc();

	outSource = KCMLiveChosenDoc(sChosenSourceDB);
	if (outSource == nil)
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
	if (KCMIsSameDoc(::GetUIDRef(target).GetDataBase(), ::GetUIDRef(source).GetDataBase()))
	{
		KCMSayStatus("Target and Source are the same document. Bring another document to the front, or set one of them to another document.");
		// As in the branch above: this one returns without reaching the end of the function, so it
		// asks for the panel refresh itself. No document travels with it -- nothing has changed but
		// what the status line says.
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
