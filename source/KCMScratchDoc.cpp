//========================================================================================
//
//  KCMScratchDoc.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <set>

#include "ICommand.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentCommands.h"		// CreateNewCommand - the windowless document, as KCMRehydrate makes it
#include "IDocumentList.h"
#include "IDOMElement.h"
#include "IGraphicFrameData.h"		// GetTextContentUID - the imported spline -> its text frame
#include "IHierarchy.h"
#include "IMultiColumnTextFrame.h"	// GetTextModelUID - the text frame -> its story
#include "INewDocCmdData.h"
#include "IPMStream.h"
#include "ISession.h"
#include "ISnippetImport.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "StreamUtil.h"
#include "UIDList.h"
#include "Utils.h"

#include "KCMScratchDoc.h"
#include "KCMExternalSource.h"		// KCMIsDbAlive - the re-check after the close
#include "KCMMemXferBytes.h"
#include "KCMRehydrate.h"			// KCMMarkRehydratedClean / KCMCloseRehydrated

namespace
{

bool16 sLastClosed = kTrue;		// what the most recently destroyed KCMScratchDoc found

/** A Windows-only alias for the bytes: IXferBytes over a std::string, written once, read by the import. */
void FillBytes(const std::string& text, KCMMemXferBytes& into)
{
	if (!text.empty())
		into.Write(const_cast<char*>(text.data()), static_cast<uint32>(text.size()));
	into.Seek(0, kSeekFromStart);
}

void CollectDescendants(IDataBase* db, UID node, std::set<uint32>& into)
{
	InterfacePtr<IHierarchy> hierarchy(db, node, UseDefaultIID());
	if (hierarchy == nil)
		return;
	const int32 count = hierarchy->GetChildCount();
	for (int32 i = 0; i < count; ++i)
	{
		const UID child = hierarchy->GetChildUID(i);
		if (child == kInvalidUID || !into.insert(child.Get()).second)
			continue;
		CollectDescendants(db, child, into);
	}
}

}	// anonymous namespace

KCMScratchDoc::KCMScratchDoc() : fDoc(UIDRef::gNull) {}

KCMScratchDoc::~KCMScratchDoc()
{
	if (fDoc == UIDRef::gNull)
	{
		sLastClosed = kTrue;
		return;
	}
	IDataBase* const db = fDoc.GetDataBase();
	KCMMarkRehydratedClean(db);		// ours, nothing in it to save: the close-all must not ask
	KCMCloseRehydrated(fDoc);
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
	sLastClosed = (docList != nil && !KCMIsDbAlive(docList, db)) ? kTrue : kFalse;
	fDoc = UIDRef::gNull;
}

bool16 KCMScratchDoc::Open(PMString& whyNot)
{
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (fDoc != UIDRef::gNull)
	{
		whyNot = "already open";
		return kFalse;
	}
	InterfacePtr<ICommand> newDocCmd(Utils<IDocumentCommands>()->CreateNewCommand(kSuppressUI));
	InterfacePtr<INewDocCmdData> data(newDocCmd, UseDefaultIID());
	if (newDocCmd == nil || data == nil)
	{
		whyNot = "could not make the new-document command";
		return kFalse;
	}
	data->SetCreateBasicDocument(kFalse);	// the defaults, as KCMRehydrate's NewDocumentLike leaves them
	data->SetNumPages(1);
	// ★THE CALLER'S ERROR STATE IS KEPT OUT OF THIS, the shape NewDocumentLike uses for the very same
	//   command: an error raised here would stand in the global state and pull down the command the
	//   caller runs next (ErrorUtils.h:41-45 - later Sets are ignored until it is cleared).
	GlobalErrorStatePreserver errorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (CmdUtils::ProcessCommand(newDocCmd) != kSuccess)
	{
		whyNot = "could not create a scratch document";
		return kFalse;
	}
	const UIDList& made = newDocCmd->GetItemListReference();
	if (made.Length() == 0 || made.GetRef(0) == UIDRef::gNull)
	{
		whyNot = "the new-document command made nothing";
		return kFalse;
	}
	fDoc = made.GetRef(0);
	return kTrue;
}

IDataBase* KCMScratchDoc::DB() const
{
	return (fDoc == UIDRef::gNull) ? nil : fDoc.GetDataBase();
}

bool16 KCMScratchDoc::ImportSnippet(const std::string& snippet, std::vector<UIDRef>& outStories, PMString& whyNot)
{
	outStories.clear();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	IDataBase* const db = DB();
	if (db == nil || snippet.empty())
	{
		whyNot = "no scratch document, or nothing to import";
		return kFalse;
	}

	// The first spread, and what stands on its page before the import.
	InterfacePtr<IDocument> doc(fDoc, UseDefaultIID());
	InterfacePtr<ISpreadList> spreads(doc, UseDefaultIID());
	const UID spreadUID = (spreads != nil && spreads->GetSpreadCount() > 0) ? spreads->GetNthSpreadUID(0) : kInvalidUID;
	InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
	InterfacePtr<IDOMElement> frag(db, spreadUID, UseDefaultIID());
	Utils<ISnippetImport> importer;
	if (spread == nil || frag == nil || !importer)
	{
		whyNot = "the scratch document's spread could not be opened";
		return kFalse;
	}
	std::set<uint32> before;
	CollectDescendants(db, spreadUID, before);

	// The snippet's bytes, read from memory - the same stream KCMPdfSpike's SnippetOnePageInto reads.
	KCMMemXferBytes bytes;
	FillBytes(snippet, bytes);
	InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&bytes, kFalse, kFalse));
	if (read == nil)
	{
		whyNot = "no memory stream";
		return kFalse;
	}
	ErrorCode err = kFailure;
	{
		// The caller's error state, kept out of the import - the shape KCMRehydrate wraps ImportINX in.
		GlobalErrorStatePreserver errorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		err = importer->ImportFromStream(read, frag, kInvalidClass, kSuppressUI, nil);
		read->Close();
	}
	if (err != kSuccess)
	{
		whyNot = "ImportFromStream refused the snippet";
		return kFalse;
	}

	// The new items: on the spread now and not before. ImportFromStream does not say what it made
	// (KIDMCPRevert.cpp measured the same), so the difference is what names them.
	// ⚠EVERY story, not the first one found: the set walks UIDs in numeric order, which says nothing
	//  about which frame is the table's - a table whose cells hold anchored objects brings in a frame
	//  for each of them, and the lowest UID could be any of the lot. The caller picks by what is IN
	//  the story, which is a fact, not an order.
	std::set<uint32> after;
	CollectDescendants(db, spreadUID, after);
	for (std::set<uint32>::const_iterator it = after.begin(); it != after.end(); ++it)
	{
		if (before.count(*it) != 0)
			continue;
		// A spline with text content -> its multi-column frame -> its story.
		InterfacePtr<IGraphicFrameData> frameData(db, UID(*it), UseDefaultIID());
		if (frameData == nil)
			continue;
		const UID content = frameData->GetTextContentUID();
		if (content == kInvalidUID)
			continue;
		InterfacePtr<IMultiColumnTextFrame> mcf(db, content, UseDefaultIID());
		if (mcf == nil)
			continue;
		const UID story = mcf->GetTextModelUID();
		if (story == kInvalidUID)
			continue;
		outStories.push_back(UIDRef(db, story));
	}
	if (outStories.empty())
	{
		whyNot = "the import made no text frame";
		return kFalse;
	}
	return kTrue;
}

bool16 KCMScratchDoc::LastOneWasClosed()
{
	return sLastClosed;
}

// ---- KCMTargetItemCountGuard --------------------------------------------------------------------

int32 KCMTargetItemCountGuard::Count(IDataBase* db)
{
	if (db == nil)
		return 0;
	InterfacePtr<ISpreadList> spreads(db, db->GetRootUID(), UseDefaultIID());
	if (spreads == nil)
		return 0;
	std::set<uint32> items;
	const int32 n = spreads->GetSpreadCount();
	for (int32 i = 0; i < n; ++i)
		CollectDescendants(db, spreads->GetNthSpreadUID(i), items);
	return static_cast<int32>(items.size());
}

KCMTargetItemCountGuard::KCMTargetItemCountGuard(IDataBase* target)
	: fTarget(target), fThen(Count(target)) {}

bool16 KCMTargetItemCountGuard::Unchanged() const
{
	return (Delta() == 0) ? kTrue : kFalse;
}

int32 KCMTargetItemCountGuard::Delta() const
{
	return Count(fTarget) - fThen;
}

// End, KCMScratchDoc.cpp.
