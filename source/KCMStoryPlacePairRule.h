//========================================================================================
//
//  KCMStoryPlacePairRule.h
//
//  THE RULE that pairs a story the UIDs could not pair: an Added story of the Target with a
//  Removed story of the Source, when the first frame of each stands at EXACTLY the same place on
//  corresponding pages.
//
//  ★WHY (2026-10-26, the author): "a frame copied from a past document gets a new UID - brought
//  over and put in the same place". The Story mode pairs stories by UID (KCMStoryStamp.h), so the
//  pasted story and the one it replaced came out as two rows - Added and Removed - and what had
//  changed inside was never compared. This rule gives the two one row.
//
//  ★ONLY EXACTLY THE SAME PLACE AND SIZE (2026-10-25, the author: "位置とサイズだけでいいかな、
//  一先ずは" / "ジオメトリックバウンドというのがまったくおなじならでいいかな"). What "the same" means
//  is the caller's `equal` - in the plug-in PMRect's ==, which is InDesign's own (each side within
//  kPMRealEpsilon); no tolerance of this file's own.
//  ★ONLY ONE TO ONE, BOTH WAYS (the author's call on 2026-10-26, the page rule's thinking of
//  2026-09-13): a target with two equal sources, or a source with two equal targets, pairs with
//  nothing - a wrong pair would compare two different stories and show a difference that is not
//  there, while two rows left unpaired only fail to show one.
//  ★ONLY ON THE SAME PAGE: the caller hands the Target's pages in already turned into the Source
//  pages they are compared against (the page pairing - KCMPageMap.h), so "the same page" here is
//  the plain equality of two Source pages.
//
//  PURE. A template over the id, page and box types, so the offline test (work/kcm-storyplace-test)
//  runs it on plain integers and doubles; the plug-in instantiates it on UID, UID and PMRect
//  (KCMStoryPlace.cpp).
//
//========================================================================================
#ifndef __KCMStoryPlacePairRule_h__
#define __KCMStoryPlacePairRule_h__

#include "BaseType.h"		// bool16
#include <map>
#include <utility>
#include <vector>

/** One story as the rule takes it. A story with no page (or whose page has no partner) is not handed in at all. */
template <class Id, class Page, class Box>
struct KCMStoryAtPlace
{
	Id		fId;
	Page	fPage;	// a Target story: the SOURCE page its page is compared against. A Source story: its own page.
	Box		fBox;	// its first frame's box on that page
};

/** Pair the Target's unpaired stories with the Source's, by the place of their first frames.

    @param equal        equal(targetBox, sourceBox) - "exactly the same place and size".
    @param outPairs     cleared on entry; one (target id, source id) per pair, in Target order. A target pairs
                        when its page holds exactly one source whose box is equal to its own, and that source's
                        page holds exactly one target whose box is equal to the source's. */
template <class Id, class Page, class Box, class Equal>
void KCMPairStoriesByPlace(const std::vector<KCMStoryAtPlace<Id, Page, Box> >& targets,
						   const std::vector<KCMStoryAtPlace<Id, Page, Box> >& sources,
						   Equal equal, std::vector<std::pair<Id, Id> >& outPairs)
{
	outPairs.clear();

	// Each side's stories by page: a story is only ever compared with the other side's on its own page.
	typedef std::map<Page, std::vector<size_t> > ByPage;
	ByPage sourcesOn;
	for (size_t s = 0; s < sources.size(); ++s)
		sourcesOn[sources[s].fPage].push_back(s);
	ByPage targetsOn;
	for (size_t t = 0; t < targets.size(); ++t)
		targetsOn[targets[t].fPage].push_back(t);

	for (size_t t = 0; t < targets.size(); ++t)
	{
		const typename ByPage::const_iterator here = sourcesOn.find(targets[t].fPage);
		if (here == sourcesOn.end())
			continue;

		// exactly one source on this page stands where this target stands ...
		size_t found = sources.size();
		int32 equalSources = 0;
		for (size_t k = 0; k < here->second.size(); ++k)
		{
			const size_t s = here->second[k];
			if (equal(targets[t].fBox, sources[s].fBox))
			{
				found = s;
				++equalSources;
			}
		}
		if (equalSources != 1)
			continue;

		// ... and no other target on the page stands there too
		const std::vector<size_t>& rivals = targetsOn[targets[t].fPage];
		int32 equalTargets = 0;
		for (size_t k = 0; k < rivals.size(); ++k)
			if (equal(targets[rivals[k]].fBox, sources[found].fBox))
				++equalTargets;
		if (equalTargets != 1)
			continue;

		outPairs.push_back(std::make_pair(targets[t].fId, sources[found].fId));
	}
}

#endif // __KCMStoryPlacePairRule_h__

// End, KCMStoryPlacePairRule.h.
