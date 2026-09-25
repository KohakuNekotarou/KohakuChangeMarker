//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  WHICH PARAGRAPH A TAKEN-BACK CHANGE IS ABOUT (2026-09-25): the one question "Redo from Word" asks of the
//  document before it plans anything - in the reading KCMTextRead gives (ReadStory's paragraphs, their document
//  starts and their attributes), which paragraph holds the record's place, which paragraph the change itself is,
//  and that paragraph's number within its place (the body, one cell, one note), which is what a plan names.
//  ★PURE: no SDK type, so work/kcm-storydocx-test checks it with no application in the room.
//
//  ★★WHY IT IS A QUESTION OF ITS OWN (the user's report of 2026-09-25: words typed after a table in Word, taken
//    back, and "Redo from Word" answered "nothing to redo here"). A whole paragraph added or removed is cut WITH
//    THE BREAK BEFORE IT whenever a paragraph of the same place stands before it ("\rNEW" - KCMStoryDiffRun's
//    AddWholeParagraphs, kKCMBreakLeads), so the record's place begins ON the previous paragraph's return. The
//    paragraph holding that position is the one BEFORE the change; the change's own paragraph is the next one in
//    the same place - the Source's paragraph standing again for a deletion, and for an insertion the slot the
//    words go back into, which may be past the place's last paragraph. Measured the same day on every such shape
//    (work/kcm-table-spike/redo-matrix): an addition at the end, in the middle, after a table and inside a cell
//    all answered "nothing to redo", a removal in the middle and at the end "the Source's words are not in this
//    paragraph"; only a place's FIRST paragraph (the break after it, kKCMBreakTrails) came back.
//
//========================================================================================

#ifndef __KCMRedoPlace_h__
#define __KCMRedoPlace_h__

#include "BaseType.h"
#include "KCMParaText.h"		// KCMParaAttrs / ModelOffsetInParagraph / CountCodePoints

#include <string>
#include <vector>

namespace KCMRedoPlace
{

/** Whether two paragraphs of the reading stand in the same place: the body, the same cell, the same note. ★The
	four fields the redo has always numbered paragraphs by (KCMRedoFromWord's WhereParaOf) - one test, so that the
	next paragraph and the numbering cannot disagree about what "the same place" is. */
inline bool16 SamePlace(const KCMParaAttrs& a, const KCMParaAttrs& b)
{
	return (a.fTableOrdinal == b.fTableOrdinal && a.fCellRow == b.fCellRow && a.fCellCol == b.fCellCol
			&& a.fFootnoteOrdinal == b.fFootnoteOrdinal) ? kTrue : kFalse;
}

/** The reading paragraph holding document position `at` - from its first character to just past its return, so a
	caret on a paragraph's return belongs to that paragraph - or -1 when no paragraph does. */
inline int32 ParaAt(const std::vector<std::string>& paras, const std::vector<int32>& starts,
					const std::vector<KCMParaAttrs>& attrs, int32 at)
{
	for (size_t i = 0; i < starts.size() && i < attrs.size() && i < paras.size(); ++i)
	{
		const int32 end = starts[i]
						  + KCMParaText::ModelOffsetInParagraph(attrs[i], KCMParaText::CountCodePoints(paras[i])) + 1;
		if (at >= starts[i] && at < end)
			return static_cast<int32>(i);
	}
	return -1;
}

/** The next reading paragraph after `k` that stands in k's place, or -1 when k is its place's last. */
inline int32 NextInPlace(const std::vector<KCMParaAttrs>& attrs, int32 k)
{
	if (k < 0 || static_cast<size_t>(k) >= attrs.size())
		return -1;
	for (size_t j = static_cast<size_t>(k) + 1; j < attrs.size(); ++j)
		if (SamePlace(attrs[j], attrs[static_cast<size_t>(k)]))
			return static_cast<int32>(j);
	return -1;
}

/** How many paragraphs of k's place stand before k in the reading - k's number within its place. */
inline int32 NumberInPlace(const std::vector<KCMParaAttrs>& attrs, int32 k)
{
	int32 n = 0;
	for (int32 i = 0; i < k && static_cast<size_t>(i) < attrs.size(); ++i)
		if (SamePlace(attrs[static_cast<size_t>(i)], attrs[static_cast<size_t>(k)]))
			++n;
	return n;
}

/** The paragraph a taken-back change is about. */
struct Para
{
	int32 fHold;	///< the reading paragraph holding the record's place - its place (body, cell, note) is the change's
	int32 fOwn;		///< the reading paragraph that IS the change's: fHold, or for a change cut with the break before it
					///<   the next one in the place (-1: an insertion past the place's last paragraph)
	int32 fNumber;	///< the change's paragraph number within its place - what a plan's step names (N's numbering)
	Para() : fHold(-1), fOwn(-1), fNumber(-1) {}
};

/** Where the change of a record standing at `at` is. `afterBreak`: the change is a whole paragraph cut with the break
	before it (KCMStoryChange fWholeParagraph and fBreakAt == kKCMBreakLeads) - its paragraph is the one after the
	paragraph holding `at`. kFalse when no paragraph holds `at`. */
inline bool16 Of(const std::vector<std::string>& paras, const std::vector<int32>& starts,
				 const std::vector<KCMParaAttrs>& attrs, int32 at, bool16 afterBreak, Para& out)
{
	out = Para();
	const int32 hold = ParaAt(paras, starts, attrs, at);
	if (hold < 0)
		return kFalse;
	out.fHold = hold;
	out.fNumber = NumberInPlace(attrs, hold) + (afterBreak ? 1 : 0);
	out.fOwn = afterBreak ? NextInPlace(attrs, hold) : hold;
	return kTrue;
}

}	// namespace KCMRedoPlace

#endif // __KCMRedoPlace_h__

// End, KCMRedoPlace.h.
