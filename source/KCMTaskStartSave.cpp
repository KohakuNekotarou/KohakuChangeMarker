//========================================================================================
//
//  KCMTaskStartSave.cpp
//
//  Task Start: save a copy of the document, and choose that file as the Source.
//
//  MODEL side. KCMTaskStartSave.h carries the whole of why a file rather than a snapshot.
//
//  ⚠**A FILE DIALOG RAISED FROM THE MODEL HALF**, and the grounds are the ones the PDF report
//  states for the same move (KCMReport.cpp): the dialog is a boss of the application, not of a UI
//  plug-in, and this path is entered from the flyout only, never from a drawing thread.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#ifdef WINDOWS
#include "ShObjIdl.h"			// FOS_OVERWRITEPROMPT / FOS_NOREADONLYRETURN
#endif

// Interface includes:
#include "IApplication.h"
#include "IDataBase.h"
#include "IDocFileHandler.h"		// CanSaveACopy / SaveACopy - the facade, not kSaveACopyCmdBoss
#include "IDocument.h"
#include "IDocumentList.h"
#include "IDocumentUtils.h"		// QueryDocFileHandler
#include "ISaveFileDialog.h"
#include "ISession.h"
#include "DocumentID.h"			// kSaveFileDialogBoss
#include "ErrorUtils.h"
#include "FileUtils.h"			// GetParentDirectory / GetDirectorySeparatorPMString
#include "PMString.h"
#include "PersistUtils.h"
#include "SDKFileHelper.h"		// GetPath
#include "UIDRef.h"
#include "Utils.h"

// Project includes:
#include "KCMTaskStartSave.h"
#include "KCMPairChoice.h"		// the pair this chooses
#include "KCMCore.h"				// KCMActiveDocDB / KCMIsArmed / KCMArmedTargetDB
#include "KCMComparisonRun.h"	// KCMStopComparison

#include <time.h>

namespace {

/*	The document a Task Start copies - KCMTaskDocumentDB, below. (A name of its own here because the steps
	of KCMTakeTaskStartCopy read as "the document to copy".)
*/
IDataBase* DocumentToCopy()
{
	return KCMTaskDocumentDB();
}

/*	"20260921-143052" - what makes the suggested name unique.
	★Built the way KCMOrigin's own clock was (time / localtime_s / sprintf_s - ⛔that file went on
	  2026-09-21, and this is the last of it), with the date in
	  front: a Task Start copy outlives the session that made it, so the time alone would collide.
*/
void NowStamp(PMString& out)
{
	time_t t = ::time(nil);
	struct tm local;
	::localtime_s(&local, &t);
	char buf[32];
	::sprintf_s(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
				local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
				local.tm_hour, local.tm_min, local.tm_sec);
	out = buf;
	out.SetTranslatable(kFalse);
}

/*	The document's name with its extension taken off. Empty when the database names no open
	document. ★The same walk the PDF report's suggested name does (KCMReport.cpp).
*/
void BareName(IDataBase* db, PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	IDocument* const doc = (docList != nil && db != nil) ? docList->FindDocByDataBase(db) : nil;
	if (doc == nil)
		return;

	PMString name;
	doc->GetName(name);
	name.SetTranslatable(kFalse);

	const int32 n = name.NumUTF16TextChars();
	const UTF16TextChar* const b = name.GrabUTF16Buffer(nil);
	int32 cut = n;
	for (int32 i = n - 1; i > 0; --i)
		if (b[i] == '.') { cut = i; break; }
	for (int32 i = 0; i < cut; ++i)
		out.AppendW(UTF32TextChar(b[i]));
}

/*	The save dialog, opened ON THE DOCUMENT'S OWN FOLDER. kFalse when the reader cancelled.

	⚠★★★**NOT SDKFileSaveChooser, and the reason is one argument** (measured 2026-09-21). That
	  helper fixes DoDialog's fourth argument, useSystemDefaultDir, at kTrue
	  (SDKFileHelper.cpp), and kTrue means "the last used save directory" (ISaveFileDialog.h:66).
	  ⇒ The folder the user asked for - the document's own - could never be the one it opened on.
	  What is left is exactly what that helper does, one layer down.
	⚠**isSaveACopy, the seventh argument, is marked "INTERNAL USE ONLY" and is not passed.** This
	  plug-in ships through Adobe Exchange, where internal doors are not ours to use.
	⚠**The header's own instructions name a method that does not exist** (ISaveFileDialog.h:44
	  says "Call SetFileTypeInfo()"; the interface has AddFileTypeInfo).
*/
bool16 AskWhereToSave(IDataBase* docDB, IDFile& outFile)
{
	InterfacePtr<ISaveFileDialog> dlg(
		(ISaveFileDialog*)::CreateObject(kSaveFileDialogBoss, IID_ISAVEFILEDIALOG));
	if (dlg == nil)
		return kFalse;

	PMString typeName("InDesign document");	typeName.SetTranslatable(kFalse);
	PMString typeExt("indd");					typeExt.SetTranslatable(kFalse);
	dlg->AddFileTypeInfo(typeName, typeExt);
#ifdef WINDOWS
	dlg->SetAdditionalFOSFlags(FOS_OVERWRITEPROMPT | FOS_NOREADONLYRETURN);
#endif

	PMString suggested;
	KCMSuggestedTaskStartName(docDB, suggested);

	// ★**A FULL PATH INTO IDFile::SetString** is the Windows form SDKFileHelper uses for exactly
	//   this (SDKFileHelper.cpp). KCM ships Windows-only, so there is no second branch here.
	IDFile defaultFile;
	const IDFile* const docFile = (docDB != nil) ? docDB->GetSysFile() : nil;
	IDFile parent;
	if (docFile != nil && FileUtils::GetParentDirectory(*docFile, parent))
	{
		SDKFileHelper parentHelper(parent);
		PMString path = parentHelper.GetPath();
		path.Append(FileUtils::GetDirectorySeparatorPMString());
		path.Append(suggested);
		path.SetTranslatable(kFalse);
		defaultFile.SetString(path);
	}
	else
	{
		// A document never saved has no folder to offer, so the dialog picks one and the reader
		// moves it if they like - the user's decision ("raise the dialog anyway").
		defaultFile.SetString(suggested);
	}

	PMString title("Task Start - save a copy of this document");
	title.SetTranslatable(kFalse);
	int32 selectedIndex = 0;
	return dlg->DoDialog(&defaultFile, &outFile, &selectedIndex,
						 kFalse /*useSystemDefaultDir - see the note above*/,
						 kTrue /*showTypeMenu*/, &title);
}

}	// namespace

//----------------------------------------------------------------------------------------

void KCMSuggestedTaskStartName(IDataBase* docDB, PMString& out)
{
	PMString stem;
	BareName(docDB, stem);
	if (stem.IsEmpty())
		stem = PMString("Untitled");
	stem.SetTranslatable(kFalse);

	PMString stamp;
	NowStamp(stamp);

	out = stem;
	out.Append("_TaskStart_");
	out.Append(stamp);
	out.Append(".indd");
	out.SetTranslatable(kFalse);
}

IDataBase* KCMTaskDocumentDB()
{
	IDataBase* const chosen = KCMChosenTargetDB();
	return (chosen != nil) ? chosen : KCMActiveDocDB();
}

bool16 KCMCanTakeTaskStartCopy()
{
	return (DocumentToCopy() != nil) ? kTrue : kFalse;
}

bool16 KCMTakeTaskStartCopy(PMString& outWhyNot)
{
	outWhyNot.Clear();
	outWhyNot.SetTranslatable(kFalse);

	// 1. WHICH DOCUMENT - decided first, and held for the rest of the call. Everything below,
	//    including the stop, is allowed to change what is active; this is not asked again.
	IDataBase* const docDB = DocumentToCopy();
	if (docDB == nil)
	{
		outWhyNot = "Task Start needs a document to copy";
		return kFalse;
	}
	// ★A document's UIDRef is its database's root - the form KCM uses elsewhere (KCMCore.cpp).
	const UIDRef docRef(docDB, docDB->GetRootUID());

	// 2. ⚠**ASKED BEFORE ANY TRANSACTION IS OPENED.** CanSaveACopy answers kFalse from inside one
	//    ([[docfilehandler-save-close-facade]], measured 2026-08-30), so asking it later would
	//    refuse a document that is perfectly able to be copied.
	InterfacePtr<IDocFileHandler> handler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
	if (handler == nil || !handler->CanSaveACopy(docRef))
	{
		outWhyNot = "This document cannot be copied right now.";
		return kFalse;
	}

	// 3. WHERE. ★★A cancel ends the whole thing here, having changed nothing - no stop, no choice,
	//    and outWhyNot stays EMPTY so that the caller says nothing either (the user's rule).
	IDFile dest;
	if (!AskWhereToSave(docDB, dest))
		return kFalse;

	// 4. WRITE IT. ★SaveACopy adds no undo step (measured 2026-08-30), so the reader's undo stack
	//    is untouched by a Task Start - which is what makes it safe to press at any moment.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	handler->SaveACopy(docRef, &dest);
	// ⚠SaveACopy returns void; whatever it raised is on the error stack, and an unread error
	//  would be carried into the next command (KCMBookCompare.cpp says the same of its close).
	const ErrorCode saveErr = ErrorUtils::PMGetGlobalErrorCode();
	if (saveErr != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		outWhyNot = "The copy could not be saved (error ";
		outWhyNot.AppendNumber(saveErr);
		outWhyNot.Append(").");
		outWhyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	// 5. STOP. The marks on screen were made from a pair that is about to change, and leaving them
	//    would have them name one comparison while the panel names another.
	if (KCMIsArmed() && KCMArmedTargetDB() != nil)
		KCMStopComparison();

	// 6. THE PAIR. ★The Target is THE DOCUMENT THAT WAS COPIED, named outright rather than asked
	//    for again: the stop above can bring a different window to the front, and "what is active"
	//    would then answer with that one instead.
	KCMSetChosenTargetDB(docDB);
	KCMSetChosenSourceFile(dest);
	return kTrue;
}

// End, KCMTaskStartSave.cpp.
