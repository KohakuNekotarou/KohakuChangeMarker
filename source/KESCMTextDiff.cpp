//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMTextDiff.h for where the method comes from, why nothing is borrowed from git, and
//  the warning about the second copy of this file that still lives in KohakuTest.
//
//  The shape of the search, in one paragraph: walk the edit graph from (0,0) to (n,m), where
//  moving right consumes one baseline element (a deletion) and moving down consumes one target
//  element (an insertion), and a diagonal step is free because the two elements are equal. For
//  each edit distance D in turn, record how far each diagonal k = x - y has reached; the first
//  D that reaches (n,m) is the answer. Keeping every step's row lets the path be walked back
//  afterwards, which is what turns "the distance is 3" into "here are the three edits".
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include <map>

// Project includes:
#include "KESCMTextDiff.h"

namespace
{
	using KESCMTextDiff::Change;

	/* Append
	   Adds one edit, merging it into the previous one when they touch. A replacement reaches
	   here as a deletion and an insertion at the same place, and reporting those separately
	   would mean "this paragraph was rewritten" arrives as two entries pointing at one spot.
	*/
	void Append(std::vector<Change>& changes, int32 aStart, int32 aCount, int32 bStart, int32 bCount)
	{
		if (aCount == 0 && bCount == 0)
			return;

		if (!changes.empty())
		{
			Change& last = changes.back();
			if (last.aStart + last.aCount == aStart && last.bStart + last.bCount == bStart)
			{
				last.aCount += aCount;
				last.bCount += bCount;
				return;
			}
		}

		Change change;
		change.aStart = aStart;
		change.aCount = aCount;
		change.bStart = bStart;
		change.bCount = bCount;
		changes.push_back(change);
	}

	/* MyersSearch
	   The core. Works on the middle section only - the caller has already stripped whatever head
	   and tail the two sequences share - and reports positions relative to that section, which
	   the caller shifts back by the head length.
	*/
	bool16 MyersSearch(const int32* a, int32 n, const int32* b, int32 m,
					   std::vector<Change>& changes, int32 maxEdits)
	{
		if (n == 0 && m == 0)
			return kTrue;

		// One side empty is the whole answer already, and the general search would only arrive
		// at the same place after n+m steps.
		if (n == 0)
		{
			Append(changes, 0, 0, 0, m);
			return kTrue;
		}
		if (m == 0)
		{
			Append(changes, 0, n, 0, 0);
			return kTrue;
		}

		const int32 maxD = (n + m < maxEdits) ? (n + m) : maxEdits;
		const int32 offset = maxD + 1;			// k runs -maxD..maxD, so shift it to index an array
		const int32 width = 2 * maxD + 3;

		std::vector<int32> v(width, 0);
		std::vector<std::vector<int32> > trace;	// one row per step, so the path can be walked back
		trace.reserve(maxD + 1);

		int32 foundD = -1;
		for (int32 d = 0; d <= maxD; ++d)
		{
			// The row is saved BEFORE this step's writes, so trace[d] is the state the step
			// started from - that is what the backward walk needs.
			trace.push_back(v);

			for (int32 k = -d; k <= d; k += 2)
			{
				const int32 index = k + offset;

				int32 x = 0;
				if (k == -d || (k != d && v[index - 1] < v[index + 1]))
					x = v[index + 1];			// down: took one element from b
				else
					x = v[index - 1] + 1;		// right: took one element from a

				int32 y = x - k;

				while (x < n && y < m && a[x] == b[y])
				{
					++x;
					++y;
				}

				v[index] = x;

				if (x >= n && y >= m)
				{
					foundD = d;
					break;
				}
			}

			if (foundD >= 0)
				break;
		}

		if (foundD < 0)
			return kFalse;		// gave up at maxEdits; the caller decides what that means

		// Walk back. At each step the row that produced the current point says which move was
		// taken to get onto this diagonal, and the free diagonal run before it is skipped.
		int32 x = n;
		int32 y = m;
		std::vector<Change> reversed;

		for (int32 d = foundD; d > 0; --d)
		{
			const std::vector<int32>& previous = trace[d];
			const int32 k = x - y;
			const int32 index = k + offset;

			int32 previousK = 0;
			if (k == -d || (k != d && previous[index - 1] < previous[index + 1]))
				previousK = k + 1;		// arrived by a down move
			else
				previousK = k - 1;		// arrived by a right move

			const int32 previousX = previous[previousK + offset];
			const int32 previousY = previousX - previousK;

			// Skip the diagonal run - those elements are equal and are not part of any edit.
			while (x > previousX && y > previousY)
			{
				--x;
				--y;
			}

			if (previousK == k + 1)
				Append(reversed, x, 0, previousY, 1);		// an insertion of b[previousY]
			else
				Append(reversed, previousX, 1, y, 0);		// a deletion of a[previousX]

			x = previousX;
			y = previousY;
		}

		// The walk produced the edits back to front, and Append merged neighbours in that order,
		// so reverse the list and rebuild each entry's start from its own numbers.
		for (int32 i = static_cast<int32>(reversed.size()) - 1; i >= 0; --i)
			Append(changes, reversed[i].aStart, reversed[i].aCount, reversed[i].bStart, reversed[i].bCount);

		return kTrue;
	}
}

/* Diff
*/
bool16 KESCMTextDiff::Diff(const std::vector<int32>& a, const std::vector<int32>& b,
						std::vector<Change>& changes, int32 maxEdits)
{
	changes.clear();

	const int32 aSize = static_cast<int32>(a.size());
	const int32 bSize = static_cast<int32>(b.size());

	// Strip the common head and tail. For the case this exists for - a long story with a few
	// edits in the middle - this is what does most of the work, and what is left over is small
	// enough that the O((N+M)*D) search never becomes the thing that hurts.
	int32 head = 0;
	while (head < aSize && head < bSize && a[head] == b[head])
		++head;

	int32 tail = 0;
	while (tail < (aSize - head) && tail < (bSize - head) && a[aSize - 1 - tail] == b[bSize - 1 - tail])
		++tail;

	const int32 aMiddle = aSize - head - tail;
	const int32 bMiddle = bSize - head - tail;

	if (aMiddle == 0 && bMiddle == 0)
		return kTrue;		// identical

	std::vector<Change> middle;
	const bool16 ok = MyersSearch(aMiddle > 0 ? &a[head] : nil, aMiddle,
								  bMiddle > 0 ? &b[head] : nil, bMiddle,
								  middle, maxEdits);
	if (!ok)
		return kFalse;

	for (size_t i = 0; i < middle.size(); ++i)
	{
		Change change = middle[i];
		change.aStart += head;
		change.bStart += head;
		changes.push_back(change);
	}

	return kTrue;
}

/* MergeNearbyChanges
   See the header for where the rule comes from and why it is not applied to paragraphs.

   Looping until nothing changes matters: merging two runs can bring a third close enough to
   merge as well, and one pass would leave that one behind. It terminates because every pass
   that sets merged either removed an entry or did nothing.
*/
void KESCMTextDiff::MergeNearbyChanges(std::vector<Change>& changes)
{
	if (changes.size() < 2)
		return;

	bool16 mergedSomething = kTrue;
	while (mergedSomething)
	{
		mergedSomething = kFalse;

		std::vector<Change> merged;
		merged.reserve(changes.size());

		for (size_t i = 0; i < changes.size(); ++i)
		{
			if (merged.empty())
			{
				merged.push_back(changes[i]);
				continue;
			}

			Change& previous = merged.back();
			const Change& current = changes[i];

			// The unchanged run between the two. Both sides give the same length - the run is
			// unchanged, after all - so a disagreement would mean the change list is malformed;
			// take the smaller and let the comparison below decline to merge.
			const int32 gapA = current.aStart - (previous.aStart + previous.aCount);
			const int32 gapB = current.bStart - (previous.bStart + previous.bCount);
			const int32 gap = (gapA < gapB) ? gapA : gapB;

			// A change's size is the larger of what it removed and what it added: replacing one
			// word with a longer one is one edit the size of the longer word.
			const int32 previousSize = (previous.aCount > previous.bCount) ? previous.aCount : previous.bCount;
			const int32 currentSize = (current.aCount > current.bCount) ? current.aCount : current.bCount;

			// ★★★STRICTLY SHORTER, NOT "no longer than" (2026-08-20). The gap has to be SMALLER
			//   than both neighbours to be swallowed. ⚠With <= , two ONE-character edits a single
			//   character apart always merged (1 <= 1 on both sides) - which in Japanese is not an
			//   edge case but the ordinary sentence: 琥珀猫太郎 -> 琥あ珀犬太郎 came out as one
			//   change reading "珀猫" -> "あ珀犬", when what happened is that あ was inserted and 猫
			//   became 犬 (user's report, 2026-08-20; Myers had said exactly that and this rule
			//   undid it).
			//   ★Measured against 10 cases outside InDesign before changing it: the ONLY one that
			//     moves is that one. Everything this rule exists for still merges, because those
			//     gaps are strictly smaller than their neighbours - "sleeping"->"awake" (1 < 4 and
			//     1 < 5), "postponed"->"cancelled", and two 2-character edits one character apart
			//     (1 < 2). A one-character gap between two one-character edits is the only shape
			//     that <= caught and < does not, and it is precisely the shape that should not be
			//     caught: there is nothing to say those two edits are one.
			//   ⚠The header said BOTH things at once - "no longer than EACH" in one sentence and
			//     "shorter than both" in the next. The implementation followed the first. Fixed
			//     there too.
			if (gap >= 0 && gap < previousSize && gap < currentSize)
			{
				previous.aCount = (current.aStart + current.aCount) - previous.aStart;
				previous.bCount = (current.bStart + current.bCount) - previous.bStart;
				mergedSomething = kTrue;
			}
			else
			{
				merged.push_back(current);
			}
		}

		changes.swap(merged);
	}
}

/* Tokenize
*/
void KESCMTextDiff::Tokenize(const std::vector<std::string>& strings,
						  std::vector<std::string>& table, std::vector<int32>& tokens)
{
	tokens.clear();
	tokens.reserve(strings.size());

	// A map from the strings already numbered, rebuilt from the table so that a caller can pass
	// the same table to two calls and have the numbering agree across them.
	std::map<std::string, int32> index;
	for (size_t i = 0; i < table.size(); ++i)
		index[table[i]] = static_cast<int32>(i);

	for (size_t i = 0; i < strings.size(); ++i)
	{
		std::map<std::string, int32>::const_iterator found = index.find(strings[i]);
		if (found != index.end())
		{
			tokens.push_back(found->second);
		}
		else
		{
			const int32 token = static_cast<int32>(table.size());
			table.push_back(strings[i]);
			index[strings[i]] = token;
			tokens.push_back(token);
		}
	}
}

/* ToCodePoints
*/
void KESCMTextDiff::ToCodePoints(const std::string& utf8, std::vector<int32>& codePoints,
							  std::vector<int32>* byteOffsets)
{
	codePoints.clear();
	if (byteOffsets != nil)
		byteOffsets->clear();

	size_t i = 0;
	while (i < utf8.size())
	{
		const unsigned char lead = static_cast<unsigned char>(utf8[i]);

		int32 extra = 0;
		int32 value = 0;
		if (lead < 0x80)			{ extra = 0; value = lead; }
		else if ((lead & 0xE0) == 0xC0)	{ extra = 1; value = lead & 0x1F; }
		else if ((lead & 0xF0) == 0xE0)	{ extra = 2; value = lead & 0x0F; }
		else if ((lead & 0xF8) == 0xF0)	{ extra = 3; value = lead & 0x07; }
		else
		{
			// Not a lead byte. Rather than stop, take it as one unit and carry on: a single bad
			// byte should cost one wrong position, not the whole comparison.
			extra = 0;
			value = lead;
		}

		if (byteOffsets != nil)
			byteOffsets->push_back(static_cast<int32>(i));

		++i;
		for (int32 n = 0; n < extra && i < utf8.size(); ++n, ++i)
			value = (value << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);

		codePoints.push_back(value);
	}
}

// End, KESCMTextDiff.cpp.
