//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Character ranges to light up, kept in the one shape the marker can draw straight from:
//  sorted, and with nothing overlapping anything else.
//
//  ★★★WHY MERGING IS NOT AN OPTIMISATION HERE - IT IS CORRECTNESS. The mark is painted with
//  Difference blending against white, which INVERTS what is underneath (KESCMStoryMarker.cpp).
//  Painting the same pixels twice inverts them twice, which is the same as not painting them at
//  all: two overlapping ranges would leave a hole exactly where BOTH of them said "look here".
//  So overlaps are merged before anything is drawn, and the drawing side never has to know.
//
//  ⚠Touching ranges are merged too ([0,5) + [5,9) -> [0,9)). They do not overlap, so nothing
//  would be lost by leaving them apart - but two adjacent rectangles are two rectfill calls and
//  one seam, and a press over a story with a few hundred small edits makes plenty of both.
//
//  ★HEADER-ONLY AND FREE OF THE SDK EXCEPT FOR TextIndex, WHICH IS WHAT MAKES IT TESTABLE. The
//  test outside InDesign is work\kescm-markranges-test (it stubs BaseType.h and includes this
//  file as it stands - it is not a copy that can drift, the way KTTextDiff drifted from
//  KESCMTextDiff).
//
//========================================================================================

#ifndef __KESCMStoryMarkRanges_h__
#define __KESCMStoryMarkRanges_h__

#include "BaseType.h"		// TextIndex, bool16

#include <algorithm>
#include <vector>

/** One stretch of characters to light up, in one story. */
struct KESCMMarkRange
{
	TextIndex	fFrom;		// first character
	TextIndex	fTo;		// one PAST the last - an END, not a length (RangeData.h:69)

	/** ★★A CARET, NOT A STRETCH (2026-08-22, user's call: "細いバーにするがいいです、キャレットの位置で").
		A DELETION has no width on the side it was deleted from - there is nothing there to invert -
		and until now it was shown by widening it to one character, which lit up whatever had closed
		up over the gap. That is a different character saying "I am the edit", and in the two cases
		the user hit first it was plainly wrong: deleting a whole paragraph lit the first character
		of the NEXT one, and deleting the end of a story lit the story's final carriage return,
		which draws nothing at all.
		⇒ A caret occupies [fFrom, fFrom+1) so that it sorts, merges and intersects exactly like any
		  other range - but the drawing side gives it a thin bar at the START of that character
		  instead of inverting it (KESCMStoryMarker's GetMarkBoxes). Nothing else has to know. */
	bool16		fCaret;

	KESCMMarkRange() : fFrom(0), fTo(0), fCaret(kFalse) {}
	KESCMMarkRange(TextIndex from, TextIndex to) : fFrom(from), fTo(to), fCaret(kFalse) {}
	KESCMMarkRange(TextIndex from, TextIndex to, bool16 caret) : fFrom(from), fTo(to), fCaret(caret) {}

	/** The caret standing in front of character `at`. */
	static KESCMMarkRange Caret(TextIndex at) { return KESCMMarkRange(at, at + 1, kTrue); }
};

typedef std::vector<KESCMMarkRange> KESCMMarkRangeList;

/** Reading order, and a stable tie-break so that a merge of equal starts is deterministic. */
inline bool KESCMMarkRangeIsBefore(const KESCMMarkRange& a, const KESCMMarkRange& b)
{
	return (a.fFrom != b.fFrom) ? (a.fFrom < b.fFrom) : (a.fTo < b.fTo);
}

/** For the binary search below: is this range finished by the time v starts? */
inline bool KESCMMarkRangeEndsAtOrBefore(const KESCMMarkRange& r, TextIndex v)
{
	return r.fTo <= v;
}

/** Sort, drop the empty ones, and fuse everything that overlaps or touches.

	★EMPTY RANGES ARE DROPPED, NOT WIDENED. A deletion has no width on the side it was deleted
	from, and widening it is a decision about what the reader should see - which belongs to the
	caller that knows it is looking at a deletion (KESCMStoryMarkBuild), not to a list of numbers.
	★That caller's answer since 2026-08-22 is KESCMMarkRange::Caret - see below for why a caret is
	carried through here rather than being fused away.

	★★CARETS ARE KEPT APART FROM THE FUSING, and there are two reasons, both about correctness:
	  ① fusing would lose the flag - a caret swallowed into a neighbouring stretch would come out
	    the other side as an ordinary inverted character, which is the very thing it replaced;
	  ② a caret that sits INSIDE a stretch is dropped, because that place is already lit and
	    Difference blending inverts twice = not at all (the hole this whole file exists to prevent).
	  ⇒ What comes out is still sorted and still non-overlapping, so the binary searches below are
	    unaffected and every existing test still holds.

	@param ranges [in,out] rewritten in place: sorted, non-empty, non-overlapping.
*/
inline void KESCMMergeMarkRanges(KESCMMarkRangeList& ranges)
{
	KESCMMarkRangeList kept, carets;
	kept.reserve(ranges.size());
	for (KESCMMarkRangeList::const_iterator it = ranges.begin(); it != ranges.end(); ++it)
	{
		if (it->fFrom >= it->fTo)
			continue;
		if (it->fCaret)
			carets.push_back(*it);
		else
			kept.push_back(*it);
	}

	std::sort(kept.begin(), kept.end(), KESCMMarkRangeIsBefore);

	KESCMMarkRangeList merged;
	merged.reserve(kept.size() + carets.size());
	for (KESCMMarkRangeList::const_iterator it = kept.begin(); it != kept.end(); ++it)
	{
		// ">" and not ">=": a range that STARTS where the last one ended touches it, and touching
		// ranges are fused (see the header note).
		if (merged.empty() || it->fFrom > merged.back().fTo)
			merged.push_back(*it);
		else if (it->fTo > merged.back().fTo)
			merged.back().fTo = it->fTo;
	}

	if (!carets.empty())
	{
		const size_t stretchCount = merged.size();
		std::sort(carets.begin(), carets.end(), KESCMMarkRangeIsBefore);

		TextIndex lastCaret = -1;
		for (KESCMMarkRangeList::const_iterator c = carets.begin(); c != carets.end(); ++c)
		{
			if (c->fFrom == lastCaret)
				continue;					// the same place asked for twice

			// Is this place already lit by a stretch? Only the fused stretches are searched (the
			// carets added so far are past `stretchCount` and are not sorted against them yet).
			bool inside = false;
			for (size_t i = 0; i < stretchCount; ++i)
			{
				if (merged[i].fFrom <= c->fFrom && c->fFrom < merged[i].fTo) { inside = true; break; }
				if (merged[i].fFrom > c->fFrom) break;		// sorted: no later stretch can hold it
			}
			if (inside)
				continue;

			merged.push_back(*c);
			lastCaret = c->fFrom;
		}

		std::sort(merged.begin(), merged.end(), KESCMMarkRangeIsBefore);

		// ★NOTHING IS CLIPPED AFTERWARDS, AND NOTHING NEEDS TO BE. A caret occupies exactly one
		//   character, [i, i+1). For it to overlap a stretch, that stretch would have to contain i -
		//   and every such caret was just dropped by the test above. A stretch beginning at i+1 only
		//   TOUCHES it, which this list allows between a caret and its neighbour (they are not fused,
		//   deliberately: fusing would lose the flag).
		//   ⚠A clipping pass was written here first and removed: it could never fire, and the one
		//     thing it could do was turn [i, i+1) into an empty range that the intersect below drops.
	}

	ranges.swap(merged);
}

/** The part of a merged list that falls inside [runStart, runEnd), expressed as offsets into
	that run - which is the form the wax asks its questions in (a run reports its own characters
	from 0).

	★BINARY SEARCH, BECAUSE THIS IS THE PER-RUN INNER LOOP. A press marks every edit in every
	story, so a long story can hold thousands of ranges while a wax run holds a handful of
	characters; walking the list for each run would make the cost of drawing a page grow with the
	number of edits in the document rather than with what is on the page.

	@param merged must have been through KESCMMergeMarkRanges - sorted and non-overlapping.
	@param runStart the run's first character, in story coordinates.
	@param runEnd one past its last.
	@param out [out] cleared, then filled with the overlaps as run-relative offsets.
*/
inline void KESCMIntersectMarkRanges(const KESCMMarkRangeList& merged,
									 TextIndex runStart, TextIndex runEnd,
									 KESCMMarkRangeList& out)
{
	out.clear();
	if (runEnd <= runStart || merged.empty())
		return;

	KESCMMarkRangeList::const_iterator it =
		std::lower_bound(merged.begin(), merged.end(), runStart, KESCMMarkRangeEndsAtOrBefore);

	for (; it != merged.end() && it->fFrom < runEnd; ++it)
	{
		const TextIndex from = (it->fFrom > runStart) ? it->fFrom : runStart;
		const TextIndex to   = (it->fTo   < runEnd)   ? it->fTo   : runEnd;
		// ⚠The caret flag travels with the piece. A caret clipped by a run boundary keeps its flag,
		//   which is right: the bar belongs at the START of its character, and that is the end the
		//   run containing it sees.
		if (from < to)
			out.push_back(KESCMMarkRange(from - runStart, to - runStart, it->fCaret));
	}
}

/** Does any of a merged list fall inside [runStart, runEnd)? The question GetCouldDraw asks, and
	it wants an answer without building a list. */
inline bool16 KESCMMarkRangesTouchRun(const KESCMMarkRangeList& merged,
									  TextIndex runStart, TextIndex runEnd)
{
	if (runEnd <= runStart || merged.empty())
		return kFalse;

	KESCMMarkRangeList::const_iterator it =
		std::lower_bound(merged.begin(), merged.end(), runStart, KESCMMarkRangeEndsAtOrBefore);

	return (it != merged.end() && it->fFrom < runEnd) ? kTrue : kFalse;
}

#endif // __KESCMStoryMarkRanges_h__

// End, KESCMStoryMarkRanges.h.
