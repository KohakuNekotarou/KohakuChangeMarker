//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The window an attribute change row's "Restore from Source" covers, and what it writes there (2026-09-24,
//  stage 2 B - design section 14 of docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md).
//
//  ★PURE, AND MEASURED OUTSIDE InDesign (work/kcm-storydocx-test): no SDK object, only positions and strings.
//   What needs the SDK - reading the two stories, comparing the characters, the writers - is KCMRestoreAttr.cpp.
//
//  ★★THE ROW'S TWO RANGES ARE THE SEED, AND THE WINDOW GROWS UNTIL NO MARK OF THE KIND IS CUT BY ITS EDGE ON
//   EITHER SIDE. A Target ruby whose base grew past the row, a Source ruby standing right after the row's (the
//   measured shape: Target A[10,12) against Source A[10,11) B[11,14) - restoring A alone would cut B in half, and
//   a group reading over half its characters is a wrong answer nobody asked for), and so on until nothing
//   crosses. ⚠Without the closure the two rows of that shape refuse each other for ever: A's window cuts B,
//   B's cuts A. With it, either row restores the whole stretch and the other reads "no change" afterwards.
//
//========================================================================================

#ifndef __KCMAttrRestorePlan_h__
#define __KCMAttrRestorePlan_h__

#include "BaseType.h"		// int32 / bool16
#include <string>
#include <vector>

/** One mark of one kind, where it stands IN THE DOCUMENT'S COUNT (a TextIndex, absolute). fValue is what
	KCMAttrSpan carries (a reading; "BlackCircle" / "Custom:X"; the characters for a warichu or tate-chu-yoko),
	fGroup for ruby only. */
struct KCMAttrPiece
{
	int32		fStart;
	int32		fEnd;		// exclusive
	std::string	fValue;
	bool16		fGroup;
	KCMAttrPiece() : fStart(0), fEnd(0), fGroup(kFalse) {}
	KCMAttrPiece(int32 s, int32 e, const std::string& v, bool16 g = kFalse)
		: fStart(s), fEnd(e), fValue(v), fGroup(g) {}
};

/** What one restore does: the WINDOW cleared of the kind in the Target, the same window in the Source, and the
	Source's marks inside it moved to Target positions. */
struct KCMAttrRestorePlan
{
	int32	fTargetFrom, fTargetTo;
	int32	fSourceFrom, fSourceTo;		// fSourceTo - fSourceFrom == fTargetTo - fTargetFrom
	std::vector<KCMAttrPiece>	fWrites;	// in TARGET positions
	KCMAttrRestorePlan() : fTargetFrom(0), fTargetTo(0), fSourceFrom(0), fSourceTo(0) {}
};

/** The window a change row's restore has to cover (see the file header for why it grows).
	Both sides are aligned by (tFrom - sFrom), the row's own pairing of the two documents.
	@param targetPieces / sourcePieces every mark of the KIND in each story, in each document's own count.
	@param tFrom, tTo / sFrom, sTo the row's ranges (Change::fTargetStart.. / fSourceStart..).
	@return kFalse when a side is EMPTY (from == to): that is the diff saying the WORDS differ as well
	  (CompareParagraphAttr's textDiffered), and no position over there names these characters. */
inline bool16 KCMPlanAttrRestore(const std::vector<KCMAttrPiece>& targetPieces,
								 const std::vector<KCMAttrPiece>& sourcePieces,
								 int32 tFrom, int32 tTo, int32 sFrom, int32 sTo,
								 KCMAttrRestorePlan& out)
{
	if (tTo <= tFrom || sTo <= sFrom)
		return kFalse;
	const int32 shift = tFrom - sFrom;			// a Source position + shift = the Target position
	int32 a = tFrom;
	int32 b = (tTo - tFrom >= sTo - sFrom) ? tTo : (sTo + shift);
	for (bool16 grew = kTrue; grew; )
	{
		grew = kFalse;
		for (size_t i = 0; i < targetPieces.size(); ++i)
		{
			const KCMAttrPiece& p = targetPieces[i];
			if (p.fStart < b && p.fEnd > a)		// touches the window (an edge-to-edge neighbour does not)
			{
				if (p.fStart < a) { a = p.fStart; grew = kTrue; }
				if (p.fEnd > b)   { b = p.fEnd;   grew = kTrue; }
			}
		}
		for (size_t i = 0; i < sourcePieces.size(); ++i)
		{
			const int32 s = sourcePieces[i].fStart + shift;
			const int32 e = sourcePieces[i].fEnd + shift;
			if (s < b && e > a)
			{
				if (s < a) { a = s; grew = kTrue; }
				if (e > b) { b = e; grew = kTrue; }
			}
		}
	}
	out.fTargetFrom = a;
	out.fTargetTo = b;
	out.fSourceFrom = a - shift;
	out.fSourceTo = b - shift;
	out.fWrites.clear();
	for (size_t i = 0; i < sourcePieces.size(); ++i)
	{
		const int32 s = sourcePieces[i].fStart + shift;
		const int32 e = sourcePieces[i].fEnd + shift;
		if (s >= a && e <= b)
			out.fWrites.push_back(KCMAttrPiece(s, e, sourcePieces[i].fValue, sourcePieces[i].fGroup));
	}
	return kTrue;
}

#endif // __KCMAttrRestorePlan_h__

// End, KCMAttrRestorePlan.h.
