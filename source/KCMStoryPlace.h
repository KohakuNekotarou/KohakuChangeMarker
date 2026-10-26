//========================================================================================
//
//  KCMStoryPlace.h
//
//  Stories the UIDs could not pair, paired by the PLACE of their first frame (2026-10-26, the
//  author: "a frame copied from a past document gets a new UID - brought over and put in the same
//  place"). The rule is KCMStoryPlacePairRule.h; this reads what it is handed - each unpaired
//  story's page and box - and merges each pair it finds into one row.
//
//  ★ONE CALLER: KCMBuildStoryRowsByDiff (KCMCore.cpp), right after the counters' comparison - the
//  one place the Story mode's rows are built, for the panel and for the Before/After report alike
//  ([[one-question-one-place]]).
//
//  ★READS ONLY, AND COMPOSES NOTHING: the frames' geometry and the page pairing are asked as they
//  stand. An anchored or inline frame's place is a result of composition, so in a document not
//  composed yet it can be stale - it then simply fails to pair, the safe way round (design 2-2 #10).
//
//  Design: docs/superpowers/specs/2026-10-26-kcm-story-pair-by-frame-place-design.md.
//
//========================================================================================
#ifndef __KCMStoryPlace_h__
#define __KCMStoryPlace_h__

#include "PMRect.h"
#include "UIDRef.h"

#include <vector>

#include "KCMStoryStamp.h"	// KCMStoryDiff

class IDataBase;

/** Where a story's first frame stands: the page it is on, its box on that page, and whether the story runs ON the
	frame's path or inside it.

	@param db the document that holds the story.
	@param storyUID the story.
	@param outPage [out] the page of the story's first text column (KCMFramePageUID - the page a Story row names).
	@param outBox [out] the box of the item that holds that column - the frame the reader sees, found by the SDK's two
		steps (ITOPFrameData::GetMainSplineItemUID for text on a path, else ITextUtils::QuerySplineFromTextFrame) - as its
		PATH bounds (no stroke - the script's geometricBounds) in the page's own coordinates, measured from the page's
		top-left corner. Through one matrix (the item's inner to the pasteboard, then the pasteboard to the page's inner),
		so a rotated frame is boxed tight, as the script boxes it.
	@param outOnPath [out] kTrue for words on a path (the column is the path's - ITOPFrameData), kFalse for words inside a
		frame. ★ONE FRAME CAN CARRY ONE STORY OF EACH (2026-10-26, the final review - tests-place.ps1 P20): both stand at
		the frame's box, so with the box alone they were two equal places on the page and neither paired. The pairing tells
		them apart by this: inside pairs with inside, on the path with on the path.
	@return kFalse when the story is not there, sits in no frame, its first frame is on no page (the pasteboard), or the
		geometry cannot be read. The outs are left alone then. */
bool16 KCMStoryPlaceKey(IDataBase* db, UID storyUID, UID& outPage, PMRect& outBox, bool16& outOnPath);

/** Pair the Added (Target) and Removed (Source) diffs whose first frames stand at exactly the same place and size on
	corresponding pages, one to one both ways, and make each pair ONE diff: the Added one keeps the Target's story in
	fStoryUID, takes the Source's in fSourceStoryUID and becomes kKCMStoryKindByPosition alone; the Removed one goes.

	★"Corresponding pages" are the comparison's own page pairing, read once here (KCMBuildPairing - by UID, or in order
	  with "Pair Pages by UID" off, the registered pages out - and KCMBuildMasterPairing, masters by name): a Target story
	  on a page with no partner is not offered at all.
	★The order of the diffs is left to KCMStoryList::Build, which sorts.
	@param targetDB / sourceDB the two documents. A nil one, or a list with no Added or no Removed diff, does nothing.
	@param diffs what KCMStoryEdits::Compare made, changed in place. */
void KCMStoryPairByPlace(IDataBase* targetDB, IDataBase* sourceDB, std::vector<KCMStoryDiff>& diffs);

#endif // __KCMStoryPlace_h__

// End, KCMStoryPlace.h.
