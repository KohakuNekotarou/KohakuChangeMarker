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
	bool16			fSourceExact;	// fChange's Source range was checked against the copy (KCMTrackRead)
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
		recognise the next one. notRejectedInCopy / otherRecords are the two numbers the report line says. */
	void	Set(const std::vector<KCMTrackAuthor>& authors, const std::vector<KCMTrackRow>& rows,
				int32 notRejectedInCopy, int32 otherRecords);
	/** Empty the list AND the counter history (Stop, a closed document). */
	void	Clear();
	int32	GetRowCount();
	const KCMTrackRow*		GetRow(int32 nth);
	int32	GetAuthorCount();
	const KCMTrackAuthor*	GetAuthor(int32 a);
	int32	GetTotalChangeCount();
	int32	GetNotRejectedInCopy();
	int32	GetOtherRecordCount();
	/** Every story the list names, once each. */
	void	GetStoryUIDs(std::vector<UID>& out);
	/** The Undo / Redo rule of KCMStoryList::NeedsCompareAgain, for one story: its aggregate change counter
		(ITextModel::GetChangeCount) went below the value it was read at, or came back to one it was read at.
		Plain typing makes new values and answers kFalse. */
	bool16	NeedsReadAgain(UID storyUID, IDataBase* targetDB);
	/** After a read: every listed story's counter goes into its history. */
	void	NoteStoryCounters(IDataBase* targetDB);
	/** The list as TSV for app.kcmStoryRows in the Track mode. */
	void	RowsAsTsv(PMString& out);
	void	ShutdownCleanup();
}

#endif // __KCMTrackList_h__

// End, KCMTrackList.h.
