//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: running it. See KESCMBookCompare.h for the contract.
//
//  The open/close machinery is ported from KBS (KBSBookScope::ReopenChapterDoc and its close),
//  which arrived at its present shape through several measured faults - each one is named at the
//  line it guards, because none of them is guessable from the API alone.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// GetSysFile - a document's identity is its FILE, never its UID
#include "IDocFileHandler.h"	// Close / CanClose - the windowless, UI-suppressed close
#include "IDocument.h"
#include "IDocumentCommands.h"	// Open by file, windowless
#include "IDocumentList.h"		// FindDoc - is this chapter already open?
#include "IDocumentUtils.h"		// QueryDocFileHandler
#include "IOpenFileCmdData.h"	// kOpenDefault / kUseLockFile
#include "ISession.h"

// General includes:
#include "ErrorUtils.h"			// GlobalErrorStatePreserver - an open or a close that is allowed to
								// fail must not poison the caller's next command
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "SDKFileHelper.h"		// GetPath - documents are compared by their path

// Project includes:
#include "KESCMBookCompare.h"
#include "KESCMBookPair.h"		// KESCMBuildChapterPairing

namespace
{

/** Does this open document live in that file?

    ***** A document's identity is its FILE. ***** Asked through IDataBase::GetSysFile. A document
    that has never been saved has no file and can never be the chapter being looked for.

    This check is not optional caution. KBS used to trust IBookUtils::IsSourceDocumentAlreadyOpen,
    which hands back an INDEX into the document list, and that put a DIFFERENT chapter's document
    in a chapter's place: measured 2026-08-04, 4 book replaces in 10 came back with a whole
    chapter's rows marked missing - silently, because the call had reported success. One string
    compare is the whole distance between that and a right answer. */
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

/** Open a chapter windowless, or rebind to it when it is already open.

    outWeOpened says whether THIS call opened it. ***** A chapter the user already had open is used
    as it stands and never closed afterwards ***** - closing something somebody else opened would
    surprise them, and it is not ours to close. */
bool16 OpenChapter(const IDFile& file, UIDRef& outDocRef, bool16& outWeOpened, PMString& outWhy)
{
	outDocRef   = UIDRef::gNull;
	outWeOpened = kFalse;

	SDKFileHelper fileHelper(file);
	const PMString wantedPath = fileHelper.GetPath();
	if (wantedPath.empty())
	{
		outWhy = PMString("the chapter names no file");
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}

	// Open already - by the user, or by an earlier step of this same run?
	// Asked through the session's own lookup by file, and then CHECKED against the file asked for
	// (see DocumentLivesInFile). Never report failure from here: "not already open" is the
	// ordinary case, and the open below handles it.
	{
		ISession* session = GetExecutionContextSession();
		InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
		if (docList != nil)
		{
			IDocument* openDoc = docList->FindDoc(file);
			if (DocumentLivesInFile(openDoc, wantedPath))
			{
				outDocRef = ::GetUIDRef(openDoc);
				return kTrue;		// outWeOpened stays kFalse - not ours to close
			}
		}
	}

	// The windowless, UI-suppressed open, by FILE (the book itself may be closed).
	//
	// ⚠ NOT IBookUtils::OpenOneDocument. That one takes no UI-suppression argument, so a chapter
	// that raises an alert on opening (missing font, missing link, saved by another version)
	// fails - and its caller sees only "could not open", with the chapter vanishing from the
	// result as if it had had nothing to report. KBS lost a whole chapter that way.
	//
	// ***** THIS OPEN IS ALLOWED TO FAIL, so what it raises must not leave this scope. ***** A
	// chapter that will not open gets its own row with a reason; an error left standing would
	// instead fail whatever command runs next. Preserve, then clear (ErrorUtils.h:115-117) - the
	// shape the SDK itself uses for this operation
	// (buttonui/actiondatapanels/gotoanchor/GoToAnchorPanelObserver.cpp:395-401).
	UIDRef    docRef;
	ErrorCode err = kFailure;
	{
		GlobalErrorStatePreserver openErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		err = Utils<IDocumentCommands>()->Open(&docRef, file, kSuppressUI,
			IOpenFileCmdData::kOpenDefault, IOpenFileCmdData::kUseLockFile, kFalse /*showInWindow*/);
	}

	if (err != kSuccess || docRef == UIDRef::gNull)
	{
		outWhy = PMString("could not be opened");
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}

	outWeOpened = kTrue;
	outDocRef   = docRef;
	return kTrue;
}

/** Close a chapter THIS run opened. Anything that was already open is left exactly as it was.
    kFalse means it is still standing (and the caller counts it, so the user is told). */
bool16 CloseChapter(const UIDRef& docRef, bool16 weOpened)
{
	if (!weOpened || docRef == UIDRef::gNull)
		return kTrue;		// nothing of ours to close

	// The close is allowed to fail too, and for the same reason as the open: this runs BETWEEN
	// chapters, so anything left standing here would fail the next chapter's open.
	GlobalErrorStatePreserver closeErrorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
	if (docFileHandler == nil)
		return kFalse;
	if (!docFileHandler->CanClose(docRef))
		return kFalse;

	// ***** kProcess closes NOW; kSchedule would not close until this whole run unwinds. *****
	// A run IS the current tick, so with kSchedule every chapter it had "handed back" would still
	// be open - and still locking its .indd - until the entire comparison was over (measured by
	// KBS on a four-chapter run, 2026-08-04). With one pair in flight that is the difference
	// between two open documents and all of them.
	//
	// ***** kProcess is only legal because this document has no window. ***** Closing a document
	// that HAS one with kProcess is a stated error - IDocFileHandler::Close's own implementation
	// asserts "Close() illegal with open document windows and cmdMode == kProcess". Everything
	// reaching this line was opened by OpenChapter with showInWindow=kFalse, so it is windowless
	// by construction. Keep that true if this is ever reused.
	//
	// ⚠ Do NOT "improve" this to IBookUtils::CloseDocumentsInBook: it takes no UI flag and no
	// command mode, closes immediately, and crashed KESCL in 2026-07-17 when called from a
	// notification.
	docFileHandler->Close(docRef, kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kProcess);
	return kTrue;
}

}	// anonymous namespace

ErrorCode KESCMCompareBooks(IBook* target, IBook* source,
                            std::vector<KESCMChapterResult>& outChapters, PMString& outReport)
{
	KESCMBuildChapterPairing(target, source, outChapters);

	int32 leftOpen = 0;

	for (size_t i = 0; i < outChapters.size(); ++i)
	{
		KESCMChapterResult& chapter = outChapters[i];

		// Already answered by the pairing: no counterpart on the other side, or no file to open.
		if (chapter.fState != kKESCMChapterUnknown)
			continue;

		UIDRef   targetRef;
		UIDRef   sourceRef;
		bool16   targetMine = kFalse;
		bool16   sourceMine = kFalse;
		PMString why;

		if (!OpenChapter(chapter.fTargetFile, targetRef, targetMine, why) ||
		    !OpenChapter(chapter.fSourceFile, sourceRef, sourceMine, why))
		{
			// Whichever side did open has to be put back before moving on. (When the first open
			// failed the second never ran, and closing a null UIDRef is a no-op.)
			if (!CloseChapter(sourceRef, sourceMine))
				++leftOpen;
			if (!CloseChapter(targetRef, targetMine))
				++leftOpen;

			chapter.fState = kKESCMChapterFailed;
			chapter.fWhy   = why;
			continue;
		}

		// ⚠ The judgement goes HERE (the next task). Until it does, every pair that opens reads
		//    as unchanged - which is why this step is verified by watching the documents open and
		//    close, not by the answers.
		chapter.fState = kKESCMChapterNoChange;

		if (!CloseChapter(sourceRef, sourceMine))
			++leftOpen;
		if (!CloseChapter(targetRef, targetMine))
			++leftOpen;
	}

	int32 changed = 0, unchanged = 0, added = 0, deleted = 0, failed = 0;
	for (size_t i = 0; i < outChapters.size(); ++i)
	{
		switch (outChapters[i].fState)
		{
			case kKESCMChapterChanged:		++changed;		break;
			case kKESCMChapterNoChange:		++unchanged;	break;
			case kKESCMChapterAdded:		++added;		break;
			case kKESCMChapterDeleted:		++deleted;		break;
			case kKESCMChapterFailed:		++failed;		break;
			default:									break;
		}
	}

	// ***** The chapter COUNT is always stated. ***** An empty list has to be readable as "every
	// chapter was compared and none changed" rather than "nothing could be opened" - conflating
	// those two is the fault that took a day to find in KBS.
	outReport = PMString("book compare: ");
	outReport.AppendNumber(int32(outChapters.size()));
	outReport.Append(" chapters, ");
	outReport.AppendNumber(changed);
	outReport.Append(" changed, ");
	outReport.AppendNumber(unchanged);
	outReport.Append(" unchanged");
	if (added > 0)
	{
		outReport.Append(", ");
		outReport.AppendNumber(added);
		outReport.Append(" added");
	}
	if (deleted > 0)
	{
		outReport.Append(", ");
		outReport.AppendNumber(deleted);
		outReport.Append(" deleted");
	}
	if (failed > 0)
	{
		outReport.Append(", ");
		outReport.AppendNumber(failed);
		outReport.Append(" failed");
	}
	if (leftOpen > 0)
	{
		outReport.Append(", ");
		outReport.AppendNumber(leftOpen);
		outReport.Append(" left open");
	}
	outReport.SetTranslatable(kFalse);

	return kSuccess;
}

// End, KESCMBookCompare.cpp.
