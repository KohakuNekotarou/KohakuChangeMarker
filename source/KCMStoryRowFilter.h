//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Which rows belong in the Story Edits list: did this story's CONTENT change, or only the way
//  it is set?
//
//  The change counters answer "this story is not the same as it was" and nothing finer, so a
//  story whose only edit was a font, a colour, a style or a table stroke would get a row of
//  its own. The reader is looking for what the text now SAYS, and those rows are noise in front
//  of it.
//
//  @warning **WHAT COUNTS AS CONTENT IS SETTLED ELSEWHERE:** the words, and every mark written over them. This file
//    must not grow a list of its own: it asks whether the diff found ANY child, and what the diff looks for is
//    KCMStoryDiffRun's business (memory one-question-one-place).
//
//  **THE ROW CARRIES HOW MUCH IS KNOWN ABOUT ITS STORY** (fTextCompared), so the rule below reads that and never asks
//  KCMGetCompareMode(). Only the Story mode keeps these rows; the Before/After report builds its Story table from the
//  same rows.
//
//  **IT IS A FREE FUNCTION TAKING THREE PLAIN NUMBERS SO THAT IT CAN BE MEASURED.** A row is full of PMStrings and UIDs
//  and cannot leave InDesign; these three fields can, and they are the whole of the decision. work/kescm-rowfilter-test
//  builds this header and runs the table of cases.
//
//========================================================================================

#ifndef __KCMStoryRowFilter_h__
#define __KCMStoryRowFilter_h__

// The TYPES ONLY: KCMStoryStamp.h also declares the free functions that READ a document's counters, and the offline
// test could not build through it.
#include "KCMStoryKinds.h"	// KCMStoryChangeKind, kKCMStoryKindUnpaired

/** Does this row's story differ in its CONTENT -- the words, or the ruby written over them --
	rather than only in how it is set?

	@param kinds the row's fKinds: which change counters moved, plus Added / Removed.
	@param textCompared kTrue when the two versions' text was actually put side by side for this
		row (KCMStoryRow::fTextCompared).
	@param changeCount how many differences the diff attached to the row. Meaningless unless
		textCompared is kTrue -- nobody looked, so nothing was found either way.
	@return kTrue to keep the row.

	The four answers, in the order they are decided:

	**UNPAIRED ROWS ARE ALWAYS KEPT.** An added or a removed story is the largest content change
	  there is -- a whole story appeared or went away -- and there is nothing to diff it against,
	  so the two tests below would both answer no for the wrong reason. @warning this is also the
	  one place where "no children" is not evidence of anything: those rows never get children.

	**A ROW THAT WAS DIFFED IS KEPT WHEN THE DIFF FOUND SOMETHING.** Any kind of child will do --
	  a text edit or a ruby -- because both are things the reader is looking for. Empty means the
	  words and the readings agree and only the
	  setting moved: that is the row being dropped.
	  @warning not "the story is unchanged". The counters moved or the row would never have been
	    built (KCMStoryStamp.h). What is being said is that the difference is not one of these.

	**A ROW PAIRED BY PLACE THAT WAS NOT DIFFED IS KEPT** (kKCMStoryKindByPosition). Its Added
	  and its Removed went into it, so dropping it would drop both - and it carries no counter bit to fall
	  back on (the two stories' counters belong to two histories), so the next answer would always drop it.
	  It stands the way an unpaired row stands: the spec map's "a story that could not be compared keeps its
	  row" (ST-35). ⚠A row of this kind that WAS diffed follows the answer above, like any row.

	**A ROW THAT WAS NOT DIFFED FALLS BACK ON THE TEXT COUNTER**, which is the best that can be
	  known about it: a row whose story could not be READ at all (KCMStoryDiffRun's ReadStory - nothing
	  else leaves a row undiffed: KCMTextDiff::Diff has no limit to refuse by). "The Text counter
	  moved" is real evidence that characters were inserted, removed or replaced, and it is evidence
	  nobody else is offering.
	  @warning Attr or Other alone is dropped here, which costs the ruby of a story that could not be read.
*/
inline bool16 KCMStoryRowHasContentChange(uint32 kinds, bool16 textCompared, int32 changeCount)
{
	// A row an import could not fill is kept as well: its story's counter may not have
	//   moved at all - nothing went in - and the row is there to say so, not to report a difference.
	if ((kinds & (kKCMStoryKindUnpaired | kKCMStoryKindRefused)) != 0)
		return kTrue;

	if (textCompared)
		return (changeCount > 0) ? kTrue : kFalse;

	// Paired by place and not read: kept - its two sides went into it (the header says why).
	if ((kinds & kKCMStoryKindByPosition) != 0)
		return kTrue;

	return ((kinds & kKCMStoryKindText) != 0) ? kTrue : kFalse;
}

#endif // __KCMStoryRowFilter_h__

// End, KCMStoryRowFilter.h.
