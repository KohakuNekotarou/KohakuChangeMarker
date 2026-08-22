//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Reading a story's TEXT and its RUBY out of the snippet XML. Nothing here touches the SDK.
//
//  ★★WHY THIS IS A HEADER OF ITS OWN. It was written inside KESCMStoryDiffRun.cpp, where it could
//  only ever be exercised by running a comparison inside InDesign. What it actually does is turn one
//  string into another, so it can be measured outside - and the moment ruby arrived (2026-08-22) the
//  parsing stopped being "find <Content>" and started being a small XML reader with state, which is
//  exactly the kind of code that earns a test.
//  ⇒ The test is work\kescm-snippet-test, and it includes THIS FILE as it stands - not a copy that
//    can drift, the way KTTextDiff drifted from KESCMTextDiff.
//
//  ★★★WHY RUBY IS READ HERE AND NOT FROM THE TEXT MODEL. The SDK has a direct route
//  (SnpPerformTextAttrRuby::GetRubyStrandInfo: IRubyAttrStrand::GetRubyRun for the run,
//  kTARubyStringBoss for the string), and this file's RubyFlag / RubyString are that same pair seen
//  through the snippet - the flag IS the strand's run, the string IS the attribute's value.
//  The reason to read them HERE is TIME: a comparison is a photograph of one moment, and the text
//  already comes from this snippet. Reading ruby from the live model instead would put two moments
//  in one row - the same fault the 2026-08-21 row refresh was written to prevent ("行は文書を引用して
//  いるので、文書を読み直すものは同じものを読み直さないと1行の中に2つの時点が並ぶ").
//
//  ⚠AN EMPTY RUBY STRING IS NO RUBY, which is the official rule and not an invention here:
//  GetRubyStrandInfo turns the attribute off when the string it read has length 0.
//
//========================================================================================

#ifndef __KESCMSnippetText_h__
#define __KESCMSnippetText_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// nil. ⚠BaseType.h does NOT define it, and this header uses it - without
							//  this line it only compiles when something else (VCPlugInHeaders.h)
							//  has been included first, which is exactly the hidden ordering
							//  dependency the file comment above claims it does not have.

#include <string>
#include <vector>

/** One stretch of characters carrying ONE character attribute, inside one paragraph.

	★POSITIONS ARE CODE POINTS, counted the way InDesign counts text positions, so a number worked
	out here lines up with the paragraph offsets the diff already produces (a surrogate pair is one).

	★★ONE TYPE FOR RUBY AND KENTEN (圏点), since 2026-08-22. They are different mechanisms in the
	SDK - ruby is a STRAND (IRubyAttrStrand, run-based) and kenten is a set of CHARACTER ATTRIBUTES -
	but what the panel needs of them is identical: a stretch of characters, and a value that says
	what is sitting over it. Writing the comparison twice would mean fixing it twice.
	⇒ fValue holds the READING for ruby and the KIND for kenten ("KentenBlackCircle").
*/
struct KESCMAttrSpan
{
	int32		fStart;		// first character of the base text, within its paragraph
	int32		fLen;		// how many characters the attribute covers
	std::string	fValue;		// the reading (ruby) or the kind (kenten), UTF-8. ⚠Never empty

	/** kTrue for GROUP ruby - one reading spread over several base characters (琥珀 -> こはく) -
		against MONO ruby, where each character has its own (琥 -> こ, 珀 -> はく).

		★It is carried because the two are different typesetting, so turning one into the other IS
		a change even when every reading stays the same. InDesign writes it as RubyType="GroupRuby"
		and omits the attribute for mono, so mono is the default here too. The pair is the SDK's
		own: IRubyStyle.h:53-54, kRubyKind_Group / kRubyKind_Mono.
		⚠RUBY ONLY. Kenten has no such distinction - it is per character by nature - so its spans
		  always leave this kFalse, and the comparison then never reports a difference in it. */
	bool16		fGroup;

	KESCMAttrSpan() : fStart(0), fLen(0), fGroup(kFalse) {}
	KESCMAttrSpan(int32 start, int32 len, const std::string& value, bool16 group = kFalse)
		: fStart(start), fLen(len), fValue(value), fGroup(group) {}
};

typedef std::vector<KESCMAttrSpan> KESCMAttrSpanList;

/** Everything one paragraph carries OVER its characters - the attributes a change can hide in
	while the words themselves stay identical (2026-08-22).

	★WHY A STRUCT RATHER THAN ANOTHER OUT-PARAMETER. Ruby was the first, kenten is the second, and
	the parser's signature would grow a parameter for each. This way the parser answers one thing
	per paragraph and a third attribute costs a field, not a new argument at every call site.
	⚠WHAT IS DELIBERATELY NOT IN HERE: applied styles. Finding those was considered and REJECTED
	  (user's call, 2026-08-22: "スタイルの変更は、逆に無視することにしますね ... 見つけないで").
	  A paragraph whose text is unchanged and whose style was swapped keeps reading "None".
*/
struct KESCMParaAttrs
{
	KESCMAttrSpanList	fRuby;
	KESCMAttrSpanList	fKenten;

	/** Characters the text model counts after this paragraph that are not in its text (2026-08-22).

		★★WHY IT EXISTS: TABLES. A table is not made of text, but ITextModel counts it - one
		character for the table itself, one at the end of every row but the last - and every position
		worked out from the XML is off by that much until they are added back. The comparison checks
		exactly this (KESCMStoryDiffRun's LengthAgrees) and refuses the whole story when it does not
		add up, which is why a document with ONE table used to produce no differences at all.

		★MEASURED, NOT ASSUMED (2026-08-22, five documents of 0/4/6/6/8 cells plus a merged one):
		    TotalLength = text + 1 per table + Σ(cell text) + 1 per cell + (rows - 1) per table
		The "+1 per cell" needs nothing here - a cell IS a paragraph, and ParagraphStarts already
		adds one break character per paragraph. What is left over is the table's own character and
		the row terminators, and those are what this carries.
		⚠A merged cell is simply one cell fewer; nothing about the formula changes (measured). */
	int32				fExtraChars;

	KESCMParaAttrs() : fExtraChars(0) {}
};

namespace KESCMSnippetText
{

/** Append one code point to a UTF-8 string. */
inline void AppendUtf8(std::string& out, int32 codePoint)
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

/** Resolve XML entities in place.

	Only the five XML built-ins and numeric references can appear here; anything else is left as it
	stands rather than guessed at, because a wrong guess would shift every position after it by the
	difference in length.
*/
inline void DecodeEntities(std::string& text)
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

/** How many CODE POINTS a UTF-8 string holds - continuation bytes (10xxxxxx) are not counted.

	★This is the unit the whole comparison works in, so a four-byte character counts once here
	exactly as it counts once as a TextIndex.
*/
inline int32 CountCodePoints(const std::string& utf8)
{
	int32 n = 0;
	for (size_t i = 0; i < utf8.size(); ++i)
	{
		if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80)
			++n;
	}
	return n;
}

/** The value of one attribute of a start tag, or "" when the tag does not carry it.

	@param tag the tag WITHOUT its angle brackets, e.g. `CharacterStyleRange RubyFlag="1"`.
	@param name the attribute to look for.
	⚠Matched as ` name="`, with the leading space, so that RubyString is not found inside a longer
	  attribute name that happens to end with it.
*/
inline std::string AttrValue(const std::string& tag, const std::string& name)
{
	const std::string needle = " " + name + "=\"";
	const size_t at = tag.find(needle);
	if (at == std::string::npos)
		return std::string();

	const size_t valueStart = at + needle.size();
	const size_t valueEnd = tag.find('"', valueStart);
	if (valueEnd == std::string::npos)
		return std::string();

	std::string value = tag.substr(valueStart, valueEnd - valueStart);
	DecodeEntities(value);
	return value;
}

/** Read the story's text out of the snippet: <Content> holds it, <Br /> ends a paragraph.

	★Only the region between <Story and </Story> is looked at. The snippet also carries every
	object the story depends on - inks, fonts, styles, cross-reference formats - and some of those
	have text of their own that must not be mistaken for the story's. (Measured in KohakuTest:
	the dependencies are more than eight tenths of the file and contribute nothing to the diff.)

	★★THE ATTRIBUTES ARE COLLECTED ON THE WAY THROUGH (ruby 2026-08-22, kenten the same day). Both
	live on the <CharacterStyleRange> that encloses the text they sit over, so they are read when
	that tag opens and forgotten when it closes. Positions are counted in code points as the text is
	appended, so they line up with the paragraph offsets the diff produces.

	@param xml the snippet.
	@param paragraphs [out] cleared, then filled - one entry per paragraph.
	@param attrsPerPara [out] when not nil: cleared, then filled to the SAME length as paragraphs,
		each entry holding that paragraph's ruby and kenten spans in reading order.
*/
inline void ExtractParagraphs(const std::string& xml,
							  std::vector<std::string>& paragraphs,
							  std::vector<KESCMParaAttrs>* attrsPerPara)
{
	paragraphs.clear();
	if (attrsPerPara != nil)
		attrsPerPara->clear();

	const size_t storyStart = xml.find("<Story ");
	const size_t storyEnd = xml.rfind("</Story>");
	if (storyStart == std::string::npos || storyEnd == std::string::npos || storyEnd < storyStart)
		return;

	std::string current;
	KESCMParaAttrs currentAttrs;		// spans found so far in the paragraph being built
	std::string openRuby;				// the ruby of the CharacterStyleRange we are inside, "" for none
	bool16 openGroup = kFalse;			// ...and whether that one is group ruby
	bool16 openContinues = kFalse;		// ...and whether it CONTINUES the span before it (RubyFlag="2")
	bool16 openStarted = kFalse;		// ...and whether a span was already opened inside THIS range
	std::string openKenten;				// the KentenKind of that same range, "" for none
	int32 paraPos = 0;					// code points appended to `current` so far
	size_t pos = storyStart;

	// ★★★TABLES (2026-08-22). A table lives INSIDE the story - its cells' <Content> sits between the
	//   story's own - so reading every <Content> the way this used to did two things at once: it
	//   glued the cells onto whatever paragraph the table interrupted, and it made the character
	//   count disagree with ITextModel::TotalLength. The second one is what the reader saw: the
	//   comparison's LengthAgrees guard refused the whole story, so a document with a table got NO
	//   text differences at all - and no ruby or kenten either, since those are found after it.
	//
	// ★A CELL IS A PARAGRAPH (user's call, 2026-08-22: "1つのセルを1つの段落様に考えないとかな、
	//   1つのセルのなかで変化しているか、追加か、削除かですよね"). That is all the diff needs: cells
	//   become elements of the same sequence the paragraphs are in, so a rewritten cell comes out as
	//   a change, an added row as insertions and a deleted one as deletions - with no new machinery.
	//
	// ★★HOW MANY CHARACTERS A TABLE IS, measured 2026-08-22 on five documents (0/4/6/6/8 cells) and
	//   checked against a sixth:
	//        TotalLength = text + 1 (the table's own anchor character)
	//                           + Σ(cell contents) + one per cell + (rows - 1)
	//   ⚠The last row has no terminator, which is why it is rows-1 and not rows. All five agreed
	//     exactly; the real document came to 52 against a measured 52.
	int32 tableDepth = 0;				// >0 while inside a table (⚠tables can nest)
	int32 lastCellRow = -1;				// the row the previous cell was in, to spot a row change

	// Finish the paragraph being built and start a new one. ★ONE PLACE, because a paragraph is now
	// ended by three different things - a <Br />, a cell closing, and the end of the story - and the
	// three must agree about what "finish" means (decode, push, push the attributes, reset).
	struct Flush
	{
		static void Do(std::string& current, KESCMParaAttrs& attrs, int32& paraPos,
					   std::vector<std::string>& paragraphs,
					   std::vector<KESCMParaAttrs>* attrsPerPara)
		{
			DecodeEntities(current);
			paragraphs.push_back(current);
			if (attrsPerPara != nil)
				attrsPerPara->push_back(attrs);
			current.clear();
			attrs = KESCMParaAttrs();
			paraPos = 0;
		}
	};

	// Charge one of the text model's invisible characters to the paragraph that has just been
	// finished, so that everything after it is counted from the right place.
	// ⚠It goes on the LAST FINISHED paragraph rather than the one being built: these characters sit
	//   between paragraphs (a table's anchor, a row's terminator), and ParagraphStarts adds each
	//   paragraph's own break AFTER its text.
	struct Charge
	{
		static void One(std::vector<KESCMParaAttrs>* attrsPerPara, int32 n)
		{
			if (attrsPerPara != nil && !attrsPerPara->empty())
				attrsPerPara->back().fExtraChars += n;
		}
	};

	while (pos < storyEnd)
	{
		const size_t lt = xml.find('<', pos);
		if (lt == std::string::npos || lt >= storyEnd)
			break;

		if (xml.compare(lt, 7, "<Table ") == 0)
		{
			const size_t gt = xml.find('>', lt);
			if (gt == std::string::npos || gt > storyEnd)
				break;

			if (tableDepth == 0)
			{
				// ★The story's paragraph stops here - but ONLY IF THERE WAS ONE. A table that begins
				//   a paragraph (which is the ordinary case: the <Br /> before it has just ended the
				//   previous one) leaves nothing to flush, and flushing anyway would invent an empty
				//   paragraph and with it one break character that the text model does not count.
				//   ⚠Measured before and after: 53 against the model's 52, and 21 against 20.
				if (!current.empty())
					Flush::Do(current, currentAttrs, paraPos, paragraphs, attrsPerPara);

				Charge::One(attrsPerPara, 1);		// the table itself, one character
				lastCellRow = -1;
			}
			++tableDepth;
			pos = gt + 1;
		}
		else if (xml.compare(lt, 8, "</Table>") == 0)
		{
			if (tableDepth > 0)
				--tableDepth;
			if (tableDepth == 0)
				lastCellRow = -1;
			pos = lt + 8;
		}
		else if (tableDepth > 0 && xml.compare(lt, 6, "<Cell ") == 0)
		{
			// ★A CELL IS A PARAGRAPH. Nothing is pushed here - the cell's text is collected the same
			//   way any paragraph's is, and </Cell> ends it. What this tag is read for is the ROW:
			//   Name is "column:row", and a change of row means the previous cell was the last one
			//   in its row, which is where the row's terminator character belongs.
			const size_t gt = xml.find('>', lt);
			if (gt == std::string::npos || gt > storyEnd)
				break;

			const std::string tag(xml, lt, gt - lt);
			const std::string name = AttrValue(tag, "Name");
			const size_t colon = name.find(':');
			int32 row = -1;
			if (colon != std::string::npos)
			{
				row = 0;
				for (size_t i = colon + 1; i < name.size(); ++i)
				{
					if (name[i] < '0' || name[i] > '9')
						break;
					row = row * 10 + (name[i] - '0');
				}
			}

			// ⚠The FIRST cell of the table charges nothing: the terminator belongs to the end of a
			//   row, and the last row has none (measured - that is why the formula says rows-1).
			if (row >= 0 && lastCellRow >= 0 && row != lastCellRow)
				Charge::One(attrsPerPara, 1);
			lastCellRow = row;

			pos = gt + 1;
		}
		else if (tableDepth > 0 && xml.compare(lt, 7, "</Cell>") == 0)
		{
			// The cell's text ends here. Its own break character is what ParagraphStarts adds to
			// every paragraph, which is exactly the "+1 per cell" the measurement asked for.
			Flush::Do(current, currentAttrs, paraPos, paragraphs, attrsPerPara);
			pos = lt + 7;
		}
		else if (xml.compare(lt, 9, "<Content>") == 0)
		{
			const size_t close = xml.find("</Content>", lt);
			if (close == std::string::npos || close > storyEnd)
				break;

			const std::string piece(xml, lt + 9, close - (lt + 9));
			current.append(piece);

			// ⚠Measured on the DECODED text: an entity is several bytes and one character, and a
			//   ruby span put at the undecoded offset would drift by the difference.
			std::string decoded = piece;
			DecodeEntities(decoded);
			const int32 pieceLen = CountCodePoints(decoded);

			if (!openRuby.empty() && pieceLen > 0)
			{
				// ★★★WHETHER THIS CONTINUES THE SPAN BEFORE IT IS INDESIGN'S ANSWER, NOT A GUESS
				//   MADE HERE. RubyFlag says it: "1" opens a run, "2" carries the same run onto the
				//   next base character (measured on two real snippets, 2026-08-22 - group ruby
				//   こはく over 琥珀 comes out as flag 1 then flag 2, each range holding one
				//   character and the same RubyString).
				//   ⚠A first attempt fused "adjacent spans with the same reading" instead, which
				//     LOOKS equivalent and is not: two mono rubies that happen to read the same
				//     (各 and 々 both かく) sit next to each other with the same string, and fusing
				//     them would report one ruby where the document has two.
				// ★TWO WAYS TO BE A CONTINUATION, and both are needed:
				//   ① the flag says so (group ruby, whose run crosses ranges);
				//   ② this range has already contributed - one range can hold several <Content>
				//     runs when the base text changes formatting part-way through, and that is one
				//     ruby over one stretch, not two.
				const bool16 continues = (openContinues || openStarted) ? kTrue : kFalse;
				if (continues && !currentAttrs.fRuby.empty() &&
					currentAttrs.fRuby.back().fValue == openRuby &&
					currentAttrs.fRuby.back().fStart + currentAttrs.fRuby.back().fLen == paraPos)
				{
					currentAttrs.fRuby.back().fLen += pieceLen;
				}
				else
				{
					currentAttrs.fRuby.push_back(KESCMAttrSpan(paraPos, pieceLen, openRuby, openGroup));
				}
				openStarted = kTrue;
			}

			// ★★★KENTEN JOINS ADJACENT RANGES, WHERE RUBY MUST NOT (2026-08-22, measured on
			//   work\Snippet_3209A15EF.idms). Ruby needed RubyFlag to tell "the same reading
			//   continues" from "a second reading that happens to read the same" - 各 and 画 both
			//   かく sit side by side and are two rubies. Kenten has no such pair: it is one mark
			//   PER CHARACTER, so two adjacent stretches of the same kind ARE one stretch, and the
			//   range boundary between them says nothing about the document - it is wherever some
			//   OTHER formatting happened to change.
			//   ⇒ Joining is not an optimisation here, it is what makes the comparison stable:
			//     without it, italicising one word inside a kenten run would split the span and be
			//     reported as a kenten change.
			if (!openKenten.empty() && pieceLen > 0)
			{
				if (!currentAttrs.fKenten.empty() &&
					currentAttrs.fKenten.back().fValue == openKenten &&
					currentAttrs.fKenten.back().fStart + currentAttrs.fKenten.back().fLen == paraPos)
				{
					currentAttrs.fKenten.back().fLen += pieceLen;
				}
				else
				{
					currentAttrs.fKenten.push_back(KESCMAttrSpan(paraPos, pieceLen, openKenten));
				}
			}

			paraPos += pieceLen;
			pos = close + 10;
		}
		else if (xml.compare(lt, 21, "<CharacterStyleRange ") == 0)
		{
			const size_t gt = xml.find('>', lt);
			if (gt == std::string::npos || gt > storyEnd)
				break;

			const std::string tag(xml, lt, gt - lt);
			const std::string flag = AttrValue(tag, "RubyFlag");
			const std::string str  = AttrValue(tag, "RubyString");

			// ⚠★★★RubyFlag IS NOT A BOOLEAN. GROUP ruby is written as one range per base character,
			//   every one carrying the same RubyString, with the flag going "1" on the first and
			//   "2" on the second (measured 2026-08-22 on こはく over 琥珀). Reading it as "on = 1"
			//   drops every character of a group ruby except the first - and the mono snippet, which
			//   has nothing but "1" in it, could never have shown that.
			// ★★SETTLED BY MEASUREMENT (2026-08-22). On two base characters, "the run continues" and
			//   "this is character number 2" predict the same file, so the first sample could not
			//   tell them apart. A FIVE-character group ruby did: こはくねこたろう over 琥珀猫太郎
			//   came out **1, 2, 2, 2, 2** - so the flag says OPEN or CONTINUE, and is neither a
			//   count nor a running number.
			//   ⇒ Anything that is not "1" is a continuation, which is what this reads.
			// ⚠An empty ruby string is no ruby, whatever the flag says: that is the official rule
			//   (GetRubyStrandInfo turns the attribute off when the string it read has length 0).
			if (!flag.empty() && flag != "0" && !str.empty())
			{
				openRuby = str;
				openGroup = (AttrValue(tag, "RubyType") == "GroupRuby") ? kTrue : kFalse;
				openContinues = (flag != "1") ? kTrue : kFalse;
			}
			else
			{
				openRuby.clear();
				openGroup = kFalse;
				openContinues = kFalse;
			}
			openStarted = kFalse;		// a new range has contributed nothing yet

			// ★KENTEN IS ONE ATTRIBUTE ON THE RANGE - no flag, no run to rebuild. Measured: five
			//   characters marked with one kind come out as ONE range carrying KentenKind, where the
			//   same five characters with ruby come out as five ranges.
			// ⚠OFF IS A VALUE, NOT AN ABSENCE. The SDK turns kenten off by putting Kenten_None into
			//   the attribute rather than removing it (codesnippets-cjk note, SnpPerformTextAttrKenten),
			//   so a range can carry a kind that means "no mark". Both spellings are refused because
			//   only the attribute name has been seen in a real file so far - the OFF value has not.
			const std::string kenten = AttrValue(tag, "KentenKind");
			openKenten = (kenten.empty() || kenten == "KentenNone" || kenten == "Kenten_None")
						 ? std::string() : kenten;

			pos = gt + 1;
		}
		else if (xml.compare(lt, 22, "</CharacterStyleRange>") == 0)
		{
			// ★Safe even for group ruby, which spans several ranges: each range carries its own
			//   RubyString and its own flag, so the next one re-opens what it needs.
			openRuby.clear();
			openGroup = kFalse;
			openContinues = kFalse;
			openKenten.clear();
			pos = lt + 22;
		}
		else if (xml.compare(lt, 4, "<Br ") == 0 || xml.compare(lt, 4, "<Br/") == 0)
		{
			Flush::Do(current, currentAttrs, paraPos, paragraphs, attrsPerPara);

			const size_t gt = xml.find('>', lt);
			pos = (gt == std::string::npos) ? storyEnd : gt + 1;
		}
		else
		{
			pos = lt + 1;
		}
	}

	// The last paragraph has no <Br /> after it.
	Flush::Do(current, currentAttrs, paraPos, paragraphs, attrsPerPara);
}

/** True when two paragraphs' ruby differs - the question "did only the ruby change?" is this one
	asked about a paragraph whose text came out identical.

	⚠Compared as an ordered list, not as a set: moving the same ruby onto different characters is a
	change, and so is reordering two of them.
*/
inline bool16 SpansDiffer(const KESCMAttrSpanList& a, const KESCMAttrSpanList& b)
{
	if (a.size() != b.size())
		return kTrue;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].fStart != b[i].fStart || a[i].fLen != b[i].fLen || a[i].fValue != b[i].fValue)
			return kTrue;
		// ★Mono turned into group is a change even when every reading is the same: 琥珀 read as
		//   こ+はく and 琥珀 read as こはく are different typesetting, and the reader asked to see it.
		if ((a[i].fGroup != 0) != (b[i].fGroup != 0))
			return kTrue;
	}
	return kFalse;
}

}	// namespace KESCMSnippetText

#endif // __KESCMSnippetText_h__

// End, KESCMSnippetText.h.
