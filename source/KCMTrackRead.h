//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  READING THE TRACKED CHANGES (2026-10-05, design section 3). Every story's records - main text, table
//  cells, hidden conditional text; not notes; a footnote's are never made (InDesign records nothing there,
//  KFC measured) - copied out of the iterator (the same walk as KIDMCP's track_changes, KIDMCPTrackChanges.cpp),
//  put together by KCMTrackPlan.h, given their words, their author's colour and their place in the original,
//  and handed to KCMTrackList.
//  ★THE COLOUR IS READ HERE, ON THE MAIN THREAD, ONCE (ITrackChangeUtils::GetTrackedChangeBGColor): which
//   plug-in implements that utility is not known, so a background export cannot be trusted to call it. The
//   marks draw the value (design 6-1).
//
//========================================================================================

#ifndef __KCMTrackRead_h__
#define __KCMTrackRead_h__

#include "UIDRef.h"

class IDataBase;
struct KCMTrackChange;

namespace KCMTrackRead
{
	/** Read targetDB's tracked changes into KCMTrackList, replacing what it held. When sourceDB is the copy
		"Compare with Tracked Changes..." made from targetDB (KCMIsTrackOriginalOf), each change's Source range
		is computed and checked against that copy; otherwise it is (0,0) - the story's start. Main thread.
		@return the number of changes; -1 when the progress bar's Cancel was pressed (*outCancelled kTrue). */
	int32	Build(IDataBase* targetDB, IDataBase* sourceDB, bool16* outCancelled);

	/** Is the change still in the document: a record of the same author, kind and time in its story?
		An accept, a reject or an undo since the read leaves none. */
	bool16	StillRecorded(IDataBase* targetDB, UID storyUID, const KCMTrackChange& change);
}

#endif // __KCMTrackRead_h__

// End, KCMTrackRead.h.
