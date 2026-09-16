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
#include "KCMOriginCompare.h"	// KCMOriginRunInProgress - is a rehydrated origin standing?

namespace
{

/** One story's Source side, as KCMTextRead answered it. */
struct Entry
{
	std::vector<std::string>	fParas;
	std::vector<KCMParaAttrs>	fAttrs;
	std::vector<int32>			fStarts;
	WideString					fRaw;		// the write's own copy - the header says why
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

bool16 KCMSourceCacheMayKeep()
{
	// ★**THE ONE TEST, IN ONE PLACE.** A rehydrated origin is standing: what is being read is a
	//   byte string's document, which nobody can edit and which will be built again identically
	//   the next time somebody asks. An ARMED Source document answers kFalse here and is never
	//   kept - the reader can type in it, and it costs nothing to read anyway.
	return KCMOriginRunInProgress();
}

void KCMSourceCacheClear()
{
	gStories.clear();
}

// End, KCMSourceCache.cpp.
