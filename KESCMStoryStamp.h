//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Reads every user story's change counter out of a document, and matches two documents'
//  readings by story UID, so the panel can say which stories had their TEXT edited.
//
//  KESCM compares PIXELS, so on its own it can only say "this page looks different". What
//  this file adds is the distinction between "the text changed" and "only the layout changed".
//
//  What is read is ITextModel::GetChangeCount() - the "all changes" counter (ITextModel.h:185-192),
//  not the text-only one. Two reasons: GetTextChangeCount does not move for an edit that only
//  changes attributes, and GetOtherChangeCount's own documentation (ITextModel.h:173-183) states
//  that a TableModel signals change through these counters - "a change to a Table stroke does
//  represent an effective change to the TextModel" - so a table edit reaches the story that
//  contains it only through the counter that includes Other.
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

/** One story's reading: which story, and what its change counter said. */
struct KESCMStoryStamp
{
	UID		fStoryUID;
	uint32	fChangeCount;

	KESCMStoryStamp() : fStoryUID(kInvalidUID), fChangeCount(0) {}
};

/** One row of the comparison: a story whose text differs between the two versions. */
struct KESCMStoryDiff
{
	UID		fStoryUID;		// the TARGET side's UID (every row exists in the target)
	uint32	fSourceCount;	// 0 when fAdded is kTrue - there is no source story to read
	uint32	fTargetCount;
	bool16	fAdded;			// kTrue when the source has no story with this UID

	KESCMStoryDiff() : fStoryUID(kInvalidUID), fSourceCount(0), fTargetCount(0), fAdded(kFalse) {}
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

	/** Match two readings by story UID and report the stories whose text differs.

		A row is produced when the same UID reads a different counter, and when the target holds a
		UID the source does not (added). Stories that read the same, and stories the source has but
		the target does not (removed), produce nothing - a removed story cannot be jumped to.

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
