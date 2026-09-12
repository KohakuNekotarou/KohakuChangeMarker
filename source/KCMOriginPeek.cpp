//========================================================================================
//
//  KCMOriginPeek.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocumentList.h"
#include "IHierarchy.h"				// GetSpreadUID - the spread a paired page sits on
#include "ISession.h"
#include "ISpread.h"

#include <vector>

// General includes:
#include "PersistUtils.h"

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

	// The copy is WHOLE and is kept across spreads: a press on another spread of the same Target
	// only looks its counterpart up again below. ⚠It used to be cut down to the one spread
	// (kDeleteSpreadCmdBoss on every other), and that was a defect, measured 2026-09-12 evening:
	// InDesign does not delete the text of a threaded story with the spreads its frames sit on -
	// it REFLOWS it into the frames that remain, so the copy's page 2 showed the origin's page-1
	// text ("L1..." where "L4..." had stood) and the peek laid the wrong words over the page. The
	// pages the peek draws are the paired ones only (MakeOrigImage per page), so nothing needed the
	// other spreads gone; the deletion bought speed and cost correctness.
	if (!(CopyAlive() && sTargetDB == targetDB))
	{
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
		sCopy = copy;
		sTargetDB = targetDB;
		sTargetSpreadUID = kInvalidUID;
		sCopySpreadUID = kInvalidUID;
	}
	IDataBase* const copyDB = sCopy.GetDataBase();

	// ★THE SPREAD IS FOUND THROUGH THE PAGE PAIRING, NOT THROUGH ITS LABEL (2026-09-12, measured
	//  on the first live peek): the copy's FIRST spread is the one the new document was born with,
	//  reused by the import, and it does not receive the <Properties><Label> the injection put on
	//  it - "labels read on 2 of 3 spreads", and page 1 sits on the missing one. The page pairing
	//  is what the comparison itself paired the marks by (KCMBuildPairing: ordinary pages by
	//  position with the registered ones left out, masters by name), so a peek that follows it
	//  lays over exactly the page the ring was computed against. The spread labels stay in the
	//  XML - they cost nothing and the tables in KCMOriginCompare still read them where they
	//  survive - but nothing rests on them any more.
	if (sTargetSpreadUID != targetSpreadUID || sCopySpreadUID == kInvalidUID)
	{
		const UID copySpread = PairedSpread(targetDB, targetSpreadUID, copyDB);
		if (copySpread == kInvalidUID)
		{
			// The copy stays (it is whole and good for the other spreads); only this press has no
			// counterpart to show.
			PMString msg("could not find the spread in the task-start copy: no page of spread ");
			msg.SetTranslatable(kFalse);
			msg.AppendNumber(static_cast<int32>(targetSpreadUID.Get()));
			msg.Append(" has a counterpart");
			KCMNotifyStatus(msg);
			return nil;
		}
		sTargetSpreadUID = targetSpreadUID;
		sCopySpreadUID = copySpread;
	}
	outCopySpreadUID = sCopySpreadUID;
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

void KCMOriginPeekDrop(bool16 deferred)
{
	// Forgotten first, closed second. The close raises kAfterCloseDoc, whose sweep reaches
	// KCMReleaseOrigin and this function again; with the statics already empty that second call
	// finds nothing and returns, instead of closing the same document twice.
	const UIDRef doomed = sCopy;
	const bool16 alive = CopyAlive();
	sCopy = UIDRef::gNull;
	sTargetDB = nil;
	sTargetSpreadUID = kInvalidUID;
	sCopySpreadUID = kInvalidUID;
	if (alive)
		KCMCloseRehydrated(doomed, deferred);
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
