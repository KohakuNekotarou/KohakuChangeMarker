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
#include "ITextModel.h"

// General includes:
#include "CObserver.h"
#include "PersistUtils.h"		// ::GetUIDRef - which story the notification is about
#include "TextID.h"				// IID_ITEXTMODEL - the protocol InDesign raises a story's change under

// Project includes:
#include "KCMID.h"				// IID_IKCMSTORYFOLLOWOBSERVER / kKCMStoryEditsRebuiltMessage
#include "KCMCore.h"			// KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB / KCMIsDocDBOpen / KCMGetCompareMode
#include "KCMModelNotify.h"		// KCMNotify - the panel is told, never called
#include "KCMStoryDiffRun.h"	// RunOne - the row compared again
#include "KCMStoryList.h"		// RowOfTargetStory / NeedsCompareAgain
#include "KCMThreadSafety.h"	// KCMIsMainThread
#include "KCMStoryFollowObserver.h"

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
   documents open, the story the Target's, a row for it - and then whether the story really came back to a state the
   row was compared at (KCMStoryList::NeedsCompareAgain). A KCM write in the story (a reject, a redo, a match, the
   import's own pour) has compared the row already, so the counter it left is the row's own and nothing is done twice.
   ★★EVERY ROW THAT NEEDS IT, AND ONE NOTIFICATION (re-check 2026-09-25): one Undo can move many stories at once - the
   whole import is one step - and each story sends its own notification. Answered one row at a time, the panel would
   be rebuilt once per story. The first notification compares every row that needs it and tells the panel once; the
   ones after it find the counters where the rows now are and do nothing.
   The data is ignored: it names neither what changed nor whether it was an undo, and may be nil.
*/
void KCMStoryFollowObserver::LazyUpdate(ISubject* theSubject, const PMIID& protocol,
										const LazyNotificationData* data)
{
	if (protocol != IID_ITEXTMODEL || theSubject == nil)
		return;
	// ⚠The list, the armed state and the diff are the main thread's (KCMThreadSafety.h) - a background export's clone
	//  carries no run-time attachment, and this is the belt in case one ever does.
	if (!KCMIsMainThread())
		return;
	if (!KCMIsArmed() || !KCMModeUsesStoryRows(KCMGetCompareMode()))
		return;
	IDataBase* const targetDB = KCMArmedTargetDB();
	IDataBase* const sourceDB = KCMArmedSourceDB();
	if (targetDB == nil || sourceDB == nil || !KCMIsDocDBOpen(targetDB) || !KCMIsDocDBOpen(sourceDB))
		return;
	const UIDRef story = ::GetUIDRef(theSubject);
	if (story.GetDataBase() != targetDB)
		return;
	const int32 nth = KCMStoryList::RowOfTargetStory(story.GetUID());
	if (nth < 0 || !KCMStoryList::NeedsCompareAgain(nth, targetDB))
		return;
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
void KCMStoryFollowEnsureObservers(IDataBase* targetDB)
{
	if (targetDB == nil)
		return;
	const int32 rows = KCMStoryList::GetRowCount();
	for (int32 i = 0; i < rows; ++i)
	{
		const KCMStoryRow* const row = KCMStoryList::GetRow(i);
		if (row == nil || row->fStoryUID == kInvalidUID || (row->fKinds & kKCMStoryKindRemoved) != 0)
			continue;		// a story only the Source has is not in this document
		const UIDRef storyRef(targetDB, row->fStoryUID);
		InterfacePtr<ISubject> subject(storyRef, UseDefaultIID());
		// Asked for by OUR IID: kTextStoryBoss carries other people's IID_IOBSERVER, and the unit of collision
		// between vendors is the ImplementationID (KCM.fr).
		InterfacePtr<IObserver> observer(storyRef, IID_IKCMSTORYFOLLOWOBSERVER);
		if (subject == nil || observer == nil)
			continue;
		if (!subject->IsAttached(ISubject::kLazyAttachment, observer, IID_ITEXTMODEL, IID_IKCMSTORYFOLLOWOBSERVER))
			subject->AttachObserver(ISubject::kLazyAttachment, observer, IID_ITEXTMODEL, IID_IKCMSTORYFOLLOWOBSERVER);
	}
}

// End, KCMStoryFollowObserver.cpp.
