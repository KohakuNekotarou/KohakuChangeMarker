//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryMarkBuild.h for what this shows and why, and ui/KCMStoryPressMarks.h for the thin
//  layer the UI still calls (this file was that layer's other half until the adornment moved to
//  the model plug-in so that the marks could reach paper and PDF).
//
//  Everything here is a READ of the Story Edits list plus one call to the marker. Nothing is
//  cached between refreshes: the list can be rebuilt by a comparison, a refresh or a row's own
//  right-click menu at any moment, and a cache of ranges would go stale silently -- the marks
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
#include "IKCMCompareFacade.h"		// armed state, the two databases, the toggles, the opacity
#include "IKCMStoryEditsFacade.h"		// the rows and their changes - what is marked
#include "KCMBoundaryID.h"			// kKCMModeStory
#include "KCMCore.h"					// KCMArmedTargetDB / KCMArmedSourceDB (@warning declared here, not in KCMPeek.h)
#include "KCMDrawEventHandler.h"		// sPrintMarks / sSrcMarksOn - the two toggles that decide printing
#include "KCMStoryMarkBuild.h"
#include "KCMStoryMarker.h"			// the adornment that draws them
#include "KCMThreadSafety.h"			// KCMIsSameDoc - the background draws a CLONE of the document

namespace
{

// Is the tool's button down, and over which window? @warning NOT "is anything showing" -- the
// toggles can be holding marks up with no button down at all.
bool16 gPressActive = kFalse;
bool16 gPressUseSource = kFalse;

// @warning **"IS WHAT IS ON SCREEN MINE" IS NEITHER REMEMBERED NOR ASKED HERE -- IT IS OWNED.**
//   The marker keeps the standing set and the jump's flash apart, so this file takes down what
//   it put up by name (ClearStanding) and cannot reach the other.
//   The history is worth keeping because the same mistake is easy to make again: this file
//     first kept its own flag for "did I put the current mark up", which was one fact written
//     down in two places ([[one-question-one-place]]) and went stale the moment anything ELSE
//     took the mark down -- the double click does exactly that (KCMStorySelectChange), after
//     which this file still believed a standing mark was on screen (bug A2). Asking the marker
//     fixed the staleness; separating the two sets removed the question.

/* KCMStoryWholeTextEnd
   One past the last character a reader can see in this story - what an Added or Removed story is
   marked from 0 to.

   @warning **THE STORY'S OWN LENGTH IS ONE MORE THAN THAT.** ITextModel is explicit that
    TotalLength counts "the non-editable, must-have carriage return at the end", and a paragraph
    mark has no glyph to mark. Including it would stretch the range past the last real character
    for nothing.
   The rest of what TotalLength counts IS wanted: it includes the text of embedded tables, and in
   a story that is entirely new the table's words are new as well.

   @return kFalse for a story that is not there or holds nothing but that final return.
*/
bool16 KCMStoryWholeTextEnd(IDataBase* db, UID storyUID, TextIndex& outEnd)
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

/* KCMStoryCollectRanges
   Every edit that is visible in ONE of the two documents, as ranges per story.

   @param db the document to read the ranges out of.
   @param useSourceDocument kTrue when db is the older one.
   @param out [out] cleared, then filled. Empty when nothing in this document is markable.
*/
void KCMStoryCollectRanges(IDataBase* db, bool16 useSourceDocument, KCMStoryMarkMap& out)
{
	out.clear();
	if (db == nil)
		return;

	// **HELD FOR THE WHOLE LOOP, NOT ASKED FOR PER ROW.** Every Utils<IFace>() written out is its
	//   own Query off the Utils boss -- the constructor calls UtilsBoss::QueryUtils and the
	//   destructor Releases (Utils.h:87-89) -- and this loop asks for a row, its change count and
	//   every one of its changes. On a document with a few thousand edits that was a few thousand
	//   Queries, and the whole of it runs again on every press and every refresh. Two other files
	//   here already hold it across a loop (KCMStoryNav.cpp, KCMChangeNav.cpp).
	// @warning **asked without a nil test, where KCMStoryMarkRefresh below tests its own facade**
	//   -- and the two are not in disagreement. That one is the entry point and can be reached from
	//   a model notification during teardown; **this is only ever reached after it has already got
	//   IKCMCompareFacade off kUtilsBoss**, which answers the same question for the whole boss.
	//   Testing again here would be the second place one fact is written down.
	InterfacePtr<IKCMStoryEditsFacade> edits(Utils<IKCMStoryEditsFacade>().QueryUtilInterface());

	const int32 rowCount = edits->GetRowCount();
	for (int32 n = 0; n < rowCount; ++n)
	{
		IKCMStoryEditsFacade::Row row;
		if (!edits->GetRow(n, row))
			continue;
		if (row.fStoryUID == kInvalidUID)
			continue;

		// **WHICH ROWS EXIST IN THE DOCUMENT BEING ASKED FOR.** An ADDED story is only in the newer
		//   version and a REMOVED one only in the older, so each is skipped on the side it is not on.
		//   **Everything else is in BOTH**, and is marked in whichever one is being asked for.
		//
		// **THE SAME UID NAMES THE SAME STORY IN BOTH DOCUMENTS.** That is the premise the whole
		//   feature stands on -- the two are versions of one document, and saving under a new name
		//   carries the UIDs across (KCMStoryStamp.h, "WHY TWO VERSIONS CAN BE MATCHED AT ALL").
		//   Two other places already ask exactly this way, which is the check that this is not a local
		//   assumption: the diff reads the older side with UIDRef(sourceDB, row->fStoryUID)
		//   (KCMStoryDiffRun::Run), and the double click selects with it (KCMStoryJump.cpp).
		//
		// @warning **this used to ask "is this a removed row?" and require that to MATCH
		//   useSourceDocument**, which quietly dropped every ORDINARY row from the source side -- found
		//   by the reader at the running application, not by this file's own reasoning. Two things were
		//   broken by it and both were invisible from here:
		//     * holding the button over the older window marked nothing at all, unless a whole story
		//       happened to have been deleted;
		//     * a DELETION had nowhere left to be shown correctly -- the characters survive only in the
		//       older version, so the source side is the ONLY side that can show one. What the reader
		//       got instead was the target side's stand-in (see the "to == from" note below), which
		//       lights the character that closed up over the gap.
		//   The comment that stood here argued from "a uid means a different object in the other
		//     document", which is true in general and false for this feature. **An argument that proves
		//     too much is worth testing against what the neighbours actually do.**
		const bool16 addedRow   = ((row.fKinds & kKCMStoryKindAdded)   != 0) ? kTrue : kFalse;
		const bool16 removedRow = ((row.fKinds & kKCMStoryKindRemoved) != 0) ? kTrue : kFalse;
		if (useSourceDocument ? addedRow : removedRow)
			continue;

		const int32 changeCount = edits->GetChangeCount(n);

		KCMMarkRangeList ranges;

		if (changeCount <= 0)
		{
			// **AN UNPAIRED STORY IS MARKED WHOLE.** An Added story has no partner in the older version
			//   and a Removed one has none in the newer, so no text diff was ever run for either -- which
			//   is why they arrive here with no changes at all (KCMStoryStamp.h, kKCMStoryKindUnpaired).
			//   Every character of them is new, or gone, so every character is the answer to "what
			//   changed".
			// @warning **the other two reasons for an empty list are not this one**, and must not be
			//   marked whole: the diff refused the story (it ran out, or the length check failed) or it
			//   ran and the WORDS AGREE -- only formatting or a table moved. Lighting up the whole story
			//   would claim to know something in the first case and would be plainly wrong in the second
			//   (IKCMStoryEditsFacade.h names all three).
			// Asked as Unpaired rather than as Added, which is this plug-in's own rule: the two kinds
			//   differ only in WHICH document holds the story, and that has already been settled by the
			//   row filter above (KCMStoryStamp.h, at kKCMStoryKindUnpaired).
			if ((row.fKinds & kKCMStoryKindUnpaired) == 0)
				continue;

			TextIndex wholeEnd = 0;
			if (!KCMStoryWholeTextEnd(db, row.fStoryUID, wholeEnd))
				continue;

			ranges.push_back(KCMMarkRange(0, wholeEnd));
		}
		else
			ranges.reserve(changeCount);

		for (int32 i = 0; i < changeCount; ++i)		// no changes = not entered; the whole range is already in
		{
			IKCMStoryEditsFacade::Change change;
			if (!edits->GetChange(n, i, change))
				continue;

			TextIndex from = 0;
			TextIndex to = 0;
			if (useSourceDocument)
			{
				// **AN INSERTION MARKS ITS PLACE HERE, AS A CARET.** It used to be skipped, because the
				//   older side was said to hold nothing to point at -- true of characters, false of the
				//   place. The range comes through EMPTY for one, and the zero-width branch below turns
				//   that into a caret without being told, so this needed nothing but the removal of the
				//   guard.
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
				// **A DELETION HAS NO WIDTH on the side it was deleted from**, and it is shown as a CARET.
				//   @warning it used to be widened to one character, which inverted whatever had closed up
				//     over the gap -- a DIFFERENT character claiming to be the edit. Two ordinary cases
				//     showed how wrong that reads: deleting a whole paragraph lit the first character of the
				//     next one, and deleting the end of a story lit the final carriage return, which draws
				//     nothing at all.
				//   The range still covers one character so that it sorts and merges like every other one;
				//     the flag is all the drawing side needs (KCMStoryMarkRanges.h).
				//   The jump's flash answers the same way (KCMStoryMarker::AddFlashRange), so the two agree
				//     about what a deletion looks like.
				ranges.push_back(KCMMarkRange::Caret(from));
				continue;
			}

			ranges.push_back(KCMMarkRange(from, to));
		}

		if (!ranges.empty())
		{
			// @warning **APPENDED, NOT ASSIGNED.** Two rows naming the same story in the same document
			//   should not happen -- the list holds one row per story per side -- but if it ever did, an
			//   assignment would silently throw the first one's edits away. Appending cannot: the marker
			//   merges each story's list before it draws anything.
			KCMMarkRangeList& dst = out[row.fStoryUID];
			dst.insert(dst.end(), ranges.begin(), ranges.end());
		}
	}

	// @warning **the ranges are not clamped to the story as it stands now**, and the jump's are.
	//   The jump makes a selection, which a stale index would have the suite refuse; this only asks
	//   the text engine "is any of this run marked", and a range past the end of the story simply
	//   never meets a run. Reading every story's length to clamp them would cost a Query per row
	//   for no answer.
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------

void KCMStoryMarkRefresh()
{
	KCMStoryMarkDocs docs;

	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare != nil && compare->IsArmed() && compare->GetCompareMode() == kKCMModeStory)
	{
		// @warning **the opacity is no longer read here.** It used to be picked up in this line and
		//   handed to the standing-mark call, which meant the jump's flash -- a different caller --
		//   never saw it. KCMStoryMarker asks for itself now, at the moment it draws.

		// **A PRESS TURNS ITS OWN WINDOW ROUND. IT DOES NOT ADD TO THE TOGGLE.** One rule covers the
		//   whole plug-in: **while the button is held, that window is the other way round** -- marks
		//   that were off come on, and marks that were on go off, which is what lets the reader look at
		//   the plain page underneath the ones they asked to keep.
		//   So XOR, where this was OR until "Hold to Hide Marks" was retired (that toggle used to own
		//     the "hide while held" half, and its other half duplicated Always Show Marks on Target).
		//   The Pixel mode's frames follow the same rule, but spelt out in two places rather than one,
		//     because they are drawn by the draw event: the toggle puts them up (alwaysScreen in
		//     KCMDrawEventHandler) and the press takes them down (sMarksTempHidden, set in
		//     KCMPeekGesture). Here both halves are this one expression.
		//   @warning a press is over ONE window, so the other window's toggle is left exactly as it is.
		//   @warning written as (a != 0) != (b != 0) rather than a != b: bool16 is an integer type, and
		//     any non-zero truth other than kTrue would make a bare != answer the wrong way round.
		const bool16 pressTarget = (gPressActive && !gPressUseSource) ? kTrue : kFalse;
		const bool16 pressSource = (gPressActive && gPressUseSource) ? kTrue : kFalse;
		// **"Print comparison marks" ALSO PUTS THEM UP ON SCREEN**, and this is what makes printing
		//   possible at all. The Pixel mode has always worked this way -- its wantMarks is
		//   `(sPrintMarks || sMarksVisible || ...)`, so turning the print toggle on shows the frames
		//   permanently as well (KCMDrawEventHandler, where wantMarks is worked out). WYSIWYG: what
		//   will come out is what you are looking at.
		// @warning **it is also a necessity, not a choice.** What gets drawn onto paper is drawn from
		//   this same set -- so "on paper but not on screen" would mean keeping a second set of ranges,
		//   which is one question answered in two places ([[one-question-one-place]]). The screen and
		//   the page agree because there is only ever one answer to draw from.
		// @warning **the Source side does NOT read the print toggle** -- "Always Show Marks on Source"
		//   decides both its screen and its paper, which is the specification in IKCMCompareFacade.h,
		//   where GetShowSourceMarks is declared.
		const bool16 tgtWanted = (compare->GetShowTargetMarks() || compare->GetPrintMarks()) ? kTrue : kFalse;
		const bool16 wantTarget = ((tgtWanted != 0) != (pressTarget != 0)) ? kTrue : kFalse;
		const bool16 wantSource = ((compare->GetShowSourceMarks() != 0) != (pressSource != 0)) ? kTrue : kFalse;

		IDataBase* const targetDB = compare->GetArmedTargetDB();
		IDataBase* const sourceDB = compare->GetArmedSourceDB();

		if (wantTarget && targetDB != nil && compare->IsDocDBOpen(targetDB))
		{
			KCMStoryMarkMap byStory;
			KCMStoryCollectRanges(targetDB, kFalse, byStory);
			if (!byStory.empty())
				docs[targetDB].swap(byStory);		// @warning only when non-empty: an empty entry would
		}											//   make docs look occupied and clear nothing

		if (wantSource && sourceDB != nil && compare->IsDocDBOpen(sourceDB))
		{
			KCMStoryMarkMap byStory;
			KCMStoryCollectRanges(sourceDB, kTrue, byStory);
			if (!byStory.empty())
				docs[sourceDB].swap(byStory);
		}
	}

	if (docs.empty())
	{
		// ONLY TAKE DOWN A STANDING MARK. A jump's pointer may be on screen and it is not ours to
		//   clear -- and saying so is the whole of it: there is no test to get right, because the call
		//   names which of the two kinds is coming down (KCMStoryMarker.h).
		KCMStoryMarker::ClearStanding();
		return;
	}

	KCMStoryMarker::ShowStanding(docs);
}

void KCMStoryMarkSetPress(bool16 active, bool16 useSourceDocument)
{
	// @warning **a release that nobody pressed does nothing** -- it must not go and rebuild the
	//   marks, because the button also comes up after gestures this file never heard the start of
	//   (Pixel mode, the CMYK sampler, a peek). It is also what stops a release from taking down a
	//   jump's marker.
	if (!active && !gPressActive)
		return;

	gPressActive = active;

	// WHICH WINDOW IS ONLY RECORDED ON THE WAY DOWN. A release does not carry the answer -- the
	//   cursor may have left the window by then -- so the press keeps its own.
	if (active)
		gPressUseSource = useSourceDocument;

	KCMStoryMarkRefresh();
}

bool16 KCMStoryMarkPrintAllowedFor(IDataBase* db)
{
	if (db == nil)
		return kFalse;

	// ONE PLACE ANSWERS THIS, AND BOTH GetIsActive AND Draw ASK IT (KCMStoryMarker.cpp). The
	//   adornment is consulted per parcel and per run, so an answer that drifted between the two
	//   would show as marks that half-print.
	// @warning **read directly, not through the facade:** this runs on the export's background
	//   thread, where a Query off kUtilsBoss per parcel is a cost with nothing to buy. The Pixel
	//   side reads the same two flags the same way (KCMRingAdornment.cpp,
	//   KCMMarksCouldBeTranslucent).
	// @warning **KCMIsSameDoc, NOT ==:** an asynchronous export hands the drawing a CLONE of the
	//   database, so the pointer never matches the one we armed with (KCMThreadSafety.h has the
	//   measurement).

	if (KCMDrawEventHandler::sPrintMarks && KCMIsSameDoc(db, KCMArmedTargetDB()))
		return kTrue;

	// THE OLDER DOCUMENT IGNORES THE PRINT TOGGLE -- "Always Show Marks on Source" decides its
	//   screen and its paper together. Asymmetric on purpose, and stated as the specification in
	//   IKCMCompareFacade.h (at GetShowSourceMarks) long before the Story mode could print at all.
	if (KCMDrawEventHandler::sSrcMarksOn && KCMIsSameDoc(db, KCMArmedSourceDB()))
		return kTrue;

	return kFalse;
}

bool16 KCMStoryMarkPrintPossibleAtAll()
{
	// @warning **the same two flags as above, with the document test dropped** -- see the header
	//   for why the coarse form exists at all (GetIsActive is handed an IParcelShape, which is not
	//   an IPMUnknown and therefore has no database to ask about).
	return (KCMDrawEventHandler::sPrintMarks || KCMDrawEventHandler::sSrcMarksOn) ? kTrue : kFalse;
}

// End, KCMStoryMarkBuild.cpp.
