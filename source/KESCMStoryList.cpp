//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryList.h for what this list is and why it is a file static.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IComposeScanner.h"
#include "IFrameList.h"
#include "IFrameListComposer.h"	// RecomposeThruLastFrame - the wax read below is a RESULT of composition
#include "IGeometry.h"			// the frame's inner->pasteboard matrix, for where a story begins
#include "IPageList.h"
#include "IParcelList.h"		// GetFirstParcelKey / GetNextParcelKey / GetParcelToFrameMatrix
#include "ITextModel.h"
#include "ITextParcelList.h"	// QueryTextParcelList - the parcels a story flows through
#include "IWaxGlyphs.h"			// GetEscapementAt - how far into the run one character sits
#include "IWaxIterator.h"		// GetFirstWaxLine - the line a TextIndex was composed onto
#include "IWaxLine.h"
#include "IWaxRun.h"			// GetToPasteboardMatrix
#include "IWaxStrand.h"

// General includes:
#include "K2SmartPtr.h"			// K2::scoped_ptr - what NewWaxIterator hands back has to be deleted
#include "PMMatrix.h"
#include "PMRect.h"				// GetParcelBounds - the leading corner comes off this
#include "TextChar.h"			// kTextChar_Space - the boundary the readability test draws its line at
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "UnicodeClass.h"		// IsWhiteSpace
#include "WideString.h"

#include <algorithm>			// std::sort
#include <vector>

// Project includes:
#include "KESCMCore.h"			// KESCMFramePageUID - shared with the overset scan since 2026-08-09
#include "KESCMStoryList.h"

namespace
{

// The list. See the header for why this is a file static rather than a boss.
//
// *** NO LOCK, AND THE REASON IS THAT NOTHING ON A BACKGROUND THREAD READS IT. *** Every caller
// runs on the main thread. FIVE of them, re-counted one by one on 2026-08-17 (the 2026-08-16 count
// named four and folded the two Clear() callers into one):
//   1. Build()            - the comparison (KESCMCore::KESCMRebuildStoryEdits, itself reached from
//                           Start and from Refresh Page Comparison, both main-thread commands);
//   2. GetRowCount/GetRow - the panel, through the facade (KESCMFacades.cpp) - and a UI plug-in's
//                           bosses are invisible to a background thread anyway;
//   3. Clear()            - Stop (KESCMCore.cpp, KESCMDoClearMarks);
//   4. Clear()            - a compared document closing (KESCMPeek.cpp, which the IsMainThread
//                           guard on that path already covers);
//   5. ShutdownCleanup()  - the shutdown service.
// ***** The background thread runs the DRAWING path only, and the drawing path never asks about
// story edits ***** - it reads the page map and the mark state, neither of which is here.
//
// ⚠WHAT WOULD BREAK IT: drawing a story-edit marker on the page (a badge on a changed story's
// frame, say), or building this list from a background export. Either one puts a reader on the BG
// thread, and then this needs KESCMMarkStateLock exactly as the mark maps do.
//
// ★Same shape as KESCMDrawEventHandler::DropAllOrig (audit B5): what was missing there was not a
// lock but the REASON - and the condition under which the reason stops holding.
std::vector<KESCMStoryRow> gRows;

// A safety valve, NOT a display limit. How much of a story's opening text is shown is decided by
// the row's text cell alone: it is kEllipsizeMiddle and bound kBindLeft|kBindRight (the cell is
// kKESCMStoryRowTextWidgetID, declared UI-side in ui/KCMUI.fr - ⚠this note said "KESCM.fr" until
// 2026-08-17, which is this file's own copy of the stale name audit B4 fixed in KESCMPageMap.cpp;
// that fix did not go looking for its siblings, and this was the only other one), so
// it is the one cell that grows when the panel is widened, and it shortens its own text to fit.
//
// ★This panel already answered this exact question once. KESCMDocNameFromDB used to shorten the
//   Target / Source document names by character count in C++; the block 8 A-2 audit (2026-08-06)
//   deleted that and left the shortening to kEllipsizeMiddle, so the question is asked in one
//   place only (memory one-question-one-place). A character cap here was the same duplicate, and
//   it went unnoticed until the panel became resizable on 2026-08-10: from then on 30 characters
//   silently capped every width, so widening the panel bought empty space rather than more text.
//   The old cap took its number from SnpCreateCrossReference.cpp:179, which names stories in a
//   fixed-width dialog - a list that cannot be resized has no such question to answer.
//
// The number below only has to sit past anything a cell could ever show. A palette-font character
// is never narrower than about 3px, so even a text cell spanning a 4K screen (~3700px) runs out
// before ~1300 characters. 2000 leaves room and still bounds a pathological single-paragraph
// story, which is the only thing this limit exists to stop.
//
// No ellipsis is appended when the text is cut here: the cell adds its own, and a string already
// ending in "..." would be shortened again around it.
const int32 kRowTextSafetyLimit = 2000;

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
	// characters into the control range - which is what IsK2SpecificChar is for: "the low-ascii
	// characters that have meaning to InDesign ... standard values like tab, carriage return and
	// special ones like IndentToHere, Table" (TextChar.h, its declaration).
	//
	// ⚠This used to cite IsIllegalControlChar as drawing the same line "too". It does not: it is
	// (n < kTextChar_Space && !IsK2SpecificChar(n)), so it EXCLUDES the very characters this test
	// exists to drop. The SDK has no predicate for what is wanted here, which is why the boundary
	// is spelled out rather than borrowed.
	if (v < kTextChar_Space)
		return kFalse;

	if (v == kTextChar_ObjectReplacementCharacter)
		return kFalse;			// an inline graphic is not text

	// Spaces of every width, the ideographic one included - a paragraph of them says nothing.
	if (UnicodeClass::IsWhiteSpace(ch))
		return kFalse;

	return kTrue;
}

/* FirstReadableText
	The opening words of a story, or an empty string when it has none to show.

	The scan runs paragraph by paragraph and stops at the first paragraph holding anything readable.
	It runs to TotalLength() rather than to the end of the primary thread on purpose: table cells and
	footnotes are further threads inside the SAME model, so a frame holding nothing but a table only
	says something once the scan is allowed past the main thread. ⚠The headers state this for TABLE
	CELLS only - TotalLength "including data for embedded tables" against GetPrimaryStoryThreadSpan
	"does not include any characters that are part of story threads for table cells"
	(ITextModel.h:137-145). That FOOTNOTES sit there too is measured, not documented: audit B6
	scanned a document whose only overset was a footnote and got its one stop (2026-08-17).

	★Where this parts company with the official example. SnpCreateCrossReference.cpp:206-222 does
	the same job and copies span-1 characters so that the paragraph's CR is left behind. Here the
	whole span is copied and unreadable characters are filtered out instead, because this list has a
	requirement the snippet does not: a paragraph made of nothing but control characters has to be
	SKIPPED so the scan moves on to the next one, which cannot be decided without looking at the
	characters anyway. Filtering also steps around the snippet's edge case, where span-1 eats a real
	character in the last paragraph of a story that ends without a CR.
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

	while (pos < total)
	{
		int32 span = 0;

		// excludeEOS = kFalse so that a story ending without a CR still reports its last paragraph,
		// which is the only paragraph a short story has.
		// ★THE DEFAULT REVERSED - AND ADOBE REVERSES IT FOR THIS SAME JOB. The parameter defaults to
		// kTrue (IComposeScanner.h:94). Every call to it in the SDK, counted 2026-08-17, and they
		// split by what the walk is FOR:
		//   - walking text the user is EDITING takes the default, because there the end-of-story
		//     mark is not a paragraph worth reporting: spellpanel's SpellCheckWalkerData.cpp:435,452
		//     and AutoCorrectTypingIdleTask.cpp:353;
		//   - walking stories in order to NAME them passes kFalse, exactly as here:
		//     SnpCreateCrossReference.cpp:206 - the same snippet this file takes as its example
		//     twice over (see kRowTextSafetyLimit and the note below).
		// This list is looking for the first words that exist AT ALL, so a story that is one
		// CR-less paragraph long has to count.
		//
		// ⚠Until 2026-08-17 this note read "all three of Adobe's own calls take that default". It
		// had counted the product code and left out the snippet it was already quoting - so the one
		// official call that had made the SAME choice was the one missing from the count.
		const TextIndex paraStart = scanner->FindSurroundingParagraph(pos, &span, kFalse);
		if (paraStart < 0 || span <= 0)
			break;

		WideString para;
		scanner->CopyText(paraStart, span, &para);

		WideString kept;
		bool16 pendingGap = kFalse;
		const int32 charCount = para.CharCount();

		for (int32 i = 0; i < charCount && kept.CharCount() < kRowTextSafetyLimit; ++i)
		{
			const UTF32TextChar ch = para.GetChar(i);

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
		}

		if (!kept.empty())
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

	return out;
}

/* RowIsBefore
	Page order, with the pageless rows last (their index is kMaxInt32).
*/
bool RowIsBefore(const KESCMStoryRow& a, const KESCMStoryRow& b)
{
	// ★★FIRST KEY: WHICH DOCUMENT THE ROW LIVES IN (2026-08-21). Every target row comes before
	//   every removed one, and only then does the page order below apply - within each group.
	//   ⇒ The page numbers in the column then come from ONE document at a time, in order, and the
	//     reader is not asked to notice that the column changed documents part-way down (user's
	//     call; it is also the order Export Changed Pages has always used).
	//   ⚠NOT DONE BY GIVING REMOVED ROWS kMaxInt32: that would drop them in among the master-page
	//     and pasteboard rows, and leave removed rows ordered by uid rather than by page - "which
	//     document" and "has a page" are different questions and need different keys.
	//   ★It also keeps the uid tie-break below honest: it now only ever compares two uids from the
	//     same document, and a uid means nothing across documents.
	const bool aRemoved = (a.fKinds & kKESCMStoryKindRemoved) != 0;
	const bool bRemoved = (b.fKinds & kKESCMStoryKindRemoved) != 0;
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

/* KESCMStoryFirstFrameUID (declared in KESCMStoryList.h)

	★RecomposeThruLastFrame is deliberately NOT called. This asks where the story STARTS, not where
	its text overflows, so there is nothing to compose - and composing here would cost the property
	stage 1 measured and wrote down: reading what changed changes nothing (KESCMStoryStamp.h,
	"READING COUNTERS COMPOSES NOTHING").
*/
UID KESCMStoryFirstFrameUID(IDataBase* db, UID storyUID)
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

/* KESCMStoryStartPoint (declared in KESCMStoryList.h)

	The mirror image of the overset scan's KESCMLastPlacedOutport: that one walks BACK from the last
	parcel to find where the text stopped fitting; this walks FORWARD from the first to find where it
	started. Same three coordinate spaces, same reason vertical text needs no branch.
*/
bool16 KESCMStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb)
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

/* KESCMStoryPointAt (declared in KESCMStoryList.h)

	Where ONE character of a story sits on the pasteboard - what a jump to a change needs, as
	against KESCMStoryStartPoint above, which answers where the whole story begins.

	★★★PORTED FROM KBSJump.cpp (user's pointer, 2026-08-22: "KBS には検索結果に飛ぶのがあるので
	  それを参考にしてもらっていいかも"). KBS's own copy says "ported from KESCLFindInDoc", so this
	  is the third plug-in in this family to carry the same recipe, and the two before it have
	  already paid for the corrections written into it - the overset test, and the recompose.
	  ⇒ The shape is theirs on purpose: GetFirstWaxLine -> QueryRunByTextOffset ->
	    GetEscapementAt -> GetToPasteboardMatrix.

	★★COMPOSITION IS BROUGHT UP TO DATE FIRST, AND THAT IS THE ONE DIFFERENCE FROM ITS NEIGHBOUR.
	  KESCMStoryFirstFrameUID says, in as many words, that it deliberately does NOT compose - and it
	  is right to, because it asks which parcels EXIST, which composition does not decide. This asks
	  where a character was PUT, which is nothing but a result of composition: read without
	  composing and the answer is wherever that character stood before the last edit.
	  The recipe is the SDK's (IFrameList::GetFirstDamagedFrameIndex() != -1 ->
	  IFrameListComposer::RecomposeThruLastFrame, SnpInspectTextModel.cpp:724-733); KESCM already
	  spells it the same way where it asks about overset (KESCMOversetScan.cpp).
	  ⚠**COMPOSING DIRTIES THE DOCUMENT**, so the caller must hold a
	    IDataBase::SaveRestoreModifiedState. That is a change of contract for the jump path, which
	    measured itself clean in 2026-08-18 precisely because nothing on it touched the model -
	    and its own comment says to measure again if anything ever did. This is that thing.

	@param index the character to find. Clamped by the caller; an index past the end simply has no
		wax line and answers kFalse.
	@param outPb [out] the middle of that character's line, in pasteboard coordinates. Untouched
		when this answers kFalse.
	@return kFalse when the story is not there, the position is OVERSET or in no frame, or the
		text has not been composed and cannot be - callers fall back to the story's start.
*/
bool16 KESCMStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb)
{
	if (db == nil || storyUID == kInvalidUID || index < 0)
		return kFalse;

	InterfacePtr<ITextModel> textModel(db, storyUID, UseDefaultIID());
	if (textModel == nil)
		return kFalse;

	InterfacePtr<IWaxStrand> waxStrand((IWaxStrand*)textModel->QueryStrand(kFrameListBoss, IID_IWAXSTRAND));
	if (waxStrand == nil)
		return kFalse;

	InterfacePtr<IFrameList> frameList(waxStrand, UseDefaultIID());
	if (frameList != nil && frameList->GetFirstDamagedFrameIndex() != -1)
	{
		InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
		if (composer != nil)
			composer->RecomposeThruLastFrame();
	}

	K2::scoped_ptr<IWaxIterator> waxIter(waxStrand->NewWaxIterator());
	if (waxIter == nil)
		return kFalse;

	int32 offsetInLine = 0;
	IWaxLine* waxLine = waxIter->GetFirstWaxLine(index, &offsetInLine);
	if (waxLine == nil)
		return kFalse;			// overset, or not placed at all - there is no "where" to answer with

	// Which run holds that character, and how far into the run it is. ★The escapement is measured
	// up to the glyph BEFORE it, which is the start of the character rather than its far edge.
	int32 glyphOffset = -1;
	InterfacePtr<IWaxRun> waxRun(waxLine->QueryRunByTextOffset(offsetInLine, &glyphOffset));
	if (waxRun == nil)
		return kFalse;

	PMReal x(0.0);
	if (glyphOffset > 0)
	{
		InterfacePtr<IWaxGlyphs> waxGlyphs(waxRun, UseDefaultIID());
		if (waxGlyphs != nil)
			x = waxGlyphs->GetEscapementAt(glyphOffset - 1);
	}

	// ★THE RUN'S OWN MATRIX DOES THE WORK, and it is why this follows vertical text and rotated
	//   frames without a single branch: the run reports its position in its own space, and the
	//   matrix is what that space means on the pasteboard. (The same reason the Story marker draws
	//   correctly in vertical text - KESCMStoryMarker.cpp.)
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
   scrolls to, and the page that frame sits on. Answers kFalse for a story that cannot be read -
   which cannot be shown or jumped to either, so Build drops it and a refresh leaves the row alone.

   ★ONE PLACE, TWO CALLERS (2026-08-21). Build fills a new row with it, and RefreshRowFromDocument
   fills an existing one again. It was written out only inside Build until a refreshed row was seen
   still showing the sentence the story USED to start with (user's report): the words are read from
   the document, so anything that re-reads the document has to read them the same way, or the two
   answers drift the moment one of them is edited.

   ★fKinds IS NOT HERE, and that is not an oversight: it comes from the two documents' change
   counters, not from the target's text (KESCMStoryStamp.h), so it is not the target document's to
   answer. Nor is fPageIndex - see RefreshRowFromDocument for why a refresh must not touch it.
*/
static bool16 ReadRowFromDocument(IDataBase* db, KESCMStoryRow& row)
{
	InterfacePtr<ITextModel> model(db, row.fStoryUID, UseDefaultIID());
	if (model == nil)
		return kFalse;	// a story that cannot be read cannot be shown, or jumped to later

	row.fText = FirstReadableText(model);

	// ★Document text is not a string key. Left translatable, a PMString that happens to match an
	//   entry in the built-in table comes back as something else entirely - KESCM has already
	//   had "Source:" turn into a Japanese style-source label that way.
	row.fText.SetTranslatable(kFalse);

	// ★The frame is kept as well as the page it sits on, because the two answer different
	//   questions: the frame is WHERE TO SCROLL (a click centres it), and the page is WHERE IT
	//   BELONGS (the sort order, and the page the status line names).
	//   ⚠The page is NOT how the older version's window gets aimed - that goes by story UID,
	//   because the same story can sit somewhere else entirely over there (2026-08-10; see
	//   KESCMGotoStoryFrame). Why reading this composes nothing: KESCMStoryFirstFrameUID.
	row.fFrameUID = KESCMStoryFirstFrameUID(db, row.fStoryUID);
	row.fPageUID = (row.fFrameUID != kInvalidUID) ? KESCMFramePageUID(db, row.fFrameUID)
												  : kInvalidUID;
	return kTrue;
}

/* AddRowsFromDocument
	Turn into rows every diff whose story lives in THIS document. Build calls it twice: once for the
	target's rows and once for the source's removed ones (2026-08-21).

	★TWO PASSES RATHER THAN ONE LOOP THAT PICKS A db PER ROW. The page list is a per-document
	  object, and a row needs it to answer where it sits; opening it once per row would mean a Query
	  for every row in the list. Splitting by document opens exactly two.

	★AND IT IS A FILTER, NOT A SPLIT OF THE INPUT. Both passes see the whole diff list and take the
	  half that is theirs, so there is no intermediate vector to keep in step with the original.

	@param db the document to read from. nil adds nothing - that is how a missing source drops its
	       removed rows, the same silent drop an unreadable story already gets.
	@param wantRemoved kTrue to take only the removed rows, kFalse to take only the others.
	@param rows appended to; not cleared.
*/
static void AddRowsFromDocument(IDataBase* db, const std::vector<KESCMStoryDiff>& diffs,
                                bool16 wantRemoved, std::vector<KESCMStoryRow>& rows)
{
	if (db == nil)
		return;

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());

	for (std::vector<KESCMStoryDiff>::const_iterator it = diffs.begin(); it != diffs.end(); ++it)
	{
		const bool16 removed = ((it->fKinds & kKESCMStoryKindRemoved) != 0) ? kTrue : kFalse;
		if (removed != wantRemoved)
			continue;	// belongs to the other document's pass

		KESCMStoryRow row;
		row.fStoryUID = it->fStoryUID;
		row.fKinds = it->fKinds;
		if (!ReadRowFromDocument(db, row))
			continue;

		if (row.fPageUID != kInvalidUID && pageList != nil)
		{
			// A story on a MASTER PAGE keeps kMaxInt32 and sorts to the END, which is what is wanted:
			// it is a real edit, but it belongs after the pages. IPageList.h:96-104 counts pages
			// within the pub and a master is not one of them, so the index cannot come back as a
			// position among them.
			//
			// ★MEASURED 2026-08-17, and measured so it could have come out the other way. The first
			// attempt built its stories in page order, which made story UID order and page order
			// AGREE - and RowIsBefore breaks a tie by UID, so rows carrying kMaxInt32 would have come
			// out in that same order either way. The second pair built them in reverse (master first,
			// so the master story holds the LOWEST uid) and the panel listed page 1, then page 3,
			// then the master - the exact opposite of UID order. ⚠What is NOT distinguished: whether
			// GetPageIndex answered negative or KESCMFramePageUID never produced a page UID at all.
			// Both land here as kMaxInt32, and the row goes to the end either way.
			//
			// ★AND THE SECOND ARGUMENT IS LEFT AT ITS DEFAULT ON PURPOSE: includePagesOfHiddenSpread
			// defaults to kTrue (IPageList.h:104), so a page whose spread is hidden still counts.
			// ***** That is what keeps Hide Unchanged from renumbering this list ***** - hide two
			// spreads and the rows keep the positions they had. It is the same property the
			// comparison's own page walk depends on, measured 2026-08-16 in audit B3: IPageList
			// includes the pages of hidden spreads and enumerates them in the same order as the
			// spread walk it replaced. ⚠Passing kFalse here would reorder the panel every time a
			// spread is hidden.
			const int32 idx = pageList->GetPageIndex(row.fPageUID);
			if (idx >= 0)
				row.fPageIndex = idx;
		}

		rows.push_back(row);
	}
}

/* Build
*/
void KESCMStoryList::Build(IDataBase* targetDB, IDataBase* sourceDB,
                           const std::vector<KESCMStoryDiff>& diffs)
{
	gRows.clear();
	if (targetDB == nil)
		return;

	AddRowsFromDocument(targetDB, diffs, kFalse, gRows);

	// ★THE REMOVED ROWS ARE READ OUT OF THE OLDER DOCUMENT (2026-08-21). Their story is not in the
	//   target at all, so there is nothing there to read a name, a frame or a page from.
	//   ⚠A nil sourceDB is not an error here: those rows simply do not appear, which is the same
	//     thing that happens to a story whose ITextModel cannot be read.
	AddRowsFromDocument(sourceDB, diffs, kTrue, gRows);

	std::sort(gRows.begin(), gRows.end(), RowIsBefore);
}

/* Clear
*/
void KESCMStoryList::Clear()
{
	gRows.clear();
}

/* GetRowCount
*/
int32 KESCMStoryList::GetRowCount()
{
	return static_cast<int32>(gRows.size());
}

/* GetRow
*/
const KESCMStoryRow* KESCMStoryList::GetRow(int32 nth)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()))
		return nil;

	return &gRows[nth];
}

/* SetRowChanges
*/
void KESCMStoryList::SetRowChanges(int32 nth, const std::vector<KESCMStoryChange>& changes,
								   bool16 textCompared)
{
	// The same bounds test GetRow makes, and for the same reason: the list is rebuilt by one
	// comparison and thrown away by the next, so an index is only ever as good as the moment it
	// was handed out.
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()))
		return;

	gRows[nth].fChanges = changes;
	gRows[nth].fTextCompared = textCompared;
}

/* RefreshRowFromDocument
*/
void KESCMStoryList::RefreshRowFromDocument(int32 nth, IDataBase* targetDB)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()) || targetDB == nil)
		return;

	// A story that has since been deleted answers kFalse and the row keeps what it had. That is the
	// same rule the whole feature follows: a row the comparison found must not vanish because
	// something about it could not be worked out a second time.
	(void)ReadRowFromDocument(targetDB, gRows[nth]);
}

/* ShutdownCleanup
*/
void KESCMStoryList::ShutdownCleanup()
{
	// Assigning a fresh vector releases the storage too, not just the contents, so the static
	// destructor at DLL unload finds nothing left to do. clear() would leave the rows' PMStrings
	// holding their buffers - the very thing this call exists to prevent (KBSResultModel.cpp:363-370).
	gRows = std::vector<KESCMStoryRow>();
}


// End, KESCMStoryList.cpp.
