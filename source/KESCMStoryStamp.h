//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Reads every user story's change counters out of a document, and matches two documents'
//  readings by story UID, so the panel can say which stories changed - and in what way.
//
//  KESCM compares PIXELS, so on its own it can only say "this page looks different". What this
//  file adds is what kind of change it was: the words, their formatting, or something attached to
//  the story such as a table.
//
//  ALL FOUR COUNTERS ARE READ, and they answer two different questions:
//
//    - GetChangeCount() - the "all changes" counter (ITextModel.h:185-192) - decides whether the
//      story is reported at all. It has to be this one rather than any single sub-counter, because
//      which sub-counter an edit lands on cannot be predicted from the headers. Measured
//      2026-08-10: a table stroke moved Attr, not Other, even though ITextModel.h:173-183 gives
//      exactly that edit as its example of what Other is for ("a change to a Table stroke does
//      represent an effective change to the TextModel ... the TableModel will use this counter").
//      Adding a table row and inserting an inline both moved Text and Attr, and Other stayed put
//      in every case measured. The aggregate is the only reading that cannot be wrong-footed.
//
//      *** OTHER HAS NEVER ONCE MOVED, AND IT HAS NOW BEEN LOOKED FOR PROPERLY. *** 2026-08-11, nine
//      more edits in one comparison (work/kescm-storytest/make-kinds-docs.jsx): a table cell's fill,
//      merging cells, column width, applying a table style, an inline's fill, an inline's size, a
//      footnote, a condition, a hyperlink. SEVEN of them reported "Attr" alone - and since the label
//      names the FIRST kind that moved in the order Text, Attr, Other, "Attr" alone proves Other did
//      not move. (The two that read "Text+" - merging cells, adding a footnote - keep their second
//      kind hidden behind the "+", and the user chose to leave it at that.) All nine WERE listed,
//      which is the point: the aggregate caught every one of them.
//      Full record: docs/ai-notes/kescm-story-counters-2026-08-09.md.
//
//    - The three sub-counters name what moved, and nothing more. They are not the test: the header
//      promises the aggregate moves for any change to them, but never promises it is their sum.
//
//  (Reading the sub-counters was added 2026-08-09 at the user's request - "show changes other than
//  text ones too". Note that such stories were ALREADY being listed, since the test was always the
//  aggregate; what was missing was saying which kind of change it had been.)
//
//  A table is NOT a story of its own. Table cells and footnotes are further story threads inside
//  the same ITextModel (ITextModel.h:140,145), so an edit in a cell moves the counter of the story
//  the table sits in. There is no way to list a table as its own row, and no need to.
//
//  Two properties measured on 2026-08-08 are what make this work at all:
//    - saving under a new name carries both the story UIDs and the counters across, so the old
//      and the new version of a document can be matched story by story;
//    - the counters are persisted in the file and wind back on Undo, so they are a version number
//      for the story's state rather than a count of edits. "Edited and then undone" reads as
//      unchanged, so no false row appears.
//
//  Reading counters composes nothing, so no SaveRestoreModifiedState guard is needed here - the
//  same note KESCL's CaptureDocStamp carries (KESCLFindInDoc.cpp:375-397).
//
//  *** WHY NOT TRACK CHANGES? *** InCopy's track-changes feature answers a question that LOOKS like
//  this one: ITrackChangeUtils::StoryHasChanges(UIDRef) is literally "did this story change?"
//  (incopy/ITrackChangeUtils.h:154), with RangeHasChanges, GetDeletedText and the redline's kind and
//  colour beside it. It is not the road for this feature, and any ONE of these three would be enough
//  on its own (audit B7, 2026-08-16):
//    - it records nothing unless the user switched it on BEFORE the edits were made, whereas KESCM
//      is a tool for comparing two versions after the fact;
//    - it lives inside ONE story's own history and does not span two documents, which is the entire
//      problem here;
//    - it tracks insertions, deletions and moves (MarkInsertChanges / MarkDeleteChanges /
//      MarkMoveChanges) and NOT formatting - so it cannot answer the one thing this file exists to
//      answer beyond the pixels: were it the words, or their attributes?
//  Recorded here so the question is not re-opened from scratch the next time somebody notices that
//  InDesign already has a "what changed" feature.
//
//  *** THIS FILE KEEPS NO STATE. *** Both functions fill a vector the caller owns. Nothing has to
//  be cleared at shutdown, and nothing survives between comparisons.
//
//========================================================================================

#ifndef __KESCMStoryStamp_h__
#define __KESCMStoryStamp_h__

#include "UIDRef.h"

#include <vector>

class IDataBase;

/** Which kind of change moved. Values are OR'd together: one edit can move more than one of them.

	The first three map one-to-one onto ITextModel's three sub-counters (ITextModel.h:158-183).
	Added is not a counter - it means the source has no story with this UID at all, so there is
	nothing to have compared.
*/
enum KESCMStoryChangeKind
{
	kKESCMStoryKindNone		= 0,
	kKESCMStoryKindText		= 1,	// characters inserted, removed or replaced
	kKESCMStoryKindAttr		= 2,	// effective attributes - INCLUDING applied styles and overrides,
									// and, measured, table strokes and cells as well
	kKESCMStoryKindOther	= 4,	// the Other counter. Nothing has been found that moves it: the
									// table and inline edits its documentation names all landed on
									// Attr or Text instead (see the file comment). Kept because the
									// header defines it, and because Compare names it for the row
									// whose aggregate moved while no sub-counter did
	kKESCMStoryKindAdded	= 8		// no story with this UID on the source side
};

/** One story's reading: which story, and what each of its change counters said. */
struct KESCMStoryStamp
{
	UID		fStoryUID;
	uint32	fChangeCount;	// the all-changes counter - this alone decides whether a row appears
	uint32	fTextCount;
	uint32	fAttrCount;
	uint32	fOtherCount;

	KESCMStoryStamp()
		: fStoryUID(kInvalidUID), fChangeCount(0), fTextCount(0), fAttrCount(0), fOtherCount(0) {}
};

/** One row of the comparison: a story that differs between the two versions, and how.

	No counter values are carried. They are version numbers for the story's state rather than counts
	of edits (measured 2026-08-08), so the size of the difference means nothing to a reader and the
	panel does not show it - only which kinds moved.
*/
struct KESCMStoryDiff
{
	UID		fStoryUID;	// the TARGET side's UID (every row exists in the target)
	uint32	fKinds;		// OR of KESCMStoryChangeKind - what the row names

	KESCMStoryDiff() : fStoryUID(kInvalidUID), fKinds(kKESCMStoryKindNone) {}
};

namespace KESCMStoryEdits
{
	/** Read ONE story's change counters.

		Added 2026-08-15 for the script properties (stories[n].kcmChangeCount and the three
		sub-counters). CollectStamps below is written in terms of this, so the panel and a script
		can never read the story differently.

		@param storyRef the story to read.
		@param out filled only when kTrue is returned; untouched otherwise.
		@return kTrue when the reference names something with an ITextModel, kFalse when it does
		        not (a script can hand us any object, so this is a real answer, not a can't-happen).
	*/
	bool16 ReadStamp(const UIDRef& storyRef, KESCMStoryStamp& out);

	/** Read every user-accessible story's change counter in this document.

		User-accessible only, not every story: IStoryList.h:38-42 states that internal stories are
		"not subject to search through find change, spell checking", and a row the user cannot reach
		is a row they cannot act on.

		@param db the document to read. nil is tolerated and yields an empty list.
		@param out filled with one entry per readable story, cleared first.
	*/
	void CollectStamps(IDataBase* db, std::vector<KESCMStoryStamp>& out);

	/** Match two readings by story UID and report the stories that differ, with the kinds that moved.

		A row is produced when the same UID reads a different all-changes counter, and when the
		target holds a UID the source does not (added). Stories that read the same, and stories the
		source has but the target does not (removed), produce nothing - a removed story cannot be
		jumped to.

		Each row's fKinds says which of the three sub-counters moved. A row whose aggregate moved
		while no sub-counter did is reported as Other rather than dropped: nothing in the header
		rules that combination out, and having already decided the story changed, saying "something"
		beats saying nothing.

		Note that when the two versions are NOT related by a save-as - a version built by copying
		into a new document, say - none of the UIDs will line up and every target story is reported
		as added. That is deliberate and needs no special case: the caller shows what it is given.

		@param source the older version's reading.
		@param target the newer version's reading.
		@param out filled with one entry per differing story, cleared first. Order follows the
		       target's enumeration; sorting by page happens in the UI layer, not here.
	*/
	void Compare(const std::vector<KESCMStoryStamp>& source,
	             const std::vector<KESCMStoryStamp>& target,
	             std::vector<KESCMStoryDiff>& out);
}

#endif // __KESCMStoryStamp_h__

// End, KESCMStoryStamp.h.
