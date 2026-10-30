//========================================================================================
//
//  KCMPagePairRule.h
//
//  THE RULE that says which page of the Target is compared against which page of the Source:
//  a page is paired with the page of the same identity, and with nothing else.
//
//  Pairing BY POSITION (the first with the first, and so on, the registered pages taken out first)
//  makes a page inserted in the middle shift every pair after it, and leaves the Register toggle to
//  repair that by hand. Page UIDs survive Save As, Save a Copy and the database clone (measured,
//  docs/ai-notes/kcm-page-pairing-by-uid-proposal-2026-08-31.md), so the identity is there to be
//  read: a page that exists on both sides is the same page wherever it now stands, a page that
//  exists on one side only was added (Target) or removed (Source).
//
//  UID ONLY. There is no fall-back to position when the two documents share no ids, and no
//    threshold deciding between the two (the author's decision: "UID only").
//    @warning **two documents made separately do not "share no ids"**: page UIDs are small numbers
//      each document hands out in the same order, so they coincide by chance (measured: two new
//      3-page documents both numbered their pages 243, 258, 259; one made with 1 page and 2 added
//      at the front numbered them 264, 258, 243), and this rule would pair such a pair by those
//      numbers - unrelated pages compared. So KCMBuildPairing asks first whether
//      the two were made separately (their XMP OriginalDocumentID differs - KCMPageMapMadeSeparately)
//      and pairs none of their pages then: the "/" on every page, and the report says why. Pairing
//      them in order is the "Pair Pages by UID" toggle's (KCMPageMap.h). This rule is not asked.
//
//  PURE. A template over the id type, so the offline test (work/kcm-pagepair-test) runs it on
//  plain integers; the plug-in instantiates it on UID. The lookup in a pairing once it is built
//  (KCMPartnerOf, at the end) is here for the same reason: what KCMMapTargetToSource and
//  KCMMapSourceToTarget answer is tested where nothing else can reach it (their one caller is the
//  CMYK sampler's Alt + hover).
//
//========================================================================================
#ifndef __KCMPagePairRule_h__
#define __KCMPagePairRule_h__

#include "BaseType.h"		// bool16 / kTrue / kFalse
#include <vector>
#include <map>
#include <utility>			// std::make_pair

/** Pair targetPages with sourcePages by identity: a page pairs with the page of the same id on the other side.

    @param targetPages / sourcePages  each side's pages, in document order (the registered ones already taken out).
    @param outTarget / outSource  one pair per index, in Target order, both cleared on entry.
    @param outOverflowTarget  (optional) the Target's pages with no namesake = ADDED pages, in document order.
    @param outOverflowSource  (optional) the Source's pages nobody named = REMOVED pages, in document order.
    An id is unique within a document; were one to come twice, the first page holding it would answer to it. */
template <class Id>
void KCMPairPagesByIdentity(const std::vector<Id>& targetPages,
							const std::vector<Id>& sourcePages,
							std::vector<Id>& outTarget, std::vector<Id>& outSource,
							std::vector<Id>* outOverflowTarget, std::vector<Id>* outOverflowSource)
{
	outTarget.clear();
	outSource.clear();
	if (outOverflowTarget) outOverflowTarget->clear();
	if (outOverflowSource) outOverflowSource->clear();

	// id -> index of the source page holding it
	std::map<Id, size_t> byId;
	const size_t ns = sourcePages.size();
	for (size_t i = 0; i < ns; ++i)
		byId.insert(std::make_pair(sourcePages[i], i));

	// the Target in order: a namesake pairs, the rest were added
	std::vector<bool> used(ns, false);
	for (size_t i = 0; i < targetPages.size(); ++i)
	{
		const typename std::map<Id, size_t>::const_iterator it = byId.find(targetPages[i]);
		if (it != byId.end() && !used[it->second])
		{
			used[it->second] = true;
			outTarget.push_back(targetPages[i]);
			outSource.push_back(sourcePages[it->second]);
		}
		else if (outOverflowTarget)
			outOverflowTarget->push_back(targetPages[i]);
	}

	// the Source pages nobody named were removed
	if (outOverflowSource)
		for (size_t i = 0; i < ns; ++i)
			if (!used[i])
				outOverflowSource->push_back(sourcePages[i]);
}

/** A page's partner in a pairing that was built - two parallel lists, the i-th of each one pair (outTarget / outSource
	above, or the master pairing's). `page` is looked for in `from`, and the same index of `to` answers.

    @return kFalse when `page` is in no pair - or stands past the end of the shorter list, which a pairing never
            produces: the lists are checked here rather than trusted - and outPartner is then left as it was. */
template <class Id>
bool16 KCMPartnerOf(const std::vector<Id>& from, const std::vector<Id>& to, Id page, Id& outPartner)
{
	for (size_t i = 0; i < from.size() && i < to.size(); ++i)
	{
		if (from[i] == page)
		{
			outPartner = to[i];
			return kTrue;
		}
	}
	return kFalse;
}

#endif // __KCMPagePairRule_h__

// End, KCMPagePairRule.h.
