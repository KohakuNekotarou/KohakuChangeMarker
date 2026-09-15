//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Whether one place in a story is OVERSET, and where InDesign draws the "+" for it
//  (2026-09-15, the user's request: "I want it to jump to where the + is").
//
//  ★★WHERE THIS CAME FROM, AND WHAT DID NOT COME BACK WITH IT. KCM had a whole Find Overset
//  feature - a scan of every story, a red "+" drawn on the Pages panel thumbnails, a red band on
//  the scroll map, and Prev/Next cycling through them - and it was retired on 2026-09-08, feature
//  and files together, because the application's own preflight panel does that job (commit
//  3e98956). **That judgement stands and none of it is coming back.** What is restored here are
//  the two computations underneath it, because the Story Edits list has a use for them the scan
//  never had: a CHANGE can sit in text that is not composed anywhere, and until now a jump to one
//  fell back to the story's first frame and said nothing (measured on the application, 2026-09-15:
//  the caret was placed correctly, parentTextFrames was 0, and the window moved to page 1).
//
//  ⚠**THE "+" POINT IS NOT A PUBLIC API.** There is no interface that hands out the indicator's
//  position; it is worked out as the OUTPORT - the bottom-right corner, in PARCEL-LOCAL
//  coordinates, of the last parcel of the thread that still has a frame - and transformed
//  parcel -> frame inner -> pasteboard. KBS's KBSOversetLocator computes it the same way.
//
//  ⚠★★**DO NOT ADD A BRANCH ON WRITING DIRECTION.** The corner is taken in parcel-local
//  coordinates and GetParcelToFrameMatrix carries the direction, so (Right, Bottom) lands at the
//  bottom-LEFT for vertical text without being told. This was measured on the real application in
//  a Japanese document (2026-08-06) and the note is kept because the code looks horizontal-only:
//  a branch added here breaks what is currently correct ([[overset-and-table-jump-location]]).
//
//========================================================================================

#ifndef __KCMOversetPoint_h__
#define __KCMOversetPoint_h__

#include "PMPoint.h"	// PBPMPoint - the "+" in pasteboard coordinates
#include "UIDRef.h"		// UID

class IDataBase;
class ITextModel;

/** Is the text at `pos` OVERSET - not composed into any frame the reader can see?

	★**ASKED OF THE PLACE, NOT OF THE THREAD.** ITextParcelList::GetIsOverset answers "does this
	thread overflow anywhere", which is a different question: a story can overflow while the
	change being asked about sits comfortably on page 1. The parcel containing this very index is
	what decides, and the official header says so outright - "If the TextIndex is in overset an
	invalid ParcelKey will be returned" (ITextParcelList.h, above GetParcelContaining).

	⚠**TWO WAYS TO BE UNCOMPOSED, AND BOTH COUNT**: no parcel contains the index at all, or the
	parcel that does has no frame (its GetParcelFrameUID is kInvalidUID). KESCL judges a search
	hit's visibility the same way.

	@return kFalse when the model is nil or the question cannot be asked - the safe answer, since
		the caller then behaves exactly as it did before any of this existed. */
bool16 KCMIsTextIndexOverset(ITextModel* textModel, TextIndex pos);

/** Where the "+" of the overflowing thread at `pos` is drawn.

	Walks the thread's parcels backwards from the end and takes the first one that still has a
	frame; its outport corner is the "+". When nothing of the thread is placed - a table cell
	pushed out of its frame along with its row - the table anchors are climbed towards the parent
	thread and the first ancestor with a placed frame answers instead.

	@param outFrame the frame that shows the "+".
	@param outPb the point itself, in pasteboard coordinates - what a view scrolls to.
	@return kFalse when no parcel of the thread, or of any ancestor, is placed at all. The caller
		should then do whatever it did before (for the Story Edits jump: fall back to the story's
		first frame). */
bool16 KCMFindOversetOutport(ITextModel* textModel, IDataBase* db, TextIndex pos,
							 UID& outFrame, PBPMPoint& outPb);

#endif // __KCMOversetPoint_h__

// End, KCMOversetPoint.h.
