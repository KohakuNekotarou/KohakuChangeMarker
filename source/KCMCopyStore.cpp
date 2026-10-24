//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  KCM's own copy stores - see KCMCopyStore.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IDataBase.h"
#include "IDataExchangeController.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"		// DeleteCmd - an empty copy put back
#include "ITextStoryThread.h"	// QueryStoryThread - the thread a range lies in
#include "ITextUtils.h"			// CreateNewStory, QueryCopyStoryToEndCommand, QueryCopyStoryCommand

// General includes:
#include "CDataExchangeController.h"
#include "CmdUtils.h"
#include "CreateObject.h"
#include "DBUtils.h"
#include "ErrorUtils.h"
#include "RangeData.h"			// StoryRange
#include "Utils.h"

#include <map>

// Project includes:
#include "KCMCopyStore.h"
#include "KCMDiag.h"			// KCM_DIAG_LOG / KCM_DIAG_FAULT - test builds only
#include "KCMID.h"

namespace
{
// THE NUMBER THE NEXT STORE IS MADE WITH - read by the controller as it is constructed, for its scrap database's file
// name: CDataExchangeController.h asks that name to be unique, and InDesign keeps a scrap database as a file in its
// Caches folder (its own clipboards are "InDesign ClipboardScrap1", "...2" - measured for KFC). One store per
// document, so each takes the number of its own.
uint32 gNamingNumber = 0;
}

/** THE STORE'S CONTROLLER - KFC's KFCCopyStoreController (itself the shape of KIDMCP's task-start store, measured first
	in KT: docs/ai-notes/private-scrap-store-2026-10-06.md). Nothing registers it with the clipboard or a drag, so the
	person's clipboard is never touched and the scrap is nobody else's. */
class KCMCopyStoreController : public CDataExchangeController
{
public:
	KCMCopyStoreController(IPMUnknown* boss) : CDataExchangeController(boss), fNumber(gNamingNumber) {}
	virtual ~KCMCopyStoreController() {}

	virtual IDataExchangeHandler* QueryHandlerForInternalize(DataExchangeResponse& /*response*/,
		const PMFlavor& /*internalizeToThisFlavor*/) const
	{
		return nil;		// nothing is ever brought in from outside
	}
	virtual bool16 NeedsToInternalize() const
	{
		return kFalse;
	}
	virtual ErrorCode FulfillPromise(IPMDataObject* /*whichItem*/, const ExternalPMFlavor& /*whichFlavor*/) const
	{
		return kFailure;	// nothing is ever promised to another application
	}

	/** NEVER PURGED: a scrap may be purged when memory runs low (CDataExchangeController.h, "Memory purge") - the
		clipboard's can be, since the person can copy again. These are the only copies of what Apply This Change wrote
		over, so a store is let go by KCMCopyStore and by nothing else. */
	virtual void Purge(int32 /*level*/) {}

protected:
	virtual PMString GetScrapDBFileName() const
	{
		PMString name("KohakuChangeMarkerCopyStore");
		name.AppendNumber(static_cast<int32>(fNumber));
		name.SetTranslatable(kFalse);
		return name;
	}
	virtual IDataBase* CreateDataBase() const
	{
		// DBUtils.h: set the magic bytes of the kind of database to make, make it, set them back.
		static const DBUtils::MagicBytes kStoreBytes = { 'K', 'C', 'M', 'C', 'O', 'P', 'Y', '1' };
		DBUtils::SetMagicBytes(kStoreBytes);
		IDataBase* db = DBUtils::CreateDataBase(IDataBase::kNoUndoSupport);
		ResetMagicBytes();
		return db;
	}
	virtual void ResetMagicBytes() const
	{
		static const DBUtils::MagicBytes kDocumentBytes = { 'D', 'O', 'C', 'U', 'M', 'E', 'N', 'T' };
		DBUtils::SetMagicBytes(kDocumentBytes);
	}

private:
	const uint32	fNumber;
};

CREATE_PMINTERFACE(KCMCopyStoreController, kKCMCopyStoreImpl)

namespace
{
// One store: its controller, and the document it holds copies of.
struct Store
{
	IDataExchangeController*	controller;	// held - let go by LetGo
	IDataBase*					openDB;		// the document. Compared, never followed.
	Store() : controller(nil), openDB(nil) {}
};

// THE STORES, BY NUMBER. A RAW POINTER IN EACH, NOT AN InterfacePtr: they are let go while the session is whole
// (ShutdownCleanup); a static InterfacePtr would release whatever is left at DLL unload, after the object model has gone.
std::map<uint32, Store>	gStores;
uint32					gLastNumber = 0;

void LetGo(Store& store)
{
	IDataExchangeController* const going = store.controller;
	store.controller = nil;
	store.openDB = nil;
	if (going != nil)
	{
		// Cleanup deletes the scrap database ("Release referenced objects and destroy database",
		// CDataExchangeController.h); a scrap is independent of the document it was copied from, so this is safe before
		// or after that document closes (KT measured copies outliving their document).
		going->Cleanup();
		going->Release();
	}
}

// The store's database, or nil (no such store, or one let go).
IDataBase* StoreDB(uint32 number)
{
	const std::map<uint32, Store>::const_iterator it = gStores.find(number);
	return (it != gStores.end() && it->second.controller != nil) ? it->second.controller->GetScrapDataBase() : nil;
}

// THE STORE OF A DOCUMENT: the one bound to it, else a new one. 0 = none could be made.
uint32 StoreFor(IDataBase* db)
{
	for (std::map<uint32, Store>::iterator it = gStores.begin(); it != gStores.end(); ++it)
		if (it->second.controller != nil && it->second.openDB == db)
			return it->first;
	const uint32 number = gLastNumber + 1;
	gNamingNumber = number;		// read by the controller's constructor (its scrap database's file name)
	IDataExchangeController* const controller = ::CreateObject2<IDataExchangeController>(kKCMCopyStoreBoss);
	if (controller == nil)
	{
		KCM_DIAG_LOG("COPY store could not be made - the boss (is KCMFactoryList.h current?)");
		return 0;
	}
	const ErrorCode init = controller->Initialize();
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (init != kSuccess || !controller->IsValid() || controller->GetScrapDataBase() == nil)
	{
		KCM_DIAG_LOG("COPY store could not be made err=%d valid=%d", (int)init, controller->IsValid() ? 1 : 0);
		controller->Cleanup();
		controller->Release();
		return 0;
	}
	Store store;
	store.controller = controller;
	store.openDB = db;
	gStores[number] = store;
	gLastNumber = number;
	KCM_DIAG_LOG("COPY store %u made doc=%p", (unsigned)number, (void*)db);
	return number;
}

// The primary thread's text length of a story - its span without the closing return; -1 when it cannot be read.
int32 PrimaryLength(const UIDRef& story)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	return (model != nil) ? model->GetPrimaryStoryThreadSpan() - 1 : -1;
}
}	// anonymous namespace

bool16 KCMCopyStore::InOneThread(ITextModel* model, TextIndex start, TextIndex end)
{
	if (model == nil || start < 0 || end < start || end > model->TotalLength())
		return kFalse;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(start, &threadStart, &threadSpan));
	if (thread == nil || threadSpan <= 0)
		return kFalse;
	// Never the thread's own closing character (KCMStorySyncApply's ApplyParagraph keeps the same distance).
	return (end <= threadStart + threadSpan - 1) ? kTrue : kFalse;
}

bool16 KCMCopyStore::Take(const UIDRef& story, TextIndex start, TextIndex end, CopyRef& outCopy)
{
	outCopy = CopyRef();
	IDataBase* const db = story.GetDataBase();
	// NOTHING PROCESSED OVER AN ERROR ALREADY STANDING: a command processed while the global error code is set is
	// InDesign's protective shutdown (CmdUtils.h, ProcessCommand) - and the error is not this function's to clear.
	if (db == nil || ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		return kFalse;
	if (KCM_DIAG_FAULT("copy-fail"))
	{
		KCM_DIAG_LOG("COPY take story=%u at=%d len=%d ok=0 (fault switch)", story.GetUID().Get(), (int)start, (int)(end - start));
		return kFalse;
	}
	{
		InterfacePtr<ITextModel> model(story, UseDefaultIID());
		if (!InOneThread(model, start, end))
			return kFalse;
	}
	const uint32 number = StoreFor(db);
	IDataBase* const sdb = StoreDB(number);
	if (sdb == nil)
		return kFalse;
	const UID copy = Utils<ITextUtils>()->CreateNewStory(sdb, kFalse);
	bool ok = (copy != kInvalidUID) && ErrorUtils::PMGetGlobalErrorCode() == kSuccess;
	if (ok && end > start)
	{
		InterfacePtr<ICommand> cmd(Utils<ITextUtils>()->QueryCopyStoryToEndCommand(story, UIDRef(sdb, copy),
			StoryRange(start, end)));
		ok = (cmd != nil && CmdUtils::ProcessCommand(cmd) == kSuccess && ErrorUtils::PMGetGlobalErrorCode() == kSuccess);
	}
	// What the copy raised is the store's, not the caller's - who found no error standing.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	CopyRef taken;
	taken.store = number;
	taken.story = copy;
	// THE COPY MUST READ AS LONG AS THE RANGE: a copy that came out otherwise is not offered back.
	const int32 len = ok ? Length(taken) : -1;
	ok = ok && (len == end - start);
	KCM_DIAG_LOG("COPY take story=%u at=%d len=%d -> store=%u copy=%u copied=%d ok=%d", story.GetUID().Get(), (int)start,
		(int)(end - start), (unsigned)number, copy.Get(), (int)len, ok ? 1 : 0);
	if (!ok)
		return kFalse;
	outCopy = taken;
	return kTrue;
}

int32 KCMCopyStore::Length(const CopyRef& copy)
{
	IDataBase* const sdb = StoreDB(copy.store);
	if (sdb == nil || copy.story == kInvalidUID || !sdb->IsValidUID(copy.story))
		return -1;
	return PrimaryLength(UIDRef(sdb, copy.story));
}

bool16 KCMCopyStore::PasteOver(const CopyRef& copy, const UIDRef& story, TextIndex start, TextIndex end)
{
	const int32 len = Length(copy);
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	// The same check as the copy's: the commands check nothing.
	if (len < 0 || !InOneThread(model, start, end) || ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		KCM_DIAG_LOG("COPY paste refused store=%u copy=%u len=%d story=%u at=%d over=%d", (unsigned)copy.store,
			copy.story.Get(), (int)len, story.GetUID().Get(), (int)start, (int)(end - start));
		return kFalse;
	}
	ErrorCode result = kSuccess;
	if (len == 0)
	{
		// AN EMPTY COPY (Apply put words in where the document had none): the range written is taken away.
		if (end > start)
		{
			InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
			InterfacePtr<ICommand> del(cmds != nil ? cmds->DeleteCmd(start, end - start) : nil);
			result = (del != nil) ? CmdUtils::ProcessCommand(del) : kFailure;
		}
	}
	else
	{
		// ONE COMMAND THAT REPLACES THE RANGE WITH THE COPY (ITextUtils.h: "This replaces the range in the destination
		// story with a copy of the specified text from the source story").
		InterfacePtr<ICommand> cmd(Utils<ITextUtils>()->QueryCopyStoryCommand(UIDRef(StoreDB(copy.store), copy.story),
			story, StoryRange(0, len), StoryRange(start, end)));
		result = (cmd != nil) ? CmdUtils::ProcessCommand(cmd) : kFailure;
	}
	const bool16 ok = (result == kSuccess && ErrorUtils::PMGetGlobalErrorCode() == kSuccess) ? kTrue : kFalse;
	KCM_DIAG_LOG("COPY paste store=%u copy=%u len=%d story=%u at=%d over=%d ok=%d", (unsigned)copy.store, copy.story.Get(),
		(int)len, story.GetUID().Get(), (int)start, (int)(end - start), ok ? 1 : 0);
	return ok;
}

bool16 KCMCopyStore::CopyOver(const UIDRef& from, TextIndex fromStart, TextIndex fromEnd, const UIDRef& story,
							  TextIndex start, TextIndex end)
{
	IDataBase* const fromDB = from.GetDataBase();
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	bool16 inThreads = kFalse;
	if (fromDB != nil && ErrorUtils::PMGetGlobalErrorCode() == kSuccess)
	{
		IDataBase::SaveRestoreModifiedState fromGuard(fromDB);
		InterfacePtr<ITextModel> fromModel(from, UseDefaultIID());
		// The same check as the copy's and the paste's, on both sides: the commands check nothing.
		inThreads = (InOneThread(fromModel, fromStart, fromEnd) && InOneThread(model, start, end)) ? kTrue : kFalse;
	}
	if (!inThreads)
	{
		KCM_DIAG_LOG("COPY over refused from=%u at=%d len=%d story=%u at=%d over=%d", from.GetUID().Get(), (int)fromStart,
			(int)(fromEnd - fromStart), story.GetUID().Get(), (int)start, (int)(end - start));
		return kFalse;
	}
	ErrorCode result = kSuccess;
	if (fromEnd == fromStart)
	{
		// NOTHING ON THE OTHER SIDE: the range is taken away (PasteOver's empty copy).
		if (end > start)
		{
			InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
			InterfacePtr<ICommand> del(cmds != nil ? cmds->DeleteCmd(start, end - start) : nil);
			result = (del != nil) ? CmdUtils::ProcessCommand(del) : kFailure;
		}
	}
	else
	{
		// The other document only READ by the copy - its modified state kept as it was found.
		IDataBase::SaveRestoreModifiedState fromGuard(fromDB);
		InterfacePtr<ICommand> cmd(Utils<ITextUtils>()->QueryCopyStoryCommand(from, story, StoryRange(fromStart, fromEnd),
			StoryRange(start, end)));
		result = (cmd != nil) ? CmdUtils::ProcessCommand(cmd) : kFailure;
	}
	const bool16 ok = (result == kSuccess && ErrorUtils::PMGetGlobalErrorCode() == kSuccess) ? kTrue : kFalse;
	KCM_DIAG_LOG("COPY over from=%u at=%d len=%d story=%u at=%d over=%d ok=%d", from.GetUID().Get(), (int)fromStart,
		(int)(fromEnd - fromStart), story.GetUID().Get(), (int)start, (int)(end - start), ok ? 1 : 0);
	return ok;
}

void KCMCopyStore::DocumentClosing(IDataBase* db)
{
	if (db == nil)
		return;
	for (std::map<uint32, Store>::iterator it = gStores.begin(); it != gStores.end(); )
	{
		if (it->second.openDB != db)
		{
			++it;
			continue;
		}
		KCM_DIAG_LOG("COPY store %u let go - its document closes", (unsigned)it->first);
		LetGo(it->second);
		it = gStores.erase(it);
	}
}

void KCMCopyStore::ShutdownCleanup()
{
	for (std::map<uint32, Store>::iterator it = gStores.begin(); it != gStores.end(); ++it)
		LetGo(it->second);
	std::map<uint32, Store>().swap(gStores);	// fresh storage, not clear() (memory plugin-teardown-robustness)
	gLastNumber = 0;
}

// End, KCMCopyStore.cpp.
