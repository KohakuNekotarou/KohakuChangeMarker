//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The list of stories that changed between the two versions, ready to be shown as rows.
//
//  This file turns KESCMStoryStamp's raw findings into something a person can read: the first
//  words of each story, which page it starts on, and what kind of change it saw. It knows nothing
//  about trees or widgets - the Story Edits tree reads this and only this.
//
//  *** THE LIST IS A FILE-STATIC GLOBAL, NOT A BOSS. *** It is built by one comparison and thrown
//  away by the next, so there is nothing to persist and nobody to share it with. KBS keeps its
//  result list exactly this way (KBSResultModel.cpp:27-29). The one obligation that comes with it:
//  ShutdownCleanup() has to empty it during a controlled shutdown, because these rows hold PMStrings
//  and a static PMString still holding storage at DLL unload is the crash KBS logged three times
//  over (KBSResultModel.cpp:363-377).
//
//========================================================================================

#ifndef __KESCMStoryList_h__
#define __KESCMStoryList_h__

#include "PMString.h"
#include "PMPoint.h"	// PBPMPoint - where a story begins, for the jump
#include "UIDRef.h"

#include <vector>

#include "KESCMStoryStamp.h"	// KESCMStoryDiff / KESCMStoryChangeKind

class IDataBase;

/** One row of the Story Edits section. */
struct KESCMStoryRow
{
	UID			fStoryUID;	// the target document's story
	PMString	fText;		// first readable words. NOT shortened for display - the row's text cell
							// is kEllipsizeMiddle and does that itself, at whatever width it has
	uint32		fKinds;		// OR of KESCMStoryChangeKind - named on the right of the row
	UID			fFrameUID;	// the story's FIRST frame - what a click scrolls to. kInvalidUID for an
							// unplaced story (no frame at all), which cannot be jumped to
	UID			fPageUID;	// where the story starts; kInvalidUID when it starts on the pasteboard
	int32		fPageIndex;	// sort key only. kMaxInt32 when there is no page, so those sink to the end

	KESCMStoryRow()
		: fStoryUID(kInvalidUID), fKinds(kKESCMStoryKindNone), fFrameUID(kInvalidUID),
		  fPageUID(kInvalidUID), fPageIndex(kMaxInt32) {}
};

/** The first frame a story is placed in - where a jump to that story should go.

	★TWO DOCUMENTS ASK THIS, WHICH IS WHY IT IS NOT PRIVATE TO THE LIST. Building the rows asks the
	target for it, and a click asks the SOURCE for the same story's frame, because the two versions
	can hold the story in DIFFERENT PLACES - the older window cannot be aimed by page number alone
	(user's observation, 2026-08-10). Matching by story UID works for the same reason the whole
	feature does: saving under a new name carries the UIDs across (KESCMStoryStamp.h:36-38).

	⚠ For two documents that are NOT versions of each other, a UID means nothing in common - the
	same reading as everywhere else in this feature, where the rows simply come out as "Added"
	(the design's §2-5: report it plainly, do not try to detect it).

	@param db which document to ask.
	@param storyUID the story. Anything that is not a placed story answers kInvalidUID.
	@return the first frame's UID, or kInvalidUID when there is no story there or it sits in no frame.
*/
UID KESCMStoryFirstFrameUID(IDataBase* db, UID storyUID);

/** Where a story BEGINS on the page, as a pasteboard point - what a jump to it should centre.

	★The first frame's centre is not the same thing. In a tall frame the centre is the middle of the
	text, and what a reader wants is the beginning of it (user's call, 2026-08-10). So this walks the
	parcels forward from the first and takes the leading corner of the first one actually placed.

	★VERTICAL TEXT NEEDS NO SPECIAL CASE. The corner is taken in PARCEL-LOCAL coordinates and
	GetParcelToFrameMatrix absorbs the writing direction - the same formula the overset scan uses for
	the opposite corner, and that one was verified on real vertical text
	(KESCMOversetScan.cpp:59-62). ⚠Do not add a writing-direction branch here.

	@param db which document to ask - the newer one for the click, the older one for its window.
	@param storyUID the story.
	@param outFrame [out] the frame that beginning sits in. Untouched when this answers kFalse.
	@param outPb [out] the point, in pasteboard coordinates. Untouched when this answers kFalse.
	@return kFalse when there is no story there, or none of its parcels are placed - callers fall
		back to centring the first frame (KESCMStoryFirstFrameUID).
*/
bool16 KESCMStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb);

namespace KESCMStoryList
{
	/** Replace the list with one row per entry in diffs, read out of the target document.

		Rows come out in page order. A story that starts on the pasteboard, or on a master page, has
		no page index and sorts to the end rather than being dropped - it is still a real edit.

		Reads only. Nothing here composes, which keeps the property stage 1 measured and wrote into
		KESCMStoryStamp.h:42-43: looking at what changed costs no recomposition.

		@param targetDB the newer document. nil clears the list.
		@param diffs what KESCMStoryEdits::Compare produced for this comparison.
	*/
	void Build(IDataBase* targetDB, const std::vector<KESCMStoryDiff>& diffs);

	/** Empty the list. Called on Stop and when a compared document closes. */
	void Clear();

	int32 GetRowCount();

	/** The nth row, or nil when nth is out of range. Callers get a pointer rather than a reference
		so that an index the tree asks for after the list was rebuilt cannot walk off the end.
	*/
	const KESCMStoryRow* GetRow(int32 nth);

	/** Empty the list during a controlled shutdown. See the file comment for why this exists. */
	void ShutdownCleanup();
}

#endif // __KESCMStoryList_h__

// End, KESCMStoryList.h.
