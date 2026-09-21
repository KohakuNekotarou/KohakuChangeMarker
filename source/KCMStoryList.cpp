//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryList.h for what this list is and why it is a file static.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IComposeScanner.h"
#include "IFrameList.h"
#include "IFrameListComposer.h"	// RecomposeThruLastFrame - the wax read below is a RESULT of composition
#include "IGeometry.h"			// the frame's inner->pasteboard matrix, for where a story begins
#include "IHierarchy.h"			// GetParentUID - a text column's frame is its parent (2026-08-22)
#include "IPageList.h"
#include "IParcelList.h"		// GetFirstParcelKey / GetNextParcelKey / GetParcelToFrameMatrix
#include "ITextFrameColumn.h"	// what QueryFrameContaining hands back (2026-08-22)
#include "ITextModel.h"
#include "ITextParcelList.h"	// QueryTextParcelList - the parcels a story flows through
#include "ITextStoryThread.h"		// QueryStoryThread's answer: which thread (body, cell, footnote) holds an index (2026-09-12)
#include "ITextStoryThreadDict.h"	// GetAnchorTextRange - where a table or footnote is anchored in the thread above it
#include "IWaxGlyphs.h"			// GetEscapementAt - how far into the run one character sits
#include "IWaxIterator.h"		// GetFirstWaxLine - the line a TextIndex was composed onto
#include "IWaxLine.h"
#include "IWaxRun.h"			// GetToPasteboardMatrix
#include "IWaxStrand.h"

// General includes:
#include "K2SmartPtr.h"			// K2::scoped_ptr - what NewWaxIterator hands back has to be deleted
#include "PMMatrix.h"
#include "PMRect.h"				// GetParcelBounds - the leading corner comes off this
#include "RangeData.h"			// Text::StoryRange - what GetAnchorTextRange answers with
#include "TextChar.h"			// kTextChar_Space - the boundary the readability test draws its line at
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "UnicodeClass.h"		// IsWhiteSpace
#include "WideString.h"

#include <algorithm>			// std::sort, std::remove_if
#include <sstream>			// the reading port's number formatting (RowsAsTsv)
#include <string>
#include <vector>

// Project includes:
#include "IKCMStoryEditsFacade.h"	// RowsAsTsv reads the SHOWN ranges through the facade's GetChange - the one place that cuts a paragraph break off them
#include "Utils.h"
#include "KCMCore.h"			// KCMFramePageUID - shared with the overset scan since 2026-08-09
#include "KCMStoryList.h"
#include "KCMStoryRowFilter.h"	// KCMStoryRowHasContentChange - which rows belong in the list
#include "KCMStoryTextImport.h"	// KCMImportRefusals - what the last import could not put in, put back as rows on every Build

namespace
{

// The list. See the header for why this is a file static rather than a boss.
//
// **NO LOCK, AND THE REASON IS THAT NOTHING ON A BACKGROUND THREAD READS IT.** Every caller runs
// on the main thread, in three groups:
//   - the comparison BUILDS the list and annotates it -- KCMRebuildStoryEdits (KCMCore.cpp),
//     reached from Start and from Refresh Page Comparison, and the text diff it drives
//     (KCMStoryDiffRun.cpp), which walks the rows and writes their children back;
//   - the panel READS it through the facade (KCMFacades.cpp), and a UI plug-in's bosses are
//     invisible to a background thread anyway. The comparison reads it too, to count the edits
//     for its report;
//   - Stop, a compared document closing, and the shutdown service EMPTY it (KCMCore.cpp's
//     KCMDoClearMarks, and KCMPeek.cpp for the other two).
// **The background thread runs the DRAWING path only, and the drawing path never asks about
// story edits** -- it reads the page map and the mark state, neither of which is here.
//
// @warning **what would break it:** drawing a story-edit marker on the page (a badge on a changed
//  story's frame, say), or building this list from a background export. Either one puts a reader
//  on the BG thread, and then this needs KCMMarkStateLock exactly as the mark maps do.
//
// Same shape as KCMDrawEventHandler::DropAllOrig: what tends to be missing in a place like this
// is not the lock but the REASON -- and the condition under which the reason stops holding.
std::vector<KCMStoryRow> gRows;

// A safety valve, NOT a display limit. How much of a story's opening text is shown is decided by
// the row's text cell alone: it is kEllipsizeMiddle and bound kBindLeft|kBindRight (the cell is
// kKCMStoryRowTextWidgetID, declared UI-side in ui/KCMUIID.h and laid out in ui/KCMUI.fr), so it
// is the one cell that grows when the panel is widened, and it shortens its own text to fit.
//
// **THE PANEL ALREADY ANSWERED THIS EXACT QUESTION ONCE.** KCMDocNameFromDB used to shorten the
//   Target / Source document names by character count in C++; that was deleted and the
//   shortening left to kEllipsizeMiddle, so the question is asked in one place only
//   ([[one-question-one-place]]). A character cap here was the same duplicate, and it went
//   unnoticed until the panel became resizable: from then on 30 characters silently capped every
//   width, so widening the panel bought empty space rather than more text. The old cap took its
//   number from SnpCreateCrossReference.cpp, which names stories in a fixed-width dialog -- a
//   list that cannot be resized has no such question to answer.
//
// The number below only has to sit past anything a cell could ever show. A palette-font character
// is never narrower than about 3px, so even a text cell spanning a 4K screen (~3700px) runs out
// before ~1300 characters. 2000 leaves room and still bounds a pathological single-paragraph
// story, which is the only thing this limit exists to stop.
//
// No ellipsis is appended when the text is cut here: the cell adds its own, and a string already
// ending in "..." would be shortened again around it.
const int32 kRowTextSafetyLimit = 2000;

// ★★THE SIGN A TABLE LEAVES IN A STORY ROW (2026-09-19, the user: "a picture that says table at a
// glance"): SQUARE WITH ORTHOGONAL CROSSHATCH FILL, U+25A6 - a square ruled into a grid.
// Until today the anchor read as a GAP (`a b` for a-table-b), which said nothing about what stood
// there; the child rows, cut by KCMTextRead, still read `ab`, because there the table's characters
// are positions and not text (KCMTextRead.cpp says why they have to come out).
// ⚠NOT a CJK character on purpose (the user turned 田 down: "that is Japanese") - the sign has to
//   read the same to an English reader, and has to come from a font the palette is sure to have
//   on either locale. It is a code point rather than a literal because it is not in CP932: a
//   literal would fail to build here, which is the safe way round (cpp-japanese-needs-bom).
// ⚠★**U+229E (SQUARED PLUS) WAS TRIED FIRST AND TURNED DOWN ON SIGHT** (the same evening): in the
//   palette font its plus does not touch the square, and a box with a loose plus in it is what
//   InDesign draws for OVERSET text - the one thing a row in this list must not seem to say.
//   Measured, not guessed: it was built, looked at in the panel, and replaced.
// ★KIDMCP's twin (KIDMCPRuby.cpp, Readable) stands `#` in the same place for the same reason -
//   one character per position; this list shows it to a person, so it gets the picture.
const UTF32TextChar kKCMTableSign(0x25A6);

/* IsReadable
	Is this character worth putting in a row? Only characters a reader would recognise as the story's
	opening words qualify.
*/
bool16 IsReadable(const UTF32TextChar& ch)
{
	const uint32 v = ch.GetValue();

	// Everything below the space character, in one test. That single boundary covers the paragraph
	// and line breaks, the tab, the table anchors (0x0016/0x0017) and the page-number and
	// section-name placeholders (0x0018/0x0019), because InDesign packs all of its own special
	// characters into the control range -- which is what IsK2SpecificChar is for: "the low-ascii
	// characters that have meaning to InDesign ... standard values like tab, carriage return and
	// special ones like IndentToHere, Table" (TextChar.h, at its declaration).
	// ★The table's two characters are not readable, and FirstReadableText asks about them BEFORE
	//   it asks this: the anchor becomes the table sign (kKCMTableSign), the per-row continuation
	//   nothing at all. This test is only reached by what is left.
	//
	// @warning **IsIllegalControlChar draws a different line and cannot stand in for this.** It is
	// (n < kTextChar_Space && !IsK2SpecificChar(n)), so it EXCLUDES the very characters this test
	// exists to drop. The SDK has no predicate for what is wanted here, which is why the boundary
	// is spelled out rather than borrowed.
	if (v < kTextChar_Space)
		return kFalse;

	if (v == kTextChar_ObjectReplacementCharacter)
		return kFalse;			// an inline graphic is not text

	// Spaces of every width, the ideographic one included -- a paragraph of them says nothing.
	if (UnicodeClass::IsWhiteSpace(ch))
		return kFalse;

	return kTrue;
}

/* FirstReadableText
	The opening words of a story, or an empty string when it has none to show.

	The scan runs paragraph by paragraph and stops at the first paragraph holding anything readable.
	It runs to TotalLength() rather than to the end of the primary thread on purpose: table cells and
	footnotes are further threads inside the SAME model, so a frame holding nothing but a table only
	says something once the scan is allowed past the main thread.
	@warning the headers state this for TABLE CELLS only -- TotalLength "including data for embedded
	 tables" against GetPrimaryStoryThreadSpan "does not include any characters that are part of
	 story threads for table cells" (ITextModel.h, the two declarations next to one another). That
	 FOOTNOTES sit there too is measured, not documented: a document whose only overset was a
	 footnote reported its one stop here.

	**Where this parts company with the official example.** SnpCreateCrossReference.cpp does the
	same job and copies span-1 characters so that the paragraph's CR is left behind. Here the whole
	span is copied and unreadable characters are filtered out instead, because this list has a
	requirement the snippet does not: a paragraph made of nothing but control characters has to be
	SKIPPED so the scan moves on to the next one, which cannot be decided without looking at the
	characters anyway. Filtering also steps around the snippet's edge case, where span-1 eats a real
	character in the last paragraph of a story that ends without a CR.

	★★**A TABLE STANDS IN THE ROW AS A SIGN** (2026-09-19, kKCMTableSign), where its anchor stands
	in the text: `a-table-b` reads as `a▦b`, with nothing between - the anchor IS the gap, so it
	spends no space of its own. The continuation characters behind it (one per further row) are
	dropped without leaving a gap either; a table is one sign however many rows it has.
	**The sign is carried, not counted.** A paragraph holding nothing but a table is still not a
	paragraph with words in it, so the scan goes on exactly as before - into the next paragraph, or
	into the table's own cells (they are further threads of the same model, see above) - and the
	sign is put in front of the first words found: `▦ c` for a story that opens with a table, and
	`▦ 品名` for a frame holding nothing but one, which used to read `品名` and now says what it is.
	The gap between them is the paragraph break's, the same one a tab leaves. A story with tables
	and no words at all (empty cells) answers with the signs alone rather than with nothing.
	⚠**The sign is a DISPLAY of the anchor and goes nowhere else**: the child rows, the diff, the
	  Story Text files and the write-back all read the text through KCMTextRead / KCMParaText,
	  which drop the table's characters, and none of them is touched by this.
*/
PMString FirstReadableText(ITextModel* model)
{
	PMString out;
	out.SetTranslatable(kFalse);

	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return out;

	const TextIndex total = model->TotalLength();
	TextIndex pos = 0;

	// ★Both outlive one paragraph on purpose: a table sign met in a paragraph with no words is
	//   carried into the next, and so is the gap its paragraph break leaves (see the header).
	WideString kept;
	bool16 pendingGap = kFalse;
	int32 wordsKept = 0;		// readable characters in `kept` - the signs are not counted

	while (pos < total)
	{
		int32 span = 0;

		// excludeEOS = kFalse so that a story ending without a CR still reports its last paragraph,
		// which is the only paragraph a short story has.
		// **THE DEFAULT REVERSED -- AND ADOBE REVERSES IT FOR THIS SAME JOB.** The parameter defaults
		// to kTrue (IComposeScanner::FindSurroundingParagraph). Every call to it in the SDK splits by
		// what the walk is FOR:
		//   - walking text the user is EDITING takes the default, because there the end-of-story mark
		//     is not a paragraph worth reporting: spellpanel's SpellCheckWalkerData.cpp (twice) and
		//     AutoCorrectTypingIdleTask.cpp;
		//   - walking stories in order to NAME them passes kFalse, exactly as here:
		//     SnpCreateCrossReference.cpp -- the same snippet this file takes as its example twice
		//     over (see kRowTextSafetyLimit and the note above).
		// This list is looking for the first words that exist AT ALL, so a story that is one CR-less
		// paragraph long has to count.
		const TextIndex paraStart = scanner->FindSurroundingParagraph(pos, &span, kFalse);
		if (paraStart < 0 || span <= 0)
			break;

		WideString para;
		scanner->CopyText(paraStart, span, &para);

		const int32 charCount = para.CharCount();

		for (int32 i = 0; i < charCount && kept.CharCount() < kRowTextSafetyLimit; ++i)
		{
			const UTF32TextChar ch = para.GetChar(i);
			const uint32 v = ch.GetValue();

			// A table: its anchor becomes the sign, its per-row continuations become nothing. Asked
			// before IsReadable, which would turn either into a gap. A gap already owed is paid
			// first, so `a<tab><table>b` keeps the tab's space in front of the sign.
			if (v == kTextChar_Table)
			{
				if (pendingGap)
				{
					kept.Append(UTF32TextChar(kTextChar_Space));
					pendingGap = kFalse;
				}
				kept.Append(kKCMTableSign);
				continue;
			}
			if (v == kTextChar_TableContinued)
				continue;

			if (!IsReadable(ch))
			{
				// An unreadable character becomes at most one space, and only between readable ones:
				// leading runs are dropped because nothing has been kept yet, and trailing runs
				// because the gap is never spent. A tab between two columns of text reads as a gap,
				// which is what it looks like on the page.
				pendingGap = !kept.empty();
				continue;
			}

			if (pendingGap)
			{
				kept.Append(UTF32TextChar(kTextChar_Space));
				pendingGap = kFalse;
			}
			kept.Append(ch);
			++wordsKept;
		}

		// Words found: this is the row. Signs alone are not (see the header) - they are carried on.
		if (wordsKept > 0)
		{
			out = PMString(kept);
			out.SetTranslatable(kFalse);
			return out;
		}

		// Advance past this paragraph. The guard is not ceremony: FindSurroundingParagraph answers
		// with the start of the paragraph CONTAINING pos, which can lie before it, so a span that
		// does not reach past pos would ask the same question forever.
		const TextIndex next = paraStart + span;
		if (next <= pos)
			break;
		pos = next;
	}

	// No words anywhere, but a table (or several) was passed: say so rather than nothing.
	if (!kept.empty())
	{
		out = PMString(kept);
		out.SetTranslatable(kFalse);
	}
	return out;
}

/* RowIsBefore
	Page order, with the pageless rows last (their index is kMaxInt32).
*/
bool RowIsBefore(const KCMStoryRow& a, const KCMStoryRow& b)
{
	// ★★**BEFORE EVERYTHING: WHAT AN IMPORT COULD NOT PUT IN** (2026-09-19, the user: "the ! rows at
	//   the top of the Story Edits list"). A reader who has just imported wants to know first what
	//   did NOT go in; the rows that did are the ordinary comparison and follow in their own order.
	const bool aRefused = (a.fKinds & kKCMStoryKindRefused) != 0;
	const bool bRefused = (b.fKinds & kKCMStoryKindRefused) != 0;
	if (aRefused != bRefused)
		return aRefused;

	// **FIRST KEY: WHICH DOCUMENT THE ROW LIVES IN.** Every target row comes before every removed
	//   one, and only then does the page order below apply -- within each group. The page numbers
	//   in the column then come from ONE document at a time, in order, and the reader is not asked
	//   to notice that the column changed documents part-way down. It is also the order Export
	//   Changed Pages has always used.
	//   @warning **not done by giving removed rows kMaxInt32:** that would drop them in among the
	//     master-page and pasteboard rows, and leave removed rows ordered by uid rather than by
	//     page -- "which document" and "has a page" are different questions and need different keys.
	//   It also keeps the uid tie-break below honest: it only ever compares two uids from the same
	//   document, and a uid means nothing across documents.
	const bool aRemoved = (a.fKinds & kKCMStoryKindRemoved) != 0;
	const bool bRemoved = (b.fKinds & kKCMStoryKindRemoved) != 0;
	if (aRemoved != bRemoved)
		return !aRemoved;

	if (a.fPageIndex != b.fPageIndex)
		return a.fPageIndex < b.fPageIndex;

	// Same page, or both without one. Broken by UID so that comparing the same two documents twice
	// lists the rows in the same order: std::sort is not stable, so ties left to it could swap
	// places between two runs that found exactly the same edits.
	return a.fStoryUID < b.fStoryUID;
}

}	// anonymous namespace

/* KCMStoryFirstFrameUID (declared in KCMStoryList.h)

	**RecomposeThruLastFrame is deliberately NOT called.** This asks where the story STARTS, not
	where its text overflows, so there is nothing to compose -- and composing here would cost the
	property KCMStoryStamp.h records under "READING COUNTERS COMPOSES NOTHING": reading what
	changed changes nothing.
*/
UID KCMStoryFirstFrameUID(IDataBase* db, UID storyUID)
{
	if (db == nil || storyUID == kInvalidUID)
		return kInvalidUID;

	// Quietly nil for a UID this document does not hold a story at - which is the ordinary answer
	// when the SOURCE is asked about a story that only the target has (an "Added" row).
	InterfacePtr<ITextModel> model(db, storyUID, UseDefaultIID());
	if (model == nil)
		return kInvalidUID;

	InterfacePtr<IFrameList> frameList(model->QueryFrameList());
	if (frameList == nil || frameList->GetFrameCount() == 0)
		return kInvalidUID;	// a real story, placed in no frame: there is nowhere to scroll to

	return frameList->GetNthFrameUID(0);
}

/* KCMStoryStartPoint (declared in KCMStoryList.h)

	The mirror image of the overset scan's KCMLastPlacedOutport: that one walks BACK from the last
	parcel to find where the text stopped fitting; this walks FORWARD from the first to find where it
	started. Same three coordinate spaces, same reason vertical text needs no branch.
*/
bool16 KCMStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb)
{
	if (db == nil || storyUID == kInvalidUID)
		return kFalse;

	InterfacePtr<ITextModel> textModel(db, storyUID, UseDefaultIID());
	if (textModel == nil)
		return kFalse;

	// Index 0 is the start of the story, so this is the thread the beginning is in.
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(0));
	if (tpl == nil)
		return kFalse;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	for (ParcelKey k = pl->GetFirstParcelKey(); k.IsValid(); k = pl->GetNextParcelKey(k))
	{
		const UID frameUID = pl->GetParcelFrameUID(k);
		if (frameUID == kInvalidUID)
			continue;	// this piece is not placed; keep going forward for one that is

		InterfacePtr<IGeometry> frameGeo(db, frameUID, UseDefaultIID());
		if (frameGeo == nil)
			continue;

		const PMRect   parcelBounds = pl->GetParcelBounds(k);				// parcel-local
		const PMMatrix toFrame      = pl->GetParcelToFrameMatrix(k);			// parcel -> frame inner
		const PMMatrix toPasteboard = ::InnerToPasteboardMatrix(frameGeo);	// frame inner -> pasteboard

		PMPoint corner(parcelBounds.Left(), parcelBounds.Top());	// the inport corner, parcel-local
		toFrame.Transform(&corner);
		toPasteboard.Transform(&corner);

		outFrame = frameUID;
		outPb    = PBPMPoint(corner.X(), corner.Y());
		return kTrue;
	}
	return kFalse;
}

/* KCMStoryPointAt (declared in KCMStoryList.h)

	Where ONE character of a story sits on the pasteboard -- what a jump to a change needs, as
	against KCMStoryStartPoint above, which answers where the whole story begins.

	**PORTED FROM KBSJump.cpp.** KBS's own copy says "ported from KESCLFindInDoc", so this is the
	  third plug-in in this family to carry the same recipe, and the two before it have already
	  paid for the corrections written into it -- the overset test, and the recompose. The shape is
	  theirs on purpose: GetFirstWaxLine -> QueryRunByTextOffset -> GetEscapementAt ->
	  GetToPasteboardMatrix.

	**COMPOSITION IS BROUGHT UP TO DATE FIRST, AND THAT IS THE ONE DIFFERENCE FROM ITS NEIGHBOUR.**
	  KCMStoryFirstFrameUID says, in as many words, that it deliberately does NOT compose -- and it
	  is right to, because it asks which parcels EXIST, which composition does not decide. This
	  asks where a character was PUT, which is nothing but a result of composition: read without
	  composing and the answer is wherever that character stood before the last edit.
	  The recipe is the SDK's (IFrameList::GetFirstDamagedFrameIndex() != -1 ->
	  IFrameListComposer::RecomposeThruLastFrame, as SnpInspectTextModel.cpp spells it); KCM already
	  writes it the same way where it asks about overset (KCMOversetScan.cpp).
	  @warning **COMPOSING DIRTIES THE DOCUMENT**, so the caller must hold a
	    IDataBase::SaveRestoreModifiedState. That is a change of contract for the jump path, which
	    measured itself clean while nothing on it touched the model -- and its own comment says to
	    measure again if anything ever did. This is that thing.

	@param index the character to find. **AN INDEX OUTSIDE THE STORY AS IT STANDS NOW IS REFUSED
		HERE**, not passed on to the text engine. It once said "clamped by the caller", and the
		source side could not honour that: it is handed Change::fSourceStart, a number the diff
		worked out against the OLDER document, which nothing had measured against that document as
		it stands now. The check belongs where the length is already in hand, and one line covers
		both sides.
	@param outPb [out] the middle of that character's line, in pasteboard coordinates. Untouched
		when this answers kFalse.
	@return kFalse when the story is not there, the position is OVERSET or in no frame, or the
		text has not been composed and cannot be -- callers fall back to the story's start.
*/
/* KCMRecomposeIfDamaged
   Bring a frame list's composition up to date, if it is not already.

   **TWO QUESTIONS IN THIS FILE ARE READINGS OF THE COMPOSITION AND THEY MUST READ THE SAME
   ONE:** where a character sits (KCMStoryPointAt) and which frame holds it (KCMStoryFrameAt).
   A jump uses both -- one to choose the spread, the other to choose the point inside it -- so
   if only one of them composes, the two answers come from different compositions and the view
   is scrolled to a point that belongs to a different spread. The target path did exactly that
   while it resolved the frame BEFORE the point composed.
   Both go through here, and the caller of either composes before it asks anything else.
   KBS says the same thing from the other end: GetFirstChunkPasteboardRect assumes its caller
   has already recomposed, "and so is the overset test the caller made, so both have to be
   looking at the same one".
   @warning **COMPOSING DIRTIES THE DOCUMENT** -- every caller holds a
    IDataBase::SaveRestoreModifiedState.
*/
static void KCMRecomposeIfDamaged(IFrameList* frameList)
{
	if (frameList == nil || frameList->GetFirstDamagedFrameIndex() == -1)
		return;
	InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
	if (composer != nil)
		composer->RecomposeThruLastFrame();
}

/* KCMPrimaryIndexOf
   The index in the PRIMARY story thread that stands for `index`: the index itself when it is
   already in the body, and otherwise the ANCHOR of the table (or footnote) whose thread holds it -
   climbing out of nested tables until the body is reached.

   ⚠★★★**WRITTEN THE NIGHT INDESIGN CRASHED ON A TABLE** (2026-09-12, measured on the live
   application). The user added a table to the newer document, the comparison listed each new
   cell as an inserted paragraph, and a click on one of those rows jumped to it: KCMStoryFrameAt
   handed the cell's TextIndex to IFrameList::QueryFrameContaining, and the call died inside
   TEXT.RPLN with EXCEPTION_ACCESS_VIOLATION - the crash reporter's stack reads
   KCMStoryRowEH::LButtonUp -> KCMStoryJumpToChange -> KCMGotoStoryFrame ->
   KCMStoryEditsFacade::GetStoryFrameAt -> KCMStoryFrameAt -> (TEXT.RPLN). A cell's characters
   live in a thread of the same ITextModel that stands AFTER the body text (memory
   table-cells-live-after-body-text), so the index is smaller than TotalLength and passed every
   test this file made.
   ⚠**This crossing alone did NOT stop the crash** - the second crash, with it in place, died at
     the same instruction. QueryFrameContaining's own contract (IFrameList.h:120-125) admits an
     index in any thread and COMPOSES up to it, and that composition is what died; what stopped
     it was leaving that call for the parcel route (KCMStoryFrameAt). This crossing is kept
     because KCMStoryPointAt's wax reading is of the body, and the two readings must be of one
     place: what the reader is sent to is the frame that holds the TABLE (its anchor character in
     the body), which is where the cell is drawn.

   @return the body index, or -1 when the index cannot be placed in the body at all (a thread whose
    dictionary answers "not anchored" while not being the body's own).
*/
static TextIndex KCMPrimaryIndexOf(ITextModel* textModel, TextIndex index)
{
	if (textModel == nil)
		return -1;

	// Bounded rather than while(true): a dictionary that anchors into its own thread would
	// otherwise spin, and sixteen levels of nested tables is more than a document holds.
	for (int32 depth = 0; depth < 16; ++depth)
	{
		// The body is [0, GetPrimaryStoryThreadSpan) - an index there is its own answer, and so
		// is the caret position at its very end when the story has no threads beyond it
		// (KCMStoryFrameAt's `>` test admits TotalLength for exactly that case).
		if (index < textModel->GetPrimaryStoryThreadSpan())
			return index;

		InterfacePtr<ITextStoryThread> thread(textModel->QueryStoryThread(index, nil, nil));
		if (thread == nil)
			return index;			// nothing claims it (the end-of-story caret) - as before

		InterfacePtr<ITextStoryThreadDict> dict(::GetDataBase(textModel), thread->GetDictUID(), UseDefaultIID());
		if (dict == nil)
			return -1;

		// ITextStoryThreadDict.h:86 - the body's own dictionary answers "not anchored"; a table's
		// or a footnote's answers the range of its anchor character in the thread above it.
		bool16 anchored = kFalse;
		const Text::StoryRange anchor = dict->GetAnchorTextRange(&anchored);
		if (!anchored)
			return (thread->GetDictUID() == ::GetUID(textModel)) ? index : -1;

		index = anchor.Start(nil);	// RangeData::Start takes an optional Lean* out-parameter (RangeData.h:67)
	}
	return -1;
}

/* KCMStoryFrameAt (declared in KCMStoryList.h)

	**WHY THIS IS NOT KCMStoryFirstFrameUID.** That one answers where a story STARTS, which is the
	right frame for a row that names a story. This answers where one CHARACTER is, which is the
	right frame for a row that names an edit -- and in a story threaded across several spreads the
	two are nowhere near each other. The jump needs this one to choose which spread to bring into
	view, and pasteboard coordinates are spread-relative, so choosing the wrong spread does not put
	the reader slightly off: it puts them on another page entirely.

	**IT RETURNS THE PAGE ITEM, NOT THE TEXT COLUMN.** QueryFrameContaining hands back the column
	that holds the text; the frame the reader sees, and the thing with the geometry, is the column's
	parent (IHierarchy). @warning this is a real difference from KCMStoryFirstFrameUID, which
	returns GetNthFrameUID(0) -- a column UID.
*/
UID KCMStoryFrameAt(IDataBase* db, UID storyUID, TextIndex index)
{
	if (db == nil || storyUID == kInvalidUID || index < 0)
		return kInvalidUID;

	InterfacePtr<ITextModel> textModel(db, storyUID, UseDefaultIID());
	if (textModel == nil || index > textModel->TotalLength())
		return kInvalidUID;		// no such story here, or no such position in it any more

	// @warning **THE TEST IS `>` AND NOT `>=`, DELIBERATELY.** TotalLength is a valid TextIndex --
	//   it is where the caret stands after the last character, and a deletion at the very end of a
	//   story is reported at exactly that position. Refusing it would send those rows to the story's
	//   beginning instead of to the frame the edit is in. What is being kept out is an index from
	//   ANOTHER length: the diff measured the older document as it was, and it may have been edited
	//   since.

	// ★A cell's or a footnote's index is asked as the index of its ANCHOR in the body, so that the
	//   two readings of the composition (this and KCMStoryPointAt) are of one place, and that place
	//   is one the body's wax can answer for (KCMPrimaryIndexOf, 2026-09-12).
	index = KCMPrimaryIndexOf(textModel, index);
	if (index < 0)
		return kInvalidUID;

	InterfacePtr<IFrameList> frameList(textModel->QueryFrameList());
	if (frameList == nil || frameList->GetFrameCount() == 0)
		return kInvalidUID;		// a real story placed in no frame: nowhere to go

	KCMRecomposeIfDamaged(frameList);

	// ★★★**THE PARCEL ROUTE, NOT IFrameList::QueryFrameContaining** (2026-09-12). InDesign died
	//   twice inside TEXT.RPLN under this function, both times on the first row of a table the user
	//   had just added, and both times at the same instruction. QueryFrameContaining's contract
	//   (IFrameList.h:120-125) admits an index in any thread and COMPOSES up to it, which is more
	//   than this function wants: it wants to know which frame a body position is in, and the
	//   parcel list answers exactly that - the same route KCMStoryStartPoint has always taken, and
	//   the one the story rows' jump survived the same table on. ★Measured after the change
	//   (2026-09-12, live): 24 changes walked with Next, 16 of them cells of the table that had
	//   crashed the old route twice, and a breadcrumb probe (since removed) showed each cell index
	//   crossing to its anchor and the parcel answering.
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(index));
	if (tpl == nil)
		return kInvalidUID;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kInvalidUID;

	// ★THE END OF THE STORY LEANS LEFT. A deletion at the very end is reported AT TotalLength -
	//   the caret's place after the last character - which no parcel contains; the old route's `>`
	//   test above admits it for exactly that row, and QueryFrameContaining used to answer for it.
	//   GetParcelContainingLeanLeft is the parcel list's own word for "the parcel of the character
	//   before this position" (ITextParcelList.h:92-94), so that row still lands in its frame
	//   rather than falling back to the story's first (found in the recheck of 2026-09-12, after
	//   the parcel route went in).
	ParcelKey key = tpl->GetParcelContaining(index);
	if (!key.IsValid() && index > 0)
		key = tpl->GetParcelContainingLeanLeft(index);
	if (!key.IsValid())
		return kInvalidUID;		// overset, or placed nowhere - the caller keeps its own fallback

	// The parcel's page item is the text COLUMN; the frame the reader sees is its parent.
	const UID columnUID = pl->GetParcelFrameUID(key);
	if (columnUID == kInvalidUID)
		return kInvalidUID;

	InterfacePtr<IHierarchy> columnHierarchy(db, columnUID, UseDefaultIID());
	if (columnHierarchy == nil)
		return kInvalidUID;

	return columnHierarchy->GetParentUID();		// kInvalidUID is already the "no answer" value
}

bool16 KCMStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb)
{
	if (db == nil || storyUID == kInvalidUID || index < 0)
		return kFalse;

	InterfacePtr<ITextModel> textModel(db, storyUID, UseDefaultIID());
	if (textModel == nil || index > textModel->TotalLength())
		return kFalse;		// see the @param note above: neither caller can clamp this for us.
							// `>`, not `>=` -- the reason is written out in KCMStoryFrameAt.

	InterfacePtr<IWaxStrand> waxStrand((IWaxStrand*)textModel->QueryStrand(kFrameListBoss, IID_IWAXSTRAND));
	if (waxStrand == nil)
		return kFalse;

	InterfacePtr<IFrameList> frameList(waxStrand, UseDefaultIID());
	KCMRecomposeIfDamaged(frameList);

	K2::scoped_ptr<IWaxIterator> waxIter(waxStrand->NewWaxIterator());
	if (waxIter == nil)
		return kFalse;

	// ★TWO PLACES ARE TRIED, THE CHARACTER'S OWN FIRST (recheck of 2026-09-12). A cell's or a
	//   footnote's characters are composed into wax of their own, and that wax answers where the
	//   character really stands - inside the table, which is where the reader wants the window.
	//   Only when the wax has no line for it is the index crossed to its ANCHOR in the body
	//   (KCMPrimaryIndexOf - the frame KCMStoryFrameAt named holds that anchor, so the point still
	//   lands in the same frame). ⚠Trying the anchor FIRST would be a quiet regression: the table
	//   anchor character has no wax of its own (the composer stops at kTextChar_Table - memory
	//   text-composition-damage-and-recompose), so GetFirstWaxLine could answer nil there and the
	//   caller would fall back to the STORY'S START, far from the table.
	// ⚠Neither reading COMPOSES anything beyond the RecomposeIfDamaged above: the wax iterator
	//   reads what is there, which is the difference from the QueryFrameContaining that crashed.
	TextIndex candidates[2] = { index, KCMPrimaryIndexOf(textModel, index) };
	IWaxLine* waxLine = nil;
	InterfacePtr<IWaxRun> waxRun;
	int32 glyphOffset = -1;
	for (int32 c = 0; c < 2 && waxRun == nil; ++c)
	{
		if (candidates[c] < 0 || (c == 1 && candidates[1] == candidates[0]))
			continue;

		int32 offsetInLine = 0;
		waxLine = waxIter->GetFirstWaxLine(candidates[c], &offsetInLine);
		if (waxLine == nil)
			continue;			// overset, or not placed at all - there is no "where" to answer with

		// Which run holds that character, and how far into the run it is. The escapement is
		// measured up to the glyph BEFORE it, which is the start of the character rather than its
		// far edge.
		glyphOffset = -1;
		waxRun = InterfacePtr<IWaxRun>(waxLine->QueryRunByTextOffset(offsetInLine, &glyphOffset));
	}
	if (waxLine == nil || waxRun == nil)
		return kFalse;

	PMReal x(0.0);
	if (glyphOffset > 0)
	{
		InterfacePtr<IWaxGlyphs> waxGlyphs(waxRun, UseDefaultIID());
		if (waxGlyphs != nil)
			x = waxGlyphs->GetEscapementAt(glyphOffset - 1);
	}

	// **THE RUN'S OWN MATRIX DOES THE WORK**, and it is why this follows vertical text and rotated
	//   frames without a single branch: the run reports its position in its own space, and the
	//   matrix is what that space means on the pasteboard. (The same reason the Story marker draws
	//   correctly in vertical text -- KCMStoryMarker.cpp.)
	const PMMatrix toPasteboard = waxRun->GetToPasteboardMatrix();

	// Up and down from the baseline, as fractions of the line height - the proportions KBS settled
	// on. The midpoint of the two is what gets centred, so that the line, and not its baseline,
	// lands in the middle of the window.
	const PMReal lineHeight = waxLine->GetLineHeight();
	PMPoint above(x, -lineHeight * PMReal(0.95));
	PMPoint below(x,  lineHeight * PMReal(0.2));
	toPasteboard.Transform(&above);
	toPasteboard.Transform(&below);

	outPb = PBPMPoint((above.X() + below.X()) / PMReal(2.0),
					  (above.Y() + below.Y()) / PMReal(2.0));
	return kTrue;
}

/* ReadRowFromDocument
   Everything a row takes from the TARGET DOCUMENT ITSELF: the words it shows, the frame a click
   scrolls to, and the page that frame sits on. Answers kFalse for a story that cannot be read --
   which cannot be shown or jumped to either, so Build drops it and a refresh leaves the row
   alone.

   **ONE PLACE, TWO CALLERS.** Build fills a new row with it, and RefreshRowFromDocument fills an
   existing one again. It was written out only inside Build until a refreshed row was seen still
   showing the sentence the story USED to start with: the words are read from the document, so
   anything that re-reads the document has to read them the same way, or the two answers drift
   the moment one of them is edited.

   **fKinds IS NOT HERE**, and that is not an oversight: it comes from the two documents' change
   counters, not from the target's text (KCMStoryStamp.h), so it is not the target document's to
   answer. Nor is fPageIndex -- see RefreshRowFromDocument for why a refresh must not touch it.
*/
static bool16 ReadRowFromDocument(IDataBase* db, KCMStoryRow& row, UID storyInDb)
{
	// storyInDb is the uid the story has IN db. ⛔It is row.fStoryUID for EVERY row since 2026-09-21:
	// a Removed one read from a REHYDRATED Task Start copy used to need translating, those uids
	// being freshly handed out by the import (KCMOriginToSourceUID, retired with the origin), and a
	// copy saved to a file carries the originals.
	InterfacePtr<ITextModel> model(db, storyInDb, UseDefaultIID());
	if (model == nil)
		return kFalse;	// a story that cannot be read cannot be shown, or jumped to later

	row.fText = FirstReadableText(model);

	// **Document text is not a string key.** Left translatable, a PMString that happens to match an
	//   entry in the built-in table comes back as something else entirely -- KCM has already had
	//   "Source:" turn into a Japanese style-source label that way.
	row.fText.SetTranslatable(kFalse);

	// The frame is kept as well as the page it sits on, because the two answer different
	//   questions: the frame is WHERE TO SCROLL (a click centres it), and the page is WHERE IT
	//   BELONGS (the sort order, and the page the status line names).
	//   @warning the page is NOT how the older version's window gets aimed -- that goes by story
	//   UID, because the same story can sit somewhere else entirely over there (KCMGotoStoryFrame).
	//   Why reading this composes nothing: KCMStoryFirstFrameUID.
	row.fFrameUID = KCMStoryFirstFrameUID(db, storyInDb);
	row.fPageUID = (row.fFrameUID != kInvalidUID) ? KCMFramePageUID(db, row.fFrameUID)
												  : kInvalidUID;
	return kTrue;
}

/* AddRowsFromDocument
	Turn into rows every diff whose story lives in THIS document. Build calls it twice: once for
	the target's rows and once for the source's removed ones.

	**TWO PASSES RATHER THAN ONE LOOP THAT PICKS A db PER ROW.** The page list is a per-document
	  object, and a row needs it to answer where it sits; opening it once per row would mean a Query
	  for every row in the list. Splitting by document opens exactly two.

	**AND IT IS A FILTER, NOT A SPLIT OF THE INPUT.** Both passes see the whole diff list and take
	  the half that is theirs, so there is no intermediate vector to keep in step with the original.

	@param db the document to read from. nil adds nothing -- that is how a missing source drops its
		   removed rows, the same silent drop an unreadable story already gets.
	@param wantRemoved kTrue to take only the removed rows, kFalse to take only the others.
	@param rows appended to; not cleared.
*/
static void AddRowsFromDocument(IDataBase* db, const std::vector<KCMStoryDiff>& diffs,
                                bool16 wantRemoved, std::vector<KCMStoryRow>& rows)
{
	if (db == nil)
		return;

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());

	for (std::vector<KCMStoryDiff>::const_iterator it = diffs.begin(); it != diffs.end(); ++it)
	{
		const bool16 removed = ((it->fKinds & kKCMStoryKindRemoved) != 0) ? kTrue : kFalse;
		if (removed != wantRemoved)
			continue;	// belongs to the other document's pass

		KCMStoryRow row;
		row.fStoryUID = it->fStoryUID;
		row.fKinds = it->fKinds;
		// ⛔The uid translation went on 2026-09-21. A Task Start copy was REHYDRATED then and its
		//   stories carried numbers the import had handed out; a copy saved to a file carries the
		//   originals, so a Removed row is read under the uid it already holds.
		const UID storyInDb = row.fStoryUID;
		if (!ReadRowFromDocument(db, row, storyInDb))
			continue;

		if (row.fPageUID != kInvalidUID && pageList != nil)
		{
			// A story on a MASTER PAGE keeps kMaxInt32 and sorts to the END, which is what is wanted:
			// it is a real edit, but it belongs after the pages. IPageList::GetPageIndex counts pages
			// within the pub and a master is not one of them, so the index cannot come back as a
			// position among them.
			//
			// **MEASURED, AND MEASURED SO IT COULD HAVE COME OUT THE OTHER WAY.** The first attempt
			// built its stories in page order, which made story UID order and page order AGREE -- and
			// RowIsBefore breaks a tie by UID, so rows carrying kMaxInt32 would have come out in that
			// same order either way. The second pair built them in reverse (master first, so the master
			// story holds the LOWEST uid) and the panel listed page 1, then page 3, then the master --
			// the exact opposite of UID order.
			// @warning what is NOT distinguished: whether GetPageIndex answered negative or
			// KCMFramePageUID never produced a page UID at all. Both land here as kMaxInt32, and the row
			// goes to the end either way.
			//
			// **AND THE SECOND ARGUMENT IS LEFT AT ITS DEFAULT ON PURPOSE:** includePagesOfHiddenSpread
			// defaults to kTrue, so a page whose spread is hidden still counts. **That is what keeps Hide
			// Unchanged from renumbering this list** -- hide two spreads and the rows keep the positions
			// they had. It is the same property the comparison's own page walk depends on: IPageList
			// includes the pages of hidden spreads and enumerates them in the same order as the spread
			// walk it replaced. @warning passing kFalse here would reorder the panel every time a spread
			// is hidden.
			const int32 idx = pageList->GetPageIndex(row.fPageUID);
			if (idx >= 0)
				row.fPageIndex = idx;
		}

		rows.push_back(row);
	}
}

/* Build
*/
void KCMStoryList::Build(IDataBase* targetDB, IDataBase* sourceDB,
                           const std::vector<KCMStoryDiff>& diffs)
{
	gRows.clear();
	if (targetDB == nil)
		return;

	AddRowsFromDocument(targetDB, diffs, kFalse, gRows);

	// **THE REMOVED ROWS ARE READ OUT OF THE OLDER DOCUMENT.** Their story is not in the target
	//   at all, so there is nothing there to read a name, a frame or a page from.
	//   @warning a nil sourceDB is not an error here: those rows simply do not appear, which is
	//     the same thing that happens to a story whose ITextModel cannot be read.
	AddRowsFromDocument(sourceDB, diffs, kTrue, gRows);

	// ★★WHAT THE LAST IMPORT COULD NOT PUT IN (2026-09-19): a row for each such story - the one the
	//   comparison built, or one made here when no counter moved - and a child per refusal.
	//   ⚠Refilled on EVERY build, because the list starts empty each time; the material lives with
	//    the origin (KCMImportRefusals) and is not the list's own. Rows first, then the children, so
	//    that a story refused twice gets one row and two children whatever the order they were noted.
	{
		const std::vector<KCMImportRefusal>& refusals = KCMImportRefusals();
		std::vector<int32> rowOf(refusals.size(), -1);
		for (size_t i = 0; i < refusals.size(); ++i)
		{
			// A file with no story shows its own name, and the uid it was named after, in the text cell.
			PMString fileText = refusals[i].fFileName;
			fileText.SetTranslatable(kFalse);
			fileText.Append(" - no story ");
			fileText.AppendNumber(static_cast<int32>(refusals[i].fStory.Get()));
			fileText.Append(" in the document");
			rowOf[i] = AddRefusalRow(targetDB, refusals[i].fStory, fileText);
		}
		for (size_t i = 0; i < refusals.size(); ++i)
			AddRefusalChange(rowOf[i], refusals[i].fKind, refusals[i].fWhereAndWhy);
	}

	std::sort(gRows.begin(), gRows.end(), RowIsBefore);
}

/* Clear
*/
void KCMStoryList::Clear()
{
	gRows.clear();
}

/* GetRowCount
*/
int32 KCMStoryList::GetRowCount()
{
	return static_cast<int32>(gRows.size());
}

/* GetRow
*/
const KCMStoryRow* KCMStoryList::GetRow(int32 nth)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()))
		return nil;

	return &gRows[nth];
}

/* RowTextForTypesetting
	★★THE PALETTE DRAWS THE SIGN; INDESIGN'S COMPOSER DOES NOT (measured 2026-09-19, the first
	report exported with the sign in it): the report document is set in the document's default
	font, and U+25A6 is not in Minion, Kozuka or the other fonts a default is likely to be, so
	every sign came out as the notdef box - a box with a cross through it, which is exactly the
	"something is broken" mark a report must never carry. The palette has no such trouble because
	the UI font falls back through the system's fonts on its own; typeset text does not.
	So the report is handed WORDS, which every font has: `a[table]b`, `[table] 品名`.
	⚠A real U+25A6 typed into a story would be spelled the same way - and it would have been a
	  notdef box otherwise, so nothing is lost that the report could have shown.
*/
PMString KCMStoryList::RowTextForTypesetting(const PMString& rowText)
{
	const WideString in(rowText);
	WideString outWide;
	const int32 count = in.CharCount();
	for (int32 i = 0; i < count; ++i)
	{
		const UTF32TextChar ch = in.GetChar(i);
		if (ch.GetValue() == kKCMTableSign.GetValue())
			outWide.Append(WideString("[table]"));
		else
			outWide.Append(ch);
	}
	PMString out(outWide);
	out.SetTranslatable(kFalse);
	return out;
}

/* SetRowChanges
*/
void KCMStoryList::SetRowTargetTextCount(int32 nth, uint32 count)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()))
		return;
	gRows[nth].fTargetTextCount = count;
}

// (⛔AddReplacedChange and ClearReplacedChanges stood here and went on 2026-09-21 with the restore.
//  They kept, in fReplacedStart order, the changes the reader had taken in - the records that let a
//  row stay in the list after a write, so that an undo had something to come back to.)

/* AddRefusalRow
*/
int32 KCMStoryList::AddRefusalRow(IDataBase* targetDB, UID storyUID, const PMString& textWhenNoStory)
{
	// The comparison's own row for this story, when it built one: a story that took some of the
	// words and refused others has moved its counter, and is in the list already.
	// ⚠A Removed row of the same uid is NOT that row: it lives in the Source, and a refusal is about
	//   the Target's story (the file was named after it). ⚠Nor is a row standing for a file
	//   (kInvalidUID): two files with no story are two rows.
	for (size_t i = 0; i < gRows.size(); ++i)
	{
		if (gRows[i].fStoryUID != kInvalidUID && gRows[i].fStoryUID == storyUID
			&& (gRows[i].fKinds & kKCMStoryKindRemoved) == 0)
		{
			gRows[i].fKinds |= kKCMStoryKindRefused;
			return static_cast<int32>(i);
		}
	}

	KCMStoryRow row;
	row.fStoryUID = storyUID;
	row.fKinds = kKCMStoryKindRefused;
	if (targetDB != nil && ReadRowFromDocument(targetDB, row, storyUID))
	{
		// The same page lookup AddRowsFromDocument makes, for the same reason (a master-page story
		// keeps kMaxInt32 and sorts to the end of its group).
		InterfacePtr<IPageList> pageList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
		if (row.fPageUID != kInvalidUID && pageList != nil)
		{
			const int32 idx = pageList->GetPageIndex(row.fPageUID);
			if (idx >= 0)
				row.fPageIndex = idx;
		}
	}
	else
	{
		// ★★NO SUCH STORY IN THE DOCUMENT - or the uid names something that is not one (ReadRowFromDocument
		//   asks for ITextModel): the row stands for the FILE. **Its uid is kInvalidUID from here on**,
		//   the value every reader of this list passes over (the header says which), so that a number
		//   that names nothing here - or a swatch, or a page - is never handed to the document as a
		//   story. The file's own uid is in the text, for the reader.
		row.fStoryUID = kInvalidUID;
		row.fText = textWhenNoStory;
		row.fText.SetTranslatable(kFalse);
		row.fFrameUID = kInvalidUID;
		row.fPageUID = kInvalidUID;
	}
	gRows.push_back(row);
	return static_cast<int32>(gRows.size()) - 1;
}

/* AddRefusalChange
*/
void KCMStoryList::AddRefusalChange(int32 nth, const PMString& kind, const PMString& whereAndWhy)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()))
		return;

	KCMStoryChange c;
	c.fWhat = KCMStoryChange::kRefused;
	c.fTextPre = kind;
	c.fTextPre.SetTranslatable(kFalse);
	c.fText = whereAndWhy;
	c.fText.SetTranslatable(kFalse);
	// (⛔It also said "nothing here can be written back", which the menu asked first, until 2026-09-21.)
	gRows[nth].fRefusals.push_back(c);
}

// (⛔**FIVE MORE WENT ON 2026-09-21**, all of them the restore's bookkeeping: ReplacedSlotFor (where
//  a new record belonged, with the caret tie rule the panel was shown); AddReplacedChangeAt;
//  ShiftOnePlace and ShiftReplacedChanges, which slid the records a later write in the same story
//  had moved - including the mark spans a Table row's jump aimed at; and MergedSlots, which fed the
//  two ascending lists to KCMStoryRowMerge. ★**KCMStoryRowMerge went with them** - it had no other
//  caller - and with it the offline test that covered the tie rules.)


/* GetMergedChangeCount
*/
int32 KCMStoryList::GetMergedChangeCount(int32 nth)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()))
		return 0;
	const KCMStoryRow& row = gRows[nth];
	return static_cast<int32>(row.fRefusals.size() + row.fChanges.size());
}

/* GetMergedChange
*/
const KCMStoryChange* KCMStoryList::GetMergedChange(int32 nth, int32 which)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()) || which < 0)
		return nil;

	const KCMStoryRow& row = gRows[nth];

	// ★THE REFUSALS COME FIRST (2026-09-19): what an import could not put in, before the changes it
	//   did. They take the first fRefusals.size() indices, and the live changes count from there.
	if (which < static_cast<int32>(row.fRefusals.size()))
		return &row.fRefusals[which];
	which -= static_cast<int32>(row.fRefusals.size());

	// (⛔**A THIRD LIST STOOD BETWEEN THEM UNTIL 2026-09-21**: the changes the reader had taken in,
	//  kept so that a row stayed in the list after a restore wrote, and merged with the live ones in
	//  TEXT order - which is why this was ever more than an index. Nothing is taken in now, so the
	//  merged index is the refusals followed by the live changes, and the name "merged" is history.)
	return (which < static_cast<int32>(row.fChanges.size())) ? &row.fChanges[which] : nil;
}

// (⛔ReplacedSlotOfMerged and RemoveReplacedChangeAt went the same day: the first answered which
//  record a merged index had fallen on, the second dropped one an undo had put back.)


void KCMStoryList::SetRowChanges(int32 nth, const std::vector<KCMStoryChange>& changes,
								   bool16 textCompared)
{
	// The same bounds test GetRow makes, and for the same reason: the list is rebuilt by one
	// comparison and thrown away by the next, so an index is only ever as good as the moment it
	// was handed out.
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()))
		return;

	gRows[nth].fChanges = changes;
	gRows[nth].fTextCompared = textCompared;

	// **WHICH ATTRIBUTE THE ROW SHOULD NAME**, worked out here rather than asked for later: the
	//   row is drawn many times and the children are walked once.
	// **FIRST ONE NAMES THE ROW, AND THE COUNT SAYS WHETHER THERE WERE MORE** (2026-09-03, user's
	//   ask: "Ruby+" / "Kenten+" when both moved). The second half is exactly the "one more fact on
	//   the row" this comment used to say a "Ruby+" would need - how many kinds were seen - and it
	//   is worked out in the same walk, so the two cannot disagree about which children exist.
	//   ⚠DISTINCT kinds, not attribute children: four ruby edits are one kind. The children are in
	//     reading order (ChangeIsBefore), so "first" is the kind that stands earliest in the story.
	// **THE CHILD CARRIES THE ANSWER**, so this does not guess it from which string is filled. The
	//   old test -- "fRuby is not empty, or fOtherRuby is" -- was really asking "is this a ruby",
	//   and kenten showed within a day why that is not the same question: it filled the very same
	//   fields with a KIND rather than a reading, and every such test called it a ruby.
	gRows[nth].fAttrKind = kKCMStoryAttrNone;
	gRows[nth].fAttrKindCount = 0;
	// ★**AND WHETHER THE WORDS THEMSELVES MOVED** - worked out in the same walk, for the reason on
	//   KCMStoryRow::fHasTextChange: a note's marker is a character, so the change counters answer
	//   "the text changed" for an edit whose only visible difference is a note. What the column
	//   needs is what the DIFF found.
	gRows[nth].fHasTextChange = kFalse;

	// **THE KINDS ALREADY MET, AND NOT A BIT PER KIND.** This held `1u << fAttrKind` until
	//   2026-09-04, which is correct for the values that exist (0, 1, 2) and undefined for the
	//   thirty-second -- and KCMStoryKinds.h invites exactly that, in as many words: a third
	//   attribute is "one more value". A list costs the same to read, cannot be made undefined by
	//   accepting that invitation, and does not have to be counted against the width of a uint32.
	std::vector<int32> kindsSeen;
	for (size_t i = 0; i < changes.size(); ++i)
	{
		if (changes[i].fWhat != KCMStoryChange::kAttr ||
			changes[i].fAttrKind == kKCMStoryAttrNone)
		{
			// ★A table's row counts as the words having changed (2026-09-19 night): its cells' text
			//   changes were folded into it, and the row's Change column must not fall back to "Attr".
			if (changes[i].fWhat == KCMStoryChange::kText || changes[i].fWhat == KCMStoryChange::kTable)
				gRows[nth].fHasTextChange = kTrue;
			continue;
		}

		const int32 kind = static_cast<int32>(changes[i].fAttrKind);
		if (std::find(kindsSeen.begin(), kindsSeen.end(), kind) != kindsSeen.end())
			continue;
		kindsSeen.push_back(kind);

		if (gRows[nth].fAttrKind == kKCMStoryAttrNone)
			gRows[nth].fAttrKind = changes[i].fAttrKind;
		++gRows[nth].fAttrKindCount;
	}
}

/* RowIsSettingOnly
	The one row std::remove_if is looking for: a story that differs only in how it is set.

	**IT ONLY ADAPTS -- THE DECISION IS KCMStoryRowFilter.h's.** All this does is read the three
	fields off the row and turn "keep" into "remove", which is the shape remove_if wants. The rule
	itself has to stay where it can be built without InDesign and checked case by case
	(work/kescm-rowfilter-test); a copy of it here would be a second answer to the same question.
*/
static bool RowIsSettingOnly(const KCMStoryRow& row)
{
	return KCMStoryRowHasContentChange(row.fKinds, row.fTextCompared,
										 static_cast<int32>(row.fChanges.size())) == kFalse;
}

/* DropRowsWithNoContentChange
*/
void KCMStoryList::DropRowsWithNoContentChange()
{
	gRows.erase(std::remove_if(gRows.begin(), gRows.end(), RowIsSettingOnly), gRows.end());
}

/* RefreshRowFromDocument
*/
void KCMStoryList::RefreshRowFromDocument(int32 nth, IDataBase* targetDB)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()) || targetDB == nil)
		return;

	// A story that has since been deleted answers kFalse and the row keeps what it had. That is the
	// same rule the whole feature follows: a row the comparison found must not vanish because
	// something about it could not be worked out a second time.
	(void)ReadRowFromDocument(targetDB, gRows[nth], gRows[nth].fStoryUID);
}

/* ShutdownCleanup
*/
void KCMStoryList::ShutdownCleanup()
{
	// Assigning a fresh vector releases the storage too, not just the contents, so the static
	// destructor at DLL unload finds nothing left to do. clear() would leave the rows' PMStrings
	// holding their buffers -- the very thing this call exists to prevent
	// (KBSResultModel::ShutdownCleanup does it the same way, and says why).
	gRows = std::vector<KCMStoryRow>();
}

//----------------------------------------------------------------------------------------
// RowsAsTsv -- the reading port. See the header for why it exists.
//----------------------------------------------------------------------------------------

namespace
{

/** One field, with the two characters that would break a TSV taken out.

	⚠**THE TEXT PIECES ARE ALREADY MARKED UP** (KCMStoryDiffRun's MarkUpBreaks turns a paragraph
	 end into ¶ and a line break into ↵), so this catches what that does not reach: a reading, a
	 kenten kind, and any field a future change adds. A tab inside a field would silently shift
	 every column after it - the failure that reads as "the plug-in reported the wrong thing".
*/
std::string Field(const PMString& value)
{
	std::string utf8 = value.GetUTF8String();
	for (size_t i = 0; i < utf8.size(); ++i)
	{
		if (utf8[i] == '\t' || utf8[i] == '\n' || utf8[i] == '\r')
			utf8[i] = ' ';
	}
	return utf8.empty() ? std::string("-") : utf8;
}

std::string Num(int32 n)
{
	std::ostringstream s;
	s << n;
	return s.str();
}

/** Which change counters moved, as words - the raw material of the panel's Change column.

	⚠**NOT THE COLUMN'S WORDING.** The '+' and the "name the first, sign that there were more"
	 rule live in the UI half (KCMStoryTreeWidgetMgr::KindLabel). Repeating them here would put one
	 judgement in two plug-ins.
*/
std::string KindsWord(uint32 kinds)
{
	std::string s;
	if (kinds & kKCMStoryKindAdded)   s += (s.empty() ? "" : ",") + std::string("Added");
	if (kinds & kKCMStoryKindRemoved) s += (s.empty() ? "" : ",") + std::string("Removed");
	if (kinds & kKCMStoryKindText)    s += (s.empty() ? "" : ",") + std::string("Text");
	if (kinds & kKCMStoryKindAttr)    s += (s.empty() ? "" : ",") + std::string("Attr");
	if (kinds & kKCMStoryKindOther)   s += (s.empty() ? "" : ",") + std::string("Other");
	if (kinds & kKCMStoryKindRefused) s += (s.empty() ? "" : ",") + std::string("Refused");	// an import could not fill it (2026-09-19)
	return s.empty() ? std::string("-") : s;
}

/** Which attribute a row or a change is about. ⚠The values are KCMStoryAttrKind's, so a kind
	added there and forgotten here comes out as "Attr<n>" rather than as silence. */
std::string AttrWord(int32 attrKind)
{
	switch (attrKind)
	{
		case kKCMStoryAttrNone:		return "-";
		case kKCMStoryAttrRuby:		return "Ruby";
		case kKCMStoryAttrKenten:	return "Kenten";
		case kKCMStoryAttrFootnote:	return "Footnote";
		case kKCMStoryAttrEndnote:	return "Endnote";
		case kKCMStoryAttrWarichu:	return "Warichu";
		case kKCMStoryAttrTcy:		return "TCY";		// the tagged-text name for it (TTImportExportAttrId.h)
		default:					return "Attr" + Num(attrKind);
	}
}

/** What sort of edit one change is. Same three the row draws as + - ≠. */
std::string ChangeKindWord(int32 kind)
{
	switch (kind)
	{
		case KCMStoryChange::kInsert:	return "insert";
		case KCMStoryChange::kDelete:	return "delete";
		case KCMStoryChange::kReplace:	return "replace";
		default:						return "kind" + Num(kind);
	}
}

/** The three facts about a row that the Change column's rule reads, beside the counters.

	★**hasText IS THE ONE THAT MATTERS MOST HERE** (2026-09-08): the column names an attribute only
	when the DIFF found no text change, and a note's marker is a character, so the counters and the
	diff disagree exactly where footnotes are involved. Printing both is what lets a reader see
	which of the two the column obeyed.
*/
/** How a ruby is SET, for the row that reports one - "Mono" or "Group", the two words the panel
	draws on the upper line.

	★★★**IT NEEDS A COLUMN OF ITS OWN, and finding that out is what this port is for** (2026-09-08).
	The first version of RowsAsTsv printed the READING in `value` and stopped there, so a
	mono-to-group change - where both readings are identical and only the setting moved - came out
	as `Ruby replace こはく 琥珀`: two rows that differ in nothing. The panel shows the difference
	(LIST-17), the table did not, and a reader checking the panel against the table would have
	concluded the panel was inventing it.
	⚠**BOTH SIDES.** "it is mono now" is only half the fact; what changed is mono AGAINST group.
	⚠**RUBY ONLY** - kenten has no such distinction and a note's marker has none either, so they
	 print "-" rather than a word that would read as a claim about them.
*/
std::string RubySetting(const KCMStoryChange& c)
{
	if (c.fAttrKind != kKCMStoryAttrRuby)
		return "-";

	std::string s;
	s += c.fOtherRuby.IsEmpty() ? "-" : (c.fOtherRubyGroup ? "Group" : "Mono");
	s += ">";
	s += c.fRuby.IsEmpty() ? "-" : (c.fRubyGroup ? "Group" : "Mono");
	return s;
}

/** Where a change's words stand (KCMStoryPlace) - the fact the UI's "Text" / "Cell Text" / "Note Text"
	are made of, not the words themselves (KCMStoryTreeWidgetMgr::PlaceIdLabel owns those; a whole
	paragraph says "Paragraph" wherever it stands, and a whole cell "Cell" - the `whole` column, 1 / 2). */
std::string PlaceWord(int32 place)
{
	switch (place)
	{
		case kKCMPlaceBody:	return "body";
		case kKCMPlaceCell:	return "cell";
		case kKCMPlaceNote:	return "note";
		default:			return "place" + Num(place);
	}
}

/** The columns every line carries after `text` (2026-09-19 night), or "-" for each where a line has
	nothing to say (a story row, a refusal). Listed once so that the header, the parent line and the
	change line cannot disagree about how many there are. */
const int32 kTrailingColumns = 8;	// state whole place tstart tend sstart send other

std::string NoTrailing()
{
	std::string s;
	for (int32 i = 0; i < kTrailingColumns; ++i)
		s += "\t-";
	return s;
}

std::string FlagsWord(const KCMStoryRow& row)
{
	std::string s;
	if (row.fTextCompared)  s += "compared";
	if (row.fHasTextChange) s += (s.empty() ? "" : ",") + std::string("hasText");
	s += (s.empty() ? "" : ",") + std::string("attrs=") + Num(row.fAttrKindCount);
	s += ",changes=" + Num(static_cast<int32>(row.fChanges.size()));
	if (!row.fRefusals.empty())
		s += ",refused=" + Num(static_cast<int32>(row.fRefusals.size()));
	return s;
}

}	// anonymous namespace

/* RowsAsTsv
*/
void KCMStoryList::RowsAsTsv(PMString& out)
{
	std::string s = "row\tchange\tuid\tkinds\tflags\tattr\tkind\tset\tvalue\ttext"
					"\tstate\twhole\tplace\ttstart\ttend\tsstart\tsend\tother\r\n";

	// ★THE SHOWN RANGES COME THROUGH THE FACADE (2026-09-19 night), because that is the ONE place a whole
	//   paragraph's break is cut off them (KCMFacades' GetChange, KCMShownSpan): what a test reads here is
	//   what the marks, the jump and the double click use, not the model's write range.
	//   ⚠Guarded: the facade lives on kUtilsBoss, which can be gone during teardown (the same test
	//    KCMStoryMarkBuild's caller makes).
	Utils<IKCMStoryEditsFacade> utils;
	InterfacePtr<IKCMStoryEditsFacade> facade(utils ? utils.QueryUtilInterface() : nil);

	for (size_t i = 0; i < gRows.size(); ++i)
	{
		const KCMStoryRow& row = gRows[i];
		const int32 nth = static_cast<int32>(i);

		// The PARENT line: what the story row itself says.
		s += Num(nth) + "\t-\t"
		   + ((row.fStoryUID == kInvalidUID) ? std::string("-") : Num(static_cast<int32>(row.fStoryUID.Get())))	// "-": a row standing for a file (2026-09-19)
		   + "\t" + KindsWord(row.fKinds)
		   + "\t" + FlagsWord(row)
		   + "\t" + AttrWord(static_cast<int32>(row.fAttrKind))
		   + "\t-\t-\t-\t" + Field(row.fText) + NoTrailing() + "\r\n";

		// ★One line per CHILD, IN THE PANEL'S OWN INDEX SPACE (2026-09-19 night): the refusals first, then
		//   the live changes and the ones already taken in, merged in text order - exactly the numbers
		//   kcmTakeInChange / kcmUndoRestore count in and the tree draws. ⚠Until then this printed the
		//   live changes alone, so after a take-in its `change` column and the panel's rows disagreed.
		//   `kind` says "refused" for what an import could not put in (`set` the kind word, `text`
		//   where and why); `state` says live / replaced / undone for the rest.
		// ⚠**fRuby IS THE VALUE COLUMN**, and it holds a reading for a ruby, a kind for a kenten and a
		//   NUMBER for a note - which is precisely the field no reader outside could see before this
		//   port existed.
		const int32 count = GetMergedChangeCount(nth);
		for (int32 k = 0; k < count; ++k)
		{
			const KCMStoryChange* const cp = GetMergedChange(nth, k);
			if (cp == nil)
				continue;
			const KCMStoryChange& c = *cp;

			if (c.fWhat == KCMStoryChange::kRefused)
			{
				s += Num(nth) + "\t" + Num(k)
				   + "\t-\t-\t-\t-\trefused\t" + Field(c.fTextPre) + "\t-\t" + Field(c.fText) + NoTrailing() + "\r\n";
				continue;
			}

			// ⛔The column said "replaced" or "undone" for a change the reader had taken in, until the
			//  restore went on 2026-09-21. It is kept, and says "live", so that a reader of the TSV
			//  written before that date and one written after can still be put side by side.
			const std::string state = "live";

			IKCMStoryEditsFacade::Change shown;
			const bool16 haveShown = (facade != nil && facade->GetChange(nth, k, shown)) ? kTrue : kFalse;

			s += Num(nth) + "\t" + Num(k)
			   + "\t-\t-\t-\t" + AttrWord(static_cast<int32>(c.fAttrKind))
			   + "\t" + ChangeKindWord(static_cast<int32>(c.fKind))
			   + "\t" + RubySetting(c)
			   + "\t" + Field(c.fRuby)
			   + "\t" + Field(c.fText)
			   + "\t" + state
			   + "\t" + ((c.fWhat == KCMStoryChange::kTable) ? "3" : (c.fWholeParagraph ? "1" : "0"))	// 3 = a TABLE (2026-09-19 night; 2, a whole cell, lived one evening)
			   + "\t" + PlaceWord(c.fPlace)
			   + "\t" + (haveShown ? Num(shown.fTargetStart) : std::string("-"))
			   + "\t" + (haveShown ? Num(shown.fTargetEnd) : std::string("-"))
			   + "\t" + (haveShown ? Num(shown.fSourceStart) : std::string("-"))
			   + "\t" + (haveShown ? Num(shown.fSourceEnd) : std::string("-"))
			   + "\t" + Field(c.fOtherText) + "\r\n";
		}
	}

	out.SetUTF8String(s);
	out.SetTranslatable(kFalse);	// document text rides in here - see SetDocumentText's note
}

// End, KCMStoryList.cpp.
