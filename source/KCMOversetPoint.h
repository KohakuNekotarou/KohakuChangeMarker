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
//  ★★**A FRAME'S "+" IS ASKED FOR; A CELL'S RED DOT IS WORKED OUT** (2026-10-31, measured against the red
//  pixels of the frame selected). InDesign's handle code draws the "+" at
//  IPageItemUtils::GetTextFrameHandlePositions (CHandleShape::DrawTextFrameHandles), in the frame's inner
//  coordinates: within a pixel of the middle of the red in a plain, a vertical, a rotated, a two-column, an
//  inset, an oval and a threaded frame. The parcel's corner taken until then stood 15 points below it, and
//  about 50 points off on the oval; KFC's KFCOversetLocator still takes the corner. Nothing was found to ask
//  for a cell, so there the point stays the OUTPORT - the bottom-right corner, in PARCEL-LOCAL coordinates,
//  of the last parcel of the thread that still has a frame - transformed parcel -> frame inner -> pasteboard.
//
//  ⚠★★**DO NOT ADD A BRANCH ON WRITING DIRECTION.** The cell's corner is taken in parcel-local
//  coordinates and GetParcelToFrameMatrix carries the direction, so (Right, Bottom) lands at the
//  bottom-LEFT for vertical text without being told. This was measured on the real application in
//  a Japanese document (2026-08-06) and the note is kept because the code looks horizontal-only:
//  a branch added here breaks what is currently correct ([[overset-and-table-jump-location]]).
//
//========================================================================================

#ifndef __KCMOversetPoint_h__
#define __KCMOversetPoint_h__

#include "BaseType.h"	// TextIndex
#include "OMTypes.h"	// UID
#include "PMPoint.h"	// PBPMPoint - the "+" in pasteboard coordinates

class IDataBase;
class IParcelList;
class ITextModel;
class ParcelKey;

/** How many threads a place is climbed out of, at most: a dictionary that anchored into its own thread would
	otherwise spin, and this many levels of nested tables is more than a document holds. */
const int32 kKCMThreadClimbLimit = 16;

/** The place the thread at `pos` hangs from - a table's anchor character for a cell, the reference character for a
	footnote (ITextStoryThreadDict::GetAnchorTextRange, as SnpManipulateTextModel.cpp asks it; it answers for a cell what
	ITableUtils::TableToPrimaryTextIndex answers, measured 2026-10-31).
	@return kFalse for a thread that hangs from nothing: the body's, and one not anchored. */
bool16 KCMThreadAnchorAt(ITextModel* textModel, TextIndex pos, TextIndex& outAnchor);

/** The text column the place `pos` is composed into, the composition brought up to date first - THE ONE QUESTION behind
	"is this change overset" (the row's OV) and "which frame is it in" (the jump, the reach). Until 2026-10-31 there were
	two answers: the row asked the place's own parcel without composing, the jump asked the frame of its table's or its
	note's anchor - so a row pushed out with its table was "OV" and went to the table's first page, and a restore left the
	row under it saying what it said before the restore.

	★**ASKED OF THE PLACE, NOT OF THE THREAD.** ITextParcelList::GetIsOverset answers "does this
	thread overflow anywhere", which is a different question: a story can overflow while the
	change being asked about sits comfortably on page 1. The parcel containing this very index is
	what decides, and the official header says so outright - "If the TextIndex is in overset an
	invalid ParcelKey will be returned" (ITextParcelList.h, above GetParcelContaining).

	⚠**TWO WAYS TO BE UNCOMPOSED, AND BOTH COUNT**: no parcel contains the index at all, or the
	parcel that does has no frame (its GetParcelFrameUID is kInvalidUID - a cell pushed out with its
	row, measured). KESCL judges a search hit's visibility the same way.

	@warning **THE CALLER MUST HOLD A IDataBase::SaveRestoreModifiedState**: composing dirties the document.
	@param caret kTrue for a GAP rather than a character (a deletion's place): a gap just after the last placed character
		stands at the end of that line, which ITextParcelList::GetParcelContainingLeanLeft answers. Not for a character:
		lean-left says "placed" of the very first overset one.
	@return kInvalidUID when the place is overset, or the question cannot be asked. */
UID KCMPlaceColumn(ITextModel* textModel, TextIndex pos, bool16 caret);

/** Is the text at `pos` OVERSET - not composed into any frame the reader can see (KCMPlaceColumn)?

	@return kFalse when the model is nil or the question cannot be asked - the safe answer, since
		the caller then behaves exactly as it did before any of this existed. */
bool16 KCMIsTextIndexOverset(ITextModel* textModel, TextIndex pos, bool16 caret);

/** Where the "+" of the overflowing thread at `pos` is drawn.

	Walks the thread's parcels backwards from the end and takes the first one that still has a
	frame: for the body, that frame's "+"; for a cell, the corner of the cell. When nothing of the
	thread is placed - a table cell pushed out of its frame along with its row, a footnote whose
	reference is overset - the anchors are climbed towards the parent thread and the first ancestor
	with a placed frame answers instead (KFCFindOversetLocator climbs the same way).

	@param outFrame the frame that shows the "+".
	@param outPb the point itself, in pasteboard coordinates - what a view scrolls to.
	@return kFalse when no parcel of the thread, or of any ancestor, is placed at all. The caller
		should then do whatever it did before (for the Story Edits jump: fall back to the story's
		first frame). */
bool16 KCMFindOversetOutport(ITextModel* textModel, TextIndex pos, UID& outFrame, PBPMPoint& outPb);

/** Where one corner of a parcel stands on the pasteboard, and the frame the parcel is in - the step
	the walk above and the walk to a story's start (KCMStoryStartPoint) both take.

	@param outport kTrue for the parcel's outport corner (Right, Bottom), kFalse for its inport (Left, Top).
	@return kFalse for a parcel that is not placed in a frame. */
bool16 KCMParcelCornerOnPasteboard(IParcelList* pl, const ParcelKey& key, IDataBase* db, bool16 outport,
								   UID& outFrame, PBPMPoint& outPb);

#endif // __KCMOversetPoint_h__

// End, KCMOversetPoint.h.
