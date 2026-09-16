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
//  **IT ONLY LOOKS AT STORIES THE COUNTERS ALREADY FLAGGED.** KCMStoryList holds those and
//  nothing else, and this walks that list rather than the document. That is what makes the mode
//  usable on a real book chapter: exporting and diffing every story would cost time proportional
//  to the document, and reading the counters costs nothing (KCMStoryStamp.h, "READING COUNTERS
//  COMPOSES NOTHING"). A document with three edited stories does three comparisons however long
//  it is.
//
//  @warning **A STORY THAT CANNOT BE COMPARED KEEPS ITS ROW AND LOSES ITS DETAIL.** Three things
//   end that way -- the story has no partner in the older document (it was added), the edit
//   distance runs past KCMTextDiff's limit, or the length check below fails. In every one of
//   them the row still appears in the panel, with no children. A story that the counters say
//   changed must never vanish because the detail could not be worked out.
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

		**IT GUARDS THE MODIFIED FLAG ITSELF, and it has to** -- which was measured rather than
		assumed. This is reached through KCMRebuildStoryEdits, and THAT has two callers, only one
		of which is inside a guard:

		    KCMCore.cpp   the full comparison     -> inside KCMDoMarkChangesDoc, which guards
		    KCMPeek.cpp   Refresh Page Comparison -> KCMRefreshComparisonForSelectedPages, which
		                                             does NOT

		The second one sits past the end of KCMRefreshComparisonCore, whose guard covers only
		itself. Reading the change counters needed no guard either way -- counters compose nothing
		-- but exporting a snippet can compose, so the story diff would have left that path
		dirtying documents it only read. Guarding here covers both callers, and a guard inside a
		guard is harmless: each one restores the value it found, so the outer one restores the
		same value.

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

		**IT WRITES AN EMPTY RESULT, WHICH IS THE ONE THING Run DOES NOT.** Run leaves a row's
		previous detail alone when the story now compares equal, and it is right to: it is
		filling in a list that was just built, so there is nothing there to preserve or destroy.
		Here there is. A row whose text has been brought back into agreement must LOSE its
		children, because that is the answer the reader asked for -- and leaving yesterday's
		differences under a row that was explicitly refreshed would be showing them something
		untrue about the document in front of them.

		The guard is the same one Run takes, and for the same reason (exporting a snippet can
		compose). A guard inside a guard is harmless; this one is the only guard on this path.

		**THE ROW IS RE-READ TOO** -- its opening words, the frame a click scrolls to, and that
		frame's page (KCMStoryList::RefreshRowFromDocument). This was missing from the first build
		and had to be reported before it was noticed: the row quotes the document, so a refresh
		that re-read only the CHILDREN left the row quoting a sentence the reader had just
		rewritten -- one line of the panel showing two different moments.

		@warning what is still NOT re-read is fKinds, and that is deliberate rather than the same
		 oversight: the kinds come from the two documents' change COUNTERS, which move forward as
		 a story is edited and never come back (KCMStoryStamp.h, "WHY TWO VERSIONS CAN BE MATCHED
		 AT ALL"). Re-reading them costs a walk of both documents to produce the answer they
		 already gave. Nor is the row's place in the list: see RefreshRowFromDocument for why one
		 row's sort key must not be updated while the sequence keeps its old order.

		Either way the row STAYS. A row that vanished because its text was repaired would be
		claiming the story is untouched when the counters say otherwise. Refreshing means "what
		differs NOW", not "does this row still belong here".

		@param targetDB the newer document. nil answers -1.
		@param sourceDB the older document. nil answers -1.
		@param rowIndex which row, in the order the list is in now.
		@return how many differences are attached to the row after this, or **-1** when the story
			could not be compared at all -- out of range, an added story (nothing on the older
			side to compare against), or the diff refused it. The row keeps its place in every
			one of those cases; only its detail is cleared.
	*/
	int32 RunOne(IDataBase* targetDB, IDataBase* sourceDB, int32 rowIndex);

	/** The story's text change counter (ITextModel::GetTextChangeCount), 0 when the story cannot
		be opened. Run and RunOne record it on the row as they attach the changes; "Restore Source
		Text" compares it before it writes (KCMStoryRow::fTargetTextCount). */
	uint32 TextCountOf(const UIDRef& story);

	/** ★★★**THE COUNTER A CHANGE OF THIS KIND IS MEASURED BY** - the one question "which
		instrument?", asked in one place (2026-09-16).

		A replaced change is drawn as replaced while the story has got at least as far as the
		counter recorded when it was written, and an undo takes that counter back on its own -
		which is the whole of KCM's undo handling for the Story Edits list. That works only while
		the counter can SEE the write.

		⚠**AND THE TEXT COUNTER CANNOT SEE AN ATTRIBUTE.** Measured 2026-09-16 on a real document,
		 taking in a ruby and undoing it: GetTextChangeCount answered **4, 4 and 4** across before,
		 after and undone, while GetChangeCount answered **13, 14, 13**. A ruby or kenten row
		 therefore stayed drawn as taken-in for ever - the reader's own report ("the document comes
		 back, the panel does not"). It was never a missing signal: the panel was asking an
		 instrument that had not moved.

		★**THE AGGREGATE IS THE RIGHT ONE FOR ATTRIBUTES**, and KCMStoryStamp.h's essay says why
		  in more detail than belongs here: which sub-counter an edit lands on cannot be predicted
		  from the headers, and the aggregate is the only reading that cannot be wrong-footed.
		⚠**WORDS KEEP THE TEXT COUNTER**, deliberately: the ">=" behaviour of the text path was
		 measured on the application on 2026-09-15 and nothing here is trying to re-decide it.

		@param kind kKCMStoryAttrNone for a change to the WORDS, else the attribute's kind.
		@return 0 when the story cannot be opened. */
	uint32 CountForKind(const UIDRef& story, int32 kind);

	/** ★★★**IS THIS REPLACED CHANGE STILL STANDING AS REPLACED?** - the one question, in the one
		place, asked by everything that draws a row AND by everything that writes (2026-09-16).

		It was a static inside KCMFacades while only the DRAWING asked it, and the writing side had
		a test of its own: "already replaced" meant the record existed at all. The two then said
		different things the moment the reader pressed Ctrl+Z - the row went back to unreplaced,
		correctly, while the menu went on refusing to take it in ("this change has already been
		taken in"). One question, two answers, which is the fault this file has the most scars from
		([[one-question-one-place]]).

		★The answer is the document's own: the story's counter, of the kind this change is measured
		 by (CountForKind), against the counter recorded when the change went in. An undo takes the
		 counter back and this answers kFalse with no undo-specific code anywhere.
		@return kFalse for a change that was never replaced, and for one an undo has taken back. */
	bool16 StillReplaced(const KCMStoryRow& row, const KCMStoryChange& change);

	/** Drop the row's replaced records that an undo has taken back (StillReplaced answers kFalse).

		★**THE RECORD IS THE READER'S OWN HISTORY, so only the ones that are no longer true go.**
		  Undo in InDesign is a stack: undoing once takes back the LAST take-in, and the ones
		  before it are still in the document. Clearing the row outright - what "Refresh Story
		  Comparison" does, deliberately, as a fresh start - would throw those away as well.
		@return kTrue when any went, which is also "this story now needs comparing again": the
			change that came back is not in the live list until it is. */
	bool16 DropUndoneReplaced(int32 nth);
}

#endif // __KCMStoryDiffRun_h__

// End, KCMStoryDiffRun.h.
