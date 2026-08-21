//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryDiffRun.h for what this is for and what it deliberately does not do.
//
//  The reading helpers below (AppendUtf8 / DecodeEntities / ExtractParagraphs / ParagraphStarts /
//  Join / Slice) came from KohakuTest's KTStoryDiff on 2026-08-20 and work the same way. The
//  comparison itself is NOT a straight port - it does two things KT had no need of:
//
//    1. IT WORKS OUT THE OLDER DOCUMENT'S POSITIONS TOO. KT selected in the front document only.
//       Here a click moves both windows, so the source-side TextIndex has to exist. Nothing extra
//       is diffed for it: KESCMTextDiff::Change already carries the a-side range, and the same
//       paragraph-start arithmetic runs over the older side's paragraphs.
//
//    2. IT CHOOSES WHICH SIDE'S WORDS TO SHOW. KT reported the newer text and left a deletion
//       blank, which is right for a report ("what is gone is precisely what is not there") and
//       useless in a panel, where it is a row with nothing in it. A deletion here shows the text
//       that was REMOVED, taken from the older side.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"		// SaveRestoreModifiedState - see Run()
#include "ITextModel.h"

// General includes:
#include "PMString.h"
#include "UIDRef.h"

#include <string>
#include <vector>

// Project includes:
#include "KESCMStoryDiffRun.h"
#include "KESCMStoryList.h"
#include "KESCMStoryStamp.h"	// kKESCMStoryKindAdded - which rows have no partner to compare against
#include "KESCMStoryXml.h"
#include "KESCMTextDiff.h"

namespace
{

/** How many code points of a change to keep for the row, INCLUDING the context on either side.
	The cell ellipsizes in the middle, so this only has to be short enough not to carry a paragraph
	around in memory per change. */
const int32 kExcerptCodePoints = 60;

/** How many code points to keep on EACH SIDE of a change (user's request, 2026-08-20 - the same
	context a KBS hit row shows). ★Narrow on purpose: two of these plus the change itself has to
	stay under kExcerptCodePoints, or the context would push the change out of the cell - which is
	the opposite of what it is for. */
const int32 kContextCodePoints = 14;

// ---- reading the text out of the snippet ----------------------------------------------

void AppendUtf8(std::string& out, int32 codePoint)
{
	if (codePoint < 0x80)
	{
		out += static_cast<char>(codePoint);
	}
	else if (codePoint < 0x800)
	{
		out += static_cast<char>(0xC0 | (codePoint >> 6));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
	else if (codePoint < 0x10000)
	{
		out += static_cast<char>(0xE0 | (codePoint >> 12));
		out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (codePoint >> 18));
		out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
}

/* DecodeEntities
   Only the five XML built-ins and numeric references can appear here; anything else is left as it
   stands rather than guessed at, because a wrong guess would shift every position after it by the
   difference in length.
*/
void DecodeEntities(std::string& text)
{
	if (text.find('&') == std::string::npos)
		return;

	std::string out;
	out.reserve(text.size());

	size_t i = 0;
	while (i < text.size())
	{
		if (text[i] != '&')
		{
			out += text[i];
			++i;
			continue;
		}

		const size_t semi = text.find(';', i);
		if (semi == std::string::npos || semi - i > 12)
		{
			out += text[i];
			++i;
			continue;
		}

		const std::string name = text.substr(i + 1, semi - i - 1);
		if (name == "amp")
			out += '&';
		else if (name == "lt")
			out += '<';
		else if (name == "gt")
			out += '>';
		else if (name == "quot")
			out += '"';
		else if (name == "apos")
			out += '\'';
		else if (name.size() > 1 && name[0] == '#')
		{
			const bool16 hex = (name[1] == 'x' || name[1] == 'X');
			const std::string digits = name.substr(hex ? 2 : 1);
			int32 value = 0;
			bool16 ok = !digits.empty();
			for (size_t d = 0; d < digits.size() && ok; ++d)
			{
				const char c = digits[d];
				int32 digit = -1;
				if (c >= '0' && c <= '9')				digit = c - '0';
				else if (hex && c >= 'a' && c <= 'f')	digit = c - 'a' + 10;
				else if (hex && c >= 'A' && c <= 'F')	digit = c - 'A' + 10;
				if (digit < 0)
					ok = kFalse;
				else
					value = value * (hex ? 16 : 10) + digit;
			}
			if (ok)
				AppendUtf8(out, value);
			else
				out.append(text, i, semi - i + 1);
		}
		else
		{
			out.append(text, i, semi - i + 1);
		}

		i = semi + 1;
	}

	text.swap(out);
}

/* ExtractParagraphs
   Reads the story's text out of the snippet: <Content> holds it, <Br /> ends a paragraph.

   ★Only the region between <Story and </Story> is looked at. The snippet also carries every
   object the story depends on - inks, fonts, styles, cross-reference formats - and some of those
   have text of their own that must not be mistaken for the story's. (Measured in KohakuTest:
   the dependencies are more than eight tenths of the file and contribute nothing to the diff.)
*/
void ExtractParagraphs(const std::string& xml, std::vector<std::string>& paragraphs)
{
	paragraphs.clear();

	const size_t storyStart = xml.find("<Story ");
	const size_t storyEnd = xml.rfind("</Story>");
	if (storyStart == std::string::npos || storyEnd == std::string::npos || storyEnd < storyStart)
		return;

	std::string current;
	size_t pos = storyStart;

	while (pos < storyEnd)
	{
		const size_t lt = xml.find('<', pos);
		if (lt == std::string::npos || lt >= storyEnd)
			break;

		if (xml.compare(lt, 9, "<Content>") == 0)
		{
			const size_t close = xml.find("</Content>", lt);
			if (close == std::string::npos || close > storyEnd)
				break;
			current.append(xml, lt + 9, close - (lt + 9));
			pos = close + 10;
		}
		else if (xml.compare(lt, 4, "<Br ") == 0 || xml.compare(lt, 4, "<Br/") == 0)
		{
			DecodeEntities(current);
			paragraphs.push_back(current);
			current.clear();

			const size_t gt = xml.find('>', lt);
			pos = (gt == std::string::npos) ? storyEnd : gt + 1;
		}
		else
		{
			pos = lt + 1;
		}
	}

	// The last paragraph has no <Br /> after it.
	DecodeEntities(current);
	paragraphs.push_back(current);
}

/* ParagraphStarts
   Where each paragraph begins, counted in CODE POINTS - the unit InDesign counts text positions
   in, so a number worked out here can be handed to the text model unchanged (a surrogate pair is
   one TextIndex). Every paragraph is followed by one break character, including the last: a story
   always ends with one.
*/
void ParagraphStarts(const std::vector<std::string>& paragraphs,
					 std::vector<int32>& starts, int32& total)
{
	starts.clear();
	starts.reserve(paragraphs.size());

	int32 index = 0;
	for (size_t i = 0; i < paragraphs.size(); ++i)
	{
		starts.push_back(index);

		std::vector<int32> codePoints;
		KESCMTextDiff::ToCodePoints(paragraphs[i], codePoints);
		index += static_cast<int32>(codePoints.size()) + 1;
	}

	total = index;
}

/* Join
   The text of a run of paragraphs, with the break characters put back, so that offsets found
   inside the joined text can be added straight onto the run's start position.
*/
std::string Join(const std::vector<std::string>& paragraphs, int32 start, int32 count)
{
	std::string out;
	for (int32 i = 0; i < count; ++i)
	{
		const int32 index = start + i;
		if (index < 0 || index >= static_cast<int32>(paragraphs.size()))
			continue;
		if (i > 0)
			out += "\n";
		out += paragraphs[index];
	}
	return out;
}

/* MarkUpBreaks
   Turns the break characters into the marks InDesign itself draws with Show Hidden Characters on,
   so that a row showing text which crosses a paragraph end does not show a gap where the break was.

   ★DISPLAY ONLY - nothing measured or selected ever goes through here. The marks are wider than
   nothing, so a string that has been through this no longer matches the text it came from.

   Same two marks and the same reasoning as KBS's KBSResultModel::MarkUpBreaksForDisplay: a pilcrow
   for a paragraph end, a return arrow for a forced line break. Written on UTF-8 here rather than on
   a PMString's UTF-16 buffer, because at this point in the comparison the text is still the XML's
   own bytes - both marks are outside ASCII, hence the escapes.
*/
std::string MarkUpBreaks(const std::string& utf8)
{
	if (utf8.find('\n') == std::string::npos && utf8.find('\r') == std::string::npos)
		return utf8;		// the common case pays one scan and no allocation

	std::string out;
	out.reserve(utf8.size() + 8);
	for (size_t i = 0; i < utf8.size(); ++i)
	{
		if (utf8[i] == '\n')
			out += "\xC2\xB6";			// U+00B6 PILCROW - a paragraph end (what Join puts between paragraphs)
		else if (utf8[i] == '\r')
			out += "\xE2\x86\xB5";		// U+21B5 DOWNWARDS ARROW WITH CORNER LEFTWARDS - a forced line break
		else
			out += utf8[i];
	}
	return out;
}

/* Slice
   A piece of UTF-8 text named in CODE POINTS, cut on code point boundaries so what comes out is
   still valid UTF-8. byteOffsets is what ToCodePoints filled in for this same string.

   ★IT TAKES THE SURROUNDING WORDS TOO (user's request, 2026-08-20: "KBS のように前後の文字が欲しい").
   A change on its own reads as a fragment - "awake" says nothing about where it is - so the row
   shows what stands on either side of it, the way a KBS hit row shows its context. An ellipsis marks
   each end that was cut, so a fragment is never mistaken for the whole of something.

   ★★IT HANDS BACK THREE PIECES, NOT ONE (user's request, 2026-08-20: the row is to draw the
   changed characters at full strength and fade the context around them, the way a KBS hit row
   draws its match). The split has to be made HERE: by the time the row is drawn, the only thing
   that knows which characters were the change is this function - the boundary between the context
   and the change is a code point index into a string that has already been cut at both ends and
   had its break characters replaced. Handing over one string plus an offset would ask the panel to
   count code points in a PMString, whose own index is UTF-16.

   @param from/count name the CHANGE, in code points, within text.
   @param context how many code points to keep on each side. 0 = the change alone.
   @param outPre [out] what stands before the change, with a leading ellipsis when the text was cut
	  there. Empty when the change begins the text.
   @param outMid [out] the changed characters themselves - what the row draws at full strength.
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

	// ★A DELETION HAS count == 0 ON THE SIDE THAT LOST IT, and it still has a place - the words that
	//   closed up over it. So an empty range is not refused here; only an empty RESULT is.
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
	// ★Counted on the WHOLE excerpt, exactly as it was when this returned one string: the cut lands
	//   at the same code point it always did. It can fall inside the change itself when a single
	//   change is longer than the whole allowance - which is the honest outcome, since the change is
	//   what the excerpt is for. Both boundaries are pulled back with it so that no piece can end up
	//   naming a range outside the one being kept.
	//   ⚠THE TRAILING ELLIPSIS THIS FORCES WAS NEVER DRAWN UNTIL 2026-08-20. The old code said
	//     "last = total; // force the trailing ellipsis", and the test just below it is
	//     "if (last < total)" - so setting last TO total turned the ellipsis OFF, and turned it off
	//     even for an excerpt that would have carried one anyway. A flag says it instead, because a
	//     flag cannot be read as its own opposite.
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

	// ★Each piece goes through MarkUpBreaks on its own. The marks replace one break character each,
	//   so nothing straddles a boundary and the three marked-up pieces read exactly as the one
	//   marked-up string used to.
	outPre = MarkUpBreaks(text.substr(beginByte, midFromByte - beginByte));
	outMid = MarkUpBreaks(text.substr(midFromByte, midToByte - midFromByte));
	outPost = MarkUpBreaks(text.substr(midToByte, endByte - midToByte));

	// ★The ellipses say "this is a window onto something longer". Without them the reader cannot
	//   tell a change that begins a paragraph from one that merely appears to.
	//   They belong to the CONTEXT pieces, which is also where they belong visually: an ellipsis
	//   stands for words that were cut away, and those words are context, never the change.
	if (first > 0)
		outPre = "\xE2\x80\xA6" + outPre;	// U+2026 HORIZONTAL ELLIPSIS
	if (last < total || truncated)
		outPost += "\xE2\x80\xA6";
}

/* LengthAgrees
   ★THE CHECK THAT KEEPS A WRONG SELECTION FROM LOOKING LIKE A RIGHT ONE.

   Every position handed out below is counted from the XML. If the XML and the text model disagree
   about how long the story is, every one of them is off by the difference - and a selection that
   lands on the wrong words looks exactly like one that landed on the right words. There is no
   symptom to notice later, so it is caught here instead.

   Measured in KohakuTest at 12,987 against 12,987 on a 60-paragraph story; this is the guard for
   the case where that stops being true (a construct the paragraph reader does not know about).
*/
bool16 LengthAgrees(const UIDRef& storyRef, int32 computed)
{
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	return model->TotalLength() == computed;
}

// ---- one story --------------------------------------------------------------------------

/* Add
   Builds one change and appends it. Kept in one place so that the two callers below - a run that
   was narrowed down to characters, and one that was not - cannot describe the same thing in two
   different ways.

   @param tFrom/tCount, sFrom/sCount are offsets WITHIN the joined run, in code points.
*/
void Add(std::vector<KESCMStoryChange>& out, int32 paraIndex,
		 const std::string& targetText, const std::vector<int32>& targetBytes,
		 int32 tBase, int32 tFrom, int32 tCount,
		 const std::string& sourceText, const std::vector<int32>& sourceBytes,
		 int32 sBase, int32 sFrom, int32 sCount)
{
	KESCMStoryChange change;
	change.fParaIndex = paraIndex;
	change.fWhat = KESCMStoryChange::kText;

	change.fKind = (sCount == 0) ? KESCMStoryChange::kInsert
				 : (tCount == 0) ? KESCMStoryChange::kDelete
								 : KESCMStoryChange::kReplace;

	change.fTargetStart = tBase + tFrom;
	change.fTargetEnd = change.fTargetStart + tCount;

	// ★An insertion has nothing in the older document to point at. Saying "here is where it
	//   would have been" would be pointing at text that is not the change.
	change.fHasSource = (sCount > 0);
	if (change.fHasSource)
	{
		change.fSourceStart = sBase + sFrom;
		change.fSourceEnd = change.fSourceStart + sCount;
	}

	// ★BOTH SIDES ARE CUT, ALWAYS (2026-08-20). The row shows the side that changed; the panel's
	//   message area shows the other one while that row is selected, so that the reader can see
	//   what the words used to be (or, for a deletion, what stands there now).
	std::string newPre, newMid, newPost;
	Slice(targetText, targetBytes, tFrom, tCount, kContextCodePoints, newPre, newMid, newPost);

	std::string oldPre, oldMid, oldPost;
	Slice(sourceText, sourceBytes, sFrom, sCount, kContextCodePoints, oldPre, oldMid, oldPost);

	// ★A deletion shows the OLDER side on the row: what was taken out is what the reader has to
	//   see, and the newer side has nothing there to show. Everything else shows the newer side.
	//   ⇒ Whichever of the two that is, the OTHER one goes to fOtherText - see KESCMStoryList.h for
	//     why it is named that rather than "old".
	const bool16 rowShowsOldSide = (change.fKind == KESCMStoryChange::kDelete);

	change.fTextPre.SetUTF8String(rowShowsOldSide ? oldPre : newPre);
	change.fText.SetUTF8String(rowShowsOldSide ? oldMid : newMid);
	change.fTextPost.SetUTF8String(rowShowsOldSide ? oldPost : newPost);

	change.fOtherTextPre.SetUTF8String(rowShowsOldSide ? newPre : oldPre);
	change.fOtherText.SetUTF8String(rowShowsOldSide ? newMid : oldMid);
	change.fOtherTextPost.SetUTF8String(rowShowsOldSide ? newPost : oldPost);

	// ★Text out of a document is not a translation key. Without this it can be looked up in the
	//   string tables and come back as something else entirely (memory menu-string-translation-traps).
	//   All six pieces, not just the middles: they are all document text, and the context pieces are
	//   the ones most likely to be a short common word that a table has an entry for.
	change.fTextPre.SetTranslatable(kFalse);
	change.fText.SetTranslatable(kFalse);
	change.fTextPost.SetTranslatable(kFalse);
	change.fOtherTextPre.SetTranslatable(kFalse);
	change.fOtherText.SetTranslatable(kFalse);
	change.fOtherTextPost.SetTranslatable(kFalse);

	out.push_back(change);
}

/* CompareOneStory
   Fills out with everything that differs between the two versions of one story. kFalse means the
   story could not be compared at all; an empty out with kTrue means it was compared and the text
   is identical (the counters also move for formatting).
*/
bool16 CompareOneStory(const UIDRef& targetStory, const UIDRef& sourceStory,
					   std::vector<KESCMStoryChange>& out)
{
	out.clear();

	std::string targetXml;
	std::string sourceXml;
	if (!KESCMStoryXml::ExportStory(targetStory, targetXml))
		return kFalse;
	if (!KESCMStoryXml::ExportStory(sourceStory, sourceXml))
		return kFalse;

	std::vector<std::string> targetParas;
	std::vector<std::string> sourceParas;
	ExtractParagraphs(targetXml, targetParas);
	ExtractParagraphs(sourceXml, sourceParas);

	// ★ONE TABLE FOR BOTH SEQUENCES. Numbering them from separate tables would give equal
	//   paragraphs different tokens, and every paragraph would look changed.
	std::vector<std::string> table;
	std::vector<int32> sourceTokens;
	std::vector<int32> targetTokens;
	KESCMTextDiff::Tokenize(sourceParas, table, sourceTokens);
	KESCMTextDiff::Tokenize(targetParas, table, targetTokens);

	std::vector<KESCMTextDiff::Change> paragraphChanges;
	if (!KESCMTextDiff::Diff(sourceTokens, targetTokens, paragraphChanges))
		return kFalse;		// too different to place - the row stays, the detail does not

	std::vector<int32> targetStarts;
	std::vector<int32> sourceStarts;
	int32 targetComputed = 0;
	int32 sourceComputed = 0;
	ParagraphStarts(targetParas, targetStarts, targetComputed);
	ParagraphStarts(sourceParas, sourceStarts, sourceComputed);

	// ★BOTH SIDES ARE CHECKED. KohakuTest checked only the side it selected in; here a click
	//   moves both windows, so a mismatch on the older side would aim the older window wrongly.
	if (!LengthAgrees(targetStory, targetComputed) || !LengthAgrees(sourceStory, sourceComputed))
		return kFalse;

	for (size_t c = 0; c < paragraphChanges.size(); ++c)
	{
		const KESCMTextDiff::Change& change = paragraphChanges[c];

		const std::string sourceText = Join(sourceParas, change.aStart, change.aCount);
		const std::string targetText = Join(targetParas, change.bStart, change.bCount);

		// Where this run starts on each side. A run with no paragraphs of its own sits where the
		// next surviving paragraph begins.
		const int32 tBase = (change.bStart < static_cast<int32>(targetStarts.size()))
							? targetStarts[change.bStart] : targetComputed;
		const int32 sBase = (change.aStart < static_cast<int32>(sourceStarts.size()))
							? sourceStarts[change.aStart] : sourceComputed;

		std::vector<int32> sourceCodePoints;
		std::vector<int32> targetCodePoints;
		std::vector<int32> sourceBytes;
		std::vector<int32> targetBytes;
		KESCMTextDiff::ToCodePoints(sourceText, sourceCodePoints, &sourceBytes);
		KESCMTextDiff::ToCodePoints(targetText, targetCodePoints, &targetBytes);

		// The second pass: narrow the run down to the characters that actually differ, so that a
		// one-word edit selects the word and not the paragraph it sits in.
		std::vector<KESCMTextDiff::Change> fineChanges;
		const bool16 narrowed = KESCMTextDiff::Diff(sourceCodePoints, targetCodePoints, fineChanges)
								&& !fineChanges.empty();

		// ★ONLY ON CHARACTERS. Applied to the paragraph list this would report paragraphs nobody
		//   touched as changed - see KESCMTextDiff.h.
		if (narrowed)
			KESCMTextDiff::MergeNearbyChanges(fineChanges);

		if (!narrowed)
		{
			// The whole run, as one change. This is where a run lands when the character pass
			// cannot place it - not an error, just a coarser answer.
			Add(out, change.bStart,
				targetText, targetBytes, tBase, 0, static_cast<int32>(targetCodePoints.size()),
				sourceText, sourceBytes, sBase, 0, static_cast<int32>(sourceCodePoints.size()));
			continue;
		}

		for (size_t k = 0; k < fineChanges.size(); ++k)
		{
			const KESCMTextDiff::Change& fine = fineChanges[k];
			Add(out, change.bStart,
				targetText, targetBytes, tBase, fine.bStart, fine.bCount,
				sourceText, sourceBytes, sBase, fine.aStart, fine.aCount);
		}
	}

	return kTrue;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// Run
//----------------------------------------------------------------------------------------

int32 KESCMStoryDiffRun::Run(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (targetDB == nil || sourceDB == nil)
		return 0;

	// ★THE GUARD BELONGS HERE, NOT AT THE CALLER - see the header for the two callers and which
	//   one of them lacks it. Exporting a snippet can compose (asking for text that has never been
	//   laid out lays it out), and composing sets the modified flag on a document this feature only
	//   ever reads. KESCM's whole premise is that comparing changes nothing.
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

	int32 total = 0;

	const int32 rowCount = KESCMStoryList::GetRowCount();
	for (int32 i = 0; i < rowCount; ++i)
	{
		const KESCMStoryRow* row = KESCMStoryList::GetRow(i);
		if (row == nil || row->fStoryUID == kInvalidUID)
			continue;

		// ★An added story has no partner to compare against, and the list already knows that -
		//   this is the same judgement KESCMStoryStamp made when it built the row, read rather
		//   than made again. Asking the older document for the UID ourselves would be a second
		//   place where "does this story exist over there" gets answered.
		if ((row->fKinds & kKESCMStoryKindAdded) != 0)
			continue;

		std::vector<KESCMStoryChange> changes;
		if (!CompareOneStory(UIDRef(targetDB, row->fStoryUID),
							 UIDRef(sourceDB, row->fStoryUID), changes))
			continue;		// the row keeps its place and loses its detail

		if (changes.empty())
			continue;		// compared, and the text is the same - a formatting-only edit

		KESCMStoryList::SetRowChanges(i, changes);
		total += static_cast<int32>(changes.size());
	}

	return total;
}

// End, KESCMStoryDiffRun.cpp.
