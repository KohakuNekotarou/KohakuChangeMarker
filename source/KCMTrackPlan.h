//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  THE TRACK CHANGES MODE'S ARITHMETIC (2026-10-05, design
//  docs/superpowers/specs/2026-10-05-kcm-track-changes-mode-design.md section 3).
//
//  From InDesign's records (copied out by KCMTrackRead) to the rows the list shows: an insertion and the
//  deletion that touches it, by the SAME author, are one replacement (a word typed over, KFC's replace);
//  moved text is a row of its own and never pairs; kApply / kUnknown are left out and counted. And where
//  each change stands in the ORIGINAL - the text with every record taken back, which is what the copy
//  "Compare with Tracked Changes..." saves - so that the Source window can be taken to it.
//
//  ★HEADER-ONLY AND FREE OF THE SDK EXCEPT FOR BaseType.h, WHICH IS WHAT MAKES IT TESTABLE: the test is
//   work\kcm-track-test (a stub BaseType.h, and an oracle that REBUILDS the original text from the records
//   rather than repeating the formula below).
//
//========================================================================================

#ifndef __KCMTrackPlan_h__
#define __KCMTrackPlan_h__

#include "BaseType.h"		// TextIndex, int32, uint64, bool16

#include <algorithm>
#include <vector>

/** What a record is: VOSRedlineChange::kInsert / kDelete; kApply and kUnknown are "other". */
enum KCMTrackRecordKind { kKCMTrackRecInsert = 0, kKCMTrackRecDelete = 1, kKCMTrackRecOther = 2 };

/** One record, as KCMTrackRead copies it out of the iterator. */
struct KCMTrackRecord
{
	int32		fKind;		// KCMTrackRecordKind
	bool16		fMoved;		// an insertion InDesign calls moved text (VOSRedlineChange::GetIsMovedText)
	TextIndex	fAt;		// an insertion's first character; a deletion's anchor
	int32		fLen;		// an insertion's length; for a deletion, its deleted text's length (code points)
	int32		fAuthor;	// an index into the reader's author table - only compared here
	uint64		fTime;		// VOSRedlineChange::GetTimeStamp
	bool16		fHidden;	// stands in hidden conditional text: its positions are in that thread
	KCMTrackRecord() : fKind(kKCMTrackRecOther), fMoved(kFalse), fAt(0), fLen(0), fAuthor(-1), fTime(0), fHidden(kFalse) {}
};

/** What a row is - the sign in its Delta column. Numbers, so that the facade's int32 agrees by definition. */
enum KCMTrackChangeKind { kKCMTrackReplace = 0, kKCMTrackInsert = 1, kKCMTrackDelete = 2, kKCMTrackMove = 3 };

/** One row of the Track list. */
struct KCMTrackPlanned
{
	int32		fKind;		// KCMTrackChangeKind
	int32		fAuthor;
	uint64		fTime;		// the insertion's; the deletion's when there is no insertion
	int32		fInsRec;	// index into the records, -1 when none
	int32		fDelRec;	// index into the records, -1 when none
	TextIndex	fFrom;		// Target: the inserted characters [fFrom, fTo); a deletion alone: fFrom == fTo == its anchor
	TextIndex	fTo;
	TextIndex	fOrigFrom;	// the original: [fOrigFrom, fOrigTo) holds the deleted characters; an insertion alone is a caret
	TextIndex	fOrigTo;
	bool16		fHidden;
	KCMTrackPlanned() : fKind(kKCMTrackInsert), fAuthor(-1), fTime(0), fInsRec(-1), fDelRec(-1),
						fFrom(0), fTo(0), fOrigFrom(0), fOrigTo(0), fHidden(kFalse) {}
};

/** True for a colour InDesign uses for "this author has no colour of their own" - the galley's white. */
inline bool16 KCMTrackColourIsWhite(double r, double g, double b)
{
	return (r >= 0.99 && g >= 0.99 && b >= 0.99) ? kTrue : kFalse;
}

/** Where a change that starts at Target position p begins in the original. Inserted characters before p
	are not there; a deletion anchored before p, or AT p with a lower record index than `ownRec`, has put its
	characters back in front of p. Hidden records stand in another thread and move nothing here. */
inline TextIndex KCMTrackOriginalIndex(const std::vector<KCMTrackRecord>& recs, TextIndex p, int32 ownRec)
{
	int32 shift = 0;
	for (size_t i = 0; i < recs.size(); ++i)
	{
		const KCMTrackRecord& r = recs[i];
		if (r.fHidden || static_cast<int32>(i) == ownRec)
			continue;
		if (r.fKind == kKCMTrackRecInsert)
		{
			const TextIndex end = r.fAt + r.fLen;
			const TextIndex upTo = (end < p) ? end : p;
			if (upTo > r.fAt)
				shift -= (upTo - r.fAt);
		}
		else if (r.fKind == kKCMTrackRecDelete)
		{
			if (r.fAt < p || (r.fAt == p && static_cast<int32>(i) < ownRec))
				shift += r.fLen;
		}
	}
	return p + shift;
}

namespace KCMTrackPlanDetail
{
	inline bool IsBefore(const KCMTrackPlanned& a, const KCMTrackPlanned& b)
	{
		if (a.fHidden != b.fHidden)
			return a.fHidden == kFalse;			// the hidden ones go last: they have no place in the main text
		if (a.fFrom != b.fFrom)
			return a.fFrom < b.fFrom;
		const int32 ar = (a.fInsRec >= 0) ? a.fInsRec : a.fDelRec;
		const int32 br = (b.fInsRec >= 0) ? b.fInsRec : b.fDelRec;
		return ar < br;
	}
}

/** The rows `recs` make, in text order. outOther = records left out (kApply / kUnknown). */
inline void KCMPlanTrackChanges(const std::vector<KCMTrackRecord>& recs, std::vector<KCMTrackPlanned>& out, int32& outOther)
{
	out.clear();
	outOther = 0;
	std::vector<bool> used(recs.size(), false);

	// Pass 1: an insertion (not moved text) takes the deletion that touches it, same author, same thread -
	// the one at its END first (KFC measured a replace that way), else the one at its START.
	for (size_t i = 0; i < recs.size(); ++i)
	{
		const KCMTrackRecord& ins = recs[i];
		if (ins.fKind != kKCMTrackRecInsert || ins.fMoved)
			continue;
		int32 pair = -1;
		for (int pass = 0; pass < 2 && pair < 0; ++pass)
		{
			const TextIndex want = (pass == 0) ? (ins.fAt + ins.fLen) : ins.fAt;
			for (size_t j = 0; j < recs.size(); ++j)
			{
				const KCMTrackRecord& d = recs[j];
				if (used[j] || d.fKind != kKCMTrackRecDelete || d.fAuthor != ins.fAuthor
					|| d.fHidden != ins.fHidden || d.fAt != want)
					continue;
				pair = static_cast<int32>(j);
				break;
			}
		}
		KCMTrackPlanned p;
		p.fAuthor = ins.fAuthor;
		p.fTime = ins.fTime;
		p.fInsRec = static_cast<int32>(i);
		p.fFrom = ins.fAt;
		p.fTo = ins.fAt + ins.fLen;
		p.fHidden = ins.fHidden;
		if (pair >= 0)
		{
			p.fKind = kKCMTrackReplace;
			p.fDelRec = pair;
			used[pair] = true;
		}
		else
			p.fKind = kKCMTrackInsert;
		used[i] = true;
		out.push_back(p);
	}

	// Pass 2: everything left - moved text, lone deletions, and the others (counted, not shown).
	for (size_t i = 0; i < recs.size(); ++i)
	{
		if (used[i])
			continue;
		const KCMTrackRecord& r = recs[i];
		if (r.fKind == kKCMTrackRecOther)
		{
			++outOther;
			continue;
		}
		KCMTrackPlanned p;
		p.fAuthor = r.fAuthor;
		p.fTime = r.fTime;
		p.fHidden = r.fHidden;
		if (r.fKind == kKCMTrackRecInsert)		// moved text
		{
			p.fKind = kKCMTrackMove;
			p.fInsRec = static_cast<int32>(i);
			p.fFrom = r.fAt;
			p.fTo = r.fAt + r.fLen;
		}
		else
		{
			p.fKind = kKCMTrackDelete;
			p.fDelRec = static_cast<int32>(i);
			p.fFrom = r.fAt;
			p.fTo = r.fAt;
		}
		out.push_back(p);
	}

	// Where each stands in the original.
	for (size_t k = 0; k < out.size(); ++k)
	{
		KCMTrackPlanned& p = out[k];
		if (p.fHidden)
		{
			p.fOrigFrom = p.fOrigTo = 0;
			continue;
		}
		const int32 own = (p.fDelRec >= 0) ? p.fDelRec : p.fInsRec;
		p.fOrigFrom = KCMTrackOriginalIndex(recs, p.fFrom, own);
		p.fOrigTo = p.fOrigFrom + ((p.fDelRec >= 0) ? recs[p.fDelRec].fLen : 0);
	}

	std::sort(out.begin(), out.end(), KCMTrackPlanDetail::IsBefore);
}

#endif // __KCMTrackPlan_h__

// End, KCMTrackPlan.h.
