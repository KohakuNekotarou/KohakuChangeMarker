//========================================================================================
//
//  KCMStorySnapshot.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <map>
#include <set>
#include <string>

#include "KCMStorySnapshot.h"
#include "KCMMemXferBytes.h"
#include "KCMTableSnippet.h"	// KCMExportStoryInx - the export, with the document's style roots in it

namespace
{

/** One table, named the way the rest of KCM names one: the story it stands in, and the table's OWN
    ID - the last step of its Self, which is its uid in the document (KCMTableSnippet.h).
    ⚠**IT WAS THE TABLE'S ORDINAL UNTIL 2026-09-20**, and a key by position is a key that answers
     about the wrong table the moment another one is inserted before it - which is exactly what the
     user asked about ("is it looking at tables by position?"). */
struct TableKey
{
	UID		fStory;
	UID		fTable;

	TableKey(UID story, UID table) : fStory(story), fTable(table) {}

	bool operator<(const TableKey& other) const
	{
		if (fStory != other.fStory)
			return fStory < other.fStory;
		return fTable < other.fTable;
	}
};

typedef std::map<UID, std::string>								StoryMap;
typedef std::map<TableKey, std::map<std::string, std::string> >	CellIdMap;
typedef std::set<TableKey>										TableSet;
typedef std::map<TableKey, UID>									TableIdMap;

// ⚠Statics holding containers: each has a line in the model's shutdown (KCMPeek.cpp's
//   ShutdownCleanup), the rule KCMStoryList.h states for every static of ours. All either of them
//   does is empty a map of strings - which is exactly why leaving it to static destruction is not
//   the same thing.
StoryMap	sStories;
CellIdMap	sCellIds;
// ★Which tables an import has written during this comparison - see the header. It outlives sCellIds
//   on purpose: an Undo the Restore throws the translation away and this stays.
TableSet	sImported;
// ★★★"the table whose id is now <key.fTable> is Task Start's <value>" - what keeps a table KCM has
//   put back recognisable, since an import gives it an id Task Start never saw (the user, 2026-09-20).
TableIdMap	sTableIds;

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

UID KCMStorySnapshotTranslateTableId(UID story, UID liveTable)
{
	const TableIdMap::const_iterator it = sTableIds.find(TableKey(story, liveTable));
	return (it == sTableIds.end()) ? liveTable : it->second;
}

void KCMStorySnapshotPutTableId(UID story, UID liveTable, UID taskStartTable)
{
	if (liveTable == kInvalidUID || taskStartTable == kInvalidUID)
		return;
	if (liveTable == taskStartTable)
	{
		// Nothing to translate - and saying so is not the same as keeping an identity entry, which
		// would have to be dropped as carefully as a real one.
		sTableIds.erase(TableKey(story, liveTable));
		return;
	}
	sTableIds[TableKey(story, liveTable)] = taskStartTable;
}

void KCMStorySnapshotDropTableId(UID story, UID liveTable)
{
	sTableIds.erase(TableKey(story, liveTable));
}

const std::map<std::string, std::string>* KCMStorySnapshotGetCellIds(UID story, UID table)
{
	const CellIdMap::const_iterator it = sCellIds.find(TableKey(story, table));
	return (it == sCellIds.end()) ? nil : &it->second;
}

void KCMStorySnapshotPutCellIds(UID story, UID table, const std::map<std::string, std::string>& wasTaskStart)
{
	sCellIds[TableKey(story, table)] = wasTaskStart;
}

void KCMStorySnapshotDropStory(UID story)
{
	// ⚠**THE INX ONLY** - see the header. A restore ENDS by refreshing its own story, so dropping
	//   the cell ids here would have thrown away what the restore had just learned, every time.
	sStories.erase(story);
}

void KCMStorySnapshotDropCellIds(UID story, UID table)
{
	// ⚠**sImported IS NOT TOUCHED HERE** - the header says why: the translation goes, the fact that
	//   the ids mean nothing stays.
	sCellIds.erase(TableKey(story, table));
}

void KCMStorySnapshotMarkTableImported(UID story, UID table)
{
	sImported.insert(TableKey(story, table));
}

bool16 KCMStorySnapshotTableWasImported(UID story, UID table)
{
	return (sImported.find(TableKey(story, table)) != sImported.end()) ? kTrue : kFalse;
}

void KCMStorySnapshotDropAllStories()
{
	sStories.clear();
}

void KCMStorySnapshotClear()
{
	sStories.clear();
	sCellIds.clear();
	sImported.clear();
	sTableIds.clear();
}

// End, KCMStorySnapshot.cpp.
