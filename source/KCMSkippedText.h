//========================================================================================
//
//  KCMSkippedText.h
//
//  The stretches of a story's text model that are NOT the story as the page sets it.
//
//========================================================================================

#pragma once
#ifndef __KCMSkippedText_h__
#define __KCMSkippedText_h__

#include <utility>
#include <vector>

class ITextModel;

/** The stretches of a story's text model that are NOT the story as the page sets it: the threads
	InDesign hangs off the body for text deleted under Track Changes (kDeletedTextBoss), hidden
	conditional text and notes - AND everything standing inside them: a footnote whose reference is
	in there, a table whose anchor is in there, a table or a note inside THAT table, and so on down.

	★★★WHY THIS EXISTS (measured 2026-09-24, work/kcm-table-spike/track4.jsx): KCMTextRead has always
	  stepped over the deleted-text thread itself, but a footnote or a table deleted under Track
	  Changes keeps its OWN thread (and is rebuilt under a new UID inside the deleted text), so the
	  readers met it as a live footnote or table with no paragraph to stand in. The export refused the
	  story ("a footnote's reference could not be placed", "1 table(s) became 0") - for any document
	  whose owner deleted a footnote or a table with Track Changes on, not only after an import.
	★★★EVERY READER ASKS THIS ONE OBJECT. The table walks number tables by position in their list
	  (fTableOrdinal, KCMTableRefsOfStory's index), so they have to drop exactly the same tables; one
	  answer asked in seven places is what keeps the ordinals in step ([[one-question-one-place]]).
	⚠AN ENDNOTE IS NOT DROPPED. Its words are a story of their own, and InDesign keeps SETTING them on
	 the page until the deletion of their anchor is accepted (measured 2026-09-24, track5.jsx) - so
	 reading them as they are is reading the page. */
class KCMSkippedText
{
public:
	/** Walks the model once. @return kFalse only when there is no model to walk; a story with
		nothing to skip answers kTrue with nothing in it. */
	bool16 Build(ITextModel* model);

	/** Does this position stand in text the page does not set? */
	bool16 Contains(TextIndex at) const;

	bool16 IsEmpty() const { return fRanges.empty() ? kTrue : kFalse; }

private:
	std::vector<std::pair<TextIndex, TextIndex> > fRanges;	// [start, end)
};

#endif // __KCMSkippedText_h__
