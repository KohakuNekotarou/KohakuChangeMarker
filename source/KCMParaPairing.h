//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Which paragraph of the document goes with which paragraph of an edited story file, when the file
//  has more or fewer of them (2026-09-17, the user's rule: "a <p> added or removed in a text editor is
//  a paragraph added or removed"). PURE: the import pours by it (KCMStoryTextImport), and the harness
//  in work\kcm-storydocx-test measures it outside InDesign.
//
//========================================================================================

#ifndef __KCMParaPairing_h__
#define __KCMParaPairing_h__

#include "BaseType.h"
#include "KCMTextDiff.h"

#include <string>
#include <vector>

namespace KCMParaPairing
{

/** One step of turning one place's paragraphs (the body, a cell, a note) into the file's. In document
	order. */
struct Step
{
	enum Kind { kPair = 0, kInsert = 1, kDelete = 2 };

	int32	fKind;
	/** kPair / kDelete: the document paragraph. kInsert: the document paragraph the new ones FOLLOW, or
		-1 when they go in before the first. */
	int32	fDoc;
	/** kPair: the file paragraph. kInsert: the first of the new ones. kDelete: -1. */
	int32	fFile;
	/** kInsert / kDelete: how many paragraphs, in a row. kPair: 1. */
	int32	fCount;

	Step() : fKind(kPair), fDoc(0), fFile(0), fCount(1) {}
	Step(int32 kind, int32 doc, int32 file, int32 count) : fKind(kind), fDoc(doc), fFile(file), fCount(count) {}
};

/** How many code points two paragraphs share, as the diff counts them. */
inline int32 Similarity(const std::string& a, const std::string& b)
{
	std::vector<int32> ca;
	std::vector<int32> cb;
	KCMTextDiff::ToCodePoints(a, &ca, nil);
	KCMTextDiff::ToCodePoints(b, &cb, nil);
	if (a == b)
		return static_cast<int32>(ca.size());
	std::vector<KCMTextDiff::Change> changes;
	if (!KCMTextDiff::Diff(ca, cb, changes))
		return 0;
	int32 changed = 0;
	for (size_t k = 0; k < changes.size(); ++k)
		changed += changes[k].aCount;
	return static_cast<int32>(ca.size()) - changed;
}

/** Of `few` paragraphs and `many` (few < many), which of the many each of the few pairs with: the
	ordered choice that shares the most characters. `chosen` gets one index into `many` per paragraph of
	`few`, ascending. Ties go to the EARLIER paragraph - a split paragraph keeps its first half.
	@param manyIsFile kTrue when `many` is the file's side (Similarity is asked document first). */
inline void ChooseOrdered(const std::vector<std::string>& few, int32 fewStart, int32 fewCount,
						  const std::vector<std::string>& many, int32 manyStart, int32 manyCount,
						  bool16 manyIsFile, std::vector<int32>& chosen)
{
	chosen.clear();
	const int32 n = fewCount;
	const int32 m = manyCount;
	// dp[i][j]: the best score pairing the first i of `few` among the first j of `many`.
	std::vector< std::vector<int32> > dp(static_cast<size_t>(n + 1), std::vector<int32>(static_cast<size_t>(m + 1), 0));
	std::vector< std::vector<int32> > sim(static_cast<size_t>(n), std::vector<int32>(static_cast<size_t>(m), 0));
	for (int32 i = 0; i < n; ++i)
		for (int32 j = 0; j < m; ++j)
			sim[i][j] = manyIsFile ? Similarity(few[fewStart + i], many[manyStart + j])
								   : Similarity(many[manyStart + j], few[fewStart + i]);
	const int32 kImpossible = -1;
	for (int32 i = 1; i <= n; ++i)
	{
		for (int32 j = 0; j <= m; ++j)
		{
			if (j < i)
			{
				dp[i][j] = kImpossible;
				continue;
			}
			int32 best = (j - 1 >= i) ? dp[i][j - 1] : kImpossible;
			if (dp[i - 1][j - 1] != kImpossible && dp[i - 1][j - 1] + sim[i - 1][j - 1] > best)
				best = dp[i - 1][j - 1] + sim[i - 1][j - 1];
			dp[i][j] = best;
		}
	}
	// Walk back. Skipping the later paragraph whenever that costs nothing is what sends a tie to the
	// earlier one.
	std::vector<int32> reversed;
	int32 i = n;
	int32 j = m;
	while (i > 0)
	{
		if (j - 1 >= i && dp[i][j - 1] != kImpossible && dp[i][j - 1] >= dp[i][j])
		{
			--j;
			continue;
		}
		reversed.push_back(j - 1);
		--i;
		--j;
	}
	for (size_t k = reversed.size(); k > 0; --k)
		chosen.push_back(manyStart + reversed[k - 1]);
}

/** The steps that turn `doc` into `file` - paragraphs paired, paragraphs inserted, paragraphs deleted.

	★**UNCHANGED PARAGRAPHS PAIR WHERE THEY STAND**; the runs between them are paired as far as the
	  shorter side goes, choosing the pairs that share the most characters (so a paragraph added ABOVE
	  an edited one pairs the edited one, not the new one), and the rest is inserted or deleted.
	⚠**NEVER AN INSERTION AND A DELETION IN ONE RUN**: a run of three against two is two pairs and one
	  deletion, not three deletions and two insertions - a paired paragraph keeps its style and its
	  marks, and only its words are rewritten. */
inline void Pair(const std::vector<std::string>& doc, const std::vector<std::string>& file,
				 std::vector<Step>& out)
{
	out.clear();

	std::vector<std::string> table;
	std::vector<int32> a;
	std::vector<int32> b;
	KCMTextDiff::Tokenize(doc, table, a);
	KCMTextDiff::Tokenize(file, table, b);

	std::vector<KCMTextDiff::Change> changes;
	if (!KCMTextDiff::Diff(a, b, changes))
		changes.clear();

	int32 ai = 0;
	int32 bi = 0;
	for (size_t c = 0; c <= changes.size(); ++c)
	{
		const int32 aStop = (c < changes.size()) ? changes[c].aStart : static_cast<int32>(doc.size());
		const int32 bStop = (c < changes.size()) ? changes[c].bStart : static_cast<int32>(file.size());
		for (; ai < aStop && bi < bStop; ++ai, ++bi)
			out.push_back(Step(Step::kPair, ai, bi, 1));
		if (c == changes.size())
			break;

		const KCMTextDiff::Change& ch = changes[c];
		if (ch.aCount == ch.bCount)
		{
			for (int32 k = 0; k < ch.aCount; ++k)
				out.push_back(Step(Step::kPair, ch.aStart + k, ch.bStart + k, 1));
		}
		else if (ch.aCount < ch.bCount)
		{
			std::vector<int32> chosen;		// the file paragraph each document paragraph pairs with
			ChooseOrdered(doc, ch.aStart, ch.aCount, file, ch.bStart, ch.bCount, kTrue, chosen);
			int32 nextFile = ch.bStart;
			int32 after = ch.aStart - 1;
			for (int32 k = 0; k <= ch.aCount; ++k)
			{
				const int32 upTo = (k < ch.aCount) ? chosen[static_cast<size_t>(k)] : ch.bStart + ch.bCount;
				if (upTo > nextFile)
					out.push_back(Step(Step::kInsert, after, nextFile, upTo - nextFile));
				if (k < ch.aCount)
				{
					out.push_back(Step(Step::kPair, ch.aStart + k, upTo, 1));
					after = ch.aStart + k;
					nextFile = upTo + 1;
				}
			}
		}
		else
		{
			std::vector<int32> chosen;		// the document paragraph each file paragraph pairs with
			ChooseOrdered(file, ch.bStart, ch.bCount, doc, ch.aStart, ch.aCount, kFalse, chosen);
			int32 nextDoc = ch.aStart;
			for (int32 k = 0; k <= ch.bCount; ++k)
			{
				const int32 upTo = (k < ch.bCount) ? chosen[static_cast<size_t>(k)] : ch.aStart + ch.aCount;
				for (int32 d = nextDoc; d < upTo; ++d)
				{
					if (!out.empty() && out.back().fKind == Step::kDelete
						&& out.back().fDoc + out.back().fCount == d)
						++out.back().fCount;
					else
						out.push_back(Step(Step::kDelete, d, -1, 1));
				}
				if (k < ch.bCount)
				{
					out.push_back(Step(Step::kPair, upTo, ch.bStart + k, 1));
					nextDoc = upTo + 1;
				}
			}
		}
		ai = ch.aStart + ch.aCount;
		bi = ch.bStart + ch.bCount;
	}
}

}	// namespace KCMParaPairing

#endif // __KCMParaPairing_h__

// End, KCMParaPairing.h.
