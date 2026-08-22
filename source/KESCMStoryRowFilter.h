//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Which rows belong in the Story Edits list: did this story's CONTENT change, or only the way
//  it is set?
//
//  The change counters answer "this story is not the same as it was" and nothing finer, so until
//  2026-08-22 the list showed a row for a story whose only edit was a font, a colour, a style or
//  a table stroke. The reader is looking for what the text now SAYS (user's call: "属性の変更は
//  無視"), and those rows are noise in front of it.
//
//  ⚠WHAT COUNTS AS CONTENT IS SETTLED ELSEWHERE, AND IT SHRANK ON 2026-08-23: the words, and the
//    ruby written over them ("ストーリーモードの StoryEdit にでるのは、テキストの変更と、ルビだけ
//    で"). KENTEN (圏点) was in that list for one day and is no longer reported at all. ★This file
//    did not have to change for that and must not grow a list of its own: it asks whether the diff
//    found ANY child, and what the diff looks for is KESCMStoryDiffRun's business
//    ([[one-question-one-place]]).
//
//  *** ONE RULE COVERS BOTH MODES, AND THAT IS WHY IT IS WORTH A FILE OF ITS OWN. *** The two
//  comparison modes know different amounts about a story, and the temptation is to write a rule
//  for each and to ask KESCMGetCompareMode() which one to run. That would put the same question
//  in two places ([[one-question-one-place]]) - and it is not even necessary, because the row
//  already carries the answer to "how much do we know about this one": fTextCompared. The rule
//  below reads that, and the mode never comes into it.
//
//  *** WHAT THE PIXEL MODE CANNOT DO, AND THAT IS DELIBERATE. *** Ruby is an ATTRIBUTE, so a story
//  where only the reading changed moves the Attr counter and nothing else - exactly like a story
//  where only the font changed. Telling those two apart takes the text diff, which the pixel mode
//  does not run. Running it there was offered and declined (user, 2026-08-22:
//  "ピクセルモードでは、ルビとけんてんを見つけるのはあきらめましょう、Text の変更のだけ
//  StoryEdit にでるようにで") - so in the pixel mode a ruby-only edit does not produce a row.
//  ⚠A row LOST here is lost from the panel, not from the comparison: the pixel comparison still
//    marks the page, which is what that mode is for.
//
//  *** IT IS A FREE FUNCTION TAKING THREE PLAIN NUMBERS SO THAT IT CAN BE MEASURED. *** A row is
//  full of PMStrings and UIDs and cannot leave InDesign; these three fields can, and they are the
//  whole of the decision. work/kescm-rowfilter-test builds this header against a stub UIDRef.h
//  and runs the table of cases - the same way KESCMStoryMarkRanges.h and KESCMSnippetText.h are
//  checked.
//
//========================================================================================

#ifndef __KESCMStoryRowFilter_h__
#define __KESCMStoryRowFilter_h__

#include "KESCMStoryStamp.h"	// KESCMStoryChangeKind, kKESCMStoryKindUnpaired

/** Does this row's story differ in its CONTENT - the words, or the ruby written over them - rather
	than only in how it is set?

	@param kinds the row's fKinds: which change counters moved, plus Added / Removed.
	@param textCompared kTrue when the two versions' text was actually put side by side for this
		row (KESCMStoryRow::fTextCompared).
	@param changeCount how many differences the diff attached to the row. Meaningless unless
		textCompared is kTrue - nobody looked, so nothing was found either way.
	@return kTrue to keep the row.

	The three answers, in the order they are decided:

	★UNPAIRED ROWS ARE ALWAYS KEPT. An added or a removed story is the largest content change
	  there is - a whole story appeared or went away - and there is nothing to diff it against, so
	  the two tests below would both answer no for the wrong reason. ⚠This is also the one place
	  where "no children" is not evidence of anything: those rows never get children.

	★A ROW THAT WAS DIFFED IS KEPT WHEN THE DIFF FOUND SOMETHING. Any kind of child will do - a
	  text edit or a ruby - because both are things the reader is looking for, and the row's own
	  label already says which it was. Empty means the words and the readings agree and only the
	  setting moved: that is the row being dropped.
	  ⚠NOT "the story is unchanged". The counters moved or the row would never have been built
	    (KESCMStoryStamp.h). What is being said is that the difference is not one of these.

	★A ROW THAT WAS NOT DIFFED FALLS BACK ON THE TEXT COUNTER, which is the best that can be
	  known about it. Two quite different rows arrive here and the same reading serves both: every
	  row in the pixel mode, and a row in the story mode whose diff was refused (the edit distance
	  ran past the limit, or the length check failed). In both, "the Text counter moved" is real
	  evidence that characters were inserted, removed or replaced, and it is evidence nobody else
	  is offering.
	  ⚠Attr or Other alone is dropped here - and in the story mode that costs the ruby of a story
	    too large to diff. That is the same trade the pixel mode makes above, arrived at from the
	    other direction.
*/
inline bool16 KESCMStoryRowHasContentChange(uint32 kinds, bool16 textCompared, int32 changeCount)
{
	if ((kinds & kKESCMStoryKindUnpaired) != 0)
		return kTrue;

	if (textCompared)
		return (changeCount > 0) ? kTrue : kFalse;

	return ((kinds & kKESCMStoryKindText) != 0) ? kTrue : kFalse;
}

#endif // __KESCMStoryRowFilter_h__

// End, KESCMStoryRowFilter.h.
