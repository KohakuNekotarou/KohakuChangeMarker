//========================================================================================
//
//  KCMOriginPeek.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBoolData.h"
#include "ICommand.h"
#include "IDataBase.h"
#include "IDocumentList.h"
#include "IHierarchy.h"				// GetSpreadUID - the spread a paired page sits on
#include "ISession.h"
#include "ISpread.h"
#include "ISpreadList.h"

#include <vector>

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "PersistUtils.h"
#include "SpreadID.h"				// kDeleteSpreadCmdBoss
#include "UIDList.h"

// Project includes:
#include "KCMOriginPeek.h"
#include "KCMExternalSource.h"		// KCMIsDbAlive
#include "KCMModelNotify.h"			// KCMNotifyStatus / KCMSayStatus
#include "KCMOrigin.h"
#include "KCMPageMap.h"				// KCMBuildPairing / KCMBuildMasterPairing - the comparison's own page pairing
#include "KCMRehydrate.h"
#include "KCMResourceBytes.h"

namespace
{

UIDRef		sCopy;							// the peek document
IDataBase*	sTargetDB = nil;				// compared, never dereferenced
UID			sTargetSpreadUID = kInvalidUID;	// which Target spread the copy holds
UID			sCopySpreadUID = kInvalidUID;	// that spread's uid in the copy

bool16 CopyAlive()
{
	if (sCopy == UIDRef::gNull)
		return kFalse;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
	return (docList != nil && KCMIsDbAlive(docList, sCopy.GetDataBase())) ? kTrue : kFalse;
}

/** Delete every ordinary spread of copyDB except keepSpread. Masters stay (the pages draw them). */
bool16 KeepOnlySpread(IDataBase* copyDB, UID keepSpread)
{
	InterfacePtr<ISpreadList> spreads(copyDB, copyDB->GetRootUID(), UseDefaultIID());
	if (spreads == nil)
		return kFalse;
	UIDList doomed(copyDB);
	const int32 n = spreads->GetSpreadCount();
	for (int32 i = 0; i < n; ++i)
	{
		const UID uid = spreads->GetNthSpreadUID(i);
		if (uid != keepSpread)
			doomed.Append(uid);
	}
	if (doomed.Length() == 0)
		return kTrue;
	// The shape of SnpManipulateSpreadsAndPages::DeleteSpread (codesnippets, line 874): the
	// command's IBoolData says whether pages may shuffle - not here, the copy's pages stay put.
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kDeleteSpreadCmdBoss));
	InterfacePtr<IBoolData> allowShuffle(cmd, UseDefaultIID());
	if (cmd == nil || allowShuffle == nil)
		return kFalse;
	allowShuffle->Set(kFalse);
	cmd->SetItemList(doomed);
	GlobalErrorStatePreserver errorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return (CmdUtils::ProcessCommand(cmd) == kSuccess) ? kTrue : kFalse;
}

/** The copy's spread that holds the counterpart of targetSpreadUID's pages, through the same page
    pairing the comparison used (ordinary pages by position with the registered ones left out,
    masters by name). kInvalidUID when no page of that spread has a counterpart. */
UID PairedSpread(IDataBase* targetDB, UID targetSpreadUID, IDataBase* copyDB)
{
	InterfacePtr<ISpread> tSpread(targetDB, targetSpreadUID, UseDefaultIID());
	if (tSpread == nil)
		return kInvalidUID;

	std::vector<UID> tPages, sPages;
	KCMBuildPairing(targetDB, copyDB, tPages, sPages);
	{
		std::vector<UID> mT, mS;
		KCMBuildMasterPairing(targetDB, copyDB, mT, mS);
		tPages.insert(tPages.end(), mT.begin(), mT.end());
		sPages.insert(sPages.end(), mS.begin(), mS.end());
	}

	const int32 np = tSpread->GetNumPages();
	for (int32 p = 0; p < np; ++p)
	{
		const UID tPage = tSpread->GetNthPageUID(p);
		for (size_t k = 0; k < tPages.size() && k < sPages.size(); ++k)
		{
			if (tPages[k] != tPage)
				continue;
			InterfacePtr<IHierarchy> hier(copyDB, sPages[k], UseDefaultIID());
			if (hier != nil)
				return hier->GetSpreadUID();
		}
	}
	return kInvalidUID;
}

}	// namespace

IDataBase* KCMOriginPeekDBFor(IDataBase* targetDB, UID targetSpreadUID, UID& outCopySpreadUID)
{
	outCopySpreadUID = kInvalidUID;
	if (targetDB == nil || targetSpreadUID == kInvalidUID || targetDB != KCMOriginDocDB())
		return nil;
	if (CopyAlive() && sTargetDB == targetDB && sTargetSpreadUID == targetSpreadUID)
	{
		outCopySpreadUID = sCopySpreadUID;
		return sCopy.GetDataBase();
	}
	KCMOriginPeekDrop();

	const KCMResourceBytes* bytes = KCMOriginBytes();
	const KCMOriginShape* shape = KCMOriginShapeOf();
	if (bytes == nil || shape == nil)
		return nil;
	UIDRef copy;
	PMString whyNot;
	if (!KCMRehydrate(*bytes, *shape, copy, whyNot))
	{
		PMString msg("could not rebuild the task-start copy for the peek: ");
		msg.SetTranslatable(kFalse);
		msg.Append(whyNot);
		KCMNotifyStatus(msg);
		return nil;
	}
	IDataBase* const copyDB = copy.GetDataBase();
	// ★THE SPREAD IS FOUND THROUGH THE PAGE PAIRING, NOT THROUGH ITS LABEL (2026-09-12, measured
	//  on the first live peek): the copy's FIRST spread is the one the new document was born with,
	//  reused by the import, and it does not receive the <Properties><Label> the injection put on
	//  it - "labels read on 2 of 3 spreads", and page 1 sits on the missing one. The page pairing
	//  is what the comparison itself paired the marks by (KCMBuildPairing: ordinary pages by
	//  position with the registered ones left out, masters by name), so a peek that follows it
	//  lays over exactly the page the ring was computed against. The spread labels stay in the
	//  XML - they cost nothing and the tables in KCMOriginCompare still read them where they
	//  survive - but nothing rests on them any more.
	const UID copySpread = PairedSpread(targetDB, targetSpreadUID, copyDB);
	if (copySpread == kInvalidUID)
	{
		PMString msg("could not find the spread in the task-start copy: no page of spread ");
		msg.SetTranslatable(kFalse);
		msg.AppendNumber(static_cast<int32>(targetSpreadUID.Get()));
		msg.Append(" has a counterpart");
		KCMCloseRehydrated(copy);
		KCMNotifyStatus(msg);
		return nil;
	}
	if (!KeepOnlySpread(copyDB, copySpread))
	{
		KCMCloseRehydrated(copy);
		KCMSayStatus("could not delete the other spreads of the task-start copy");
		return nil;
	}
	sCopy = copy;
	sTargetDB = targetDB;
	sTargetSpreadUID = targetSpreadUID;
	sCopySpreadUID = copySpread;
	outCopySpreadUID = copySpread;
	return copyDB;
}

bool16 KCMOriginPeekMapPage(IDataBase* targetDB, UID targetPageUID, UID& outCopyPageUID)
{
	outCopyPageUID = kInvalidUID;
	if (!CopyAlive() || targetDB != sTargetDB)
		return kFalse;
	InterfacePtr<ISpread> tSpread(targetDB, sTargetSpreadUID, UseDefaultIID());
	InterfacePtr<ISpread> cSpread(sCopy.GetDataBase(), sCopySpreadUID, UseDefaultIID());
	if (tSpread == nil || cSpread == nil)
		return kFalse;
	const int32 n = tSpread->GetNumPages();
	if (n != cSpread->GetNumPages())
		return kFalse;
	for (int32 p = 0; p < n; ++p)
	{
		if (tSpread->GetNthPageUID(p) == targetPageUID)
		{
			outCopyPageUID = cSpread->GetNthPageUID(p);
			return kTrue;
		}
	}
	return kFalse;
}

void KCMOriginPeekDrop()
{
	if (CopyAlive())
		KCMCloseRehydrated(sCopy);
	sCopy = UIDRef::gNull;
	sTargetDB = nil;
	sTargetSpreadUID = kInvalidUID;
	sCopySpreadUID = kInvalidUID;
}

void KCMOriginPeekDescribe(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	if (!CopyAlive())
		out = "-";
	else
		out.AppendNumber(static_cast<int32>(sTargetSpreadUID.Get()));
}

// End, KCMOriginPeek.cpp.
