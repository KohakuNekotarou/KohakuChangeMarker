//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryDiffRun.h for what this is for and what it deliberately does not do.
//
//  The reading helpers (ParagraphStarts / Join / Slice, and the XML parsing that now lives in
//  KCMSnippetText.h) came from KohakuTest's KTStoryDiff and work the same way. The comparison
//  itself is NOT a straight port -- it does two things KT had no need of:
//
//    1. IT WORKS OUT THE OLDER DOCUMENT'S POSITIONS TOO. KT selected in the front document
//       only. Here a click moves both windows, so the source-side TextIndex has to exist.
//       Nothing extra is diffed for it: KCMTextDiff::Change already carries the a-side range,
//       and the same paragraph-start arithmetic runs over the older side's paragraphs.
//
//    2. IT CHOOSES WHICH SIDE'S WORDS TO SHOW. KT reported the newer text and left a deletion
//       blank, which is right for a report ("what is gone is precisely what is not there") and
//       useless in a panel, where it is a row with nothing in it. A deletion here shows the
//       text that was REMOVED, taken from the older side.
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
#include "KCMSnippetText.h"	// the snippet's text AND its ruby - see the note at "reading the text"
#include "KCMStoryDiffRun.h"
#include "KCMStoryCellBases.h"	// where a table's cells REALLY are -- see the note at LengthAgrees
#include "KCMStoryList.h"
#include "KCMStoryStamp.h"	// kKCMStoryKindAdded - which rows have no partner to compare against
#include "KCMStoryXml.h"
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

// ---- reading the text out of the snippet ----------------------------------------------
//
// AppendUtf8 / DecodeEntities / ExtractParagraphs live in KCMSnippetText.h. They turn one
// string into another and touch nothing else, so out there they can be measured without
// starting InDesign -- which is what RUBY made necessary: the parsing went from "find
// <Content>" to a small XML reader carrying state, and a mistake in that is invisible from
// here (a ruby missed reads as "nothing changed", which is the very bug this was written for).
// Test = work\kescm-snippet-test, which builds those headers as they stand.

/* ParagraphStarts
   Where each paragraph begins, counted in CODE POINTS - the unit InDesign counts text positions
   in, so a number worked out here can be handed to the text model unchanged (a surrogate pair is
   one TextIndex). Every paragraph is followed by one break character, including the last: a story
   always ends with one.
*/
void ParagraphStarts(const std::vector<std::string>& paragraphs,
					 const std::vector<KCMParaAttrs>& attrs,
					 std::vector<int32>& starts, int32& total)
{
	starts.clear();
	starts.reserve(paragraphs.size());

	int32 index = 0;
	for (size_t i = 0; i < paragraphs.size(); ++i)
	{
		// **AND THE ONES STANDING IN FRONT OF IT.** A story that BEGINS with a table -- a frame
		//   holding nothing but a table, which is the ordinary way to make one -- has the table's
		//   character before its first paragraph, where there is no earlier paragraph to charge it
		//   to. It used to be dropped, the story counted one short, and LengthAgrees below then
		//   refused the whole thing (no text differences, no ruby). Only the first paragraph can
		//   carry this -- see KCMParaAttrs::fLeadingChars.
		if (i < attrs.size())
			index += attrs[i].fLeadingChars;

		starts.push_back(index);

		std::vector<int32> codePoints;
		KCMTextDiff::ToCodePoints(paragraphs[i], codePoints);
		index += static_cast<int32>(codePoints.size()) + 1;

		// **CHARACTERS THE TEXT MODEL COUNTS THAT THE TEXT DOES NOT SHOW** -- a table's own
		//   character, and one at the end of every row but the last. Without them every position
		//   after a table is short by that much, and LengthAgrees below refuses the whole story
		//   rather than aim a jump at the wrong words. See KCMParaAttrs::fExtraChars.
		if (i < attrs.size())
			index += attrs[i].fExtraChars;
	}

	total = index;
}

// Join lives in KCMSnippetText.h as JoinParagraphs. It is one half of a convention -- how far
// apart two paragraphs are once they have been strung together -- and the other half
// (IndexInStory, which turns an offset back into a document position) has to agree with it
// EXACTLY. While they sat in different files only one of them knew that a table puts extra
// characters at a paragraph boundary, and a change spanning such a boundary was placed one
// character early. Both are now in the one header the test harness can build without InDesign.

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
	// item stands (measured 2026-08-23; see AnchoredItemTagLen in KCMSnippetText.h).
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

/* LengthAgrees
   **THE CHECK THAT KEEPS A WRONG SELECTION FROM LOOKING LIKE A RIGHT ONE.**

   Every position handed out below is counted from the XML. If the XML and the text model
   disagree about how long the story is, every one of them is off by the difference -- and a
   selection that lands on the wrong words looks exactly like one that landed on the right
   words. There is no symptom to notice later, so it is caught here instead.

   Measured in KohakuTest at 12,987 against 12,987 on a 60-paragraph story; this is the guard
   for the case where that stops being true (a construct the paragraph reader does not know
   about).
*/
bool16 LengthAgrees(const UIDRef& storyRef, int32 computed)
{
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	return model->TotalLength() == computed;
}

// ---- one story --------------------------------------------------------------------------

/* RunSide
   One run of paragraphs on ONE of the two sides, and the one thing every change asks of it:
   where an offset into the run's joined text lands in the document.

   **IT REPLACED A BARE `base`, AND THAT IS THE WHOLE OF THE FIX.** `base + offset` is right
   only while the joined text and the document agree about how far apart two paragraphs are,
   and a table makes them disagree -- its own character and its row terminators sit exactly at
   a paragraph boundary (KCMParaAttrs::fExtraChars). A change covering two adjacent paragraphs
   with one of those between them came out short, silently, and no length check could see it.
   The run is asked instead of counted on, and the rule it answers with lives beside
   JoinParagraphs in KCMSnippetText.h, where the two ends of the convention can be measured
   against each other.
*/
struct RunSide
{
	const std::vector<std::string>*		fParagraphs;
	const std::vector<int32>*			fStarts;	// one document position per paragraph
	int32								fStart;		// first paragraph of the run
	int32								fCount;		// how many it covers
	int32								fBase;		// where the run begins, as a TextIndex

	RunSide(const std::vector<std::string>& paragraphs, const std::vector<int32>& starts,
			int32 start, int32 count, int32 base)
		: fParagraphs(&paragraphs), fStarts(&starts), fStart(start), fCount(count), fBase(base) {}

	int32 Index(int32 joinedOffset) const
	{
		return KCMSnippetText::IndexInStory(*fParagraphs, *fStarts, fStart, fCount, fBase, joinedOffset);
	}
};

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

	// A deletion shows the OLDER side on the row: what was taken out is what the reader has to
	//   see, and the newer side has nothing there to show. Everything else shows the newer side.
	//   Whichever of the two that is, the OTHER one goes to fOtherText -- see KCMStoryList.h for
	//     why it is named that rather than "old".
	const bool16 rowShowsOldSide = (change.fKind == KCMStoryChange::kDelete);

	change.fTextPre.SetUTF8String(rowShowsOldSide ? oldPre : newPre);
	change.fText.SetUTF8String(rowShowsOldSide ? oldMid : newMid);
	change.fTextPost.SetUTF8String(rowShowsOldSide ? oldPost : newPost);

	change.fOtherTextPre.SetUTF8String(rowShowsOldSide ? newPre : oldPre);
	change.fOtherText.SetUTF8String(rowShowsOldSide ? newMid : oldMid);
	change.fOtherTextPost.SetUTF8String(rowShowsOldSide ? newPost : oldPost);

	// Text out of a document is not a translation key. Without this it can be looked up in the
	//   string tables and come back as something else entirely (memory
	//   menu-string-translation-traps). All six pieces, not just the middles: they are all
	//   document text, and the context pieces are the ones most likely to be a short common word
	//   that a table has an entry for.
	change.fTextPre.SetTranslatable(kFalse);
	change.fText.SetTranslatable(kFalse);
	change.fTextPost.SetTranslatable(kFalse);
	change.fOtherTextPre.SetTranslatable(kFalse);
	change.fOtherText.SetTranslatable(kFalse);
	change.fOtherTextPost.SetTranslatable(kFalse);

	out.push_back(change);
}

/* AddAttrChange
   One ATTRIBUTE difference -- a ruby today -- turned into the child row that reports it.

   **THE BASE TEXT IS SHOWN FROM THE NEWER SIDE, ALWAYS** -- unlike a text change, where a
   deletion has to be shown from the older side because the newer one has nothing there. An
   attribute is different: the characters are in BOTH versions and only what sits over them
   changed, so the newer side always has something to show and there is no case to branch on.

   **IT TAKES attrKind RATHER THAN ASSUMING RUBY**, and that was kept after kenten was
   withdrawn. What an attribute's VALUE means is not the same for all of them -- a ruby's is a
   READING and a kenten's was a KIND ("KentenBlackCircle") -- and the field they travel in is
   the same one, so whoever draws it has to be told which it is looking at. Filling that in
   here is what let the mistake be a one-line one when it happened, in the single place that
   asked the wrong question (KCMStoryJump's message area).
*/
void AddAttrChange(KCMStoryChange::Kind kind, KCMStoryAttrKind attrKind,
				   int32 tStart, int32 tCount, int32 sStart, int32 sCount,
				   const std::string& targetPara, const std::string& sourcePara,
				   const std::string& newRuby, const std::string& oldRuby,
				   int32 tBase, int32 sBase, int32 paraIndex,
				   std::vector<KCMStoryChange>& out)
{
	KCMStoryChange change;
	change.fKind = kind;
	change.fWhat = KCMStoryChange::kAttr;		// the field that has waited for exactly this
	change.fAttrKind = attrKind;
	change.fParaIndex = paraIndex;

	change.fTargetStart = tBase + tStart;
	change.fTargetEnd   = change.fTargetStart + tCount;

	// @warning **THE OLDER SIDE ALWAYS HAS CHARACTERS HERE**, unlike a text change: a ruby-only
	//   difference is found by comparing two paragraphs whose TEXT matched, so the same
	//   characters exist on both sides. (Ruby being ADDED is still "these characters, which are
	//   in both, now carry a reading".) This range is never empty, where a text insertion's is.
	change.fSourceStart = sBase + sStart;
	change.fSourceEnd   = change.fSourceStart + sCount;

	std::vector<int32> tCode, tBytes, sCode, sBytes;
	KCMTextDiff::ToCodePoints(targetPara, tCode, &tBytes);
	KCMTextDiff::ToCodePoints(sourcePara, sCode, &sBytes);

	std::string newPre, newMid, newPost, oldPre, oldMid, oldPost;
	Slice(targetPara, tBytes, tStart, tCount, kContextCodePoints, newPre, newMid, newPost);
	Slice(sourcePara, sBytes, sStart, sCount, kContextCodePoints, oldPre, oldMid, oldPost);

	change.fTextPre.SetUTF8String(newPre);
	change.fText.SetUTF8String(newMid);
	change.fTextPost.SetUTF8String(newPost);
	change.fOtherTextPre.SetUTF8String(oldPre);
	change.fOtherText.SetUTF8String(oldMid);
	change.fOtherTextPost.SetUTF8String(oldPost);
	change.fRuby.SetUTF8String(newRuby);
	change.fOtherRuby.SetUTF8String(oldRuby);

	// Same reason as the text pieces: document text is not a translation key.
	change.fTextPre.SetTranslatable(kFalse);
	change.fText.SetTranslatable(kFalse);
	change.fTextPost.SetTranslatable(kFalse);
	change.fOtherTextPre.SetTranslatable(kFalse);
	change.fOtherText.SetTranslatable(kFalse);
	change.fOtherTextPost.SetTranslatable(kFalse);
	change.fRuby.SetTranslatable(kFalse);
	change.fOtherRuby.SetTranslatable(kFalse);

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
						  const std::string& sourcePara, const std::string& targetPara,
						  int32 sBase, int32 tBase, int32 paraIndex,
						  std::vector<KCMStoryChange>& out)
{
	if (!KCMSnippetText::SpansDiffer(sourceSpans, targetSpans))
		return;

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
							  targetPara, sourcePara,
							  targetSpans[j].fValue, sourceSpans[i].fValue,
							  tBase, sBase, paraIndex, out);
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
						  targetPara, sourcePara,
						  targetSpans[j].fValue, std::string(),
						  tBase, sBase, paraIndex, out);
			++j;
		}
		else
		{
			// Ruby taken off. @warning the characters are still there -- it is the reading that is
			//   gone -- so the range is a real one on both sides, unlike a text deletion.
			AddAttrChange(KCMStoryChange::kDelete, attrKind,
						  sourceSpans[i].fStart, sourceSpans[i].fLen,
						  sourceSpans[i].fStart, sourceSpans[i].fLen,
						  targetPara, sourcePara,
						  std::string(), sourceSpans[i].fValue,
						  tBase, sBase, paraIndex, out);
			++i;
		}
	}
}

/* AddAttrOnlyChanges
   Ruby differences in the paragraphs the text diff said were UNCHANGED.

   **THIS IS WHERE THE WHOLE FEATURE LIVES.** A ruby-only edit leaves the text identical, so
   the paragraph diff reports nothing at all and the row comes out "None" -- which is what the
   reader saw. The paragraphs the diff did NOT mention are exactly the ones that need asking
   about.
   @warning paragraphs that the diff DID report are left alone on purpose: their text changed,
     so they already have children saying so, and ruby that moved with rewritten words is not
     a separate edit the reader needs pointed out.
*/
void AddAttrOnlyChanges(const std::vector<KCMTextDiff::Change>& paragraphChanges,
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
			if (a < static_cast<int32>(sourceAttrs.size()) && b < static_cast<int32>(targetAttrs.size()) &&
				a < static_cast<int32>(sourceStarts.size()) && b < static_cast<int32>(targetStarts.size()))
			{
				// **EACH ATTRIBUTE IS COMPARED ON ITS OWN LIST**, and they cannot be merged into one
				//   pass: two sets of spans are matched by position within their OWN kind.
				//
				// @warning **KENTEN IS NOT COMPARED HERE, AND THAT IS A DECISION, NOT AN OMISSION.** It
				//   WAS compared for one day and the machinery all still stands -- the snippet parser
				//   still reads the spans and the test harness still checks that it reads them rightly,
				//   because that reading cost a snippet from the user to get right and is free to keep
				//   (it comes off the same pass as the ruby). What was removed is the REPORTING: a
				//   kenten-only edit now produces no child row, and its story therefore drops out of the
				//   list on the row filter, exactly as a font-only edit does.
				//   Turning it back on is this one call and its label. Nothing else was taken out.
				CompareParagraphAttr(kKCMStoryAttrRuby,
									 sourceAttrs[a].fRuby, targetAttrs[b].fRuby,
									 sourceParas[a], targetParas[b],
									 sourceStarts[a], targetStarts[b], b, out);
			}
			++a;
			++b;
		}

		if (c < paragraphChanges.size())
		{
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

	std::string targetXml;
	std::string sourceXml;
	if (!KCMStoryXml::ExportStory(targetStory, targetXml))
		return kFalse;
	if (!KCMStoryXml::ExportStory(sourceStory, sourceXml))
		return kFalse;

	// Ruby comes out of the same read as the text, and for the reason spelt out in
	//   KCMSnippetText.h: a comparison is one moment, and asking the live model for ruby instead
	//   would put two moments in one row.
	std::vector<std::string> targetParas;
	std::vector<std::string> sourceParas;
	std::vector<KCMParaAttrs> targetAttrs;
	std::vector<KCMParaAttrs> sourceAttrs;
	KCMSnippetText::ExtractParagraphs(targetXml, targetParas, &targetAttrs);
	KCMSnippetText::ExtractParagraphs(sourceXml, sourceParas, &sourceAttrs);

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
		std::vector<KCMSnippetText::RegionPair> pieces;
		for (size_t c = 0; c < paragraphChanges.size(); ++c)
		{
			const KCMTextDiff::Change& run = paragraphChanges[c];
			KCMSnippetText::SplitRunAtPlaces(sourceAttrs, run.aStart, run.aCount,
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

	std::vector<int32> targetStarts;
	std::vector<int32> sourceStarts;
	int32 targetComputed = 0;
	int32 sourceComputed = 0;
	ParagraphStarts(targetParas, targetAttrs, targetStarts, targetComputed);
	ParagraphStarts(sourceParas, sourceAttrs, sourceStarts, sourceComputed);

	// **BOTH SIDES ARE CHECKED.** KohakuTest checked only the side it selected in; here a click
	//   moves both windows, so a mismatch on the older side would aim the older window wrongly.
	if (!LengthAgrees(targetStory, targetComputed) || !LengthAgrees(sourceStory, sourceComputed))
		return kFalse;

	// **AND THEN THE POSITIONS THEMSELVES ARE ASKED OF THE DOCUMENT**, which REPLACES what
	//   ParagraphStarts just put in the two tables. The count above is still needed -- it is what
	//   LengthAgrees checks -- but it is counted straight down the snippet, and a table's cells
	//   are not where the snippet puts them: the text model keeps them after the whole of the
	//   story's own text (ITableTextContent.h). Counting therefore places everything after a
	//   table wrongly, and no total can show it. See KCMStoryCellBases.h for the measurements.
	//   @warning it refuses stories it cannot match up (a shape the body walk does not
	//     understand, a table whose position the two sides disagree about), and a refusal here
	//     means the same as one above: no differences for this story, rather than differences
	//     aimed at the wrong words.
	if (!KCMResolveParagraphPositions(targetStory, targetParas, targetAttrs, targetStarts)
		|| !KCMResolveParagraphPositions(sourceStory, sourceParas, sourceAttrs, sourceStarts))
		return kFalse;

	for (size_t c = 0; c < paragraphChanges.size(); ++c)
	{
		const KCMTextDiff::Change& change = paragraphChanges[c];

		const std::string sourceText = KCMSnippetText::JoinParagraphs(sourceParas, change.aStart, change.aCount);
		const std::string targetText = KCMSnippetText::JoinParagraphs(targetParas, change.bStart, change.bCount);

		// Where this run starts on each side. A run with no paragraphs of its own sits where the
		// next surviving paragraph begins.
		const int32 tBase = (change.bStart < static_cast<int32>(targetStarts.size()))
							? targetStarts[change.bStart] : targetComputed;
		const int32 sBase = (change.aStart < static_cast<int32>(sourceStarts.size()))
							? sourceStarts[change.aStart] : sourceComputed;

		// The run itself, so that a position inside it can be asked for rather than added up -- see
		//   RunSide. Built once here because both callers of Add below need the same two.
		const RunSide tRun(targetParas, targetStarts, change.bStart, change.bCount, tBase);
		const RunSide sRun(sourceParas, sourceStarts, change.aStart, change.aCount, sBase);

		std::vector<int32> sourceCodePoints;
		std::vector<int32> targetCodePoints;
		std::vector<int32> sourceBytes;
		std::vector<int32> targetBytes;
		KCMTextDiff::ToCodePoints(sourceText, sourceCodePoints, &sourceBytes);
		KCMTextDiff::ToCodePoints(targetText, targetCodePoints, &targetBytes);

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
	AddAttrOnlyChanges(paragraphChanges, sourceParas, targetParas, sourceAttrs, targetAttrs,
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

int32 KCMStoryDiffRun::Run(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (targetDB == nil || sourceDB == nil)
		return 0;

	// **THE GUARD BELONGS HERE, NOT AT THE CALLER** -- see the header for the two callers and
	//   which one of them lacks it. Exporting a snippet can compose (asking for text that has
	//   never been laid out lays it out), and composing sets the modified flag on a document this
	//   feature only ever reads. KCM's whole premise is that comparing changes nothing.
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

	int32 total = 0;

	const int32 rowCount = KCMStoryList::GetRowCount();
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

		std::vector<KCMStoryChange> changes;
		if (!CompareOneStory(UIDRef(targetDB, row->fStoryUID),
							 UIDRef(sourceDB, row->fStoryUID), changes))
			continue;		// the row keeps its place and loses its detail

		// **WRITTEN EVEN WHEN NOTHING DIFFERS.** It used to `continue` here, on the grounds that
		//   writing an empty list changes nothing -- which was true of the CHANGES and false of the
		//   fact that somebody looked. That fact is what lets the row say "None" instead of standing
		//   there mute beside the rows that could not be compared at all.
		KCMStoryList::SetRowChanges(i, changes, kTrue);
		total += static_cast<int32>(changes.size());
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
