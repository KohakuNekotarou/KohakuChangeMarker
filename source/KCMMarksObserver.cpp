//========================================================================================
//
//  KCMMarksObserver.cpp
//
//  Refills the session store from the document's script labels. The reasoning is in the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IObserver.h"
#include "ISubject.h"

// General includes:
#include "CObserver.h"
#include "PersistUtils.h"			// GetDataBase(IPMUnknown*)

// Project includes:
#include "KCMID.h"
#include "KCMMarksObserver.h"
#include "KCMPageMarksDoc.h"		// KCMMarksSyncFromDocument -- the rebuild itself

//========================================================================================
// The observer.
//========================================================================================
class KCMMarksObserver : public CObserver
{
public:
	KCMMarksObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMMARKSOBSERVER) {}
	virtual ~KCMMarksObserver() {}

	/** Deliberately empty. The work is in LazyUpdate, because that is the only one of the two that
		is called on undo and redo (ISubject.h:78-82). Doing it here as well would give Do a road
		Undo does not have. */
	virtual void Update(const ClassID& theChange, ISubject* theSubject,
	                    const PMIID& protocol, void* changedBy) {}

	virtual void LazyUpdate(ISubject* theSubject, const PMIID& protocol,
	                        const LazyNotificationData* data);
};

CREATE_PMINTERFACE(KCMMarksObserver, kKCMMarksObserverImpl)

/* One notification, one rebuild.

   The protocol is checked even though this observer is attached under one protocol only: an
   observer that acts on whatever it is handed is an observer that starts doing work for somebody
   else's change the day a second attachment is added.

   The data is ignored, and there is nothing to salvage from it -- see the header.
*/
void KCMMarksObserver::LazyUpdate(ISubject* theSubject, const PMIID& protocol,
                                  const LazyNotificationData* data)
{
	if (protocol != IID_IKCMPAGEMARKS)
		return;

	IDataBase* const db = ::GetDataBase(theSubject);
	if (db == nil)
		return;

	KCMMarksSyncFromDocument(db, nil, nil);
}

//========================================================================================
// Attaching.
//
//  There is no detach. The header says why, and it is a decision rather than an omission.
//========================================================================================
void KCMMarksEnsureObserver(IDataBase* db)
{
	if (db == nil)
		return;

	const UID root = db->GetRootUID();
	if (root == kInvalidUID)
		return;

	InterfacePtr<ISubject> subject(db, root, IID_ISUBJECT);
	if (subject == nil)
		return;

	// Asked for by OUR IID, which is the whole point of the AddIn: kDocBoss already has an
	// IID_IOBSERVER belonging to somebody else.
	InterfacePtr<IObserver> observer(db, root, IID_IKCMMARKSOBSERVER);
	if (observer == nil)
		return;

	if (!subject->IsAttached(ISubject::kLazyAttachment, observer,
	                         IID_IKCMPAGEMARKS, IID_IKCMMARKSOBSERVER))
	{
		subject->AttachObserver(ISubject::kLazyAttachment, observer,
		                        IID_IKCMPAGEMARKS, IID_IKCMMARKSOBSERVER);
	}
}

// End, KCMMarksObserver.cpp.
