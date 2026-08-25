//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  Book comparison: what a row of the chapter list DOES. See KCMBookOpen.h for the contract.
//
//  ⚠ THE OPEN HERE IS THE OPPOSITE OF THE COMPARISON'S. KCMBookCompare.cpp opens chapters
//  windowless and UI-suppressed, because nobody is meant to see them and every one is closed again.
//  These opens are FOR the user: a window, and full UI - a chapter with a missing font or a broken
//  link must raise its alert here, where the person who asked for it is looking at the screen.
//  Nothing opened here is ever closed by this plug-in.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"			// the window-opening command, for a chapter that is open windowless
#include "IDataBase.h"			// GetSysFile - a document's identity is its FILE, never its UID
#include "IDocument.h"
#include "IDocumentCommands.h"	// Open - with a window, and with the UI left on
#include "IDocumentList.h"		// FindDoc - is this chapter already open?
#include "IDocumentPresentation.h"	// MakeActive - raise the window a chapter already has
#include "IDocumentUIUtils.h"	// FindPresentationForDocument - HAS this chapter a window at all?
#include "IGlobalRecompose.h"	// ForceRecompositionToComplete - see ComposeChapter
#include "IOpenLayoutCmdData.h"	// GetResultingPresentation - did the window actually appear?
#include "ISession.h"
#include "IWindow.h"			// the window kOpenLayoutCmdBoss is supposed to have produced

// General includes:
#include "CmdUtils.h"			// CreateCommand / ProcessCommand
								// (*"CreateObject.h" was included here and never used - the only
								//  command this file makes comes from CmdUtils::CreateCommand.
								//  Dropped 2026-08-17, B-U9. It was also the one include in this
								//  list with no note saying what it was for.)
#include "ErrorUtils.h"			// GlobalErrorStatePreserver - an open that may fail must not poison
								// the caller's next command
#include "LayoutUIID.h"			// kOpenLayoutCmdBoss - a chapter's first window
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "SDKFileHelper.h"		// GetPath - a chapter the book names no file for
#include "UIDList.h"			// the open-window command's item list
#include "Utils.h"				// Utils<IDocumentCommands>() / Utils<IDocumentUIUtils>() - named
								// rather than relied on through another header, as this plug-in's
								// other files do

#include <vector>

// Project includes:
#include "KCMBookDialog.h"	// KCMBookDialogRows - the model the list is drawn from
#include "KCMBookOpen.h"
#include "KCMBookResult.h"
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "IKCMCompareFacade.h"	// Stop / StartComparisonFor / the armed state

namespace
{

/** Which row the context menu was popped over. -1 = none.

    Module scope for the same reason the double-click flag in the row handler is: it belongs to
    "the interaction going on right now", and only one is ever going on. */
int32 gMenuRow = -1;

const KCMChapterResult* RowAt(int32 rowIndex)
{
	const std::vector<KCMChapterResult>& rows = KCMBookDialogRows();
	if (rowIndex < 0 || rowIndex >= static_cast<int32>(rows.size()))
		return nil;
	return &rows[rowIndex];
}

/** Does the book name a file for this side of the chapter? */
bool16 FileIsNamed(const IDFile& file)
{
	SDKFileHelper helper(file);
	return helper.GetPath().empty() ? kFalse : kTrue;
}

/** Does this open document live in that file?

    ***** A document's identity is its FILE. ***** Same check, and the same reason, as
    KCMBookCompare.cpp's: an answer about "the document for this file" that is not verified
    against the file can put a DIFFERENT chapter in a chapter's place, and go on working. Written
    out again rather than shared because that one lives in an anonymous namespace; two copies of six
    lines is a smaller risk than a header that exports it.

    ⚠ ASKING GetSysFile IS RIGHT HERE, THOUGH IT WAS WRONG SOMEWHERE ELSE. Bug recheck B9
    (2026-08-18) found KCMIsSameDoc identifying documents this way and failing for every UNSAVED
    one - GetSysFile answers nil, so the test was always false - and moved it to
    IDataBase::GetDocumentID. This function cannot meet that case: it is only ever asked about a
    document IDocumentList::FindDoc(file) just handed back, so a file is what it was found by. The
    question here is "is this the document for THAT FILE", not "are these two documents the same
    one", and only the second of those has to work without a file. (Noted 2026-08-18, B-U5: the
    shapes are alike enough that the next reader will wonder.) */
bool16 DocumentLivesInFile(IDocument* doc, const PMString& wantedPath)
{
	if (doc == nil)
		return kFalse;

	IDataBase* db = ::GetDataBase(doc);
	if (db == nil)
		return kFalse;

	const IDFile* docFile = db->GetSysFile();
	if (docFile == nil)
		return kFalse;

	SDKFileHelper helper(*docFile);
	return (helper.GetPath() == wantedPath) ? kTrue : kFalse;
}

/** Accept any presentation at all.

    ***** WHY A PREDICATE OF OUR OWN. ***** The stock criteria (is_active, is_layout, …) live in the
    WidgetBin shared library, and DocumentPresFindCriteria.h:40-46 says so, telling callers that
    cannot link it to "create local implementations" and printing this exact two-line shape. KBS and
    KESCL each carry the same one for the same reason (KBSBookScope.cpp:234 / KESCLFindInDoc.cpp:676).
    ⚠ That sentence was attributed to IDocumentUIUtils.h until 2026-08-17 (audit B-U5). It is the
    OTHER header - the one the criteria themselves live in - and both files happen to have something
    at 40-46, which is why the wrong name read as checkable and survived.
    ★KBS states it correctly (KBSBookScope.cpp:226-233 names DocumentPresFindCriteria.h:82 for the
      stock predicate and then says "that file's own preamble (:40-46)"), so this was KCM losing
      the citation in the copy, not a rule the two disagree about. */
bool KCMAcceptAnyPresentation(IDocumentPresentation* /*p*/)
{
	return true;
}

/** Make sure this chapter is IN FRONT - and that it has a window at all.

    ***** OPENING A DOCUMENT THAT IS ALREADY OPEN NEITHER RAISES IT NOR WINDOWS IT. ***** Two
    separate faults lived here, and one route closes both:

      - a chapter already open behind another tab stayed exactly where it was, so a double click on
        its row appeared to do nothing: the status line said "2 documents open" and the screen did
        not change;
      - IDocumentList::FindDoc answers for a WINDOWLESS document too - which is precisely what the
        book comparison leaves behind when its own CloseChapter fails (its `leftOpen` counter). Such
        a chapter was reported as open with nothing on screen, and "Start Change Marker" on it armed
        a comparison whose marks had no window to be drawn in.

    ★FindPresentationForDocument, not GetFrontmostPresentationForDocument: the latter answers nil for
    a window sitting behind another tab, which would send us on to open a SECOND window for a
    document that already has one. Both sibling plug-ins learned this and say so
    (KESCLFindInDoc.cpp:716-718 / KBSBookScope.cpp:239-241).

    ⚠ The comment that used to be at the double-click's status line claimed there was "no published
    route from a document to its window … IWindowUtils has nothing either (searched 2026-08-12)".
    That was wrong: the search had covered IDocument, ILayoutUIUtils and IWindowUtils but not
    IDocumentUIUtils, which is where it is (IDocumentUIUtils.h:88 / IDocumentPresentation.h:112) and
    where KBS and KESCL were already using it in FIVE places - KBSBookScope, KBSJump, KESCLBookScope
    (twice) and KESCLFindInDoc. ⚠"four" until the callers were actually counted (2026-08-18, bug
    recheck B-U5); named by file rather than by line so the count can be re-run and cannot rot. */
bool16 BringChapterToFront(const UIDRef& docRef)
{
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return kFalse;

	FindPresentation_PreferCriteria noPreference;	// the first window found is fine
	IDocumentPresentation* pres = Utils<IDocumentUIUtils>()->FindPresentationForDocument(
		db, KCMAcceptAnyPresentation, noPreference);
	if (pres != nil)
	{
		// ★NO GlobalErrorStatePreserver ON THIS BRANCH, AND THE ASYMMETRY WITH THE ONE BELOW IS
		//   DELIBERATE (checked 2026-08-18, bug recheck B-U5 second pass - the next reader will see
		//   two branches of one function guarding different amounts and wonder). MakeActive raises
		//   no command of ours to fail: it returns void and IDocumentPresentation.h promises nothing
		//   about the error state, and BOTH sibling plug-ins call it bare in the same shape
		//   (KBSJump.cpp / KESCLFindInDoc.cpp, each with the open-a-window branch guarded and this
		//   one not). The branch below is guarded because it PROCESSES A COMMAND, which is the thing
		//   that can leave an error standing. ⚠ Not measured - argued from the header and from three
		//   implementations agreeing; if MakeActive is ever seen to leave an error up, this is where
		//   the guard goes.
		pres->MakeActive();
		return kTrue;
	}

	// No window at all: the chapter is open windowless. Give it its first layout window, which also
	// makes it active - the shape KESCL uses (KESCLFindInDoc.cpp:731-747), without the zoom it
	// inherits from the front view, because this path has no view to inherit one from.
	//
	// ***** THE WINDOW IS ALLOWED NOT TO APPEAR - this function's kFalse says so - so its error state
	// stays in here. ***** Placed ahead of the command rather than around the processing alone, so
	// that all FOUR ways this can end without a window are covered: the command that would not
	// build, the one that has no data interface, the one that failed, and the one that succeeded
	// without producing a window.
	// ⚠ It said THREE until 2026-08-18 (bug recheck B-U5). The fourth was added the day before, by
	//   the B-U5 audit, at the foot of this very function - and the sentence counting them, four
	//   lines above the command, was not re-read. Same shape as the miscounts B6 and B10 found.
	GlobalErrorStatePreserver openWinErrorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	InterfacePtr<ICommand> openWinCmd(CmdUtils::CreateCommand(kOpenLayoutCmdBoss));
	if (openWinCmd == nil)
		return kFalse;
	openWinCmd->SetItemList(UIDList(docRef));

	// ***** NO DATA INTERFACE = FAILURE, AND THE COMMAND IS NOT RUN AT ALL. ***** Taken BEFORE
	// processing, because the answer is read back off it afterwards. Nothing is set on it - the
	// defaults are what a chapter window should get. The SDK's own recipe for this command breaks off
	// at exactly this point too (SDKLayoutHelper.cpp:268-272).
	InterfacePtr<IOpenLayoutPresentationCmdData> openData(openWinCmd, IID_IOPENLAYOUTCMDDATA);
	if (openData == nil)
		return kFalse;

	if (CmdUtils::ProcessCommand(openWinCmd) != kSuccess)
		return kFalse;		// whatever it raised goes back with the preserver above

	// ***** "THE COMMAND SUCCEEDED" AND "THERE IS A WINDOW" ARE TWO DIFFERENT STATEMENTS. *****
	// SDKLayoutHelper::OpenLayoutWindow does not stop at the return code either - it reads
	// GetResultingPresentation() and checks that an IWindow comes out of it (SDKLayoutHelper.cpp:282-287,
	// "If we couldn't get an IWindow the postconditions won't be met").
	// ★WHY IT MATTERS HERE IN PARTICULAR: this kTrue is counted into the caller's status line as
	//   "N documents open", which is the one claim this whole function exists to make true. Reporting
	//   a window that is not there is the same fault it was written to fix - a chapter open WINDOWLESS
	//   being reported as open with nothing on screen.
	// ⚠ Added 2026-08-17 (audit B-U5). KBS has had both steps since its own block 11 audit
	//   (2026-08-08, KBSBookScope::ShowChapterWindow) - this file was ported from it before that, and
	//   the correction did not travel with the copy.
	InterfacePtr<IWindow> window(openData->GetResultingPresentation(), UseDefaultIID());
	return (window != nil) ? kTrue : kFalse;
}

/** Finish composing a chapter before the comparison reads its pixels.

    ***** A DOCUMENT THAT HAS JUST BEEN OPENED IS NOT COMPOSED YET. ***** Rasterising it in that
    state paints composition in progress, and two chapters with identical content then come out
    different. KESHR measured the same thing as "identical content gave different hashes".

    ★★WHY THIS IS NEEDED HERE, WHEN THE PANEL'S OWN Start DOES NOT DO IT. KCMBookCompare.cpp's
    RecomposeChapter states the reason the document comparison never had to: "it only ever rasterises
    documents the user has open on screen, which are composed by the time anyone asks". ***THAT
    PREMISE IS THE ONE THIS FILE BROKE*** - "Start Change Marker" hands KCMStartComparisonFor two
    chapters opened a moment earlier. So the call the book comparison makes before every chapter has
    to be made here too, on the same two documents, before the comparison starts.
    ⚠ Named by function rather than by line (2026-08-18, bug recheck B-U5): the range written here
    pointed twenty lines short, at CloseChapter's kProcess note. ★The audit that put the two
    references at the foot of this file onto function names counted two of them - this was the
    THIRD, in the same file and of the same kind, and it stayed a number.

    ⚠ MakeEntry deliberately does NOT do this - it can be reached from inside a draw event, where
    recomposing would re-enter - so it cannot be fixed down there. It belongs to whoever opened the
    documents. This path is a menu command, so it is safe here.

    Six lines written out rather than shared: the book comparison's copy lives in an anonymous
    namespace, and this file already keeps its own DocumentLivesInFile for the same reason. */
void ComposeChapter(const UIDRef& docRef)
{
	InterfacePtr<IDocument> doc(docRef, UseDefaultIID());
	if (doc == nil)
		return;

	InterfacePtr<IGlobalRecompose> recompose(doc, IID_IGLOBALRECOMPOSE);
	if (recompose != nil)
		recompose->ForceRecompositionToComplete();
}

/** Open a chapter WITH A WINDOW, or hand back the one that is already open.

    outWasAlreadyOpen separates "it is on screen because of this click" from "it was there all
    along" - the two need different words in the status line, and the caller is the only one that
    knows which of them matters. */
bool16 OpenChapterWindowed(const IDFile& file, UIDRef& outDocRef, bool16& outWasAlreadyOpen)
{
	outDocRef         = UIDRef::gNull;
	outWasAlreadyOpen = kFalse;

	SDKFileHelper fileHelper(file);
	const PMString wantedPath = fileHelper.GetPath();
	if (wantedPath.empty())
		return kFalse;

	{
		ISession* session = GetExecutionContextSession();
		InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
		if (docList != nil)
		{
			IDocument* openDoc = docList->FindDoc(file);
			if (DocumentLivesInFile(openDoc, wantedPath))
			{
				outDocRef         = ::GetUIDRef(openDoc);
				outWasAlreadyOpen = kTrue;
				// ***** Raise it - and window it if it has none. ***** Without this, a double click
				// on a chapter that was already open did nothing a user could see, and a chapter
				// open WINDOWLESS was reported as open with nothing on screen. Both are one route:
				// see BringChapterToFront.
				return BringChapterToFront(outDocRef);
			}
		}
	}

	// ***** THE DEFAULTS ARE THE POINT HERE: kFullUI and showInWindow = kTrue *****
	// (IDocumentCommands.h:91-95). The comparison's open spells out kSuppressUI / kFalse because it
	// works behind the user's back; this one is the user's own request, so a chapter that needs to
	// say something on opening is allowed to say it.
	//
	// The open is still allowed to FAIL without taking the next command with it: the caller reports
	// a chapter that would not open, and an error left standing would fail whatever runs next
	// (ErrorUtils.h:115-117; the SDK's own shape for this is
	// buttonui/actiondatapanels/gotoanchor/GoToAnchorPanelObserver.cpp:395-401).
	UIDRef    docRef;
	ErrorCode err = kFailure;
	{
		GlobalErrorStatePreserver openErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		err = Utils<IDocumentCommands>()->Open(&docRef, file);
	}

	if (err != kSuccess || docRef == UIDRef::gNull)
		return kFalse;

	outDocRef = docRef;
	// A fresh open is in front already; raising it anyway is what makes the caller's rule - "the
	// TARGET is opened last, so it is the one left in front" - true for BOTH sides. Before this, a
	// pair whose chapters were both already open left whichever window happened to be frontmost.
	BringChapterToFront(docRef);
	return kTrue;
}

/** Put a message in the panel's status line, untranslated (as every string this plug-in shows is).

    ⚠★★IT GOES TO THE PANEL, THOUGH THE CLICK HAPPENED IN THE DIALOG - AND THE DIALOG HAS A STATUS
    LINE OF ITS OWN. That line belongs to the BOOK comparison ("3 chapters: 1 changed…"); these
    messages are about one chapter, and overwriting the book's summary with them would leave the
    dialog describing a run it is no longer showing the numbers for. So the two stay apart, and the
    panel is where a row's outcome is reported.

    ⚠WHAT THAT COSTS, MEASURED 2026-08-19 (bug recheck B-U5, third pass): KCMSetStatus returns
    silently when the panel is not visible (KCMPanelObserver.cpp - "the panel is hidden: there is
    nothing to touch"), so with the panel closed or behind another tab a double click on a chapter that cannot
    be opened reports NOTHING ANYWHERE ON SCREEN. The measurement with the panel open: the row was
    double-clicked, one side opened, and the panel read
        ch3.indd: 1 document open, 1 could not be opened
    while the dialog's own status line still read the book summary, unchanged.
    ★The text is never lost even then - KCMSetStatus stores it in the session first, which is what
      app.kcmStatus returns - so this is a matter of where it is SHOWN, not of whether it exists.
      Left as it is (user's call, 2026-08-19); if a row's outcome ever has to be readable with the
      panel hidden, the place to put it is the dialog's status line, plus a rule for restoring the
      summary afterwards. */
void Say(const PMString& text)
{
	PMString msg(text);
	msg.SetTranslatable(kFalse);
	KCMSetStatus(msg);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// Which row the menu belongs to
//----------------------------------------------------------------------------------------

void KCMBookSetMenuRow(int32 rowIndex)
{
	gMenuRow = rowIndex;
}

int32 KCMBookMenuRow()
{
	return gMenuRow;
}

bool16 KCMBookRowCanStart(int32 rowIndex)
{
	const KCMChapterResult* row = RowAt(rowIndex);
	if (row == nil)
		return kFalse;

	// ***** CHANGED CHAPTERS ONLY (user's call, 2026-08-12: "on an Add chapter nothing should come
	// up on right-click - only on Change chapters"). *****
	// The item exists to answer "where did it change?", so it is offered exactly where that question
	// has an answer:
	//   ChapterAdded / ChapterDeleted … one side only - there is nothing to compare against
	//   NoChange                      … already answered; starting would draw no marks at all
	//   Failed / NotCompared          … this chapter was never judged, so "where" has no meaning yet
	// ⚠ Measured 2026-08-12: when every item in a popup is disabled, InDesign does not show the menu
	// at all - so on those rows the right click produces nothing, which is exactly what was asked
	// for. (Do not "improve" this into an empty menu that appears and does nothing.)
	if (row->fState != kKCMChapterChanged)
		return kFalse;

	// Both files all the same - a Changed verdict implies them, and this is the check that the
	// comparison itself would need. Cheap, and it keeps the guarantee local.
	return (FileIsNamed(row->fTargetFile) && FileIsNamed(row->fSourceFile)) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// Double click - open that chapter
//----------------------------------------------------------------------------------------

void KCMBookOpenChapterForRow(int32 rowIndex)
{
	const KCMChapterResult* row = RowAt(rowIndex);
	if (row == nil)
		return;

	// ***** COPY THE ROW OFF BEFORE OPENING ANYTHING. ***** RowAt hands back a pointer INTO
	// gDialogRows, and that vector is REBUILT WHOLE when a comparison finishes - cleared and refilled
	// in KCMBookDialogSetResult (KCMBookDialog.cpp) - which frees every element the old one held.
	// Opening a document is not a moment this file controls: it is long, and it is the caller's own
	// menu command, so the safe assumption is that anything can have happened by the time it returns.
	// fName was already taken by value here (why, the old code does not say); the two IDFiles were not,
	// and they were being read AFTER the first open. Three values, copied once - the cheapest possible
	// version of "do not hold a pointer across a call that can invalidate it".
	const PMString name       = row->fName;
	const IDFile   targetFile = row->fTargetFile;
	const IDFile   sourceFile = row->fSourceFile;
	// ★row is not touched again below this line.

	const bool16   hasTarget  = FileIsNamed(targetFile);
	const bool16   hasSource  = FileIsNamed(sourceFile);

	if (!hasTarget && !hasSource)
	{
		PMString msg(name);
		msg.Append(": the book gives no file for this chapter");
		Say(msg);
		return;
	}

	// ***** BOTH SIDES (user's call, 2026-08-12: "can the double click open the source document as
	// well, not only the target?"). ***** A row of this list is a statement about a PAIR - "this
	// chapter changed" - so the thing a reader wants in front of them is the pair. Opening only the
	// target left them to find the other half through the book panel by hand.
	// ★A chapter that exists on one side only (ChapterAdded / ChapterDeleted) simply has one side to
	// open; that is not a failure and is not reported as one.
	// ★NO SaveRestoreModifiedState HERE, AND THAT IS MEASURED RATHER THAN ASSUMED (2026-08-18, bug
	//   recheck B-U5). "Start Change Marker" wraps its work in that guard - it RECOMPOSES, which
	//   touches the document - while this path only opens windows, so there is nothing to undo. It
	//   was checked in both places it could show up: after a double click the two chapters read
	//   modified=false AND SO DID BOTH BOOKS. Opening a chapter of a book is exactly the kind of
	//   thing that could mark the .indb (it holds each chapter's page numbering), so the books were
	//   read too. ⚠ If anything on this path ever starts writing to the documents, this needs the
	//   guard - do not conclude from here that it never will.
	// ⚠ The TARGET IS OPENED LAST, so it is the one InDesign leaves in front - the target is the
	// version being checked, and it is what the panel's own comparison calls Target too.
	int32  opened = 0, already = 0, failed = 0;
	UIDRef docRef;
	bool16 wasOpen = kFalse;

	if (hasSource)
	{
		if (OpenChapterWindowed(sourceFile, docRef, wasOpen)) { if (wasOpen) ++already; else ++opened; }
		else ++failed;
	}
	if (hasTarget)
	{
		if (OpenChapterWindowed(targetFile, docRef, wasOpen)) { if (wasOpen) ++already; else ++opened; }
		else ++failed;
	}

	// ★An already-open chapter IS raised now, and windowed if it had no window (2026-08-12). The
	// route is Utils<IDocumentUIUtils>()->FindPresentationForDocument + MakeActive - see
	// BringChapterToFront, which also records why the claim that used to stand here ("there is no
	// published route from a document to its window") was wrong.
	// ∴ "N documents open" now means N windows the user can actually look at.
	PMString msg;
	if (opened == 0 && already == 0)
	{
		msg = PMString("could not open ");
		msg.Append(name);
	}
	else
	{
		msg = name;
		msg.Append(": ");
		msg.AppendNumber(opened + already);
		msg.Append((opened + already) == 1 ? " document open" : " documents open");
		if (failed > 0)
		{
			msg.Append(", ");
			msg.AppendNumber(failed);
			msg.Append(" could not be opened");
		}
	}
	Say(msg);
}

//----------------------------------------------------------------------------------------
// Context menu - open both sides and compare them
//----------------------------------------------------------------------------------------

void KCMBookStartComparisonForRow(int32 rowIndex)
{
	// The menu item is greyed when this is false, so reaching it means the list changed under an
	// open menu. Refusing here rather than opening what is left is the same choice the panel's Start
	// makes for a vanished source document.
	if (!KCMBookRowCanStart(rowIndex))
		return;

	const KCMChapterResult* row = RowAt(rowIndex);
	if (row == nil)
		return;

	// Copied off the row before the first open, for the reason spelled out in
	// KCMBookOpenChapterForRow: gDialogRows is replaced whole when a comparison finishes, and this
	// function reads the SECOND file after the first document has already been opened.
	const PMString name       = row->fName;
	const IDFile   targetFile = row->fTargetFile;
	const IDFile   sourceFile = row->fSourceFile;
	// ★row is not touched again below this line.

	UIDRef targetRef, sourceRef;
	bool16 targetWasOpen = kFalse, sourceWasOpen = kFalse;
	if (!OpenChapterWindowed(targetFile, targetRef, targetWasOpen))
	{
		PMString msg("could not open the target side of ");
		msg.Append(name);
		Say(msg);
		return;
	}
	if (!OpenChapterWindowed(sourceFile, sourceRef, sourceWasOpen))
	{
		// ⚠ The target side stays open. It was opened at the user's request and holds no state of
		// this run; closing it again would undo something they asked for to tidy up after something
		// they did not.
		PMString msg("could not open the source side of ");
		msg.Append(name);
		Say(msg);
		return;
	}

	// ***** STOP FIRST. ***** A comparison already running belongs to a different pair of documents,
	// and its marks are drawn from state this plug-in keeps for exactly one pair
	// (KCMDrawEventHandler's statics). Arming a second pair on top of it would leave marks on
	// documents nobody is comparing any more. The user asked for "start on this chapter", and
	// stopping the previous one is part of that (their words, 2026-08-12: "if it is already started,
	// stop and start").
	if (Utils<IKCMCompareFacade>()->IsArmed() && Utils<IKCMCompareFacade>()->GetArmedTargetDB() != nil)
		Utils<IKCMCompareFacade>()->StopComparison();

	// Held for the length of the call: KCMStartComparisonFor takes raw IDocument pointers, and
	// these references are what keep them alive while it runs.
	InterfacePtr<IDocument> targetDoc(targetRef, UseDefaultIID());
	InterfacePtr<IDocument> sourceDoc(sourceRef, UseDefaultIID());
	if (targetDoc == nil || sourceDoc == nil)
	{
		PMString msg("could not read the documents for ");
		msg.Append(name);
		Say(msg);
		return;
	}

	// ***** COMPOSE BOTH SIDES FIRST - AND DO NOT DIRTY THEM DOING IT. *****
	// These two chapters were opened moments ago, so their composition is not finished, and comparing
	// them in that state rasterises composition in progress. The book comparison does exactly this
	// before every chapter, wrapped exactly this way - KCMCompareBooks' chapter loop, the two
	// SaveRestoreModifiedState guards it puts around RecomposeChapter; ComposeChapter records why the
	// panel's own Start can skip it and this path cannot.
	// ⚠ The guards matter as much as the compose: composing touches the document, and a chapter the
	//   user only asked to LOOK at must not start asking to be saved. "Do not dirty it" means "if it
	//   was clean going in, it is clean coming out" - the rule KCMDoMarkChangesDoc already applies
	//   to the comparison itself.
	// ★Both references name FUNCTIONS rather than line numbers (2026-08-17, audit B-U5): the line
	//   numbers they used to carry were correct when written and were moved by later edits to those
	//   very files - the B8 audit shifted the guards by twenty lines the same week.
	{
		IDataBase* targetDB = targetRef.GetDataBase();
		IDataBase* sourceDB = sourceRef.GetDataBase();
		if (targetDB != nil && sourceDB != nil)
		{
			IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
			IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

			ComposeChapter(targetRef);
			ComposeChapter(sourceRef);
		}
	}

	// ***** THE TARGET GOES IN FRONT, AND IT HAS TO BE RAISED LAST TO GET THERE. *****
	// This function opens TARGET FIRST on purpose - a target that will not open must cost nothing on
	// the source side - so the side raised last is the SOURCE, and that is what the user would be left
	// looking at. ⚠ MEASURED 2026-08-12: without this line the active document after "Start Change
	// Marker" was old/ch01.indd. The marks are drawn on the target, so the target is the window that
	// has to be in front.
	// ★The double click has the opposite order (source, then target) and therefore needs no such line;
	//   both paths end with the target in front, by different means. Do not "tidy" the two into the
	//   same order without re-reading why this one opens the target first.
	BringChapterToFront(targetRef);

	// ★The status line is left to the comparison itself - it ends by writing its own report there
	// (page counts, failures), which is more use than anything this function could add.
	Utils<IKCMCompareFacade>()->StartComparisonFor(targetDoc, sourceDoc);
}

// End, KCMBookOpen.cpp.
