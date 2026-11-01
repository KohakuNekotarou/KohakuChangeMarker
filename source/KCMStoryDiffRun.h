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
//  **IT ONLY LOOKS AT STORIES THE COUNTERS ALREADY FLAGGED** (or the words' print, where the counters
//  agree - KCMStoryStamp::fTextPrint). KCMStoryList holds those and nothing else, and this walks that
//  list rather than the document. That is what makes the mode usable on a real book chapter: diffing
//  every story would cost time proportional to the document, and reading the counters costs nothing
//  (KCMStoryStamp.h, "READING COUNTERS COMPOSES NOTHING"). A document with three edited stories does
//  three comparisons however long it is.
//
//  @warning **A STORY THAT CANNOT BE COMPARED KEEPS ITS ROW AND LOSES ITS DETAIL.** Two things end
//   that way -- the story has no partner in the older document (it was added), or it cannot be
//   read. In either the row still appears in the panel, with no children. A story that the
//   counters say changed must never vanish because the detail could not be worked out.
//
//========================================================================================

#ifndef __KCMStoryDiffRun_h__
#define __KCMStoryDiffRun_h__

class IDataBase;
struct KCMStoryRow;
struct KCMStoryChange;

/** Filling in the Story Edits list's children.
	@ingroup KCM
*/
namespace KCMStoryDiffRun
{
	/** Compare every story KCMStoryList holds, and attach what differs to its row.

		Call this AFTER KCMStoryList::Build -- the rows have to exist, and they have to be in
		their final order, because a change names its row by position.

		**IT GUARDS THE MODIFIED FLAG ITSELF.** Reading a story can compose (asking for text that has
		never been laid out lays it out), and composing sets the modified flag on a document this feature
		only reads. A guard inside a guard is harmless: each one restores the value it found.

		@param targetDB the newer document. nil does nothing.
		@param sourceDB the older document. nil does nothing.
		@param outCancelled OUT kTrue when the person pressed Cancel on the progress bar, which
			appears after kKCMProgressBarDelayMs (KCMProgressBar.h). The rows read so far keep
			their changes and the rest are left as Build made them; the caller decides what to do
			with that (KCMRebuildStoryEdits hands it up, and the comparison's callers go back to
			Stop). nil when the caller does not care.
		@return how many differences were attached in total, across every row. 0 is a real
			answer: the counters can flag a story whose text is identical, because they also
			move for formatting and for things attached to the story.
	*/
	int32 Run(IDataBase* targetDB, IDataBase* sourceDB, bool16* outCancelled = nil);

	/** Compare ONE row's story again, and replace what is attached to it.

		"Refresh Story Comparison" on the row's right-click menu. The reader edits the newer
		document with the panel open, and the row keeps showing what differed when the comparison
		ran -- this is how they bring one row up to date without re-running the whole comparison.

		**AN EMPTY RESULT IS WRITTEN.** A row whose text has been brought back into agreement LOSES its
		children: leaving the old differences under a row that was explicitly refreshed would be showing
		the reader something untrue about the document in front of them.

		The guard is the same one Run takes, and for the same reason; this one is the only guard on
		this path.

		**THE ROW IS RE-READ TOO** -- its opening words, the frame a click scrolls to, and that
		frame's page (KCMStoryList::RefreshRowFromDocument): the row quotes the document, so a refresh
		that re-read only the CHILDREN would leave it quoting a sentence the reader has just rewritten.

		@warning what is NOT re-read is fKinds, deliberately: the kinds come from the two documents'
		change COUNTERS and say why the comparison
		 listed the row (KCMStoryStamp.h); what differs now is the diff's to say, and re-reading them
		 costs a walk of both documents. Nor is the row's place in the list: see RefreshRowFromDocument
		 for why one row's sort key must not be updated while the sequence keeps its old order.

		Either way the row STAYS. A row that vanished because its text was repaired would be
		claiming the story is untouched when the counters say otherwise. Refreshing means "what
		differs NOW", not "does this row still belong here".

		@param targetDB the newer document. nil answers -1.
		@param sourceDB the older document. nil answers -1.
		@param rowIndex which row, in the order the list is in now.
		@return how many differences are attached to the row after this, or **-1** when the story
			could not be compared at all -- out of range, an added story (nothing on the older
			side to compare against), or a story that cannot be read. The row keeps its place in every
			one of those cases; only its detail is cleared.
	*/
	int32 RunOne(IDataBase* targetDB, IDataBase* sourceDB, int32 rowIndex);

	/** **THE COUNTER A CHANGE OF THIS KIND IS MEASURED BY** - the one question "which
		instrument?", asked in one place.

		A "=" record stands while the story has got at least as far as the counter recorded when
		it was written, and an undo takes that counter back on its own - which is the whole of KCM's
		undo handling for the Story list. That works only while the counter can SEE the write.

		⚠**AND THE TEXT COUNTER CANNOT SEE AN ATTRIBUTE.** Measured on a real document, writing a ruby and
		 undoing it: GetTextChangeCount answered **4, 4 and 4** across before, after and undone, while
		GetChangeCount answered **13, 14, 13**.

		**THE AGGREGATE IS THE RIGHT ONE FOR ATTRIBUTES**, and KCMStoryStamp.h's essay says why
		  in more detail than belongs here: which sub-counter an edit lands on cannot be predicted
		  from the headers, and the aggregate is the only reading that cannot be wrong-footed.
		⚠**WORDS KEEP THE TEXT COUNTER**, deliberately: the ">=" behaviour of the text path was
		 measured on the application and nothing here is trying to re-decide it.

		@param kind kKCMStoryAttrNone for a change to the WORDS, else the attribute's kind.
		@return 0 when the story cannot be opened. */
	uint32 CountForKind(const UIDRef& story, int32 kind);

	/** The aggregate counter (ITextModel::GetChangeCount), for a question about the story as a whole rather than about one
		change: has it moved since it was compared, did it come back (KCMCounterHistory). 0 when the story cannot be opened. */
	uint32 CountAllChanges(const UIDRef& story);

}

#endif // __KCMStoryDiffRun_h__

// End, KCMStoryDiffRun.h.
