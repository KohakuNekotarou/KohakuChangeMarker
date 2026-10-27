//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  THE TRACK CHANGES MODE'S LIST (2026-10-05, design docs/superpowers/specs/2026-10-05-kcm-track-changes-mode-design.md).
//  Authors -> rows (one story under one author) -> changes. The rows of one author are CONSECUTIVE, so the
//  tree's author level is a range of row indices (KCMTrackAuthor::fFirstRow / fRowCount).
//
//  ★A FILE-STATIC LIST, LIKE KCMStoryList's, and for the same reasons: built by one comparison, thrown away by
//   the next, nothing to persist. ShutdownCleanup empties it (the shutdown service calls it - KCMPeek.cpp).
//  ★WHY NOT KCMStoryList ITSELF: that list carries the import's "!" rows and the taken-back records, whose
//   index space every reader of it relies on. The facade switches between the two lists by mode
//   (KCMFacades.cpp) - one question, one place.
//
//========================================================================================

#ifndef __KCMTrackList_h__
#define __KCMTrackList_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KCMStoryList.h"	// KCMStoryRow / KCMStoryChange - a Track row is drawn as a Story row

class IDataBase;

/** One record a change was made of, as KCMTrackRead copied it out of the iterator (2026-10-25): what Accept and Reject
	find it again by in its story (KCMTrackResolve) - with the author's name, which the change carries by index. */
struct KCMTrackRecordRef
{
	bool16		fDelete;	// a deletion; else an insertion
	TextIndex	fAt;		// the iterator's position: an insertion's first character, a deletion's anchor
	uint64		fTime;		// VOSRedlineChange::GetTimeStamp
	KCMTrackRecordRef() : fDelete(kFalse), fAt(0), fTime(0) {}
};

/** One change of the Track mode: the Story change it is drawn as, and who made it in what colour. */
struct KCMTrackChange
{
	KCMStoryChange	fChange;		// ranges and the three pieces: fText* = now, fOtherText* = before
	int32			fAuthor;		// index into the authors
	int32			fKind;			// KCMTrackChangeKind (KCMTrackPlan.h)
	uint64			fTime;
	bool16			fHasColour;		// kFalse = InDesign draws this author on white: the Mark colour stands in
	uint8			fR, fG, fB;
	bool16			fHidden;		// in hidden conditional text
	bool16			fSourceExact;	// fChange's Source range was checked against the copy (KCMTrackRead) - ⚠always kFalse since 2026-10-27 (no copy); its readers stay for the waste-removal pass
	std::vector<KCMTrackRecordRef>	fRecords;	// the insertion and / or the deletion it was made of (2026-10-25)
	KCMTrackChange() : fAuthor(-1), fKind(0), fTime(0), fHasColour(kFalse), fR(0), fG(0), fB(0),
					   fHidden(kFalse), fSourceExact(kFalse) {}
};

struct KCMTrackRow
{
	KCMStoryRow						fRow;		// KCMStoryList::ReadRowForStory
	int32							fAuthor;
	std::vector<KCMTrackChange>		fChanges;	// text order
	KCMTrackRow() : fAuthor(-1) {}
};

struct KCMTrackAuthor
{
	PMString	fName;			// as the record holds it - may be empty
	bool16		fHasColour;
	uint8		fR, fG, fB;
	int32		fFirstRow;		// [fFirstRow, fFirstRow + fRowCount)
	int32		fRowCount;
	int32		fChangeCount;
	KCMTrackAuthor() : fHasColour(kFalse), fR(0), fG(0), fB(0), fFirstRow(0), fRowCount(0), fChangeCount(0) {}
};

namespace KCMTrackList
{
	/** Replace the list. The counter history (NeedsReadAgain) is KEPT: a re-read after an undo has to be able to
		recognise the next one. otherRecords is the number the report line says. (A second number - the records a copy
		kept although every change was rejected - went on 2026-10-27 with the copy.) */
	void	Set(const std::vector<KCMTrackAuthor>& authors, const std::vector<KCMTrackRow>& rows, int32 otherRecords);
	/** Empty the list AND the counter history (Stop, a closed document). */
	void	Clear();
	int32	GetRowCount();
	const KCMTrackRow*		GetRow(int32 nth);
	int32	GetAuthorCount();
	const KCMTrackAuthor*	GetAuthor(int32 a);
	int32	GetTotalChangeCount();
	int32	GetOtherRecordCount();
	/** Every story the list names, once each. */
	void	GetStoryUIDs(std::vector<UID>& out);
	/** The Undo / Redo rule of KCMStoryList::NeedsCompareAgain, for one story: its aggregate change counter
		(ITextModel::GetChangeCount) went below the value it was read at, or came back to one it was read at.
		Plain typing makes new values and answers kFalse. */
	bool16	NeedsReadAgain(UID storyUID, IDataBase* targetDB);
	/** NeedsReadAgain for every story in the counter history - the listed ones and those that have lost their rows
		(2026-10-25: the Undo follower is told by the document now, which does not say which story). */
	bool16	AnyNeedsReadAgain(IDataBase* targetDB);
	/** After a read: every listed story's counter goes into its history - and every story's already in it (2026-10-25:
		a story whose rows all went must still recognise the Undo that brings them back). */
	void	NoteStoryCounters(IDataBase* targetDB);
	/** After a read: how many tracked-change records the Target had (KCMTrackRead::Build counts them as it reads), and
		that number back (-1 = not read since Clear). An accept changes no text and moves no counter (measured
		2026-10-25), so the records are what tells an accept, a reject and their Undo and Redo
		(KCMTrackRead::RecordsMovedSinceRead). */
	void	NoteRecordCount(int32 records);
	int32	GetRecordCountAtRead();
	/** The list as TSV for app.kcmStoryRows in the Track mode. */
	void	RowsAsTsv(PMString& out);
	void	ShutdownCleanup();
}

#endif // __KCMTrackList_h__

// End, KCMTrackList.h.
