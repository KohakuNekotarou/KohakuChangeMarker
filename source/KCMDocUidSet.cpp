//========================================================================================
//
//  KCMDocUidSet.cpp
//
//  The shared implementation of "document database -> set of page UIDs" (see KCMDocUidSet.h).
//  Registered (Added/Removed) and checked (the tick) had the same container and the same routine
//  operations, so only those live here; the two sets themselves stay with their callers.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"
#include "IApplication.h"
#include "IDataBase.h"
#include "IDocumentList.h"		// the liveness sweep (pointer comparison against FindDocByDataBase)

#include "KCMDocUidSet.h"
#include "KCMThreadSafety.h"	// KCMIsSameDoc (a background thread's clone) / the shared-state lock
#include "KCMExternalSource.h"	// KCMIsDbAlive (the lent Source's page state must survive the close sweep)

//========================================================================================
// This container is **written by the main thread and read by the background thread's drawing
//   pass** (the asynchronous PDF export), so **every method in this file**, reader and writer
//   alike, takes the shared lock. The lock is recursive, so being called from a drawing loop
//   that already holds it is safe. The reasoning in full is in KCMThreadSafety.h.
//   @warning **two methods are exceptions, and both of them are in the header**: IsEmpty() is
//     inline and takes no lock, and GetMap() hands out the raw map so its caller locks instead.
//     Do not read "every method guards itself" here and then add a background-thread caller of
//     those two.
//========================================================================================

// The entry for db (falling back on file identity when the pointer misses). The declaration says why.
KCMDocUidSet::Map::const_iterator KCMDocUidSet::FindDoc(IDataBase* db) const
{
	Map::const_iterator it = fMap.find(db);
	if (it != fMap.end())
		return it;						// the main thread's ordinary route settles here, as it always did
	for (it = fMap.begin(); it != fMap.end(); ++it)
	{
		if (KCMIsSameDoc(it->first, db))
			return it;					// a background thread's clone is caught here
	}
	return fMap.end();
}

//========================================================================================
// Readers
//========================================================================================
bool16 KCMDocUidSet::Contains(IDataBase* db, UID uid) const
{
	if (db == nil)
		return kFalse;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	return (it != fMap.end() && it->second.count(uid) > 0) ? kTrue : kFalse;
}

bool16 KCMDocUidSet::HasAny(IDataBase* db) const
{
	if (db == nil)
		return kFalse;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	return (it != fMap.end() && !it->second.empty()) ? kTrue : kFalse;
}

int32 KCMDocUidSet::CountIn(IDataBase* db) const
{
	if (db == nil)
		return 0;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	return (it != fMap.end()) ? (int32)it->second.size() : 0;
}

int32 KCMDocUidSet::CountIn(IDataBase* db, const std::vector<UID>& uids) const
{
	if (db == nil)
		return 0;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	if (it == fMap.end())
		return 0;
	int32 n = 0;
	for (size_t i = 0; i < uids.size(); ++i)
	{
		if (it->second.count(uids[i]) > 0)
			++n;
	}
	return n;
}

bool16 KCMDocUidSet::AnyNotIn(IDataBase* db, const std::vector<UID>& uids) const
{
	if (uids.empty())
		return kFalse;					// nothing was asked about, so nothing is missing
	if (db == nil)
		return kTrue;					// no set to look in = none of them are in it (what Contains says)
	KCMMarkStateLock lock(KCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	if (it == fMap.end())
		return kTrue;					// likewise (this document has no entry at all)
	for (size_t i = 0; i < uids.size(); ++i)
	{
		if (it->second.count(uids[i]) == 0)
			return kTrue;
	}
	return kFalse;
}

void KCMDocUidSet::CollectInto(IDataBase* db, std::set<UID>& out) const
{
	if (db == nil)
		return;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	if (it == fMap.end())
		return;
	out.insert(it->second.begin(), it->second.end());
}

//========================================================================================
// Writers (an entry that became empty is dropped at once -- the rule in KCMDocUidSet.h)
//========================================================================================
void KCMDocUidSet::Insert(IDataBase* db, UID uid)
{
	if (db == nil)
		return;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	fMap[db].insert(uid);
}

void KCMDocUidSet::Erase(IDataBase* db, UID uid)
{
	if (db == nil)
		return;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	Map::iterator it = fMap.find(db);
	if (it == fMap.end())
		return;
	it->second.erase(uid);
	if (it->second.empty())
		fMap.erase(it);
}

bool16 KCMDocUidSet::ToggleAll(IDataBase* db, const std::vector<UID>& uids)
{
	if (db == nil || uids.empty())
		return kFalse;

	// The lock is held across the decision and the change alike. **KCMMarkStateLock is recursive**
	// (the discipline is in KCMThreadSafety.h), so the three calls below take it again harmlessly
	// -- and going through them is the point: the rule that an entry which became empty is dropped
	// at once lives in Erase, and writing the loop here again would be a second copy of it.
	KCMMarkStateLock lock(KCMMarkStateMutex());
	const bool16 anyNotIn = AnyNotIn(db, uids);
	for (size_t i = 0; i < uids.size(); ++i)
	{
		if (anyNotIn)
			Insert(db, uids[i]);
		else
			Erase(db, uids[i]);
	}
	return anyNotIn;
}

// (ClearDoc was removed: its only caller KCMPageMapClearAll had no caller of its own. When one
//  document's set has to be dropped, Replace(db, empty) does exactly that -- entry and all.)

void KCMDocUidSet::ClearAllDocs()
{
	KCMMarkStateLock lock(KCMMarkStateMutex());
	fMap.clear();
}

void KCMDocUidSet::Replace(IDataBase* db, const std::vector<UID>& uids)
{
	if (db == nil)
		return;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	if (uids.empty())
	{
		fMap.erase(db);
		return;
	}
	std::set<UID>& s = fMap[db];
	s.clear();
	for (size_t i = 0; i < uids.size(); ++i)
		s.insert(uids[i]);
}

void KCMDocUidSet::Replace(IDataBase* db, const std::set<UID>& uids)
{
	if (db == nil)
		return;
	KCMMarkStateLock lock(KCMMarkStateMutex());
	if (uids.empty())
	{
		fMap.erase(db);
		return;
	}
	fMap[db] = uids;
}

void KCMDocUidSet::PruneEmptyDocs()
{
	KCMMarkStateLock lock(KCMMarkStateMutex());
	Map::iterator it = fMap.begin();
	while (it != fMap.end())
	{
		if (it->second.empty())
			fMap.erase(it++);
		else
			++it;
	}
}

//========================================================================================
// The liveness sweep
//   Run right after documents close (from KCMHandleDocsClosed). Drops the entries of closed
//   documents, state only. **A closed database is never dereferenced** -- pointer comparison
//   against FindDocByDataBase and nothing else, the same way as KCM's other close-up work.
//   Dropping them promptly also leaves the least room to confuse a closed document with a new
//   one that reused its address ([[uidref-reuse-after-close]]).
//========================================================================================
void KCMDocUidSet::SweepClosedDocs()
{
	// @warning this reasons "not in the document list, therefore closed", which **does not hold
	//   on a background thread** (it looks at a different database). The caller,
	//   KCMHandleDocsClosed, tests IDThreading::IsMainThreadDomain() at its single entry, so a
	//   background thread never reaches here. The test is not repeated ([[one-question-one-place]]).
	KCMMarkStateLock lock(KCMMarkStateMutex());
	if (fMap.empty())
		return;

	// The close-up can arrive during the shutdown sequence too, hence the nil guards on session.
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	Map::iterator it = fMap.begin();
	while (it != fMap.end())
	{
		if (!KCMIsDbAlive(docList, it->first))
			fMap.erase(it++);	// a closed document: drop the state, dereference nothing
		else
			++it;
	}
}

// End of KCMDocUidSet.cpp
