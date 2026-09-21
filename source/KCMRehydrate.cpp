//========================================================================================
//
//  KCMRehydrate.cpp -- see the header.
//
//  ⛔**THE REHYDRATION ITSELF WENT ON 2026-09-21**, with the origin it existed for: KCMRehydrate,
//  the import, the new document made in the origin's own page setup, the sacrificial token, the
//  surviving dummy story's deletion, the page labels and the copy's name. Task Start saves a copy
//  of the document to a FILE and Start opens it, so nothing is built out of held bytes any more.
//
//  ★★**WHAT WENT WITH IT**: this plug-in does not call IINXManager::ImportINX anywhere now
//  (KCMPdfSpike.cpp still does, and that goes before shipping). The measurements those functions
//  carried - how ImportINX has to be called, why the document under it cannot be
//  IDocumentCommands::New, what the import drops and what it keeps - are in
//  **docs/ai-notes/kcm-rehydration-retired-2026-09-21.md**, and the code is one `git revert` away.
//  ⇒ Read that note before writing an import again.
//
//  ⚠**THE FILE KEEPS ITS NAME.** What is left is about a document this plug-in made and has to
//  close cleanly, which is the half of the name that never depended on the origin. Renaming it
//  would touch every caller for no behaviour - a rename is its own change, not a rider on a
//  removal (the same reasoning KCMOrigin.h was kept under until it went).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocFileHandler.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "IDocumentUtils.h"
#include "IScript.h"				// a story's scripting facet IS an IScriptLabel (KCMPageMarksDoc.cpp)
#include "IScriptLabel.h"
#include "ISession.h"

// General includes:
#include "ErrorUtils.h"				// GlobalErrorStatePreserver
#include "Utils.h"

#include <string>

// Project includes:
#include "KCMRehydrate.h"
#include "KCMXmlInject.h"			// kKCMOriginUidLabelKey / KCMParseSelfUid

namespace
{

IDocument* DocOf(IDataBase* db)
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
	return (docList != nil && db != nil) ? docList->FindDocByDataBase(db) : nil;
}

}	// namespace

void KCMMarkRehydratedClean(IDataBase* db)
{
	if (db != nil && db->IsModified())
		db->SetModified(kFalse);
}

void KCMCloseRehydrated(const UIDRef& doc, bool16 deferred)
{
	if (doc == UIDRef::gNull || DocOf(doc.GetDataBase()) == nil)
		return;						// already gone
	GlobalErrorStatePreserver errorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	InterfacePtr<IDocFileHandler> handler(Utils<IDocumentUtils>()->QueryDocFileHandler(doc));
	if (handler == nil || !handler->CanClose(doc))
		return;
	// kProcess: closes now, legal because the document has no window (KCMBookCompare.cpp).
	// kSchedule: the handler's default, for the caller inside a close responder (the header).
	handler->Close(doc, kSuppressUI, kFalse /*allowCancel*/,
				   deferred ? IDocFileHandler::kSchedule : IDocFileHandler::kProcess);
}

bool16 KCMReadOriginUidLabel(IDataBase* db, UID uid, UID& outOriginal)
{
	outOriginal = kInvalidUID;
	if (db == nil || uid == kInvalidUID)
		return kFalse;
	InterfacePtr<IScript> script(db, uid, UseDefaultIID());
	if (script == nil)
		return kFalse;
	const IScriptLabel::ScriptLabelValue value = script->GetTag(PMString(kKCMOriginUidLabelKey));
	// A few bytes ("ufe"), so the temporary std::string is not the buffer rule's concern.
	const std::string narrow = value.GetUTF8String();
	uint32 parsed = 0;
	if (!KCMParseSelfUid(narrow.c_str(), narrow.size(), parsed))
		return kFalse;
	outOriginal = UID(parsed);
	return kTrue;
}

// End, KCMRehydrate.cpp.
