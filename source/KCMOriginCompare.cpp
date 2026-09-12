//========================================================================================
//
//  KCMOriginCompare.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "ISpreadList.h"
#include "IStoryList.h"

// General includes:
#include "PersistUtils.h"
#include "PMString.h"

#include <map>

// Project includes:
#include "KCMOriginCompare.h"

#include "KCMComparisonRun.h"		// KCMStartComparisonOn / KCMStopComparison / KCMChosenSourceIsOrigin
#include "KCMCore.h"				// KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB / KCMDetachArmedSource
#include "KCMID.h"				// kKCMMarksRebuiltMessage
#include "KCMModelNotify.h"			// KCMSayStatus / KCMNotifyStatus / KCMNotify
#include "KCMOrigin.h"
#include "KCMOriginPeek.h"			// KCMOriginPeekDrop
#include "KCMRehydrate.h"

namespace
{

bool16					sArmedSourceIsOrigin = kFalse;
IDataBase*				sRunSourceDB = nil;			// the copy, while the procedure holds it
std::map<UID, UID>		sOriginToSource;			// original story/spread uid -> the copy's
std::map<UID, UID>		sSourceToOrigin;

/** Read every user story's and every spread's label in the copy into the two tables. */
void BuildTables(IDataBase* copyDB)
{
	sOriginToSource.clear();
	sSourceToOrigin.clear();
	if (copyDB == nil)
		return;
	InterfacePtr<IStoryList> stories(copyDB, copyDB->GetRootUID(), UseDefaultIID());
	if (stories != nil)
	{
		const int32 n = stories->GetUserAccessibleStoryCount();
		for (int32 i = 0; i < n; ++i)
		{
			const UID copyUID = stories->GetNthUserAccessibleStoryUID(i).GetUID();
			UID original = kInvalidUID;
			if (KCMReadOriginUidLabel(copyDB, copyUID, original))
			{
				sOriginToSource[original] = copyUID;
				sSourceToOrigin[copyUID] = original;
			}
		}
	}
	InterfacePtr<ISpreadList> spreads(copyDB, copyDB->GetRootUID(), UseDefaultIID());
	if (spreads != nil)
	{
		const int32 n = spreads->GetSpreadCount();
		for (int32 i = 0; i < n; ++i)
		{
			const UID copyUID = spreads->GetNthSpreadUID(i);
			UID original = kInvalidUID;
			if (KCMReadOriginUidLabel(copyDB, copyUID, original))
			{
				sOriginToSource[original] = copyUID;
				sSourceToOrigin[copyUID] = original;
			}
		}
	}
}

/** The whole run: rehydrate, compare, detach, close. */
bool16 Run(bool16 isRefresh)
{
	IDataBase* const targetDB = KCMOriginDocDB();
	if (targetDB == nil || KCMOriginBytes() == nil)
	{
		KCMReleaseOrigin();
		KCMSayStatus("The Task Start origin's document has closed - origin released.");
		KCMNotify(kKCMMarksRebuiltMessage);
		return kFalse;
	}

	bool16 compared = kFalse;
	{
		KCMOriginScopedCopy copy;
		PMString whyNot;
		if (!copy.Open(whyNot))
		{
			PMString msg("could not rebuild the task-start copy: ");
			msg.SetTranslatable(kFalse);
			msg.Append(whyNot);
			KCMNotifyStatus(msg);
			KCMNotify(kKCMMarksRebuiltMessage);
			return kFalse;
		}
		compared = KCMStartComparisonOn(targetDB, copy.DB());
		// The copy leaves the armed state BEFORE it is closed (the scope's end), so that the close
		// sweep finds no pointer of ours at it (KCMHandleDocsClosed would otherwise clear everything).
		KCMDetachArmedSource();
	}

	if (compared)
	{
		sArmedSourceIsOrigin = kTrue;
		return kTrue;
	}
	// A cancel (or a failure): the ordinary routes' rule - a Start leaves nothing armed, a Refresh
	// stops so that the panel does not read Stop with no marks on screen.
	sArmedSourceIsOrigin = kFalse;
	if (isRefresh)
	{
		KCMStopComparison();
		KCMSayStatus("refresh cancelled - comparison stopped");
	}
	return kFalse;
}

}	// namespace

//----------------------------------------------------------------------------------------
// KCMOriginScopedCopy
//----------------------------------------------------------------------------------------

KCMOriginScopedCopy::KCMOriginScopedCopy() : fDoc(UIDRef::gNull) {}

KCMOriginScopedCopy::~KCMOriginScopedCopy()
{
	if (fDoc == UIDRef::gNull)
		return;
	// Forgotten first, closed second: the close raises the sweep, and by then nothing of ours
	// names the copy (the same order as KCMOriginPeekDrop).
	sOriginToSource.clear();
	sSourceToOrigin.clear();
	sRunSourceDB = nil;
	KCMCloseRehydrated(fDoc);
}

bool16 KCMOriginScopedCopy::Open(PMString& whyNot)
{
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	const KCMResourceBytes* bytes = KCMOriginBytes();
	const KCMOriginShape* shape = KCMOriginShapeOf();
	if (bytes == nil || shape == nil)
	{
		whyNot = "no origin is held";
		return kFalse;
	}
	if (sRunSourceDB != nil || fDoc != UIDRef::gNull)
	{
		whyNot = "a task-start copy is already open";
		return kFalse;
	}
	if (!KCMRehydrate(*bytes, *shape, fDoc, whyNot))
	{
		fDoc = UIDRef::gNull;
		return kFalse;
	}
	sRunSourceDB = fDoc.GetDataBase();
	BuildTables(sRunSourceDB);
	return kTrue;
}

IDataBase* KCMOriginScopedCopy::DB() const
{
	return (fDoc == UIDRef::gNull) ? nil : fDoc.GetDataBase();
}

bool16 KCMOriginStart()
{
	if (!KCMChosenSourceIsOrigin())
		return kFalse;
	return Run(kFalse);
}

bool16 KCMOriginRefresh()
{
	if (!KCMOriginArmed())
		return kFalse;
	return Run(kTrue);
}

bool16 KCMOriginArmed()
{
	// The flag AND the state it describes (the header says which stale reading this closes).
	return (sArmedSourceIsOrigin && KCMHasOrigin() && KCMIsArmed()
			&& KCMArmedTargetDB() != nil && KCMArmedSourceDB() == nil) ? kTrue : kFalse;
}

void KCMOriginOnStop()
{
	sArmedSourceIsOrigin = kFalse;
	KCMOriginPeekDrop();
}

bool16 KCMOriginRunInProgress()		{ return (sRunSourceDB != nil) ? kTrue : kFalse; }

UID KCMOriginToSourceUID(IDataBase* sourceDB, UID originalUID)
{
	if (sourceDB == nil || sourceDB != sRunSourceDB)
		return originalUID;
	std::map<UID, UID>::const_iterator it = sOriginToSource.find(originalUID);
	return (it != sOriginToSource.end()) ? it->second : originalUID;
}

UID KCMSourceToOriginUID(IDataBase* sourceDB, UID sourceUID)
{
	if (sourceDB == nil || sourceDB != sRunSourceDB)
		return sourceUID;
	std::map<UID, UID>::const_iterator it = sSourceToOrigin.find(sourceUID);
	return (it != sSourceToOrigin.end()) ? it->second : sourceUID;
}

// End, KCMOriginCompare.cpp.
