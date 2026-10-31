//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryStamp.h for what is read and why it is the all-changes counter.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IComposeScanner.h"	// CopyText - the words, for the words' test
#include "IDataBase.h"
#include "IStoryList.h"
#include "ITextModel.h"

// General includes:
#include "WideString.h"

// Project includes:
#include "KCMDiag.h"		// KCM_DIAG_LOG - what the reading took (test builds only)
#include "KCMObjectPrint.h"	// KCMObjectsPrintOfStory - the objects placed in a story, for the second test (2026-10-27)
#include "KCMStoryStamp.h"
#include "KCMTextRead.h"	// MarksPrint - what stands over the words, for the second test

#ifdef KCM_DIAG
#include <chrono>
#endif
#include <map>
#include <set>		// which story UIDs the target holds - see the Removed sweep in Compare

/* ReadStamp
*/
bool16 KCMStoryEdits::ReadStamp(const UIDRef& storyRef, KCMStoryStamp& out)
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

/* TextPrintOf
	The story's whole text as one number (KCMStoryStamp::fTextPrint): FNV-1a over the code points of every thread of its model, in
	blocks. Walked by iterator, not by WideString::GetChar: a block is long, and a position has to be counted out where a
	surrogate stands. 0 = it could not be read, on either side alike.
*/
static uint64 TextPrintOf(ITextModel* model)
{
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return 0;
	const int32 kBlock = 65536;
	const int32 total = model->TotalLength();
	uint64 print = 14695981039346656037ULL;
	for (int32 at = 0; at < total; at += kBlock)
	{
		WideString block;
		scanner->CopyText(at, (total - at < kBlock) ? (total - at) : kBlock, &block);
		for (WideString::const_iterator it = block.begin(); it != block.end(); ++it)
		{
			print ^= static_cast<uint64>(*it);
			print *= 1099511628211ULL;
		}
	}
	return print;
}

/* CollectStamps
*/
void KCMStoryEdits::CollectStamps(IDataBase* db, std::vector<KCMStoryStamp>& out)
{
	out.clear();
	if (db == nil)
		return;

	InterfacePtr<IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
	if (storyList == nil)
		return;

	// (The counters compose nothing and read no words; the words' test does read - and reading must not dirty a document.)
	IDataBase::SaveRestoreModifiedState guard(db);
#ifdef KCM_DIAG
	const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
#endif

	const int32 storyCount = storyList->GetUserAccessibleStoryCount();
	for (int32 i = 0; i < storyCount; ++i)
	{
		// One reading, one place. The script properties go through the same call, so the panel
		// and app.documents[0].stories[n].kcmChangeCount can never disagree about a story.
		KCMStoryStamp stamp;
		const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(i);
		if (ReadStamp(storyRef, stamp))
		{
			// its objects, its words and its marks, for the second test in Compare (KCMStoryStamp's three prints)
			InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
			stamp.fObjectsPrint = KCMObjectsPrintOfStory(model);
			stamp.fTextPrint = TextPrintOf(model);
			stamp.fMarksPrint = KCMTextRead::MarksPrint(model);
			out.push_back(stamp);	// a story that cannot be read contributes nothing to either side
		}
	}
#ifdef KCM_DIAG
	KCM_DIAG_LOG("stamps %d stories, %d ms", (int)storyCount,
				 (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count());
#endif
}

// One row of the answer. The three places below build it identically and differ only in which uid
// and which kinds go in, so a field added to KCMStoryDiff is added here once rather than in three
// places that have to be found first.
static void AppendDiff(std::vector<KCMStoryDiff>& out, UID storyUID, uint32 kinds, UID sourceStoryUID)
{
	KCMStoryDiff row;
	row.fStoryUID = storyUID;
	row.fKinds    = kinds;
	row.fSourceStoryUID = sourceStoryUID;	// the Source story it compares against (KCMStoryStamp.h)
	out.push_back(row);
}

/* Compare
*/
void KCMStoryEdits::Compare(const std::vector<KCMStoryStamp>& source,
                              const std::vector<KCMStoryStamp>& target,
                              std::vector<KCMStoryDiff>& out)
{
	out.clear();

	// Matched by UID rather than by walking both lists in step. The two versions can hold different
	// numbers of stories, and a story added to the newer version is inserted part-way through the
	// enumeration rather than appended (measured 2026-08-08), so position says nothing.
	std::map<UID, KCMStoryStamp> sourceByUID;
	for (std::vector<KCMStoryStamp>::const_iterator it = source.begin(); it != source.end(); ++it)
		sourceByUID[it->fStoryUID] = *it;

	for (std::vector<KCMStoryStamp>::const_iterator it = target.begin(); it != target.end(); ++it)
	{
		const std::map<UID, KCMStoryStamp>::const_iterator found = sourceByUID.find(it->fStoryUID);

		if (found == sourceByUID.end())
		{
			// Nothing to compare against, so no kind can be named - "added" is the whole answer.
			AppendDiff(out, it->fStoryUID, kKCMStoryKindAdded, kInvalidUID);	// no Source story (the place pairing may find one)
			continue;
		}

		// Whether the story is reported is still the aggregate counter's call. See the header: the
		// sub-counters name what moved, they are not the test.
		// (Until 2026-09-19 the fourth, Import, mode declined this sieve: its words went into the
		//  COPY, so no counter of the document moved. An import puts its words into the document
		//  now, and the counters answer for it like for any other edit.)
		if (found->second.fChangeCount == it->fChangeCount)
		{
			// ONE COUNT - BUT THE WORDS MAY DIFFER: the count is not a unique version number (KCMStoryStamp::fTextPrint). Listed as
			//   a text change; the comparison finds where, and a story whose difference it does not read (only the deleted words
			//   Track Changes keeps) drops out as any row with nothing found does.
			// AND AN OBJECT PLACED IN THEM MAY NOT READ THE SAME: an edit to the object itself moves no counter of the story
			//   (KCMStoryStamp::fObjectsPrint). Listed as an attribute change; its rows are the Object rows the comparison
			//   finds (KCMStoryDiffRun's CompareParagraphObjects).
			uint32 kinds = kKCMStoryKindNone;
			if (found->second.fTextPrint != it->fTextPrint)
				kinds |= kKCMStoryKindText;
			if (found->second.fMarksPrint != it->fMarksPrint || found->second.fObjectsPrint != it->fObjectsPrint)
				kinds |= kKCMStoryKindAttr;
			if (kinds != kKCMStoryKindNone)
				AppendDiff(out, it->fStoryUID, kinds, it->fStoryUID);
			continue;
		}

		uint32 kinds = kKCMStoryKindNone;
		if (found->second.fTextCount  != it->fTextCount)  kinds |= kKCMStoryKindText;
		if (found->second.fAttrCount  != it->fAttrCount)  kinds |= kKCMStoryKindAttr;
		if (found->second.fOtherCount != it->fOtherCount) kinds |= kKCMStoryKindOther;

		if (kinds == kKCMStoryKindNone)
			kinds = kKCMStoryKindOther;	// aggregate moved, no sub-counter did - say "something"

		AppendDiff(out, it->fStoryUID, kinds, it->fStoryUID);	// the same story on both sides
	}

	// ---- Removed: a story the SOURCE holds and the target does not ----
	//
	// **THESE ROWS CARRY A SOURCE UID.** Every row above names a story in the target; these name
	//   one that is no longer there, so the uid can only be the older document's. Callers tell the
	//   two apart by kKCMStoryKindRemoved (KCMStoryStamp.h's fStoryUID).
	//
	// **A SECOND SET RATHER THAN MARKING sourceByUID AS IT IS MATCHED.** Ticking off source entries
	//   during the walk above would give the same answer only because that walk happens to finish
	//   first -- an order dependency that would break silently if anything were ever moved between
	//   the two loops. A plain membership test of the target reads correctly whatever the order.
	//
	// **NO KIND CAN BE NAMED**, exactly as for Added: there are no two counters to compare, so
	//   "removed" is the whole answer. (Reading the source's own sub-counters would say what has
	//   been edited during the older version's OWN history, which is not what this list reports.)
	std::set<UID> targetUIDs;
	for (std::vector<KCMStoryStamp>::const_iterator it = target.begin(); it != target.end(); ++it)
		targetUIDs.insert(it->fStoryUID);

	for (std::vector<KCMStoryStamp>::const_iterator it = source.begin(); it != source.end(); ++it)
	{
		if (targetUIDs.find(it->fStoryUID) != targetUIDs.end())
			continue;	// still there - it was reported above, or it read the same

		AppendDiff(out, it->fStoryUID, kKCMStoryKindRemoved, it->fStoryUID);	// the SOURCE document's uid, on both fields
	}
}


// End, KCMStoryStamp.cpp.
