//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Character ranges to light up, kept in the one shape the marker can draw straight from:
//  sorted, and with nothing overlapping anything else.
//
//  **WHY MERGE.** It keeps the drawing cheap (one fill per stretch, not one per edit), and the binary searches below
//  assume a sorted, non-overlapping list. **Do not stop doing it.** (It is not for the look: the mark is an opaque
//  coloured wash, and an overlap would only paint the same colour twice. KCMStoryMarker.h says why it is not an
//  inversion, where an overlap would leave a hole.)
//
//  @warning touching ranges of one colour are merged too ([0,5) + [5,9) -> [0,9)). They do not overlap, so
//   nothing would be lost by leaving them apart -- but two adjacent rectangles are two rectfill
//   calls and one seam, and a press over a story with a few hundred small edits makes plenty of
//   both.
//
//  **HEADER-ONLY AND FREE OF THE SDK EXCEPT FOR TextIndex AND uint8, WHICH IS WHAT MAKES IT TESTABLE.**
//  The test outside InDesign is work\kescm-markranges-test (it stubs BaseType.h and includes
//  this file as it stands -- it is not a copy that can drift).
//
//========================================================================================

#ifndef __KCMStoryMarkRanges_h__
#define __KCMStoryMarkRanges_h__

#include "BaseType.h"		// TextIndex, bool16

#include <algorithm>
#include <vector>

/** The colour a range is painted in.

	A colour of its own is the Track mode's: each change in the colour InDesign paints its record on, by default its
	AUTHOR's. None of its own = the panel's Mark colour, which is every range of the other modes. Ranges are fused only
	with ranges of the same colour (KCMMergeMarkRanges). */
struct KCMMarkColour
{
	bool16	fHasOwn;		// kFalse = the panel's Mark colour, and the other three say nothing
	uint8	fR, fG, fB;

	KCMMarkColour() : fHasOwn(kFalse), fR(0), fG(0), fB(0) {}
	KCMMarkColour(uint8 r, uint8 g, uint8 b) : fHasOwn(kTrue), fR(r), fG(g), fB(b) {}

	/** Do two ranges paint the same colour? Two "Mark colour" ranges do; a colour of its own only matches itself. */
	bool operator==(const KCMMarkColour& other) const
	{
		if (!fHasOwn || !other.fHasOwn)
			return !fHasOwn && !other.fHasOwn;
		return fR == other.fR && fG == other.fG && fB == other.fB;
	}
};

/** One stretch of characters to light up, in one story. */
struct KCMMarkRange
{
	TextIndex	fFrom;		// first character
	TextIndex	fTo;		// one PAST the last -- an END, not a length, as RangeData::End() is against its Length()

	/** What is drawn for those characters. */
	enum Shape
	{
		/** The characters are washed. */
		kStretch,

		/** **A CARET, NOT A STRETCH.** A DELETION has no width on the side it was deleted from -- there is no
			character there to mark -- and an insertion has none on the older side. Do not widen it to one character:
			that lights up whatever closed up over the gap, a different character saying "I am the edit". Deleting a
			whole paragraph would light the first character of the NEXT one, and deleting the end of a story its final
			carriage return, which draws nothing at all.
			A caret occupies [fFrom, fFrom+1) so that it sorts, merges and intersects exactly like any
			  other range -- but the drawing side gives it a thin bar at the START of that character
			  instead of washing the whole of it (KCMStoryMarker's GetMarkBoxes). Nothing else has to know. */
		kCaretBefore,

		/** **A CARET AT THE END OF ITS CHARACTER RATHER THAN THE START**, for a gap with nothing drawn behind it. What
			follows the very END of a story is its final carriage return, which draws nothing, and a range standing on
			it reaches no wax run at all - no bar, and a deletion with no place. Such a caret is carried as
			[last, last+1) over the LAST VISIBLE character, and the drawing side stands the bar after it.
			KCMStoryMarker::CaretAt says where else this happens (in front of a table). */
		kCaretAfter
	};
	Shape		fShape;

	KCMMarkColour	fColour;

	KCMMarkRange(TextIndex from, TextIndex to, Shape shape = kStretch) : fFrom(from), fTo(to), fShape(shape) {}

	bool16 IsCaret() const { return (fShape != kStretch) ? kTrue : kFalse; }

	/** This range in this colour (the Track mode's; a KCMMarkColour with none of its own is the Mark colour). */
	KCMMarkRange WithColour(const KCMMarkColour& colour) const
	{
		KCMMarkRange c(*this);
		c.fColour = colour;
		return c;
	}

	/** The caret standing in front of character `at`. */
	static KCMMarkRange Caret(TextIndex at) { return KCMMarkRange(at, at + 1, kCaretBefore); }

	/** The caret standing AFTER character `at - 1` - for a gap at `at` that nothing visible
		follows (the end of the story, the front of a table). @warning `at` must be > 0; the caller checks. */
	static KCMMarkRange CaretAfter(TextIndex at) { return KCMMarkRange(at - 1, at, kCaretAfter); }
};

typedef std::vector<KCMMarkRange> KCMMarkRangeList;

/** Reading order; equal starts by their end, so that a merge of stretches is deterministic.
	@warning a caret before character i and a caret after it compare equal (the same range, another shape), and the
	 merge keeps one of them. The diff does not ask for both: it makes two deletions around one character into one
	 replace (measured). */
inline bool KCMMarkRangeIsBefore(const KCMMarkRange& a, const KCMMarkRange& b)
{
	return (a.fFrom != b.fFrom) ? (a.fFrom < b.fFrom) : (a.fTo < b.fTo);
}

/** For the binary search below: is this range finished by the time v starts? */
inline bool KCMMarkRangeEndsAtOrBefore(const KCMMarkRange& r, TextIndex v)
{
	return r.fTo <= v;
}

/** Sort, drop the empty ones, and fuse everything that overlaps or touches.

	**EMPTY RANGES ARE DROPPED, NOT WIDENED.** A deletion has no width on the side it was deleted
	from, and what the reader should see there is a decision for the caller that knows it is looking at a deletion, not
	for a list of numbers. That caller's answer is a caret (KCMStoryMarker::CaretAt) -- see below for why a caret is
	carried through here rather than being fused away.

	**CARETS ARE KEPT APART FROM THE FUSING**, and there are two reasons:
	  (1) fusing would lose the shape -- a caret swallowed into a neighbouring stretch would come
	    out the other side as an ordinary marked character, which is the very thing it replaced;
	  (2) a caret that sits INSIDE a stretch is dropped, because that place is already lit.
	    With the wash it would be a bar over ground already the same colour: invisible.
	  What comes out is still sorted and still non-overlapping, so the binary searches below are
	    unaffected.

	@param ranges [in,out] rewritten in place: sorted, non-empty, non-overlapping.
*/
inline void KCMMergeMarkRanges(KCMMarkRangeList& ranges)
{
	KCMMarkRangeList kept, carets;
	kept.reserve(ranges.size());
	for (KCMMarkRangeList::const_iterator it = ranges.begin(); it != ranges.end(); ++it)
	{
		if (it->fFrom >= it->fTo)
			continue;
		if (it->IsCaret())
			carets.push_back(*it);
		else
			kept.push_back(*it);
	}

	std::sort(kept.begin(), kept.end(), KCMMarkRangeIsBefore);

	KCMMarkRangeList merged;
	merged.reserve(kept.size() + carets.size());
	for (KCMMarkRangeList::const_iterator it = kept.begin(); it != kept.end(); ++it)
	{
		// ">" and not ">=": a range that STARTS where the last one ended touches it, and touching
		// ranges OF ONE COLOUR are fused (see the header note).
		if (merged.empty() || it->fFrom > merged.back().fTo)
		{
			merged.push_back(*it);
			continue;
		}
		if (it->fColour == merged.back().fColour)
		{
			if (it->fTo > merged.back().fTo)
				merged.back().fTo = it->fTo;
			continue;
		}
		// TWO COLOURS MEET: the earlier one keeps its characters, and the later one is cut
		//   to start where the earlier ends - the list stays sorted and non-overlapping, which the binary
		//   searches below rely on. A later range wholly inside the earlier is dropped.
		if (it->fTo > merged.back().fTo)
		{
			KCMMarkRange rest(*it);
			rest.fFrom = merged.back().fTo;
			merged.push_back(rest);
		}
	}

	if (!carets.empty())
	{
		const size_t stretchCount = merged.size();
		std::sort(carets.begin(), carets.end(), KCMMarkRangeIsBefore);

		TextIndex lastCaret = -1;
		size_t stretch = 0;
		for (KCMMarkRangeList::const_iterator c = carets.begin(); c != carets.end(); ++c)
		{
			if (c->fFrom == lastCaret)
				continue;					// the same place asked for twice

			// Is this place already lit by a stretch? Only the fused stretches are searched (the
			// carets added so far are past `stretchCount` and are not sorted against them yet).
			// **ONE WALK OVER BOTH LISTS, NOT ONE SCAN PER CARET.** Both are sorted, so the stretch
			//   a caret has to be tested against is never before the one the previous caret stopped
			//   at: `stretch` is carried across the loop instead of starting at 0 each time. A press
			//   over a story with a few thousand edits makes both of these lists long.
			// @warning it is an INDEX and not an iterator on purpose -- the push_back below can
			//   reallocate `merged`, and only the stretches before stretchCount are ever read here.
			while (stretch < stretchCount && merged[stretch].fTo <= c->fFrom)
				++stretch;
			if (stretch < stretchCount && merged[stretch].fFrom <= c->fFrom)
				continue;					// inside a stretch: this place is already lit

			merged.push_back(*c);
			lastCaret = c->fFrom;
		}

		std::sort(merged.begin(), merged.end(), KCMMarkRangeIsBefore);

		// **NOTHING IS CLIPPED AFTERWARDS, AND NOTHING NEEDS TO BE.** A caret occupies exactly one
		//   character, [i, i+1). For it to overlap a stretch, that stretch would have to contain i --
		//   and every such caret was just dropped by the test above. A stretch beginning at i+1 only
		//   TOUCHES it, which this list allows between a caret and its neighbour (they are not fused,
		//   deliberately: fusing would lose the shape).
		//   @warning a clipping pass could never fire here, and the one thing it could do is turn [i, i+1) into an
		//     empty range that the intersect below drops.
	}

	ranges.swap(merged);
}

/** The part of a merged list that falls inside [runStart, runEnd), expressed as offsets into
	that run - which is the form the wax asks its questions in (a run reports its own characters
	from 0).

	**BINARY SEARCH, BECAUSE THIS IS THE PER-RUN INNER LOOP.** A press marks every edit in every
	story, so a long story can hold thousands of ranges while a wax run holds a handful of
	characters; walking the list for each run would make the cost of drawing a page grow with the
	number of edits in the document rather than with what is on the page.

	@param merged must have been through KCMMergeMarkRanges - sorted and non-overlapping.
	@param runStart the run's first character, in story coordinates.
	@param runEnd one past its last.
	@param out [out] cleared, then filled with the overlaps as run-relative offsets.
*/
inline void KCMIntersectMarkRanges(const KCMMarkRangeList& merged,
									 TextIndex runStart, TextIndex runEnd,
									 KCMMarkRangeList& out)
{
	out.clear();

	KCMMarkRangeList::const_iterator it =
		std::lower_bound(merged.begin(), merged.end(), runStart, KCMMarkRangeEndsAtOrBefore);

	for (; it != merged.end() && it->fFrom < runEnd; ++it)
	{
		const TextIndex from = (it->fFrom > runStart) ? it->fFrom : runStart;
		const TextIndex to   = (it->fTo   < runEnd)   ? it->fTo   : runEnd;
		// @warning a caret clipped by a run boundary stays a caret.
		if (from < to)
		{
			KCMMarkRange piece(*it);		// the shape AND the colour travel with the piece
			piece.fFrom = from - runStart;
			piece.fTo = to - runStart;
			out.push_back(piece);
		}
	}
}

/** Does any of a merged list fall inside [runStart, runEnd)? The question GetCouldDraw asks, and
	it wants an answer without building a list. */
inline bool16 KCMMarkRangesTouchRun(const KCMMarkRangeList& merged,
									  TextIndex runStart, TextIndex runEnd)
{
	if (runEnd <= runStart)
		return kFalse;

	KCMMarkRangeList::const_iterator it =
		std::lower_bound(merged.begin(), merged.end(), runStart, KCMMarkRangeEndsAtOrBefore);

	return (it != merged.end() && it->fFrom < runEnd) ? kTrue : kFalse;
}

#endif // __KCMStoryMarkRanges_h__

// End, KCMStoryMarkRanges.h.
