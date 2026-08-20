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
//  result list exactly this way (KBSResultModel's gChapters). The one obligation that comes with
//  it: ShutdownCleanup() has to empty it during a controlled shutdown, because these rows hold
//  PMStrings, and nothing of ours should still be holding storage when the DLL unloads.
//
//  ⚠KBS found FIVE statics doing exactly that, one at a time - not the three this note used to
//  claim. They are counted in ONE place, KBSReplaceConfirmDialog::ShutdownCleanup: gChangeText,
//  gSearchedFindAttrs, gLastStatus, KBSEditStamp's gPending and sMessage. The reference here used
//  to point at KBSResultModel's ShutdownCleanup, which explains only the first of the five.
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

/** One difference inside one story: where it is, what sort it is, and what it reads.

	★NEW WITH THE STORY CHANGES MODE (2026-08-20). A row of this list used to say only THAT a
	story changed, which is all the change counters in KESCMStoryStamp can answer. These say
	WHERE, which is what the text diff produces.

	★POSITIONS ARE KEPT FOR BOTH DOCUMENTS, and that is deliberate. Clicking a change moves the
	newer window to it and the older window to the matching place; both sides are worked out
	here, while the diff still has them (KESCMTextDiff::Change carries the a-side range as well
	as the b-side one). Working the older side out at click time would mean diffing again.
*/
struct KESCMStoryChange
{
	enum Kind { kReplace, kInsert, kDelete };

	/** What sort of thing changed.

		★ALWAYS kText TODAY. The diff reads <Content> and nothing else, so a change of formatting
		alone is not seen at all. The field exists so that attribute differences can arrive later
		as another sort of child WITHOUT having to change the row drawing, the click handling and
		the boundary struct at the same time. */
	enum What { kText, kAttr };

	Kind		fKind;
	What		fWhat;

	// ---- the newer document (Target) - what a click jumps to and selects ----
	TextIndex	fTargetStart;
	TextIndex	fTargetEnd;		// ★AN END, NOT A LENGTH (RangeData.h:69)

	// ---- the older document (Source) ----
	// fHasSource is kFalse for an insertion: there is nothing in the older version to point at,
	// and selecting "where it would have gone" would be selecting something that is not the
	// change. The older window simply does not move for those.
	TextIndex	fSourceStart;
	TextIndex	fSourceEnd;
	bool16		fHasSource;

	/** The words the row shows, in THREE PIECES: what stands before the change, the changed
		characters themselves, and what stands after.

		★WHICH SIDE THESE COME FROM DEPENDS ON THE KIND: the newer text for a replacement or an
		insertion, and the OLDER text for a deletion. What was removed is precisely what the
		reader needs to see, and the newer side has nothing there to show. (KohakuTest reported
		the newer side only and left deletions empty, which is correct for a script reading a
		report and useless in a panel - a column of blank rows.)

		★THREE RATHER THAN ONE SINCE 2026-08-20, so that the row can draw the change at full
		strength and fade the context around it - the way a KBS hit row draws its match (user's
		request: "変更されたところ以外は薄い色にして欲しい、KBSを参考に"). Concatenated they are
		exactly the one string this used to be.

		★THE SPLIT IS MADE WHERE THE INFORMATION IS. Which characters were the change is known in
		code points, inside a string that has already been cut at both ends and had its break
		characters replaced (KESCMStoryDiffRun's Slice). Handing the panel one string and an offset
		would ask it to count code points in a PMString, whose own index is UTF-16.

		★THE ELLIPSES BELONG TO THE CONTEXT PIECES. An ellipsis stands for words that were cut
		away, and those are always context, never the change - so a faded ellipsis is right. */
	PMString	fTextPre;
	PMString	fText;
	PMString	fTextPost;

	int32		fParaIndex;		// which paragraph it fell in. Not drawn; kept for ordering and for
								// anything later that wants to group changes by paragraph

	KESCMStoryChange()
		: fKind(kReplace), fWhat(kText), fTargetStart(0), fTargetEnd(0),
		  fSourceStart(0), fSourceEnd(0), fHasSource(kFalse), fParaIndex(0) {}
};

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

	/** The differences found inside this story, in reading order.

		★EMPTY IN THE PIXEL MODE, and that is what makes the tree flat there: the hierarchy
		adapter asks how many children a row has and gets 0, so no branch grows. Nothing has to
		switch trees between the modes (user's call, 2026-08-20).

		★ALSO EMPTY when the story could not be compared - it had no partner in the older
		document (an added story), or the edit distance ran past the limit, or the length check
		failed. The row still appears; only the detail is missing. A story that changed must
		never disappear because the detail could not be worked out. */
	std::vector<KESCMStoryChange> fChanges;

	KESCMStoryRow()
		: fStoryUID(kInvalidUID), fKinds(kKESCMStoryKindNone), fFrameUID(kInvalidUID),
		  fPageUID(kInvalidUID), fPageIndex(kMaxInt32) {}
};

/** The first frame a story is placed in - where a jump to that story should go.

	★TWO DOCUMENTS ASK THIS, WHICH IS WHY IT IS NOT PRIVATE TO THE LIST. Building the rows asks the
	target for it, and a click asks the SOURCE for the same story's frame, because the two versions
	can hold the story in DIFFERENT PLACES - the older window cannot be aimed by page number alone
	(user's observation, 2026-08-10). Matching by story UID works for the same reason the whole
	feature does: saving under a new name carries the UIDs across (KESCMStoryStamp.h, "WHY TWO
	VERSIONS CAN BE MATCHED AT ALL").

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

	★VERTICAL TEXT NEEDS NO SPECIAL CASE - AND THAT IS NOW MEASURED HERE, NOT INHERITED (2026-08-16,
	audit B7). The corner is taken in PARCEL-LOCAL coordinates and GetParcelToFrameMatrix absorbs the
	writing direction. Until then this claim rested on the overset scan's measurement of the OPPOSITE
	corner (the preamble to KESCMLastPlacedOutport in KESCMOversetScan.cpp, at the line that says the
	same formula holds for vertical text - the line numbers this used to give pointed at the
	paragraph above it, and moved again when audit B6 edited that file); it has now been measured for
	THIS corner. Two frames of
	identical size in one document, one vertical and one horizontal, with the point printed beside
	all four corners of its own frame in one coordinate space:
	    vertical   -> the point landed on the frame's TOP-RIGHT, where line 1 begins (DOM: h=146.75
	                  against a right edge of 150, baseline 22.86 against a top edge of 20)
	    horizontal -> the point landed on the frame's TOP-LEFT, where line 1 begins (DOM: h=20.00)

	★★AND THE FRAMES ARE NOT ROTATED - rotationAngle was 0 for both. It is the INNER COORDINATE SPACE
	of a vertical frame that is turned a quarter turn, so IGeometry's inner rectangle has its
	Left/Right running down the PAGE'S VERTICAL axis and its Top/Bottom across it (measured: the
	inner rectangle's Top-to-Bottom span came back as the frame's WIDTH). Anything that reads an
	inner rectangle and takes "Left" to mean "towards the left of the page" is wrong on vertical
	text. This file is safe because it never interprets a corner - it only hands corners to the
	matrices. ⚠Do not add a writing-direction branch here.

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
		KESCMStoryStamp.h ("READING COUNTERS COMPOSES NOTHING"): looking at what changed costs no
		recomposition.

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

	/** Give row nth the differences the text diff found inside it (Story Changes mode).

		★A SEPARATE STEP FROM Build, ON PURPOSE. Build orders the rows by page, and a change
		names its row by position in that finished order - so the diff runs after the ordering,
		not inside it. Doing both at once would mean the diff had to know the sort key.

		★THE ONLY WAY TO WRITE A ROW. GetRow hands out a const pointer precisely so that nothing
		can edit the list behind its back; this is the one door, and it is the one KESCMStoryDiffRun
		knocks on.

		@param nth the row. Out of range does nothing - the list may have been rebuilt underneath
			a caller that is still walking the previous one.
		@param changes what to attach. Copied; the caller keeps ownership of its own vector.
	*/
	void SetRowChanges(int32 nth, const std::vector<KESCMStoryChange>& changes);

	/** Empty the list during a controlled shutdown. See the file comment for why this exists. */
	void ShutdownCleanup();
}

#endif // __KESCMStoryList_h__

// End, KESCMStoryList.h.
