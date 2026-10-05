//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - "Compare with Tracked Changes...". See KCMTrackOriginal.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ICommand.h"
#include "IDataBase.h"
#include "IDocFileHandler.h"
#include "IDocument.h"
#include "IDocumentUtils.h"
#include "IGlobalRecompose.h"
#include "IRedlineDataStrand.h"
#include "IStoryList.h"
#include "ITextModel.h"

#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "InCopySharedID.h"			// kRejectAllRedlineCmdBoss, kRedlineStrandBoss
#include "SDKFileHelper.h"
#include "UIDList.h"
#include "Utils.h"
#include "WideString.h"
#include "VOSRedline.h"
#include "redlineiterator.h"

#include "KCMBoundaryID.h"
#include "KCMDiag.h"				// KCM_DIAG_LOG - test builds only
#include "KCMComparisonRun.h"		// KCMToggleStartStop / KCMStopComparison
#include "KCMCore.h"				// KCMIsArmed / KCMArmedTargetDB / KCMSetCompareMode
#include "KCMModelNotify.h"			// KCMNotify / KCMGetSessionStatus
#include "KCMPairChoice.h"			// KCMRealisePairEnd / KCMChooseDBPair / KCMChosenTargetDB
#include "KCMTaskStartSave.h"		// KCMAskWhereToSaveCopy / KCMSuggestedCopyName / KCMTaskDocumentDB
#include "KCMTrackOriginal.h"

namespace
{
// The copy this session made, and the Target it was made from - by FILE (or document ID), never by pointer.
IDFile		gCopyFile;
bool16		gHaveCopy = kFalse;
IDFile		gTargetFile;
bool16		gTargetHasFile = kFalse;
PMString	gTargetDocumentID;

PMString DocumentIDOf(IDataBase* db)
{
	// ★The INTERNAL door, on the user's earlier call for the same purpose - an identity key for a document with no
	//   file (KCMThreadSafety.h says why it was chosen over IAdobeMediaMgmtMetaData).
	PMString id;
	if (db != nil)
		id = db->GetDocumentID();
	id.SetTranslatable(kFalse);
	return id;
}

// Reject every tracked change of every story in db - the product's Accept All shape (InCopyDocUtils.cpp:2400-2404),
// the other command, no author named. Returns how many records stand afterwards.
int32 RejectAll(IDataBase* db)
{
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	const int32 count = (stories != nil) ? stories->GetAllTextModelCount() : 0;
	for (int32 i = 0; i < count; ++i)
	{
		const UIDRef story = stories->GetNthTextModelUID(i);
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kRejectAllRedlineCmdBoss));
		if (cmd == nil)
			continue;
		cmd->SetItemList(UIDList(story));
		if (CmdUtils::ProcessCommand(cmd) != kSuccess)
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// a story it could not take back is counted below
	}
	int32 left = 0;
	for (int32 i = 0; i < count; ++i)
	{
		InterfacePtr<ITextModel> model(stories->GetNthTextModelUID(i), UseDefaultIID());
		InterfacePtr<IRedlineDataStrand> redline(model != nil
			? static_cast<IRedlineDataStrand*>(model->QueryStrand(kRedlineStrandBoss, IRedlineDataStrand::kDefaultIID)) : nil);
		RedlineIterator* it = (redline != nil) ? redline->NewRedlineIterator(0) : nil;
		if (it == nil)
			continue;
		for (bool16 more = kTrue; more; more = it->Increment(kFalse))
		{
			const VOSRedlineChange* record = it->GetCurrentChangeRecord();
			if (record == nil)
				continue;
			++left;
			delete record;
		}
		delete it;
	}
	return left;
}

// The last line of the status line - after a Start, the comparison's own report line ("tracked changes=N authors=M",
// KCMCore.cpp). Carried into this road's sentence because the caller puts that sentence on the same line, over it -
// the reason the import carries its start's words too (KCMStoryTextImport.cpp).
PMString LastStatusLine()
{
	PMString status;
	KCMGetSessionStatus(status);
	const WideString all(status);
	WideString line;
	for (WideString::const_iterator it = all.begin(); it != all.end(); ++it)
	{
		if (*it == 0x0A || *it == 0x0D)
			line.Clear();
		else
			line.Append(*it);
	}
	PMString out(line);
	out.SetTranslatable(kFalse);
	return out;
}

void Recompose(IDataBase* db)
{
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	InterfacePtr<IGlobalRecompose> recompose(doc, IID_IGLOBALRECOMPOSE);
	if (recompose != nil)
		recompose->ForceRecompositionToComplete();
}
}	// namespace

bool16 KCMCanTakeTrackOriginalCopy()
{
	return (KCMTaskDocumentDB() != nil) ? kTrue : kFalse;
}

bool16 KCMIsTrackOriginalOf(IDataBase* sourceDB, IDataBase* targetDB)
{
	if (!gHaveCopy || sourceDB == nil || targetDB == nil)
		return kFalse;
	const IDFile* sourceFile = sourceDB->GetSysFile();
	if (sourceFile == nil || !FileUtils::IsEqual(*sourceFile, gCopyFile))
		return kFalse;
	const IDFile* targetFile = targetDB->GetSysFile();
	if (gTargetHasFile)
		return (targetFile != nil && FileUtils::IsEqual(*targetFile, gTargetFile)) ? kTrue : kFalse;
	return (targetFile == nil && DocumentIDOf(targetDB) == gTargetDocumentID) ? kTrue : kFalse;
}

bool16 KCMTakeTrackOriginalCopy(const IDFile* dest, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	// 1. WHICH DOCUMENT - decided first and held (RUN-65): the stop below can bring another window forward.
	IDataBase* const docDB = KCMTaskDocumentDB();
	if (docDB == nil)
	{
		outMessage = "Compare with Tracked Changes needs a document";
		return kFalse;
	}
	const UIDRef docRef(docDB, docDB->GetRootUID());
	InterfacePtr<IDocFileHandler> handler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
	if (handler == nil || !handler->CanSaveACopy(docRef))	// asked before any transaction (KCMTaskStartSave.cpp, step 2)
	{
		outMessage = "This document cannot be copied right now.";
		return kFalse;
	}

	// 2. WHERE. A cancel ends it here, having changed nothing, with nothing to say.
	IDFile file;
	if (dest != nil)
		file = *dest;
	else
	{
		PMString suggested;
		KCMSuggestedCopyName(docDB, "_TrackOriginal_", suggested);
		if (!KCMAskWhereToSaveCopy(docDB, suggested,
				PMString("Compare with Tracked Changes - save the original version (every change rejected)"), file))
			return kFalse;
	}

	// ★NOT THE DOCUMENT'S OWN FILE (re-check 2026-10-05): the copy would be written over the original it is the copy of -
	//   the one file this road promises never to write (design 2-2). The dialog's overwrite prompt does not stop it, and the
	//   script door has no dialog at all.
	const IDFile* const ownFile = docDB->GetSysFile();
	if (ownFile != nil && FileUtils::IsEqual(*ownFile, file))
	{
		outMessage = "The copy cannot be saved over the document itself - choose another name.";
		return kFalse;
	}

	// 3. THE COPY. SaveACopy adds no undo step (measured 2026-08-30): the Target's undo stack is untouched.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	handler->SaveACopy(docRef, &file);
	const ErrorCode saveErr = ErrorUtils::PMGetGlobalErrorCode();
	if (saveErr != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		outMessage = "The copy could not be saved (error ";
		outMessage.AppendNumber(saveErr);
		outMessage.Append(").");
		return kFalse;
	}

	// 4. STOP a running comparison: its marks name a pair that is about to change.
	if (KCMIsArmed() && KCMArmedTargetDB() != nil)
		KCMStopComparison();

	// 5. OPEN THE COPY IN A WINDOW, the way Start opens a Task Start copy (and composes it).
	KCMPairEnd end;
	end.fIsFile = kTrue;
	end.fFile = file;
	IDataBase* copyDB = nil;
	PMString why;
	if (!KCMRealisePairEnd(end, copyDB, why) || copyDB == nil)
	{
		outMessage = "The copy was saved but could not be opened: ";
		outMessage.Append(why);
		return kFalse;
	}

	// 6. EVERY CHANGE REJECTED IN THE COPY, composed again (the text flows again), saved again.
	const int32 left = RejectAll(copyDB);
	KCM_DIAG_LOG("track original: copy opened, rejected all, records left=%d", (int)left);
	Recompose(copyDB);
	const UIDRef copyRef(copyDB, copyDB->GetRootUID());
	InterfacePtr<IDocFileHandler> copyHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(copyRef));
	if (copyHandler != nil && copyHandler->CanSave(copyRef))
	{
		copyHandler->Save(copyRef, kSuppressUI);
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}

	// 7. REMEMBER WHAT THIS COPY IS, by file.
	gCopyFile = file;
	gHaveCopy = kTrue;
	const IDFile* targetFile = docDB->GetSysFile();
	gTargetHasFile = (targetFile != nil) ? kTrue : kFalse;
	if (targetFile != nil)
		gTargetFile = *targetFile;
	gTargetDocumentID = DocumentIDOf(docDB);

	// 8. THE PAIR, THE MODE, THE START - and the Target to the front (the import's message, KCMStoryTextImport.cpp).
	KCMChooseDBPair(docDB, copyDB);
	KCMSetCompareMode(kKCMModeTrack);
	KCMToggleStartStop();
	KCMNotify(kKCMTargetToFrontMessage);
	KCM_DIAG_LOG("track original: pair chosen, Track started -> armed=%d", (int)KCMIsArmed());

	SDKFileHelper helper(file);
	outMessage = "Track: the original version saved as ";
	outMessage.Append(helper.GetPath());
	outMessage.Append(" and chosen as the Source");
	if (left > 0)
	{
		outMessage.Append(" - ");
		outMessage.AppendNumber(left);
		outMessage.Append(" change(s) could not be rejected in the copy");
	}
	outMessage.Append(".");
	if (KCMIsArmed() && KCMArmedTargetDB() != nil)
	{
		const PMString report = LastStatusLine();
		if (report.CharCount() > 0)
		{
			outMessage.Append(" ");
			outMessage.Append(report);
		}
	}
	else
	{
		PMString startSaid;
		KCMGetSessionStatus(startSaid);
		outMessage.Append(" The comparison did not start");
		if (startSaid.CharCount() > 0)
		{
			outMessage.Append(" (");
			outMessage.Append(startSaid);
			outMessage.Append(")");
		}
	}
	outMessage.SetTranslatable(kFalse);
	return kTrue;
}

// End, KCMTrackOriginal.cpp.
