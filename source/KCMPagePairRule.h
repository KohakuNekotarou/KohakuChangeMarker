//========================================================================================
//
//  KCMPagePairRule.h
//
//  THE RULE that says which page of the Target is compared against which page of the Source:
//  a page is paired with the page of the same identity, and with nothing else.
//
//  Until 2026-09-13 ordinary pages paired BY POSITION (the first with the first, and so on, the
//  registered pages taken out first). That is what made a page inserted in the middle shift every
//  pair after it, and what the Register toggle existed to repair by hand. Page UIDs survive
//  Save As, Save a Copy and the database clone (measured 2026-08-31 / 2026-09-10,
//  docs/ai-notes/kcm-page-pairing-by-uid-proposal-2026-08-31.md), so the identity is there to be
//  read: a page that exists on both sides is the same page wherever it now stands, a page that
//  exists on one side only was added (Target) or removed (Source).
//
//  ★UID ONLY. There is no fall-back to position when the two documents share no ids, and no
//    threshold deciding between the two (the user's decision, 2026-09-13: "UID のみ", the same
//    rule the Story mode has always had).
//    @warning **two documents made separately do not "share no ids"**: page UIDs are small numbers
//      each document hands out in the same order, so they coincide by chance (measured: two new
//      3-page documents both numbered their pages 243, 258, 259; one made with 1 page and 2 added
//      at the front numbered them 264, 258, 243). Such a pair is paired by those numbers - unrelated
//      pages compared, not the "/" on every page. For them the "Pair Pages by UID" toggle goes off
//      (KCMPageMap.h).
//
//  ⛔**THE ONE EXCEPTION STOPPED BEING NEEDED ON 2026-09-21**, and it was inside the rule rather
//  than beside it: a REHYDRATED task-start copy was a new document whose pages had new ids, so each
//  of them carried the origin's id as a label (KcmOriginUid, written into the copy after the import
//  because ImportINX drops the labels the XML carried - KCMXmlInject.h; the writer was
//  KCMRehydrate.cpp's LabelCopyPages, retired with the rehydration). **A copy saved to a FILE
//  carries the numbers the original carries**, so its pages pair by identity with no help at all.
//  ★The way in is untouched - a caller may still hand labels in as `sourceKeys` - and so is the
//  rule below.
//  A source page the write-back could not name (none seen on the real thing, 2026-09-13; the
//  guard is for a spread the table does not hold) is keyless, and is matched by order against
//  the target pages that found no namesake. Nothing else is ever matched by order.
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

/** Pair targetPages with sourcePages by identity: a target page answers to its own id, a source page to its key.

    @param targetPages / sourcePages  each side's pages, in document order (the registered ones already taken out).
    @param sourceKeys   parallel to sourcePages: the identity each source page answers to (its own id for an ordinary
                        document, or `invalid`). A key that names more than one source page counts for the first of
                        them only; the others are treated as keyless.
    @param invalid      the id value that means "no identity" (kInvalidUID in the plug-in).
    @param outTarget / outSource  one pair per index, both cleared on entry. The keyed pairs come first, in Target
                        order; the keyless ones (a keyless source page against a leftover target page, both in document
                        order) after them.
    @param outOverflowTarget  (optional) the Target's pages that found no namesake and no keyless page to fall to =
                        ADDED pages, in document order.
    @param outOverflowSource  (optional) the keyed Source pages nobody named = REMOVED pages, in document order. A
                        keyless page never lands here while a target page is left over; when none is, it does. */
template <class Id>
void KCMPairPagesByIdentity(const std::vector<Id>& targetPages,
							const std::vector<Id>& sourcePages,
							const std::vector<Id>& sourceKeys,
							Id invalid,
							std::vector<Id>& outTarget, std::vector<Id>& outSource,
							std::vector<Id>* outOverflowTarget, std::vector<Id>* outOverflowSource)
{
	outTarget.clear();
	outSource.clear();
	if (outOverflowTarget) outOverflowTarget->clear();
	if (outOverflowSource) outOverflowSource->clear();

	// key -> index of the FIRST source page that answers to it. That page OWNS the key (ownsKey), decided here once:
	// a keyless page, and a later page naming a key already owned, own nothing.
	std::map<Id, size_t> byKey;
	const size_t ns = sourcePages.size();
	std::vector<bool> ownsKey(ns, false);
	for (size_t i = 0; i < ns && i < sourceKeys.size(); ++i)
	{
		if (sourceKeys[i] != invalid && byKey.insert(std::make_pair(sourceKeys[i], i)).second)
			ownsKey[i] = true;
	}

	// the Target in order: a namesake pairs, the rest wait
	std::vector<bool> used(ns, false);
	std::vector<Id> leftoverTarget;
	for (size_t i = 0; i < targetPages.size(); ++i)
	{
		const Id key = targetPages[i];
		const typename std::map<Id, size_t>::const_iterator it = (key == invalid) ? byKey.end() : byKey.find(key);
		if (it != byKey.end() && !used[it->second])
		{
			used[it->second] = true;
			outTarget.push_back(targetPages[i]);
			outSource.push_back(sourcePages[it->second]);
		}
		else
			leftoverTarget.push_back(targetPages[i]);
	}

	// the source pages that own no key (a copy's unlabelled page; a duplicate key's later holders)
	// take the leftover target pages in order; the key owners nobody named are removed pages
	size_t nextLeftover = 0;
	for (size_t i = 0; i < ns; ++i)
	{
		if (used[i])
			continue;
		if (!ownsKey[i] && nextLeftover < leftoverTarget.size())
		{
			outTarget.push_back(leftoverTarget[nextLeftover++]);
			outSource.push_back(sourcePages[i]);
			used[i] = true;
		}
		else if (outOverflowSource)
			outOverflowSource->push_back(sourcePages[i]);
	}

	if (outOverflowTarget)
		for (size_t i = nextLeftover; i < leftoverTarget.size(); ++i)
			outOverflowTarget->push_back(leftoverTarget[i]);
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
