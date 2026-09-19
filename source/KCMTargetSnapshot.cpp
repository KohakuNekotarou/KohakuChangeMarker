//========================================================================================
//
//  KCMTargetSnapshot.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "ISession.h"
#include "K2SmartPtr.h"			// K2::scoped_ptr - the one holder of the bytes

#include "KCMTargetSnapshot.h"
#include "KCMResourceBytes.h"
#include "KCMResourceSnapshot.h"	// KCMTakeResourceSnapshot - the same export the Resources mode makes

namespace
{

// ⚠A static holding a buffer: it has a line in the model's shutdown (KCMPeek.cpp's ShutdownCleanup),
//   the rule KCMStoryList.h states for every static of ours.
K2::scoped_ptr<KCMResourceBytes>	sBytes;

}	// anonymous namespace

bool16 KCMTargetSnapshotTake(IDataBase* targetDB, PMString& whyNot)
{
	sBytes.reset();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (targetDB == nil)
	{
		whyNot = "no Target document";
		return kFalse;
	}
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
	IDocument* const doc = (docList != nil) ? docList->FindDocByDataBase(targetDB) : nil;
	if (doc == nil)
	{
		whyNot = "the Target is not an open document";
		return kFalse;
	}
	K2::scoped_ptr<KCMResourceBytes> taken(new (std::nothrow) KCMResourceBytes());
	if (taken.get() == nil)
	{
		whyNot = "no memory for the Target snapshot";
		return kFalse;
	}
	// ★CLEAN GOING IN, CLEAN COMING OUT. This is taken on the way INTO a comparison, which is a
	//   reading, and a reading must not be what makes the reader's document ask to be saved - the
	//   guard every comparison path in KCM holds (KCMStoryDiffRun.cpp:1942, KCMPeek.cpp:262).
	IDataBase::SaveRestoreModifiedState dirtyGuard(targetDB);
	if (!KCMTakeResourceSnapshot(doc, *taken, whyNot))
		return kFalse;
	sBytes.reset(taken.release());
	return kTrue;
}

const KCMResourceBytes* KCMTargetSnapshotBytes()
{
	return sBytes.get();
}

void KCMTargetSnapshotDrop()
{
	sBytes.reset();
}

// End, KCMTargetSnapshot.cpp.
