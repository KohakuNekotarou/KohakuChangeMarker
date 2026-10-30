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
//  (KCMPdfSpike.cpp still did, until it went before shipping on 2026-10-02 - in git at 5720208). The measurements those functions
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
#include "IApplication.h"			// QueryDocumentList - the open documents (ISession's is for internal use only)
#include "ISession.h"

// General includes:
#include "ErrorUtils.h"				// GlobalErrorStatePreserver
#include "Utils.h"

// Project includes:
#include "KCMRehydrate.h"
// (KCMXmlInject.h, IScript.h, IScriptLabel.h and <string> went on 2026-10-30 with KCMReadOriginUidLabel.)

namespace
{

IDocument* DocOf(IDataBase* db)
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
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

// End, KCMRehydrate.cpp.
