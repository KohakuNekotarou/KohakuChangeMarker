//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The taken-back records of a Story Edits row - where a new one goes, how the others slide when a write changes
//  the story's length, and how the records and the live changes are shown as ONE list (2026-09-24, stage 2 C -
//  design section 15 of docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md).
//
//  ★PURE, AND MEASURED OUTSIDE InDesign (work/kcm-storydocx-test): positions and kinds only. The records
//   themselves (KCMRejectedRecord, KCMStoryList.h) carry the change and the counters; what is here is the part
//   that has to be right for two records at once, which is where the retired restore was wrong twice
//   (docs/ai-notes/kcm-restore-retired-2026-09-21.md section 3-3: two paragraphs taken out and put back came back
//   as "ba" until the slot rule below existed).
//
//========================================================================================

#ifndef __KCMRejectedOrder_h__
#define __KCMRejectedOrder_h__

#include "BaseType.h"		// int32 / bool16
#include <vector>

/** What the order and the shifting need to know of a change: its place in the Target, what it is, and - for a
	record - whether it is shown as taken back ("=") or as live. */
struct KCMRejectedSpan
{
	int32	fStart, fEnd;
	int32	fWhat, fKind;
	bool16	fShownRejected;
	KCMRejectedSpan() : fStart(0), fEnd(0), fWhat(0), fKind(0), fShownRejected(kFalse) {}
	KCMRejectedSpan(int32 s, int32 e, int32 what = 0, int32 kind = 0, bool16 rejected = kTrue)
		: fStart(s), fEnd(e), fWhat(what), fKind(kind), fShownRejected(rejected) {}
};

/** Where a new record whose live words began at writeAt goes among the records (their starts in list order):
	after every record standing AT OR BEFORE writeAt, before the rest. ★THE RULE IS WHAT TELLS TOP-DOWN FROM
	BOTTOM-UP: two adjacent paragraphs taken back in either order come out in the order they stood, because the
	one taken back second either begins where the first now stands (top-down: after it) or has slid the first's
	record past its own start (bottom-up: before it). */
inline size_t KCMRejectedSlotFor(const std::vector<int32>& starts, int32 writeAt)
{
	size_t slot = 0;
	while (slot < starts.size() && starts[slot] <= writeAt)
		++slot;
	return slot;
}

/** Slide the records from `slot` on by what a write at writeAt removed and put in - by the two lengths, not by
	one delta, so that a caret standing where the removed words began is not pushed under writeAt. */
inline void KCMShiftRejectedFrom(std::vector<KCMRejectedSpan>& spans, size_t slot, int32 writeAt, int32 removed, int32 inserted)
{
	for (size_t i = slot; i < spans.size(); ++i)
	{
		const int32 len = spans[i].fEnd - spans[i].fStart;
		int32 s = spans[i].fStart;
		if (s >= writeAt + removed)
			s = s - removed + inserted;
		else if (s > writeAt)
			s = writeAt + inserted;
		spans[i].fStart = s;
		spans[i].fEnd = s + len;
	}
}

/** One entry of the merged list: a live change (fIsRecord kFalse, an index into the live list) or a record. */
struct KCMMergedRef
{
	bool16	fIsRecord;
	int32	fIndex;
	KCMMergedRef(bool16 r, int32 i) : fIsRecord(r), fIndex(i) {}
};

/** The live changes and the records as ONE list in text order: by start, a record before a live change at the
	same start. ★A live change that is the TWIN of a record shown LIVE (the same what, kind, start and end) is
	HIDDEN - the record stands for it, so that a change put back (an Undo of a reject, or a redo) is not shown
	twice, and the record keeps the place a later reject or redo acts from. */
inline void KCMMergeRejected(const std::vector<KCMRejectedSpan>& live, const std::vector<KCMRejectedSpan>& records,
							 std::vector<KCMMergedRef>& out)
{
	out.clear();
	size_t i = 0, r = 0;
	while (i < live.size() || r < records.size())
	{
		if (r < records.size() && (i >= live.size() || records[r].fStart <= live[i].fStart))
		{
			out.push_back(KCMMergedRef(kTrue, static_cast<int32>(r)));
			++r;
			continue;
		}
		bool16 hidden = kFalse;
		for (size_t k = 0; k < records.size() && !hidden; ++k)
			hidden = (!records[k].fShownRejected && records[k].fWhat == live[i].fWhat && records[k].fKind == live[i].fKind
					  && records[k].fStart == live[i].fStart && records[k].fEnd == live[i].fEnd) ? kTrue : kFalse;
		if (!hidden)
			out.push_back(KCMMergedRef(kFalse, static_cast<int32>(i)));
		++i;
	}
}

#endif // __KCMRejectedOrder_h__

// End, KCMRejectedOrder.h.
