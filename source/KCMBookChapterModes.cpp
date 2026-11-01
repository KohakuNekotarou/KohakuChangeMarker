//========================================================================================
//
//  KCMBookChapterModes.cpp
//
//  Book comparison: the story and the resources judgements. See KCMBookChapterModes.h for why they
//  are not in KCMBookCompare.cpp, and for what they must not touch.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include <vector>

// Project includes:
#include "KCMBookChapterModes.h"
#include "KCMResourceDiff.h"		// KCMDiffResources / KCMResourceChangeList / KCMResourceDiffStats
#include "KCMResourceParse.h"		// KCMResourceList
#include "KCMResourceSnapshot.h"	// KCMReadResourceList
#include "KCMSentence.h"			// KCMSentence - a fixed sentence, never a key
#include "KCMStoryStamp.h"			// KCMStoryEdits::CollectStamps / ::Compare

KCMBookModeVerdict KCMJudgeChapterStory(IDataBase* targetDB, IDataBase* sourceDB)
{
	// Reading the counters composes nothing and dirties nothing (KCMStoryStamp.h, "READING COUNTERS
	// COMPOSES NOTHING"), so no SaveRestoreModifiedState guard is wanted here. The caller holds one
	// around the whole chapter anyway, for the pixel work, which does compose.
	std::vector<KCMStoryStamp> targetStamps;
	std::vector<KCMStoryStamp> sourceStamps;
	KCMStoryEdits::CollectStamps(targetDB, targetStamps);
	KCMStoryEdits::CollectStamps(sourceDB, sourceStamps);

	// ⚠SOURCE FIRST, TARGET SECOND -- the order Compare declares (older, then newer). Swapped, an
	//  added story would be reported as a removed one, and the COUNT would come out the same, so
	//  the one bit this function returns could never catch it. Read the declaration, not the call.
	std::vector<KCMStoryDiff> diffs;
	KCMStoryEdits::Compare(sourceStamps, targetStamps, diffs);

	// One differing story is the whole answer, and the list is dropped here. Which story it was is
	// the panel's business, reached from the chapter row's right click.
	return diffs.empty() ? kKCMBookVerdictUnchanged : kKCMBookVerdictChanged;
}

KCMBookModeVerdict KCMJudgeChapterResources(IDataBase* targetDB, IDataBase* sourceDB, PMString& outWhy)
{
	// KCMReadResourceList's reason already names the SIDE ("source: ..."), so "Resources: " is
	// prefixed here -- one sentence, assembled in one place, saying which mode and then which side.
	KCMResourceList sourceItems;
	KCMResourceList targetItems;
	PMString why;

	if (!KCMReadResourceList(sourceDB, "source", sourceItems, why) || !KCMReadResourceList(targetDB, "target", targetItems, why))
	{
		outWhy = KCMSentence("Resources: ");
		outWhy.Append(why);
		return kKCMBookVerdictUnjudged;
	}

	// ⚠**A REFUSAL HERE IS NOT "no differences".** KCMDiffResources answers kFalse only when a
	//  container could not grow, and it EMPTIES its output rather than handing back half a list --
	//  so an unasked kFalse would arrive as an empty list and read as "nothing differs", which is
	//  the one wrong answer that looks like a right one.
	KCMResourceChangeList changes;
	KCMResourceDiffStats stats;
	if (!KCMDiffResources(sourceItems, targetItems, changes, stats))
	{
		outWhy = KCMSentence("Resources: out of memory while pairing");
		return kKCMBookVerdictUnjudged;
	}

	return changes.empty() ? kKCMBookVerdictUnchanged : kKCMBookVerdictChanged;
}

// End, KCMBookChapterModes.cpp.
