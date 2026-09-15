//========================================================================================
//
//  KCMStoryUndoObserver.cpp
//
//  The panel follows an undo. The header says why this is one line of work.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IObserver.h"
#include "ISubject.h"
#include "ITextModel.h"			// the protocol, and the boss the observer sits on

// General includes:
#include "CObserver.h"
#include "TextID.h"				// IID_ITEXTMODEL - the protocol InDesign raises the change under

// Project includes:
#include "KCMID.h"				// IID_IKCMSTORYUNDOOBSERVER / kKCMStoryEditsRebuiltMessage
#include "KCMStoryUndoObserver.h"
#include "KCMStoryList.h"		// which stories have rows
#include "KCMModelNotify.h"		// KCMNotify - the whole of the work

//========================================================================================
// The observer.
//========================================================================================
class KCMStoryUndoObserver : public CObserver
{
public:
	KCMStoryUndoObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMSTORYUNDOOBSERVER) {}
	virtual ~KCMStoryUndoObserver() {}

	/** Deliberately empty. The work is in LazyUpdate, because that is the only one of the two
		called on undo and redo (ISubject.h:78-82) - and undo is what this observer is for. Doing
		it here as well would give the write a road the undo does not have, and the two would drift
		the first time one was changed. */
	virtual void Update(const ClassID& theChange, ISubject* theSubject,
	                    const PMIID& protocol, void* changedBy) {}

	virtual void LazyUpdate(ISubject* theSubject, const PMIID& protocol,
	                        const LazyNotificationData* data);
};

CREATE_PMINTERFACE(KCMStoryUndoObserver, kKCMStoryUndoObserverImpl)

/* One notification, one redraw.

   ★**IT WRITES NOTHING, READS NOTHING AND DECIDES NOTHING.** Whether a change is drawn as taken in
   is derived from the story's text change counter, which the undo has already put back by the time
   this runs. The panel rebuilds its rows from the list, and the list is right. All that was
   missing was somebody saying so.

   The protocol is checked even though the attachment names one: an observer that acts on whatever
   it is handed starts doing work for somebody else's change the day a second attachment is added.
   The data is ignored - LazyUpdate is told neither what changed nor by whom, and its data may be
   nil (IObserver.h:101-106).
*/
void KCMStoryUndoObserver::LazyUpdate(ISubject* theSubject, const PMIID& protocol,
                                      const LazyNotificationData* data)
{
	if (protocol != IID_ITEXTMODEL)
		return;

	KCMNotify(kKCMStoryEditsRebuiltMessage);
}

//========================================================================================
// Attaching.
//
//  There is no detach. The header says why, and it is a decision rather than an omission.
//========================================================================================
void KCMStoryUndoEnsureObservers(IDataBase* targetDB)
{
	if (targetDB == nil)
		return;

	const int32 rows = KCMStoryList::GetRowCount();
	for (int32 i = 0; i < rows; ++i)
	{
		const KCMStoryRow* const row = KCMStoryList::GetRow(i);
		if (row == nil || row->fStoryUID == kInvalidUID)
			continue;

		// ⚠A REMOVED story is not in the Target at all - it exists only in the Source - so the
		//   interfaces below come back nil and the row is passed over. That is the right answer:
		//   there is nothing in this document for an undo to put back.
		const UIDRef storyRef(targetDB, row->fStoryUID);
		InterfacePtr<ISubject> subject(storyRef, UseDefaultIID());
		if (subject == nil)
			continue;

		// Asked for by OUR IID, which is the whole point of the AddIn: kTextStoryBoss already
		// carries other people's IID_IOBSERVER, and the unit of collision is the ImplementationID.
		InterfacePtr<IObserver> observer(storyRef, IID_IKCMSTORYUNDOOBSERVER);
		if (observer == nil)
			continue;

		if (!subject->IsAttached(ISubject::kLazyAttachment, observer,
		                         IID_ITEXTMODEL, IID_IKCMSTORYUNDOOBSERVER))
		{
			subject->AttachObserver(ISubject::kLazyAttachment, observer,
			                        IID_ITEXTMODEL, IID_IKCMSTORYUNDOOBSERVER);
		}
	}
}

// End, KCMStoryUndoObserver.cpp.
