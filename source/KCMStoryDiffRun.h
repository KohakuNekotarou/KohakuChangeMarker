//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The Story Changes comparison: what actually differs inside each story that changed.
//
//  KCM's original comparison rasterises pages and compares pixels, which can say "this page
//  looks different" and nothing more. The Story Edits list added "and this story is one of the
//  ones that changed", by matching ITextModel's change counters (KCMStoryStamp). This file
//  answers the next question - WHERE, and what the words were - by diffing the text itself.
//
//  *** IT ONLY LOOKS AT STORIES THE COUNTERS ALREADY FLAGGED. *** KCMStoryList holds those and
//  nothing else, and this walks that list rather than the document. That is what makes the mode
//  usable on a real book chapter: exporting and diffing every story would cost time proportional
//  to the document, and reading the counters costs nothing (KCMStoryStamp.h, "READING COUNTERS
//  COMPOSES NOTHING"). A document with three edited stories does three comparisons however long
//  it is.
//
//  ⚠A STORY THAT CANNOT BE COMPARED KEEPS ITS ROW AND LOSES ITS DETAIL. Three things end that
//  way - the story has no partner in the older document (it was added), the edit distance runs
//  past KCMTextDiff's limit, or the length check below fails. In every one of them the row
//  still appears in the panel, with no children. A story that the counters say changed must
//  never vanish because the detail could not be worked out.
//
//========================================================================================

#ifndef __KCMStoryDiffRun_h__
#define __KCMStoryDiffRun_h__

class IDataBase;

/** Filling in the Story Edits list's children.
	@ingroup KCM
*/
namespace KCMStoryDiffRun
{
	/** Compare every story KCMStoryList holds, and attach what differs to its row.

		Call this AFTER KCMStoryList::Build - the rows have to exist, and they have to be in
		their final order, because a change names its row by position.

		★IT GUARDS THE MODIFIED FLAG ITSELF, and it has to - which was measured rather than
		assumed (2026-08-20). This is reached through KCMRebuildStoryEdits, and THAT has two
		callers, only one of which is inside a guard:

		    KCMCore.cpp:823   the full comparison    -> inside KCMDoMarkChangesDoc's guard ✅
		    KCMPeek.cpp:518   Refresh Page Comparison -> NOT guarded ⚠

		The second one sits in KCMRefreshComparisonForSelectedPages, past the end of
		KCMRefreshComparisonCore whose guard (KCMPeek.cpp:263-264) covers only itself. Reading
		the change counters needed no guard either way - counters compose nothing - but exporting
		a snippet can compose, so the story diff would have left that path dirtying documents it
		only read. Guarding here covers both callers, and a guard inside a guard is harmless:
		each one restores the value it found, so the outer one restores the same value.

		@param targetDB the newer document. nil does nothing.
		@param sourceDB the older document. nil does nothing.
		@return how many differences were attached in total, across every row. 0 is a real
			answer: the counters can flag a story whose text is identical, because they also
			move for formatting and for things attached to the story.
	*/
	int32 Run(IDataBase* targetDB, IDataBase* sourceDB);

	/** Compare ONE row's story again, and replace what is attached to it.

		"Refresh Story Comparison" on the row's right-click menu (2026-08-21, user's request:
		"それを使うとそのストーリーだけ比較を更新したい"). The reader edits the newer document with
		the panel open, and the row keeps showing what differed when the comparison ran - this
		is how they bring one row up to date without re-running the whole comparison.

		★IT WRITES AN EMPTY RESULT, WHICH IS THE ONE THING Run DOES NOT. Run leaves a row's
		previous detail alone when the story now compares equal, and it is right to: it is
		filling in a list that was just built, so there is nothing there to preserve or destroy.
		Here there is. A row whose text has been brought back into agreement must LOSE its
		children, because that is the answer the reader asked for - and leaving yesterday's
		differences under a row that was explicitly refreshed would be showing them something
		untrue about the document in front of them.

		★The guard is the same one Run takes, and for the same reason (exporting a snippet can
		compose). A guard inside a guard is harmless; this one is the only guard on this path.

		★THE ROW IS RE-READ TOO - its opening words, the frame a click scrolls to, and that
		frame's page (KCMStoryList::RefreshRowFromDocument). ⚠This was missing from the first
		build and had to be reported before it was noticed (user, 2026-08-21: "親の行のテキストの
		内容が変更されたのに変わっていない"): the row quotes the document, so a refresh that
		re-read only the CHILDREN left the row quoting a sentence the reader had just rewritten -
		one line of the panel showing two different moments.

		⚠What is still NOT re-read is fKinds, and that is deliberate rather than the same
		oversight: the kinds come from the two documents' change COUNTERS, which move forward as a
		story is edited and never come back (KCMStoryStamp.h, "WHY TWO VERSIONS CAN BE MATCHED
		AT ALL"). Re-reading them costs a walk of both documents to produce the answer they
		already gave. ⚠Nor is the row's place in the list: see RefreshRowFromDocument for why one
		row's sort key must not be updated while the sequence keeps its old order.

		Either way the row STAYS. A row that vanished because its text was repaired would be
		claiming the story is untouched when the counters say otherwise. Refreshing means "what
		differs NOW", not "does this row still belong here".

		@param targetDB the newer document. nil answers -1.
		@param sourceDB the older document. nil answers -1.
		@param rowIndex which row, in the order the list is in now.
		@return how many differences are attached to the row after this, or **-1** when the story
			could not be compared at all - out of range, an added story (nothing on the older
			side to compare against), or the diff refused it. The row keeps its place in every
			one of those cases; only its detail is cleared.
	*/
	int32 RunOne(IDataBase* targetDB, IDataBase* sourceDB, int32 rowIndex);
}

#endif // __KCMStoryDiffRun_h__

// End, KCMStoryDiffRun.h.
