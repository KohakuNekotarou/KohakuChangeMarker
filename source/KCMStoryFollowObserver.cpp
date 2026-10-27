//========================================================================================
//
//  KCMStoryFollowObserver.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IObserver.h"
#include "ISubject.h"

// General includes:
#include "CObserver.h"
#include "PersistUtils.h"		// ::GetDataBase - whose document the notification is about
#include "TextID.h"				// IID_ITEXTMODELCHANGED and the six IID_..REDLINE_DOCUMENT - the protocols followed

// Project includes:
#include "KCMID.h"				// IID_IKCMSTORYFOLLOWOBSERVER / kKCMStoryEditsRebuiltMessage
#include "KCMCore.h"			// KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB / KCMIsDocDBOpen / KCMGetCompareMode
#include "KCMComparisonRun.h"	// KCMStopForCancelledReread - a Track re-read cancelled at its bar stops the listing (2026-10-27)
#include "KCMDiag.h"			// KCM_DIAG_LOG - test builds only (nothing in a shipping .pln)
#include "KCMModelNotify.h"		// KCMNotify - the panel is told, never called
#include "KCMStoryDiffRun.h"	// RunOne - the row compared again
#include "KCMStoryList.h"		// NeedsCompareAgain
#include "KCMTrackList.h"		// AnyNeedsReadAgain - the Track mode follows the same rule (2026-10-05)
#include "KCMTrackRead.h"		// Build / RecordsMovedSinceRead - ...and reads the whole list again
#include "KCMThreadSafety.h"	// KCMIsMainThread
#include "KCMStoryFollowObserver.h"

namespace
{
// ★★THE PROTOCOLS FOLLOWED (2026-10-25) - what a story's change repeats on its DOCUMENT's subject, measured with KT's
//   docnotify probe (work/note-scripts/2026-10-25-kt-docnotify/ - the SDK writes it down nowhere; the three IIDs are in
//   TextID.h with no boss carrying them):
//   IID_ITEXTMODELCHANGED - a story's text or attributes changed: typing, a script's edit, a table cell's text, a
//     reject, a new frame - once per change, and again on its Undo and its Redo (lazily, which is all an Undo sends).
//   IID_ACCEPT..REDLINE_DOCUMENT / IID_REJECT..REDLINE_DOCUMENT - a tracked change accepted or rejected (one, a range,
//     all). ★AN ACCEPT CHANGES NO TEXT: IID_ITEXTMODELCHANGED never comes for it, and the story's change counter does
//     not move either (measured 7, 7, 7, 7 across a tracked insertion's accept, its Undo and its Redo).
bool16 IsRedline(const PMIID& protocol)
{
	return (protocol == IID_ACCEPTREDLINE_DOCUMENT || protocol == IID_ACCEPTRANGEREDLINE_DOCUMENT
			|| protocol == IID_ACCEPTALLREDLINE_DOCUMENT || protocol == IID_REJECTREDLINE_DOCUMENT
			|| protocol == IID_REJECTRANGEREDLINE_DOCUMENT || protocol == IID_REJECTALLREDLINE_DOCUMENT) ? kTrue : kFalse;
}

bool16 IsFollowed(const PMIID& protocol)
{
	return (protocol == IID_ITEXTMODELCHANGED || IsRedline(protocol)) ? kTrue : kFalse;
}
}	// namespace

//========================================================================================
// The observer.
//========================================================================================
class KCMStoryFollowObserver : public CObserver
{
public:
	KCMStoryFollowObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMSTORYFOLLOWOBSERVER) {}
	virtual ~KCMStoryFollowObserver() {}

	/** Deliberately empty: the work is in LazyUpdate, the only one of the two an undo and a redo reach. */
	virtual void Update(const ClassID& theChange, ISubject* theSubject,
						const PMIID& protocol, void* changedBy) {}

	virtual void LazyUpdate(ISubject* theSubject, const PMIID& protocol,
							const LazyNotificationData* data);
};

CREATE_PMINTERFACE(KCMStoryFollowObserver, kKCMStoryFollowObserverImpl)

/* The rows an Undo or a Redo moved, compared again.

   ★ASKED, NOT ASSUMED, at every step: the main thread, a comparison running, in a mode with story rows, both
   documents open, the Target's document - and then whether a story really came back to a state its row was compared
   at (KCMStoryList::NeedsCompareAgain). A KCM write in the story (a reject, a redo, a match, the import's own pour) has
   compared the row already, so the counter it left is the row's own and nothing is done twice.
   ★★EVERY ROW THAT NEEDS IT, AND ONE NOTIFICATION (re-check 2026-09-25): one Undo can move many stories at once - the
   whole import is one step. The rows that need it are compared and the panel is told once.
   ★The notification does not say WHICH story (the document's subject is the one LazyUpdate names - ISubject.h), nor
   whether it was an undo, and its data is not ours to read: the counters say.
*/
void KCMStoryFollowObserver::LazyUpdate(ISubject* theSubject, const PMIID& protocol,
										const LazyNotificationData* data)
{
	if (!IsFollowed(protocol) || theSubject == nil)
		return;
	// ⚠The list, the armed state and the diff are the main thread's (KCMThreadSafety.h) - a background export's clone
	//  carries no run-time attachment, and this is the belt in case one ever does.
	if (!KCMIsMainThread())
		return;
	if (!KCMIsArmed() || !KCMModeUsesStoryRows(KCMGetCompareMode()))
		return;
	IDataBase* const targetDB = KCMArmedTargetDB();
	IDataBase* const sourceDB = KCMArmedSourceDB();
	if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
		return;
	// (A Track listing has no Source - 2026-10-27, KCMModeNeedsSource; every other mode compares against one.)
	if (KCMModeNeedsSource(KCMGetCompareMode()) && (sourceDB == nil || !KCMIsDocDBOpen(sourceDB)))
		return;
	// The Target's document only: a document compared before keeps its attachment (KCMStoryFollowEnsureObservers - no
	// detach) and ends here.
	if (::GetDataBase(theSubject) != targetDB)
		return;
	KCM_DIAG_LOG("follow lazy: protocol 0x%X", (unsigned)protocol.Get());
	// ★★THE TRACK MODE FOLLOWS THE SAME RULE (2026-10-05, design 7-2): an Undo or a Redo of a listed story reads the
	//   whole list again - rows are (story, author) pairs, and one story's records can make or lose rows for several
	//   authors. Plain typing is left alone, as in the Story mode. ★And an accept or a reject (2026-10-25): it may move no
	//   counter, so the records themselves are counted - which also brings a story back that had lost all its rows.
	if (KCMGetCompareMode() == kKCMModeTrack)
	{
		const bool16 counters = KCMTrackList::AnyNeedsReadAgain(targetDB);
		const bool16 records = (!counters && IsRedline(protocol)) ? KCMTrackRead::RecordsMovedSinceRead(targetDB) : kFalse;
		KCM_DIAG_LOG("follow lazy: track counters=%d records=%d", (int)counters, (int)records);
		if (!counters && !records)
			return;
		// ★A CANCELLED RE-READ STOPS THE LISTING (2026-10-27, the author's answer "Cancel -> Stop"): the old list would
		//   stand, stale, after the Undo it was reading for (KCMComparisonRun.h, KCMStopForCancelledReread).
		bool16 cancelled = kFalse;
		KCMTrackRead::Build(targetDB, &cancelled);
		if (cancelled)
		{
			KCMStopForCancelledReread();
			return;
		}
		KCMNotify(kKCMStoryEditsRebuiltMessage);
		return;
	}
	// ★None is a result too: an import undone at once leaves its rows standing with nothing under them (the user's
	//   choice, 2026-09-25) - and a Redo brings the changes back under the same rows.
	int32 compared = 0;
	const int32 rows = KCMStoryList::GetRowCount();
	for (int32 i = 0; i < rows; ++i)
	{
		if (!KCMStoryList::NeedsCompareAgain(i, targetDB))
			continue;
		KCMStoryDiffRun::RunOne(targetDB, sourceDB, i);
		++compared;
	}
	if (compared > 0)
		KCMNotify(kKCMStoryEditsRebuiltMessage);
}

//========================================================================================
// Attaching.
//========================================================================================
namespace
{
/* The Target document's subject and our observer on it (KCM.fr: AddIn on kDocBoss), nil when either is missing. */
bool16 QueryFollow(IDataBase* targetDB, InterfacePtr<ISubject>& subject, InterfacePtr<IObserver>& observer)
{
	if (targetDB == nil)
		return kFalse;
	const UID root = targetDB->GetRootUID();
	if (root == kInvalidUID)
		return kFalse;
	subject.reset(InterfacePtr<ISubject>(targetDB, root, IID_ISUBJECT).forget());
	// Asked for by OUR IID: kDocBoss carries other people's IID_IOBSERVER (KCM.fr - KCMMarksObserver does the same).
	observer.reset(InterfacePtr<IObserver>(targetDB, root, IID_IKCMSTORYFOLLOWOBSERVER).forget());
	return (subject != nil && observer != nil) ? kTrue : kFalse;
}

/* The protocols, each once (IsFollowed above is the same list). */
const PMIID* Protocols(int32& count)
{
	static const PMIID kList[] = { IID_ITEXTMODELCHANGED,
		IID_ACCEPTREDLINE_DOCUMENT, IID_ACCEPTRANGEREDLINE_DOCUMENT, IID_ACCEPTALLREDLINE_DOCUMENT,
		IID_REJECTREDLINE_DOCUMENT, IID_REJECTRANGEREDLINE_DOCUMENT, IID_REJECTALLREDLINE_DOCUMENT };
	count = static_cast<int32>(sizeof(kList) / sizeof(kList[0]));
	return kList;
}
}	// namespace

void KCMStoryFollowEnsureObservers(IDataBase* targetDB)
{
	InterfacePtr<ISubject> subject;
	InterfacePtr<IObserver> observer;
	if (!QueryFollow(targetDB, subject, observer))
		return;
	int32 count = 0;
	const PMIID* const protocols = Protocols(count);
	int32 attachedNow = 0;
	for (int32 i = 0; i < count; ++i)
	{
		if (subject->IsAttached(ISubject::kLazyAttachment, observer, protocols[i], IID_IKCMSTORYFOLLOWOBSERVER))
			continue;
		subject->AttachObserver(ISubject::kLazyAttachment, observer, protocols[i], IID_IKCMSTORYFOLLOWOBSERVER);
		++attachedNow;
	}
	KCM_DIAG_LOG("follow attach: document subject %p - %d of %d protocol(s) attached now", (void*)subject.get(),
				 (int)attachedNow, (int)count);
}

#ifdef KCM_DIAG
void KCMStoryFollowDiagReport(IDataBase* targetDB, const char* when)
{
	InterfacePtr<ISubject> subject;
	InterfacePtr<IObserver> observer;
	if (!QueryFollow(targetDB, subject, observer))
	{
		KCM_DIAG_LOG("follow check (%s): no document subject or observer", when);
		return;
	}
	int32 count = 0;
	const PMIID* const protocols = Protocols(count);
	int32 attached = 0;
	for (int32 i = 0; i < count; ++i)
		if (subject->IsAttached(ISubject::kLazyAttachment, observer, protocols[i], IID_IKCMSTORYFOLLOWOBSERVER))
			++attached;
	KCM_DIAG_LOG("follow check (%s): document subject %p - %d of %d protocol(s) attached", when, (void*)subject.get(),
				 (int)attached, (int)count);
}
#endif

// End, KCMStoryFollowObserver.cpp.
