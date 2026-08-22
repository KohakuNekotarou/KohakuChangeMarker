//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryPressMarks.h for what this shows and why.
//
//  Everything here is a READ of the Story Edits list plus one call to the marker. Nothing is
//  cached between refreshes: the list can be rebuilt by a comparison, a refresh or a row's own
//  right-click menu at any moment, and a cache of ranges would go stale silently - the marks
//  would keep pointing at where the words used to be. Rebuilding costs a few thousand integers
//  on a document with a few thousand edits, which is not worth being wrong for.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITextModel.h"					// TotalLength - how far a whole-story mark reaches

// General includes:
#include "PMReal.h"
#include "UIDRef.h"
#include "Utils.h"

// Project includes:
#include "IKESCMCompareFacade.h"		// armed state, the two databases, the toggles, the opacity
#include "IKESCMStoryEditsFacade.h"		// the rows and their changes - what is marked
#include "KESCMBoundaryID.h"			// kKESCMModeStory
#include "KESCMStoryMarker.h"			// the adornment that draws them
#include "KESCMStoryPressMarks.h"

namespace
{

// Is the tool's button down, and over which window? ⚠Not "is anything showing" - the toggles can
// be holding marks up with no button down at all.
bool16 gPressActive = kFalse;
bool16 gPressUseSource = kFalse;

// ⚠★★"IS WHAT IS ON SCREEN MINE" IS NOT REMEMBERED HERE - IT IS ASKED
//   (KESCMStoryMarker::IsShowingPersistent, 2026-08-22 bug recheck A2). This file used to keep its
//   own flag for it, which was the same fact written down in two places
//   ([[one-question-one-place]]) - and the copy here went stale the moment anything ELSE took the
//   mark down: the double click does exactly that (KESCMStorySelectChange), after which this file
//   still believed a standing mark was up.

/* KESCMStoryWholeTextEnd
   One past the last character a reader can see in this story - what an Added or Removed story is
   marked from 0 to.

   ⚠THE STORY'S OWN LENGTH IS ONE MORE THAN THAT. ITextModel.h:55-56 is explicit that TotalLength
   counts "the non-editable, must-have carriage return at the end", and a paragraph mark has no
   glyph to invert. Including it would stretch the range past the last real character for nothing.
   ★The rest of what TotalLength counts IS wanted: it includes the text of embedded tables
   (ITextModel.h:138), and in a story that is entirely new the table's words are new as well.

   @return kFalse for a story that is not there or holds nothing but that final return.
*/
bool16 KESCMStoryWholeTextEnd(IDataBase* db, UID storyUID, TextIndex& outEnd)
{
	outEnd = 0;
	if (db == nil || storyUID == kInvalidUID)
		return kFalse;

	InterfacePtr<ITextModel> model(UIDRef(db, storyUID), UseDefaultIID());
	if (model == nil)
		return kFalse;

	const TextIndex total = model->TotalLength();
	outEnd = (total > 1) ? (total - 1) : 0;
	return (outEnd > 0) ? kTrue : kFalse;
}

/* KESCMStoryCollectRanges
   Every edit that is visible in ONE of the two documents, as ranges per story.

   @param db the document to read the ranges out of.
   @param useSourceDocument kTrue when db is the older one.
   @param out [out] cleared, then filled. Empty when nothing in this document is markable.
*/
void KESCMStoryCollectRanges(IDataBase* db, bool16 useSourceDocument, KESCMStoryMarkMap& out)
{
	out.clear();
	if (db == nil)
		return;

	const int32 rowCount = Utils<IKESCMStoryEditsFacade>()->GetRowCount();
	for (int32 n = 0; n < rowCount; ++n)
	{
		IKESCMStoryEditsFacade::Row row;
		if (!Utils<IKESCMStoryEditsFacade>()->GetRow(n, row))
			continue;
		if (row.fStoryUID == kInvalidUID)
			continue;

		// ★★WHICH ROWS EXIST IN THE DOCUMENT BEING ASKED FOR. An ADDED story is only in the newer
		//   version and a REMOVED one only in the older, so each is skipped on the side it is not
		//   on. ⇒ **Everything else is in BOTH**, and is marked in whichever one is being asked for.
		//
		// ★★★THE SAME UID NAMES THE SAME STORY IN BOTH DOCUMENTS. That is the premise the whole
		//   feature stands on - the two are versions of one document, and saving under a new name
		//   carries the UIDs across (KESCMStoryStamp.h, "WHY TWO VERSIONS CAN BE MATCHED AT ALL").
		//   Two other places already ask exactly this way, which is the check that this is not a
		//   local assumption: the diff reads the older side with UIDRef(sourceDB, row->fStoryUID)
		//   (KESCMStoryDiffRun::Run), and the double click selects with it (KESCMStoryJump.cpp).
		//
		// ⚠★★★THIS USED TO ASK "is this a removed row?" AND REQUIRE THAT TO **MATCH**
		//   useSourceDocument, which quietly dropped every ORDINARY row from the source side
		//   (2026-08-22, found by the user at the running application, not by this file's own
		//   reasoning). Two things were broken by it and both were invisible from here:
		//     * holding the button over the older window marked nothing at all, unless a whole
		//       story happened to have been deleted;
		//     * ★a DELETION had nowhere left to be shown correctly - the characters survive only
		//       in the older version, so the source side is the ONLY side that can show one. What
		//       the reader got instead was the target side's stand-in (see the "to = from + 1"
		//       note below), which lights the character that closed up over the gap.
		//   ⇒ The comment that stood here argued from "a uid means a different object in the other
		//     document", which is true in general and false for this feature. ★An argument that
		//     proves too much is worth testing against what the neighbours actually do.
		const bool16 addedRow   = ((row.fKinds & kKESCMStoryKindAdded)   != 0) ? kTrue : kFalse;
		const bool16 removedRow = ((row.fKinds & kKESCMStoryKindRemoved) != 0) ? kTrue : kFalse;
		if (useSourceDocument ? addedRow : removedRow)
			continue;

		const int32 changeCount = Utils<IKESCMStoryEditsFacade>()->GetChangeCount(n);

		KESCMMarkRangeList ranges;

		if (changeCount <= 0)
		{
			// ★★AN UNPAIRED STORY IS MARKED WHOLE (user's request, 2026-08-22: "AddされたStoryは、
			//   全テキストになりそうですがマーク出せます？"). An Added story has no partner in the
			//   older version and a Removed one has none in the newer, so no text diff was ever run
			//   for either - which is why they arrive here with no changes at all
			//   (KESCMStoryStamp.h, kKESCMStoryKindUnpaired). Every character of them is new, or
			//   gone, so every character is the answer to "what changed".
			// ⚠THE OTHER TWO REASONS FOR AN EMPTY LIST ARE NOT THIS ONE, and must not be marked
			//   whole: the diff refused the story (it ran out, or the length check failed) or it
			//   ran and the WORDS AGREE - only formatting or a table moved. Lighting up the whole
			//   story would claim to know something in the first case and would be plainly wrong
			//   in the second (IKESCMStoryEditsFacade.h names all three).
			// ★Asked as Unpaired rather than as Added, which is this plug-in's own rule: the two
			//   kinds differ only in WHICH document holds the story, and that has already been
			//   settled by the row filter above (KESCMStoryStamp.h:117-127).
			if ((row.fKinds & kKESCMStoryKindUnpaired) == 0)
				continue;

			TextIndex wholeEnd = 0;
			if (!KESCMStoryWholeTextEnd(db, row.fStoryUID, wholeEnd))
				continue;

			ranges.push_back(KESCMMarkRange(0, wholeEnd));
		}
		else
			ranges.reserve(changeCount);

		for (int32 i = 0; i < changeCount; ++i)		// no changes = not entered; the whole range is already in
		{
			IKESCMStoryEditsFacade::Change change;
			if (!Utils<IKESCMStoryEditsFacade>()->GetChange(n, i, change))
				continue;

			TextIndex from = 0;
			TextIndex to = 0;
			if (useSourceDocument)
			{
				// ⚠AN INSERTION HAS NO PLACE HERE. fHasSource is kFalse for one precisely because
				//   there is nothing in the older version to point at (IKESCMStoryEditsFacade.h).
				if (!change.fHasSource)
					continue;
				from = change.fSourceStart;
				to = change.fSourceEnd;
			}
			else
			{
				from = change.fTargetStart;
				to = change.fTargetEnd;
			}

			if (to < from)
				continue;
			if (to == from)
			{
				// ★★★A DELETION HAS NO WIDTH on the side it was deleted from, and since 2026-08-22
				//   it is shown as a CARET (user's call: "細いバーにするがいいです、キャレットの位置で").
				//   ⚠It used to be widened to one character, which inverted whatever had closed up
				//     over the gap - a DIFFERENT character claiming to be the edit. Two ordinary
				//     cases showed how wrong that reads: deleting a whole paragraph lit the first
				//     character of the next one, and deleting the end of a story lit the final
				//     carriage return, which draws nothing at all.
				//   ★The range still covers one character so that it sorts and merges like every
				//     other one; the flag is all the drawing side needs (KESCMStoryMarkRanges.h).
				//   ★The jump's one-shot marker answers the same way, so the two agree about what a
				//     deletion looks like (KESCMStoryMarker::Show).
				ranges.push_back(KESCMMarkRange::Caret(from));
				continue;
			}

			ranges.push_back(KESCMMarkRange(from, to));
		}

		if (!ranges.empty())
		{
			// ⚠APPENDED, NOT ASSIGNED. Two rows naming the same story in the same document should
			//   not happen - the list holds one row per story per side - but if it ever did, an
			//   assignment would silently throw the first one's edits away. Appending cannot: the
			//   marker merges each story's list before it draws anything.
			KESCMMarkRangeList& dst = out[row.fStoryUID];
			dst.insert(dst.end(), ranges.begin(), ranges.end());
		}
	}

	// ⚠THE RANGES ARE NOT CLAMPED TO THE STORY AS IT STANDS NOW, and the jump's are. The jump makes
	//   a selection, which a stale index would have the suite refuse; this only asks the text engine
	//   "is any of this run marked", and a range past the end of the story simply never meets a run.
	//   Reading every story's length to clamp them would cost a Query per row for no answer.
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------

void KESCMStoryMarksRefresh()
{
	KESCMStoryMarkDocs docs;
	PMReal opacity(1.0);

	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	if (compare != nil && compare->IsArmed() && compare->GetCompareMode() == kKESCMModeStory)
	{
		opacity = compare->GetSelectedMarkOpacity();

		// ★★★A PRESS TURNS ITS OWN WINDOW ROUND. IT DOES NOT ADD TO THE TOGGLE (2026-08-22, user's
		//   call). One rule now covers the whole plug-in: **while the button is held, that window is
		//   the other way round** - marks that were off come on, and marks that were on go off, which
		//   is what lets the reader look at the plain page underneath the ones they asked to keep.
		//   ⇒ XOR, where this was OR until "Hold to Hide Marks" was retired (that toggle used to own
		//     the "hide while held" half, and its other half duplicated Show Marks on Target).
		//   ★The Pixel mode's frames follow the same rule, but spelt out in two places rather than
		//     one, because they are drawn by the draw event: the toggle puts them up (alwaysScreen in
		//     KESCMDrawEventHandler) and the press takes them down (sMarksTempHidden, set in
		//     KESCMPeekGesture). Here both halves are this one expression.
		//   ⚠A press is over ONE window, so the other window's toggle is left exactly as it is.
		//   ⚠Written as (a != 0) != (b != 0) rather than a != b: bool16 is an integer type, and any
		//     non-zero truth other than kTrue would make a bare != answer the wrong way round.
		const bool16 pressTarget = (gPressActive && !gPressUseSource) ? kTrue : kFalse;
		const bool16 pressSource = (gPressActive && gPressUseSource) ? kTrue : kFalse;
		const bool16 wantTarget = ((compare->GetShowTargetMarks() != 0) != (pressTarget != 0)) ? kTrue : kFalse;
		const bool16 wantSource = ((compare->GetShowSourceMarks() != 0) != (pressSource != 0)) ? kTrue : kFalse;

		IDataBase* const targetDB = compare->GetArmedTargetDB();
		IDataBase* const sourceDB = compare->GetArmedSourceDB();

		if (wantTarget && targetDB != nil && compare->IsDocDBOpen(targetDB))
		{
			KESCMStoryMarkMap byStory;
			KESCMStoryCollectRanges(targetDB, kFalse, byStory);
			if (!byStory.empty())
				docs[targetDB].swap(byStory);		// ⚠only when non-empty: an empty entry would
		}											//   make docs look occupied and clear nothing

		if (wantSource && sourceDB != nil && compare->IsDocDBOpen(sourceDB))
		{
			KESCMStoryMarkMap byStory;
			KESCMStoryCollectRanges(sourceDB, kTrue, byStory);
			if (!byStory.empty())
				docs[sourceDB].swap(byStory);
		}
	}

	if (docs.empty())
	{
		// ★ONLY TAKE DOWN A STANDING MARK. A jump's pointer may be on screen, and it is not ours to
		//   clear - the two share one adornment and are exclusive (KESCMStoryMarker.h).
		//   ★The mark is asked rather than remembered; see the note on the statics above.
		if (KESCMStoryMarker::IsShowingPersistent())
			KESCMStoryMarker::Clear();
		return;
	}

	KESCMStoryMarker::ShowDocs(docs, opacity);
}

void KESCMStoryPressMarksBegin(bool16 useSourceDocument)
{
	gPressActive = kTrue;
	gPressUseSource = useSourceDocument;
	KESCMStoryMarksRefresh();
}

void KESCMStoryPressMarksEnd()
{
	if (!gPressActive)
		return;					// nothing was pressed - leave a jump's marker, and the toggles, alone

	gPressActive = kFalse;
	KESCMStoryMarksRefresh();	// what the toggles asked for comes back; the rest goes
}

// End, KESCMStoryPressMarks.cpp.
