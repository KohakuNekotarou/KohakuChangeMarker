//========================================================================================
//
//  KCMStorySnapshot.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <map>
#include <string>

#include "KCMStorySnapshot.h"
#include "KCMMemXferBytes.h"
#include "KCMTableSnippet.h"	// KCMExportStoryInx - the export, with the document's style roots in it

namespace
{

/** One table, named the way the rest of KCM names one: the story it stands in, and its ordinal among
    that story's tables in KCMTextRead's order. */
struct TableKey
{
	UID		fStory;
	int32	fOrdinal;

	TableKey(UID story, int32 ordinal) : fStory(story), fOrdinal(ordinal) {}

	bool operator<(const TableKey& other) const
	{
		if (fStory != other.fStory)
			return fStory < other.fStory;
		return fOrdinal < other.fOrdinal;
	}
};

typedef std::map<UID, std::string>								StoryMap;
typedef std::map<TableKey, std::map<std::string, std::string> >	CellIdMap;

// ⚠Statics holding containers: each has a line in the model's shutdown (KCMPeek.cpp's
//   ShutdownCleanup), the rule KCMStoryList.h states for every static of ours. All either of them
//   does is empty a map of strings - which is exactly why leaving it to static destruction is not
//   the same thing.
StoryMap	sStories;
CellIdMap	sCellIds;

}	// anonymous namespace

const std::string* KCMStorySnapshotTake(IDataBase* db, UID story)
{
	const StoryMap::const_iterator held = sStories.find(story);
	if (held != sStories.end())
		return &held->second;
	if (db == nil || story == kInvalidUID)
		return nil;

	// ★WITH THE STYLE ROOTS: one export then carries the story AND the document's cell and table
	//   style groups, which is what a table's snippet has to be dressed in. ⚠The root list is where
	//   paragraph styles, character styles and variables will join when their turn comes
	//   (KCMExportStoryInx) - the point of keeping the WHOLE story rather than a table's cut of it.
	KCMMemXferBytes bytes;
	if (!KCMExportStoryInx(db, story, bytes, kTrue) || bytes.GetData() == nil || bytes.GetSize() == 0)
		return nil;
	// Copied out of the transfer buffer once: what is kept is a plain string, so that the map can own
	// it and hand out a pointer that stays good until the story is dropped.
	sStories[story] = std::string(bytes.GetData(), bytes.GetSize());
	return &sStories[story];
}

const std::string* KCMStorySnapshotPeek(UID story)
{
	const StoryMap::const_iterator it = sStories.find(story);
	return (it == sStories.end()) ? nil : &it->second;
}

const std::map<std::string, std::string>* KCMStorySnapshotGetCellIds(UID story, int32 ordinal)
{
	const CellIdMap::const_iterator it = sCellIds.find(TableKey(story, ordinal));
	return (it == sCellIds.end()) ? nil : &it->second;
}

void KCMStorySnapshotPutCellIds(UID story, int32 ordinal, const std::map<std::string, std::string>& wasTaskStart)
{
	sCellIds[TableKey(story, ordinal)] = wasTaskStart;
}

void KCMStorySnapshotDropStory(UID story)
{
	// ⚠**THE INX ONLY** - see the header. A restore ENDS by refreshing its own story, so dropping
	//   the cell ids here would have thrown away what the restore had just learned, every time.
	sStories.erase(story);
}

void KCMStorySnapshotDropCellIds(UID story, int32 ordinal)
{
	sCellIds.erase(TableKey(story, ordinal));
}

void KCMStorySnapshotDropAllStories()
{
	sStories.clear();
}

void KCMStorySnapshotClear()
{
	sStories.clear();
	sCellIds.clear();
}

// End, KCMStorySnapshot.cpp.
