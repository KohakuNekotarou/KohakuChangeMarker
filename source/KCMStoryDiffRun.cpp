//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryDiffRun.h for what this is for and what it deliberately does not do.
//
//  The text is read straight from the text model (KCMTextRead); what stands here cuts it, compares it and writes the
//  rows.
//
//    1. BOTH DOCUMENTS' POSITIONS: a click moves both windows. KCMTextDiff::Change carries the a-side range, and the
//       same paragraph-start arithmetic runs over the older side's paragraphs.
//
//    2. BOTH SIDES' WORDS: the row shows the newer version and the panel's message area shows the older one.
//       ⚠**THE ROW IS THE NEWER VERSION FOR EVERY KIND OF CHANGE, INCLUDING A DELETION** (the author's decision): a row
//       showing the older text would be the one row in the list showing the opposite document from every other. What
//       was deleted is in the message area, where every other row's other side is.
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"		// SaveRestoreModifiedState - see Run()
#include "ITextModel.h"

// General includes:
#include "PMString.h"
#include "UIDRef.h"

#include <algorithm>	// std::stable_sort - the ruby children are found by a second walk
#include <string>
#include <vector>

// Project includes:
#include "KCMParaText.h"	// KCMParaAttrs and the pure functions over paragraphs (Join / IndexInStory / SplitRunAtPlaces / SpansDiffer)
#include "KCMParaPairing.h"	// which paragraph goes with which, when a run holds more on one side (the import pours by it too)
#include "KCMProgressBar.h"	// KCMProgressStepper - the progress bar and Cancel of Run, shown after kKCMProgressBarDelayMs
#include "KCMSentence.h"	// the bar's fixed words
#include "KCMStoryDiffRun.h"
#include "KCMTableShape.h"	// KCMReadTableShapes / KCMTableShapesDiffer - the Table row
#include "KCMTableSnippet.h"	// KCMReadTableIdsInStory / KCMExportStoryInx - the Source's tables, by their own ids
#include "KCMTaskStartClone.h"	// KCMIsImportClone - a Source that is a clone of the Target pairs its tables by their uids
#include "KCMExternalSource.h"	// KCMIsExternalSource - ...the Task Start kept in memory too
#include "KCMMemXferBytes.h"	// ...and the buffer that export writes into
#include "KCMTextRead.h"		// the reader: paragraphs, their positions and their attributes, straight from the text model
#include "KCMStoryList.h"
#include "KCMTextDiff.h"
#include "KCMOversetPoint.h"	// KCMChangeIsOverset - is this change's text composed anywhere
#include "KCMDiag.h"			// KCM_DIAG_SLOW - test builds only (nothing in a shipping .pln)

namespace
{

/** How many code points of a change to keep for the row, INCLUDING the context on either side.
	The cell ellipsizes in the middle, so this only has to be short enough not to carry a paragraph
	around in memory per change. */
const int32 kExcerptCodePoints = 60;

/** How many code points to keep on EACH SIDE of a change. Narrow on purpose: two of these plus the change itself has to
stay under
	kExcerptCodePoints, or the context would push the change out of the cell, which is the
	opposite of what it is for. */
const int32 kContextCodePoints = 14;

// ---- cutting the text up for the rows --------------------------------------------------
//
// The paragraphs, their positions and their attributes come from KCMTextRead, which asks the
// text model. What stands here only cuts and joins strings. Join lives in KCMParaText.h as
// JoinParagraphs: it is one half of a convention -- how far apart two paragraphs are once they
// have been strung together -- and the other half (IndexInStory, which turns an offset back into
// a document position) has to agree with it EXACTLY. Both are in the one header the test harness
// (work\kescm-snippet-test) builds without InDesign.

/** The byte where a code point begins - or the end of the text for the one-past-the-last index: byteOffsets (what
	KCMTextDiff::ToCodePoints filled in for this same string) holds one entry per code point, so the last boundary has no
	entry of its own. */
int32 ByteOfCodePoint(const std::string& text, const std::vector<int32>& byteOffsets, int32 cp)
{
	return (cp < static_cast<int32>(byteOffsets.size())) ? byteOffsets[cp] : static_cast<int32>(text.size());
}

/** The code point at index cp of a string byteOffsets was filled in for; -1 past its end. */
int32 CodePointAt(const std::string& text, const std::vector<int32>& byteOffsets, int32 cp)
{
	if (cp < 0 || cp >= static_cast<int32>(byteOffsets.size()))
		return -1;
	std::vector<int32> one;
	const int32 from = byteOffsets[cp];
	const int32 to = ByteOfCodePoint(text, byteOffsets, cp + 1);
	KCMTextDiff::ToCodePoints(text.substr(static_cast<size_t>(from), static_cast<size_t>(to - from)), &one, nil);
	return one.empty() ? -1 : one[0];
}

/* Slice
   A piece of UTF-8 text named in CODE POINTS, cut on code point boundaries so what comes out is
   still valid UTF-8. byteOffsets is what ToCodePoints filled in for this same string.

   **IT TAKES THE SURROUNDING WORDS TOO.** A change on its own reads as a fragment --
   "awake" says nothing about where it is -- so the row shows what stands on either side of it.
   An ellipsis marks each end that was cut, so a
   fragment is never mistaken for the whole of something.

   **IT HANDS BACK THREE PIECES, NOT ONE**, so that the row can draw the changed characters at
   full strength and fade the context around them. The
   split has to be made HERE: by the time the row is drawn, the only thing that knows which
   characters were the change is this function -- the boundary between the context and the
   change is a code point index into a string that has already been cut at both ends and had its
   break characters replaced. Three pieces leave the panel nothing to count (a PMString itself is
   cut in code points - Truncate / Remove / GetUTF32TextChar, UnicodeSavvyString.h).

   @param from/count name the CHANGE, in code points, within text.
   @param outPre [out] what stands before the change, with a leading ellipsis when the text was
      cut there. Empty when the change begins the text.
   @param outMid [out] the changed characters themselves -- what the row draws at full strength.
      Empty for the side of a change that has nothing there (a deletion seen from the newer side).
   @param outPost [out] what stands after it, with a trailing ellipsis on the same terms as outPre.
*/
void Slice(const std::string& text, const std::vector<int32>& byteOffsets,
		   int32 from, int32 count,
		   std::string& outPre, std::string& outMid, std::string& outPost)
{
	outPre.clear();
	outMid.clear();
	outPost.clear();

	const int32 total = static_cast<int32>(byteOffsets.size());
	if (total <= 0 || from < 0)
		return;

	// A DELETION HAS count == 0 ON THE SIDE THAT LOST IT, and it still has a place -- the words
	//   that closed up over it. So an empty range is not refused here; only an empty RESULT is.
	int32 first = from - kContextCodePoints;
	if (first < 0)
		first = 0;
	int32 midFrom = (from < total) ? from : total;
	int32 midTo = from + count;			// exclusive
	if (midTo > total)
		midTo = total;
	if (midTo < midFrom)
		midTo = midFrom;
	int32 last = from + count + kContextCodePoints;	// exclusive
	if (last > total)
		last = total;
	if (last < midTo)
		last = midTo;
	if (last <= first)
		return;

	// THE CONTEXT IS NOT CUT AT A PARAGRAPH END (the author tried it on one build and asked for the breaks back: the
	//   words on both sides of a break read better than the change alone). What must NOT be crossed is a PLACE - the
	//   next cell,
	//   the next footnote - and that cut is made before this function is reached, where the run is split
	//   (SplitRunAtPlaces, KCMParaText.h: "a cell is a place").

	// Long enough to fill the cell and no longer. The cell ellipsizes for itself.
	//
	// Counted on the WHOLE excerpt: the cut can fall inside the change itself when a single change is longer than the
	//   whole allowance, which is the honest outcome, since the change is what the excerpt is for. Both boundaries are
	//   pulled back with it so that no piece can end up naming a range outside the one being kept. The flag forces the
	//   trailing ellipsis.
	bool16 truncated = kFalse;
	if (last - first > kExcerptCodePoints)
	{
		last = first + kExcerptCodePoints;
		truncated = kTrue;		// force the trailing ellipsis: something was cut here too
		if (midTo > last)
			midTo = last;
		if (midFrom > last)
			midFrom = last;
	}

	// A CUT NEVER STANDS BETWEEN A CHARACTER AND WHAT SHAPES IT (a variation selector, a combining mark - KCMTextDiff::
	//   IsClusterExtender; memory wrservices-unicode-facts, measured). Cut after a kanji whose selector is left behind,
	//   the
	//   row draws ANOTHER GLYPH of it; cut before the selector, the context begins with a character that draws nothing.
	//   The context gives way, which is all a cut through the change itself can do as well.
	while (first < midFrom && KCMTextDiff::IsClusterExtender(CodePointAt(text, byteOffsets, first)))
		++first;
	const int32 lastAllowed = truncated ? first + 1 : midTo;
	while (last > lastAllowed && KCMTextDiff::IsClusterExtender(CodePointAt(text, byteOffsets, last)))
		--last;
	if (midTo > last)
		midTo = last;
	if (midFrom > last)
		midFrom = last;

	const int32 beginByte = ByteOfCodePoint(text, byteOffsets, first);
	const int32 midFromByte = ByteOfCodePoint(text, byteOffsets, midFrom);
	const int32 midToByte = ByteOfCodePoint(text, byteOffsets, midTo);
	const int32 endByte = ByteOfCodePoint(text, byteOffsets, last);
	if (endByte <= beginByte)
		return;

	// Each piece goes through KCMParaText::MarkUpForRow on its own. A mark stands for one character,
	//   so nothing straddles a boundary.
	outPre = KCMParaText::MarkUpForRow(text.substr(beginByte, midFromByte - beginByte));
	outMid = KCMParaText::MarkUpForRow(text.substr(midFromByte, midToByte - midFromByte));
	outPost = KCMParaText::MarkUpForRow(text.substr(midToByte, endByte - midToByte));

	// The ellipses say "this is a window onto something longer". Without them the reader cannot
	//   tell a change that begins a paragraph from one that merely appears to.
	//   They belong to the CONTEXT pieces, which is also where they belong visually: an ellipsis
	//   stands for words that were cut away, and those words are context, never the change.
	if (first > 0)
		outPre = "\xE2\x80\xA6" + outPre;	// U+2026 HORIZONTAL ELLIPSIS
	if (last < total || truncated)
		outPost += "\xE2\x80\xA6";
}

// ---- one story --------------------------------------------------------------------------

/* StoryParas
   One version of a story as KCMTextRead reads it: three lists with one entry per paragraph, and its tables - whose index is
   the fTableOrdinal the cells carry.
   ONE VALUE, BECAUSE THE TWO VERSIONS MUST NOT MIX. As six separate lists they went through three functions in three
   different orders, and one version's text beside the other's starts compiles just the same.
*/
struct StoryParas
{
	std::vector<std::string>	fParas;
	std::vector<KCMParaAttrs>	fAttrs;
	std::vector<int32>			fStarts;
	std::vector<KCMTableShape>	fTables;
};

/* RunSide
   One run of paragraphs on ONE of the two sides, and the one thing every change asks of it:
   where an offset into the run's joined text lands in the document.

   **NOT A BARE `base`.** `base + offset` is right only while the joined text and the document agree about how far apart
   two paragraphs are, and a table makes them disagree -- its own character and its row terminators sit exactly at a
   paragraph boundary, and its cells stand past the end of the body. So the run is asked instead of counted on: every
   paragraph's start is a TextIndex the reader took from the walk, and the rule that turns an offset into one lives
   beside JoinParagraphs in KCMParaText.h, where the two ends of the convention can be measured against each other.
*/
struct RunSide
{
	const StoryParas&	fStory;
	int32				fStart;			// first paragraph of the run
	int32				fCount;			// how many it covers - never 0: a run on one side only is AddWholeParagraphs'
	std::string			fText;			// the run's paragraphs, joined (KCMParaText::JoinParagraphs)
	std::vector<int32>	fCodePoints;	// fText's, one token each
	std::vector<int32>	fBytes;			// where each of them begins in fText
	std::vector<int32>	fObjects;		// the run's tables and note references, in the joined text's count
	std::vector<int32>	fTables;		// its tables alone - where the character pass cuts

	RunSide(const StoryParas& story, int32 start, int32 count)
		: fStory(story), fStart(start), fCount(count),
		  fText(KCMParaText::JoinParagraphs(story.fParas, start, count))
	{
		KCMTextDiff::ToCodePoints(fText, &fCodePoints, &fBytes);
		KCMParaText::RunObjectOffsets(story.fParas, story.fAttrs, start, count, fObjects);
		KCMParaText::RunTableAnchors(story.fParas, story.fAttrs, start, count, fTables);
	}

	/** Where an offset into the run's joined TEXT stands in the DOCUMENT.
		⚠**THE ATTRIBUTES ARE CARRIED FOR ONE FIELD** (fUncountedAt), and it is what makes this a
		 crossing between two counts rather than a lookup: a table standing inside a paragraph is
		 counted by the document and not by the text. See KCMParaText::ModelOffsetInParagraph. */
	int32 Index(int32 joinedOffset) const
	{
		return KCMParaText::IndexInStory(fStory.fParas, fStory.fStarts, fStory.fAttrs, fStart, fCount, joinedOffset);
	}
};

/* SetExcerptPieces
   One side of a change, cut into its three pieces and stored in the row's fields.

   **CUTTING AND STORING ARE ONE ACT.** They stood as ten lines written out twice - once for a text
   change, once for an attribute one: apart, a piece added to the cut and forgotten in the store is
   a fault nothing reports.
   @warning Text out of a document is not a translation key: left translatable it can be looked up in
    the string tables and come back as something else (memory menu-string-translation-traps), the
    CONTEXT pieces most of all, being the likeliest to be a short common word a table has an entry
    for. SetUTF8String is what marks it, every field of a change (PMString.h, at its declaration).

   @param from/count name the CHANGE, in code points, within text.
*/
void SetExcerptPieces(PMString& outPre, PMString& outMid, PMString& outPost,
					  const std::string& text, const std::vector<int32>& bytes,
					  int32 from, int32 count)
{
	std::string pre, mid, post;
	Slice(text, bytes, from, count, pre, mid, post);

	outPre.SetUTF8String(pre);
	outMid.SetUTF8String(mid);
	outPost.SetUTF8String(post);
}

/* Add
   Builds one change and appends it: a run narrowed down to characters and one that was not (AddCutAtObjects)
   describe the same thing the same way.

   @param tFrom/tCount, sFrom/sCount are offsets WITHIN the joined run, in code points.
   @param objectsBefore how many of the run's objects stand before this change on both sides
          (KCMParaText::KCMObjectPiece), or -1 when nothing is known - see AddCutAtObjects.
*/
void Add(std::vector<KCMStoryChange>& out,
		 const RunSide& tRun, int32 tFrom, int32 tCount,
		 const RunSide& sRun, int32 sFrom, int32 sCount,
		 int32 objectsBefore)
{
	KCMStoryChange change;
	change.fWhat = KCMStoryChange::kText;

	change.fKind = (sCount == 0) ? KCMStoryChange::kInsert
				 : (tCount == 0) ? KCMStoryChange::kDelete
								 : KCMStoryChange::kReplace;

	// @warning **BOTH ENDS ARE ASKED FOR**, rather than the start plus the count. A change may
	//   run across a paragraph boundary, and a boundary can be worth more than the one character
	//   the joined text spends on it -- see RunSide above.
	// **AND AN END IS NOT A START** (the import matrix's G2). Index answers for a START -
	//   the character at an offset stands AFTER a table standing there - so asking it for an END put
	//   a range that stops right before a table one position wide, over the table's anchor: the change
	//   then "held a table" and could not be taken in, though its words touched nothing but letters.
	//   An end is "just past the last character"; an empty range ends where it starts.
	// **AND AN EMPTY RANGE NEXT TO A TABLE HAS A SIDE** (the same day, G1): "after everything standing
	//   there" is Index's answer, and objectsBefore says how many of those it stays in front of.
	change.fTargetStart = tRun.Index(tFrom);
	if (tCount == 0)
		change.fTargetStart -= KCMParaText::ObjectsToStepBack(tRun.fObjects, tFrom, objectsBefore);
	change.fTargetEnd = (tCount > 0) ? tRun.Index(tFrom + tCount - 1) + 1 : change.fTargetStart;

	// **AN INSERTION HAS A PLACE IN THE OLDER DOCUMENT EVEN THOUGH IT HAS NO CHARACTERS THERE.**
	//   The reader wants to see where the new words went in, and the older version has an exact
	//   spot for it: between the two characters that used to be neighbours -- drawn as the same
	//   thin caret a deletion gets on the newer side.
	//
	//   **AN EMPTY RANGE IS THE ANSWER TO BOTH.** fSourceStart == fSourceEnd says "this place,
	//     no characters" -- which is exactly what the newer side already carries for a DELETION,
	//     and what the marks already draw as a caret (KCMStoryMarkBuild turns a zero-width range
	//     into KCMMarkRange::Caret without being asked). So + and - are mirror images.
	change.fSourceStart = sRun.Index(sFrom);
	if (sCount == 0)
		change.fSourceStart -= KCMParaText::ObjectsToStepBack(sRun.fObjects, sFrom, objectsBefore);
	change.fSourceEnd = (sCount > 0) ? sRun.Index(sFrom + sCount - 1) + 1 : change.fSourceStart;	// the same END rule

	// **BOTH SIDES ARE CUT, ALWAYS.** The row shows the side that changed; the panel's message
	//   area shows the other one while that row is selected, so that the reader can see what the
	//   words used to be (or, for a deletion, what stands there now).
	//
	// THE ROW IS ALWAYS THE NEWER VERSION. ONE RULE, NO EXCEPTIONS (the author's decision): consistency across every
	//   row is worth more than one row being self-contained. What was deleted is in the message area, where every other
	//   row's other side is, and the row's sign says which kind of row it is.
	//
	//   Whichever side the row shows, the OTHER one goes to fOtherText -- see KCMStoryList.h for
	//   why it is named that rather than "old".
	SetExcerptPieces(change.fTextPre, change.fText, change.fTextPost,
					 tRun.fText, tRun.fBytes, tFrom, tCount);
	SetExcerptPieces(change.fOtherTextPre, change.fOtherText, change.fOtherTextPost,
					 sRun.fText, sRun.fBytes, sFrom, sCount);

	out.push_back(change);
}

/* AddCutAtObjects
   Add, once for each piece of the change that the tables and note references standing in it leave
   (the import matrix's G2).

   **A CHANGE ACROSS A TABLE IS TWO CHANGES, ONE ON EACH SIDE OF IT.** Deleting 文表 around a table
   came out as one change whose range held the table's anchor, and it could not be taken in: writing it
   would have deleted the table. Cut where the table stands, each piece holds letters alone and the
   table stays - and the rows say what happened, one on each side of it.
   The objects are paired in order across the two versions (KCMParaText::CutChangeAtObjects); when
   they cannot be - a table was added or moved - the change is added whole.
*/
void AddCutAtObjects(std::vector<KCMStoryChange>& out,
					 const RunSide& tRun, int32 tFrom, int32 tCount,
					 const RunSide& sRun, int32 sFrom, int32 sCount)
{
	std::vector<KCMParaText::KCMObjectPiece> pieces;
	KCMParaText::CutChangeAtObjects(sRun.fObjects, tRun.fObjects, sFrom, sCount, tFrom, tCount, pieces);
	for (size_t i = 0; i < pieces.size(); ++i)
	{
		const KCMParaText::KCMObjectPiece& piece = pieces[i];
		Add(out, tRun, piece.fBStart, piece.fBCount, sRun, piece.fAStart, piece.fACount, piece.fObjectsBefore);
	}
}

/* AddWholeParagraphs
   One change per paragraph of a run that stands on ONE side only - paragraphs added or removed whole
   (the author's rule and request: "+ and - per paragraph").

   **THE RANGES CARRY A PARAGRAPH BREAK.** Measured: a paragraph added in the file and
   taken in ran into the next paragraph, and one removed left an empty paragraph behind - the joined
   run's words held no break to write or take out.
   **WHICH BREAK: THE ONE BEFORE THE PARAGRAPH**, cut at returns - [return of the paragraph before,
   return of this one). Taken in, that is "\rNEW" right before the return of the paragraph it follows:
   what pressing Return at its end puts in (the new paragraph starts in that paragraph's style, and the
   restore then gives it the next style - KCMParagraphStyle.h). Taken out, it joins the paragraph before
   with the removed one's return, and a join keeps the UPPER paragraph's style (measured).
   ⚠**A PLACE'S FIRST PARAGRAPH HAS NO RETURN BEFORE IT IN ITS PLACE**, so it is cut at its own start
   instead: [its start, just past its return), before the paragraph that follows.
   ⚠**ONE CARET FOR ALL OF A RUN'S PARAGRAPHS ON THE SIDE THAT LACKS THEM.** Taken in one by one they
   all go in right before the same return, each press writing in front of the ones already in - so taken
   in from the last to the first they come out in their order; the order is the reader's own. */
void AddWholeParagraphs(std::vector<KCMStoryChange>& out, const KCMTextDiff::Change& run,
						const StoryParas& source, const StoryParas& target)
{
	// The source holds them and the target lacks them - taking in ADDS them - or the other way round.
	const bool16 adds = (run.aCount > 0) ? kTrue : kFalse;
	const int32 first = adds ? run.aStart : run.bStart;
	const int32 count = adds ? run.aCount : run.bCount;
	const StoryParas& own = adds ? source : target;
	const StoryParas& other = adds ? target : source;

	const int32 sourceSize = static_cast<int32>(source.fParas.size());
	const int32 targetSize = static_cast<int32>(target.fParas.size());
	const bool16 afterReturn = (run.aStart > 0 && run.bStart > 0
								&& run.aStart - 1 < sourceSize && run.bStart - 1 < targetSize
								&& KCMParaText::ParagraphsSharePlace(own.fAttrs, first, source.fAttrs, run.aStart - 1)
								&& KCMParaText::ParagraphsSharePlace(own.fAttrs, first, target.fAttrs, run.bStart - 1))
							   ? kTrue : kFalse;
	const int32 nextA = run.aStart + run.aCount;
	const int32 nextB = run.bStart + run.bCount;
	const bool16 beforeNext = (!afterReturn && nextA < sourceSize && nextB < targetSize
							   && KCMParaText::ParagraphsSharePlace(own.fAttrs, first, source.fAttrs, nextA)
							   && KCMParaText::ParagraphsSharePlace(own.fAttrs, first, target.fAttrs, nextB))
							  ? kTrue : kFalse;

	// The side that lacks them: one caret for all of them (the head says why), and the paragraph it stands in, for the
	// rows' context.
	int32 caret = 0;
	int32 otherFrom = 0;
	std::string otherText;
	if (afterReturn)
	{
		const int32 before = adds ? run.bStart - 1 : run.aStart - 1;
		caret = KCMParaText::ParagraphReturn(other.fParas, other.fStarts, other.fAttrs, before);
		otherText = other.fParas[static_cast<size_t>(before)];
		otherFrom = KCMParaText::CountCodePoints(otherText);
	}
	else if (beforeNext)
	{
		const int32 next = adds ? nextB : nextA;
		caret = KCMParaText::ParagraphLineStart(other.fStarts, other.fAttrs, next);
		otherText = other.fParas[static_cast<size_t>(next)];
	}
	else
	{
		// THE PLACE IS ON ONE SIDE ONLY (a footnote there with its reference, and not here): nowhere to write them, and the caret is only
		// where a press goes - the end of the paragraph before the run, the last place it can be said to follow. (Never
		// the holder's own position: a number from the other document, past the story's end or among words it has
		// nothing to do with.)
		const int32 before = (adds ? run.bStart : run.aStart) - 1;
		if (before >= 0 && before < (adds ? targetSize : sourceSize))
			caret = KCMParaText::ParagraphReturn(other.fParas, other.fStarts, other.fAttrs, before);
	}
	std::vector<int32> otherBytes;
	KCMTextDiff::ToCodePoints(otherText, nil, &otherBytes);

	for (int32 k = 0; k < count; ++k)
	{
		const int32 p = first + k;
		KCMStoryChange change;
		change.fWhat = KCMStoryChange::kText;
		change.fWholeParagraph = kTrue;

		// **AND WHICH END OF THE RANGE THE BREAK IS AT** (the author: the mark reached the end
		//   of the paragraph above). The ranges below are cut for the WRITE; the facade cuts the break
		//   back off them for everything the reader sees (KCMStoryList.h, KCMShownSpan).
		change.fBreakAt = afterReturn ? kKCMBreakLeads : (beforeNext ? kKCMBreakTrails : kKCMBreakNone);

		// The side that holds the paragraph: its range.
		int32 ownStart = 0;
		int32 ownEnd = KCMParaText::ParagraphReturn(own.fParas, own.fStarts, own.fAttrs, p);
		if (afterReturn)
			ownStart = KCMParaText::ParagraphReturn(own.fParas, own.fStarts, own.fAttrs, p - 1);
		else if (beforeNext)
		{
			ownStart = KCMParaText::ParagraphLineStart(own.fStarts, own.fAttrs, p);
			++ownEnd;
		}
		else
		{
			// Nowhere to put it: shown all the same.
			ownStart = own.fStarts[static_cast<size_t>(p)];
		}

		const std::string& ownText = own.fParas[static_cast<size_t>(p)];
		std::vector<int32> ownBytes;
		KCMTextDiff::ToCodePoints(ownText, nil, &ownBytes);
		const int32 ownLength = static_cast<int32>(ownBytes.size());

		if (adds)
		{
			change.fKind = KCMStoryChange::kDelete;		// the target lacks it (Add's naming: tCount == 0)
			change.fTargetStart = caret;
			change.fTargetEnd = caret;
			change.fSourceStart = ownStart;
			change.fSourceEnd = ownEnd;
			SetExcerptPieces(change.fTextPre, change.fText, change.fTextPost, otherText, otherBytes, otherFrom, 0);
			SetExcerptPieces(change.fOtherTextPre, change.fOtherText, change.fOtherTextPost,
							 ownText, ownBytes, 0, ownLength);
		}
		else
		{
			change.fKind = KCMStoryChange::kInsert;		// the target holds it and the source does not
			change.fTargetStart = ownStart;
			change.fTargetEnd = ownEnd;
			change.fSourceStart = caret;
			change.fSourceEnd = caret;
			SetExcerptPieces(change.fTextPre, change.fText, change.fTextPost, ownText, ownBytes, 0, ownLength);
			SetExcerptPieces(change.fOtherTextPre, change.fOtherText, change.fOtherTextPost,
							 otherText, otherBytes, otherFrom, 0);
		}
		out.push_back(change);
	}
}

/* ParaSide
   ONE PARAGRAPH OF ONE VERSION, and everything an attribute comparison asks about it: its text,
   where it stands in the document, the crossing from the text's count into the document's, and
   the byte each of its code points begins at - the last of these MADE ONLY IF SOMETHING ASKS.

   **WHY THE BYTE TABLE IS LAZY.** Most paragraphs carrying a mark carry the SAME mark in both versions, so the table is
   never wanted at all; asked for here, it is made once per paragraph per comparison, or not at all.

   **WHY THE BASE AND THE ATTRIBUTES RIDE WITH IT.** The three are one paragraph seen from one side.
   Apart, a helper could be handed one version's text with the other version's base and still
   compile - and the answer would be a position in the wrong document.
*/
struct ParaSide
{
	const std::string&		fText;
	const KCMParaAttrs&		fAttrs;
	int32					fBase;		// where this paragraph begins, as a TextIndex

	ParaSide(const std::string& text, const KCMParaAttrs& attrs, int32 base)
		: fText(text), fAttrs(attrs), fBase(base), fBytesMade(kFalse) {}

	/** Where each code point of the paragraph begins, in bytes - made once, on the first ask. */
	const std::vector<int32>& Bytes()
	{
		if (!fBytesMade)
		{
			KCMTextDiff::ToCodePoints(fText, nil, &fBytes);
			fBytesMade = kTrue;
		}
		return fBytes;
	}

	/** Where an offset into this paragraph's TEXT stands in the document.
		⚠**NOT fBase + offset.** A table standing inside the paragraph is counted by the document
		 and not by the text, and this is the crossing - see
		 KCMParaText::ModelOffsetInParagraph, which also says which end of a range it answers
		  for. */
	int32 ModelIndex(int32 textOffset) const
	{
		return fBase + KCMParaText::ModelOffsetInParagraph(fAttrs, textOffset);
	}

private:
	std::vector<int32>	fBytes;
	bool16				fBytesMade;
};

/* BuildLayers
   One side of a warichu or tate-chu-yoko change, as the lines the panel draws (KCMStoryLayers.h):
   the plan decides which characters go on which line (KCMParaText::PlanLayers, tested outside
   InDesign), and this cuts the words.
   LINE 0 IS CUT THE WAY EVERY ROW IS - Slice, with its context and its ellipses - around its one
    bar. The lines above are whole pieces of a warichu or a tate-chu-yoko and are not shortened
    here: the cell ellipsizes what does not fit.
   @param present whether this side carries the mark (its value is not empty). */
void BuildLayers(ParaSide& side, bool16 changedIsWarichu, int32 start, int32 len, bool16 present,
				 KCMStoryLayers& out)
{
	out = KCMStoryLayers();

	KCMParaText::LayerPlan plan;
	KCMParaText::PlanLayers(changedIsWarichu, side.fAttrs.fWarichu, side.fAttrs.fTcy,
							start, len, present, plan);
	if (plan.fCount < 2)
		return;

	out.fCount = plan.fCount;
	out.fChanged = plan.fChanged;
	out.fShowsText = plan.fShowsText;

	// The text line only when it is drawn (a change inside another layer leaves it out - the author's
	//   call, KCMStoryLayers::fShowsText); its two pieces stay empty otherwise.
	if (plan.fShowsText)
	{
		std::string pre, mid, post;
		Slice(side.fText, side.Bytes(), plan.fHole.fFrom, plan.fHole.fTo - plan.fHole.fFrom, pre, mid, post);
		out.fBottomPre.SetUTF8String(pre);
		out.fBottomPost.SetUTF8String(post);
	}

	auto piece = [&](int32 from, int32 to) -> PMString
	{
		PMString s;
		s.SetUTF8String(KCMParaText::MarkUpForRow(KCMParaText::SliceCodePoints(side.fText, from, to - from)));
		return s;
	};

	out.fMiddleIsBar = plan.fMiddleIsBar;
	if (!plan.fMiddleIsBar)
	{
		int32 at = plan.fMiddle.fFrom;
		for (size_t h = 0; h < plan.fUpperHoles.size(); ++h)
		{
			out.fMiddleParts.push_back(piece(at, plan.fUpperHoles[h].fFrom));
			at = plan.fUpperHoles[h].fTo;

			const bool16 bar = (static_cast<int32>(h) == plan.fChangedPiece && plan.fChangedPieceIsBar)
							   ? kTrue : kFalse;
			out.fUpperPieces.push_back(bar ? piece(0, 0) : piece(plan.fUpperHoles[h].fFrom, plan.fUpperHoles[h].fTo));
		}
		out.fMiddleParts.push_back(piece(at, plan.fMiddle.fTo));
	}
	out.fChangedPiece = plan.fChangedPiece;
	out.fChangedPieceIsBar = plan.fChangedPieceIsBar;
}

/* AddAttrChange
   One ATTRIBUTE difference turned into the child row that reports it. The row shows the newer side, as every row does.

   **IT TAKES attrKind RATHER THAN ASSUMING RUBY.** What an attribute's VALUE means is not the same for all of them -- a
   ruby's is a READING and a kenten's is a KIND ("BlackCircle") -- and the field they travel in is the same one, so
   whoever draws it has to be told which it is looking at.

   @param tSpan/sSpan the change's place on each side and what stands over it. A side without the mark has its place and
    no value - fGroup kFalse then says nothing either: it has no ruby, not a mono one, and the panel reads the empty reading
    beside it, never this (KCMStoryList.h).
   @param target/source the two paragraphs, each carrying its own text, its base and its byte table (ParaSide).
*/
void AddAttrChange(KCMStoryChange::Kind kind, KCMStoryAttrKind attrKind,
				   const KCMAttrSpan& tSpan, const KCMAttrSpan& sSpan,
				   ParaSide& target, ParaSide& source,
				   std::vector<KCMStoryChange>& out)
{
	const int32 tStart = tSpan.fStart;
	const int32 tCount = tSpan.fLen;
	const int32 sStart = sSpan.fStart;
	const int32 sCount = sSpan.fLen;

	KCMStoryChange change;
	change.fKind = kind;
	change.fWhat = KCMStoryChange::kAttr;		// the field that has waited for exactly this
	change.fAttrKind = attrKind;

	// @warning **BOTH ENDS ARE ASKED FOR SEPARATELY**, exactly as Add does for a text change and
	//   for a reason it did not have: a span reaching across a table's own character covers one
	//   FEWER character of text than of model, so `start + count` would be a length in the wrong
	//   count. ModelIndex crosses between the two.
	change.fTargetStart = target.ModelIndex(tStart);
	change.fTargetEnd   = target.ModelIndex(tStart + tCount);

	// ⚠**A SPAN ON ONE SIDE ONLY: THE CALLER HANDS THE OTHER SIDE ITS PLACE**, where its characters stand there or the
	//   words that took their place, read from a character diff of the two (PairMarksAcross) - never a number from the
	//   other version (measured: work/kcm-selftest/attrpos, a ruby added to 銀河 at target index 3 selected 名な in the
	//   older document).
	change.fSourceStart = source.ModelIndex(sStart);
	change.fSourceEnd   = source.ModelIndex(sStart + sCount);

	// A NOTE'S MARKER THE PARAGRAPH BEGINS WITH STANDS BEHIND THE PARAGRAPH'S START (KCMParaAttrs::fLeadingNoteBack), where
	//   ModelIndex does not go, and the range takes it in - as the range of a span standing before a marker takes that marker
	//   in: what writes this row reads the range to learn that a marker is there (a footnote at a paragraph's start).
	if (attrKind == kKCMStoryAttrFootnote || attrKind == kKCMStoryAttrEndnote)
	{
		if (tStart == 0 && !tSpan.fValue.empty())
			change.fTargetStart -= target.fAttrs.fLeadingNoteBack;
		if (sStart == 0 && !sSpan.fValue.empty())
			change.fSourceStart -= source.fAttrs.fLeadingNoteBack;
	}

	SetExcerptPieces(change.fTextPre, change.fText, change.fTextPost,
					 target.fText, target.Bytes(), tStart, tCount);
	SetExcerptPieces(change.fOtherTextPre, change.fOtherText, change.fOtherTextPost,
					 source.fText, source.Bytes(), sStart, sCount);

	// The readings go through the same door as the base text: they are document text too.
	change.fRuby.SetUTF8String(tSpan.fValue);
	change.fOtherRuby.SetUTF8String(sSpan.fValue);

	// HOW THE RUBY IS SET, carried beside the reading rather than worked out from it -- the two
	//   settings can produce identical readings, which is the whole reason the panel needed telling
	//   (KCMStoryList.h, fRubyGroup). ⚠For KENTEN both are kFalse and mean nothing: the caller has
	//   nothing else to hand over, kenten having no such distinction.
	change.fRubyGroup = tSpan.fGroup;
	change.fOtherRubyGroup = sSpan.fGroup;

	// WARICHU AND TATE-CHU-YOKO ARE DRAWN IN LAYERS (the author's drawings), and the lines are cut HERE, where the
	//   paragraph and its spans are - each side on its own terms, since a side without the mark is drawn differently
	//   from one with it
	//   (KCMParaText::PlanLayers). Both can be put back wherever they stand (the author's decision - KCMApplyWarichu /
	//   KCMApplyTcy, ON/OFF only).
	if (KCMAttrKindIsLayered(attrKind))
	{
		const bool16 isWarichu = (attrKind == kKCMStoryAttrWarichu) ? kTrue : kFalse;
		BuildLayers(target, isWarichu, tStart, tCount, tSpan.fValue.empty() ? kFalse : kTrue, change.fLayers);
		BuildLayers(source, isWarichu, sStart, sCount, sSpan.fValue.empty() ? kFalse : kTrue, change.fOtherLayers);
	}

	out.push_back(change);
}

/* CompareParagraphAttr
   One ATTRIBUTE's spans over two versions of a paragraph: what KCMParaText::PairMarksAcross says of them, as rows.

   **LENGTH IS NOT PART OF THE MATCHING**: it is part of what changed (琥珀 read as こ+はく against こはく is a change of length, and
   the whole point).

   @param chars the character diff of the two paragraphs; empty when their words are the same.
*/
void CompareParagraphAttr(KCMStoryAttrKind attrKind,
						  const KCMAttrSpanList& sourceSpans, const KCMAttrSpanList& targetSpans,
						  ParaSide& source, ParaSide& target, const std::vector<KCMParaText::KCMCharChange>& chars,
						  std::vector<KCMStoryChange>& out)
{
	if (chars.empty() && !KCMParaText::SpansDiffer(sourceSpans, targetSpans))
		return;		// and NOTHING is built: the byte tables are asked for below or never

	std::vector<KCMParaText::KCMMarkPair> pairs;
	KCMParaText::PairMarksAcross(chars, sourceSpans, targetSpans, pairs);
	for (size_t p = 0; p < pairs.size(); ++p)
	{
		const KCMParaText::KCMMarkPair& pair = pairs[p];
		if (pair.fA >= 0 && pair.fB >= 0)
		{
			const KCMAttrSpan& s = sourceSpans[static_cast<size_t>(pair.fA)];
			const KCMAttrSpan& t = targetSpans[static_cast<size_t>(pair.fB)];
			// fGroup is NOT part of "same" (the author's decision - KCMParaText.h's
			//   SpansDiffer says it in full): a ruby re-set from mono to group over the same
			//   reading is not reported.
			if (s.fValue == t.fValue && KCMParaText::MarksCoverSameCharacters(chars, s, t))
				continue;

			// ONLY THE INNERMOST LAYER REPORTS A CHANGE OF ITS WORDS (the author's call).
			//   A warichu's value is its characters, so rewriting a tate-chu-yoko inside it changes the
			//   warichu's value too - and the tate-chu-yoko's own row already says so. The same the
			//   other way round, for a warichu standing inside a tate-chu-yoko. ⚠The warichu is the
			//   outer one of two over the same range, which is what the last argument says.
			if (KCMAttrKindIsLayered(attrKind))
			{
				const bool16 isWarichu = (attrKind == kKCMStoryAttrWarichu) ? kTrue : kFalse;
				if (KCMParaText::OnlyNestedDiffers(source.fText, s, isWarichu ? source.fAttrs.fTcy : source.fAttrs.fWarichu,
												   target.fText, t, isWarichu ? target.fAttrs.fTcy : target.fAttrs.fWarichu,
												   isWarichu))
					continue;
			}
			AddAttrChange(KCMStoryChange::kReplace, attrKind, t, s, target, source, out);
		}
		else
		{
			// A mark one version alone has. The other version has the place its characters stand, or the words that took their place, and
			// no value. @warning a ruby taken off: the characters are still there - it is the reading that is gone - so the range is a real
			// one on both sides, unlike a text deletion.
			const KCMAttrSpan place(pair.fPlaceStart, pair.fPlaceLen, std::string());
			if (pair.fB >= 0)
				AddAttrChange(KCMStoryChange::kInsert, attrKind, targetSpans[static_cast<size_t>(pair.fB)], place, target, source, out);
			else
				AddAttrChange(KCMStoryChange::kDelete, attrKind, place, sourceSpans[static_cast<size_t>(pair.fA)], target, source, out);
		}
	}
}

/* CompareParagraphObjects
   The objects placed in one paragraph, paired by their ORDER in it - not by where they start, which is what
   CompareParagraphAttr pairs marks by.
   WHY THE ORDER: every object stands on the same character (U+FFFC), so an object that merely moved - words typed
   before it - has nothing but its place to tell it from another, and its place is exactly what moved. Paired by start,
   one object whose content changed in a paragraph whose words also changed came out as an object added AND one
   taken away (the rule PairMarksAcross keeps for marks, whose characters differ). The k-th object here and the
   k-th over there are the same object between two versions of one document - the only pairs this is asked about
   (KCMTextRead's withObjects is the Story mode's UID pairs alone).
   ⚠A PARAGRAPH WHOSE OBJECTS ARE NOT AS MANY ON BOTH SIDES SAYS NOTHING HERE: an object added or taken away is its
    anchor character added or taken away, and the text row already reports that; with the order broken, no pair can be
    named with confidence.
*/
void CompareParagraphObjects(const KCMAttrSpanList& sourceSpans, const KCMAttrSpanList& targetSpans,
							 ParaSide& source, ParaSide& target, std::vector<KCMStoryChange>& out)
{
	if (sourceSpans.size() != targetSpans.size())
		return;
	for (size_t k = 0; k < targetSpans.size(); ++k)
	{
		if (sourceSpans[k].fValue == targetSpans[k].fValue)
			continue;
		AddAttrChange(KCMStoryChange::kReplace, kKCMStoryAttrObject, targetSpans[k], sourceSpans[k], target, source, out);
	}
}

/** How many rows every kind of mark of a pair of paragraphs comes to across a character diff (KCMParaText::RowsOfMarks). */
int32 RowsOfAllMarks(const std::vector<KCMParaText::KCMCharChange>& chars, const KCMParaAttrs& source, const KCMParaAttrs& target)
{
	return KCMParaText::RowsOfMarks(chars, source.fRuby, target.fRuby)
		 + KCMParaText::RowsOfMarks(chars, source.fKenten, target.fKenten)
		 + KCMParaText::RowsOfMarks(chars, source.fFootnote, target.fFootnote)
		 + KCMParaText::RowsOfMarks(chars, source.fEndnote, target.fEndnote)
		 + KCMParaText::RowsOfMarks(chars, source.fWarichu, target.fWarichu)
		 + KCMParaText::RowsOfMarks(chars, source.fTcy, target.fTcy);
}

/** The character diff of two versions of a paragraph, as what becomes of each character (KCMParaText::PairMarksAcross). One that cannot be had leaves
	it empty: the guard that cannot fire - KCMTextDiff.h.

	**ONE EDIT CAN BE WRITTEN SEVERAL WAYS, AND THE TEXT ALONE CANNOT SAY WHICH WAS MADE** (measured): 銀河と銀河 -> 銀河 is
	the first word gone
	or the second. A mark on the words can: the search keeps what the two begin with, so with the reading on the second word it read that word as gone, and
	the one left as having been given a reading nobody put on it. ⇒ **The way a reader would describe it (the slide the text rows take, KCMTextDiff::
	AlignChangeBoundaries, widening nothing) and the way the search found it are both asked, and the one the marks have less to say about stands** - the
	slide when they tie, so that the marks read as the text row does. */
void CharChangesOf(const std::string& sourceText, const std::string& targetText, const KCMParaAttrs& sourceAttrs, const KCMParaAttrs& targetAttrs,
				   std::vector<KCMParaText::KCMCharChange>& out)
{
	std::vector<int32> sourcePoints;
	std::vector<int32> targetPoints;
	KCMTextDiff::ToCodePoints(sourceText, &sourcePoints, nil);
	KCMTextDiff::ToCodePoints(targetText, &targetPoints, nil);
	std::vector<KCMTextDiff::Change> found;
	if (!KCMTextDiff::Diff(sourcePoints, targetPoints, found))
		return;
	std::vector<KCMTextDiff::Change> slid(found);
	KCMTextDiff::AlignChangeBoundaries(sourcePoints, targetPoints, slid, kFalse);
	const auto asChars = [](const std::vector<KCMTextDiff::Change>& changes)
	{
		std::vector<KCMParaText::KCMCharChange> chars;
		chars.reserve(changes.size());
		for (size_t c = 0; c < changes.size(); ++c)
		{
			const KCMParaText::KCMCharChange one = { changes[c].aStart, changes[c].aCount, changes[c].bStart, changes[c].bCount };
			chars.push_back(one);
		}
		return chars;
	};
	const std::vector<KCMParaText::KCMCharChange> asFound = asChars(found);
	out = asChars(slid);
	if (RowsOfAllMarks(asFound, sourceAttrs, targetAttrs) < RowsOfAllMarks(out, sourceAttrs, targetAttrs))
		out = asFound;
}

/* AddAttributeChanges
   The marks over the words - ruby, kenten, note references, warichu, tate-chu-yoko, objects - of every pair of
   paragraphs that line up. **THE WORDS CAN BE THE SAME** (a ruby-only edit leaves the paragraph diff with nothing to
   say, and the paragraphs it did not mention are exactly the ones to ask about) **OR NOT**: what the paragraph diff
   reported, when it
   holds as many paragraphs on each side, is asked too, across a character diff (PairMarksAcross).
*/
void AddAttributeChanges(const std::vector<KCMTextDiff::Change>& paragraphChanges,
						const StoryParas& sourceStory, const StoryParas& targetStory,
						std::vector<KCMStoryChange>& out)
{
	int32 a = 0;
	int32 b = 0;

	// ONE PLACE DECIDES WHAT AN ATTRIBUTE COMPARISON IS, and both walks below call it. The two
	//   walks differ only in WHICH paragraphs they hand over; writing the comparison twice would be
	//   two things to keep right (memory one-question-one-place), and the second copy is exactly where
	//   a kind gets forgotten when a third one is added.
	// ⚠**ONE FACT, ONE NAME.** `textDiffered` says the diff reported THIS pair of paragraphs as a
	//   change, and two things follow from it: a mark whose characters went is not a change of its own,
	//   and **a position on one side cannot be handed to the other**. Both are answered by
	//   a character diff of the two paragraphs (PairMarksAcross), made when there is a mark to ask about.
	auto compareParagraphPair = [&](int32 ai, int32 bi, bool16 textDiffered)
	{
		// The two paragraphs, each as ONE thing: its text, its base, the crossing into the
		//   document's count, and a byte table built only if something below asks for it (see
		//   ParaSide). A paragraph whose marks did not move asks for nothing.
		ParaSide source(sourceStory.fParas[ai], sourceStory.fAttrs[ai], sourceStory.fStarts[ai]);
		ParaSide target(targetStory.fParas[bi], targetStory.fAttrs[bi], targetStory.fStarts[bi]);

		const KCMParaAttrs& sourceAttrs = sourceStory.fAttrs[ai];
		const KCMParaAttrs& targetAttrs = targetStory.fAttrs[bi];
		const bool16 anyMark = (!sourceAttrs.fRuby.empty() || !targetAttrs.fRuby.empty() || !sourceAttrs.fKenten.empty() || !targetAttrs.fKenten.empty()
								|| !sourceAttrs.fFootnote.empty() || !targetAttrs.fFootnote.empty() || !sourceAttrs.fEndnote.empty() || !targetAttrs.fEndnote.empty()
								|| !sourceAttrs.fWarichu.empty() || !targetAttrs.fWarichu.empty() || !sourceAttrs.fTcy.empty() || !targetAttrs.fTcy.empty()) ? kTrue : kFalse;
		std::vector<KCMParaText::KCMCharChange> chars;
		if (textDiffered && anyMark)
			CharChangesOf(source.fText, target.fText, sourceAttrs, targetAttrs, chars);

		// **EACH ATTRIBUTE IS COMPARED ON ITS OWN LIST**, and they cannot be merged into one pass:
		//   two sets of spans are matched by position within their OWN kind.
		// ⚠RUBY FIRST, KENTEN SECOND, and it does not matter: ChangeIsBefore re-sorts the whole list
		//   by fTargetStart afterwards. The order here is only what two changes standing at the very
		//   same character fall back on.
		auto compareAttr = [&](KCMStoryAttrKind kind,
							   const KCMAttrSpanList& sourceSpans, const KCMAttrSpanList& targetSpans)
		{
			CompareParagraphAttr(kind, sourceSpans, targetSpans, source, target, chars, out);
		};

		compareAttr(kKCMStoryAttrRuby, sourceStory.fAttrs[ai].fRuby, targetStory.fAttrs[bi].fRuby);

		compareAttr(kKCMStoryAttrKenten, sourceStory.fAttrs[ai].fKenten, targetStory.fAttrs[bi].fKenten);

		// NOTE MARKERS, THROUGH THE SAME DOOR (user's request). A reference is a
		//   character rather than an attribute, but the reader's question about it is the same one
		//   ruby answers - "what is standing over these words, and did it change" - and its VALUE is
		//   the number the page prints.
		//   ⚠**AN ENDNOTE'S TEXT IS NOT COMPARED HERE**: it lives in another story
		//     (kEndnoteStoryBoss) and arrives as a story row of its own. This is the marker alone.
		compareAttr(kKCMStoryAttrFootnote, sourceStory.fAttrs[ai].fFootnote, targetStory.fAttrs[bi].fFootnote);
		compareAttr(kKCMStoryAttrEndnote,  sourceStory.fAttrs[ai].fEndnote,  targetStory.fAttrs[bi].fEndnote);

		// WARICHU AND TATE-CHU-YOKO (user's request), through the same door once more.
		//   Their VALUE is the characters they cover (KCMParaAttrs::fWarichu), so two things fall out
		//   of the rules above with nothing added here:
		//   - rewriting the words INSIDE one keeps its span where it starts on both sides, the pair
		//     is compared, the values differ, and a row of this kind stands beside the text row;
		//   - typing or deleting one TOGETHER WITH its words says nothing of its own (PairMarksAcross),
		//     so only the text row says so - the text is the subject, the mark its attendant.
		//   What IS added is in CompareParagraphAttr: of two nested layers, only the inner one
		//   reports a change of the words inside it.
		compareAttr(kKCMStoryAttrWarichu,  sourceStory.fAttrs[ai].fWarichu,  targetStory.fAttrs[bi].fWarichu);
		compareAttr(kKCMStoryAttrTcy,      sourceStory.fAttrs[ai].fTcy,      targetStory.fAttrs[bi].fTcy);

		// AN OBJECT PLACED IN THE TEXT, by its own door: paired by order, not by start
		//   (CompareParagraphObjects says why). Empty on both sides unless the reader was asked for them.
		CompareParagraphObjects(sourceStory.fAttrs[ai].fObject, targetStory.fAttrs[bi].fObject, source, target, out);
	};

	// Walk the two paragraph lists side by side, stepping over each reported change. What is left
	// between them lines up one to one - that is what "unchanged" means to the diff.
	for (size_t c = 0; c <= paragraphChanges.size(); ++c)
	{
		const int32 aStop = (c < paragraphChanges.size())
							? paragraphChanges[c].aStart : static_cast<int32>(sourceStory.fParas.size());
		const int32 bStop = (c < paragraphChanges.size())
							? paragraphChanges[c].bStart : static_cast<int32>(targetStory.fParas.size());

		while (a < aStop && b < bStop)
		{
			compareParagraphPair(a, b, kFalse);
			++a;
			++b;
		}

		if (c < paragraphChanges.size())
		{
			// AND THE PARAGRAPHS THE DIFF *DID* REPORT: a reader who rewrote a line and re-marked it in one pass is
			// shown both.
			// ⚠ONLY WHEN THE TWO SIDES HOLD THE SAME NUMBER OF PARAGRAPHS HERE. Then they line up one
			//   to one and their attribute lists can be matched by position, which is what
			//   CompareParagraphAttr needs. A change that INSERTS or DELETES paragraphs has no such
			//   correspondence, and pairing them off anyway would report readings and marks moving
			//   between paragraphs that have nothing to do with each other - a wrong answer where there
			//   is currently a missing one.
			if (paragraphChanges[c].aCount == paragraphChanges[c].bCount)
			{
				for (int32 k = 0; k < paragraphChanges[c].aCount; ++k)
				{
					// WHETHER THE WORDS DIFFERED IS ASKED OF THE PAIR: since the paragraph diff compares
					//   a table's place as well (ParagraphKey), a pair can be reported with the same words - a table
					//   moved - and its marks then stand where they stood, which is the unchanged paragraph's case.
					const int32 ai = paragraphChanges[c].aStart + k;
					const int32 bi = paragraphChanges[c].bStart + k;
					const bool16 wordsDiffer = (sourceStory.fParas[static_cast<size_t>(ai)] == targetStory.fParas[static_cast<size_t>(bi)])
											   ? kFalse : kTrue;
					compareParagraphPair(ai, bi, wordsDiffer);
				}
			}

			a = paragraphChanges[c].aStart + paragraphChanges[c].aCount;
			b = paragraphChanges[c].bStart + paragraphChanges[c].bCount;
		}
	}
}

/** Reading order, for putting the ruby children back among the text ones. */
bool ChangeIsBefore(const KCMStoryChange& x, const KCMStoryChange& y)
{
	return x.fTargetStart < y.fTargetStart;
}

/* MarkOverset
   Says of each change whether the text it names is composed anywhere the reader can see it.

   **ONE PASS, HERE, RATHER THAN A QUESTION THE PANEL ASKS WHILE IT DRAWS.** The list is drawn
   many times over between comparisons, and every road that rebuilds a row runs the diff, and the
   diff runs this. (The cost is a parcel-list lookup per change, and composing what is damaged up to
   it; asked from the cell drawing it would be one per change per repaint, and worse, a "read-only"
   call in a draw path is how this SDK gets talked into recomposing.)
   ⚠**SO IT SAYS WHAT WAS TRUE AT THE COMPARISON.** A frame made smaller, or text typed above the
   change, moves the edge without touching this row (measured) - the ID column keeps its
   word until the next Refresh. The press does not rely on it: KCMStoryJumpToChange asks again.

   ⚠**THE TARGET SIDE ONLY.** A change's fTargetStart names the newer document, which is the one
   with frames on screen; the older side is a task-start copy with no window at all for a
   Task Start comparison, so "is it visible" has no meaning over there.
*/
void MarkOverset(IDataBase* targetDB, UID storyUID, std::vector<KCMStoryChange>& changes)
{
	if (targetDB == nil || changes.empty())
		return;

	InterfacePtr<ITextModel> model(UIDRef(targetDB, storyUID), UseDefaultIID());
	if (model == nil)
		return;		// every change keeps kFalse, which is what the panel did before this existed

	for (size_t i = 0; i < changes.size(); ++i)
	{
		// The position the reader is SHOWN, not the write's: a whole paragraph's write
		//   range starts at the return of the paragraph ABOVE, and asking about that character said
		//   "composed" for a new paragraph that had itself gone over the edge.
		TextIndex from = changes[i].fTargetStart;
		TextIndex to = changes[i].fTargetEnd;
		KCMShownSpan(changes[i].fBreakAt, from, to);
		changes[i].fOverset = KCMChangeIsOverset(model, from, to);
	}
}

/* ParagraphIndexAt
   The paragraph whose start is the last one at or before `at` - the starts ascend, the walk being in TextIndex order. -1 before
   the first paragraph. */
int32 ParagraphIndexAt(const std::vector<int32>& starts, TextIndex at)
{
	return static_cast<int32>(std::upper_bound(starts.begin(), starts.end(), at) - starts.begin()) - 1;
}

/* ParagraphHolding
   ParagraphIndexAt for a position a change names - which can stand on what a paragraph BEGINS with (a table, a note's marker): that
   stands before the paragraph's recorded start and belongs to the paragraph all the same (KCMParaAttrs::fLeadingUncounted). */
int32 ParagraphHolding(const StoryParas& story, TextIndex at)
{
	const int32 which = ParagraphIndexAt(story.fStarts, at);
	const int32 next = which + 1;
	if (next < static_cast<int32>(story.fStarts.size())
		&& KCMParaText::ParagraphLineStart(story.fStarts, story.fAttrs, next) <= at)
		return next;
	return which;
}

/* CaretOnThePage
   Where a caret for what `story` no longer has can stand: at `at` when a paragraph the page sets reaches it, else at the end of the last one before it. */
TextIndex CaretOnThePage(const StoryParas& story, TextIndex at)
{
	const int32 which = ParagraphHolding(story, at);
	if (which < 0)
		return 0;
	const TextIndex end = KCMParaText::ParagraphReturn(story.fParas, story.fStarts, story.fAttrs, which);
	return (at > end) ? end : at;
}

/* MarkPlaces
   Says of each change where its words stand - the body, a cell, a footnote - for the ID column
   (the author: "Cell Text" for a change inside a cell, "Text" for an ordinary one).

   THE TARGET SIDE'S PARAGRAPHS, BY POSITION: the paragraph whose start is the last one at or before
     the change's target position. starts[] runs in document order, and a cell's thread stands after
     the whole body (ITableTextContent.h), so the place is one lookup. A removed paragraph
     has no paragraph of its own in the target; its caret stands in the paragraph next to where it was,
     which is the right place to name.
     ⚠UNLESS THE TARGET HAS NO SUCH PLACE (kKCMBreakNone - a footnote gone with its reference): that row's caret is only where a press goes, and
     what it is about stands in the Source. Named by the caret it read as body text, and Apply refused it unless the caret happened to fall in
     another footnote (measured).
*/
void MarkPlaces(const StoryParas& source, const StoryParas& target, std::vector<KCMStoryChange>& changes)
{
	for (size_t c = 0; c < changes.size(); ++c)
	{
		const bool16 inSource = (changes[c].fWholeParagraph && changes[c].fKind == KCMStoryChange::kDelete
								 && changes[c].fBreakAt == kKCMBreakNone) ? kTrue : kFalse;
		const StoryParas& side = inSource ? source : target;
		const int32 which = ParagraphHolding(side, inSource ? changes[c].fSourceStart : changes[c].fTargetStart);
		if (which < 0)
			continue;			// before the first paragraph: the body's default stands
		const KCMParaAttrs& a = side.fAttrs[static_cast<size_t>(which)];
		changes[c].fPlace = a.IsCell() ? kKCMPlaceCell : (a.IsFootnote() ? kKCMPlaceNote : kKCMPlaceBody);
	}
}

/* ParaIsCellOfTable
   Is paragraph `para` of `attrs` a cell of table `ordinal`? Out-of-range indices answer kFalse, so a
   caller may hand it the -1 ParagraphIndexAt returns. */
bool16 ParaIsCellOfTable(const std::vector<KCMParaAttrs>& attrs, int32 para, int32 ordinal)
{
	return (para >= 0 && static_cast<size_t>(para) < attrs.size()
			&& attrs[para].IsCell() && attrs[para].fTableOrdinal == ordinal) ? kTrue : kFalse;
}

/* FirstTableWords
   The first paragraph of table `ordinal` (in `paras`, whose attrs name the table) that holds any
   text - what the Table row shows after its shape word. Empty when every cell is empty. */
std::string FirstTableWords(int32 ordinal, const std::vector<std::string>& paras, const std::vector<KCMParaAttrs>& attrs)
{
	for (size_t p = 0; p < paras.size() && p < attrs.size(); ++p)
		if (attrs[p].IsCell() && attrs[p].fTableOrdinal == ordinal && !paras[p].empty())
			return paras[p];
	return std::string();
}

/* AllTableWords
   Every paragraph of table `ordinal`, in reading order, joined by a return - what two tables have to agree on, word for
   word, before a table that lost its id is taken for its partner (FoldTableChanges' second pairing). */
std::string AllTableWords(int32 ordinal, const std::vector<std::string>& paras, const std::vector<KCMParaAttrs>& attrs)
{
	std::string all;
	for (size_t p = 0; p < paras.size() && p < attrs.size(); ++p)
		if (attrs[p].IsCell() && attrs[p].fTableOrdinal == ordinal)
		{
			all += paras[p];
			all += '\r';
		}
	return all;
}

/* IdsNameOneTableEach
   Every id there and no two alike: what the pairing by id rests on. A missing or repeated id means the ids were not read the
   way the pairing assumes, and the position is then the better answer. */
bool16 IdsNameOneTableEach(const std::vector<UID>& ids)
{
	for (size_t i = 0; i < ids.size(); ++i)
	{
		if (ids[i] == kInvalidUID)
			return kFalse;
		for (size_t j = i + 1; j < ids.size(); ++j)
			if (ids[i] == ids[j])
				return kFalse;
	}
	return kTrue;
}

/* SourceTableIds
   The Source's tables by the ids that name the same table on both sides, when there are such ids.
   @return kFalse = no: the tables pair by their order.
*/
bool16 SourceTableIds(UID targetStoryUID, ITextModel* targetModel, ITextModel* sourceModel,
					  const std::vector<KCMTableShape>& sShapes, bool16 pairedByPlace, std::vector<UID>& sIds)
{

	// **WHICH TABLE IS WHICH, BY THE TABLES' OWN IDS.** The ids come
	//   out of the SOURCE's own INX, and a table's id is the uid the document that wrote the text
	//   gave it, so the same table is named alike on both sides (KCMTableSnippet.h; measured, including that an id
	//   moves for no insertion, no removal and no undo).
	// ⚠**ONLY WHEN THE TWO SIDES CARRY THE SAME DOCUMENT'S NUMBERS.** A Source that is some OTHER
	//   document holds no story under this uid and answers nothing, and a text that names one table
	//   without a Self of its own can name any of them wrongly - in both cases the pairing falls back
	//   to the position, which is what it always was.
	bool16 byId = kFalse;
	// **AND THE TEST FOR "IS THE SOURCE A COPY OF THIS DOCUMENT" IS THE UID ITSELF.** A copy
	//   saved and opened carries the numbers the original carries, so asking the Source's INX for
	//   THIS story's uid answers only when the two are the same document.
	// NOT FOR A STORY PAIRED BY PLACE (KCMStoryKinds.h, kKCMStoryKindByPosition): the Target's story is
	//   a COPY pasted in, so its tables were made anew with new ids, and no id of the Source's can name one - while the
	//   Target's uid handed to the Source names some other story there, or none. The tables pair by their ORDER instead,
	//   the fall-back this has always had for a Source that is another document: the copy holds the same tables in the
	//   same order, so a cell edited in one of them is one "Table ≠" row (design 3-3), not a Table − and a Table +.
	IDataBase* const sourceDB = ::GetDataBase(sourceModel);
	KCMMemXferBytes sourceInx;
	if (!pairedByPlace
		&& KCMExportStoryInx(sourceDB, targetStoryUID, sourceInx)
		&& KCMReadTableIdsInStory(sourceInx.GetData(), sourceInx.GetSize(), targetStoryUID, sIds)
		&& sIds.size() == sShapes.size() && !sIds.empty())
	{
		// ⚠**THE n-TH TABLE OF THE TEXT HAS TO BE THE n-TH TABLE OF THE MODEL.** Both walk the
		//   story in document order with nested tables counted where they stand (KCMTableShape.h),
		//   but that is a claim about two separate pieces of code, so what can be checked cheaply
		//   is checked: an id that is missing, or one that appears twice, means the text was not
		//   read the way this assumes - and the pairing falls back to the position rather than
		//   naming the wrong table with great confidence.
		byId = IdsNameOneTableEach(sIds);
	}
	// AND FROM THE COPY ITSELF WHEN ITS INX CANNOT BE HAD (the per-row Apply of tables, design
	//   docs/superpowers/specs/2026-10-27-kcm-apply-tables-notes-per-row-design.md §5-1): a database CLONE - an import's
	//   copy, the Task Start kept in memory - cannot be exported to INX (KCMResourceSnapshot.cpp), so the road above
	//   answered nothing for it and the tables paired by their POSITION: a table the files took away made the tables
	//   after it pair with the wrong partners ("3×2 -> 2×2", measured on the import matrix's A55). A clone holds the
	//   Target's own uids, and KCMReadTableShapes has read each table's dictionary uid from it already - the very id
	//   the INX would have named. The same checks as above: a missing or repeated uid falls back to the position.
	if (!byId && !pairedByPlace && sourceDB != ::GetDataBase(targetModel)
		&& (KCMIsImportClone(sourceDB) || KCMIsExternalSource(sourceDB)) && !sShapes.empty())
	{
		sIds.clear();
		for (size_t j = 0; j < sShapes.size(); ++j)
			sIds.push_back(sShapes[j].fDictUID);
		byId = IdsNameOneTableEach(sIds);
	}
	return byId;
}

/** One table and its partner: an index into each side's list, -1 for a side that has none. */
struct TablePair
{
	int32	fTarget;
	int32	fSource;

	TablePair(int32 target, int32 source) : fTarget(target), fSource(source) {}
};

/* PairTables
   Every Target table with the Source table it is, then the Source's tables nothing paired with.
*/
void PairTables(const StoryParas& target, const StoryParas& source, bool16 byId, const std::vector<UID>& sIds,
				std::vector<TablePair>& pairs)
{
	const std::vector<KCMTableShape>& tShapes = target.fTables;
	const std::vector<KCMTableShape>& sShapes = source.fTables;

	// The pairs: every Target table with the Task Start table it IS, then Task Start's tables that
	// nothing paired with (Table −). ⚠A table pairs with at most one - an id is taken once, so a
	// recycled id cannot make two tables claim the same partner.
	std::vector<bool16> sTaken(sShapes.size(), kFalse);
	for (size_t i = 0; i < tShapes.size(); ++i)
	{
		int32 partner = -1;
		if (byId)
		{
			const UID mine = tShapes[i].fDictUID;
			for (size_t j = 0; j < sShapes.size(); ++j)
				if (!sTaken[j] && sIds[j] == mine)
				{
					partner = static_cast<int32>(j);
					break;
				}
		}
		else if (i < sShapes.size() && !sTaken[i])
			partner = static_cast<int32>(i);
		if (partner >= 0)
			sTaken[static_cast<size_t>(partner)] = kTrue;
		pairs.push_back(TablePair(static_cast<int32>(i), partner));
	}
	// A TABLE THAT CAME BACK UNDER A NEW ID IS STILL ITS PARTNER (any table put back: Ctrl+Z of a table's deletion, a
	//   paste). It comes back under a NEW id (measured: 328 in the Target, 286 in Task Start, the
	//   same B1|B2 inside), so the id pairing above found no partner for either, and the list showed a Table + and a
	//   Table − for one unchanged table. What is left unpaired on BOTH sides is paired again, in order, when the two are
	//   the same table by everything but the id: the same shape, the same words in every cell, and the same words in
	//   the paragraph each stands in. ⚠Only after the id pairing, and only among the left-over tables - a table that
	//   paired by its id is never taken from its partner.
	if (byId)
	{
		size_t nextS = 0;
		for (size_t p = 0; p < pairs.size(); ++p)
		{
			if (pairs[p].fSource >= 0)
				continue;
			const int32 i = pairs[p].fTarget;
			const int32 tPara = ParagraphIndexAt(target.fStarts, tShapes[static_cast<size_t>(i)].fAnchorStart);
			const std::string tWords = AllTableWords(i, target.fParas, target.fAttrs);
			for (size_t j = nextS; j < sShapes.size(); ++j)
			{
				if (sTaken[j] || KCMTableShapesDiffer(tShapes[static_cast<size_t>(i)], sShapes[j]))
					continue;
				if (AllTableWords(static_cast<int32>(j), source.fParas, source.fAttrs) != tWords)
					continue;
				const int32 sPara = ParagraphIndexAt(source.fStarts, sShapes[j].fAnchorStart);
				if (tPara < 0 || sPara < 0 || static_cast<size_t>(tPara) >= target.fParas.size()
					|| static_cast<size_t>(sPara) >= source.fParas.size() || target.fParas[static_cast<size_t>(tPara)] != source.fParas[static_cast<size_t>(sPara)])
					continue;
				pairs[p].fSource = static_cast<int32>(j);
				sTaken[j] = kTrue;
				nextS = j + 1;
				break;
			}
		}
	}
	for (size_t j = 0; j < sShapes.size(); ++j)
		if (!sTaken[j])
			pairs.push_back(TablePair(-1, static_cast<int32>(j)));
}

/* FoldTableChanges
   A TABLE WHOSE SHAPE DIFFERS FROM TASK START'S BECOMES ONE CHANGE (the author:
   "fold every change of that table - rows, columns, merged cells, and the words inside - into one row,
   Table ≠"). The cell-level changes the paragraph diff found inside it are taken out; only the spans
   they mark stay, on fMarkSpans.
   A table with no partner is Table + (only here) or Table − (only in Task Start).

   ⚠WHICH TABLE IS WHICH: by the tables' own ids when the two sides carry one document's numbers (SourceTableIds), else
    by their order - exact when tables were added or removed at the END of the story and merely noisy when one was added
    in the middle (every table after it then pairs with the wrong one and comes out as ≠: nothing is named falsely,
    there are
    just more rows). */
void FoldTableChanges(std::vector<KCMStoryChange>& out, UID targetStoryUID,
					  ITextModel* targetModel, ITextModel* sourceModel,
					  const StoryParas& target, const StoryParas& source, bool16 pairedByPlace)
{
	std::vector<UID> sIds;
	const bool16 byId = SourceTableIds(targetStoryUID, targetModel, sourceModel, source.fTables, pairedByPlace, sIds);
	std::vector<TablePair> pairs;
	PairTables(target, source, byId, sIds, pairs);

	for (size_t p = 0; p < pairs.size(); ++p)
	{
		const int32 tAt = pairs[p].fTarget;
		const int32 sAt = pairs[p].fSource;
		const bool16 haveT = (tAt >= 0) ? kTrue : kFalse;
		const bool16 haveS = (sAt >= 0) ? kTrue : kFalse;
		// nil for a side with no table (a Table + or a Table -).
		const KCMTableShape* const tShape = haveT ? &target.fTables[static_cast<size_t>(tAt)] : nil;
		const KCMTableShape* const sShape = haveS ? &source.fTables[static_cast<size_t>(sAt)] : nil;
		if (haveT && haveS && !KCMTableShapesDiffer(*tShape, *sShape))
			continue;					// the same shape: the cell rows stay as the diff made them

		// ⚠**THE TWO SIDES NUMBER THEIR TABLES SEPARATELY** now that they are paired by id: the cells
		//   of this table are the ones whose own side's ordinal is this side's index (tAt, sAt).
		KCMStoryChange table;
		table.fWhat = KCMStoryChange::kTable;
		table.fPlace = kKCMPlaceCell;
		table.fKind = (!haveS) ? KCMStoryChange::kInsert
					: (!haveT) ? KCMStoryChange::kDelete
					: KCMStoryChange::kReplace;
		if (haveT)
		{
			table.fTargetStart = tShape->fAnchorStart;
			table.fTargetEnd = tShape->fAnchorEnd;
			table.fTargetTableUID = tShape->fDictUID;
		}
		if (haveS)
		{
			table.fSourceStart = sShape->fAnchorStart;
			table.fSourceEnd = sShape->fAnchorEnd;
			// THE SOURCE'S OWN ID ("Match the Source"): the dictionary uid in the Source document.
			table.fSourceTableUID = sShape->fDictUID;
		}

		// The cell-level changes of THIS table come out; what they say about paired cells stays.
		std::vector<KCMStoryChange> kept;
		kept.reserve(out.size());
		for (size_t c = 0; c < out.size(); ++c)
		{
			const KCMStoryChange& ch = out[c];
			if (ch.fWhat == KCMStoryChange::kTable || ch.fWhat == KCMStoryChange::kRefused)
			{
				kept.push_back(ch);
				continue;
			}
			// Where the change stands on each side: the SHOWN range's start (a whole paragraph's range
			// carries the return of the paragraph before, and asking about that character would name the
			// neighbour; an EMPTY paragraph's range has no width at all - a new empty cell, measured - so the range's
			// width cannot be what decides). The side that has the change
			// is told by its kind: an insertion is on the target side, a deletion on the source side, a
			// replacement (and an attribute) on both.
			TextIndex tFrom = ch.fTargetStart, tTo = ch.fTargetEnd;
			TextIndex sFrom = ch.fSourceStart, sTo = ch.fSourceEnd;
			KCMShownSpan(ch.fBreakAt, tFrom, tTo);
			KCMShownSpan(ch.fBreakAt, sFrom, sTo);
			const bool16 sideT = (ch.fKind != KCMStoryChange::kDelete) ? kTrue : kFalse;
			const bool16 sideS = (ch.fKind != KCMStoryChange::kInsert) ? kTrue : kFalse;
			const int32 tPara = sideT ? ParagraphHolding(target, tFrom) : -1;
			const int32 sPara = sideS ? ParagraphHolding(source, sFrom) : -1;
			const bool16 tHolds = ParaIsCellOfTable(target.fAttrs, tPara, tAt);
			const bool16 sHolds = ParaIsCellOfTable(source.fAttrs, sPara, sAt);
			if (!tHolds && !sHolds)
			{
				kept.push_back(ch);
				continue;
			}

			// **A CHANGE IN A CELL THAT IS THE SAME CELL ON BOTH SIDES STAYS A ROW OF ITS OWN** (the
			//   author's rule: "the cells whose structure did not change - take those back with the change history;
			//   the cells whose structure changed - with the new mechanism"). The same address and the same merge
			//   on both sides (KCMTableCellSame): its words are a Cell Text row, acted on one by one; only a change in
			//   a cell the shape brought, took or re-merged is folded into
			//   the Table row - which is exactly what "Match the Source" rewrites (KCMTableMatch.h). ⚠Both tables have
			//   to exist for a cell to be the same on both sides; a Table + or − folds everything, as before.
			if (haveT && haveS)
			{
				const int32 cellPara = tHolds ? tPara : sPara;
				const std::vector<KCMParaAttrs>& cellAttrs = tHolds ? target.fAttrs : source.fAttrs;
				const int32 row = cellAttrs[static_cast<size_t>(cellPara)].fCellRow;
				const int32 col = cellAttrs[static_cast<size_t>(cellPara)].fCellCol;
				if (KCMTableCellSame(*tShape, *sShape, row, col))
				{
					kept.push_back(ch);
					continue;
				}
			}

			if (tHolds)
				table.fMarkSpans.push_back(KCMTextSpan(tFrom, tTo));

		}

		// Cells only here (a new row or column, by address) and cells whose merge differs: marked whole.
		if (haveT)
		{
			for (size_t i = 0; i < tShape->fCells.size(); ++i)
			{
				const KCMTableCellPlace& cell = tShape->fCells[i];
				if (!haveS || !KCMTableCellSame(*tShape, *sShape, cell.fRow, cell.fCol))
					table.fMarkSpans.push_back(KCMTextSpan(cell.fStart, cell.fEnd));
			}
		}
		else
		{
			// Table −: the place it stood, as a caret in the Target.
			// ⚠**THE SOURCE'S OWN ANCHOR POSITION, CLAMPED TO THE STORY** (measured on
			//   the running application). It was the caret of one of the folded cell changes, which is a
			//   position among the CELLS - and cells stand past the whole body (ITableTextContent.h), so
			//   for a table that is GONE from the Target it came out past the end of the story: 12 in a
			//   story of 10. Nothing drew a mark there, and every write refused it with "the change's
			//   range is outside the story" - the range test in RestoreOne stands before the one that
			//   would have said the real reason, so even the refusal was misleading.
			//   The anchor stood in the BODY, and the body agrees on both sides unless it was edited
			//   too - and an edit there is a row of its own.
			//   ⚠Not in the body, it is a table in a cell whose own table went too: past the paragraphs the page sets, in a tracked deletion's threads, a
			//    press said "(Overset Text)" (measured).
			const TextIndex at = CaretOnThePage(target, sShape->fAnchorStart);
			table.fTargetStart = at;
			table.fTargetEnd = at;
			table.fMarkSpans.push_back(KCMTextSpan(at, at));
		}

		// The Story column: the shape word, then the table's first words (on the side that has it).
		// **A TABLE THIS VERSION ADDED SHOWS ITS SHAPE ALONE** (the author: "when a table
		//   is added, the Story column shows its contents too - that is not wanted, 2×1 or so is
		//   enough"). A Table + has no partner in the older version, so what it SAYS is not the
		//   difference - the whole table is - and the shape is the whole of what names it.
		//   ⚠The other two keep their words on purpose: a Table ≠ and a Table − are about ONE table
		//    among several, and the first words are how the reader tells which.
		table.fShapeWord = (haveT && haveS) ? KCMTableShapeWord(*sShape, *tShape)
						 : KCMTableShapeAlone(haveT ? *tShape : *sShape);
		std::string words = table.fShapeWord;
		if (haveS)
		{
			const std::string first = haveT ? FirstTableWords(tAt, target.fParas, target.fAttrs)
											: FirstTableWords(sAt, source.fParas, source.fAttrs);
			if (!first.empty())
			{
				words += " ";
				words += first;
			}
		}
		table.fText.SetUTF8String(words);
		// The other side, for the message area: Task Start's shape and first words.
		std::string other = haveS ? KCMTableShapeAlone(*sShape) : std::string();
		const std::string otherFirst = haveS ? FirstTableWords(sAt, source.fParas, source.fAttrs) : std::string();
		if (!otherFirst.empty())
		{
			other += " ";
			other += otherFirst;
		}
		table.fOtherText.SetUTF8String(other);

		// A TABLE ADDED OR TAKEN AWAY TOGETHER WITH THE PARAGRAPH IT STANDS IN IS ONE ROW. The import puts
		//   a table Word added into a paragraph of its own - a return first, then the table (KCMStorySyncApply's
		//   InsertTables) - and InDesign's change history holds the two as ONE change. The paragraph came out as a row of
		//   its own: an "insert" with no words beside the Table + row, over the same characters. Rejecting
		//   either took both back and left the other with nothing to stand for (measured on the running application with
		//   Reject and Redo from Word: the Table row vanished from the list, and the empty row's redo wrote nothing and
		//   still left an undo step). The empty paragraph's row is folded into the table's, which takes its range - the
		//   return included, cut off again for what the reader is shown (fBreakAt, KCMShownSpan) - so the one change is
		//   one row, acted on whole. ONLY A PARAGRAPH HOLDING NOTHING BUT THE TABLE: the row's shown range has to be exactly the table's
		//   anchor, on the side that has the table - a paragraph with words of its own stays a row of its own.
		if (haveT != haveS)
		{
			const TextIndex anchorFrom = haveT ? table.fTargetStart : table.fSourceStart;
			const TextIndex anchorTo = haveT ? table.fTargetEnd : table.fSourceEnd;
			for (size_t c = 0; c < kept.size(); ++c)
			{
				const KCMStoryChange& ch = kept[c];
				if (ch.fWhat != KCMStoryChange::kText || !ch.fWholeParagraph || ch.fKind != table.fKind)
					continue;
				TextIndex from = haveT ? ch.fTargetStart : ch.fSourceStart;
				TextIndex to = haveT ? ch.fTargetEnd : ch.fSourceEnd;
				KCMShownSpan(ch.fBreakAt, from, to);
				if (from != anchorFrom || to != anchorTo)
					continue;
				table.fTargetStart = ch.fTargetStart;
				table.fTargetEnd = ch.fTargetEnd;
				table.fSourceStart = ch.fSourceStart;
				table.fSourceEnd = ch.fSourceEnd;
				table.fBreakAt = ch.fBreakAt;
				table.fWholeParagraph = kTrue;
				kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(c));
				break;
			}
		}

		kept.push_back(table);
		out.swap(kept);
	}
}

/* ParagraphKeys
   What the paragraph diff compares each paragraph of a version by.
*/
void ParagraphKeys(const StoryParas& story, std::vector<std::string>& out)
{
	// AND A PARAGRAPH IS ITS WORDS AND THE PLACES OF ITS TABLES (the import matrix's X06/Y06 - the author:
	//   "the table moved, so it changed: I want it as a change row"). Compared by its words alone, words moved across a
	//   table left the paragraph "the same", and nothing came of it: the import had put the move into its copy (its
	//   planner asks the same question - KCMStorySync's ComparePara) and the list showed nothing to Apply.
	out.clear();
	out.reserve(story.fParas.size());
	for (size_t p = 0; p < story.fParas.size(); ++p)
		out.push_back(KCMParaText::ParagraphKey(story.fParas[p], KCMParaText::AttrsOfParagraph(story.fAttrs, static_cast<int32>(p))));
}

/* SplitRunsAtPlaces
   The paragraph diff's runs, cut where they leave one place for another.
*/
void SplitRunsAtPlaces(const StoryParas& source, const StoryParas& target, std::vector<KCMTextDiff::Change>& paragraphChanges)
{
	// **ONE ROW PER PLACE, NOT ONE PER RUN.** A CELL IS A PARAGRAPH, so two edits that happened
	//   to land in adjacent paragraphs come back as ONE run even when one of them is body text
	//   and the next is inside the table. The row then spans everything between them --
	//   including text nobody touched. MEASURED on the tablespan document, three one-character
	//   edits:
	//
	//       CHANGE t=[18,40) 「したよ。¶[表]表の後の段落です。¶あたらしい」   <- one row,
	//       22 characters
	//
	//   @warning ONLY WHERE A RUN LEAVES ONE PLACE FOR ANOTHER. Paragraphs of the same place
	//     still share a row, which is what a cell holding several paragraphs -- and ordinary body
	//     text -- depends on. And only when the two versions pass through the same places:
	//     SplitRunAtPlaces leaves a run whole rather than pair its halves up wrongly.
	std::vector<KCMTextDiff::Change> byPlace;
	std::vector<KCMParaText::RegionPair> pieces;
	for (size_t c = 0; c < paragraphChanges.size(); ++c)
	{
		const KCMTextDiff::Change& run = paragraphChanges[c];
		KCMParaText::SplitRunAtPlaces(source.fAttrs, run.aStart, run.aCount,
										   target.fAttrs, run.bStart, run.bCount, pieces);
		for (size_t k = 0; k < pieces.size(); ++k)
		{
			KCMTextDiff::Change piece;
			piece.aStart = pieces[k].fSourceStart;
			piece.aCount = pieces[k].fSourceCount;
			piece.bStart = pieces[k].fTargetStart;
			piece.bCount = pieces[k].fTargetCount;
			byPlace.push_back(piece);
		}
	}
	paragraphChanges.swap(byPlace);
}

/* CutUnevenRuns
   The paragraph diff's runs, with those holding more paragraphs on one side cut into pairs, additions and removals.
*/
void CutUnevenRuns(const StoryParas& source, const StoryParas& target,
				   const std::vector<std::string>& sourceKeys, const std::vector<std::string>& targetKeys,
				   std::vector<KCMTextDiff::Change>& paragraphChanges)
{
	// **A RUN HOLDING MORE PARAGRAPHS ON ONE SIDE IS CUT INTO PAIRS, ADDITIONS AND REMOVALS.** The paragraph diff
	// merges an added paragraph with an edited one beside it into one
	//   run, and the character pass then wrote "NEW\r" into the head of the edited paragraph - the new
	//   paragraph taking THAT paragraph's style, with no next style and no whole-paragraph row. Paired the
	//   way the import pours (KCMParaPairing - the paragraphs sharing the most characters pair), the edited
	//   paragraph is an edit and the new one is a paragraph of its own. A pair whose words are the same is
	//   no change at all and is left out, which is also what AddAttributeChanges reads as "unchanged".
	std::vector<KCMTextDiff::Change> expanded;
	for (size_t c = 0; c < paragraphChanges.size(); ++c)
	{
		const KCMTextDiff::Change& run = paragraphChanges[c];
		if (run.aCount == run.bCount || run.aCount == 0 || run.bCount == 0)
		{
			expanded.push_back(run);
			continue;
		}
		const std::vector<std::string> a(source.fParas.begin() + run.aStart, source.fParas.begin() + run.aStart + run.aCount);
		const std::vector<std::string> b(target.fParas.begin() + run.bStart, target.fParas.begin() + run.bStart + run.bCount);
		std::vector<KCMParaPairing::Step> steps;
		KCMParaPairing::Pair(a, b, steps);
		int32 nextA = run.aStart;
		int32 nextB = run.bStart;
		for (size_t s = 0; s < steps.size(); ++s)
		{
			const KCMParaPairing::Step& step = steps[s];
			KCMTextDiff::Change piece;
			if (step.fKind == KCMParaPairing::Step::kPair)
			{
				piece.aStart = run.aStart + step.fDoc;
				piece.aCount = 1;
				piece.bStart = run.bStart + step.fFile;
				piece.bCount = 1;
				nextA = piece.aStart + 1;
				nextB = piece.bStart + 1;
				// (the KEYS: the same words with a table moved are still a change - see ParagraphKeys)
				if (sourceKeys[static_cast<size_t>(piece.aStart)] == targetKeys[static_cast<size_t>(piece.bStart)])
					continue;
			}
			else if (step.fKind == KCMParaPairing::Step::kInsert)
			{
				piece.aStart = run.aStart + step.fDoc + 1;	// the target holds them and the source does not
				piece.aCount = 0;
				piece.bStart = run.bStart + step.fFile;
				piece.bCount = step.fCount;
				nextB = piece.bStart + piece.bCount;
			}
			else
			{
				piece.aStart = run.aStart + step.fDoc;		// the source holds them and the target does not
				piece.aCount = step.fCount;
				piece.bStart = nextB;
				piece.bCount = 0;
				nextA = piece.aStart + piece.aCount;
			}
			expanded.push_back(piece);
		}
	}
	paragraphChanges.swap(expanded);
}

/* AddChangesOfRun
   The changes of one run of paragraphs standing on both sides: the run narrowed down to the characters that differ.
*/
void AddChangesOfRun(std::vector<KCMStoryChange>& out, const KCMTextDiff::Change& change,
					 const StoryParas& source, const StoryParas& target)
{
	// The run itself, so that a position inside it can be asked for rather than added up -- see RunSide.
	const RunSide tRun(target, change.bStart, change.bCount);
	const RunSide sRun(source, change.aStart, change.aCount);

	// The second pass: narrow the run down to the characters that actually differ, so that a
	// one-word edit selects the word and not the paragraph it sits in.
	// BETWEEN THE RUN'S TABLES, WHEN THEY PAIR (the import matrix's X06/Y06): one piece per stretch from
	//   one table to the next, every step of the pass kept inside its piece (KCMTextDiff::DiffCharacters) - so words
	//   that crossed a table come back as words put in on one side of it and taken out on the other, which is what
	//   moved: a table's anchor cannot. A table that came or went (the counts differ) cuts nothing - the whole run, as
	//   it always was, and the table's own row says what happened to it (FoldTableChanges).
	const bool16 tablesPair = (sRun.fTables.size() == tRun.fTables.size()) ? kTrue : kFalse;
	// THE SAME WORDS WITH NO TABLE MOVED ARE NO CHANGE OF WORDS: a run the keys made of tables that only came or went.
	//   "The whole run, as one change" below would be a row with nothing in it.
	if (sRun.fCodePoints == tRun.fCodePoints && (!tablesPair || sRun.fTables == tRun.fTables))
		return;
	const std::vector<int32> noMarks;
	std::vector<KCMTextDiff::Change> fineChanges;

	// **ONLY ON CHARACTERS.** Applied to the paragraph list this would report paragraphs nobody
	//   touched as changed -- see KCMTextDiff.h. The same restriction covers the alignment
	//   below, for a plainer reason: a paragraph token is a number, and asking which script it
	//   is written in means nothing.
	// The two steps after the diff, as DiffCharacters takes them (only where the diff found something):
	// ⚠**THE PARAGRAPH BREAK IS HANDED OVER AS SOMETHING NOT TO SWALLOW**.
	//   JoinParagraphs puts exactly one character between two paragraphs of a run
	//   (KCMParaText::kJoinedParagraphBreak), so that code point is what marks a break here - and a change that contains one writes
	//   over it, which costs the following paragraphs their styles (KCMTextDiff.h has
	//   the measurement).
	// **THEN SLIDE EACH RUN TO THE POSITION A READER WOULD PUT IT.** Myers returns A
	//   shortest edit script, not THE one a person would describe, and when the surrounding
	//   text repeats a character the two differ visibly. Reported from the panel:
	//   「旧版です」->「新版です・ここが違います」 came back quoting「す・ここが違いま」
	//   -- starting and ending on す because the run had been rotated one step left, which
	//   costs Myers nothing and costs the reader the whole sentence.
	//   @warning AFTER the merge, not before: merging changes the shape of a run, and
	//     aligning a run that is about to be swallowed would be work thrown away.
	//   kTrue = widen a change to the whole Latin word (letters or digits) it sits in: the
	//     marks are DRAWN, and a word drawn in two pieces shows a seam. KIDMCP, which shares
	//     this engine and prints instead, passes kFalse (KCMTextDiff.h says why).
	const bool16 narrowed = KCMTextDiff::DiffCharacters(sRun.fCodePoints, tablesPair ? sRun.fTables : noMarks,
														tRun.fCodePoints, tablesPair ? tRun.fTables : noMarks,
														fineChanges, KCMParaText::kJoinedParagraphBreak, kTrue)
							&& !fineChanges.empty();

	if (!narrowed)
	{
		// The whole run, as one change. This is where a run lands when the character pass
		// cannot place it - not an error, just a coarser answer.
		AddCutAtObjects(out, tRun, 0, static_cast<int32>(tRun.fCodePoints.size()),
						sRun, 0, static_cast<int32>(sRun.fCodePoints.size()));
		return;
	}

	for (size_t k = 0; k < fineChanges.size(); ++k)
	{
		const KCMTextDiff::Change& fine = fineChanges[k];
		AddCutAtObjects(out, tRun, fine.bStart, fine.bCount, sRun, fine.aStart, fine.aCount);
	}
}

/* CompareOneStory
   Fills out with everything that differs between the two versions of one story. kFalse means the
   story could not be compared at all; an empty out with kTrue means it was compared and the text
   is identical (the counters also move for formatting).
*/
bool16 CompareOneStory(const UIDRef& targetStory, const UIDRef& sourceStory, bool16 pairedByPlace,
					   std::vector<KCMStoryChange>& out)
{
	out.clear();

	// `sourceStory` IS THE ROW'S SOURCE STORY (KCMStoryRow::fSourceStoryUID): the Target's own uid for a
	//   story paired by UID, the Source's own story for one paired by the place of its first frame (`pairedByPlace`),
	//   which is also what FoldTableChanges is told - its tables pair by order, not by id.

	// READ STRAIGHT FROM THE TEXT MODEL (KCMTextRead.h carries why): the document is asked once, and its answer is the
	// only one.
	//
	//   ONE READ, ONE MOMENT. Ruby comes out of the same walk as the text: a comparison is one moment, and reading the
	//   ruby separately would put two moments in one row.
	StoryParas target;
	StoryParas source;

	// ⚠**THE ONE REMAINING WAY TO DECLINE**, and it means one thing only: the story could not be
	//   opened at all. **An empty story is not a failure** (KCMTextRead.h).
	// THE OBJECTS PLACED IN THE TEXT ARE READ TOO (KCMObjectPrint.h), on both sides - but not for a story
	//   paired by place: its objects were made anew with new UIDs, and a fingerprint holds UIDs, so every object would
	//   differ (the plan's call for 2.0.0: docs/superpowers/plans/2026-10-27-kcm-story-object-changes.md).
	const bool16 withObjects = pairedByPlace ? kFalse : kTrue;
	if (!KCMTextRead::ReadStory(targetStory, target.fParas, target.fAttrs, target.fStarts, withObjects, &target.fTables))
		return kFalse;
	if (!KCMTextRead::ReadStory(sourceStory, source.fParas, source.fAttrs, source.fStarts, withObjects, &source.fTables))
		return kFalse;

	// **ONE TABLE FOR BOTH SEQUENCES.** Numbering them from separate tables would give equal
	//   paragraphs different tokens, and every paragraph would look changed.
	std::vector<std::string> sourceKeys;
	std::vector<std::string> targetKeys;
	ParagraphKeys(source, sourceKeys);
	ParagraphKeys(target, targetKeys);
	std::map<std::string, int32> table;
	std::vector<int32> sourceTokens;
	std::vector<int32> targetTokens;
	KCMTextDiff::Tokenize(sourceKeys, table, sourceTokens);
	KCMTextDiff::Tokenize(targetKeys, table, targetTokens);

	std::vector<KCMTextDiff::Change> paragraphChanges;
	if (!KCMTextDiff::Diff(sourceTokens, targetTokens, paragraphChanges))
		return kFalse;		// (a guard that cannot fire - KCMTextDiff.h) the row stays, the detail does not

	SplitRunsAtPlaces(source, target, paragraphChanges);
	CutUnevenRuns(source, target, sourceKeys, targetKeys, paragraphChanges);

	for (size_t c = 0; c < paragraphChanges.size(); ++c)
	{
		const KCMTextDiff::Change& change = paragraphChanges[c];

		// Paragraphs on one side only: one change per paragraph, its break in its range (AddWholeParagraphs).
		if ((change.aCount == 0) != (change.bCount == 0))
			AddWholeParagraphs(out, change, source, target);
		else
			AddChangesOfRun(out, change, source, target);
	}

	// **AND THEN THE MARKS.** Everything above compared the words and nothing else.
	AddAttributeChanges(paragraphChanges, source, target, out);

	// @warning put back in reading order. The marks' rows were found by a separate walk, so
	//   without this they would all sit after the text ones and the tree would run down the
	//   story twice. STABLE, so that two changes at the same position keep the order they were
	//   made in.
	std::stable_sort(out.begin(), out.end(), ChangeIsBefore);
	MarkPlaces(source, target, out);		// the body, a cell or a note - for the ID column

	// A TABLE WHOSE SHAPE CHANGED IS ONE ROW: its cell changes fold into it.
	InterfacePtr<ITextModel> targetModel(targetStory, UseDefaultIID());
	InterfacePtr<ITextModel> sourceModel(sourceStory, UseDefaultIID());
	FoldTableChanges(out, targetStory.GetUID(), targetModel, sourceModel, target, source, pairedByPlace);
	std::stable_sort(out.begin(), out.end(), ChangeIsBefore);

	return kTrue;
}

/* IsPairedRow
   A row with a story on both sides - what Run compares and RunOne refuses without.

   A story with no partner cannot be compared against anything, and the list already knows that -- this is the same judgement
   KCMStoryStamp made when it built the row, read rather than made again. Asking the older document for the UID ourselves
   would be a second place where "does this story exist over there" gets answered.
   **kKCMStoryKindUnpaired COVERS BOTH added AND removed.** @warning for a removed row this is not merely wasted work: its
     fStoryUID is a SOURCE uid, so the comparison would ask the TARGET for it -- and a uid that means one story over there can
     name a different object over here. The row must never get that far.
*/
bool16 IsPairedRow(const KCMStoryRow* row)
{
	return (row != nil && row->fStoryUID != kInvalidUID && (row->fKinds & kKCMStoryKindUnpaired) == 0) ? kTrue : kFalse;
}

/* AttachComparison
   What one row takes from a comparison of its story, whichever road ran it (Run, RunOne).
*/
void AttachComparison(int32 rowIndex, IDataBase* targetDB, UID storyUID, std::vector<KCMStoryChange>& changes, bool16 compared)
{
	MarkOverset(targetDB, storyUID, changes);
	KCMStoryList::SetRowChanges(rowIndex, changes, compared);
	// THE "=" RECORDS FOLLOW EDITS MADE OUTSIDE KCM (the import tidy S10): each is placed again from the
	//   comparison just attached - by its Source range, which never moves - so that a word typed before a "=" does not
	//   leave it reading stale, and Restore from Copy pastes at the words' new place (KCMStoryList::FollowRecords). Only
	//   when the story was read: an empty list from a story that could not be read says nothing about where anything is.
	if (compared)
		KCMStoryList::FollowRecords(rowIndex, targetDB);
	// THE "=" RECORDS OUTLIVE THIS REFRESH (stage 2 C - design 15-1-3): a Standing one stays where the
	//   diff now finds nothing; one undone or put back has its twin among the changes just attached and is dropped, so
	//   that the change is not shown twice.
	KCMStoryList::PruneEqual(rowIndex, targetDB);
	// the state it was compared at, for the list to follow an Undo / Redo (KCMStoryFollowObserver)
	KCMStoryList::SetRowComparedAt(rowIndex, KCMStoryDiffRun::CountAllChanges(UIDRef(targetDB, storyUID)));
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// Run
//----------------------------------------------------------------------------------------

int32 KCMStoryDiffRun::Run(IDataBase* targetDB, IDataBase* sourceDB, bool16* outCancelled)
{
	if (outCancelled != nil)
		*outCancelled = kFalse;
	if (targetDB == nil || sourceDB == nil)
		return 0;

	// **THE GUARD BELONGS HERE, NOT AT THE CALLER.** Reading a story can compose (asking for text that has never
	//   been laid out lays it out), and composing sets the modified flag on a document this
	//   feature only ever reads. KCM's whole premise is that comparing changes nothing.
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

	int32 total = 0;

	// How many rows will actually be read -- the progress bar's range.
	const int32 rowCount = KCMStoryList::GetRowCount();
	int32 pairedCount = 0;
	for (int32 i = 0; i < rowCount; ++i)
	{
		if (IsPairedRow(KCMStoryList::GetRow(i)))
			++pairedCount;
	}
	// **The bar, with its Cancel, appears after kKCMProgressBarDelayMs** (the author's call: "a document can hold an
	// enormous number of stories").
	const PMString barTitle = KCMSentence("Comparing stories...");
	// A stepper, not a bar of its own: an import covers the whole of its work - this loop included -
	//   with ONE bar, and two bars alive at once is the thing KCMProgressBar.h forbids.
	KCMProgressStepper progress(barTitle, pairedCount);
	int32 done = 0;

	for (int32 i = 0; i < rowCount; ++i)
	{
		const KCMStoryRow* row = KCMStoryList::GetRow(i);
		if (!IsPairedRow(row))
			continue;
		const UID storyUID = row->fStoryUID;

		PMString item = KCMSentence("Story ");	// not a key: it carries numbers (Append keeps the mark)
		item.AppendNumber(done + 1);
		item.Append(" / ");
		item.AppendNumber(pairedCount);
		progress.Step(done, item);		// `done` stories are read; this is also where the bar first appears, once the delay has passed
		KCM_DIAG_SLOW("story-diff-slow", 200);	// test builds only: the bar and its Cancel within reach (KCMDiag.h)

		std::vector<KCMStoryChange> changes;
		// The Source side is the row's SOURCE STORY (KCMStoryRow::fSourceStoryUID): the same uid for a UID
		//   pair, the Source's own story for a row paired by the place of its first frame.
		if (CompareOneStory(UIDRef(targetDB, storyUID),
							UIDRef(sourceDB, row->fSourceStoryUID),
							((row->fKinds & kKCMStoryKindByPosition) != 0) ? kTrue : kFalse, changes))
		{
			// **WRITTEN EVEN WHEN NOTHING DIFFERS**: that somebody looked is what fTextCompared says.
			// The "=" records carried over from the list before (KCMStoryList::Build, Refresh Comparison) are placed from this
			// comparison as RunOne's are: a row built fresh has none.
			AttachComparison(i, targetDB, storyUID, changes, kTrue);
			total += static_cast<int32>(changes.size());
		}
		// (else: the row keeps its place and loses its detail)
		++done;

		// A cancel is tested at a safe point, with a story fully read: WasCancelled pumps events.
		// **Not after the LAST one** -- with nothing left to do there is nothing to interrupt, and a
		//   press landing just after the final story would throw away a comparison that is already
		//   complete (the same rule as the raster loop in KCMCore.cpp).
		if (done < pairedCount && progress.WasCancelled())
		{
			if (outCancelled != nil)
				*outCancelled = kTrue;
			return total;
		}
	}

	KCMStoryList::LetStrayFoci();	// the sweep (KCMStoryList.h says why)
	return total;
}

//----------------------------------------------------------------------------------------
// RunOne
//----------------------------------------------------------------------------------------

uint32 KCMStoryDiffRun::CountForKind(const UIDRef& story, int32 kind)
{
	// The header carries the measurement and the reasoning. Here: words are measured by the text
	// counter, anything laid OVER the words by the aggregate - because the text counter does not
	// move for a ruby or a kenten, so it can tell neither that one went in nor that it came out.
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil)
		return 0;
	return static_cast<uint32>((kind == kKCMStoryAttrNone) ? model->GetTextChangeCount() : model->GetChangeCount());
}

uint32 KCMStoryDiffRun::CountAllChanges(const UIDRef& story)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	return (model != nil) ? model->GetChangeCount() : 0;
}

int32 KCMStoryDiffRun::RunOne(IDataBase* targetDB, IDataBase* sourceDB, int32 rowIndex)
{
	if (targetDB == nil || sourceDB == nil)
		return -1;

	// **THE UID IS COPIED OUT BEFORE ANYTHING ELSE HAPPENS.** GetRow hands back a pointer into
	//   the list, and the work below writes to that same list -- so the answer to "which story is
	//   this row about" is taken while the question is still safe to ask.
	const KCMStoryRow* row = KCMStoryList::GetRow(rowIndex);
	// The menu greys the item for a row with no partner, so that half of this is the second line of defence.
	if (!IsPairedRow(row))
		return -1;
	const UID storyUID = row->fStoryUID;
	const UID sourceStoryUID = row->fSourceStoryUID;	// the row's Source story (another uid for a row paired by place)
	const bool16 pairedByPlace = ((row->fKinds & kKCMStoryKindByPosition) != 0) ? kTrue : kFalse;
	row = nil;

	// See the header: the same guard Run takes, and the only one on this path.
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

	// **THE ROW ITSELF IS RE-READ FIRST.** The row quotes the story's opening words, and points
	//   at the frame a click scrolls to -- both read from the document when the comparison ran.
	//   Refreshing only the CHILDREN left the row quoting a sentence the reader had just
	//   rewritten, which is the panel showing two different moments on one line.
	//   @warning it runs inside the guard for the same reason everything else here does. A story
	//     that has been deleted since simply leaves the row as it was -- the read refuses rather
	//     than half-writing it, and the diff below is what reports the failure.
	KCMStoryList::RefreshRowFromDocument(rowIndex, targetDB);

	std::vector<KCMStoryChange> changes;
	const bool16 compared = CompareOneStory(UIDRef(targetDB, storyUID), UIDRef(sourceDB, sourceStoryUID), pairedByPlace, changes);

	// **WRITTEN EITHER WAY, INCLUDING EMPTY.** What stands under the row after a refresh is what
	//   the documents say now, and "nothing" is a perfectly good thing for them to say (KCMStoryRow::fTextCompared).
	AttachComparison(rowIndex, targetDB, storyUID, changes, compared);
	// THE SWEEP (KCMStoryList.h says why): a focus no record holds any more, let go.
	KCMStoryList::LetStrayFoci();

	return compared ? static_cast<int32>(changes.size()) : -1;
}

// End, KCMStoryDiffRun.cpp.
