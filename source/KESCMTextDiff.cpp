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
//  D that reaches (n,m) is the answer.
//
//  ⚠**2026-08-21: how the answer is turned back into a list of edits changed.** It used to keep
//  every step's row and walk the path back, which is simple and costs O(D^2) memory - and that
//  memory was the reason the search had to give up past 2000 edits. It now searches from both
//  ends until the frontiers meet and recurses on the two halves (Myers' §4b refinement), which
//  keeps only two rows: **O(N+M) memory, no ceiling**. See the block above FrontierRows.
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

	//------------------------------------------------------------------------------------
	// The search, in linear space (2026-08-21).
	//------------------------------------------------------------------------------------
	//
	// ★★★**WHY THIS REPLACED THE STRAIGHTFORWARD VERSION.** The obvious way to turn "the
	//   distance is D" into "here are the D edits" is to keep every step's row and walk the path
	//   back afterwards - which is what this file did until 2026-08-21. It is correct, but it
	//   costs **O(D²) memory**: D+1 rows of 2D+3 integers. At the old ceiling of D=2000 that is
	//   about 31 MB, and raising the ceiling squares it (D=10,000 → ~800 MB). ⇒ **the ceiling
	//   was not a policy, it was the only thing standing between the plug-in and that number**,
	//   and every story that needed more edits than the ceiling got NO detail at all (the
	//   paragraph pass returned kFalse) or a coarse one (the character pass fell back to "this
	//   whole run changed").
	//
	// ★**The fix is Myers' own refinement (§4b of the 1986 paper): divide and conquer.**
	//   Search from both ends at once until the two frontiers meet; the place they meet - the
	//   "middle snake" - splits the problem into two smaller ones that are solved the same way.
	//   Nothing but the two frontier rows is ever kept, so the memory is **O(N+M)** no matter how
	//   different the two texts are: 50,000 characters against 50,000 costs under a megabyte.
	//   The time stays O((N+M)·D) with a constant factor of roughly two ---- the user's call was
	//   "slower is fine, but it must not be wrong and it should not eat memory" (2026-08-21).
	//
	// ⚠**The ceiling still exists, but it now guards TIME, not memory.** It is raised to a value
	//   no real document reaches (see KESCMTextDiff.h); it is kept only so that a pathological
	//   pair cannot spin forever.

	/* FrontierRows
	   The two rows the divide-and-conquer search works on, allocated ONCE for the whole run and
	   reused by every level of the recursion - allocating them per level would put the O(N+M)
	   back into the inner loop.
	     forward[offset + k] - how far the forward search has reached on diagonal k
	     reverse[offset + k] - the same for the search that starts at the far end
	   `budget` is what is left of the edit distance the caller allowed; it is spent by each
	   middle snake and, when it runs out, `gaveUp` is set and every level unwinds.
	*/
	struct FrontierRows
	{
		std::vector<int32> forward;
		std::vector<int32> reverse;
		int32              offset;
		int32              budget;
		bool16             gaveUp;
	};

	/* FindMiddleSnake
	   Runs the two frontiers forward until they overlap, and reports the snake they met on:
	   (outX,outY) is where it starts and (outU,outV) is where it ends, both in this section's own
	   coordinates. outD is the edit distance of the whole section.

	   ★**It cannot fail to find one** - the frontiers must meet by d = (n+m+1)/2 - so there is no
	     "not found" path. What it can do is cost more than the caller allowed, which is the
	     budget check at the top of each step.

	   ⚠**The reverse search runs in its own coordinates** (x' counted from the far end), and its
	     diagonal k' corresponds to the forward diagonal delta-k'. Every conversion between the
	     two is written out below rather than folded into an index, because getting one of them
	     backwards produces a plausible-looking diff that is subtly wrong.
	*/
	void FindMiddleSnake(const int32* a, int32 n, const int32* b, int32 m, FrontierRows& rows,
						 int32& outX, int32& outY, int32& outU, int32& outV, int32& outD)
	{
		const int32 offset = rows.offset;
		const int32 delta  = n - m;
		const bool16 odd   = ((delta & 1) != 0) ? kTrue : kFalse;
		const int32 maxD   = (n + m + 1) / 2 + 1;

		std::vector<int32>& vf = rows.forward;
		std::vector<int32>& vr = rows.reverse;

		// Clear what this call can touch. Both searches read one diagonal either side of the one
		// they are writing, and the overlap test reads the OTHER row at delta-k, so the span has
		// to cover |delta| as well as maxD.
		// ⚠**n+m is NOT enough.** With one side much longer than the other, |delta-k| reaches
		//   |delta| + maxD ≈ (n+m) + (n+m)/2 (e.g. n=10, m=2: delta=8, maxD=7, so 15 > 12). The
		//   rows are sized to the same 2*(n+m)+3 at the top level, so this stays inside them.
		const int32 span = 2 * (n + m) + 3;
		for (int32 i = -span; i <= span; ++i)
		{
			vf[offset + i] = 0;
			vr[offset + i] = 0;
		}

		for (int32 d = 0; d <= maxD; ++d)
		{
			if (rows.budget >= 0 && d * 2 > rows.budget)
			{
				rows.gaveUp = kTrue;
				outX = outY = outU = outV = 0;
				outD = 0;
				return;
			}

			// ---- forward one step ----
			for (int32 k = -d; k <= d; k += 2)
			{
				int32 x;
				if (k == -d || (k != d && vf[offset + k - 1] < vf[offset + k + 1]))
					x = vf[offset + k + 1];			// down: took one element from b
				else
					x = vf[offset + k - 1] + 1;		// right: took one element from a

				int32 y = x - k;
				const int32 snakeX = x, snakeY = y;	// where the free diagonal run begins

				while (x < n && y < m && a[x] == b[y])
				{
					++x;
					++y;
				}
				vf[offset + k] = x;

				// The frontiers can only cross on a diagonal the reverse search has already
				// reached, which for an ODD delta is exactly during a forward step.
				if (odd && (delta - k) >= -(d - 1) && (delta - k) <= (d - 1))
				{
					if (vf[offset + k] + vr[offset + delta - k] >= n)
					{
						outX = snakeX; outY = snakeY;
						outU = x;      outV = y;
						outD = 2 * d - 1;
						return;
					}
				}
			}

			// ---- reverse one step ----
			for (int32 k = -d; k <= d; k += 2)
			{
				int32 x;
				if (k == -d || (k != d && vr[offset + k - 1] < vr[offset + k + 1]))
					x = vr[offset + k + 1];
				else
					x = vr[offset + k - 1] + 1;

				int32 y = x - k;
				const int32 snakeX = x, snakeY = y;	// in REVERSE coordinates

				while (x < n && y < m && a[n - 1 - x] == b[m - 1 - y])
				{
					++x;
					++y;
				}
				vr[offset + k] = x;

				// ...and for an EVEN delta, only during a reverse step.
				if (!odd && (delta - k) >= -d && (delta - k) <= d)
				{
					if (vr[offset + k] + vf[offset + delta - k] >= n)
					{
						// Back into forward coordinates: the reverse search's (x,y) counts from
						// the far end, so its END is the snake's START and vice versa.
						outX = n - x;      outY = m - y;
						outU = n - snakeX; outV = m - snakeY;
						outD = 2 * d;
						return;
					}
				}
			}
		}

		// Unreachable: the frontiers must meet by maxD. Treated as "gave up" rather than
		// asserted, so that a future change to the loop bound cannot turn into a wrong answer.
		rows.gaveUp = kTrue;
		outX = outY = outU = outV = 0;
		outD = 0;
	}

	/* SearchSection
	   One level of the divide and conquer. aOfs/bOfs are where this section sits in the sequences
	   the caller handed in, so that the edits are reported in those coordinates.

	   ★**The common head and tail are stripped again at every level.** That is what makes the
	     d<=1 cases disappear: once both ends differ and neither side is empty, the distance is at
	     least 2, so the middle snake always splits the problem into strictly smaller pieces and
	     the recursion cannot stand still.

	   ★**Left half first.** Append() merges an edit into the previous one when they touch, which
	     is how a replacement (a deletion and an insertion at the same spot) arrives as one entry
	     ---- and that only works if the edits arrive in order.
	*/
	void SearchSection(const int32* a, int32 aOfs, int32 n, const int32* b, int32 bOfs, int32 m,
					   FrontierRows& rows, std::vector<Change>& changes)
	{
		if (rows.gaveUp)
			return;

		// Strip the common head and tail of THIS section.
		int32 head = 0;
		while (head < n && head < m && a[head] == b[head])
			++head;
		a += head; b += head; aOfs += head; bOfs += head; n -= head; m -= head;

		int32 tail = 0;
		while (tail < n && tail < m && a[n - 1 - tail] == b[m - 1 - tail])
			++tail;
		n -= tail; m -= tail;

		if (n == 0 && m == 0)
			return;								// identical section
		if (n == 0)
		{
			Append(changes, aOfs, 0, bOfs, m);	// pure insertion
			return;
		}
		if (m == 0)
		{
			Append(changes, aOfs, n, bOfs, 0);	// pure deletion
			return;
		}

		int32 x = 0, y = 0, u = 0, v = 0, d = 0;
		FindMiddleSnake(a, n, b, m, rows, x, y, u, v, d);
		if (rows.gaveUp)
			return;

		if (rows.budget >= 0)
			rows.budget -= d;

		SearchSection(a,     aOfs,     x,     b,     bOfs,     y,     rows, changes);
		SearchSection(a + u, aOfs + u, n - u, b + v, bOfs + v, m - v, rows, changes);
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

		// The search itself is SearchSection / FindMiddleSnake above; all this does is hand them
		// the rows to work in and decide what "gave up" means to the caller.
		//
		// ★**Allocated once for the whole run.** The width has to cover more than the diagonals
		//   the search writes: the overlap test reads the OTHER row at delta-k, and |delta-k| can
		//   reach (n+m) + (n+m)/2 when one side is much longer than the other. 2*(n+m)+3 covers
		//   that with room to spare and is still O(N+M) ---- 50,000 against 50,000 is 1.6 MB for
		//   both rows together, against the 31 MB the old row-per-step search needed at D=2000.
		FrontierRows rows;
		rows.offset = 2 * (n + m) + 3;
		rows.forward.assign(2 * rows.offset + 1, 0);
		rows.reverse.assign(2 * rows.offset + 1, 0);
		rows.budget = (maxEdits > 0) ? maxEdits : -1;	// <=0 means no ceiling (see the header)
		rows.gaveUp = kFalse;

		SearchSection(a, 0, n, b, 0, m, rows, changes);

		if (rows.gaveUp)
		{
			// ⚠**A half-built list is worse than none**: the contract with the caller is that an
			//   empty list plus kFalse means "treat the whole section as changed", and a partial
			//   list would instead claim that the parts it did not reach are unchanged.
			changes.clear();
			return kFalse;
		}

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