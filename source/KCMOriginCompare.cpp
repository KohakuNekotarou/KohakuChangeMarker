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

#include "KCMComparisonRun.h"		// KCMStartComparisonOn / KCMStopComparison
#include "KCMPairChoice.h"		// KCMChosenSourceIsOrigin
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

		// ★★**AND IF THE COPY WAS SHORT, SAY SO IN RED** (2026-09-21). The comparison has just
		//   written its own line ("pages compared=N changed=M"), and that line is worth keeping - so
		//   the check's words are appended to it rather than written over it, and the whole sentence
		//   is raised again as a warning. ⚠KCMNotifyStatusWarning, not the UI's own setter: this is
		//   model code ([[model-plugin-must-not-drive-ui]]).
		if (!copy.CheckPassed())
		{
			PMString said;
			copy.CheckSaid(said);
			PMString msg;
			KCMGetSessionStatus(msg);			// what the comparison itself just said
			// ⚠**THE MARK GOES ON AFTER THE ASSIGNMENT, NOT BEFORE IT** (found in review): the Get
			//  assigns a whole PMString and takes its translatable flag with it, so a mark set first
			//  is thrown away - and a finished sentence left translatable comes back out of the
			//  string table as something else (KCMUIShared.h says it in full).
			msg.SetTranslatable(kFalse);
			if (!msg.IsEmpty())
				msg.Append(" - ");
			msg.Append(said);
			KCMNotifyStatusWarning(msg);
		}
	}

	// ⚠**THE TARGET'S WHOLE INTERNAL IDML WAS TAKEN HERE FOR ONE DAY** (2026-09-20) and is gone the
	//   same day. It was taken "because it will be useful later", and by the evening it had exactly
	//   ONE reader left - the style groups a Table restore's snippet needs - while the table itself
	//   was being exported fresh at the moment of the restore instead (A-2). A document held in
	//   memory for the length of a comparison, to carry a few KB of style names.
	//   ★What replaced it (the user: "prepare a snippet for the tables that changed, and only for
	//    those"): KCMStoryDiffRun builds a snippet for each table it calls changed, and the story's
	//    own INX export carries the style roots with it - KCMStoryChange::fTableSnippet,
	//    KCMExportStoryInx's includeStyleRoots. Nothing is held for a story whose tables all agree.

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

KCMOriginScopedCopy::KCMOriginScopedCopy()
	: fDoc(UIDRef::gNull), fCheckPassed(kTrue)	// kTrue: nothing opened, nothing wrong
{
	fCheckSaid.SetTranslatable(kFalse);
}

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

	// ★★**THE ROUND-TRIP CHECK, ON THE COPY THE COMPARISON IS ABOUT TO USE** (2026-09-21, the user:
	//   "put it in the comparison path as well"). Until today it ran only on the menu's copy
	//   ("Open Task Start as IDML"), which means a comparison could run against a copy that was
	//   short and say nothing. It does not veto the comparison - see the header - it is reported.
	//   ⚠It costs one ExportINX of the copy (78-160ms measured at 190KB) plus the comparison of the
	//    two element tallies. The user's rule for this plug-in is accuracy over speed
	//    ([[accuracy-over-speed-in-mcp]]), and this is the instrument that catches a copy the reader
	//    would otherwise be shown as if it were the origin.
	fCheckPassed = KCMVerifyRehydration(sRunSourceDB, fCheckSaid);
	// ⚠**AND LEAVE IT CLEAN AGAIN.** The check photographs the copy, which calls Reset() on its DOM
	//  element - enough to mark the database modified. The copy is closed with kSuppressUI so
	//  nothing would be asked, but "ours, and nothing in it to save" is the state the rest of this
	//  plug-in relies on (KCMRehydrate.h), and a check must not change what it measures.
	KCMMarkRehydratedClean(sRunSourceDB);
	return kTrue;
}

bool16 KCMOriginScopedCopy::CheckPassed() const
{
	return fCheckPassed;
}

void KCMOriginScopedCopy::CheckSaid(PMString& out) const
{
	out = fCheckSaid;
	out.SetTranslatable(kFalse);
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
