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

// General includes:
#include "PMString.h"
#include "UIDRef.h"

#include <algorithm>	// std::stable_sort - the ruby children are found by a second walk
#include <string>
#include <vector>

// Project includes:
#include "KCMParaText.h"	// KCMParaAttrs and the pure functions over paragraphs (Join / IndexInStory / SplitRunAtPlaces / SpansDiffer)
#include "KCMProgressBar.h"	// KCMDeferredProgressBar - the progress bar and Cancel of Run, shown after kKCMProgressBarDelayMs
#include "KCMStoryDiffRun.h"
#include "KCMTextRead.h"		// the reader: paragraphs, their positions and their attributes, straight from the text model
#include "KCMStoryList.h"
#include "KCMStoryStamp.h"	// kKCMStoryKindAdded - which rows have no partner to compare against
#include "KCMTextDiff.h"

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

	if (utf8.find('\n') == std::string::npos && utf8.find('\r') == std::string::npos
		&& utf8.find(kAnchorChar) == std::string::npos)
		return utf8;		// the common case pays one scan and no allocation

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

	RunSide(const std::vector<std::string>& paragraphs, const std::vector<int32>& starts,
			const std::vector<KCMParaAttrs>& attrs, int32 start, int32 count, int32 base)
		: fParagraphs(&paragraphs), fStarts(&starts), fAttrs(&attrs),
		  fStart(start), fCount(count), fBase(base) {}

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

/* Add
   Builds one change and appends it. Kept in one place so that the two callers below - a run that
   was narrowed down to characters, and one that was not - cannot describe the same thing in two
   different ways.

   @param tFrom/tCount, sFrom/sCount are offsets WITHIN the joined run, in code points.
*/
void Add(std::vector<KCMStoryChange>& out, int32 paraIndex,
		 const std::string& targetText, const std::vector<int32>& targetBytes,
		 const RunSide& tRun, int32 tFrom, int32 tCount,
		 const std::string& sourceText, const std::vector<int32>& sourceBytes,
		 const RunSide& sRun, int32 sFrom, int32 sCount)
{
	KCMStoryChange change;
	change.fParaIndex = paraIndex;
	change.fWhat = KCMStoryChange::kText;

	change.fKind = (sCount == 0) ? KCMStoryChange::kInsert
				 : (tCount == 0) ? KCMStoryChange::kDelete
								 : KCMStoryChange::kReplace;

	// @warning **BOTH ENDS ARE ASKED FOR**, rather than the start plus the count. A change may
	//   run across a paragraph boundary, and a boundary can be worth more than the one character
	//   the joined text spends on it -- see RunSide above.
	change.fTargetStart = tRun.Index(tFrom);
	change.fTargetEnd = tRun.Index(tFrom + tCount);

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
	change.fSourceEnd = sRun.Index(sFrom + sCount);

	// **BOTH SIDES ARE CUT, ALWAYS.** The row shows the side that changed; the panel's message
	//   area shows the other one while that row is selected, so that the reader can see what the
	//   words used to be (or, for a deletion, what stands there now).
	std::string newPre, newMid, newPost;
	Slice(targetText, targetBytes, tFrom, tCount, kContextCodePoints, newPre, newMid, newPost);

	std::string oldPre, oldMid, oldPost;
	Slice(sourceText, sourceBytes, sFrom, sCount, kContextCodePoints, oldPre, oldMid, oldPost);

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
	//
	// All six pieces are document text, not just the middles -- see SetDocumentText.
	SetDocumentText(change.fTextPre, newPre);
	SetDocumentText(change.fText, newMid);
	SetDocumentText(change.fTextPost, newPost);

	SetDocumentText(change.fOtherTextPre, oldPre);
	SetDocumentText(change.fOtherText, oldMid);
	SetDocumentText(change.fOtherTextPost, oldPost);

	out.push_back(change);
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
				   int32 paraIndex, std::vector<KCMStoryChange>& out)
{
	KCMStoryChange change;
	change.fKind = kind;
	change.fWhat = KCMStoryChange::kAttr;		// the field that has waited for exactly this
	change.fAttrKind = attrKind;
	change.fParaIndex = paraIndex;

	// @warning **BOTH ENDS ARE ASKED FOR SEPARATELY**, exactly as Add does for a text change and
	//   for a reason it did not have: a span reaching across a table's own character covers one
	//   FEWER character of text than of model, so `start + count` would be a length in the wrong
	//   count. ModelIndex crosses between the two.
	change.fTargetStart = target.ModelIndex(tStart);
	change.fTargetEnd   = target.ModelIndex(tStart + tCount);

	// @warning **THE OLDER SIDE ALWAYS HAS CHARACTERS HERE**, unlike a text change: a ruby-only
	//   difference is found by comparing two paragraphs whose TEXT matched, so the same
	//   characters exist on both sides. (Ruby being ADDED is still "these characters, which are
	//   in both, now carry a reading".) This range is never empty, where a text insertion's is.
	change.fSourceStart = source.ModelIndex(sStart);
	change.fSourceEnd   = source.ModelIndex(sStart + sCount);

	std::string newPre, newMid, newPost, oldPre, oldMid, oldPost;
	Slice(target.fText, target.Bytes(), tStart, tCount, kContextCodePoints, newPre, newMid, newPost);
	Slice(source.fText, source.Bytes(), sStart, sCount, kContextCodePoints, oldPre, oldMid, oldPost);

	// The readings go through the same door as the base text: they are document text too.
	SetDocumentText(change.fTextPre, newPre);
	SetDocumentText(change.fText, newMid);
	SetDocumentText(change.fTextPost, newPost);
	SetDocumentText(change.fOtherTextPre, oldPre);
	SetDocumentText(change.fOtherText, oldMid);
	SetDocumentText(change.fOtherTextPost, oldPost);
	SetDocumentText(change.fRuby, newRuby);
	SetDocumentText(change.fOtherRuby, oldRuby);

	out.push_back(change);
}

/* CompareParagraphAttr
   One ATTRIBUTE's spans, on two paragraphs whose TEXT came out identical.

   **SPANS ARE MATCHED BY WHERE THEY START.** The text is the same on both sides, so a reading
   that stayed put keeps its position -- which makes the start the one thing that reliably
   identifies "the same ruby" across the two versions. Length is NOT part of the matching: it
   is part of what changed (琥珀 read as こ+はく against こはく is a change of length, and
   the whole point).
*/
void CompareParagraphAttr(KCMStoryAttrKind attrKind,
						  const KCMAttrSpanList& sourceSpans, const KCMAttrSpanList& targetSpans,
						  ParaSide& source, ParaSide& target, int32 paraIndex,
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
			const bool16 same = (sourceSpans[i].fValue == targetSpans[j].fValue &&
								 sourceSpans[i].fLen == targetSpans[j].fLen &&
								 (sourceSpans[i].fGroup != 0) == (targetSpans[j].fGroup != 0)) ? kTrue : kFalse;
			if (!same)
			{
				AddAttrChange(KCMStoryChange::kReplace, attrKind,
							  targetSpans[j].fStart, targetSpans[j].fLen,
							  sourceSpans[i].fStart, sourceSpans[i].fLen,
							  target, source,
							  targetSpans[j].fValue, sourceSpans[i].fValue,
							  paraIndex, out);
			}
			++i;
			++j;
		}
		else if (haveT && (!haveS || targetSpans[j].fStart < sourceSpans[i].fStart))
		{
			// Ruby where there was none.
			AddAttrChange(KCMStoryChange::kInsert, attrKind,
						  targetSpans[j].fStart, targetSpans[j].fLen,
						  targetSpans[j].fStart, targetSpans[j].fLen,
						  target, source,
						  targetSpans[j].fValue, std::string(),
						  paraIndex, out);
			++j;
		}
		else
		{
			// Ruby taken off. @warning the characters are still there -- it is the reading that is
			//   gone -- so the range is a real one on both sides, unlike a text deletion.
			AddAttrChange(KCMStoryChange::kDelete, attrKind,
						  sourceSpans[i].fStart, sourceSpans[i].fLen,
						  sourceSpans[i].fStart, sourceSpans[i].fLen,
						  target, source,
						  std::string(), sourceSpans[i].fValue,
						  paraIndex, out);
			++i;
		}
	}
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
*/
KCMAttrSpanList SpansWhoseTextSurvives(const KCMAttrSpanList& spans,
									   const KCMAttrSpanList& otherSpans,
									   ParaSide& own, const std::string& otherPara)
{
	KCMAttrSpanList kept;
	if (spans.empty())
		return kept;		// ★nothing asked of own, so its byte table is not built

	const std::string& ownPara = own.fText;
	const std::vector<int32>& bytes = own.Bytes();

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
		const int32 from = spans[i].fStart;
		const int32 to   = spans[i].fStart + spans[i].fLen;

		// ⚠★★★**THE END OF THE PARAGRAPH IS A POSITION, AND IT HAS NO ENTRY.** bytes holds one
		//   entry per code point, so the boundary AFTER the last character is named by the length of
		//   the text and by nothing else - exactly as Slice's ByteAt says above. This read
		//   `to >= bytes.size()` until 2026-09-04, so a span ending at the last character of its
		//   paragraph always took the "unreadable" way out and was always kept: **a word carrying
		//   ruby or kenten at the end of a line, deleted outright, produced the second row this
		//   function exists to remove**, in defiance of the user's rule that the text is the subject
		//   and the marks its attendants.
		//   ★MEASURED BOTH WAYS on 2026-09-04, because a fix that simply reported less would look
		//     the same from one side: work/kcm-selftest/endruby (「これは銀河」-> 「これは」, the
		//     ruby ON THE LAST TWO CHARACTERS) went from edits=2 to edits=1, while the two controls
		//     did not move - endruby/midruby, the same deletion with text after it, stayed at 1, and
		//     kenten/del-*, a SAME-LENGTH rewrite that also loses its ruby, stayed at 2 (which is
		//     what the rule asks for: nothing was deleted, so the mark's removal is its own edit).
		//   ⚠It went unseen for as long as it did because no resource ended a marked span at a
		//    paragraph's end - the two that existed both mark a word with text after it.
		const int32 codePointCount = static_cast<int32>(bytes.size());
		if (from < 0 || to <= from || from >= codePointCount || to > codePointCount)
		{
			kept.push_back(spans[i]);		// unreadable position - see the warning above
			continue;
		}

		// ⚠MATCHED BY THE TEXT, NOT BY POSITION. After an edit the same characters sit at a
		//  different offset, so a positional test would call every surviving span deleted.
		const int32 fromByte = bytes[from];
		const int32 toByte = (to < codePointCount) ? bytes[to] : static_cast<int32>(ownPara.size());
		const std::string text = ownPara.substr(static_cast<size_t>(fromByte),
												static_cast<size_t>(toByte - fromByte));
		if (text.empty() || otherPara.find(text) != std::string::npos)
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
	auto compareParagraphPair = [&](int32 ai, int32 bi, bool16 onlyWhereTextSurvives)
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
		const KCMAttrSpanList sourceRuby = onlyWhereTextSurvives
			? SpansWhoseTextSurvives(sourceAttrs[ai].fRuby, targetAttrs[bi].fRuby, source, targetParas[bi])
			: sourceAttrs[ai].fRuby;
		const KCMAttrSpanList targetRuby = onlyWhereTextSurvives
			? SpansWhoseTextSurvives(targetAttrs[bi].fRuby, sourceAttrs[ai].fRuby, target, sourceParas[ai])
			: targetAttrs[bi].fRuby;

		CompareParagraphAttr(kKCMStoryAttrRuby, sourceRuby, targetRuby, source, target, bi, out);

		// ★KENTEN IS REPORTED AGAIN (2026-09-01, user's call: "if it can be found, I want to find
		//   it"). It was compared for one day in August and withdrawn, and the withdrawal was never
		//   about the comparison: the KIND it produces travelled in the same field as a ruby's
		//   READING, and the message area drew that name over the older text as though somebody could
		//   read it aloud. What answers that is fAttrKind, which every row and every change already
		//   carries. ⇒ **The mistake was one place asking the wrong question, not this call.**
		const KCMAttrSpanList sourceKenten = onlyWhereTextSurvives
			? SpansWhoseTextSurvives(sourceAttrs[ai].fKenten, targetAttrs[bi].fKenten, source, targetParas[bi])
			: sourceAttrs[ai].fKenten;
		const KCMAttrSpanList targetKenten = onlyWhereTextSurvives
			? SpansWhoseTextSurvives(targetAttrs[bi].fKenten, sourceAttrs[ai].fKenten, target, sourceParas[ai])
			: targetAttrs[bi].fKenten;

		CompareParagraphAttr(kKCMStoryAttrKenten, sourceKenten, targetKenten, source, target, bi, out);
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
	if (!KCMTextRead::ReadStory(sourceStory, sourceParas, sourceAttrs, sourceStarts))
		return kFalse;

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

	for (size_t c = 0; c < paragraphChanges.size(); ++c)
	{
		const KCMTextDiff::Change& change = paragraphChanges[c];

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
			KCMTextDiff::MergeNearbyChanges(fineChanges);

			// **THEN SLIDE EACH RUN TO THE POSITION A READER WOULD PUT IT.** Myers returns A
			//   shortest edit script, not THE one a person would describe, and when the surrounding
			//   text repeats a character the two differ visibly. Reported from the panel:
			//   「旧版です」->「新版です・ここが違います」 came back quoting「す・ここが違いま」
			//   -- starting and ending on す because the run had been rotated one step left, which
			//   costs Myers nothing and costs the reader the whole sentence.
			//   @warning AFTER the merge, not before: merging changes the shape of a run, and
			//     aligning a run that is about to be swallowed would be work thrown away.
			KCMTextDiff::AlignChangeBoundaries(sourceCodePoints, targetCodePoints, fineChanges);
		}

		if (!narrowed)
		{
			// The whole run, as one change. This is where a run lands when the character pass
			// cannot place it - not an error, just a coarser answer.
			Add(out, change.bStart,
				targetText, targetBytes, tRun, 0, static_cast<int32>(targetCodePoints.size()),
				sourceText, sourceBytes, sRun, 0, static_cast<int32>(sourceCodePoints.size()));
			continue;
		}

		for (size_t k = 0; k < fineChanges.size(); ++k)
		{
			const KCMTextDiff::Change& fine = fineChanges[k];
			Add(out, change.bStart,
				targetText, targetBytes, tRun, fine.bStart, fine.bCount,
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
	KCMDeferredProgressBar progress(barTitle, pairedCount);
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
		if (CompareOneStory(UIDRef(targetDB, row->fStoryUID),
							UIDRef(sourceDB, row->fStoryUID), changes))
		{
			// **WRITTEN EVEN WHEN NOTHING DIFFERS.** It used to `continue` here, on the grounds that
			//   writing an empty list changes nothing -- which was true of the CHANGES and false of the
			//   fact that somebody looked. That fact is what lets the row say "None" instead of standing
			//   there mute beside the rows that could not be compared at all.
			KCMStoryList::SetRowChanges(i, changes, kTrue);
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

int32 KCMStoryDiffRun::RunOne(IDataBase* targetDB, IDataBase* sourceDB, int32 rowIndex)
{
	if (targetDB == nil || sourceDB == nil)
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

	// A story with no partner has nothing to be compared against - the same judgement Run reads
	// rather than makes again. The menu greys the item for these rows, so this is the second line
	// of defence. Both kinds (added AND removed); for a removed row the uid above belongs to the
	// SOURCE and must not be handed to the target (see Run's note).
	if (unpaired)
		return -1;

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
	const bool16 compared = CompareOneStory(UIDRef(targetDB, storyUID),
										    UIDRef(sourceDB, storyUID), changes);

	// **WRITTEN EITHER WAY, INCLUDING EMPTY.** What stands under the row after a refresh is what
	//   the documents say now, and "nothing" is a perfectly good thing for them to say -- the row
	//   shows it as "None" (KCMStoryRow::fTextCompared), which is how the reader tells "I have
	//   just repaired this" apart from "this was never looked at".
	if (!compared)
		changes.clear();
	KCMStoryList::SetRowChanges(rowIndex, changes, compared);

	return compared ? static_cast<int32>(changes.size()) : -1;
}

// End, KCMStoryDiffRun.cpp.
