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

/* ReadStamp
*/
bool16 KESCMStoryEdits::ReadStamp(const UIDRef& storyRef, KESCMStoryStamp& out)
{
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;	// not a story - a script may hand us any object it likes

	out.fStoryUID    = storyRef.GetUID();
	out.fChangeCount = model->GetChangeCount();
	out.fTextCount   = model->GetTextChangeCount();
	out.fAttrCount   = model->GetAttrChangeCount();
	out.fOtherCount  = model->GetOtherChangeCount();
	return kTrue;
}

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
		// One reading, one place. The script properties go through the same call, so the panel
		// and app.documents[0].stories[n].kcmChangeCount can never disagree about a story.
		KESCMStoryStamp stamp;
		if (ReadStamp(storyList->GetNthUserAccessibleStoryUID(i), stamp))
			out.push_back(stamp);	// a story that cannot be read contributes nothing to either side
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
	std::map<UID, KESCMStoryStamp> sourceByUID;
	for (std::vector<KESCMStoryStamp>::const_iterator it = source.begin(); it != source.end(); ++it)
		sourceByUID[it->fStoryUID] = *it;

	for (std::vector<KESCMStoryStamp>::const_iterator it = target.begin(); it != target.end(); ++it)
	{
		const std::map<UID, KESCMStoryStamp>::const_iterator found = sourceByUID.find(it->fStoryUID);

		if (found == sourceByUID.end())
		{
			// Nothing to compare against, so no kind can be named - "added" is the whole answer.
			KESCMStoryDiff row;
			row.fStoryUID = it->fStoryUID;
			row.fKinds = kKESCMStoryKindAdded;
			out.push_back(row);
			continue;
		}

		// Whether the story is reported is still the aggregate counter's call. See the header: the
		// sub-counters name what moved, they are not the test.
		if (found->second.fChangeCount == it->fChangeCount)
			continue;	// text AND attributes AND everything else read the same

		uint32 kinds = kKESCMStoryKindNone;
		if (found->second.fTextCount  != it->fTextCount)  kinds |= kKESCMStoryKindText;
		if (found->second.fAttrCount  != it->fAttrCount)  kinds |= kKESCMStoryKindAttr;
		if (found->second.fOtherCount != it->fOtherCount) kinds |= kKESCMStoryKindOther;

		if (kinds == kKESCMStoryKindNone)
			kinds = kKESCMStoryKindOther;	// aggregate moved, no sub-counter did - say "something"

		KESCMStoryDiff row;
		row.fStoryUID = it->fStoryUID;
		row.fKinds = kinds;
		out.push_back(row);
	}
}


// End, KESCMStoryStamp.cpp.
