//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryDiffRun.h for what this is for and what it deliberately does not do.
//
//  The text is read straight from the text model (KCMTextRead); the helpers that cut and join
//  it (Join / Slice, and the pure functions in KCMParaText.h) came from KohakuTest's KTStoryDiff
//  and work the same way. The comparison itself is NOT a straight port -- it does two things KT
//  had no need of:
//
//    1. IT WORKS OUT THE OLDER DOCUMENT'S POSITIONS TOO. KT selected in the front document
//       only. Here a click moves both windows, so the source-side TextIndex has to exist.
//       Nothing extra is diffed for it: KCMTextDiff::Change already carries the a-side range,
//       and the same paragraph-start arithmetic runs over the older side's paragraphs.
//
//    2. IT CARRIES BOTH SIDES' WORDS. KT reported the newer text and nothing else; here the row
//       shows the newer version and the panel's message area shows the older one, so a reader can
//       see what a passage used to say without leaving the row.
//       ⚠**THE ROW IS THE NEWER VERSION FOR EVERY KIND OF CHANGE, INCLUDING A DELETION**
//       (2026-09-01, user's decision). Deletions used to show the older text in the row - the
//       words that had gone - which made them the one row in the list showing the opposite
//       document from every other, and read as though the panel had the two files the wrong way
//       round. What was deleted is in the message area, where every other row's other side is.
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"		// SaveRestoreModifiedState - see Run()
#include "ITextModel.h"
#include "TextIterator.h"		// AppendToStringAndIncrement - the Source story's raw text, read once
#include "WideString.h"

// General includes:
#include "PMString.h"
#include "UIDRef.h"

#include <algorithm>	// std::stable_sort - the ruby children are found by a second walk
#include <string>
#include <vector>

// Project includes:
#include "KCMParaText.h"	// KCMParaAttrs and the pure functions over paragraphs (Join / IndexInStory / SplitRunAtPlaces / SpansDiffer)
#include "KCMParaPairing.h"	// which paragraph goes with which, when a run holds more on one side (the import pours by it too)
#include "KCMProgressBar.h"	// KCMDeferredProgressBar - the progress bar and Cancel of Run, shown after kKCMProgressBarDelayMs
#include "KCMStoryDiffRun.h"
#include "KCMCore.h"			// KCMArmedTargetDB / KCMIsDocDBOpen - which document a replaced change is measured against
#include "KCMSourceCache.h"	// the Source side, read once per origin instead of once per press
#include "KCMTableShape.h"	// KCMReadTableShapes / KCMTableShapesDiffer - the Table row (2026-09-19 night)
#include "KCMTableSnippet.h"	// KCMReadTableIdsInStory / KCMExportStoryInx - the Source's tables, by their own ids
#include "KCMMemXferBytes.h"	// ...and the buffer that export writes into
#include "KCMResourceBytes.h"
// (⛔KCMStoryRestore.h and KCMStorySnapshot.h were included here for the restore, and went with it
//  on 2026-09-21 - KCMStorySnapshot as a whole file, having had no other reader.)
#include "KCMTextRead.h"		// the reader: paragraphs, their positions and their attributes, straight from the text model
#include "KCMStoryList.h"
#include "KCMStoryStamp.h"	// kKCMStoryKindAdded - which rows have no partner to compare against
#include "KCMTextDiff.h"
#include "KCMOversetPoint.h"	// KCMIsTextIndexOverset - is this change's text composed anywhere

namespace
{

/** How many code points of a change to keep for the row, INCLUDING the context on either side.
	The cell ellipsizes in the middle, so this only has to be short enough not to carry a paragraph
	around in memory per change. */
const int32 kExcerptCodePoints = 60;

/** How many code points to keep on EACH SIDE of a change -- the same context a KBS hit row
	shows. Narrow on purpose: two of these plus the change itself has to stay under
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
//
// ⚠2026-09-03: the XML route is gone. Until then the story was exported as a snippet and parsed
//   back out of the XML, every position was COUNTED and then checked against the document
//   (ComputedLength / LengthAgrees / KCMStoryCellBases, five ways to refuse a story), and the
//   parallel run that measured the migration stood here. Nothing counts any more, so none of that
//   has anything to reconcile. The record of what it was and why it went:
//   docs/superpowers/specs/2026-08-31-kcm-story-direct-read-design.md.

/* MarkUpBreaks
   Turns the break characters into the marks InDesign itself draws with Show Hidden Characters on,
   so that a row showing text which crosses a paragraph end does not show a gap where the break was.

   DISPLAY ONLY -- nothing measured or selected ever goes through here. The marks are wider
   nothing, so a string that has been through this no longer matches the text it came from.

   Same two marks and the same reasoning as KBS's KBSResultModel::MarkUpBreaksForDisplay: a pilcrow
   for a paragraph end, a return arrow for a forced line break. Written on UTF-8 here rather than on
   a PMString's UTF-16 buffer, because at this point in the comparison the text is still the XML's
   own bytes - both marks are outside ASCII, hence the escapes.
*/
std::string MarkUpBreaks(const std::string& utf8)
{
	// U+FFFC OBJECT REPLACEMENT CHARACTER - what the text model holds where an anchored page
	// item stands (measured 2026-08-23; see AnchoredItemTagLen in KCMParaText.h).
	static const char kAnchorChar[] = "\xEF\xBF\xBC";

	// ⚠**THREE FINDS, NOT ONE** - the line below claimed "one scan" until 2026-09-08. It is left as
	//   it stands: Slice has already cut the text to kExcerptCodePoints before this is reached, so
	//   what that sentence was really defending is the ALLOCATION, and THAT part is true.
	if (utf8.find('\n') == std::string::npos && utf8.find('\r') == std::string::npos
		&& utf8.find(kAnchorChar) == std::string::npos)
		return utf8;		// nothing to mark up: no allocation, no copy

	std::string out;
	out.reserve(utf8.size() + 8);
	for (size_t i = 0; i < utf8.size(); ++i)
	{
		if (utf8[i] == '\n')
			out += "\xC2\xB6";			// U+00B6 PILCROW - a paragraph end (what Join puts between paragraphs)
		else if (utf8[i] == '\r')
			out += "\xE2\x86\xB5";		// U+21B5 DOWNWARDS ARROW WITH CORNER LEFTWARDS - a forced line break
		else if (utf8.compare(i, 3, kAnchorChar) == 0)
		{
			// **AN ANCHORED OBJECT GETS A SIGN OF ITS OWN.** Left as it is, U+FFFC draws nothing at
			//   all -- the row would show a GAP where the reader added a picture, which reads as
			//   "nothing happened".
			// The kind column still says "+" or "-": this replaces the CHARACTER, not the kind, so
			//   what the anchor did is still there to read.
			// AND IT IS DECIDED HERE, ONCE. The excerpt reaches the reader in two places -- the Story
			//   Edits row and the panel's message area -- and a sign chosen at each of them would be
			//   two answers to one question ([[one-question-one-place]], the fault behind seven of
			//   this plug-in's bugs). Everything shown goes through here.
			out += "\xE2\x9A\x93";		// U+2693 ANCHOR
			i += 2;					// the loop's ++i steps over the third byte
		}
		else
			out += utf8[i];
	}
	return out;
}

/* Slice
   A piece of UTF-8 text named in CODE POINTS, cut on code point boundaries so what comes out is
   still valid UTF-8. byteOffsets is what ToCodePoints filled in for this same string.

   **IT TAKES THE SURROUNDING WORDS TOO.** A change on its own reads as a fragment --
   "awake" says nothing about where it is -- so the row shows what stands on either side of it,
   the way a KBS hit row shows its context. An ellipsis marks each end that was cut, so a
   fragment is never mistaken for the whole of something.

   **IT HANDS BACK THREE PIECES, NOT ONE**, so that the row can draw the changed characters at
   full strength and fade the context around them, the way a KBS hit row draws its match. The
   split has to be made HERE: by the time the row is drawn, the only thing that knows which
   characters were the change is this function -- the boundary between the context and the
   change is a code point index into a string that has already been cut at both ends and had its
   break characters replaced. Handing over one string plus an offset would ask the panel to count
   code points in a PMString, whose own index is UTF-16.

   @param from/count name the CHANGE, in code points, within text.
   @param context how many code points to keep on each side. 0 = the change alone.
   @param outPre [out] what stands before the change, with a leading ellipsis when the text was
      cut there. Empty when the change begins the text.
   @param outMid [out] the changed characters themselves -- what the row draws at full strength.
      Empty for the side of a change that has nothing there (a deletion seen from the newer side).
   @param outPost [out] what stands after it, with a trailing ellipsis on the same terms as outPre.
*/
void Slice(const std::string& text, const std::vector<int32>& byteOffsets,
		   int32 from, int32 count, int32 context,
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
	int32 first = from - context;
	if (first < 0)
		first = 0;
	int32 midFrom = (from < total) ? from : total;
	int32 midTo = from + count;			// exclusive
	if (midTo > total)
		midTo = total;
	if (midTo < midFrom)
		midTo = midFrom;
	int32 last = from + count + context;	// exclusive
	if (last > total)
		last = total;
	if (last < midTo)
		last = midTo;
	if (last <= first)
		return;

	// (⛔**THE CONTEXT WAS CUT AT EVERY PARAGRAPH END FOR ONE BUILD, ON 2026-09-22, AND THAT WENT THE
	//   SAME DAY.** The reader asked for it after seeing three rows each draw the same "A¶B¶C", then
	//   looked at the result and asked for the breaks back - the words on both sides of a break read
	//   better than the change alone. What must NOT be crossed is a PLACE - the next cell, the next
	//   footnote, the words of somebody else's row - and that
	//   cut is made before this function is ever reached, where the run itself is split
	//   (SplitRunAtPlaces, KCMParaText.h: "a cell is a place"). So a break inside one place is
	//   context like any other character, and this function has nothing to say about it.)

	// Long enough to fill the cell and no longer. The cell ellipsizes for itself.
	//
	// Counted on the WHOLE excerpt, exactly as it was when this returned one string: the cut
	//   lands at the same code point it always did. It can fall inside the change itself when a
	//   single change is longer than the whole allowance -- which is the honest outcome, since
	//   the change is what the excerpt is for. Both boundaries are pulled back with it so that
	//   no piece can end up naming a range outside the one being kept.
	//   @warning **the trailing ellipsis this forces was once never drawn.** The old code said
	//     "last = total; // force the trailing ellipsis", and the test just below it is
	//     "if (last < total)" -- so setting last TO total turned the ellipsis OFF, and turned it
	//     off even for an excerpt that would have carried one anyway. A flag says it instead,
	//     because a flag cannot be read as its own opposite.
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

	// The byte where a code point begins - or the end of the text for the one-past-the-last index.
	// byteOffsets holds one entry per code point, so the last boundary has no entry of its own.
	struct ByteAt
	{
		const std::string& fText;
		const std::vector<int32>& fOffsets;
		int32 fTotal;
		int32 operator()(int32 cp) const
		{
			return (cp < fTotal) ? fOffsets[cp] : static_cast<int32>(fText.size());
		}
	};
	const ByteAt byteAt = { text, byteOffsets, total };

	const int32 beginByte = byteAt(first);
	const int32 midFromByte = byteAt(midFrom);
	const int32 midToByte = byteAt(midTo);
	const int32 endByte = byteAt(last);
	if (endByte <= beginByte)
		return;

	// Each piece goes through MarkUpBreaks on its own. The marks replace one break character
	//   each, so nothing straddles a boundary and the three marked-up pieces read exactly as
	//   the one marked-up string used to.
	outPre = MarkUpBreaks(text.substr(beginByte, midFromByte - beginByte));
	outMid = MarkUpBreaks(text.substr(midFromByte, midToByte - midFromByte));
	outPost = MarkUpBreaks(text.substr(midToByte, endByte - midToByte));

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

/* RunSide
   One run of paragraphs on ONE of the two sides, and the one thing every change asks of it:
   where an offset into the run's joined text lands in the document.

   **IT REPLACED A BARE `base`, AND THAT IS THE WHOLE OF THE FIX.** `base + offset` is right
   only while the joined text and the document agree about how far apart two paragraphs are,
   and a table makes them disagree -- its own character and its row terminators sit exactly at
   a paragraph boundary, and its cells stand past the end of the body. A change covering two
   adjacent paragraphs with one of those between them came out short, silently, and no length
   check could see it. The run is asked instead of counted on: every paragraph's start is a
   TextIndex the reader took from the walk, and the rule that turns an offset into one lives
   beside JoinParagraphs in KCMParaText.h, where the two ends of the convention can be
   measured against each other.
*/
struct RunSide
{
	const std::vector<std::string>*		fParagraphs;
	const std::vector<int32>*			fStarts;	// one document position per paragraph
	const std::vector<KCMParaAttrs>*	fAttrs;		// only fUncountedAt is read - see Index
	int32								fStart;		// first paragraph of the run
	int32								fCount;		// how many it covers
	int32								fBase;		// where the run begins, as a TextIndex
	std::vector<int32>					fObjects;	// the run's tables and note references, in the joined text's count

	RunSide(const std::vector<std::string>& paragraphs, const std::vector<int32>& starts,
			const std::vector<KCMParaAttrs>& attrs, int32 start, int32 count, int32 base)
		: fParagraphs(&paragraphs), fStarts(&starts), fAttrs(&attrs),
		  fStart(start), fCount(count), fBase(base)
	{
		KCMParaText::RunObjectOffsets(paragraphs, attrs, start, count, fObjects);
	}

	/** Where an offset into the run's joined TEXT stands in the DOCUMENT.
		⚠**THE ATTRIBUTES ARE CARRIED FOR ONE FIELD** (fUncountedAt), and it is what makes this a
		 crossing between two counts rather than a lookup: a table standing inside a paragraph is
		 counted by the document and not by the text. See KCMParaText::ModelOffsetInParagraph. */
	int32 Index(int32 joinedOffset) const
	{
		return KCMParaText::IndexInStory(*fParagraphs, *fStarts, *fAttrs,
											fStart, fCount, fBase, joinedOffset);
	}
};

/* SetDocumentText
   One field of a change, filled from text that came out of a document.

   **THE TWO CALLS BELONG TOGETHER AND WERE WRITTEN APART.** Text out of a document is not a
   translation key: without SetTranslatable(kFalse) it can be looked up in the string tables and
   come back as something else entirely (memory menu-string-translation-traps). Every field here
   holds document text -- the context pieces most of all, being the ones most likely to be a
   short common word a table has an entry for -- so the two calls are one act, and were a column
   of six SetUTF8String followed by a column of six SetTranslatable, then eight and eight. A
   field added to either column and not the other is a fault nothing would report.
*/
void SetDocumentText(PMString& out, const std::string& utf8)
{
	out.SetUTF8String(utf8);
	out.SetTranslatable(kFalse);
}

/* SetExcerptPieces
   One side of a change, cut into its three pieces and stored in the row's fields.

   **CUTTING AND STORING ARE ONE ACT.** They stood as ten lines written out twice - once for a text
   change, once for an attribute one - which is the argument SetDocumentText already made one size
   down: apart, a piece added to the cut and forgotten in the store is a fault nothing reports.
   @warning the CONTEXT pieces are document text too, and are the likeliest of the three to be a
    short common word the string tables have an entry for.

   @param from/count name the CHANGE, in code points, within text.
*/
void SetExcerptPieces(PMString& outPre, PMString& outMid, PMString& outPost,
					  const std::string& text, const std::vector<int32>& bytes,
					  int32 from, int32 count)
{
	std::string pre, mid, post;
	Slice(text, bytes, from, count, kContextCodePoints, pre, mid, post);

	SetDocumentText(outPre, pre);
	SetDocumentText(outMid, mid);
	SetDocumentText(outPost, post);
}

/* (⛔SliceHoldsObjectCharacter and MarkWriteBlocks stood here and went on 2026-09-21 with the
   restore. Together they answered "why must this change not be written back" - the two sides
   standing in different cells or footnotes, or a range holding a table, a note or an anchored
   object, which text commands bring back as a bare character without its object. The answer was
   put on every change of a run and read by the menu, which greyed its item on it.
   ★The MEASUREMENT that made them necessary is kept in docs/ai-notes/kcm-restore-retired-2026-09-21.md:
   a deleted table's cell "restored" into a position nothing could see, and an anchored object that
   came back as U+FFFC alone.) */

/* Add
   Builds one change and appends it. Kept in one place so that the two callers below - a run that
   was narrowed down to characters, and one that was not - cannot describe the same thing in two
   different ways.

   @param tFrom/tCount, sFrom/sCount are offsets WITHIN the joined run, in code points.
   @param objectsBefore how many of the run's objects stand before this change on both sides
          (KCMParaText::KCMObjectPiece), or -1 when nothing is known - see AddCutAtObjects.
*/
void Add(std::vector<KCMStoryChange>& out,
		 const std::string& targetText, const std::vector<int32>& targetBytes,
		 const RunSide& tRun, int32 tFrom, int32 tCount,
		 const std::string& sourceText, const std::vector<int32>& sourceBytes,
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
	// ★★**AND AN END IS NOT A START** (2026-09-17, the import matrix's G2). Index answers for a START -
	//   the character at an offset stands AFTER a table standing there - so asking it for an END put
	//   a range that stops right before a table one position wide, over the table's anchor: the change
	//   then "held a table" and could not be taken in, though its words touched nothing but letters.
	//   An end is "just past the last character"; an empty range ends where it starts.
	// ★★**AND AN EMPTY RANGE NEXT TO A TABLE HAS A SIDE** (the same day, G1): "after everything standing
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
	//   @warning **what this used to say, and why it was wrong:** "an insertion has nothing in
	//     the older document to point at". That is true of CHARACTERS and false of the PLACE.
	//     The old wording folded two different questions into one flag
	//     ([[one-question-one-place]]), and the answer to the second one ("is there anything to
	//     select over there") dragged the first one down with it, so the older window did not
	//     move at all.
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
	// ★★★THE ROW IS ALWAYS THE NEWER VERSION. ONE RULE, NO EXCEPTIONS (2026-09-01, user's
	//   decision: "if that had become the spec, I am changing the spec").
	//
	//   ⚠**WHAT WAS HERE BEFORE, AND WHY IT WENT.** A DELETION used to show the OLDER side on the
	//   row - the words that had gone - because "the newer side has nothing there to show". That
	//   was decided in this file on 2026-08-20 and **was never asked for**; it made the deletion
	//   the one row in the list that shows the opposite document from every other row, and a
	//   reader watching the row and the message area swap places called it exactly that: the panel
	//   looked as though it had the two documents the wrong way round.
	//   ⇒ The old reasoning was not wrong about the cost - a deletion's row really does lose its
	//     highlighted middle - it was wrong about the price. **Consistency across every row is
	//     worth more than one row being self-contained**, and what was deleted is not lost: it is
	//     in the message area, which is where every other row's other side already is, and the
	//     Change column's '-' says which kind of row it is.
	//
	//   Whichever side the row shows, the OTHER one goes to fOtherText -- see KCMStoryList.h for
	//   why it is named that rather than "old".
	SetExcerptPieces(change.fTextPre, change.fText, change.fTextPost,
					 targetText, targetBytes, tFrom, tCount);
	SetExcerptPieces(change.fOtherTextPre, change.fOtherText, change.fOtherTextPost,
					 sourceText, sourceBytes, sFrom, sCount);

	out.push_back(change);
}

/* AddCutAtObjects
   Add, once for each piece of the change that the tables and note references standing in it leave
   (2026-09-17, the import matrix's G2).

   ★★**A CHANGE ACROSS A TABLE IS TWO CHANGES, ONE ON EACH SIDE OF IT.** Deleting 文表 around a table
   came out as one change whose range held the table's anchor, and it could not be taken in: writing it
   would have deleted the table. Cut where the table stands, each piece holds letters alone and the
   table stays - and the rows say what happened, one on each side of it.
   ★The objects are paired in order across the two versions (KCMParaText::CutChangeAtObjects); when
   they cannot be - a table was added or moved - the change is added whole, as it always was.
*/
void AddCutAtObjects(std::vector<KCMStoryChange>& out,
					 const std::string& targetText, const std::vector<int32>& targetBytes,
					 const RunSide& tRun, int32 tFrom, int32 tCount,
					 const std::string& sourceText, const std::vector<int32>& sourceBytes,
					 const RunSide& sRun, int32 sFrom, int32 sCount)
{
	std::vector<KCMParaText::KCMObjectPiece> pieces;
	KCMParaText::CutChangeAtObjects(sRun.fObjects, tRun.fObjects, sFrom, sCount, tFrom, tCount, pieces);
	for (size_t i = 0; i < pieces.size(); ++i)
	{
		const KCMParaText::KCMObjectPiece& piece = pieces[i];
		Add(out, targetText, targetBytes, tRun, piece.fBStart, piece.fBCount,
			sourceText, sourceBytes, sRun, piece.fAStart, piece.fACount, piece.fObjectsBefore);
	}
}

/* AddWholeParagraphs
   One change per paragraph of a run that stands on ONE side only - paragraphs added or removed whole
   (2026-09-17 afternoon, the user's rule and request: "+ and - per paragraph").

   ★★★**THE RANGES CARRY A PARAGRAPH BREAK.** Measured the same day: a paragraph added in the file and
   taken in ran into the next paragraph, and one removed left an empty paragraph behind - the joined
   run's words held no break to write or take out.
   ★**WHICH BREAK: THE ONE BEFORE THE PARAGRAPH**, cut at returns - [return of the paragraph before,
   return of this one). Taken in, that is "\rNEW" right before the return of the paragraph it follows:
   what pressing Return at its end puts in (the new paragraph starts in that paragraph's style, and the
   restore then gives it the next style - KCMParagraphStyle.h). Taken out, it joins the paragraph before
   with the removed one's return, and a join keeps the UPPER paragraph's style (measured).
   ⚠**A PLACE'S FIRST PARAGRAPH HAS NO RETURN BEFORE IT IN ITS PLACE**, so it is cut at its own start
   instead: [its start, just past its return), before the paragraph that follows.
   ⚠**ONE CARET FOR ALL OF A RUN'S PARAGRAPHS ON THE SIDE THAT LACKS THEM.** Taken in one by one they
   all go in right before the same return, each press writing in front of the ones already in - so taken
   in from the last to the first they come out in their order. Out of order, the panel asks first
   (fAfterNewParagraph). ⚠A bulk run used to do that walk (and chain the run's next styles afterwards);
   the bulk items went on 2026-09-20 and the order is the reader's own.
   (⛔It also said OUT whether the paragraphs could be PLACED at all - whether the paragraph they
   follow, or precede, stands in the same place on both sides. Only a write back needed to know,
   and that went on 2026-09-21.) */
void AddWholeParagraphs(std::vector<KCMStoryChange>& out, const KCMTextDiff::Change& run,
						const std::vector<std::string>& sourceParas, const std::vector<int32>& sourceStarts,
						const std::vector<KCMParaAttrs>& sourceAttrs,
						const std::vector<std::string>& targetParas, const std::vector<int32>& targetStarts,
						const std::vector<KCMParaAttrs>& targetAttrs)
{
	// The source holds them and the target lacks them - taking in ADDS them - or the other way round.
	const bool16 adds = (run.aCount > 0) ? kTrue : kFalse;
	const int32 first = adds ? run.aStart : run.bStart;
	const int32 count = adds ? run.aCount : run.bCount;
	const std::vector<KCMParaAttrs>& ownAttrs = adds ? sourceAttrs : targetAttrs;

	const int32 sourceSize = static_cast<int32>(sourceParas.size());
	const int32 targetSize = static_cast<int32>(targetParas.size());
	const bool16 afterReturn = (run.aStart > 0 && run.bStart > 0
								&& run.aStart - 1 < sourceSize && run.bStart - 1 < targetSize
								&& KCMParaText::ParagraphsSharePlace(ownAttrs, first, sourceAttrs, run.aStart - 1)
								&& KCMParaText::ParagraphsSharePlace(ownAttrs, first, targetAttrs, run.bStart - 1))
							   ? kTrue : kFalse;
	const int32 nextA = run.aStart + run.aCount;
	const int32 nextB = run.bStart + run.bCount;
	const bool16 beforeNext = (!afterReturn && nextA < sourceSize && nextB < targetSize
							   && KCMParaText::ParagraphsSharePlace(ownAttrs, first, sourceAttrs, nextA)
							   && KCMParaText::ParagraphsSharePlace(ownAttrs, first, targetAttrs, nextB))
							  ? kTrue : kFalse;
	// (⛔"...and can they be placed at all" was reported to the caller here until 2026-09-21. The two
	//  answers above are still what decides WHERE the caret of a one-sided paragraph goes.)

	for (int32 k = 0; k < count; ++k)
	{
		const int32 p = first + k;
		KCMStoryChange change;
		change.fWhat = KCMStoryChange::kText;
		change.fWholeParagraph = kTrue;

		// ★**AND WHICH END OF THE RANGE THE BREAK IS AT** (2026-09-19, the user: the mark reached the end
		//   of the paragraph above). The ranges below are cut for the WRITE; the facade cuts the break
		//   back off them for everything the reader sees (KCMStoryList.h, KCMShownSpan).
		change.fBreakAt = afterReturn ? kKCMBreakLeads : (beforeNext ? kKCMBreakTrails : kKCMBreakNone);

		// The side that holds the paragraph: its range. The side that lacks it: the caret.
		int32 ownStart = 0;
		int32 ownEnd = 0;
		int32 caret = 0;
		const std::vector<std::string>& ownParas = adds ? sourceParas : targetParas;
		const std::vector<int32>& ownStarts = adds ? sourceStarts : targetStarts;
		const std::vector<std::string>& otherParas = adds ? targetParas : sourceParas;
		const std::vector<int32>& otherStarts = adds ? targetStarts : sourceStarts;
		const std::vector<KCMParaAttrs>& otherAttrs = adds ? targetAttrs : sourceAttrs;
		const int32 otherBefore = adds ? run.bStart - 1 : run.aStart - 1;
		const int32 otherNext = adds ? nextB : nextA;
		std::string otherText;			// the paragraph the caret stands in, for the row's context
		int32 otherFrom = 0;
		if (afterReturn)
		{
			ownStart = KCMParaText::ParagraphReturn(ownParas, ownStarts, ownAttrs, p - 1);
			ownEnd = KCMParaText::ParagraphReturn(ownParas, ownStarts, ownAttrs, p);
			caret = KCMParaText::ParagraphReturn(otherParas, otherStarts, otherAttrs, otherBefore);
			otherText = otherParas[static_cast<size_t>(otherBefore)];
			otherFrom = KCMParaText::CountCodePoints(otherText);
		}
		else if (beforeNext)
		{
			ownStart = KCMParaText::ParagraphLineStart(ownStarts, ownAttrs, p);
			ownEnd = KCMParaText::ParagraphReturn(ownParas, ownStarts, ownAttrs, p) + 1;
			caret = KCMParaText::ParagraphLineStart(otherStarts, otherAttrs, otherNext);
			otherText = otherParas[static_cast<size_t>(otherNext)];
			otherFrom = 0;
		}
		else
		{
			// Nowhere to put it: shown all the same (it was marked as not writable until 2026-09-21).
			ownStart = ownStarts[static_cast<size_t>(p)];
			ownEnd = KCMParaText::ParagraphReturn(ownParas, ownStarts, ownAttrs, p);
			caret = ownStart;
		}

		const std::string& ownText = ownParas[static_cast<size_t>(p)];
		std::vector<int32> ownBytes;
		std::vector<int32> otherBytes;
		KCMTextDiff::ToCodePoints(ownText, nil, &ownBytes);
		KCMTextDiff::ToCodePoints(otherText, nil, &otherBytes);
		const int32 ownLength = KCMParaText::CountCodePoints(ownText);

		if (adds)
		{
			change.fKind = KCMStoryChange::kDelete;		// the target lacks it (Add's naming: tCount == 0)
			// (⛔The second "+" of "+ +" was marked here until 2026-09-21, so that the panel could ask
			//  before one was taken in ahead of the other.)
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

   ★**WHY THE BYTE TABLE IS LAZY.** Most paragraphs carrying a mark carry the SAME mark in both
   versions, so the table is never wanted at all; building it up front would buy a walk of every
   marked paragraph in the document. Building it inside each helper - which is what happened until
   2026-09-04 - walked the same paragraph up to THREE times per attribute (the filter, then both
   sides of the comparison), six for a paragraph carrying ruby and kenten both. Asked for here, it
   is made once per paragraph per comparison, or not at all.
   ⚠It also asked ToCodePoints for the code points themselves and dropped them; that argument is
    optional since the same day, so nothing is built to be discarded any more.

   ★**WHY THE BASE AND THE ATTRIBUTES RIDE WITH IT.** They travelled as separate arguments to
   AddAttrChange, which had thirteen of them, and the three are one paragraph seen from one side.
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
		 for. It read `fBase + offset` until 2026-09-04, and the midtable pair's one reported
		 change then selected the table's anchor rather than the character after it. */
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
   ★LINE 0 IS CUT THE WAY EVERY ROW IS - Slice, with its context and its ellipses - around its one
    bar. The lines above are whole pieces of a warichu or a tate-chu-yoko and are not shortened
    here: the cell ellipsizes what does not fit.
   @param present whether this side carries the mark (its value is not empty). */
void BuildLayers(ParaSide& side, bool16 changedIsWarichu, int32 start, int32 len, bool16 present,
				 KCMStoryLayers& out)
{
	out = KCMStoryLayers();

	const std::vector<int32>& bytes = side.Bytes();
	KCMParaText::LayerPlan plan;
	KCMParaText::PlanLayers(changedIsWarichu, side.fAttrs.fWarichu, side.fAttrs.fTcy,
							start, len, present, static_cast<int32>(bytes.size()), plan);
	if (plan.fCount < 2)
		return;

	out.fCount = plan.fCount;
	out.fChanged = plan.fChanged;
	out.fShowsText = plan.fShowsText;

	// ★The text line only when it is drawn (a change inside another layer leaves it out - the user's
	//   call, KCMStoryLayers::fShowsText); its two pieces stay empty otherwise.
	if (plan.fShowsText)
	{
		std::string pre, mid, post;
		Slice(side.fText, bytes, plan.fHole.fFrom, plan.fHole.fTo - plan.fHole.fFrom, kContextCodePoints,
			  pre, mid, post);
		SetDocumentText(out.fBottomPre, pre);
		SetDocumentText(out.fBottomPost, post);
	}

	auto piece = [&](int32 from, int32 to) -> PMString
	{
		PMString s;
		SetDocumentText(s, MarkUpBreaks(KCMParaText::SliceCodePoints(side.fText, from, to - from)));
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
			out.fUpperPieces.push_back(bar ? PMString() : piece(plan.fUpperHoles[h].fFrom, plan.fUpperHoles[h].fTo));
			if (bar)
				out.fUpperPieces.back().SetTranslatable(kFalse);
		}
		out.fMiddleParts.push_back(piece(at, plan.fMiddle.fTo));
	}
	out.fChangedPiece = plan.fChangedPiece;
	out.fChangedPieceIsBar = plan.fChangedPieceIsBar;
}

/* AddAttrChange
   One ATTRIBUTE difference -- a ruby today -- turned into the child row that reports it.

   **THE BASE TEXT IS SHOWN FROM THE NEWER SIDE, ALWAYS** -- unlike a text change, where a
   deletion has to be shown from the older side because the newer one has nothing there. An
   attribute is different: the characters are in BOTH versions and only what sits over them
   changed, so the newer side always has something to show and there is no case to branch on.

   **IT TAKES attrKind RATHER THAN ASSUMING RUBY**, which was kept through the months kenten was
   not reported and is load-bearing again now. What an attribute's VALUE means is not the same for
   all of them -- a ruby's is a READING and a kenten's is a KIND ("BlackCircle") -- and the field they travel in is
   the same one, so whoever draws it has to be told which it is looking at. Filling that in
   here is what let the mistake be a one-line one when it happened, in the single place that
   asked the wrong question (KCMStoryJump's message area).

   @param target/source the two paragraphs, each carrying its own text, its base and its byte
    table. **THE TABLE IS MADE ONCE PER PARAGRAPH, NOT PER SPAN** - this is called once per
    DIFFERING SPAN and the paragraphs do not change between those calls, so a paragraph with four
    altered readings was walked eight times to build the same two tables. ParaSide is where that
    now happens, and it also carries the base, which used to arrive as two more arguments.
*/
void AddAttrChange(KCMStoryChange::Kind kind, KCMStoryAttrKind attrKind,
				   int32 tStart, int32 tCount, int32 sStart, int32 sCount,
				   ParaSide& target, ParaSide& source,
				   const std::string& newRuby, const std::string& oldRuby,
				   bool16 newGroup, bool16 oldGroup,
				   std::vector<KCMStoryChange>& out)
{
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

	// ⚠★★★**THE OLDER SIDE HAS CHARACTERS HERE ONLY WHILE THE TWO PARAGRAPHS SHARE THEIR TEXT.**
	//   Until 2026-09-01 that was always so - attribute changes were looked for only in paragraphs
	//   the diff had NOT reported, so one number named the same character on both sides. That day the
	//   search widened to paragraphs whose WORDS had changed too, and **this sentence was not
	//   re-read**: measured 2026-09-08, the commit that widened it (`cce0cc5`, 17 files) touched
	//   neither this comment nor the call that leans on it.
	//   ⇒ **The caller decides now.** For a span that exists on ONE side only, CompareParagraphAttr
	//     hands the other side the PARAGRAPH'S START and no characters rather than a position that
	//     would point at a different word - see its `textDiffered`.
	//   MEASURED before the fix (work/kcm-selftest/attrpos): a ruby added to 銀河 at target index 3
	//   selected 名な in the older document, because that is what stands at index 3 over there.
	change.fSourceStart = source.ModelIndex(sStart);
	change.fSourceEnd   = source.ModelIndex(sStart + sCount);

	SetExcerptPieces(change.fTextPre, change.fText, change.fTextPost,
					 target.fText, target.Bytes(), tStart, tCount);
	SetExcerptPieces(change.fOtherTextPre, change.fOtherText, change.fOtherTextPost,
					 source.fText, source.Bytes(), sStart, sCount);

	// The readings go through the same door as the base text: they are document text too.
	SetDocumentText(change.fRuby, newRuby);
	SetDocumentText(change.fOtherRuby, oldRuby);

	// ★HOW THE RUBY IS SET, carried beside the reading rather than worked out from it -- the two
	//   settings can produce identical readings, which is the whole reason the panel needed telling
	//   (KCMStoryList.h, fRubyGroup). ⚠For KENTEN both are kFalse and mean nothing: the caller has
	//   nothing else to hand over, kenten having no such distinction.
	change.fRubyGroup = newGroup;
	change.fOtherRubyGroup = oldGroup;

	// ★★WARICHU AND TATE-CHU-YOKO ARE DRAWN IN LAYERS (2026-09-16, the user's drawings), and the lines
	//   are cut HERE, where the paragraph and its spans are - each side on its own terms, since a
	//   side without the mark is drawn differently from one with it (KCMParaText::PlanLayers).
	//   ★Neither kind is written back (user's call): named here, where the change is made, so the
	//   menu hides the item instead of offering one that refuses.
	//   ★★★**AND SINCE 2026-09-20 THEY CAN ALWAYS BE PUT BACK** (the user's decision, taken when the
	//     fourth mode was retired). The rule used to be "not restorable, EXCEPT in the Import mode",
	//     where the reader had edited them in the file on purpose - and that mode was the only thing
	//     carrying the exception. Rather than let the capability go with it, the exception became the
	//     rule: a warichu or a tate-chu-yoko change is restorable wherever it stands.
	//     ⚠This reverses the "戻さない" of 2026-09-16 for these two attributes. The writes themselves
	//      have existed since 2026-09-17 (KCMApplyWarichu / KCMApplyTcy, ON/OFF only).
	if (KCMAttrKindIsLayered(attrKind))
	{
		const bool16 isWarichu = (attrKind == kKCMStoryAttrWarichu) ? kTrue : kFalse;
		BuildLayers(target, isWarichu, tStart, tCount, newRuby.empty() ? kFalse : kTrue, change.fLayers);
		BuildLayers(source, isWarichu, sStart, sCount, oldRuby.empty() ? kFalse : kTrue, change.fOtherLayers);
	}

	out.push_back(change);
}

/* CompareParagraphAttr
   One ATTRIBUTE's spans, on two paragraphs whose TEXT came out identical.

   **SPANS ARE MATCHED BY WHERE THEY START.** The text is the same on both sides, so a reading
   that stayed put keeps its position -- which makes the start the one thing that reliably
   identifies "the same ruby" across the two versions. Length is NOT part of the matching: it
   is part of what changed (琥珀 read as こ+はく against こはく is a change of length, and
   the whole point).

   ⚠**textDiffered** says the diff reported THIS pair of paragraphs as a change, so their texts are
    not the same. A span that exists on one side only then gets **no position on the other side** -
    the two branches below say why, and work/kcm-selftest/attrpos is what it was measured on.
*/
void CompareParagraphAttr(KCMStoryAttrKind attrKind,
						  const KCMAttrSpanList& sourceSpans, const KCMAttrSpanList& targetSpans,
						  ParaSide& source, ParaSide& target, bool16 textDiffered,
						  std::vector<KCMStoryChange>& out)
{
	if (!KCMParaText::SpansDiffer(sourceSpans, targetSpans))
		return;		// ★and NOTHING is built: the byte tables are asked for below or never

	size_t i = 0, j = 0;
	while (i < sourceSpans.size() || j < targetSpans.size())
	{
		const bool16 haveS = (i < sourceSpans.size()) ? kTrue : kFalse;
		const bool16 haveT = (j < targetSpans.size()) ? kTrue : kFalse;

		if (haveS && haveT && sourceSpans[i].fStart == targetSpans[j].fStart)
		{
			// ★fGroup is NOT part of "same" (2026-09-12, user's decision - KCMParaText.h's
			//   SpansDiffer says it in full): a ruby re-set from mono to group over the same
			//   reading is not reported.
			const bool16 same = (sourceSpans[i].fValue == targetSpans[j].fValue &&
								 sourceSpans[i].fLen == targetSpans[j].fLen) ? kTrue : kFalse;

			// ★★ONLY THE INNERMOST LAYER REPORTS A CHANGE OF ITS WORDS (2026-09-16, the user's call).
			//   A warichu's value is its characters, so rewriting a tate-chu-yoko inside it changes the
			//   warichu's value too - and the tate-chu-yoko's own row already says so. The same the
			//   other way round, for a warichu standing inside a tate-chu-yoko. ⚠The warichu is the
			//   outer one of two over the same range, which is what the last argument says.
			bool16 nestedOnly = kFalse;
			if (!same && KCMAttrKindIsLayered(attrKind))
			{
				const bool16 isWarichu = (attrKind == kKCMStoryAttrWarichu) ? kTrue : kFalse;
				nestedOnly = KCMParaText::OnlyNestedDiffers(
					source.fText, sourceSpans[i], isWarichu ? source.fAttrs.fTcy : source.fAttrs.fWarichu,
					target.fText, targetSpans[j], isWarichu ? target.fAttrs.fTcy : target.fAttrs.fWarichu,
					isWarichu);
			}

			if (!same && !nestedOnly)
			{
				AddAttrChange(KCMStoryChange::kReplace, attrKind,
							  targetSpans[j].fStart, targetSpans[j].fLen,
							  sourceSpans[i].fStart, sourceSpans[i].fLen,
							  target, source,
							  targetSpans[j].fValue, sourceSpans[i].fValue,
							  targetSpans[j].fGroup, sourceSpans[i].fGroup,
							  out);
			}
			++i;
			++j;
		}
		else if (haveT && (!haveS || targetSpans[j].fStart < sourceSpans[i].fStart))
		{
			// Ruby where there was none.
			// ⚠**THE OLDER SIDE KEEPS THIS POSITION ONLY WHILE THE TEXT IS SHARED.** Where the words
			//   changed as well, the same number names a different word over there, so the older side is
			//   given the paragraph's start and no characters - it has a PLACE but nothing to select,
			//   which is the shape a text insertion already uses (KCMStoryList.h, fSourceStart).
			const int32 sStart = textDiffered ? 0 : targetSpans[j].fStart;
			const int32 sLen   = textDiffered ? 0 : targetSpans[j].fLen;
			AddAttrChange(KCMStoryChange::kInsert, attrKind,
						  targetSpans[j].fStart, targetSpans[j].fLen,
						  sStart, sLen,
						  target, source,
						  targetSpans[j].fValue, std::string(),
						  // ⚠**THE OLDER SIDE IS kFalse BECAUSE IT HAS NO RUBY**, not because the
						  //   ruby it does not have was mono. What says so is the empty reading
						  //   beside it, and the panel reads that, never this (KCMStoryList.h).
						  targetSpans[j].fGroup, kFalse,
						  out);
			++j;
		}
		else
		{
			// Ruby taken off. @warning the characters are still there -- it is the reading that is
			//   gone -- so the range is a real one on both sides, unlike a text deletion.
			// ⚠**Unless the words changed too**, and then the NEWER side gets the paragraph's start and
			//   no characters, for the reason spelt out in the branch above. The mirror image of it.
			const int32 tStart = textDiffered ? 0 : sourceSpans[i].fStart;
			const int32 tLen   = textDiffered ? 0 : sourceSpans[i].fLen;
			AddAttrChange(KCMStoryChange::kDelete, attrKind,
						  tStart, tLen,
						  sourceSpans[i].fStart, sourceSpans[i].fLen,
						  target, source,
						  std::string(), sourceSpans[i].fValue,
						  kFalse, sourceSpans[i].fGroup,		// the mirror image of the branch above
						  out);
			++i;
		}
	}
}

/* SpanBaseText
   The characters one span covers, as UTF-8 -- and an EMPTY STRING when its position cannot be
   read, which is the caller's signal to keep the span rather than judge it.

   ⚠**THE END OF THE PARAGRAPH IS A POSITION AND HAS NO ENTRY.** The byte table holds one entry per
    code point, so the boundary after the last character is named by the length of the text and by
    nothing else (Slice's ByteAt says the same one size up). Reading it as `to >= size` is what made
    a span ending a paragraph always look unreadable -- see the warning in the caller.
*/
std::string SpanBaseText(const KCMAttrSpan& span, ParaSide& side)
{
	const std::vector<int32>& bytes = side.Bytes();
	const int32 count = static_cast<int32>(bytes.size());
	const int32 from = span.fStart;
	const int32 to = span.fStart + span.fLen;
	if (from < 0 || to <= from || from >= count || to > count)
		return std::string();

	const int32 fromByte = bytes[from];
	const int32 toByte = (to < count) ? bytes[to] : static_cast<int32>(side.fText.size());
	return side.fText.substr(static_cast<size_t>(fromByte), static_cast<size_t>(toByte - fromByte));
}

/* SpansWhoseTextSurvives
   The spans whose BASE CHARACTERS are still there on the other side.

   ★★★WHY THIS EXISTS (2026-09-01, user: "the deleted one looks wrong"). Once attribute changes
   were also looked for in paragraphs the diff HAD reported, deleting a word that carried ruby
   produced two rows for one edit: the words going ("これは銀河の行です。" -> "これはの行です。")
   and, beside it, "the ruby was removed" - which is not a second thing that happened. Worse, the
   two rows show OPPOSITE SIDES of the document by design (a deletion shows the older text, an
   attribute change shows the newer), so the pair read as though the panel had swapped them over.

   ⇒ **An attribute is only a change of its own while the characters under it survive.** When they
   go, what happened is the deletion, and the deletion row already says so.

   ⚠MATCHED BY THE TEXT, NOT BY POSITION, and that is the point: after an edit the same characters
    sit at a different offset, so a positional test would call every surviving span deleted. Asking
    "do these characters appear anywhere in the other version of this paragraph" is exactly the
    question - **it does not matter where they moved to**, only whether they are gone.

   ⚠A SPAN THIS CANNOT READ IS KEPT, not dropped. A position that does not resolve to a byte range
    is a bug in the reader, and losing a real change to it would be silent; keeping it can at worst
    restore the row this function exists to remove.

   ★★★**AND THE MARK THAT MERELY MOVED IS DROPPED TOO** (2026-09-08, measured on
   work/kcm-selftest/rubyshift). The rule above -- "an attribute is a change of its own while its
   characters survive" -- was written for the mark being TAKEN OFF characters that stayed. It also
   let through the case where NEITHER changed: shorten "あいうえお銀河です。" to "あ銀河です。" and
   the same reading on the same two characters now stands at a different offset, so
   CompareParagraphAttr (which pairs spans by fStart) saw no partner for either side and reported
   the one unchanged ruby TWICE - once removed, once added. **One edit came out as three rows.**
   ⇒ A span whose partner stands elsewhere in the other version, carrying the same characters, the
     same value and the same setting, is dropped on BOTH sides: nothing about that mark changed,
     and the text row already says the words moved.
   ⚠**PAIRED OFF ONE FOR ONE**, not merely "is there one like it" - two identical readings of which
    one was deleted must still report that one. Each side consumes a partner at most once, and the
    two calls (source and target) reach the same pairing because the test is symmetrical.
*/
KCMAttrSpanList SpansWhoseTextSurvives(const KCMAttrSpanList& spans,
									   const KCMAttrSpanList& otherSpans,
									   ParaSide& own, ParaSide& other)
{
	KCMAttrSpanList kept;
	if (spans.empty())
		return kept;		// ★nothing asked of own, so its byte table is not built

	const std::string& ownPara = own.fText;
	const std::string& otherPara = other.fText;

	// Which of the other side's spans have already been claimed as "the same mark, moved". One
	//   entry per span there; nothing is built when this side has none, the early return above
	//   having left already.
	std::vector<bool16> otherClaimed(otherSpans.size(), kFalse);

	for (size_t i = 0; i < spans.size(); ++i)
	{
		// ★★★A SPAN THE OTHER SIDE ALSO HAS IS ALWAYS COMPARED (2026-09-01, user: "when the ruby
		//   changes AND the kanji under it changes, report both"). Both versions mark these
		//   characters, so **something about the marking changed or it did not** - and that is a
		//   question this filter has no business answering. Testing the base text here would drop
		//   exactly the case the user asked for: rewrite 琥珀 as 玻珀 and re-type its reading, and
		//   neither version's text is found in the other, so both spans would vanish and the panel
		//   would report the kanji alone.
		//   ⚠MATCHED THE WAY CompareParagraphAttr MATCHES - by fStart. Two different rules for
		//    "the same span" is how the filter and the comparison would come to disagree.
		bool16 paired = kFalse;
		for (size_t k = 0; k < otherSpans.size(); ++k)
		{
			if (otherSpans[k].fStart == spans[i].fStart)
			{
				paired = kTrue;
				break;
			}
		}
		if (paired)
		{
			kept.push_back(spans[i]);
			continue;
		}

		// ---- from here: a span ONE side has and the other does not ----------------------------
		// It is either "the mark was taken off characters that are still there" (a change worth
		// reporting) or "the characters went, and the mark with them" (not a change of its own -
		// **the text is what changed, the mark merely followed**, which is the user's rule:
		// the text is the subject, ruby and kenten are its attendants).
		// ⚠★★★**A SPAN ENDING A PARAGRAPH IS READABLE, AND WAS ONCE NOT.** The boundary test lives
		//   in SpanBaseText now; it read `to >= bytes.size()` until 2026-09-04, so a span ending at
		//   the last character of its paragraph always took the "unreadable" way out and was always
		//   kept: **a word carrying ruby or kenten at the end of a line, deleted outright, produced
		//   the second row this function exists to remove**, in defiance of the user's rule that the
		//   text is the subject and the marks its attendants.
		//   ★MEASURED BOTH WAYS on 2026-09-04, because a fix that simply reported less would look
		//     the same from one side: work/kcm-selftest/endruby (「これは銀河」-> 「これは」, the
		//     ruby ON THE LAST TWO CHARACTERS) went from edits=2 to edits=1, while the two controls
		//     did not move - endruby/midruby, the same deletion with text after it, stayed at 1, and
		//     kenten/del-*, a SAME-LENGTH rewrite that also loses its ruby, stayed at 2 (which is
		//     what the rule asks for: nothing was deleted, so the mark's removal is its own edit).
		//   ⚠It went unseen for as long as it did because no resource ended a marked span at a
		//    paragraph's end - the two that existed both mark a word with text after it.
		// ⚠MATCHED BY THE TEXT, NOT BY POSITION. After an edit the same characters sit at a
		//  different offset, so a positional test would call every surviving span deleted.
		const std::string text = SpanBaseText(spans[i], own);
		if (text.empty())
		{
			kept.push_back(spans[i]);		// unreadable position - see the warning above
			continue;
		}

		// ★★★**THE SAME MARK, ON THE SAME CHARACTERS, STANDING ELSEWHERE OVER THERE** - see the head
		//   of this function. Nothing about it changed; the words under it merely moved, and the text
		//   row already reports that. Dropped rather than kept, on BOTH sides, so the pair cannot
		//   come back as "removed" plus "added" (measured: work/kcm-selftest/rubyshift, three rows
		//   for one edit).
		//   ⚠CLAIMED ONE FOR ONE. Two identical readings of which one was deleted must still report
		//    that one, so a partner already spoken for is passed over rather than re-used.
		bool16 movedOnly = kFalse;
		for (size_t k = 0; k < otherSpans.size(); ++k)
		{
			if (otherClaimed[k])
				continue;
			// ★fGroup is not compared here either (2026-09-12; see SpansDiffer): a mark that
			//   merely moved is the same mark whichever way its ruby is grouped.
			if (otherSpans[k].fLen != spans[i].fLen ||
				otherSpans[k].fValue != spans[i].fValue)
				continue;
			if (SpanBaseText(otherSpans[k], other) != text)
				continue;

			otherClaimed[k] = kTrue;
			movedOnly = kTrue;
			break;
		}
		if (movedOnly)
			continue;

		if (otherPara.find(text) != std::string::npos)
		{
			kept.push_back(spans[i]);		// the characters are still there - the mark alone moved
			continue;
		}

		// ★★★THE CHARACTERS ARE NOT FOUND, AND TWO VERY DIFFERENT EDITS LOOK ALIKE HERE
		//   (2026-09-01, user: "when the kanji changes and the ruby is removed, I want two rows"):
		//     a) they were REWRITTEN - 琥珀 became 真珠 and its reading was taken off. **Two edits**,
		//        and the reader asked to see both.
		//     b) they were DELETED - 銀河 went and its reading went with it. **One** edit, which the
		//        text row already reports (the user's rule: the text is the subject, the mark
		//        follows it).
		//
		//   ⚠**TOLD APART BY THE PARAGRAPH'S LENGTH, WHICH IS AN APPROXIMATION AND IS WRITTEN DOWN
		//    AS ONE.** A deletion leaves the paragraph shorter; a same-length rewrite does not. That
		//    is exactly right for the two shapes above and WRONG for a rewrite that also shortens
		//    the paragraph (琥珀 -> 真, reading removed), which this treats as a deletion and does
		//    not report.
		//   ⇒ The honest fix is a CHARACTER-level diff of the paragraph, so that "was this range
		//     replaced or deleted" is answered rather than guessed. It is a larger piece of work and
		//     is not here yet. What is here errs toward reporting LESS, which is the direction the
		//     text-is-the-subject rule already points.
		if (KCMParaText::CountCodePoints(otherPara) >= KCMParaText::CountCodePoints(ownPara))
			kept.push_back(spans[i]);
	}

	return kept;
}

/* AddAttributeChanges
   Ruby differences in the paragraphs the text diff said were UNCHANGED.

   **THIS IS WHERE THE WHOLE FEATURE LIVES.** A ruby-only edit leaves the text identical, so
   the paragraph diff reports nothing at all and the row comes out "None" -- which is what the
   reader saw. The paragraphs the diff did NOT mention are exactly the ones that need asking
   about.
   @warning paragraphs that the diff DID report are left alone on purpose: their text changed,
     so they already have children saying so, and ruby that moved with rewritten words is not
     a separate edit the reader needs pointed out.
*/
void AddAttributeChanges(const std::vector<KCMTextDiff::Change>& paragraphChanges,
						const std::vector<std::string>& sourceParas,
						const std::vector<std::string>& targetParas,
						const std::vector<KCMParaAttrs>& sourceAttrs,
						const std::vector<KCMParaAttrs>& targetAttrs,
						const std::vector<int32>& sourceStarts,
						const std::vector<int32>& targetStarts,
						std::vector<KCMStoryChange>& out)
{
	int32 a = 0;
	int32 b = 0;

	// ★ONE PLACE DECIDES WHAT AN ATTRIBUTE COMPARISON IS, and both walks below call it. The two
	//   walks differ only in WHICH paragraphs they hand over; writing the comparison twice would be
	//   two things to keep right ([[one-question-one-place]]), and the second copy is exactly where
	//   a kind gets forgotten when a third one is added.
	// ⚠**ONE FACT, ONE NAME.** `textDiffered` says the diff reported THIS pair of paragraphs as a
	//   change, and two things follow from it: the spans have to be filtered (a mark whose characters
	//   went is not a change of its own), and **a position on one side cannot be handed to the other**
	//   (2026-09-08). It travelled as `onlyWhereTextSurvives` - the name of one of the two - until the
	//   second consequence was found.
	auto compareParagraphPair = [&](int32 ai, int32 bi, bool16 textDiffered)
	{
		if (ai < 0 || bi < 0 ||
			ai >= static_cast<int32>(sourceAttrs.size())  || bi >= static_cast<int32>(targetAttrs.size()) ||
			ai >= static_cast<int32>(sourceParas.size())  || bi >= static_cast<int32>(targetParas.size()) ||
			ai >= static_cast<int32>(sourceStarts.size()) || bi >= static_cast<int32>(targetStarts.size()))
			return;

		// The two paragraphs, each as ONE thing: its text, its base, the crossing into the
		//   document's count, and a byte table built only if something below asks for it (see
		//   ParaSide). A paragraph whose marks did not move asks for nothing.
		ParaSide source(sourceParas[ai], sourceAttrs[ai], sourceStarts[ai]);
		ParaSide target(targetParas[bi], targetAttrs[bi], targetStarts[bi]);

		// **EACH ATTRIBUTE IS COMPARED ON ITS OWN LIST**, and they cannot be merged into one pass:
		//   two sets of spans are matched by position within their OWN kind.
		// ⚠RUBY FIRST, KENTEN SECOND, and it does not matter: ChangeIsBefore re-sorts the whole list
		//   by fTargetStart afterwards. The order here is only what two changes standing at the very
		//   same character fall back on.
		// ⚠WHEN THE WORDS THEMSELVES MOVED, only the attributes whose characters survived are a
		//   change of their own - see SpansWhoseTextSurvives. In a paragraph the diff left alone the
		//   characters are the same on both sides by definition, so the filter is not run there: it
		//   would cost a walk per span to answer a question already settled.
		//
		// ★**ONE PLACE PER ATTRIBUTE, NOT ONE BLOCK PER ATTRIBUTE** (2026-09-08). The two were eight
		//   lines each and differed in nothing but which field of KCMParaAttrs they read, so a THIRD
		//   attribute meant a third copy - in the very function that decides what a comparison IS.
		auto compareAttr = [&](KCMStoryAttrKind kind,
							   const KCMAttrSpanList& sourceSpans, const KCMAttrSpanList& targetSpans)
		{
			const KCMAttrSpanList keptSource = textDiffered
				? SpansWhoseTextSurvives(sourceSpans, targetSpans, source, target)
				: sourceSpans;
			const KCMAttrSpanList keptTarget = textDiffered
				? SpansWhoseTextSurvives(targetSpans, sourceSpans, target, source)
				: targetSpans;

			CompareParagraphAttr(kind, keptSource, keptTarget, source, target, textDiffered, out);
		};

		compareAttr(kKCMStoryAttrRuby, sourceAttrs[ai].fRuby, targetAttrs[bi].fRuby);

		// ★KENTEN IS REPORTED AGAIN (2026-09-01, user's call: "if it can be found, I want to find
		//   it"). It was compared for one day in August and withdrawn, and the withdrawal was never
		//   about the comparison: the KIND it produces travelled in the same field as a ruby's
		//   READING, and the message area drew that name over the older text as though somebody could
		//   read it aloud. What answers that is fAttrKind, which every row and every change already
		//   carries. ⇒ **The mistake was one place asking the wrong question, not this call.**
		compareAttr(kKCMStoryAttrKenten, sourceAttrs[ai].fKenten, targetAttrs[bi].fKenten);

		// ★NOTE MARKERS, THROUGH THE SAME DOOR (2026-09-08, user's request). A reference is a
		//   character rather than an attribute, but the reader's question about it is the same one
		//   ruby answers - "what is standing over these words, and did it change" - and its VALUE is
		//   the number the page prints. ⇒ adding it cost two lines here because
		//   `compareAttr` had already been made to take a kind rather than to know about ruby.
		//   ⚠**AN ENDNOTE'S TEXT IS NOT COMPARED HERE**: it lives in another story
		//     (kEndnoteStoryBoss) and arrives as a story row of its own. This is the marker alone.
		compareAttr(kKCMStoryAttrFootnote, sourceAttrs[ai].fFootnote, targetAttrs[bi].fFootnote);
		compareAttr(kKCMStoryAttrEndnote,  sourceAttrs[ai].fEndnote,  targetAttrs[bi].fEndnote);

		// ★WARICHU AND TATE-CHU-YOKO (2026-09-16, user's request), through the same door once more.
		//   Their VALUE is the characters they cover (KCMParaAttrs::fWarichu), so two things fall out
		//   of the rules above with nothing added here:
		//   - rewriting the words INSIDE one keeps its span where it starts on both sides, the pair
		//     is compared, the values differ, and a row of this kind stands beside the text row;
		//   - typing or deleting one TOGETHER WITH its words drops the span (SpansWhoseTextSurvives),
		//     so only the text row says so - the text is the subject, the mark its attendant.
		//   What IS added is in CompareParagraphAttr: of two nested layers, only the inner one
		//   reports a change of the words inside it.
		compareAttr(kKCMStoryAttrWarichu,  sourceAttrs[ai].fWarichu,  targetAttrs[bi].fWarichu);
		compareAttr(kKCMStoryAttrTcy,      sourceAttrs[ai].fTcy,      targetAttrs[bi].fTcy);
	};

	// Walk the two paragraph lists side by side, stepping over each reported change. What is left
	// between them lines up one to one - that is what "unchanged" means to the diff.
	for (size_t c = 0; c <= paragraphChanges.size(); ++c)
	{
		const int32 aStop = (c < paragraphChanges.size())
							? paragraphChanges[c].aStart : static_cast<int32>(sourceParas.size());
		const int32 bStop = (c < paragraphChanges.size())
							? paragraphChanges[c].bStart : static_cast<int32>(targetParas.size());

		while (a < aStop && b < bStop)
		{
			compareParagraphPair(a, b, kFalse);
			++a;
			++b;
		}

		if (c < paragraphChanges.size())
		{
			// ⚠★★★AND THE PARAGRAPHS THE DIFF *DID* REPORT (fixed 2026-09-01, user: "this is
			//   definitely a bug"). A paragraph whose WORDS changed AND whose ruby or kenten changed in
			//   the same edit used to lose the attribute half of that entirely: this walk only ever
			//   looked BETWEEN the reported changes, and the function's old name (AddAttributeChanges)
			//   said so out loud. **A reader who rewrote a line and re-marked it in one pass was shown
			//   half of what they had done**, with nothing to say the other half existed.
			// ⚠ONLY WHEN THE TWO SIDES HOLD THE SAME NUMBER OF PARAGRAPHS HERE. Then they line up one
			//   to one and their attribute lists can be matched by position, which is what
			//   CompareParagraphAttr needs. A change that INSERTS or DELETES paragraphs has no such
			//   correspondence, and pairing them off anyway would report readings and marks moving
			//   between paragraphs that have nothing to do with each other - a wrong answer where there
			//   is currently a missing one.
			if (paragraphChanges[c].aCount == paragraphChanges[c].bCount)
			{
				for (int32 k = 0; k < paragraphChanges[c].aCount; ++k)
					compareParagraphPair(paragraphChanges[c].aStart + k,
										 paragraphChanges[c].bStart + k, kTrue);
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

/* CompareOneStory
   Fills out with everything that differs between the two versions of one story. kFalse means the
   story could not be compared at all; an empty out with kTrue means it was compared and the text
   is identical (the counters also move for formatting).
*/
/* MarkOverset
   Says of each change whether the text it names is composed anywhere the reader can see it.

   ★**ONE PASS, HERE, RATHER THAN A QUESTION THE PANEL ASKS WHILE IT DRAWS.** The list is drawn
   many times over between comparisons and the answer cannot change in between: every road that
   rebuilds a row runs the diff, and the diff runs this. (The cost is a parcel-list lookup per
   change; asked from the cell drawing it would be one per change per repaint, and worse, a
   "read-only" call in a draw path is how this SDK gets talked into recomposing.)

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
		// ★The position the reader is SHOWN, not the write's (2026-09-19): a whole paragraph's write
		//   range starts at the return of the paragraph ABOVE, and asking about that character said
		//   "composed" for a new paragraph that had itself gone over the edge.
		TextIndex from = changes[i].fTargetStart;
		TextIndex to = changes[i].fTargetEnd;
		KCMShownSpan(changes[i].fBreakAt, from, to);
		changes[i].fOverset = KCMIsTextIndexOverset(model, from);
	}
}

/* MarkPlaces
   Says of each change where its words stand - the body, a cell, a footnote - for the ID column
   (2026-09-19, the user: "Cell Text" for a change inside a cell, "Text" for an ordinary one).

   ★THE TARGET SIDE'S PARAGRAPHS, BY POSITION: the paragraph whose start is the last one at or before
     the change's target position. starts[] runs in document order, and a cell's thread stands after
     the whole body (ITableTextContent.h), so the search is one pass from the end. A removed paragraph
     has no paragraph of its own in the target; its caret stands in the paragraph next to where it was,
     which is the right place to name.
*/
void MarkPlaces(const std::vector<KCMParaAttrs>& targetAttrs, const std::vector<int32>& targetStarts,
				std::vector<KCMStoryChange>& changes)
{
	for (size_t c = 0; c < changes.size(); ++c)
	{
		int32 which = -1;
		for (size_t i = 0; i < targetStarts.size() && i < targetAttrs.size(); ++i)
		{
			if (targetStarts[i] <= changes[c].fTargetStart)
				which = static_cast<int32>(i);
		}
		if (which < 0)
			continue;			// before the first paragraph: the body's default stands
		const KCMParaAttrs& a = targetAttrs[static_cast<size_t>(which)];
		changes[c].fPlace = a.IsCell() ? kKCMPlaceCell : (a.IsFootnote() ? kKCMPlaceNote : kKCMPlaceBody);
	}
}

/* ParagraphIndexAt
   The paragraph whose start is the last one at or before `at` - the same walk MarkPlaces makes.
   -1 before the first paragraph. */
int32 ParagraphIndexAt(const std::vector<int32>& starts, TextIndex at)
{
	int32 which = -1;
	for (size_t i = 0; i < starts.size(); ++i)
		if (starts[i] <= at)
			which = static_cast<int32>(i);
	return which;
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

/* FoldTableChanges
   ★★★A TABLE WHOSE SHAPE DIFFERS FROM TASK START'S BECOMES ONE CHANGE (2026-09-19 night, the user:
   "fold every change of that table - rows, columns, merged cells, and the words inside - into one row,
   Table ≠"). The cell-level changes the paragraph diff found inside it are taken out; only the spans
   they mark stay, on fMarkSpans.
   ⚠Which cells keep the reader's own words over a restore is NOT worked out here any more (2026-09-20):
    the restore merges the two tables as XML, where each <Cell> names itself. See the note inside.
   A table with no partner is Table + (only here) or Table − (only in Task Start).

   ⚠TABLES ARE PAIRED BY ORDINAL - the numbering KCMTextRead gives the cells, in the order the tables'
    cell blocks begin - which is exact when tables were added or removed at the END of the story and
    merely noisy when one was added in the middle (every table after it then pairs with the wrong one
    and comes out as ≠). The spec accepts that: nothing is named falsely, there are just more rows.
   ⚠THE SOURCE'S SHAPES come from the source model when it is open, and from what was kept beside its
    text otherwise (KCMSourceCache) - a re-diff after a restore has no copy to ask. */
void FoldTableChanges(std::vector<KCMStoryChange>& out, UID targetStoryUID,
					  ITextModel* targetModel, ITextModel* sourceModel,
					  const std::vector<std::string>& targetParas, const std::vector<KCMParaAttrs>& targetAttrs,
					  const std::vector<int32>& targetStarts,
					  const std::vector<std::string>& sourceParas, const std::vector<KCMParaAttrs>& sourceAttrs,
					  const std::vector<int32>& sourceStarts)
{
	std::vector<KCMTableShape> tShapes, sShapes;
	if (targetModel == nil || !KCMReadTableShapes(targetModel, tShapes))
		return;
	if (sourceModel != nil)
	{
		if (!KCMReadTableShapes(sourceModel, sShapes))
			return;
	}
	else if (!KCMSourceCacheGetTableShapes(targetStoryUID, sShapes))
		return;

	// (⛔**THE STORY'S OWN INX IS NO LONGER KEPT** (2026-09-21). It was exported here, at most once per
	//  story with a changed table, so that a restore could cut Task Start's table out of it - and with
	//  the restore gone nothing reads it. ★What that removes is an INX export per story with a changed
	//  table, on every comparison and every re-diff.)

	// ★★★**WHICH TABLE IS WHICH, BY THE TABLES' OWN IDS** (2026-09-20, the user: "is it looking at
	//   tables by position? a table has an id too - can that not say which is which?"). The ids come
	//   out of the SOURCE's own INX, and a table's id is the uid the document that wrote the text
	//   gave it, so the same table is named alike on both sides (KCMTableSnippet.h; measured the same
	//   day, including that an id moves for no insertion, no removal and no undo).
	// ⚠**ONLY WHEN THE TWO SIDES CARRY THE SAME DOCUMENT'S NUMBERS.** A Source that is some OTHER
	//   document holds no story under this uid and answers nothing, and a text that names one table
	//   without a Self of its own can name any of them wrongly - in both cases the pairing falls back
	//   to the position, which is what it always was.
	std::vector<UID> sIds;
	bool16 byId = kFalse;
	{
		// ★★**THE SOURCE'S OWN INX, NOT AN ORIGIN'S** (2026-09-21). These ids used to be read from
		//   the bytes Task Start held; a Task Start is a document now, so they are exported from it
		//   the same way the Target's are.
		// ★★★**AND THE TEST FOR "IS THE SOURCE A COPY OF THIS DOCUMENT" IS THE UID ITSELF.** A copy
		//   saved and opened carries the numbers the original carries, so asking the Source's INX for
		//   THIS story's uid answers only when the two are the same document - which is exactly the
		//   condition the origin's version rested on ("a Source whose uids mean nothing here"), now
		//   measured instead of inferred from which kind of comparison is running.
		IDataBase* const sourceDB = (sourceModel != nil) ? ::GetDataBase(sourceModel) : nil;
		KCMMemXferBytes sourceInx;
		if (sourceDB != nil
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
			byId = kTrue;
			for (size_t i = 0; i < sIds.size() && byId; ++i)
			{
				if (sIds[i] == kInvalidUID)
					byId = kFalse;
				for (size_t j = i + 1; j < sIds.size() && byId; ++j)
					if (sIds[i] == sIds[j])
						byId = kFalse;
			}
		}
	}

	// The pairs: every Target table with the Task Start table it IS, then Task Start's tables that
	// nothing paired with (Table −). ⚠A table pairs with at most one - an id is taken once, so a
	// recycled id cannot make two tables claim the same partner.
	std::vector<std::pair<int32, int32> > pairs;		// (target index, source index); -1 for neither
	std::vector<bool16> sTaken(sShapes.size(), kFalse);
	for (size_t i = 0; i < tShapes.size(); ++i)
	{
		int32 partner = -1;
		if (byId)
		{
			// (⛔A translation step stood here until 2026-09-21: a table KCM had PUT BACK carried an id
			//  Task Start never saw - a snippet import hands out new ones - so the live id was mapped
			//  back through what the restore had recorded. KCM writes no table now, so every id in this
			//  story is one the document itself gave.)
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
		pairs.push_back(std::make_pair(static_cast<int32>(i), partner));
	}
	for (size_t j = 0; j < sShapes.size(); ++j)
		if (!sTaken[j])
			pairs.push_back(std::make_pair(-1, static_cast<int32>(j)));

	for (size_t p = 0; p < pairs.size(); ++p)
	{
		const int32 tAt = pairs[p].first;
		const int32 sAt = pairs[p].second;
		const bool16 haveT = (tAt >= 0) ? kTrue : kFalse;
		const bool16 haveS = (sAt >= 0) ? kTrue : kFalse;
		// ⚠**tI AND sI ARE ONLY MEANINGFUL UNDER haveT / haveS.** Each falls back to 0 for a side that
		//   has no table, and 0 is out of range when that side's list is EMPTY - so every use below
		//   stands inside an `if (haveT)`, an `if (haveS)`, or the short-circuiting side of a `&&` or
		//   a `?:`. Re-checked line by line 2026-09-20; keep it that way when adding to this loop.
		const size_t tI = static_cast<size_t>(haveT ? tAt : 0);
		const size_t sI = static_cast<size_t>(haveS ? sAt : 0);
		if (haveT && haveS && !KCMTableShapesDiffer(tShapes[tI], sShapes[sI]))
			continue;					// the same shape: the cell rows stay as the diff made them

		// ⚠**THE TWO SIDES NUMBER THEIR TABLES SEPARATELY** now that they are paired by id: the cells
		//   of this table are the ones whose own side's ordinal is this side's index.
		const int32 tOrdinal = haveT ? tAt : -1;
		const int32 sOrdinal = haveS ? sAt : -1;
		KCMStoryChange table;
		table.fWhat = KCMStoryChange::kTable;
		table.fPlace = kKCMPlaceCell;
		table.fKind = (!haveS) ? KCMStoryChange::kInsert
					: (!haveT) ? KCMStoryChange::kDelete
					: KCMStoryChange::kReplace;
		// (⛔**SEVEN THINGS A TABLE ROW RECORDED FOR THE RESTORE WENT ON 2026-09-21**: the two tables'
		//  own ids, the write block for a pairing that had fallen back to the position, the two shape
		//  signatures, and where the table stood measured from the table before it. Every one of them
		//  was written here and read only by a write into the document. ★The ID PAIRING ITSELF STAYS -
		//  it is what tells this call WHICH table is which, and the row is drawn from it.)
		if (haveT)
		{
			table.fTargetStart = tShapes[tI].fAnchorStart;
			table.fTargetEnd = tShapes[tI].fAnchorEnd;
			table.fTargetTableUID = tShapes[tI].fDictUID;
		}
		if (haveS)
		{
			table.fSourceStart = sShapes[sI].fAnchorStart;
			table.fSourceEnd = sShapes[sI].fAnchorEnd;
			// ★THE SOURCE'S OWN ID (2026-09-25, "Match the Source"): the dictionary uid in the Source document - a
			//   shape from the cache carries kInvalidUID, and the match item is greyed by that.
			table.fSourceTableUID = (sourceModel != nil) ? sShapes[sI].fDictUID : kInvalidUID;
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
			// neighbour; an EMPTY paragraph's range has no width at all - a new empty cell, measured
			// 2026-09-20 - so the range's width cannot be what decides). The side that has the change
			// is told by its kind: an insertion is on the target side, a deletion on the source side, a
			// replacement (and an attribute) on both.
			TextIndex tFrom = ch.fTargetStart, tTo = ch.fTargetEnd;
			TextIndex sFrom = ch.fSourceStart, sTo = ch.fSourceEnd;
			KCMShownSpan(ch.fBreakAt, tFrom, tTo);
			KCMShownSpan(ch.fBreakAt, sFrom, sTo);
			const bool16 sideT = (ch.fKind != KCMStoryChange::kDelete) ? kTrue : kFalse;
			const bool16 sideS = (ch.fKind != KCMStoryChange::kInsert) ? kTrue : kFalse;
			const int32 tPara = sideT ? ParagraphIndexAt(targetStarts, tFrom) : -1;
			const int32 sPara = sideS ? ParagraphIndexAt(sourceStarts, sFrom) : -1;
			const bool16 tHolds = ParaIsCellOfTable(targetAttrs, tPara, tOrdinal);
			const bool16 sHolds = ParaIsCellOfTable(sourceAttrs, sPara, sOrdinal);
			const bool16 inTable = (haveT && tHolds) || (haveS && sHolds);
			if (!inTable)
			{
				kept.push_back(ch);
				continue;
			}

			// ★★★**A CHANGE IN A CELL THAT IS THE SAME CELL ON BOTH SIDES STAYS A ROW OF ITS OWN** (2026-09-25, the
			//   user's rule: "the cells whose structure did not change - take those back with the change history;
			//   the cells whose structure changed - with the new mechanism"). The same address and the same merge
			//   on both sides (KCMTableCellSame): its words are a Cell Text row, rejected one by one; only a change
			//   in a cell the shape brought, took or re-merged is folded into the Table row - which is exactly
			//   what "Match the Source" rewrites (KCMTableMatch.h). ⚠Both tables have to exist for a cell to be the
			//   same on both sides; a Table + or − folds everything, as before.
			if (haveT && haveS)
			{
				const int32 cellPara = tHolds ? tPara : sPara;
				const std::vector<KCMParaAttrs>& cellAttrs = tHolds ? targetAttrs : sourceAttrs;
				const int32 row = cellAttrs[static_cast<size_t>(cellPara)].fCellRow;
				const int32 col = cellAttrs[static_cast<size_t>(cellPara)].fCellCol;
				if (KCMTableCellSame(tShapes[tI], sShapes[sI], row, col))
				{
					kept.push_back(ch);
					continue;
				}
			}

			if (tHolds)
				table.fMarkSpans.push_back(KCMTextSpan(tFrom, tTo));

			// (Which cells keep the reader's own words over a restore was worked out HERE until
			//  2026-09-20 - fKeptCells - and it could not be made right from this side. A change has a
			//  paragraph only on the side that holds its characters, so an insertion or a deletion had
			//  to guess the other side by position, and a caret in a cell the other side does not have,
			//  or in an EMPTIED cell (no width at all), names the next cell along. It put a new row's
			//  "k21" into the old (1,1) on the running application.
			//  ★The restore now merges the two tables AS XML, where every <Cell> carries its own
			//  Name="col:row" and an empty cell is named as plainly as a full one - KCMTableSnippet.h,
			//  KCMMergeTableCells. Nothing about cells is carried from here any more.)
		}

		// Cells only here (a new row or column, by address) and cells whose merge differs: marked whole.
		if (haveT)
		{
			for (size_t i = 0; i < tShapes[tI].fCells.size(); ++i)
			{
				const KCMTableCellPlace& cell = tShapes[tI].fCells[i];
				bool16 mark = !haveS;
				if (haveS)
				{
					const KCMTableCellPlace* const partner = KCMTableCellAt(sShapes[sI], cell.fRow, cell.fCol);
					mark = (partner == nil) ? kTrue : kFalse;
					if (!mark)
					{
						// The merge at this address on each side (1x1 when not listed).
						int32 tr = 1, tc = 1, sr = 1, sc = 1;
						for (size_t m = 0; m < tShapes[tI].fMerges.size(); ++m)
							if (tShapes[tI].fMerges[m].fRow == cell.fRow && tShapes[tI].fMerges[m].fCol == cell.fCol)
							{ tr = tShapes[tI].fMerges[m].fRowSpan; tc = tShapes[tI].fMerges[m].fColSpan; }
						for (size_t m = 0; m < sShapes[sI].fMerges.size(); ++m)
							if (sShapes[sI].fMerges[m].fRow == cell.fRow && sShapes[sI].fMerges[m].fCol == cell.fCol)
							{ sr = sShapes[sI].fMerges[m].fRowSpan; sc = sShapes[sI].fMerges[m].fColSpan; }
						mark = (tr != sr || tc != sc) ? kTrue : kFalse;
					}
				}
				if (mark)
					table.fMarkSpans.push_back(KCMTextSpan(cell.fStart, cell.fEnd));
			}
		}
		else
		{
			// Table −: the place it stood, as a caret in the Target.
			// ⚠★★★**THE SOURCE'S OWN ANCHOR POSITION, CLAMPED TO THE STORY** (2026-09-20, measured on
			//   the running application). It was the caret of one of the folded cell changes, which is a
			//   position among the CELLS - and cells stand past the whole body (ITableTextContent.h), so
			//   for a table that is GONE from the Target it came out past the end of the story: 12 in a
			//   story of 10. Nothing drew a mark there, and every write refused it with "the change's
			//   range is outside the story" - the range test in RestoreOne stands before the one that
			//   would have said the real reason, so even the refusal was misleading.
			//   ★The anchor stood in the BODY, and the body agrees on both sides unless it was edited
			//   too - and an edit there is a row of its own.
			TextIndex at = haveS ? sShapes[sI].fAnchorStart : 0;
			const TextIndex total = (targetModel != nil) ? targetModel->TotalLength() : at;
			if (at > total)
				at = total;
			if (at < 0)
				at = 0;
			table.fTargetStart = at;
			table.fTargetEnd = at;
			table.fMarkSpans.push_back(KCMTextSpan(at, at));
		}

		// The Story column: the shape word, then the table's first words (on the side that has it).
		// ★★**A TABLE THIS VERSION ADDED SHOWS ITS SHAPE ALONE** (2026-09-20, the user: "when a table
		//   is added, the Story column shows its contents too - that is not wanted, 2×1 or so is
		//   enough"). A Table + has no partner in the older version, so what it SAYS is not the
		//   difference - the whole table is - and the shape is the whole of what names it.
		//   ⚠The other two keep their words on purpose: a Table ≠ and a Table − are about ONE table
		//    among several, and the first words are how the reader tells which.
		table.fShapeWord = (haveT && haveS) ? KCMTableShapeWord(sShapes[sI], tShapes[tI])
						 : KCMTableShapeAlone(haveT ? tShapes[tI] : sShapes[sI]);
		std::string words = table.fShapeWord;
		if (haveS)
		{
			const std::string first = haveT ? FirstTableWords(tOrdinal, targetParas, targetAttrs)
											: FirstTableWords(sOrdinal, sourceParas, sourceAttrs);
			if (!first.empty())
			{
				words += " ";
				words += first;
			}
		}
		SetDocumentText(table.fText, words);
		// The other side, for the message area: Task Start's shape and first words.
		std::string other = haveS ? KCMTableShapeAlone(sShapes[sI]) : std::string();
		const std::string otherFirst = haveS ? FirstTableWords(sOrdinal, sourceParas, sourceAttrs) : std::string();
		if (!otherFirst.empty())
		{
			other += " ";
			other += otherFirst;
		}
		SetDocumentText(table.fOtherText, other);

		kept.push_back(table);
		out.swap(kept);
	}
}

bool16 CompareOneStory(const UIDRef& targetStory, const UIDRef& sourceStory,
					   std::vector<KCMStoryChange>& out)
{
	out.clear();

	// ★★★READ STRAIGHT FROM THE TEXT MODEL. The story used to be exported as a snippet and parsed
	//   back out of XML, and the positions counted from it then had to be checked against the
	//   document's own answer - two sets of books, five ways to refuse a story, and a whole file
	//   (KCMStoryCellBases) to reconcile them. **The document is now asked once, and its answer is
	//   the only one.** ⇒ KCMTextRead.h carries why; the parallel run that measured the switch is
	//   gone with the old route (2026-09-03).
	//
	//   ⚠WHAT THIS CHANGES FOR THE READER, in one line each:
	//   ・**a footnote no longer silences a story** - the parser did not know <Footnote> and folded
	//     the note into the body, so the length never matched and the whole story was refused
	//     (measured: one footnote turned edits=1 into edits=0, in the body AND in the note).
	//   ・**a table standing inside a paragraph no longer does either** (work/kcm-selftest/midtable).
	//   ・**the ruby is read from the strand**, not inferred from an attribute's presence.
	//   ・**the paragraph ORDER differs where a table stands**: the snippet put a table's cells
	//     where the table is, the model keeps them past the body. Both sides come out in the same
	//     order as each other, and ChangeIsBefore sorts the rows by TextIndex in the end, so the
	//     panel reads the same - **but SplitRunAtPlaces cuts differently** (the body is now
	//     contiguous), which is the one visible difference and is the more natural cutting.
	//
	//   ★ONE READ, ONE MOMENT. Ruby comes out of the same walk as the text, for the reason spelt
	//   out in KCMParaText.h: a comparison is one moment, and reading the ruby separately would
	//   put two moments in one row.
	std::vector<std::string> targetParas;
	std::vector<std::string> sourceParas;
	std::vector<KCMParaAttrs> targetAttrs;
	std::vector<KCMParaAttrs> sourceAttrs;
	std::vector<int32> targetStarts;
	std::vector<int32> sourceStarts;

	// ⚠**THE FIRST OF THE TWO REMAINING WAYS TO DECLINE**, and it means one thing only: the story
	//   could not be opened at all. **An empty story is not a failure** (KCMTextRead.h).
	if (!KCMTextRead::ReadStory(targetStory, targetParas, targetAttrs, targetStarts))
		return kFalse;

	// ★★★**THE SOURCE IS READ ONCE PER ORIGIN, NOT ONCE PER PRESS** (2026-09-16, the user: "it is
	//   too heavy to work with"). Against a Task Start - or an Import, which uses the same slot -
	//   the Source is a byte string rebuilt into a whole document for the occasion, and every
	//   change taken in was rebuilding it again. It cannot change while it is held, so what was
	//   read from it is kept (KCMSourceCache says when that is allowed and when it is dropped).
	// ⚠**THE TARGET IS NEVER CACHED**: it is the reader's own document and they are editing it.
	//   That is the whole asymmetry, and it is why only one of the two reads above moved.
	if (!KCMSourceCacheGet(targetStory.GetUID(), sourceParas, sourceAttrs, sourceStarts))
	{
		if (!KCMTextRead::ReadStory(sourceStory, sourceParas, sourceAttrs, sourceStarts))
			return kFalse;

		// ★**THE RAW TEXT IS TAKEN IN THE SAME BREATH**, from the same API the restore used
		//   used to call on every press - so what the write puts in is the document's own
		//   characters and not a re-assembly of the paragraphs above (KCMSourceCache.h says what
		//   re-assembling them would get wrong, and it is the paragraph break itself).
		WideString raw;
		InterfacePtr<ITextModel> sourceModel(sourceStory, UseDefaultIID());
		if (sourceModel != nil)
		{
			const TextIndex total = sourceModel->TotalLength();
			if (total > 0)
			{
				TextIterator iter(sourceModel, 0);
				iter.AppendToStringAndIncrement(&raw, total);
			}
		}
		KCMSourceCachePut(targetStory.GetUID(), sourceParas, sourceAttrs, sourceStarts, raw);

		// ★AND THE SOURCE'S TABLES, FOR THE SAME REASON (2026-09-20): the Table row compares shapes on
		//   every re-diff, and the copy is not there to be asked then.
		if (sourceModel != nil)
		{
			std::vector<KCMTableShape> sourceTables;
			if (KCMReadTableShapes(sourceModel, sourceTables))
				KCMSourceCachePutTableShapes(targetStory.GetUID(), sourceTables);
		}
	}

	// **ONE TABLE FOR BOTH SEQUENCES.** Numbering them from separate tables would give equal
	//   paragraphs different tokens, and every paragraph would look changed.
	std::vector<std::string> table;
	std::vector<int32> sourceTokens;
	std::vector<int32> targetTokens;
	KCMTextDiff::Tokenize(sourceParas, table, sourceTokens);
	KCMTextDiff::Tokenize(targetParas, table, targetTokens);

	std::vector<KCMTextDiff::Change> paragraphChanges;
	if (!KCMTextDiff::Diff(sourceTokens, targetTokens, paragraphChanges))
		return kFalse;		// too different to place - the row stays, the detail does not

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
	{
		std::vector<KCMTextDiff::Change> byPlace;
		std::vector<KCMParaText::RegionPair> pieces;
		for (size_t c = 0; c < paragraphChanges.size(); ++c)
		{
			const KCMTextDiff::Change& run = paragraphChanges[c];
			KCMParaText::SplitRunAtPlaces(sourceAttrs, run.aStart, run.aCount,
											   targetAttrs, run.bStart, run.bCount, pieces);
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

	// ★★**A RUN HOLDING MORE PARAGRAPHS ON ONE SIDE IS CUT INTO PAIRS, ADDITIONS AND REMOVALS** (2026-09-17
	//   afternoon). The paragraph diff merges an added paragraph with an edited one beside it into one
	//   run, and the character pass then wrote "NEW\r" into the head of the edited paragraph - the new
	//   paragraph taking THAT paragraph's style, with no next style and no whole-paragraph row. Paired the
	//   way the import pours (KCMParaPairing - the paragraphs sharing the most characters pair), the edited
	//   paragraph is an edit and the new one is a paragraph of its own. A pair whose words are the same is
	//   no change at all and is left out, which is also what AddAttributeChanges reads as "unchanged".
	{
		std::vector<KCMTextDiff::Change> expanded;
		for (size_t c = 0; c < paragraphChanges.size(); ++c)
		{
			const KCMTextDiff::Change& run = paragraphChanges[c];
			if (run.aCount == run.bCount || run.aCount == 0 || run.bCount == 0)
			{
				expanded.push_back(run);
				continue;
			}
			const std::vector<std::string> a(sourceParas.begin() + run.aStart, sourceParas.begin() + run.aStart + run.aCount);
			const std::vector<std::string> b(targetParas.begin() + run.bStart, targetParas.begin() + run.bStart + run.bCount);
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
					if (sourceParas[static_cast<size_t>(piece.aStart)] == targetParas[static_cast<size_t>(piece.bStart)])
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

	// Where each story ends, for a run that covers no paragraph of its own (an insertion past the
	//   last one). ★**ASKED OF THE DOCUMENT.** The total used to be added up from what the snippet
	//   said, with a set of counters that existed only because the positions came from somewhere
	//   other than the model. Nothing counts any more, so nothing has to be reconciled.
	InterfacePtr<ITextModel> targetModel(targetStory, UseDefaultIID());
	InterfacePtr<ITextModel> sourceModel(sourceStory, UseDefaultIID());
	const int32 targetComputed = (targetModel != nil) ? targetModel->TotalLength() : 0;
	const int32 sourceComputed = (sourceModel != nil) ? sourceModel->TotalLength() : 0;

	// ⚠★★★**THERE ARE TWO WAYS TO DECLINE, AND BOTH CARRY A REASON.** A story is either
	//   unreadable (ReadStory, above) or too different to place (KCMTextDiff::Diff, further up).
	//   The XML route had five, and carried none of them back; a row with no children then meant
	//   "nothing to show" or "I could not look" and nobody could tell which. ⇒ **A row that shows
	//   nothing now means one of two known things** - which was the point of the migration as much
	//   as the footnote was.
	//   ★The old route, the checks that reconciled it (ComputedLength / LengthAgrees /
	//     KCMStoryCellBases) and the parallel run that measured the switch were removed on
	//     2026-09-03, after every pair in work/kcm-selftest agreed and the regression of
	//     2026-09-01 (docs/ai-notes/kcm-story-direct-read-regression-2026-09-01.md) passed.
	//     ⚠**With them went the only instrument that could dump what the reader read** - the user's
	//     call; the reader's answer now reaches the outside only through the rows themselves.

	// (⛔The Source's raw characters were read here until 2026-09-21, to ask whether a write back
	//  would have to carry an object. Nothing writes back now.)

	for (size_t c = 0; c < paragraphChanges.size(); ++c)
	{
		const KCMTextDiff::Change& change = paragraphChanges[c];

		// ★★Paragraphs on one side only: one change per paragraph, its break in its range (AddWholeParagraphs).
		if ((change.aCount == 0) != (change.bCount == 0))
		{
			AddWholeParagraphs(out, change, sourceParas, sourceStarts, sourceAttrs,
							   targetParas, targetStarts, targetAttrs);
			continue;
		}

		const std::string sourceText = KCMParaText::JoinParagraphs(sourceParas, change.aStart, change.aCount);
		const std::string targetText = KCMParaText::JoinParagraphs(targetParas, change.bStart, change.bCount);

		// Where this run starts on each side. A run with no paragraphs of its own sits where the
		// next surviving paragraph begins.
		const int32 tBase = (change.bStart < static_cast<int32>(targetStarts.size()))
							? targetStarts[change.bStart] : targetComputed;
		const int32 sBase = (change.aStart < static_cast<int32>(sourceStarts.size()))
							? sourceStarts[change.aStart] : sourceComputed;

		// The run itself, so that a position inside it can be asked for rather than added up -- see
		//   RunSide. Built once here because both callers of Add below need the same two.
		const RunSide tRun(targetParas, targetStarts, targetAttrs, change.bStart, change.bCount, tBase);
		const RunSide sRun(sourceParas, sourceStarts, sourceAttrs, change.aStart, change.aCount, sBase);

		std::vector<int32> sourceCodePoints;
		std::vector<int32> targetCodePoints;
		std::vector<int32> sourceBytes;
		std::vector<int32> targetBytes;
		KCMTextDiff::ToCodePoints(sourceText, &sourceCodePoints, &sourceBytes);
		KCMTextDiff::ToCodePoints(targetText, &targetCodePoints, &targetBytes);

		// The second pass: narrow the run down to the characters that actually differ, so that a
		// one-word edit selects the word and not the paragraph it sits in.
		std::vector<KCMTextDiff::Change> fineChanges;
		const bool16 narrowed = KCMTextDiff::Diff(sourceCodePoints, targetCodePoints, fineChanges)
								&& !fineChanges.empty();

		// **ONLY ON CHARACTERS.** Applied to the paragraph list this would report paragraphs nobody
		//   touched as changed -- see KCMTextDiff.h. The same restriction covers the alignment
		//   below, for a plainer reason: a paragraph token is a number, and asking which script it
		//   is written in means nothing.
		if (narrowed)
		{
			// ⚠**THE PARAGRAPH BREAK IS HANDED OVER AS SOMETHING NOT TO SWALLOW** (2026-09-16).
			//   JoinParagraphs puts exactly one '\n' between two paragraphs of a run, so that
			//   code point is what marks a break here - and a change that contains one writes
			//   over it, which costs the following paragraphs their styles (KCMTextDiff.h has
			//   the measurement).
			KCMTextDiff::MergeNearbyChanges(fineChanges, &sourceCodePoints, static_cast<int32>('\n'));

			// **THEN SLIDE EACH RUN TO THE POSITION A READER WOULD PUT IT.** Myers returns A
			//   shortest edit script, not THE one a person would describe, and when the surrounding
			//   text repeats a character the two differ visibly. Reported from the panel:
			//   「旧版です」->「新版です・ここが違います」 came back quoting「す・ここが違いま」
			//   -- starting and ending on す because the run had been rotated one step left, which
			//   costs Myers nothing and costs the reader the whole sentence.
			//   @warning AFTER the merge, not before: merging changes the shape of a run, and
			//     aligning a run that is about to be swallowed would be work thrown away.
			//   ★kTrue = widen a change to the whole Latin word (letters or digits) it sits in: the
			//     marks are DRAWN, and a word drawn in two pieces shows a seam. KIDMCP, which shares
			//     this engine and prints instead, passes kFalse (KCMTextDiff.h says why).
			KCMTextDiff::AlignChangeBoundaries(sourceCodePoints, targetCodePoints, fineChanges, kTrue);
		}

		if (!narrowed)
		{
			// The whole run, as one change. This is where a run lands when the character pass
			// cannot place it - not an error, just a coarser answer.
			AddCutAtObjects(out, targetText, targetBytes, tRun, 0, static_cast<int32>(targetCodePoints.size()),
							sourceText, sourceBytes, sRun, 0, static_cast<int32>(sourceCodePoints.size()));
			continue;
		}

		for (size_t k = 0; k < fineChanges.size(); ++k)
		{
			const KCMTextDiff::Change& fine = fineChanges[k];
			AddCutAtObjects(out, targetText, targetBytes, tRun, fine.bStart, fine.bCount,
							sourceText, sourceBytes, sRun, fine.aStart, fine.aCount);
		}
	}

	// **AND THEN THE RUBY.** Everything above compared <Content> and nothing else, so a story
	//   whose ruby alone was edited came out of it with no children at all -- the row said
	//   "None", which is what the reader reported. The paragraphs the diff did NOT mention are
	//   exactly the ones to ask about.
	AddAttributeChanges(paragraphChanges, sourceParas, targetParas, sourceAttrs, targetAttrs,
					   sourceStarts, targetStarts, out);

	// @warning put back in reading order. The ruby children were found by a separate walk, so
	//   without this they would all sit after the text ones and the tree would run down the
	//   story twice. STABLE, so that two changes at the same position keep the order they were
	//   made in.
	std::stable_sort(out.begin(), out.end(), ChangeIsBefore);
	MarkPlaces(targetAttrs, targetStarts, out);		// the body, a cell or a note - for the ID column

	// ★★A TABLE WHOSE SHAPE CHANGED IS ONE ROW (2026-09-19 night): its cell changes fold into it.
	FoldTableChanges(out, targetStory.GetUID(), targetModel, sourceModel,
					 targetParas, targetAttrs, targetStarts, sourceParas, sourceAttrs, sourceStarts);
	std::stable_sort(out.begin(), out.end(), ChangeIsBefore);

	return kTrue;
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

	// **THE GUARD BELONGS HERE, NOT AT THE CALLER** -- see the header for the two callers and
	//   which one of them lacks it. Reading a story can compose (asking for text that has never
	//   been laid out lays it out), and composing sets the modified flag on a document this
	//   feature only ever reads. KCM's whole premise is that comparing changes nothing.
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

	// (⛔A whole comparison used to drop every story's kept INX here, and a story refresh dropped its
	//  own below. Both went with the restore on 2026-09-21: nothing is kept to drop.)

	int32 total = 0;

	// How many rows will actually be read -- the progress bar's range. The same test as the loop's,
	//   so the two cannot disagree about what counts.
	const int32 rowCount = KCMStoryList::GetRowCount();
	int32 pairedCount = 0;
	for (int32 i = 0; i < rowCount; ++i)
	{
		const KCMStoryRow* row = KCMStoryList::GetRow(i);
		if (row != nil && row->fStoryUID != kInvalidUID && (row->fKinds & kKCMStoryKindUnpaired) == 0)
			++pairedCount;
	}
	// **The bar, with its Cancel, appears after kKCMProgressBarDelayMs** (2026-09-05, the user's
	//   call: "a document can hold an enormous number of stories"). Until then this loop had no bar
	//   at all -- the Pixel mode's was tied to a page count, and this mode rasterises no page.
	PMString barTitle("Comparing stories...");
	barTitle.SetTranslatable(kFalse);
	// ★A stepper, not a bar of its own: an import covers the whole of its work - this loop included -
	//   with ONE bar, and two bars alive at once is the thing KCMProgressBar.h forbids.
	KCMProgressStepper progress(barTitle, pairedCount);
	int32 done = 0;

	for (int32 i = 0; i < rowCount; ++i)
	{
		const KCMStoryRow* row = KCMStoryList::GetRow(i);
		if (row == nil || row->fStoryUID == kInvalidUID)
			continue;

		// A story with no partner cannot be compared against anything, and the list already knows
		//   that -- this is the same judgement KCMStoryStamp made when it built the row, read
		//   rather than made again. Asking the older document for the UID ourselves would be a
		//   second place where "does this story exist over there" gets answered.
		//   **kKCMStoryKindUnpaired COVERS BOTH added AND removed.** @warning for a removed row
		//     this is not merely wasted work: its fStoryUID is a SOURCE uid, so the two UIDRefs
		//     below would ask the TARGET for it -- and a uid that means one story over there can
		//     name a different object over here. The row must never reach that line.
		if ((row->fKinds & kKCMStoryKindUnpaired) != 0)
			continue;

		PMString item("Story ");
		item.AppendNumber(done + 1);
		item.Append(" / ");
		item.AppendNumber(pairedCount);
		item.SetTranslatable(kFalse);	// it holds numbers, so it is not a translatable string
		progress.Step(done, item);		// `done` stories are read; this is also where the bar first appears, once the delay has passed

		std::vector<KCMStoryChange> changes;
		// ⛔The uid translation went on 2026-09-21: a Task Start copy was rehydrated and its stories
		//   carried new numbers. A copy saved to a file carries the originals.
		if (CompareOneStory(UIDRef(targetDB, row->fStoryUID),
							UIDRef(sourceDB, row->fStoryUID), changes))
		{
			// **WRITTEN EVEN WHEN NOTHING DIFFERS.** It used to `continue` here, on the grounds that
			//   writing an empty list changes nothing -- which was true of the CHANGES and false of the
			//   fact that somebody looked. That fact is what lets the row say "None" instead of standing
			//   there mute beside the rows that could not be compared at all.
			MarkOverset(targetDB, row->fStoryUID, changes);
			KCMStoryList::SetRowChanges(i, changes, kTrue);
			KCMStoryList::SetRowTargetTextCount(i, KCMStoryDiffRun::TextCountOf(UIDRef(targetDB, row->fStoryUID)));
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

	return total;
}

//----------------------------------------------------------------------------------------
// RunOne
//----------------------------------------------------------------------------------------

uint32 KCMStoryDiffRun::TextCountOf(const UIDRef& story)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	return (model != nil) ? model->GetTextChangeCount() : 0;
}

// (⛔**StillReplaced AND DropUndoneReplaced WENT ON 2026-09-21** with the restore. The first asked
//  whether a change the reader had taken in was STANDING as taken in - by comparing the story's
//  counter with the one recorded at the write (">=", not "==", measured on the application: a
//  second write in the same story moved the counter past the first one's mark), or, for a table,
//  by looking for the table the restore had left, by its id. The second dropped the records an
//  undo had taken back. ★Nothing is ever taken in now, so nothing is ever "standing as taken in".
//  ⚠**CountForKind below is NOT the restore's**: it is what a comparison records and reads, and the
//   reasoning about which counter belongs to which kind of change is still live.)

uint32 KCMStoryDiffRun::CountForKind(const UIDRef& story, int32 kind)
{
	// The header carries the measurement and the reasoning. Here: words are measured by the text
	// counter, anything laid OVER the words by the aggregate - because the text counter does not
	// move for a ruby or a kenten, so it can tell neither that one went in nor that it came out.
	if (kind == kKCMStoryAttrNone)
		return KCMStoryDiffRun::TextCountOf(story);

	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	return (model != nil) ? static_cast<uint32>(model->GetChangeCount()) : 0;
}

int32 KCMStoryDiffRun::RunOne(IDataBase* targetDB, IDataBase* sourceDB, int32 rowIndex)
{
	// ★★**A nil SOURCE IS ALLOWED WHEN THE STORY IS ALREADY KEPT** (2026-09-16). Comparing one
	//   story again after a write no longer needs the origin rebuilt into a document: what the
	//   Source side says was read when the comparison was set up and cannot have changed since
	//   (KCMSourceCache.h). The caller passes nil to say "I did not open one", and only a story
	//   nobody has read yet refuses here.
	if (targetDB == nil)
		return -1;

	// **THE UID IS COPIED OUT BEFORE ANYTHING ELSE HAPPENS.** GetRow hands back a pointer into
	//   the list, and the work below writes to that same list -- so the answer to "which story is
	//   this row about" is taken while the question is still safe to ask.
	const KCMStoryRow* row = KCMStoryList::GetRow(rowIndex);
	if (row == nil || row->fStoryUID == kInvalidUID)
		return -1;
	const UID storyUID = row->fStoryUID;
	const bool16 unpaired = ((row->fKinds & kKCMStoryKindUnpaired) != 0);
	row = nil;

	// Nothing open and nothing kept: there is no older side to compare against at all.
	if (sourceDB == nil && !KCMSourceCacheHas(storyUID))
		return -1;

	// A story with no partner has nothing to be compared against - the same judgement Run reads
	// rather than makes again. The menu greys the item for these rows, so this is the second line
	// of defence. Both kinds (added AND removed); for a removed row the uid above belongs to the
	// SOURCE and must not be handed to the target (see Run's note).
	if (unpaired)
		return -1;

	// See the header: the same guard Run takes, and the only one on this path.
	// ⚠**THE SOURCE'S GUARD ONLY EXISTS WHEN THE SOURCE DOES.** With the story kept there is no
	//   Source document to leave dirty - and handing a nil database to a guard that exists to
	//   write its modified flag back is not a thing to find out about at run time.
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	K2::scoped_ptr<IDataBase::SaveRestoreModifiedState> sourceDirtyGuard(
		(sourceDB != nil) ? new (std::nothrow) IDataBase::SaveRestoreModifiedState(sourceDB) : nil);

	// **THE ROW ITSELF IS RE-READ FIRST.** The row quotes the story's opening words, and points
	//   at the frame a click scrolls to -- both read from the document when the comparison ran.
	//   Refreshing only the CHILDREN left the row quoting a sentence the reader had just
	//   rewritten, which is the panel showing two different moments on one line.
	//   @warning it runs inside the guard for the same reason everything else here does. A story
	//     that has been deleted since simply leaves the row as it was -- the read refuses rather
	//     than half-writing it, and the diff below is what reports the failure.
	KCMStoryList::RefreshRowFromDocument(rowIndex, targetDB);

	// ⚠**THE SOURCE'S UIDRef IS ONLY BUILT WHEN THERE IS A SOURCE.** With the story kept,
	//   CompareOneStory never opens it - the cache answers first - so an invalid ref is the honest
	//   thing to hand it, and the uid translator is not asked about a database that is not there.
	const UIDRef sourceRef = (sourceDB != nil)
		? UIDRef(sourceDB, storyUID)		// ⛔the uid translation went with the origin (2026-09-21)
		: UIDRef(nil, kInvalidUID);

	std::vector<KCMStoryChange> changes;
	const bool16 compared = CompareOneStory(UIDRef(targetDB, storyUID), sourceRef, changes);

	// **WRITTEN EITHER WAY, INCLUDING EMPTY.** What stands under the row after a refresh is what
	//   the documents say now, and "nothing" is a perfectly good thing for them to say -- the row
	//   shows it as "None" (KCMStoryRow::fTextCompared), which is how the reader tells "I have
	//   just repaired this" apart from "this was never looked at".
	if (!compared)
		changes.clear();
	MarkOverset(targetDB, storyUID, changes);
	KCMStoryList::SetRowChanges(rowIndex, changes, compared);
	// ★THE RECORDS OF WHAT THE READER TOOK BACK OUTLIVE THIS REFRESH (2026-09-24, stage 2 C - design 15-1-3): a
	//   Standing one stays where the diff now finds nothing; one the reader undid or redid has its twin among the
	//   changes just attached and is dropped, so that the change is not shown twice.
	KCMStoryList::PruneRejected(rowIndex, targetDB);
	KCMStoryList::SetRowTargetTextCount(rowIndex, KCMStoryDiffRun::TextCountOf(UIDRef(targetDB, storyUID)));

	return compared ? static_cast<int32>(changes.size()) : -1;
}

// End, KCMStoryDiffRun.cpp.
