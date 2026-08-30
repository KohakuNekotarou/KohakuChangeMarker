//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Reads every user story's change counters out of a document, and matches two documents'
//  readings by story UID, so the panel can say which stories changed -- and in what way.
//
//  KCM compares PIXELS, so on its own it can only say "this page looks different". What this
//  file adds is what kind of change it was: the words, their formatting, or something attached to
//  the story such as a table.
//
//  ALL FOUR COUNTERS ARE READ, and they answer two different questions:
//
//    - GetChangeCount() -- the "all changes" counter -- decides whether the story is reported at
//      all. It has to be this one rather than any single sub-counter, because which sub-counter
//      an edit lands on cannot be predicted from the headers. Measured: a table stroke moved
//      Attr, not Other, even though ITextModel gives exactly that edit as its example of what
//      Other is for ("a change to a Table stroke does represent an effective change to the
//      TextModel ... the TableModel will use this counter"). Adding a table row and inserting an
//      inline both moved Text and Attr, and Other stayed put in every case measured. The
//      aggregate is the only reading that cannot be wrong-footed.
//
//      **OTHER HAS NEVER ONCE MOVED, AND IT HAS BEEN LOOKED FOR PROPERLY.** Nine more edits in
//      one comparison (work/kescm-storytest/make-kinds-docs.jsx): a table cell's fill, merging
//      cells, column width, applying a table style, an inline's fill, an inline's size, a
//      footnote, a condition, a hyperlink. SEVEN of them reported "Attr" alone -- and since the
//      label names the FIRST kind that moved in the order Text, Attr, Other, "Attr" alone proves
//      Other did not move. (The two that read "Text+" -- merging cells, adding a footnote -- keep
//      their second kind hidden behind the "+".) All nine WERE listed, which is the point: the
//      aggregate caught every one of them.
//      Full record: docs/ai-notes/kescm-story-counters-2026-08-09.md.
//
//    - The three sub-counters name what moved, and nothing more. They are not the test: the
//      header promises the aggregate moves for any change to them, but never promises it is
//      their sum. Such stories were listed by the aggregate all along; naming the kind of change
//      is the whole of what reading the sub-counters added.
//
//  A table is NOT a story of its own. Table cells and footnotes are further story threads inside
//  the same ITextModel, so an edit in a cell moves the counter of the story the table sits in.
//  There is no way to list a table as its own row, and no need to.
//  @warning the headers say this of table cells only -- ITextModel's TotalLength against
//   GetPrimaryStoryThreadSpan. That FOOTNOTES sit there too is measured rather than documented;
//   see the same note in KCMStoryList.cpp's FirstReadableText.
//
//  **WHY TWO VERSIONS CAN BE MATCHED AT ALL.** Two measured properties:
//    - saving under a new name carries both the story UIDs and the counters across, so the old
//      and the new version of a document can be matched story by story;
//    - the counters are persisted in the file and wind back on Undo, so they are a version
//      number for the story's state rather than a count of edits. "Edited and then undone" reads
//      as unchanged, so no false row appears.
//  **THE TWO HEADINGS, ABOVE AND BELOW, ARE HOW THIS IS QUOTED -- NOT LINE NUMBERS.** Several
//  files lean on these two paragraphs. Every one of them cited a line range once, and every one
//  of those had rotted by nine to eleven lines: a single insertion into this comment moved the
//  lot at once, and only the reference written on the day of the split had ever been re-counted.
//  Quote the heading; it survives an edit above it. To find who is leaning on a heading, grep
//  for its words -- and note that a quotation wrapped across two lines only answers to part of
//  it, so grep for a fragment rather than the whole heading.
//
//  **READING COUNTERS COMPOSES NOTHING.** So no SaveRestoreModifiedState guard is needed here --
//  the same note KESCL's CaptureDocStamp carries (KESCLFindInDoc.cpp).
//
//  **WHY NOT TRACK CHANGES?** InCopy's track-changes feature answers a question that LOOKS like
//  this one: ITrackChangeUtils::StoryHasChanges(UIDRef) is literally "did this story change?",
//  with RangeHasChanges, GetDeletedText and the redline's kind and colour beside it. It is not
//  the road for this feature, and any ONE of these three would be enough on its own:
//    - it records nothing unless the user switched it on BEFORE the edits were made, whereas KCM
//      is a tool for comparing two versions after the fact;
//    - it lives inside ONE story's own history and does not span two documents, which is the
//      entire problem here;
//    - it tracks insertions, deletions and moves (MarkInsertChanges / MarkDeleteChanges /
//      MarkMoveChanges) and NOT formatting -- so it cannot answer the one thing this file exists
//      to answer beyond the pixels: were it the words, or their attributes?
//  Recorded here so the question is not re-opened from scratch the next time somebody notices
//  that InDesign already has a "what changed" feature.
//
//  **THIS FILE KEEPS NO STATE.** Both functions fill a vector the caller owns. Nothing has to be
//  cleared at shutdown, and nothing survives between comparisons.
//
//========================================================================================

#ifndef __KCMStoryStamp_h__
#define __KCMStoryStamp_h__

#include "UIDRef.h"

#include <vector>

// The two kinds enums the counters are reported in. THEY LIVE IN THEIR OWN HEADER because
// IKCMStoryEditsFacade carries them across to the UI, and a header the UI includes must not also
// put the three KCMStoryEdits functions BELOW within its reach -- they are model-side and cannot
// be linked from there.
#include "KCMStoryKinds.h"	// KCMStoryChangeKind / KCMStoryAttrKind / kKCMStoryKindUnpaired

class IDataBase;

/** One story's reading: which story, and what each of its change counters said. */
struct KCMStoryStamp
{
	UID		fStoryUID;
	uint32	fChangeCount;	// the all-changes counter - this alone decides whether a row appears
	uint32	fTextCount;
	uint32	fAttrCount;
	uint32	fOtherCount;

	KCMStoryStamp()
		: fStoryUID(kInvalidUID), fChangeCount(0), fTextCount(0), fAttrCount(0), fOtherCount(0) {}
};

/** One row of the comparison: a story that differs between the two versions, and how.

	No counter values are carried. They are version numbers for the story's state rather than counts
	of edits, so the size of the difference means nothing to a reader and the panel does not show
	it -- only which kinds moved.
*/
struct KCMStoryDiff
{
	/** The story's UID IN THE DOCUMENT THAT HOLDS IT, and which document that is depends on fKinds:
		the target for every row except a Removed one, which exists only in the source.

		@warning **every row used to exist in the target, and that was the whole feature's premise.**
		  Removed rows broke it deliberately -- see the Compare contract below and
		  docs/superpowers/specs/2026-08-21-kescm-removed-story-rows-design.md.
		**NO SECOND FIELD NAMING THE DOCUMENT:** fKinds already carries the answer, and a second
		field could disagree with it. */
	UID		fStoryUID;
	uint32	fKinds;		// OR of KCMStoryChangeKind - what the row names

	KCMStoryDiff() : fStoryUID(kInvalidUID), fKinds(kKCMStoryKindNone) {}
};

namespace KCMStoryEdits
{
	/** Read ONE story's change counters.

		The script properties (stories[n].kcmChangeCount and the three sub-counters) go through here,
		and CollectStamps below is written in terms of it, so the panel and a script can never read
		the story differently.

		@param storyRef the story to read.
		@param out filled only when kTrue is returned; untouched otherwise.
		@return kTrue when the reference names something with an ITextModel, kFalse when it does
		        not (a script can hand us any object, so this is a real answer, not a can't-happen).
	*/
	bool16 ReadStamp(const UIDRef& storyRef, KCMStoryStamp& out);

	/** Read every user-accessible story's change counter in this document.

		User-accessible only, not every story: IStoryList states that internal stories are "not
		subject to search through find change, spell checking", and a row the user cannot reach is a
		row they cannot act on.

		@param db the document to read. nil is tolerated and yields an empty list.
		@param out filled with one entry per readable story, cleared first.
	*/
	void CollectStamps(IDataBase* db, std::vector<KCMStoryStamp>& out);

	/** Match two readings by story UID and report the stories that differ, with the kinds that moved.

		A row is produced in three cases: the same UID reads a different all-changes counter, the
		target holds a UID the source does not (added), and the SOURCE holds a UID the target does
		not (removed). Only stories that read the same produce nothing.

		@warning **a removed row's fStoryUID is a SOURCE uid**; every other row's is a target uid.
		  Callers tell them apart by kKCMStoryKindRemoved. Removed rows were dropped altogether while
		  a row could only aim the target window; the panel now aims the SOURCE window at these, so
		  that reason is gone.

		Each row's fKinds says which of the three sub-counters moved. A row whose aggregate moved
		while no sub-counter did is reported as Other rather than dropped: nothing in the header
		rules that combination out, and having already decided the story changed, saying "something"
		beats saying nothing.

		Note that when the two versions are NOT related by a save-as -- a version built by copying
		into a new document, say -- none of the UIDs will line up and every target story is reported
		as added. That is deliberate and needs no special case: the caller shows what it is given.

		@param source the older version's reading.
		@param target the newer version's reading.
		@param out filled with one entry per differing story, cleared first. Order follows the
		       target's enumeration; sorting by page happens in the UI layer, not here.
	*/
	void Compare(const std::vector<KCMStoryStamp>& source,
	             const std::vector<KCMStoryStamp>& target,
	             std::vector<KCMStoryDiff>& out);
}

#endif // __KCMStoryStamp_h__

// End, KCMStoryStamp.h.
