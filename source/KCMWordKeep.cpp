//========================================================================================
//
//  KCMWordKeep.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IApplication.h"
#include "IDataBase.h"
#include "IDocumentList.h"
#include "ISession.h"

#include <map>
#include <utility>

#include "KCMExternalSource.h"		// KCMIsDbAlive - "still open" as a pointer comparison against the document list
#include "KCMWordKeep.h"

namespace
{

typedef std::pair<IDataBase*, uint32> Key;		// the document, and the story's uid as a number
typedef std::map<Key, KCMStoryShape::Story> Kept;

Kept& Store()
{
	static Kept sKept;
	return sKept;
}

}	// anonymous namespace

void KCMWordKeepPut(IDataBase* db, UID story, const KCMStoryShape::Story& word)
{
	if (db == nil || story == kInvalidUID)
		return;
	Store()[Key(db, story.Get())] = word;
}

bool16 KCMWordKeepGet(IDataBase* db, UID story, KCMStoryShape::Story& out)
{
	if (db == nil || story == kInvalidUID)
		return kFalse;
	Kept::const_iterator it = Store().find(Key(db, story.Get()));
	if (it == Store().end())
		return kFalse;
	out = it->second;
	return kTrue;
}

void KCMWordKeepSweepClosed()
{
	Kept& kept = Store();
	if (kept.empty())
		return;
	// The sweep can arrive during the shutdown sequence too, hence the nil guards (the same as KCMDocUidSet's).
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;
	Kept::iterator it = kept.begin();
	while (it != kept.end())
	{
		if (!KCMIsDbAlive(docList, it->first.first))
			kept.erase(it++);		// a closed document: the entry goes, its address is never dereferenced
		else
			++it;
	}
}

void KCMWordKeepClear()
{
	Kept().swap(Store());		// a fresh map releases the storage too
}

// End, KCMWordKeep.cpp.
