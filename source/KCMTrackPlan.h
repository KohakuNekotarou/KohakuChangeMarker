//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  THE TRACKED CHANGES LISTING'S ARITHMETIC (design
//  docs/superpowers/specs/2026-10-05-kcm-track-changes-mode-design.md section 3).
//
//  From InDesign's records (copied out by KCMTrackRead) to the rows the list shows: an insertion and the
//  deletion that touches it, by the SAME author, are one replacement (a word typed over, KFC's replace);
//  moved text is a row of its own and never pairs; kApply / kUnknown are left out and counted.
//
//  HEADER-ONLY AND FREE OF THE SDK EXCEPT FOR BaseType.h, WHICH IS WHAT MAKES IT TESTABLE: the test is
//   work\kcm-track-test (a stub BaseType.h).
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
	KCMTrackRecordKind	fKind;
	bool16		fMoved;		// an insertion InDesign calls moved text (VOSRedlineChange::GetIsMovedText)
	TextIndex	fAt;		// an insertion's first character; a deletion's anchor
	int32		fLen;		// an insertion's length
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
	KCMTrackChangeKind	fKind;
	int32		fAuthor;
	uint64		fTime;		// the insertion's; the deletion's when there is no insertion
	int32		fInsRec;	// index into the records, -1 when none
	int32		fDelRec;	// index into the records, -1 when none
	TextIndex	fFrom;		// Target: the inserted characters [fFrom, fTo); a deletion alone: fFrom == fTo == its anchor
	TextIndex	fTo;
	bool16		fHidden;
	KCMTrackPlanned() : fKind(kKCMTrackInsert), fAuthor(-1), fTime(0), fInsRec(-1), fDelRec(-1),
						fFrom(0), fTo(0), fHidden(kFalse) {}
};

/** True for the colour InDesign paints a record on that has no colour of its own - the galley's white. */
inline bool16 KCMTrackColourIsWhite(double r, double g, double b)
{
	return (r >= 0.99 && g >= 0.99 && b >= 0.99) ? kTrue : kFalse;
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

	std::sort(out.begin(), out.end(), KCMTrackPlanDetail::IsBefore);
}

/** A MOVE'S TWO HALVES (measured: Reject Change on the moved words alone left the story without them,
	"one threetwo " -> "one three", until the deletion where they stood was rejected too). InDesign records a move as the moved
	text's insertion and a deletion of the same text where it stood, one stamp and one author (the probe's U2,
	kcm-track-probe-2026-10-05.md) - two rows, "»" and "-". outPartner gets, for each row, the row that is its other half, or -1.
	Each deletion is one move's at most.
	@param sameWords sameWords(insRec, delRec): are the words these two records (indices into recs) stand for the same? */
template <typename SameWords>
inline void KCMPairMoves(const std::vector<KCMTrackRecord>& recs, const std::vector<KCMTrackPlanned>& planned,
						 SameWords sameWords, std::vector<int32>& outPartner)
{
	outPartner.assign(planned.size(), -1);
	for (size_t m = 0; m < planned.size(); ++m)
	{
		if (planned[m].fKind != kKCMTrackMove || planned[m].fInsRec < 0)
			continue;
		const KCMTrackRecord& ins = recs[static_cast<size_t>(planned[m].fInsRec)];
		for (size_t d = 0; d < planned.size(); ++d)
		{
			if (planned[d].fKind != kKCMTrackDelete || planned[d].fDelRec < 0 || outPartner[d] >= 0)
				continue;
			const KCMTrackRecord& del = recs[static_cast<size_t>(planned[d].fDelRec)];
			if (del.fAuthor != ins.fAuthor || del.fTime != ins.fTime || del.fHidden != ins.fHidden
				|| !sameWords(planned[m].fInsRec, planned[d].fDelRec))
				continue;
			outPartner[m] = static_cast<int32>(d);
			outPartner[d] = static_cast<int32>(m);
			break;
		}
	}
}

#endif // __KCMTrackPlan_h__

// End, KCMTrackPlan.h.
