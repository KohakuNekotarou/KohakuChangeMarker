//========================================================================================
//
//  KCMRehydrate.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IComposeScanner.h"		// DeleteSurvivingDummies: the first paragraph of each story
#include "IDataBase.h"
#include "IDocFileHandler.h"
#include "IDocument.h"
#include "IDocumentCommands.h"
#include "IDocumentList.h"
#include "IDocumentUtils.h"
#include "IDOMElement.h"
#include "IGlobalRecompose.h"		// ForceRecompositionToComplete - compose the copy before it is rasterised
#include "IINXManager.h"
#include "INewDocCmdData.h"		// NewDocumentLike - the copy is CREATED with the origin's page setup
#include "ILayoutUtils.h"			// DocPageBinding (kDefaultBinding / kLeftToRightBinding / kRightToLeftBinding)
#include "IPMStream.h"
#include "IScript.h"				// a story's scripting facet IS an IScriptLabel (KCMPageMarksDoc.cpp)
#include "IScriptLabel.h"
#include "ISession.h"
#include "IStoryList.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"		// DeleteCmd - the one edit ever made to a rehydrated copy

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"				// GlobalErrorStatePreserver
#include "PersistUtils.h"
#include "StreamUtil.h"
#include "Utils.h"
#include "INXCoreID.h"				// IID_IINXIMPORTPOLICY
#include "PMPageSize.h"				// NewDocumentLike
#include "UIDList.h"				// the new-document command's item list
#include "SnippetID.h"				// kDocElementImportBoss

#include <string>
#include <stdio.h>					// sprintf_s - the nonce as hex
#include <random>					// std::random_device - the nonce (NewSacrificialToken)
#include "WideString.h"

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

/** The sacrificial token of ONE rehydration: kKCMSacrificialPrefix + 32 hex digits of a random
	nonce (the user's ask, 2026-09-12 - see KCMXmlInject.h). std::random_device may throw on a
	platform with no entropy source; that is caught here, and the fallback mixes the clock with
	the address of a local, which is still a value nobody has typed. ASCII only, so the token can
	go into XML text and be compared char by char after the import. */
std::string NewSacrificialToken()
{
	uint32 words[4] = { 0, 0, 0, 0 };
	bool16 haveRandom = kFalse;
	try
	{
		std::random_device rd;
		for (int i = 0; i < 4; ++i)
			words[i] = static_cast<uint32>(rd());
		haveRandom = kTrue;
	}
	catch (...)
	{
	}
	if (!haveRandom)
	{
		const uint32 tick = static_cast<uint32>(::GetTickCount());
		words[0] = tick;
		words[1] = static_cast<uint32>(reinterpret_cast<uintptr_t>(&words) & 0xFFFFFFFFu);
		words[2] = tick * 2654435761u;
		words[3] = ~tick;
	}
	char hex[40];
	::sprintf_s(hex, sizeof(hex), "%08x%08x%08x%08x", words[0], words[1], words[2], words[3]);
	std::string token(kKCMSacrificialPrefix);
	token += hex;
	return token;
}

/** The sacrificial first range (KCMXmlInject.h) is put in for ImportINX to drop - and on a
	one-story document it always was. WITH TWO STORIES ONE OF THEM KEPT IT (2026-09-12: the copy
	came back 9 characters longer than the origin = "KCMDUMMY" + its return, and the shape check
	refused it, which is what the check is for). So whatever survived is deleted here BY CONTENT:
	a story whose first paragraph is exactly the sacrificial text loses that paragraph. Through a
	command, because the text model takes no other route; the copy is ours and windowless, so the
	undo step lands on nobody's stack.
	@param dummy the token this rehydration injected (NewSacrificialToken) - the one string both
	       halves of the rule share.
	@return how many paragraphs were deleted (the caller only reports it). */
int32 DeleteSurvivingDummies(IDataBase* db, const char* dummy)
{
	int32 deleted = 0;
	if (db == nil || dummy == nil || dummy[0] == '\0')
		return 0;
	const int32 dummyLen = static_cast<int32>(::strlen(dummy));
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories == nil)
		return 0;
	const int32 n = stories->GetUserAccessibleStoryCount();
	for (int32 i = 0; i < n; ++i)
	{
		InterfacePtr<ITextModel> model(stories->GetNthUserAccessibleStoryUID(i), UseDefaultIID());
		if (model == nil)
			continue;
		InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
		if (scanner == nil)
			continue;
		int32 span = 0;
		// excludeEOS = kFalse, as FirstReadableText reads (KCMStoryList.cpp): the whole paragraph,
		// return included - so a surviving dummy spans exactly the text plus one.
		const TextIndex start = scanner->FindSurroundingParagraph(0, &span, kFalse);
		if (start != 0 || span != dummyLen + 1)
			continue;
		WideString para;
		scanner->CopyText(0, dummyLen, &para);
		bool16 same = (para.CharCount() == dummyLen) ? kTrue : kFalse;
		for (int32 c = 0; same && c < dummyLen; ++c)
			if (para.GetChar(c) != UTF32TextChar(dummy[c]))
				same = kFalse;
		if (!same)
			continue;
		InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
		if (cmds == nil)
			continue;
		InterfacePtr<ICommand> cmd(cmds->DeleteCmd(0, span));
		if (cmd != nil && CmdUtils::ProcessCommand(cmd) == kSuccess)
			++deleted;
	}
	return deleted;
}

/** Step 2 of a rehydration: a fresh windowless document MADE WITH THE ORIGIN'S PAGE SETUP.

	⚠★★★NOT IDocumentCommands::New. Measured 2026-09-12 evening: New makes a document with the
	application's defaults (facing pages, for one), and ImportINX does not apply the origin's
	<DocumentPreference> to a document that already exists - a non-facing origin came back as a
	facing copy whose single pages were laid out as left-hand pages, so every frame (spread
	coordinates in the XML) sat half a page to the right. Page 1 read "changed" in a document
	nobody had touched, and the peek laid the wrong picture over the page. So the page size, the
	pages per spread and the binding are read off the XML (KCMReadDocumentPreference) and given
	to the new-document command up front, the way SDKLayoutHelper::CreateDocument does it
	(sdksamples/common). What the XML does not say is left to the defaults, as before. */
bool16 NewDocumentLike(const KCMResourceBytes& inx, UIDRef& outRef, PMString& whyNot)
{
	outRef = UIDRef::gNull;
	KCMDocSetupFromXml setup;
	KCMReadDocumentPreference(inx.Bytes(), inx.Size(), setup);	// absent attributes keep the defaults

	InterfacePtr<ICommand> newDocCmd(Utils<IDocumentCommands>()->CreateNewCommand(kSuppressUI));
	InterfacePtr<INewDocCmdData> data(newDocCmd, UseDefaultIID());
	if (newDocCmd == nil || data == nil)
	{
		whyNot = "could not make the new-document command";
		return kFalse;
	}
	data->SetCreateBasicDocument(kFalse);			// the values below, not the application's defaults
	if (setup.fHasSize)
	{
		data->SetNewDocumentPageSize(PMPageSize(setup.fPageWidth, setup.fPageHeight));
		data->SetWideOrientation((setup.fPageWidth > setup.fPageHeight) ? kTrue : kFalse);
	}
	data->SetNumPages(1);							// the import brings the rest, reusing this one
	if (setup.fHasFacing)
		data->SetPagesPerSpread(setup.fFacingPages ? 2 : 1);
	if (setup.fHasBinding)
		data->SetPageBinding(setup.fBinding);

	GlobalErrorStatePreserver errorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (CmdUtils::ProcessCommand(newDocCmd) != kSuccess)
	{
		whyNot = "could not create a document to rehydrate into";
		return kFalse;
	}
	const UIDList& made = newDocCmd->GetItemListReference();
	if (made.Length() == 0 || made.GetRef(0) == UIDRef::gNull)
	{
		whyNot = "the new-document command made nothing";
		return kFalse;
	}
	outRef = made.GetRef(0);
	return kTrue;
}

/** Steps 3 to 5 of a rehydration, on a document that already exists: import the injected XML,
	compose, check the shape.

	**EVERY INTERFACE THIS TAKES ON THE DOCUMENT IS RELEASED WHEN IT RETURNS, AND THAT IS THE
	WHOLE REASON IT IS A SEPARATE FUNCTION.** The caller closes the document on kFalse - AFTER this
	has returned, so nothing of ours is still standing on it. It used to be one function that
	closed on each failure with `parent`, `importedHolder` and the policy still in scope, and
	InDesign answers a close under outstanding references with a PROTECTIVE SHUTDOWN: the
	process ends without an exception (measured 2026-09-12, twice: "CloseDocCmd - document is
	still referenced ... Document has 3 extra references" in InDesign Recovery/
	ProtectiveShutdownLog; nothing in any crash watch, because nothing was thrown). The morning's
	"crash closing from inside a script property" was this same failure path, not the context.
*/
bool16 ImportOnly(const UIDRef& ref, KCMResourceBytes& xml, PMString& whyNot)
{
	IDocument* const doc = DocOf(ref.GetDataBase());
	InterfacePtr<IDOMElement> parent(doc, UseDefaultIID());
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inxManager(session != nil ? session->QueryINXManager() : nil);
	InterfacePtr<IPMUnknown> holder((IPMUnknown*)::CreateObject(kDocElementImportBoss, IID_IINXIMPORTPOLICY));
	xml.Seek(0, kSeekFromStart);
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamRead(&xml));
	if (doc == nil || parent == nil || inxManager == nil || holder == nil || stream == nil)
	{
		whyNot = "the import's parts could not be assembled";
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
		return kFalse;
	}
	return kTrue;
}

bool16 ImportAndCheck(const UIDRef& ref, KCMResourceBytes& copy, const KCMOriginShape& expect,
					  const char* dummy, PMString& whyNot)
{
	if (!ImportOnly(ref, copy, whyNot))
		return kFalse;
	IDocument* const doc = DocOf(ref.GetDataBase());

	// 3b. the sacrificial ranges the import did NOT drop (DeleteSurvivingDummies says why there
	//     can be any). Before the compose, so that what is composed is the text as it should be.
	{
		GlobalErrorStatePreserver errorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		DeleteSurvivingDummies(ref.GetDataBase(), dummy);
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
		return kFalse;
	}
	return kTrue;
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

	// 1. the injected copy, with this rehydration's own sacrificial token (KCMXmlInject.h)
	const std::string dummy = NewSacrificialToken();
	KCMResourceBytes copy;
	BytesSink sink(copy);
	int32 stories = 0, spreads = 0;
	if (!KCMInjectForRehydration(inx.Bytes(), inx.Size(), dummy.c_str(), sink, &stories, &spreads))
	{
		whyNot = "could not prepare the XML (out of memory, or malformed)";
		return kFalse;
	}

	// 2. a fresh windowless document, made with the origin's page setup (NewDocumentLike says why)
	UIDRef ref = UIDRef::gNull;
	if (!NewDocumentLike(inx, ref, whyNot))
		return kFalse;
	// 3-5. import, compose, check - in a function of their own, so that every interface taken on
	//      the document is gone before the close below (ImportAndCheck says why that is the rule).
	if (!ImportAndCheck(ref, copy, expect, dummy.c_str(), whyNot))
	{
		KCMCloseRehydrated(ref);	// nothing of ours stands on it any more
		return kFalse;
	}
	// 6. ours, and nothing in it to save (the header says why this matters at a Quit)
	KCMMarkRehydratedClean(ref.GetDataBase());
	outDoc = ref;
	return kTrue;
}

bool16 KCMRehydrateRaw(const KCMResourceBytes& inx, UIDRef& outDoc, PMString& whyNot)
{
	outDoc = UIDRef::gNull;
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (inx.Size() == 0)
	{
		whyNot = "the origin holds no bytes";
		return kFalse;
	}
	// The bytes AS THEY ARE: no injection, so the stream is a copy of the origin and nothing else.
	KCMResourceBytes copy;
	const uint32 size = static_cast<uint32>(inx.Size());
	if (copy.Write(const_cast<char*>(inx.Bytes()), size) != size || !copy.IsWhole())
	{
		whyNot = "out of memory copying the origin";
		return kFalse;
	}
	// The document is made with the origin's page setup even here: that is the document, not the
	// XML, and without it the raw copy's frames sit half a page off in a non-facing origin.
	UIDRef ref = UIDRef::gNull;
	if (!NewDocumentLike(inx, ref, whyNot))
		return kFalse;
	// The import and nothing after it - no sacrificial range deleted, no compose, no shape check,
	// no clean mark: the reader asked to see what ImportINX makes of the XML untouched.
	if (!ImportOnly(ref, copy, whyNot))
	{
		KCMCloseRehydrated(ref);	// after ImportOnly returned: nothing of ours stands on it
		return kFalse;
	}
	outDoc = ref;
	return kTrue;
}

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
