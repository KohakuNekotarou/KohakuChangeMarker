//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  ACCEPT AND REJECT IN THE TRACK CHANGES MODE (2026-10-25 - the author: "a right click in the Track mode that takes the
//  change back", "and confirms it", "on the Story too", "on the Story Edits heading, for the document"; design
//  docs/superpowers/specs/2026-10-25-kcm-track-accept-reject-design.md).
//
//  The records a row was made of (KCMTrackRecordRef - an insertion and / or a deletion, copied out by KCMTrackRead) are
//  found again in their story by their kind, time, author and place, and accepted or rejected one at a time with the
//  redline iterator - RedlineIterator::ProcessAccept / ProcessReject with no range, "this change only"
//  (redlineiterator.h:81-96), the calls the retired "Reject This Import Change" made (KCMRejectImport.cpp until
//  2026-10-25). ★ONLY WHAT THE LIST SHOWS: a story's other records (a note's, kApply) are not touched.
//  ⚠The caller holds the undo step (the facade's BeginStep / EndStep - one abortable step) and reads the list again.
//
//========================================================================================

#ifndef __KCMTrackResolve_h__
#define __KCMTrackResolve_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KCMTrackList.h"	// KCMTrackRecordRef

class IDataBase;

/** One record to accept or reject: its story, its author's name (the record holds it) and which one it is. */
struct KCMTrackResolveItem
{
	UID					fStory;
	PMString			fAuthor;
	KCMTrackRecordRef	fRecord;
	KCMTrackResolveItem() : fStory(kInvalidUID) {}
};

namespace KCMTrackResolve
{
	/** Accepts (`accept`) or rejects every record of `items` in targetDB, each found again at its place by its kind, time
		and author - ★FROM THE BACK OF EACH STORY TO THE FRONT: an accept or a reject moves what stands after it, never
		what stands before, so every place still to be processed is where the list read it.
		A record no longer there - accepted, rejected or undone since the list was read, or taken along by the reject of
		an insertion nested in the deletion (redlineiterator.h:46-50) - is passed over and counted in outGone.
		@return the records processed; -1 when the iterator refused one, with outMessage naming it (the caller then
		aborts its step, which takes back the ones before it). */
	int32	Run(IDataBase* targetDB, const std::vector<KCMTrackResolveItem>& items, bool16 accept,
				int32& outGone, PMString& outMessage);
}

#endif // __KCMTrackResolve_h__

// End, KCMTrackResolve.h.
