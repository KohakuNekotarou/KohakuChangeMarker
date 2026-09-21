//========================================================================================
//
//  KCMRehydrate.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IComposeScanner.h"		// DeleteDummyStory: the first paragraph of each story
#include "IFrameList.h"			// ...its columns, on the way to the frame that holds it
#include "IHierarchy.h"			// column -> multi-column frame -> the item (the two steps)
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
#include "IScriptUtils.h"			// SetScriptingTags - the page labels written INTO the copy (LabelCopyPages)
#include "ISpreadList.h"			// LabelCopyPages: the copy's spreads in document order
#include "ISpread.h"				// LabelCopyPages: a spread's pages in order
#include "RequestContext.h"			// EngineContext - default-constructed: no engine is asking
#include "ScriptData.h"				// ScriptList
#include "ISession.h"
#include "IStoryList.h"
#include "ITextModel.h"
#include "PageItemScrapID.h"		// kDeleteCmdBoss - the dummy's frame goes, and its story with it
#include "IStringData.h"			// NameCopyAfterOrigin: kSetDocNameCmdBoss carries the name here
#include "KCMOrigin.h"				// KCMOriginDocDB - the document the copy stands for

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"				// GlobalErrorStatePreserver
#include "PersistUtils.h"
#include "StreamUtil.h"
#include "Utils.h"
#include "INXCoreID.h"				// IID_IINXIMPORTPOLICY
#include "PMPageSize.h"				// NewDocumentLike
#include "UIDList.h"				// the new-document command's item list
#include "IOpenLayoutCmdData.h"	// GetResultingPresentation - did the window actually appear? (the class is IOpenLayoutPresentationCmdData)
#include "IWindow.h"				// ...and the thing it checks came out, rather than the return code
#include "LayoutUIID.h"			// kOpenLayoutCmdBoss / IID_IOPENLAYOUTCMDDATA (KBSBookScope.cpp does the same)
#include "SnippetID.h"				// kDocElementImportBoss

#include <string>
#include <stdio.h>					// sprintf_s - the nonce as hex
#include <random>					// std::random_device - the nonce (NewSacrificialToken)
#include "WideString.h"

// Project includes:
#include "KCMRehydrate.h"
#include "KCMOrigin.h"				// KCMOriginShape / KCMMeasureShape
#include "KCMResourceBytes.h"
#include "KCMResourceSnapshot.h"	// KCMTakeResourceSnapshot - the copy, photographed for the check
#include "KCMXmlInject.h"
#include "KCMXmlDeepCompare.h"	// the deep pass of the round-trip check (2026-09-21)
#include "KCMOriginIdml.h"		// KCMInxToDesignmap - so the copy is compared as the same kind of document

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

/** The sacrificial token of ONE rehydration: **32 hex digits of a random nonce, and nothing else**
	(2026-09-12 the nonce, 2026-09-15 the name coming off - the user's asks; the reasoning is in
	KCMXmlInject.h). std::random_device may throw on a platform with no entropy source; that is
	caught here, and the fallback mixes the clock with the address of a local, which is still a
	value nobody has typed. ASCII only, so the token can go into XML text and be compared char by
	char after the import. */
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
	return std::string(hex);
}

/** The sacrificial first range (KCMXmlInject.h) is put in for ImportINX to drop - and on a
	one-story document it always was. WITH TWO STORIES ONE OF THEM KEPT IT (2026-09-12: the copy
	came back 9 characters longer than the origin = "KCMDUMMY" + its return, and the shape check
	refused it, which is what the check is for. ⚠"KCMDUMMY" was the token on that day - it is 32
	hex digits and nothing else now, so the number 9 belongs to the story and not to the code).
	So whatever survived is deleted here BY CONTENT:
	a story whose first paragraph is exactly the sacrificial text loses that paragraph. Through a
	command, because the text model takes no other route; the copy is ours and windowless, so the
	undo step lands on nobody's stack.
	★★★SINCE 2026-09-19 THIS IS THE USUAL PATH, NOT THE EXCEPTION: the drop is taken by the decoy
	backing story (KCMXmlInject.h, 3.), so EVERY story's dummy survives the import and is deleted
	here. It had to be so - the story that takes the drop loses the text of its table cells as
	well (measured; the header says how), and no story of the reader's may take it.
	@param dummy the token this rehydration injected (NewSacrificialToken) - the one string both
	       halves of the rule share.
	@return how many paragraphs were deleted (the caller only reports it). */
int32 DeleteDummyStory(IDataBase* db)
{
	int32 deleted = 0;
	if (db == nil)
		return 0;
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories == nil)
		return 0;
	// ⚠The list is walked BACKWARDS and the frame is deleted inside the walk: a deletion takes a
	//  story out of this very list, and an index already passed cannot be disturbed by that.
	for (int32 i = stories->GetUserAccessibleStoryCount() - 1; i >= 0; --i)
	{
		// ⚠Named ...UID, returns a UIDRef (the database is already in it).
		const UIDRef storyRef = stories->GetNthUserAccessibleStoryUID(i);
		// ★★★FOUND BY ITS LABEL, NEVER BY ITS WORDS (measured 2026-09-20). The dummy is EMPTIED by
		//   the import - being emptied is its entire purpose - so a search for the token finds
		//   nothing and leaves it standing, which is exactly what happened the first time this ran.
		//   The label survives the import (stories and spreads keep theirs; only pages lose them),
		//   and nothing of the reader's can carry it: KCMXmlInject.cpp writes it into a story it
		//   invented, in bytes the reader never sees.
		InterfacePtr<IScript> script(storyRef, UseDefaultIID());
		if (script == nil)
			continue;
		const IScriptLabel::ScriptLabelValue value =
			script->GetTag(PMString(kKCMDummyStoryLabelKey));
		if (value.GetUTF8String().empty())
			continue;

		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;

		// ★THE FRAME IS DELETED, AND THE STORY GOES WITH IT. Deleting the text would leave an
		//   empty story and its frame standing, and the shape check counts stories.
		//   column -> multi-column frame -> the item that holds the story: the two steps
		//   KCMRingAdornment.cpp walks, for the same reason (a frame's text is never reached in
		//   one). The dummy is a plain frame written by KCMXmlInject.cpp, so there is no
		//   text-on-path case to allow for here.
		InterfacePtr<IFrameList> frames(model->QueryFrameList());
		if (frames == nil || frames->GetFrameCount() == 0)
			continue;
		InterfacePtr<IHierarchy> column(db, frames->GetNthFrameUID(0), UseDefaultIID());
		if (column == nil)
			continue;
		InterfacePtr<IHierarchy> multiColumn(column->QueryParent());
		if (multiColumn == nil)
			continue;
		const UID holder = multiColumn->GetParentUID();
		if (holder == kInvalidUID)
			continue;
		// kDeleteCmdBoss with the item in its list - codesnippets/SnpManipulateTextFrame.cpp
		// (DeleteTextFrame) is the official shape, minus the ASSERTs and the do-while(false).
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kDeleteCmdBoss));
		if (cmd == nil)
			continue;
		cmd->SetItemList(UIDList(UIDRef(db, holder)));
		if (CmdUtils::ProcessCommand(cmd) == kSuccess)
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
/** 3c. Name the copy's pages after the origin's. ImportINX drops the KcmOriginUid label the
	injection put on every <Page> (measured 2026-09-13: the injected bytes carry all of them, the
	rehydrated copy carries none, while its spreads and stories keep theirs), and the page pairing
	is by UID (KCMPagePairRule.h) - so the copy's pages are labelled HERE, after the import, from
	the spread -> pages table read off the very bytes that were imported (KCMCollectSpreadPages).
	A spread is matched to its origin through its own label, which the import keeps; the first
	spread, reused from the new document and unlabelled, is matched by position. The pages are
	then labelled by index within the spread, which is exact for a copy: it IS the snapshot.
	Written through IScriptUtils::SetScriptingTags, the door KCMPageMarksDoc.cpp uses, with
	replaceExistingLabels kFalse. The copy is a throwaway; nothing here touches the origin.
	@return how many pages were labelled (for the status line's benefit; no caller fails on it). */
int32 LabelCopyPages(IDataBase* db, const KCMResourceBytes& copyXml)
{
	std::vector<KCMXmlSpreadPages> table;
	if (db == nil || !KCMCollectSpreadPages(copyXml.Bytes(), copyXml.Size(), table) || table.empty())
		return 0;
	InterfacePtr<ISpreadList> spreads(db, db->GetRootUID(), UseDefaultIID());
	if (spreads == nil)
		return 0;

	int32 written = 0;
	const int32 n = spreads->GetSpreadCount();
	for (int32 i = 0; i < n; ++i)
	{
		const UID spreadUID = spreads->GetNthSpreadUID(i);
		const KCMXmlSpreadPages* entry = nil;
		UID origin = kInvalidUID;
		if (KCMReadOriginUidLabel(db, spreadUID, origin))
		{
			for (size_t k = 0; k < table.size() && entry == nil; ++k)
				if (table[k].fSpread == origin.Get())
					entry = &table[k];
		}
		else if (i < static_cast<int32>(table.size()))
			entry = &table[i];			// the unlabelled first spread: by position
		if (entry == nil)
			continue;

		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np && p < static_cast<int32>(entry->fPages.size()); ++p)
		{
			InterfacePtr<IScript> script(db, spread->GetNthPageUID(p), UseDefaultIID());
			if (script == nil)
				continue;
			char self[16];
			sprintf_s(self, sizeof(self), "u%x", static_cast<unsigned>(entry->fPages[p]));
			PMString key(kKCMOriginUidLabelKey);
			key.SetTranslatable(kFalse);
			PMString value(self);
			value.SetTranslatable(kFalse);
			IScriptLabel::ScriptLabelKeyValueList labels;
			labels.push_back(IScriptLabel::ScriptLabelKeyValuePair(key, value));
			ScriptList list;
			list.push_back(script);
			if (Utils<IScriptUtils>()->SetScriptingTags(list, EngineContext(), labels,
														kFalse /*replaceExistingLabels*/) == kSuccess)
				++written;
		}
	}
	return written;
}

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
	// takeOwnership kFalse, recycleBoss kFalse: `xml` is the caller's copy, and StreamUtil.h:236-239
	// warns that a recycled stream boss can keep hold of the IXferBytes past its life (the same
	// two flags as KCMResourceSnapshot / KCMResourceParse, and as KT and KIDMCP).
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamRead(&xml, kFalse, kFalse));
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

/* NameCopyAfterOrigin
	Give the copy the NAME of the document it was taken from.

	★★★WHY (measured 2026-09-15). A text variable of the File Name kind resolves from the
	DOCUMENT'S NAME, not from any file on disk: a new untitled document reports its untitled name,
	and setting the name to "allin.indd" makes the same variable read "allin" on the spot. A
	task-start copy IS a new untitled document, so a page carrying that variable came out different
	from the page it was taken from, and the Pixel comparison reported a change nobody had made -
	measured on work/kcm-storyhtml-live/allin.indd: `pages compared=7 changed=1`, and `changed=0`
	with that one instance removed. **Task Start compares one document at two moments; its name is
	the same at both**, so the difference was ours, not the reader's.

	⚠THROUGH THE COMMAND, not IDocument::SetName - the header says third parties use
	 kSetDocNameCmdBoss (IDocument.h:141-143). The SDK contains no caller of it.
	⚠NOTHING IS DONE TO THE USER'S DOCUMENT: the command's item list is the copy.
	★★★THE DATES ARE DELIBERATELY LEFT ALONE (the user's decision, 2026-09-15), and the line that
	 decides it is worth keeping: **a copy's IDENTITY may be corrected; its CONTENT may not be
	 rewritten.** The name is a correction - the document is called the same thing at both moments,
	 and "untitled" was our own doing. Creation / Modification / Output Date are not: they resolve
	 from the document itself (IID_ISTDTIME on kDocBoss has no published header; ILastOutputTime
	 ::Set() takes no argument, "sets it to the current time"), and NOT from the XMP - writing
	 xmp:CreateDate moves the metadata and leaves the variable where it was (measured). The only way
	 left would have been to rewrite those variables into Custom Text ones in the XML before the
	 import, which makes the copy something the origin never was. Task Start's whole shape rests on
	 the copy being a faithful reconstruction (the shape check; the Resources mode reading the
	 origin's own bytes rather than the copy), and a future "write the task-start INX to a file"
	 would carry the lie out of the plug-in.
	⇒ A page carrying a DATE variable therefore always compares as changed in the Pixel mode. That
	 is written in the How to Use, not worked around.
	 Full record: docs/ai-notes/kcm-story-text-export-vocabulary-2026-09-15.md §5-§6.
*/
void NameCopyAfterOrigin(const UIDRef& copyRef, IDataBase* originDB)
{
	IDocument* const origin = DocOf(originDB);
	if (origin == nil)
		return;						// the origin's document has closed: nothing to copy a name from

	PMString name;
	origin->GetName(name);
	if (name.IsEmpty())
		return;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetDocNameCmdBoss));
	if (cmd == nil)
		return;
	cmd->SetItemList(UIDList(copyRef));
	InterfacePtr<IStringData> data(cmd, IID_ISTRINGDATA);
	if (data == nil)
		return;
	data->Set(name);
	CmdUtils::ProcessCommand(cmd);
}

bool16 ImportAndCheck(const UIDRef& ref, KCMResourceBytes& copy, const KCMOriginShape& expect,
					  PMString& whyNot, KCMRehydrateMode mode)
{
	if (!ImportOnly(ref, copy, whyNot))
		return kFalse;
	IDocument* const doc = DocOf(ref.GetDataBase());

	// 3b-3d. ★★**NOT ONE OF THESE IS DONE TO AN UNTOUCHED COPY** (2026-09-20, the user: "doing
	//   nothing to it"). They are the three writes that make a copy fit to be compared - the
	//   surviving dummies deleted, the pages named after the origin's, the document named after the
	//   origin - and every one of them is a correction of something the import did. What is wanted
	//   here is the import's own answer, uncorrected. ⚠So the copy comes out untitled, its pages
	//   unlabelled and whatever the import left in it left in it.
	//   ★kKCMRehydrateKeepForCheck DOES all three: it is the comparison's copy in every respect
	//     but the refusal, which is what makes it worth measuring.
	if (mode != kKCMRehydrateUntouched)
	{
		// 3b. the dummy story, whatever the import left of it, frame and all (DeleteDummyStory).
		//     Before the compose, so that what is composed is the document as it should be.
		{
			GlobalErrorStatePreserver errorState;
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			DeleteDummyStory(ref.GetDataBase());
		}

		// 3c. the pages' origin uids, which the import did not carry (LabelCopyPages says why)
		{
			GlobalErrorStatePreserver errorState;
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			LabelCopyPages(ref.GetDataBase(), copy);
		}

		// 3d. the copy answers as the document it was taken from (NameCopyAfterOrigin says why).
		//     Before the compose, so that what is composed already reads the right name.
		{
			GlobalErrorStatePreserver errorState;
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			NameCopyAfterOrigin(ref, KCMOriginDocDB());
		}
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

bool16 KCMRehydrate(const KCMResourceBytes& inx, const KCMOriginShape& expect, UIDRef& outDoc,
					PMString& whyNot, KCMRehydrateMode mode)
{
	outDoc = UIDRef::gNull;
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (inx.Size() == 0)
	{
		whyNot = "the origin holds no bytes";
		return kFalse;
	}

	// 1. the bytes the import is handed.
	//    ★**UNTOUCHED: THE ORIGIN'S OWN, COPIED AND PASSED ON AS THEY ARE** (the header says what
	//      that measures). No token is made for it either - there is nothing to recognise afterwards.
	//    Otherwise the injected copy, with this rehydration's own sacrificial token (KCMXmlInject.h).
	const bool16 untouched = (mode == kKCMRehydrateUntouched) ? kTrue : kFalse;
	const std::string dummy = untouched ? std::string() : NewSacrificialToken();
	KCMResourceBytes copy;
	BytesSink sink(copy);
	int32 stories = 0, spreads = 0;
	if (untouched)
	{
		// The copy exists only because the import seeks on the stream it is given, and `inx` is
		// the caller's (the held origin). Not one byte is changed on the way through.
		if (!sink.Write(inx.Bytes(), inx.Size()))
		{
			whyNot = "could not take a copy of the origin's bytes (out of memory)";
			return kFalse;
		}
	}
	else if (!KCMInjectForRehydration(inx.Bytes(), inx.Size(), dummy.c_str(), sink, &stories, &spreads))
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
	if (!ImportAndCheck(ref, copy, expect, whyNot, mode))
	{
		// ★★**TWO OF THE THREE MODES KEEP THE COPY EVEN SO** (2026-09-20): the copy that FAILS the
		//   check is the one worth having - it is how a nested table was found to vanish, and a
		//   round-trip check has nothing to measure if the thing it measures is closed first.
		//   ⚠The answer stays kFalse for them: "a document was made" and "it matches the origin"
		//    are two statements, and the comparison acts on the second one.
		if (mode == kKCMRehydrateForComparison)
		{
			KCMCloseRehydrated(ref);	// nothing of ours stands on it any more
			return kFalse;
		}
		KCMMarkRehydratedClean(ref.GetDataBase());
		outDoc = ref;					// with whyNot saying what did not line up
		return kFalse;
	}
	// (Until 2026-09-19 an import's edited words were poured into THIS copy here, for the fourth
	//  mode to compare against the document. They go into the reader's own document now -
	//  KCMStoryTextImport.cpp, KCMPourStoryText - and the copy is the origin, unchanged.)

	// 6. ours, and nothing in it to save (the header says why this matters at a Quit)
	KCMMarkRehydratedClean(ref.GetDataBase());
	outDoc = ref;
	return kTrue;
}

bool16 KCMVerifyRehydration(IDataBase* copyDB, PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	const KCMResourceBytes* const origin = KCMOriginBytes();
	if (origin == nil || origin->Size() == 0)
	{
		out = "no Task Start is held to check against";
		return kFalse;
	}

	// The copy, photographed the way the origin was: ExportINX into memory, nothing on disk.
	// ★★**AND LABELLED AS A DESIGNMAP, BECAUSE THE ORIGIN IS** (2026-09-21). For the element-name
	//   pass it made no difference - the labelling rewrites one word of the processing instruction
	//   and one attribute of <Document>, and neither is a NAME, which is what the old comment here
	//   said and it was true. The deep pass compares attributes, so the same two sides must be the
	//   same KIND of document: without this the copy was reported as missing <Document>'s
	//   xmlns:idPkg on every single run (measured, the first thing the deep pass ever found).
	//   ⇒ **Make the two comparable rather than forgive the difference.**
	KCMResourceBytes copyXml;
	PMString why;
	if (!KCMTakeResourceSnapshot(DocOf(copyDB), copyXml, why))
	{
		out = "the copy could not be photographed: ";
		out.Append(why);
		return kFalse;
	}
	PMString labelWhy;
	if (!KCMInxToDesignmap(copyXml, labelWhy))
	{
		out = "the copy could not be labelled as a designmap: ";
		out.Append(labelWhy);
		return kFalse;
	}

	std::vector<KCMElementCount> diffs;
	int32 same = 0;
	KCMCompareElementCounts(origin->Bytes(), origin->Size(), copyXml.Bytes(), copyXml.Size(),
							diffs, &same);

	// ★THE THREE THAT GROW ON PURPOSE (the header says why). A DROP in any of them is still a real
	//   difference, so the test is on the direction as well as the name.
	int32 real = 0, expected = 0;
	PMString detail;
	detail.SetTranslatable(kFalse);
	for (size_t i = 0; i < diffs.size(); ++i)
	{
		const KCMElementCount& row = diffs[i];
		const bool16 isLabelName = (row.fName == "Label" || row.fName == "KeyValuePair"
									|| row.fName == "Properties") ? kTrue : kFalse;
		if (isLabelName && row.fInB > row.fInA)
		{
			++expected;
			continue;
		}
		++real;
		if (real <= 6)			// a status line has to end somewhere; the largest come first
		{
			detail.Append(real == 1 ? " " : ", ");
			PMString name(row.fName.c_str());
			name.SetTranslatable(kFalse);
			detail.Append(name);
			detail.Append(" ");
			detail.AppendNumber(row.fInA);
			detail.Append("/");
			detail.AppendNumber(row.fInB);
		}
	}

	if (real == 0)
	{
		// ★★**THE CHEAP PASS IS CLEAN - NOW THE EXPENSIVE ONE** (2026-09-21, the user: "is it only
		//   the count of the elements? compare the contents too, even if it takes longer"). Element
		//   names going missing is what the pass above sees; a VALUE that changed - a frame setting,
		//   a cell stroke, a table style, a row height - adds and removes nothing, so only a walk
		//   that compares every attribute can see it (KCMXmlDeepCompare.h).
		// ⚠It runs ONLY when the counts agree, because a walk past a structural difference reports
		//  every attribute after it as changed - noise that would bury the one line that matters.
		std::vector<KCMXmlDifference> deep;
		KCMXmlDeepTally tally;
		const bool16 deepClean = KCMCompareXmlDeep(origin->Bytes(), origin->Size(),
												   copyXml.Bytes(), copyXml.Size(), deep, tally);
		if (!deepClean)
		{
			out = "the copy's ELEMENTS all match (";
			out.AppendNumber(same);
			out.Append(" names) but its CONTENTS do not:");
			if (tally.fDivergedAt >= 0 && tally.fRealDiffs == 0)
			{
				// The shapes stopped lining up although the tallies agreed - the same number of the
				// same names in a different order. Worth its own sentence: it is not a value change.
				out.Append(" the two shapes diverge at element ");
				out.AppendNumber(tally.fDivergedAt);
				out.Append(" (");
				PMString where(tally.fDivergedPath.c_str());
				where.SetTranslatable(kFalse);
				out.Append(where);
				out.Append(")");
				return kFalse;
			}
			out.Append(" ");
			out.AppendNumber(tally.fRealDiffs);
			out.Append(tally.fRealDiffs == 1 ? " difference" : " differences");
			int32 shown = 0;
			for (size_t i = 0; i < deep.size() && shown < 4; ++i)
			{
				if (deep[i].fKind != kKCMXmlDiffReal)
					continue;
				++shown;
				out.Append(shown == 1 ? " - " : ", ");
				PMString piece(deep[i].fPath.c_str());
				piece.SetTranslatable(kFalse);
				out.Append(piece);
				out.Append(" ");
				PMString what(deep[i].fWhat.c_str());
				what.SetTranslatable(kFalse);
				out.Append(what);
				out.Append(" ");
				PMString va(deep[i].fInA.c_str());
				va.SetTranslatable(kFalse);
				out.Append(va);
				out.Append("/");
				PMString vb(deep[i].fInB.c_str());
				vb.SetTranslatable(kFalse);
				out.Append(vb);
			}
			if (tally.fRealDiffs > shown)
				out.Append(", ...");
			return kFalse;
		}

		out = "the copy matches the origin: ";
		out.AppendNumber(same);
		out.Append(" element names, ");
		out.AppendNumber(tally.fAttributes);
		out.Append(" attributes and ");
		out.AppendNumber(tally.fTexts);
		out.Append(" texts equal");
		if (expected > 0)
		{
			out.Append(", ");
			out.AppendNumber(expected);
			out.Append(" grown by our own labels");
		}
		// ★**WHAT WAS FORGIVEN, IN THE OPEN.** A check that hides its exceptions is how a check
		//   starts lying: the counts are printed so that a new kind cannot grow inside them.
		// ⚠**EVERY KIND, NOT THREE OF THEM**: a sum that leaves a kind out is exactly the place a
		//   new kind would grow unseen, which is what this sentence exists to prevent.
		const int32 known = tally.fUids + tally.fOurLabels + tally.fLanguage + tally.fMetadata
						  + tally.fNewDocPrefs + tally.fImportDefaults + tally.fMarkers;
		if (known > 0)
		{
			out.Append(" (");
			out.AppendNumber(known);
			out.Append(" known: ");
			out.AppendNumber(tally.fUids);
			out.Append(" uids, ");
			out.AppendNumber(tally.fOurLabels);
			out.Append(" labels, ");
			out.AppendNumber(tally.fImportDefaults);
			out.Append(" written-out defaults, ");
			out.AppendNumber(tally.fMetadata);
			out.Append(" xmp, ");
			out.AppendNumber(tally.fNewDocPrefs);
			out.Append(" view prefs, ");
			out.AppendNumber(tally.fMarkers);
			out.Append(" markers, ");
			out.AppendNumber(tally.fLanguage);
			out.Append(" language)");
		}
		// ★**AND WHETHER THE STORIES HAD TO BE PUT BACK IN ORDER** - the import writes them in its
		//   own order, and a walk that silently reordered them would be hiding the one fact that
		//   explains why the pairing is by label and not by position.
		if (tally.fStoriesReordered > 0)
		{
			out.Append(" [");
			out.AppendNumber(tally.fStoriesReordered);
			out.Append(" stories came back in another order]");
		}
		return kTrue;
	}

	out = "the copy DIFFERS from the origin in ";
	out.AppendNumber(real);
	out.Append(real == 1 ? " element name:" : " element names:");
	out.Append(detail);
	if (real > 6)
		out.Append(", ...");
	out.Append(" (");
	out.AppendNumber(same);
	out.Append(" equal)");
	return kFalse;
}

void KCMMarkRehydratedClean(IDataBase* db)
{
	if (db != nil && db->IsModified())
		db->SetModified(kFalse);
}

bool16 KCMOpenOriginForInspection(PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	const KCMResourceBytes* const bytes = KCMOriginBytes();
	const KCMOriginShape* const shape = KCMOriginShapeOf();
	if (bytes == nil || shape == nil)
	{
		outMessage = "no Task Start is held - take one first.";
		return kFalse;
	}

	// 1. ★**THE COPY THE COMPARISON WOULD BE GIVEN**, kept whether or not it matches
	//    (kKCMRehydrateKeepForCheck - the enum says what the three modes are for). ⚠kFalse here
	//    does NOT mean "no document": a mismatched copy is exactly the one worth opening, and it
	//    comes back in doc either way, with whyNot saying what did not line up.
	//    ⚠**kKCMRehydrateUntouched is what this asked for on the evening of 2026-09-20** - the held
	//     bytes with nothing injected - which is how the import's own damage was measured. It is
	//     still there to switch back to when that question comes up again.
	UIDRef doc = UIDRef::gNull;
	PMString whyNot;
	const bool16 matched = KCMRehydrate(*bytes, *shape, doc, whyNot, kKCMRehydrateKeepForCheck);
	if (doc == UIDRef::gNull)
	{
		outMessage = "the Task Start copy could not be made at all: ";
		outMessage.Append(whyNot);
		return kFalse;
	}

	// 2. ★A WINDOW, ASKED FOR THE WAY KBSBookScope ASKS FOR ONE (KBSBookScope.cpp, a windowless
	//    chapter): the data interface first - no data interface, no command run - and afterwards
	//    the PRESENTATION is read rather than the return code, because "the command succeeded" and
	//    "there is a window" are two different statements.
	//    ⚠**IT IS ALLOWED TO FAIL.** The document is in app.documents either way, and a windowless
	//     one can still be read by a script; the status line says which happened.
	bool16 hasWindow = kFalse;
	{
		GlobalErrorStatePreserver windowErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kOpenLayoutCmdBoss));
		InterfacePtr<IOpenLayoutPresentationCmdData> openData(cmd, IID_IOPENLAYOUTCMDDATA);
		if (cmd != nil && openData != nil)
		{
			cmd->SetItemList(UIDList(doc));
			if (CmdUtils::ProcessCommand(cmd) == kSuccess)
			{
				InterfacePtr<IWindow> window(openData->GetResultingPresentation(), UseDefaultIID());
				hasWindow = (window != nil) ? kTrue : kFalse;
			}
		}
	}

	// 3. ⚠**IT IS LEFT DIRTY-FREE BUT NOT TIDIED AWAY.** Whoever opened it closes it; nothing here
	//    remembers it, because it is not a copy any comparison will be given.
	// 2b. ★**THE ROUND-TRIP CHECK, ON THE COPY THAT WAS JUST MADE** (2026-09-20, the user's design).
	//     It is run here - on the menu's copy - before anything is said about it, so that what the
	//     status line reports is what the XML says rather than what the shape counters say. The
	//     shape check answers "is it whole enough to compare"; this one answers "what is missing".
	PMString checkSaid;
	const bool16 checkPassed = KCMVerifyRehydration(doc.GetDataBase(), checkSaid);

	outMessage = hasWindow ? "the Task Start copy is open in a window"
						   : "the Task Start copy was made but no window would open for it (it is in app.documents)";
	outMessage.Append(" - ");
	outMessage.Append(checkSaid);
	if (!checkPassed && matched)
	{
		// ⚠**THE TWO INSTRUMENTS CAN DISAGREE, AND THAT IS WORTH SAYING.** The shape check counts
		//   spreads, pages, stories and characters; a nested table that vanishes changes the last
		//   of those, but a fault that does not change any of the four would pass the shape check
		//   and fail here. Saying so is how the next such fault gets noticed.
		outMessage.Append(" [the shape check passed - only the XML saw this]");
	}
	if (!matched)
	{
		// The shape counters, after the XML's account of it: four numbers that say how far off the
		// whole thing is, where the line above says what is missing by name.
		outMessage.Append(" - ");
		outMessage.Append(whyNot);
	}
	// ★★**THE ANSWER IS "IS EVERYTHING WELL", NOT "WAS A DOCUMENT MADE"** (2026-09-20). The caller
	//   uses it to choose the colour of the status line, and what a reader needs coloured is the
	//   bad news. Whether a document appeared is in outMessage, which is the only caller's other
	//   interest. ⚠The one caller ignored the old answer entirely, so nothing was reinterpreted.
	return (checkPassed && matched) ? kTrue : kFalse;
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
