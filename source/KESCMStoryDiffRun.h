//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The Story Changes comparison: what actually differs inside each story that changed.
//
//  KESCM's original comparison rasterises pages and compares pixels, which can say "this page
//  looks different" and nothing more. The Story Edits list added "and this story is one of the
//  ones that changed", by matching ITextModel's change counters (KESCMStoryStamp). This file
//  answers the next question - WHERE, and what the words were - by diffing the text itself.
//
//  *** IT ONLY LOOKS AT STORIES THE COUNTERS ALREADY FLAGGED. *** KESCMStoryList holds those and
//  nothing else, and this walks that list rather than the document. That is what makes the mode
//  usable on a real book chapter: exporting and diffing every story would cost time proportional
//  to the document, and reading the counters costs nothing (KESCMStoryStamp.h, "READING COUNTERS
//  COMPOSES NOTHING"). A document with three edited stories does three comparisons however long
//  it is.
//
//  ⚠A STORY THAT CANNOT BE COMPARED KEEPS ITS ROW AND LOSES ITS DETAIL. Three things end that
//  way - the story has no partner in the older document (it was added), the edit distance runs
//  past KESCMTextDiff's limit, or the length check below fails. In every one of them the row
//  still appears in the panel, with no children. A story that the counters say changed must
//  never vanish because the detail could not be worked out.
//
//========================================================================================

#ifndef __KESCMStoryDiffRun_h__
#define __KESCMStoryDiffRun_h__

class IDataBase;

/** Filling in the Story Edits list's children.
	@ingroup KESCM
*/
namespace KESCMStoryDiffRun
{
	/** Compare every story KESCMStoryList holds, and attach what differs to its row.

		Call this AFTER KESCMStoryList::Build - the rows have to exist, and they have to be in
		their final order, because a change names its row by position.

		★NO dirty guard of its own. The comparison path already holds one for both databases
		(KESCMCore.cpp, at the top of the marking run), and a guard inside a guard would be a
		second answer to "who protects the modified flag here".

		@param targetDB the newer document. nil does nothing.
		@param sourceDB the older document. nil does nothing.
		@return how many differences were attached in total, across every row. 0 is a real
			answer: the counters can flag a story whose text is identical, because they also
			move for formatting and for things attached to the story.
	*/
	int32 Run(IDataBase* targetDB, IDataBase* sourceDB);
}

#endif // __KESCMStoryDiffRun_h__

// End, KESCMStoryDiffRun.h.
