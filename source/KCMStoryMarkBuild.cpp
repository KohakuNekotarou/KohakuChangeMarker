//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryMarkBuild.h for what this shows and why (nothing is cached between refreshes), and
//  ui/KCMStoryPressMarks.h for the thin layer the UI calls.
//
//  Everything here is a READ of the Story Edits list plus one call to the marker.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITextModel.h"					// TotalLength - how far a whole-story mark reaches

// General includes:
#include "UIDRef.h"
#include "Utils.h"
#include <utility>						// std::move - the set goes over to the marker

// Project includes:
#include "IKCMStoryEditsFacade.h"		// the rows and their changes - what is marked
#include "KCMBoundaryID.h"			// KCMModeUsesStoryRows
#include "KCMCore.h"					// armed state, the two databases, the mode (@warning declared here, not in KCMPeek.h)
#include "KCMDiag.h"					// KCM_DIAG_LOG - which story the Source side is marked under (test builds only)
#include "KCMDrawEventHandler.h"		// sPrintMarks, sTgtMarksOn, sSrcMarksOn - the toggles
#include "KCMStoryMarkBuild.h"
#include "KCMStoryMarker.h"			// the adornment that draws them
#include "KCMThreadSafety.h"			// KCMIsSameDoc - the background draws a CLONE of the document

namespace
{

// Is the tool's button down, and over which window? @warning NOT "is anything showing" -- the
// toggles can be holding marks up with no button down at all.
enum KCMStoryPress { kPressNone, kPressOverTarget, kPressOverSource };
KCMStoryPress gPress = kPressNone;

// @warning **"IS WHAT IS ON SCREEN MINE" IS NEITHER REMEMBERED NOR ASKED HERE -- IT IS OWNED.**
//   The marker keeps the standing set and the jump's flash apart, so this file takes down what
//   it put up by name (ShowStanding) and cannot reach the other.
//   Do not keep a flag here for "did I put the current mark up": that is one fact written down in two places, and it
//   goes stale the moment anything else takes the mark down.

/* KCMStoryWholeTextEnd
   One past the last character a reader can see in this story - what an Added or Removed story is
   marked from 0 to.

   @warning **THE STORY'S OWN LENGTH IS ONE MORE THAN THAT.** ITextModel is explicit that
    TotalLength counts "the non-editable, must-have carriage return at the end", and a paragraph
    mark has no glyph to mark. Including it would stretch the range past the last real character
    for nothing.
   The rest of what TotalLength counts IS wanted: it includes the text of embedded tables, and in
   a story that is entirely new the table's words are new as well.

   @return 0 for a story that is not there or holds nothing but that final return.
*/
TextIndex KCMStoryWholeTextEnd(IDataBase* db, UID storyUID)
{
	InterfacePtr<ITextModel> model(UIDRef(db, storyUID), UseDefaultIID());
	return (model != nil) ? model->TotalLength() - 1 : 0;
}

/* A range in the Track mode's author colour, when the change has one; otherwise as it was - every other
   mode's GetTrackChange answers kFalse. */
KCMMarkRange KCMTrackColoured(IKCMStoryEditsFacade* edits, int32 n, int32 i, const KCMMarkRange& range)
{
	IKCMStoryEditsFacade::TrackChange tc;
	if (edits->GetTrackChange(n, i, tc) && tc.fHasColour)
		return range.WithColour(KCMMarkColour(tc.fR, tc.fG, tc.fB));
	return range;
}

/* KCMStoryAddChange
   What one change of a row marks in the document being asked for, added to the row's list. Nothing, for a change that is
   not a place there.

   @param edits, n, i which change: row n, change i.
   @param db, storyHere, useSourceDocument the document being marked, the row's story in it, and which of the two it is.
   @param ranges [in,out] the row's list so far.
*/
void KCMStoryAddChange(IKCMStoryEditsFacade* edits, int32 n, int32 i, IDataBase* db, UID storyHere, bool16 useSourceDocument,
					   KCMMarkRangeList& ranges)
{
	IKCMStoryEditsFacade::Change change;
	if (!edits->GetChange(n, i, change))
		return;

	// **AND WHAT AN IMPORT COULD NOT PUT IN IS NOT A PLACE**: a "!" child names a
	//   reason, not a range - its positions are zero - and a mark drawn from it would be a
	//   caret at the start of the story pointing at nothing.
	if (change.fWhat == IKCMStoryEditsFacade::Change::kWhatRefused)
		return;

	// A CHANGE THE READER TOOK BACK IS NOT MARKED: the marks say "the two versions
	//   differ HERE", and at that place they no longer do - the row says "=" for the same reason.
	if (change.fReplaced)
		return;

	// A TABLE ROW MARKS ITS CHANGED CELLS, ONE SPAN EACH (the author: "the changed cells should be marked" - even
	//   though the list shows one row). The spans are Target coordinates, so only the Target's marks read them. The
	//   Source side takes the table's anchor range below, as any change would, and that draws nothing: the anchor's
	//   run has no glyphs (spec map SMK-26).
	if (change.fWhat == IKCMStoryEditsFacade::Change::kWhatTable && !useSourceDocument)
	{
		for (int32 s = 0; s < change.fMarkSpanCount; ++s)
		{
			TextIndex a = 0;
			TextIndex b = 0;
			if (!edits->GetChangeMarkSpan(n, i, s, a, b))
				continue;
			ranges.push_back((b > a) ? KCMMarkRange(a, b) : KCMMarkRange::Caret(a));
		}
		return;
	}

	// **AN INSERTION MARKS ITS PLACE ON THE SOURCE SIDE, AS A CARET.** The older side holds no characters for it, but
	//   it holds the place. The range comes through EMPTY for one, like a deletion on the side it was deleted
	//   from, and an empty range is a caret (KCMStoryMarker::CaretAt, which the jump's flash asks too).
	const TextIndex from = useSourceDocument ? change.fSourceStart : change.fTargetStart;
	const TextIndex to = useSourceDocument ? change.fSourceEnd : change.fTargetEnd;
	if (to < from)
		return;

	ranges.push_back(KCMTrackColoured(edits, n, i, (to > from) ? KCMMarkRange(from, to)
																 : KCMStoryMarker::CaretAt(db, storyHere, from)));
}

/* KCMStoryCollectRanges
   Every edit that is visible in ONE of the two documents, added to the set per story. A document
   with nothing to mark gets no entry.

   @param db the document to read the ranges out of. One that is not open (or nil) adds nothing.
   @param useSourceDocument kTrue when db is the older one.
   @param docs [in,out] what is lit so far.
*/
void KCMStoryCollectRanges(IDataBase* db, bool16 useSourceDocument, KCMStoryMarkDocs& docs)
{
	if (!KCMIsDocDBOpen(db))
		return;

	// **HELD FOR THE WHOLE LOOP, NOT ASKED FOR PER ROW.** Every Utils<IFace>() written out is its
	//   own Query off the Utils boss -- the constructor calls UtilsBoss::QueryUtils and the
	//   destructor Releases (Utils.h:87-89) -- and this loop asks for a row, its change count and
	//   every one of its changes, on every press and every refresh.
	// @warning **THE TEST IS ON THE Utils OBJECT, BEFORE ANYTHING IS ASKED OF IT.** This can be reached
	//   from a model notification during teardown, and QueryUtilInterface() has no test of its own.
	Utils<IKCMStoryEditsFacade> edits;
	if (!edits)
		return;

	const int32 rowCount = edits->GetRowCount();
	for (int32 n = 0; n < rowCount; ++n)
	{
		IKCMStoryEditsFacade::Row row;
		if (!edits->GetRow(n, row))
			continue;

		// **WHICH ROWS EXIST IN THE DOCUMENT BEING ASKED FOR.** An ADDED story is only in the newer
		//   version and a REMOVED one only in the older, so each is skipped on the side it is not on.
		//   **Everything else is in BOTH**, and is marked in whichever one is being asked for.
		//
		// **THE SAME UID NAMES THE SAME STORY IN BOTH DOCUMENTS.** That is the premise the whole
		//   feature stands on -- the two are versions of one document, and saving under a new name
		//   carries the UIDs across (KCMStoryStamp.h, "WHY TWO VERSIONS CAN BE MATCHED AT ALL").
		//   Two other places already ask exactly this way, which is the check that this is not a local
		//   assumption: the diff reads the older side with the row's Source story - UIDRef(sourceDB, row->fSourceStoryUID),
		//   that same uid for every row paired by UID (KCMStoryDiffRun::Run) - and the click selects with it (KCMStoryJump.cpp).
		//   A row paired by place is the one exception, and the next block takes it.
		//
		// @warning **do not ask "is this a removed row?" and require that to MATCH useSourceDocument**: that drops
		//   every ORDINARY row from the source side. Holding the button over the older window would mark nothing
		//   unless a whole story had been deleted, and a DELETION would have nowhere to be shown: its characters
		//   survive only in the older version, so the source side is the ONLY side that can show one.
		//   ("A uid means a different object in the other document" is true in general and false for this feature.)
		if ((row.fKinds & (useSourceDocument ? kKCMStoryKindAdded : kKCMStoryKindRemoved)) != 0)
			continue;

		// THE STORY ON THIS SIDE (stories paired by place): the Source side is the row's SOURCE story
		//   (Row::fSourceStoryUID) - the same uid as fStoryUID for every row paired by UID and for a Removed one, but the
		//   Source's own story for a row paired by the place of its first frame, whose fStoryUID is the Target's new story
		//   and names something else (or nothing) over there. Every read below and the map's key are this story's.
		// kInvalidUID passes over the row that stands for a file with no such story - and, on the Source side, every Track
		//   row: a Track listing has no Source document, and its Source range is (0,0), the story's start.
		const UID storyHere = useSourceDocument ? row.fSourceStoryUID : row.fStoryUID;
		if (storyHere == kInvalidUID)
			continue;

		const int32 changeCount = edits->GetChangeCount(n);

		KCMMarkRangeList ranges;

		if (changeCount > 0)
		{
			ranges.reserve(changeCount);
			for (int32 i = 0; i < changeCount; ++i)
				KCMStoryAddChange(edits, n, i, db, storyHere, useSourceDocument, ranges);
		}
		else if ((row.fKinds & kKCMStoryKindUnpaired) != 0)
		{
			// **AN UNPAIRED STORY IS MARKED WHOLE.** An Added story has no partner in the older version
			//   and a Removed one has none in the newer, so no text diff was ever run for either -- which
			//   is why they arrive here with no changes at all (KCMStoryKinds.h, kKCMStoryKindUnpaired).
			//   Every character of them is new, or gone, so every character is the answer to "what
			//   changed".
			// @warning **the other two reasons for an empty list are not this one**, and must not be marked whole: the
			//   story could not be read, or the diff ran and the WORDS AGREE -- only formatting or a table moved.
			//   Lighting up the whole story would claim to know something in the first case and would be plainly wrong
			//   in the second (IKCMStoryEditsFacade.h, at GetChangeCount).
			// Asked as Unpaired rather than as Added, which is this plug-in's own rule: the two kinds
			//   differ only in WHICH document holds the story, and that has already been settled by the
			//   row filter above (KCMStoryKinds.h, at kKCMStoryKindUnpaired).
			const TextIndex wholeEnd = KCMStoryWholeTextEnd(db, storyHere);
			if (wholeEnd > 0)
				ranges.push_back(KCMMarkRange(0, wholeEnd));
		}

		if (!ranges.empty())
		{
			// @warning **APPENDED, NOT ASSIGNED.** Two rows naming the same story in the same document
			//   should not happen -- the list holds one row per story per side -- but if it ever did, an
			//   assignment would silently throw the first one's edits away. Appending cannot: the marker
			//   merges each story's list before it draws anything.
			if (useSourceDocument)
				KCM_DIAG_LOG("marks source story=%u ranges=%d first=[%d,%d)", static_cast<unsigned>(storyHere.Get()),
							 static_cast<int>(ranges.size()), static_cast<int>(ranges[0].fFrom),
							 static_cast<int>(ranges[0].fTo));	// test builds: which story, and where, the Source side is marked
			KCMMarkRangeList& dst = docs[db][storyHere];
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

	if (KCMIsArmed() && KCMModeUsesStoryRows(KCMGetCompareMode()))
	{
		// **A PRESS TURNS ITS OWN WINDOW ROUND. IT DOES NOT ADD TO THE TOGGLE.** One rule covers the
		//   whole plug-in: **while the button is held, that window is the other way round** -- marks
		//   that were off come on, and marks that were on go off, which is what lets the reader look at
		//   the plain page underneath the ones they asked to keep.
		//   So XOR for the toggles.
		//   The Pixel mode's frames follow the same rule, but spelt out in two places rather than one: the toggle puts
		//     them up (alwaysScreen in KCMDrawEventHandler) and the press takes them down (sMarksTempHidden, set in
		//     KCMPeekGesture). Here both halves are this one expression.
		//   @warning a press is over ONE window, so the other window's toggle is left exactly as it is.
		//   @warning real bools, made by == and != 0, so that != below is an XOR: bool16 is an integer type,
		//     and any non-zero truth other than kTrue would make a bare != answer the wrong way round.
		const bool pressTarget = (gPress == kPressOverTarget);
		const bool pressSource = (gPress == kPressOverSource);
		// **"Print comparison marks" ALSO PUTS THEM UP ON SCREEN**, and this is what makes printing
		//   possible at all. The Pixel mode works this way -- its wantMarks is
		//   `(sPrintMarks || sMarksVisible || ...)`, so turning the print toggle on shows the frames
		//   permanently as well (KCMDrawEventHandler, where wantMarks is worked out). WYSIWYG: what
		//   will come out is what you are looking at.
		// @warning **it is also a necessity, not a choice.** What gets drawn onto paper is drawn from
		//   this same set -- so "on paper but not on screen" would mean keeping a second set of ranges,
		//   which is one question answered in two places. The screen and
		//   the page agree because there is only ever one answer to draw from.
		// **BOTH SIDES READ THE PRINT TOGGLE, ON SCREEN AND ON PAPER** (the author's call: "with Print ON they are
		//   printable AND permanently shown, print preview included").
		//   So it is OR-ed outside the press: a press reverses what a toggle asks for, and cannot hide what Print is
		//   showing - what the Pixel mode's frames do (srcWanted and wantMarks in KCMDrawEventHandler).
		//   The two lines below are the SAME SHAPE on purpose: a reader can see at a glance that the sides agree.
		const bool tgtToggled = (KCMDrawEventHandler::sTgtMarksOn != 0);
		const bool srcToggled = (KCMDrawEventHandler::sSrcMarksOn != 0);
		const bool printing = (KCMDrawEventHandler::sPrintMarks != 0);

		if (printing || tgtToggled != pressTarget)
			KCMStoryCollectRanges(KCMArmedTargetDB(), kFalse, docs);
		if (printing || srcToggled != pressSource)
			KCMStoryCollectRanges(KCMArmedSourceDB(), kTrue, docs);
	}

	// An empty set takes the standing marks down. A jump's pointer may be on screen and it is not ours
	//   to clear -- this call names which of the two kinds it is about (KCMStoryMarker.h).
	KCMStoryMarker::ShowStanding(std::move(docs));
}

void KCMStoryMarkSetPress(bool16 active, bool16 useSourceDocument)
{
	// @warning **a release that nobody pressed does nothing** -- it must not go and rebuild the
	//   marks, because the button also comes up after gestures this file never heard the start of
	//   (Pixel mode, the CMYK sampler, a peek).
	if (!active && gPress == kPressNone)
		return;

	// WHICH WINDOW IS ONLY RECORDED ON THE WAY DOWN. A release does not carry the answer -- the
	//   cursor may have left the window by then -- and has no need of it.
	gPress = !active ? kPressNone : (useSourceDocument ? kPressOverSource : kPressOverTarget);

	KCMStoryMarkRefresh();
}

bool16 KCMStoryMarkPrintAllowedFor(IDataBase* db)
{
	// ONE PLACE ANSWERS THIS FOR A RUN (KCMStoryMarker.cpp). GetIsActive, which cannot name the
	//   document, reads the one flag this tests first - so a pass it lets through can still be refused
	//   here, and nothing it refuses could have been allowed here.
	// @warning **read directly, not through the facade:** this runs on the export's background
	//   thread, where a Query off kUtilsBoss per run is a cost with nothing to buy. The Pixel
	//   side reads the same two flags the same way (KCMRingAdornment.cpp,
	//   KCMMarksCouldBeTranslucent).
	// @warning **KCMIsSameDoc, NOT ==:** an asynchronous export hands the drawing a CLONE of the
	//   database, so the pointer never matches the one we armed with (KCMThreadSafety.h has the
	//   measurement).

	if (!KCMDrawEventHandler::sPrintMarks)
		return kFalse;

	// **THE OLDER DOCUMENT PRINTS ON "Print comparison marks" ALONE, LIKE THE NEWER ONE** (the author: "with Print ON I
	//   want them in the output; Always Show is only ever about the screen"). A display switch must not hold a veto
	//   over the output: do not add sSrcMarksOn here (KCMDrawEventHandler.cpp, srcWanted -- spec map MK-14).
	return (KCMIsSameDoc(db, KCMArmedTargetDB()) || KCMIsSameDoc(db, KCMArmedSourceDB())) ? kTrue : kFalse;
}

// End, KCMStoryMarkBuild.cpp.
