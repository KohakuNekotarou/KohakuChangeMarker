//========================================================================================
//
//  KCMSourceCache.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <map>
#include <string>
#include <vector>

#include "KCMSourceCache.h"

namespace
{

/** One story's Source side, as KCMTextRead answered it. */
struct Entry
{
	std::vector<std::string>	fParas;
	std::vector<KCMParaAttrs>	fAttrs;
	std::vector<int32>			fStarts;
	WideString					fRaw;		// the write's own copy - the header says why
	std::vector<KCMTableShape>	fTables;	// the story's tables, for the Table row's compare (2026-09-20)
	bool16						fHaveTables;
	Entry() : fHaveTables(kFalse) {}
};

/** Keyed by the TARGET story's uid: the copy's own uids are new ones on every rehydration, so
	they cannot name anything that outlives one.

	⚠A static holding strings, so it has a line in the model's shutdown - the rule and what
	 forgetting it costs are in KCMStoryList.h. */
std::map<UID, Entry>	gStories;

}	// anonymous namespace

bool16 KCMSourceCacheGet(UID targetStoryUID,
						 std::vector<std::string>& outParas,
						 std::vector<KCMParaAttrs>& outAttrs,
						 std::vector<int32>& outStarts)
{
	const std::map<UID, Entry>::const_iterator it = gStories.find(targetStoryUID);
	if (it == gStories.end())
		return kFalse;

	outParas  = it->second.fParas;
	outAttrs  = it->second.fAttrs;
	outStarts = it->second.fStarts;
	return kTrue;
}

void KCMSourceCachePut(UID targetStoryUID,
					   const std::vector<std::string>& paras,
					   const std::vector<KCMParaAttrs>& attrs,
					   const std::vector<int32>& starts,
					   const WideString& raw)
{
	if (!KCMSourceCacheMayKeep())
		return;

	Entry& entry = gStories[targetStoryUID];
	entry.fParas  = paras;
	entry.fAttrs  = attrs;
	entry.fStarts = starts;
	entry.fRaw    = raw;
}

bool16 KCMSourceCacheGetRaw(UID targetStoryUID, WideString& outRaw)
{
	const std::map<UID, Entry>::const_iterator it = gStories.find(targetStoryUID);
	if (it == gStories.end())
		return kFalse;

	outRaw = it->second.fRaw;
	return kTrue;
}

bool16 KCMSourceCacheHas(UID targetStoryUID)
{
	return (gStories.find(targetStoryUID) != gStories.end()) ? kTrue : kFalse;
}

void KCMSourceCachePutTableShapes(UID targetStoryUID, const std::vector<KCMTableShape>& shapes)
{
	if (!KCMSourceCacheMayKeep())
		return;
	Entry& entry = gStories[targetStoryUID];
	entry.fTables = shapes;
	entry.fHaveTables = kTrue;
}

bool16 KCMSourceCacheGetTableShapes(UID targetStoryUID, std::vector<KCMTableShape>& outShapes)
{
	const std::map<UID, Entry>::const_iterator it = gStories.find(targetStoryUID);
	if (it == gStories.end() || !it->second.fHaveTables)
		return kFalse;
	outShapes = it->second.fTables;
	return kTrue;
}

bool16 KCMSourceCacheMayKeep()
{
	// ⛔★★**NOTHING MAY BE KEPT SINCE 2026-09-21, AND THAT IS NOT A SETTING.** This cache existed
	//   for ONE case: a rehydrated origin - a byte string's document that nobody could edit and
	//   that would be rebuilt identically the next time anyone asked. A Task Start is a file Start
	//   opens now, so every Source is a document THE READER CAN TYPE IN, and a Source that can
	//   change must not be remembered. The test that answered this (KCMOriginRunInProgress) went
	//   with the origin.
	// ⬜**The machinery around it is dead weight now.** Taking it out reaches into the middle of
	//   the Story comparison and of the restore, so it is left standing - empty - rather than
	//   bundled into the origin's removal: Put keeps nothing and Get finds nothing.
	return kFalse;
}

void KCMSourceCacheClear()
{
	gStories.clear();
}

// End, KCMSourceCache.cpp.
