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
#include <set>		// which story UIDs the target holds - see the Removed sweep in Compare

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

	// ---- Removed: a story the SOURCE holds and the target does not (2026-08-21) ----
	//
	// ★THESE ROWS CARRY A SOURCE UID. Every row above names a story in the target; these name one
	//   that is no longer there, so the uid can only be the older document's. Callers tell the two
	//   apart by kKESCMStoryKindRemoved (KESCMStoryStamp.h's fStoryUID).
	//
	// ★A SECOND SET RATHER THAN MARKING sourceByUID AS IT IS MATCHED. Ticking off source entries
	//   during the walk above would give the same answer only because that walk happens to finish
	//   first - an order dependency that would break silently if anything were ever moved between
	//   the two loops. A plain membership test of the target reads correctly whatever the order.
	//
	// ★NO KIND CAN BE NAMED, exactly as for Added: there are no two counters to compare, so
	//   "removed" is the whole answer. (Reading the source's own sub-counters would say what has
	//   been edited during the older version's OWN history, which is not what this list reports.)
	std::set<UID> targetUIDs;
	for (std::vector<KESCMStoryStamp>::const_iterator it = target.begin(); it != target.end(); ++it)
		targetUIDs.insert(it->fStoryUID);

	for (std::vector<KESCMStoryStamp>::const_iterator it = source.begin(); it != source.end(); ++it)
	{
		if (targetUIDs.find(it->fStoryUID) != targetUIDs.end())
			continue;	// still there - it was reported above, or it read the same

		KESCMStoryDiff row;
		row.fStoryUID = it->fStoryUID;	// ★the SOURCE document's uid
		row.fKinds = kKESCMStoryKindRemoved;
		out.push_back(row);
	}
}


// End, KESCMStoryStamp.cpp.
