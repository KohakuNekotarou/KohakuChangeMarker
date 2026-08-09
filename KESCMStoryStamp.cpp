//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryStamp.h for what is read and why it is the all-changes counter.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IStoryList.h"
#include "ITextModel.h"

// Project includes:
#include "KESCMStoryStamp.h"

#include <map>

/* CollectStamps
*/
void KESCMStoryEdits::CollectStamps(IDataBase* db, std::vector<KESCMStoryStamp>& out)
{
	out.clear();
	if (db == nil)
		return;

	InterfacePtr<IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
	if (storyList == nil)
		return;

	const int32 storyCount = storyList->GetUserAccessibleStoryCount();
	for (int32 i = 0; i < storyCount; ++i)
	{
		const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(i);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;	// a story that cannot be read contributes nothing to either side

		KESCMStoryStamp stamp;
		stamp.fStoryUID = storyRef.GetUID();
		stamp.fChangeCount = model->GetChangeCount();
		out.push_back(stamp);
	}
}

/* Compare
*/
void KESCMStoryEdits::Compare(const std::vector<KESCMStoryStamp>& source,
                              const std::vector<KESCMStoryStamp>& target,
                              std::vector<KESCMStoryDiff>& out)
{
	out.clear();

	// Matched by UID rather than by walking both lists in step. The two versions can hold different
	// numbers of stories, and a story added to the newer version is inserted part-way through the
	// enumeration rather than appended (measured 2026-08-08), so position says nothing.
	std::map<UID, uint32> sourceByUID;
	for (std::vector<KESCMStoryStamp>::const_iterator it = source.begin(); it != source.end(); ++it)
		sourceByUID[it->fStoryUID] = it->fChangeCount;

	for (std::vector<KESCMStoryStamp>::const_iterator it = target.begin(); it != target.end(); ++it)
	{
		const std::map<UID, uint32>::const_iterator found = sourceByUID.find(it->fStoryUID);

		if (found == sourceByUID.end())
		{
			KESCMStoryDiff row;
			row.fStoryUID = it->fStoryUID;
			row.fSourceCount = 0;
			row.fTargetCount = it->fChangeCount;
			row.fAdded = kTrue;
			out.push_back(row);
		}
		else if (found->second != it->fChangeCount)
		{
			KESCMStoryDiff row;
			row.fStoryUID = it->fStoryUID;
			row.fSourceCount = found->second;
			row.fTargetCount = it->fChangeCount;
			row.fAdded = kFalse;
			out.push_back(row);
		}
		// equal counters produce no row - the text is unchanged (the page may still look different,
		// which is what the pixel comparison is for)
	}
}

// End, KESCMStoryStamp.cpp.
