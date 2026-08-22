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

	KESCMMarkRange() : fFrom(0), fTo(0) {}
	KESCMMarkRange(TextIndex from, TextIndex to) : fFrom(from), fTo(to) {}
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
	caller that knows it is looking at a deletion (KESCMStoryPressMarks), not to a list of numbers.

	@param ranges [in,out] rewritten in place: sorted, non-empty, non-overlapping.
*/
inline void KESCMMergeMarkRanges(KESCMMarkRangeList& ranges)
{
	KESCMMarkRangeList kept;
	kept.reserve(ranges.size());
	for (KESCMMarkRangeList::const_iterator it = ranges.begin(); it != ranges.end(); ++it)
	{
		if (it->fFrom < it->fTo)
			kept.push_back(*it);
	}

	std::sort(kept.begin(), kept.end(), KESCMMarkRangeIsBefore);

	KESCMMarkRangeList merged;
	merged.reserve(kept.size());
	for (KESCMMarkRangeList::const_iterator it = kept.begin(); it != kept.end(); ++it)
	{
		// ">" and not ">=": a range that STARTS where the last one ended touches it, and touching
		// ranges are fused (see the header note).
		if (merged.empty() || it->fFrom > merged.back().fTo)
			merged.push_back(*it);
		else if (it->fTo > merged.back().fTo)
			merged.back().fTo = it->fTo;
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
		if (from < to)
			out.push_back(KESCMMarkRange(from - runStart, to - runStart));
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
