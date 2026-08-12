//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: what a row of the chapter list DOES. See KESCMBookOpen.h for the contract.
//
//  ⚠ THE OPEN HERE IS THE OPPOSITE OF THE COMPARISON'S. KESCMBookCompare.cpp opens chapters
//  windowless and UI-suppressed, because nobody is meant to see them and every one is closed again.
//  These opens are FOR the user: a window, and full UI - a chapter with a missing font or a broken
//  link must raise its alert here, where the person who asked for it is looking at the screen.
//  Nothing opened here is ever closed by this plug-in.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// GetSysFile - a document's identity is its FILE, never its UID
#include "IDocument.h"
#include "IDocumentCommands.h"	// Open - with a window, and with the UI left on
#include "IDocumentList.h"		// FindDoc - is this chapter already open?
#include "ISession.h"

// General includes:
#include "ErrorUtils.h"			// GlobalErrorStatePreserver - an open that may fail must not poison
								// the caller's next command
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "SDKFileHelper.h"		// GetPath - a chapter the book names no file for
#include "Utils.h"				// Utils<IDocumentCommands>() - named rather than relied on through
								// another header, as this plug-in's other files do

#include <vector>

// Project includes:
#include "KESCMBookDialog.h"	// KESCMBookDialogRows - the model the list is drawn from
#include "KESCMBookOpen.h"
#include "KESCMBookResult.h"
#include "KESCMCore.h"			// KESCMSetStatus / KESCMIsArmed / KESCMStopComparison /
								// KESCMStartComparisonFor / KESCMArmedTargetDB

namespace
{

/** Which row the context menu was popped over. -1 = none.

    Module scope for the same reason the double-click flag in the row handler is: it belongs to
    "the interaction going on right now", and only one is ever going on. */
int32 gMenuRow = -1;

const KESCMChapterResult* RowAt(int32 rowIndex)
{
	const std::vector<KESCMChapterResult>& rows = KESCMBookDialogRows();
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
    KESCMBookCompare.cpp's: an answer about "the document for this file" that is not verified
    against the file can put a DIFFERENT chapter in a chapter's place, and go on working. Written
    out again rather than shared because that one lives in an anonymous namespace; two copies of six
    lines is a smaller risk than a header that exports it. */
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
				return kTrue;
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
	return kTrue;
}

/** Put a message in the panel's status line, untranslated (as every string this plug-in shows is). */
void Say(const PMString& text)
{
	PMString msg(text);
	msg.SetTranslatable(kFalse);
	KESCMSetStatus(msg);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// Which row the menu belongs to
//----------------------------------------------------------------------------------------

void KESCMBookSetMenuRow(int32 rowIndex)
{
	gMenuRow = rowIndex;
}

int32 KESCMBookMenuRow()
{
	return gMenuRow;
}

bool16 KESCMBookRowCanStart(int32 rowIndex)
{
	const KESCMChapterResult* row = RowAt(rowIndex);
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
	if (row->fState != kKESCMChapterChanged)
		return kFalse;

	// Both files all the same - a Changed verdict implies them, and this is the check that the
	// comparison itself would need. Cheap, and it keeps the guarantee local.
	return (FileIsNamed(row->fTargetFile) && FileIsNamed(row->fSourceFile)) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// Double click - open that chapter
//----------------------------------------------------------------------------------------

void KESCMBookOpenChapterForRow(int32 rowIndex)
{
	const KESCMChapterResult* row = RowAt(rowIndex);
	if (row == nil)
		return;

	const PMString name        = row->fName;
	const bool16   hasTarget   = FileIsNamed(row->fTargetFile);
	const bool16   hasSource   = FileIsNamed(row->fSourceFile);

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
	// ⚠ The TARGET IS OPENED LAST, so it is the one InDesign leaves in front - the target is the
	// version being checked, and it is what the panel's own comparison calls Target too.
	int32  opened = 0, already = 0, failed = 0;
	UIDRef docRef;
	bool16 wasOpen = kFalse;

	if (hasSource)
	{
		if (OpenChapterWindowed(row->fSourceFile, docRef, wasOpen)) { if (wasOpen) ++already; else ++opened; }
		else ++failed;
	}
	if (hasTarget)
	{
		if (OpenChapterWindowed(row->fTargetFile, docRef, wasOpen)) { if (wasOpen) ++already; else ++opened; }
		else ++failed;
	}

	// ⚠ An already-open chapter is NOT re-opened and NOT brought forward. There is no published
	// route from a document to its window - IDocument has no presentation accessor, ILayoutUIUtils
	// only answers for the FRONTMOST one, and IWindowUtils has nothing either (searched 2026-08-12).
	// Rather than guess at one, this says what the state is; the document is in the Window menu.
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

void KESCMBookStartComparisonForRow(int32 rowIndex)
{
	// The menu item is greyed when this is false, so reaching it means the list changed under an
	// open menu. Refusing here rather than opening what is left is the same choice the panel's Start
	// makes for a vanished source document.
	if (!KESCMBookRowCanStart(rowIndex))
		return;

	const KESCMChapterResult* row = RowAt(rowIndex);
	if (row == nil)
		return;

	const PMString name = row->fName;

	UIDRef targetRef, sourceRef;
	bool16 targetWasOpen = kFalse, sourceWasOpen = kFalse;
	if (!OpenChapterWindowed(row->fTargetFile, targetRef, targetWasOpen))
	{
		PMString msg("could not open the target side of ");
		msg.Append(name);
		Say(msg);
		return;
	}
	if (!OpenChapterWindowed(row->fSourceFile, sourceRef, sourceWasOpen))
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
	// (KESCMDrawEventHandler's statics). Arming a second pair on top of it would leave marks on
	// documents nobody is comparing any more. The user asked for "start on this chapter", and
	// stopping the previous one is part of that (their words, 2026-08-12: "if it is already started,
	// stop and start").
	if (KESCMIsArmed() && KESCMArmedTargetDB() != nil)
		KESCMStopComparison();

	// Held for the length of the call: KESCMStartComparisonFor takes raw IDocument pointers, and
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

	// ★The status line is left to the comparison itself - it ends by writing its own report there
	// (page counts, failures), which is more use than anything this function could add.
	KESCMStartComparisonFor(targetDoc, sourceDoc);
}

// End, KESCMBookOpen.cpp.
