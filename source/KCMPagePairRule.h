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
//    rule the Story mode has always had). Two documents made separately therefore pair nothing,
//    and every page of both carries the "/" - that is visible, whereas the wall of false
//    "changed" rings the positional rule produced was not.
//
//  THE ONE EXCEPTION IS INSIDE THE RULE, NOT BESIDE IT: a rehydrated task-start copy is a new
//  document whose pages have new ids, but each of them carries the origin's id as a label
//  (KcmOriginUid - written into the copy after the import by KCMRehydrate.cpp, LabelCopyPages,
//  because ImportINX drops the labels the XML carried; KCMXmlInject.h). The caller hands those
//  labels in as `sourceKeys`, so the copy's pages pair by the origin's identity like any other.
//  A source page the write-back could not name (none seen on the real thing, 2026-09-13; the
//  guard is for a spread the table does not hold) is keyless, and is matched by order against
//  the target pages that found no namesake. Nothing else is ever matched by order.
//
//  PURE. A template over the id type, so the offline test (work/kcm-pagepair-test) runs it on
//  plain integers; the plug-in instantiates it on UID.
//
//========================================================================================
#ifndef __KCMPagePairRule_h__
#define __KCMPagePairRule_h__

#include "BaseType.h"		// nil
#include <vector>
#include <map>
#include <set>

/** Pair targetPages with sourcePages by identity.

    @param targetPages  the Target's pages, in document order (the registered ones already taken
                        out by the caller).
    @param sourcePages  the Source's pages, in document order (likewise).
    @param sourceKeys   parallel to sourcePages: the identity each source page answers to. For an
                        ordinary document that is the page's own id (pass sourcePages itself); for
                        a rehydrated copy it is the origin id its label names, or `invalid` for a
                        page whose label did not survive. A key that names more than one page
                        counts for the first of them only; the others are treated as keyless.
    @param invalid      the id value that means "no identity" (kInvalidUID in the plug-in).
    @param outTarget / outSource  one pair per index, both cleared on entry. The keyed pairs come
                        first, in Target order; the keyless ones (a copy's unlabelled page against
                        a leftover target page, both in document order) after them.
    @param outOverflowTarget  (optional) the Target's pages that found no namesake and no keyless
                        page to fall to = ADDED pages, in document order.
    @param outOverflowSource  (optional) the keyed Source pages nobody named = REMOVED pages, in
                        document order. A keyless page never lands here while a target page is
                        left over; when none is, it does. */
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

	// key -> index of the FIRST source page that answers to it
	std::map<Id, size_t> byKey;
	const size_t ns = sourcePages.size();
	for (size_t i = 0; i < ns && i < sourceKeys.size(); ++i)
	{
		if (sourceKeys[i] == invalid)
			continue;
		if (byKey.find(sourceKeys[i]) == byKey.end())
			byKey[sourceKeys[i]] = i;
	}

	// the Target in order: a namesake pairs, the rest wait
	std::vector<bool> used(ns, false);
	std::vector<Id> leftoverTarget;
	for (size_t i = 0; i < targetPages.size(); ++i)
	{
		const typename std::map<Id, size_t>::const_iterator it = byKey.find(targetPages[i]);
		if (it != byKey.end() && !used[it->second])
		{
			used[it->second] = true;
			outTarget.push_back(targetPages[i]);
			outSource.push_back(sourcePages[it->second]);
		}
		else
			leftoverTarget.push_back(targetPages[i]);
	}

	// the keyless source pages (a copy's unlabelled page; a duplicate key's later holders) take
	// the leftover target pages in order; the keyed ones nobody named are removed pages
	size_t nextLeftover = 0;
	for (size_t i = 0; i < ns; ++i)
	{
		if (used[i])
			continue;
		const bool16 keyed = (i < sourceKeys.size() && sourceKeys[i] != invalid
							  && byKey.find(sourceKeys[i]) != byKey.end()
							  && byKey.find(sourceKeys[i])->second == i) ? kTrue : kFalse;
		if (!keyed && nextLeftover < leftoverTarget.size())
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

#endif // __KCMPagePairRule_h__

// End, KCMPagePairRule.h.
