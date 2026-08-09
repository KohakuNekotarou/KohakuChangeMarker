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
//      story is reported at all. It has to be this one rather than the text-only counter, because
//      GetTextChangeCount does not move for an edit that only changes attributes, and because
//      GetOtherChangeCount's own documentation (ITextModel.h:173-183) states that a TableModel
//      signals change through these counters - "a change to a Table stroke does represent an
//      effective change to the TextModel" - so a table edit reaches its containing story only
//      through a counter that includes Other.
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
	kKESCMStoryKindAttr		= 2,	// effective attributes - INCLUDING applied styles and overrides
	kKESCMStoryKindOther	= 4,	// tables, inlines and the like signalling through the model
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
