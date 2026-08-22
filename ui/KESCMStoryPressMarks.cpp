//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryPressMarks.h for what this shows and why.
//
//  Everything here is a READ of the Story Edits list plus one call to the marker. Nothing is
//  cached between presses: the list can be rebuilt by a comparison, a refresh or a row's own
//  right-click menu at any moment, and a cache of ranges would go stale silently - the marks
//  would keep pointing at where the words used to be. A press over a document with a few
//  thousand edits builds a few thousand integers, which is not worth being wrong for.
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
#include "IKESCMCompareFacade.h"		// armed state, the two databases, the opacity choice
#include "IKESCMStoryEditsFacade.h"		// the rows and their changes - what is marked
#include "KESCMBoundaryID.h"			// kKESCMModeStory
#include "KESCMStoryMarker.h"			// the adornment that draws them
#include "KESCMStoryPressMarks.h"

namespace
{

// Is this file what is currently on screen? ⚠Without it, a release in the PIXEL mode would take
// down a jump's marker that this file never put up - the release path cannot tell the modes apart
// and should not have to.
bool16 gPressShowing = kFalse;

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

}	// anonymous namespace

//----------------------------------------------------------------------------------------

bool16 KESCMStoryPressMarksBegin(bool16 useSourceDocument)
{
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	if (compare == nil || !compare->IsArmed())
		return kFalse;
	if (compare->GetCompareMode() != kKESCMModeStory)
		return kFalse;

	IDataBase* const db = useSourceDocument ? compare->GetArmedSourceDB() : compare->GetArmedTargetDB();
	if (db == nil || !compare->IsDocDBOpen(db))
		return kFalse;

	KESCMStoryMarkMap byStory;

	const int32 rowCount = Utils<IKESCMStoryEditsFacade>()->GetRowCount();
	for (int32 n = 0; n < rowCount; ++n)
	{
		IKESCMStoryEditsFacade::Row row;
		if (!Utils<IKESCMStoryEditsFacade>()->GetRow(n, row))
			continue;
		if (row.fStoryUID == kInvalidUID)
			continue;

		// ★WHICH DOCUMENT A ROW BELONGS TO IS ANSWERED BY ITS KIND, and by nothing else on the row
		//   (IKESCMStoryEditsFacade.h). A removed story exists only in the source; every other row
		//   was read out of the target. Getting this wrong would not draw nothing - a UID names a
		//   DIFFERENT object in the other document, so it would mark innocent text.
		const bool16 removedRow = ((row.fKinds & kKESCMStoryKindRemoved) != 0) ? kTrue : kFalse;
		if (removedRow != useSourceDocument)
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
				// A DELETION HAS NO WIDTH on the side it was deleted from. One character makes the
				// place it used to stand in front of visible - the same answer the jump's marker
				// gives (KESCMStoryMarker::Show), so the two agree about what a deletion looks like.
				to = from + 1;
			}

			ranges.push_back(KESCMMarkRange(from, to));
		}

		if (!ranges.empty())
		{
			// ⚠APPENDED, NOT ASSIGNED. Two rows naming the same story in the same document should
			//   not happen - the list holds one row per story per side - but if it ever did, an
			//   assignment would silently throw the first one's edits away. Appending cannot: the
			//   marker merges each story's list before it draws anything.
			KESCMMarkRangeList& dst = byStory[row.fStoryUID];
			dst.insert(dst.end(), ranges.begin(), ranges.end());
		}
	}

	if (byStory.empty())
		return kFalse;

	// ⚠THE RANGES ARE NOT CLAMPED TO THE STORY AS IT STANDS NOW, and the jump's are. The jump makes
	//   a selection, which a stale index would have the suite refuse; this only asks the text engine
	//   "is any of this run marked", and a range past the end of the story simply never meets a run.
	//   Reading every story's length to clamp them would cost a Query per row for no answer.
	KESCMStoryMarker::ShowRanges(db, byStory, compare->GetSelectedMarkOpacity());
	gPressShowing = kTrue;
	return kTrue;

	// ★WHAT IS DELIBERATELY NOT MARKED: a row whose diff produced nothing and which HAS a partner
	//   in the other version. The diff either refused it or ran and found the words identical, and
	//   neither of those is "all of this changed". The row list still names the story, which is
	//   where "something about this story moved, but not its words" belongs.
}

void KESCMStoryPressMarksEnd()
{
	if (!gPressShowing)
		return;

	gPressShowing = kFalse;
	KESCMStoryMarker::Clear();
}

// End, KESCMStoryPressMarks.cpp.
