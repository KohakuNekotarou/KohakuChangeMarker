//========================================================================================
//
//  KCMRehydrate.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocFileHandler.h"
#include "IDocument.h"
#include "IDocumentCommands.h"
#include "IDocumentList.h"
#include "IDocumentUtils.h"
#include "IDOMElement.h"
#include "IGlobalRecompose.h"		// ForceRecompositionToComplete - compose the copy before it is rasterised
#include "IINXManager.h"
#include "IPMStream.h"
#include "IScript.h"				// a story's scripting facet IS an IScriptLabel (KCMPageMarksDoc.cpp)
#include "IScriptLabel.h"
#include "ISession.h"

// General includes:
#include "ErrorUtils.h"				// GlobalErrorStatePreserver
#include "PersistUtils.h"
#include "StreamUtil.h"
#include "Utils.h"
#include "INXCoreID.h"				// IID_IINXIMPORTPOLICY
#include "SnippetID.h"				// kDocElementImportBoss

#include <string>

// Project includes:
#include "KCMRehydrate.h"
#include "KCMOrigin.h"				// KCMOriginShape / KCMMeasureShape
#include "KCMResourceBytes.h"
#include "KCMXmlInject.h"

class IINXImportPolicy;				// forward-declared only in the SDK; held through IPMUnknown

namespace
{

/** KCMByteSink over a KCMResourceBytes. */
struct BytesSink : public KCMByteSink
{
	KCMResourceBytes& fOut;
	BytesSink(KCMResourceBytes& out) : fOut(out) {}
	virtual bool16 Write(const char* bytes, size_t count)
	{
		const uint32 wrote = fOut.Write(const_cast<char*>(bytes), static_cast<uint32>(count));
		return (wrote == static_cast<uint32>(count) && fOut.IsWhole()) ? kTrue : kFalse;
	}
};

IDocument* DocOf(IDataBase* db)
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
	return (docList != nil && db != nil) ? docList->FindDocByDataBase(db) : nil;
}

void AppendShape(PMString& out, const KCMOriginShape& s)
{
	out.AppendNumber(s.fSpreads); out.Append("/");
	out.AppendNumber(s.fPages);   out.Append("/");
	out.AppendNumber(s.fStories); out.Append("/");
	out.AppendNumber(s.fTextLen);
}

}	// namespace

bool16 KCMRehydrate(const KCMResourceBytes& inx, const KCMOriginShape& expect, UIDRef& outDoc, PMString& whyNot)
{
	outDoc = UIDRef::gNull;
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (inx.Size() == 0)
	{
		whyNot = "the origin holds no bytes";
		return kFalse;
	}

	// 1. the injected copy
	KCMResourceBytes copy;
	BytesSink sink(copy);
	int32 stories = 0, spreads = 0;
	if (!KCMInjectForRehydration(inx.Bytes(), inx.Size(), sink, &stories, &spreads))
	{
		whyNot = "could not prepare the XML (out of memory, or malformed)";
		return kFalse;
	}

	// 2. a fresh windowless document
	UIDRef ref = UIDRef::gNull;
	{
		GlobalErrorStatePreserver errorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		if (Utils<IDocumentCommands>()->New(&ref, kSuppressUI) != kSuccess || ref == UIDRef::gNull)
		{
			whyNot = "could not create a document to rehydrate into";
			return kFalse;
		}
	}
	IDocument* const doc = DocOf(ref.GetDataBase());
	InterfacePtr<IDOMElement> parent(doc, UseDefaultIID());
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inxManager(session != nil ? session->QueryINXManager() : nil);
	InterfacePtr<IPMUnknown> holder((IPMUnknown*)::CreateObject(kDocElementImportBoss, IID_IINXIMPORTPOLICY));
	copy.Seek(0, kSeekFromStart);
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamRead(&copy));
	if (doc == nil || parent == nil || inxManager == nil || holder == nil || stream == nil)
	{
		whyNot = "the import's parts could not be assembled";
		KCMCloseRehydrated(ref);
		return kFalse;
	}

	// 3. the import
	IINXImportPolicy* const policy = (IINXImportPolicy*)holder.get();
	IDOMElement* imported = nil;
	ErrorCode err = kFailure;
	{
		GlobalErrorStatePreserver errorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		inxManager->BeginImportSession();
		err = inxManager->ImportINX(parent, policy, stream, nil, kSuppressUI, &imported);
		inxManager->EndImportSession();
	}
	InterfacePtr<IDOMElement> importedHolder(imported);	// takes the reference the call handed back
	if (err != kSuccess)
	{
		whyNot = "ImportINX failed";
		KCMCloseRehydrated(ref);
		return kFalse;
	}

	// 4. compose BEFORE anything reads pixels or text positions. A document straight out of the
	//    import has its stories uncomposed, and the Pixel comparison rasterises it at once: measured
	//    2026-09-12 on the first live Start, every page of an unchanged document came back
	//    "changed" (4 of 4) until this line. The book comparison does the same for the chapters it
	//    opens (RecomposeChapter, KCMBookCompare.cpp), and for the same reason. A menu command is
	//    a safe place to recompose; a draw event would not be.
	{
		InterfacePtr<IGlobalRecompose> recompose(doc, IID_IGLOBALRECOMPOSE);
		if (recompose != nil)
			recompose->ForceRecompositionToComplete();
	}

	// 5. the check: whole, or nothing
	KCMOriginShape got;
	KCMMeasureShape(ref.GetDataBase(), got);
	if (!(got == expect))
	{
		whyNot = "the rehydrated document does not match the origin (spreads/pages/stories/text ";
		AppendShape(whyNot, got);
		whyNot.Append(" against ");
		AppendShape(whyNot, expect);
		whyNot.Append(")");
		KCMCloseRehydrated(ref);
		return kFalse;
	}
	outDoc = ref;
	return kTrue;
}

void KCMCloseRehydrated(const UIDRef& doc)
{
	if (doc == UIDRef::gNull || DocOf(doc.GetDataBase()) == nil)
		return;						// already gone
	GlobalErrorStatePreserver errorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	InterfacePtr<IDocFileHandler> handler(Utils<IDocumentUtils>()->QueryDocFileHandler(doc));
	if (handler == nil || !handler->CanClose(doc))
		return;
	// kProcess: closes now, legal because the document has no window (KCMBookCompare.cpp). See the
	// header for what to do if this is ever seen to crash.
	handler->Close(doc, kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kProcess);
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
