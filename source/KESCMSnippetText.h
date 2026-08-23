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

	/** ⚠★★READ, BUT NOT REPORTED (2026-08-23, user's call: "ストーリーモードの StoryEdit にでるのは、
		テキストの変更と、ルビだけで"). Kenten spans were compared for one day and are not any more -
		KESCMStoryDiffRun's AddAttrOnlyChanges is where that was switched off, and it is the only
		place that has to change to switch it back on.
		★THE READING IS KEPT because it costs nothing (it comes off the same pass as the ruby) and
		because getting it right cost a snippet from the user: five characters marked with one kind
		come out as ONE range, where the same five with ruby come out as five. The test harness still
		proves that, so the knowledge cannot rot while it waits. */
	KESCMAttrSpanList	fKenten;

	/** Characters the text model counts after this paragraph that are not in its text (2026-08-22).

		★★WHY IT EXISTS: TABLES. A table is not made of text, but ITextModel counts it, and every
		position worked out from the XML is off by that much until it is added back. The comparison
		checks exactly this (KESCMStoryDiffRun's LengthAgrees) and refuses the whole story when it
		does not add up, which is why a document with ONE table used to produce no differences.

		★★★WHAT A TABLE COSTS, MEASURED CHARACTER BY CHARACTER (2026-08-23; seven shapes read out
		  of the running document with TextIterator, printed as [index]=hex):

		      [0016] then [0017] x (BodyRowCount - 1), CONTIGUOUS, where the table stands

		  = kTextChar_Table and kTextChar_TableContinued (TextChar.h:58-59).
		  ⚠HEADER AND FOOTER ROWS COST NOTHING - two body rows plus a header is TWO characters, not
		    three - a table split across two frames costs no more than one in a single frame, and
		    the cells (which live after the whole of the body) are text+break each, with no row
		    terminator among them.
		⇒ TotalLength = text + BodyRowCount per table + Σ(cell text) + 1 per cell.
		The "+1 per cell" needs nothing here - a cell IS a paragraph, and ParagraphStarts already
		adds one break character per paragraph. What this field carries is the table's own run.

		⚠★★THE READING THIS REPLACED (2026-08-22) said "one for the table, one at the end of every
		  row but the last, charged to the CELLS". For a table with no header row that is the same
		  TOTAL, so it passed LengthAgrees for every table ever tested - while placing every body
		  position after a table (BodyRowCount - 1) characters too EARLY (measured in InDesign: the
		  jump lit '後章' where the change was '章節'). And with a header row the total was wrong
		  outright, so those stories silently produced no differences at all (model=32 computed=33).
		  ⇒ ★★★A TOTAL THAT AGREES SAYS NOTHING ABOUT WHERE THE CHARACTERS ARE.
		⚠A merged cell is simply one cell fewer; nothing else changes (measured). */
	int32				fExtraChars;

	/** The same invisible characters, but standing BEFORE this paragraph rather than after it
		(2026-08-23).

		★★WHY BOTH ENDS ARE NEEDED. fExtraChars above is charged to the paragraph that has just been
		finished, which is the right place for every table but one: a story that BEGINS with a table
		has no finished paragraph to charge, and the character was silently dropped. A text frame
		holding nothing but a table is the ordinary way to make a table, so this was not an edge:
		the count came out one short, LengthAgrees refused the story, and it produced no differences
		at all - the very fault the table support was written to cure.
		★MEASURED, not reasoned about (2026-08-23, work/kescm-snippet-test): the same table with and
		without a leading paragraph must differ by exactly that paragraph's characters plus its
		break. It differed by one more.
		⚠ONLY THE STORY'S FIRST PARAGRAPH CAN CARRY THIS, because it is only set when nothing has
		  been finished yet. Anything walking a RUN of paragraphs may therefore leave it alone -
		  the run's own base position has it already (see IndexInStory below). */
	int32				fLeadingChars;

	/** How many TABLES begin at those two boundaries (2026-08-23).

		★★WHY A COUNT IS NEEDED AS WELL AS A LENGTH. The two fields above are a SUM, and a table
		now contributes a RUN of characters rather than one - so the sum alone no longer says where
		each table's FIRST character stands. That first character is the table's anchor, and
		BodyParagraphStarts reports one of them per table so that the resolver can check them
		against the document's own answer (ITextStoryThreadDict::GetAnchorTextRange) - which is what
		catches a story shape this reader does not understand.
		⚠TWO TABLES SHARING ONE BOUNDARY cannot be placed from a sum (nothing says how the
		  characters divide between them), so no anchor is reported for that shape at all and the
		  story is refused rather than aimed wrongly. */
	int32				fExtraTables;
	int32				fLeadingTables;

	/** Which table cell this paragraph IS, if it is one at all (2026-08-23).

		★★★WHY IT EXISTS: THE XML AND THE TEXT MODEL DO NOT AGREE ABOUT ORDER. In the snippet a
		table's cells sit between the story's own <Content>, exactly where the table stands. The text
		model puts them somewhere else, and says so plainly:

		    "The Text content of the Table ... consists of zero or more contiguous TextStoryThreads
		     that are ALWAYS at greater TextIndex than the Text Story Thread that the Table Model is
		     anchored in."                          -- ITableTextContent.h:41-44

		⇒ Counting straight down the XML puts every position after a table wrong: text that follows
		the table comes out too far along (by the cells), and the cells themselves come out too early
		(by the text that follows). ⚠LengthAgrees cannot see it - it compares TOTALS, and the totals
		are right either way. MEASURED 2026-08-23: a change to the paragraph AFTER a table selected a
		character inside a cell instead; a change inside a cell selected the last character of the
		story; a third one fell outside the story altogether and selected nothing.

		★★THEY ARE ON EVERY PARAGRAPH OF THE CELL, not just its first (2026-08-23). A cell can
		hold several: anyone who presses Return inside one, and ★EVERY MERGED CELL, because merging
		moves the other cells' paragraphs into the survivor (measured - four cells came back as one
		holding 'c0/c1/c2/c3'). The reader used to lose the identity at the <Br />, because a flush
		resets these fields, so the halves after the first looked like BODY text sitting inside a
		table: the story was refused on the cell length (7 against 3) and produced no differences at
		all - with the TOTAL agreeing (27 against 27), so no length check could have found it.

		★These three fields say WHICH cell, so the position can be asked of the document instead of
		counted (ITextStoryThreadDict::QueryThread(GetGridID(GridAddress)) -> GetTextStart), which is
		the road SnpIterTableUseDictHier calls the recommended one. Reading them is the only way to
		tell a cell from a paragraph after the fact: the text of the two is indistinguishable.

		⚠fTableOrdinal counts TOP-LEVEL tables in the order they appear in the story. A nested
		  table's cells are left at -1: their length does not add up yet (the inner table's own
		  characters are not counted), so the story is refused before any position is asked for. */
	enum { kNotACell = -1, kNestedCell = -2 };

	int32				fTableOrdinal;	// kNotACell / kNestedCell, or 0.. = which table's cell
	int32				fCellRow;		// grid row of that cell, -1 when not a cell
	int32				fCellCol;		// grid column of that cell, -1 when not a cell

	KESCMParaAttrs()
		: fExtraChars(0), fLeadingChars(0), fExtraTables(0), fLeadingTables(0),
		  fTableOrdinal(kNotACell), fCellRow(-1), fCellCol(-1) {}

	/** Whether this paragraph is a table cell whose position must be asked of the text model. */
	bool16 IsCell() const { return fTableOrdinal >= 0; }

	/** A cell of a table INSIDE another table (2026-08-23).

		★★SAID OUT LOUD RATHER THAN LEFT TO ARITHMETIC. A nested table's own character and row
		terminators are not counted by this reader, so its story has always been refused by
		LengthAgrees - by accident, from a total that did not add up. Now that cells are placed by
		asking the document rather than by counting, a story could add up and still be placed
		wrongly, so the refusal is made deliberate: the resolver sees this and gives up.
		⚠The user does use nested tables ("たまに使いますね", 2026-08-23), so this is a marker for
		  work still to do, not a decision that they do not matter. */
	bool16 IsNestedCell() const { return fTableOrdinal == kNestedCell; }
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
/** A whole non-negative number out of an attribute value, or -1 when it is not one. */
inline int32 ParseCount(const std::string& text)
{
	if (text.empty())
		return -1;
	int32 value = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] < '0' || text[i] > '9')
			return -1;
		value = value * 10 + (text[i] - '0');
	}
	return value;
}

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
	int32 tableOrdinal = -1;			// which TOP-LEVEL table, counted in the order they appear
	int32 cellOrdinal = KESCMParaAttrs::kNotACell;	// the cell being read, kept across its breaks
	int32 cellRow = -1;
	int32 cellCol = -1;

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
	//
	// ★★AND WHEN THERE IS NO FINISHED PARAGRAPH, IT GOES IN FRONT OF THE ONE BEING BUILT
	//   (2026-08-23). That happens for one story shape and it is a common one: a table at the very
	//   beginning, which is what a text frame holding nothing but a table looks like. The charge
	//   used to be DROPPED there - `!attrsPerPara->empty()` and nothing else - so the story counted
	//   one character short, LengthAgrees refused it, and it produced no differences at all.
	//   ⚠The paragraph being built is empty at that moment: the table branch only skips the flush
	//     when it is, so this cannot land in front of text that has already been read.
	// The cell a paragraph belongs to. ★Put on the paragraph AFTER EVERY FLUSH rather than once
	// when the cell opens, because a cell may hold more than one paragraph and a flush resets the
	// attributes - see KESCMParaAttrs::fTableOrdinal.
	struct Stamp
	{
		static void Cell(KESCMParaAttrs& attrs, int32 ordinal, int32 row, int32 col)
		{
			attrs.fTableOrdinal = ordinal;
			attrs.fCellRow = row;
			attrs.fCellCol = col;
		}
	};

	struct Charge
	{
		static void Table(KESCMParaAttrs& building, std::vector<KESCMParaAttrs>* attrsPerPara,
						  int32 characters)
		{
			if (attrsPerPara == nil)
				return;
			if (attrsPerPara->empty())
			{
				building.fLeadingChars += characters;
				++building.fLeadingTables;
			}
			else
			{
				attrsPerPara->back().fExtraChars += characters;
				++attrsPerPara->back().fExtraTables;
			}
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

				// ★★THE WHOLE RUN AT ONCE, AND OUT OF THE TABLE'S OWN TAG. A table costs one
				//   character per BODY row (KESCMParaAttrs::fExtraChars has the measurements), and
				//   BodyRowCount is an attribute of <Table> in every snippet InDesign writes - so
				//   the cost is known HERE, where the run belongs, instead of being pieced together
				//   from the cells as they go by.
				//   ⚠★Header and footer rows are not in that number and must not be: they cost
				//     nothing (measured). Counting row BOUNDARIES instead counted them, which made
				//     the total wrong and refused every story holding a table with a header row.
				const std::string tableTag(xml, lt, gt - lt);
				const int32 declaredRows = ParseCount(AttrValue(tableTag, "BodyRowCount"));
				// ⚠A table always occupies at least its own character. If the attribute were ever
				//   missing this keeps the reading honest for the ordinary one-row case and lets
				//   LengthAgrees refuse anything larger, rather than guessing a number.
				const int32 tableChars = (declaredRows >= 1) ? declaredRows : 1;
				Charge::Table(currentAttrs, attrsPerPara, tableChars);
				++tableOrdinal;		// ★only top-level tables are numbered - see fTableOrdinal
			}
			++tableDepth;
			pos = gt + 1;
		}
		else if (xml.compare(lt, 8, "</Table>") == 0)
		{
			if (tableDepth > 0)
				--tableDepth;
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
			int32 col = -1;
			if (colon != std::string::npos)
			{
				// "column:row" - both halves are wanted now: the row for the terminator below, and
				// the pair as a GridAddress, which is how the document is asked where this cell's
				// text actually sits (KESCMParaAttrs::fTableOrdinal).
				col = 0;
				for (size_t i = 0; i < colon; ++i)
				{
					if (name[i] < '0' || name[i] > '9')
					{
						col = -1;
						break;
					}
					col = col * 10 + (name[i] - '0');
				}
				row = 0;
				for (size_t i = colon + 1; i < name.size(); ++i)
				{
					if (name[i] < '0' || name[i] > '9')
						break;
					row = row * 10 + (name[i] - '0');
				}
			}

			// ⚠★★NOTHING IS CHARGED HERE ANY MORE (2026-08-23). A row boundary used to add one
			//   character at this point, which put the table's characters among its CELLS - the
			//   right total in the wrong place. The whole run is charged where the table stands,
			//   out of BodyRowCount; see the <Table> branch above.

			// ★WHICH CELL THIS PARAGRAPH IS - AND EVERY OTHER PARAGRAPH OF THE SAME CELL. The
			//   identity is REMEMBERED here and stamped again after each break inside the cell
			//   (see the <Br /> branch): a cell may hold several paragraphs, and a flush resets the
			//   attributes.
			//   ⚠Only a top-level table's cells are named: a nested one's lengths do not add up, so
			//     the story is refused before any position is asked for (see fTableOrdinal).
			cellOrdinal = KESCMParaAttrs::kNotACell;
			cellRow = -1;
			cellCol = -1;
			if (tableDepth == 1 && row >= 0 && col >= 0)
			{
				cellOrdinal = tableOrdinal;
				cellRow = row;
				cellCol = col;
			}
			else if (tableDepth > 1)
			{
				// ⚠A table inside a table - marked so the resolver refuses the story outright
				//   rather than placing it from a count that happens to add up. See IsNestedCell.
				cellOrdinal = KESCMParaAttrs::kNestedCell;
			}
			Stamp::Cell(currentAttrs, cellOrdinal, cellRow, cellCol);

			pos = gt + 1;
		}
		else if (tableDepth > 0 && xml.compare(lt, 7, "</Cell>") == 0)
		{
			// The cell's text ends here. Its own break character is what ParagraphStarts adds to
			// every paragraph, which is exactly the "+1 per cell" the measurement asked for.
			Flush::Do(current, currentAttrs, paraPos, paragraphs, attrsPerPara);
			cellOrdinal = KESCMParaAttrs::kNotACell;	// what follows is not this cell's
			cellRow = -1;
			cellCol = -1;
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
			//   ⇒ Joining is not an optimisation here, it is what makes the reading stable: without
			//     it, italicising one word inside a kenten run would split the span, which any
			//     comparison of these spans would read as a change to the marks themselves.
			//     (⚠Nothing compares them today - see fKenten - so this is what keeps the answer
			//      right for whoever turns that back on, not something the panel depends on now.)
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
			// ★★AND THE PARAGRAPH THAT FOLLOWS IS STILL IN THE SAME CELL, if the break was inside
			//   one. Outside a cell this puts back what the flush already left there.
			Stamp::Cell(currentAttrs, cellOrdinal, cellRow, cellCol);

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

/** The text of a RUN of paragraphs, with the break characters put back.

	★MOVED HERE FROM KESCMStoryDiffRun.cpp ON 2026-08-23, TO STAND BESIDE IndexInStory. The two are
	one convention seen from both ends - this one says how a run's paragraphs are strung together,
	that one says where a position in the resulting string lands in the document - and they were in
	different files while only one of them knew about the invisible characters a table adds. What
	came of that is below, at IndexInStory.

	@param paragraphs every paragraph of the story.
	@param start the first paragraph of the run.
	@param count how many paragraphs the run covers.
*/
inline std::string JoinParagraphs(const std::vector<std::string>& paragraphs, int32 start, int32 count)
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

/** Where each ORDINARY paragraph begins, counted the way the text model counts (2026-08-23).

	★★★A TABLE'S CELLS ARE NOT WHERE THE SNIPPET PUTS THEM. In the XML a table's cells sit between
	the story's own <Content>, exactly where the table stands; the text model keeps them after the
	whole of the story's own text and says so: "TextStoryThreads that are ALWAYS at greater
	TextIndex than the Text Story Thread that the Table Model is anchored in"
	(ITableTextContent.h:41-44). So this walks the BODY only:

	  - a cell contributes nothing - neither its text nor its break. Its entry is left at -1 for
	    KESCMStoryCellBases to fill in from the document;
	  - the table's OWN RUN of characters does count - one per body row - because it stands in the
	    body where the table is (KESCMParaAttrs::fExtraChars has the measurements).

	⚠THIS IS NOT THE TOTAL. ParagraphStarts still adds up everything, because that total is what
	  LengthAgrees checks against ITextModel::TotalLength. The two answer different questions and
	  both are needed.

	@param outTableAnchors where each table's FIRST character sits, in the order the tables appear
	       (one entry per table, not per character).
	       ★The caller checks these against the document's own answer
	       (ITextStoryThreadDict::GetAnchorTextRange). That is what catches a story shape this walk
	       does not understand - two tables in a row, say, where the second table's character is
	       charged to a CELL of the first and so never reaches the body count. Such a story is
	       refused rather than aimed wrongly.
*/
inline void BodyParagraphStarts(const std::vector<std::string>& paragraphs,
								const std::vector<KESCMParaAttrs>& attrs,
								std::vector<int32>& starts,
								std::vector<int32>& outTableAnchors)
{
	starts.assign(paragraphs.size(), -1);
	outTableAnchors.clear();

	int32 index = 0;
	for (size_t i = 0; i < paragraphs.size(); ++i)
	{
		const bool16 haveAttrs = (i < attrs.size());

		// Characters standing IN FRONT of this paragraph are a table's own, and a table's own
		// character is in the body wherever its cells end up.
		if (haveAttrs)
		{
			// ★ONE ANCHOR PER TABLE, at the first of the characters it stands on. ⚠Two tables
			//   sharing a boundary cannot be told apart from a sum, so neither is reported and the
			//   resolver refuses the story on the count.
			if (attrs[i].fLeadingTables == 1)
				outTableAnchors.push_back(index);
			index += attrs[i].fLeadingChars;
		}

		if (haveAttrs && attrs[i].IsCell())
			continue;					// its text, its break and its row terminator are elsewhere

		starts[i] = index;
		index += CountCodePoints(paragraphs[i]) + 1;

		if (haveAttrs)
		{
			if (attrs[i].fExtraTables == 1)
				outTableAnchors.push_back(index);
			index += attrs[i].fExtraChars;
		}
	}
}

/** A run of consecutive paragraphs that all sit in the same PLACE: the story's body, or one cell.

	★★★WHY A ROW IS CUT HERE (2026-08-23, user's call: 「変化している文字の有るセルだけに
	  なるのが正しいかな」). A cell IS a paragraph, so two paragraphs that both changed and
	  happen to be next to each other in the snippet go into ONE run of the paragraph diff - even
	  when one of them is body text and the other is inside the table. The row then reads as one
	  edit spanning the words before the table, the cell, and whatever follows, and its mark covers
	  all the unchanged text in between (measured: a row covering 22 characters for two edits of one
	  character each).
	⚠ADJACENT PARAGRAPHS IN THE SAME PLACE STILL SHARE A ROW - that part is right, and a cell
	  holding several paragraphs depends on it.
	★The body appears more than once when a table stands in the middle of it; those are separate
	  runs, because they are not next to each other.
*/
struct ParaRegion
{
	int32	fStart;		///< first paragraph of the run
	int32	fCount;		///< how many paragraphs
	int32	fTable;		///< KESCMParaAttrs::kNotACell for body text, else which table
	int32	fRow;		///< grid row when it is a cell, -1 otherwise
	int32	fCol;		///< grid column when it is a cell, -1 otherwise

	ParaRegion() : fStart(0), fCount(0), fTable(KESCMParaAttrs::kNotACell), fRow(-1), fCol(-1) {}

	/** The same place - not the same paragraphs. */
	bool16 SamePlaceAs(const ParaRegion& other) const
	{
		return fTable == other.fTable && fRow == other.fRow && fCol == other.fCol;
	}
};

/** The places a run of paragraphs passes through, in order. */
inline void ParagraphRegions(const std::vector<KESCMParaAttrs>& attrs, int32 start, int32 count,
							 std::vector<ParaRegion>& out)
{
	out.clear();
	for (int32 i = start; i < start + count; ++i)
	{
		ParaRegion here;
		here.fStart = i;
		here.fCount = 1;
		if (i >= 0 && static_cast<size_t>(i) < attrs.size() && attrs[i].IsCell())
		{
			here.fTable = attrs[i].fTableOrdinal;
			here.fRow = attrs[i].fCellRow;
			here.fCol = attrs[i].fCellCol;
		}

		if (!out.empty() && out.back().SamePlaceAs(here))
			++out.back().fCount;
		else
			out.push_back(here);
	}
}

/** One piece of a split run: the paragraphs it covers on each side. */
struct RegionPair
{
	int32	fSourceStart;
	int32	fSourceCount;
	int32	fTargetStart;
	int32	fTargetCount;

	RegionPair() : fSourceStart(0), fSourceCount(0), fTargetStart(0), fTargetCount(0) {}
};

/** Cut one run of the paragraph diff into one piece per PLACE (see ParaRegion).

	★★WHEN IT DOES NOT CUT, IT SAYS SO BY ANSWERING WITH ONE PIECE. Three shapes are cut:
	  - a pure insertion: every piece goes in at the same spot in the older version;
	  - a pure deletion: the mirror of it;
	  - a replacement whose two sides pass through the SAME places in the same order.
	⚠Anything else - the table itself gained or lost cells between the versions, say - is left
	  whole. There is no honest way to pair the halves up, and one row that is too wide is better
	  than several that point at the wrong cells.
*/
inline void SplitRunAtPlaces(const std::vector<KESCMParaAttrs>& sourceAttrs,
							 int32 aStart, int32 aCount,
							 const std::vector<KESCMParaAttrs>& targetAttrs,
							 int32 bStart, int32 bCount,
							 std::vector<RegionPair>& out)
{
	out.clear();

	std::vector<ParaRegion> aRegions;
	std::vector<ParaRegion> bRegions;
	ParagraphRegions(sourceAttrs, aStart, aCount, aRegions);
	ParagraphRegions(targetAttrs, bStart, bCount, bRegions);

	RegionPair whole;
	whole.fSourceStart = aStart;
	whole.fSourceCount = aCount;
	whole.fTargetStart = bStart;
	whole.fTargetCount = bCount;

	if (aCount == 0 && bRegions.size() > 1)
	{
		for (size_t i = 0; i < bRegions.size(); ++i)
		{
			RegionPair piece;
			piece.fSourceStart = aStart;			// nothing of the older version is involved
			piece.fSourceCount = 0;
			piece.fTargetStart = bRegions[i].fStart;
			piece.fTargetCount = bRegions[i].fCount;
			out.push_back(piece);
		}
		return;
	}

	if (bCount == 0 && aRegions.size() > 1)
	{
		for (size_t i = 0; i < aRegions.size(); ++i)
		{
			RegionPair piece;
			piece.fSourceStart = aRegions[i].fStart;
			piece.fSourceCount = aRegions[i].fCount;
			piece.fTargetStart = bStart;
			piece.fTargetCount = 0;
			out.push_back(piece);
		}
		return;
	}

	if (aRegions.size() > 1 && aRegions.size() == bRegions.size())
	{
		for (size_t i = 0; i < aRegions.size(); ++i)
		{
			if (!aRegions[i].SamePlaceAs(bRegions[i]))
			{
				out.clear();
				out.push_back(whole);		// the versions do not pass through the same places
				return;
			}

			RegionPair piece;
			piece.fSourceStart = aRegions[i].fStart;
			piece.fSourceCount = aRegions[i].fCount;
			piece.fTargetStart = bRegions[i].fStart;
			piece.fTargetCount = bRegions[i].fCount;
			out.push_back(piece);
		}
		return;
	}

	out.push_back(whole);
}

/** Where the run of paragraphs belonging to ONE cell ends - the index one past its last.

	★★A CELL IS NOT ALWAYS ONE PARAGRAPH (2026-08-23). It holds one for every Return pressed in it,
	and a MERGED cell holds the paragraphs of everything merged into it. They arrive together, in
	order, all naming the same (table, row, column) - which is what this walks.

	⚠Asked about a paragraph that is not a cell, this answers with the paragraph itself, so the
	  caller gets an EMPTY run rather than a guess.
*/
inline size_t CellRunEnd(const std::vector<KESCMParaAttrs>& attrs, size_t at)
{
	if (at >= attrs.size() || !attrs[at].IsCell())
		return at;

	size_t end = at + 1;
	while (end < attrs.size() && attrs[end].IsCell()
		   && attrs[end].fTableOrdinal == attrs[at].fTableOrdinal
		   && attrs[end].fCellRow == attrs[at].fCellRow
		   && attrs[end].fCellCol == attrs[at].fCellCol)
		++end;

	return end;
}

/** How many characters such a run occupies in the text model: every paragraph AND its break.

	★This is what a cell's own text story thread holds, which is why the resolver can check it
	against ITextStoryThread::GetTextStart's span before trusting the cell it found.
*/
inline int32 CellRunLength(const std::vector<std::string>& paragraphs, size_t from, size_t to)
{
	int32 length = 0;
	for (size_t i = from; i < to && i < paragraphs.size(); ++i)
		length += CountCodePoints(paragraphs[i]) + 1;
	return length;
}

/** Where an offset into that joined string lands in the document, as a TextIndex.

	★★WHY THIS IS NOT `base + offset` (2026-08-23). JoinParagraphs puts ONE character between two
	paragraphs; the document may not have them that close together at all. Two faults found on the
	same day, both of this shape:
	  (1) a table's own character and a row's terminator sit at exactly such a boundary, so a change
	      covering two ADJACENT paragraphs put the second one short by them - MEASURED on the real
	      table snippet: at 21 where the document has 22, and at 31 against 32;
	  (2) ★★★and a table's CELLS are not between the paragraphs at all (see BodyParagraphStarts
	      above), so the distance across a table is nothing like the sum of what the snippet lists
	      there - MEASURED: a change to the paragraph after a table selected a character inside a
	      cell instead.
	⚠SILENT, and no length check can catch either: LengthAgrees compares TOTALS, which were right.
	⇒ Both are answered the same way: every paragraph's position is LOOKED UP, never added up.

	@param paragraphs every paragraph of the story.
	@param starts one document position per paragraph - BodyParagraphStarts for the ordinary ones,
	       KESCMStoryCellBases for the cells. ⚠An unresolved cell (-1) would answer nonsense, so the
	       caller refuses the story before asking anything of this.
	@param start the first paragraph of the run.
	@param count how many paragraphs the run covers.
	@param base where to answer from when the run covers no paragraph of its own (an insertion
	       between two paragraphs): the caller's position for the next surviving paragraph.
	@param joinedOffset a position in JoinParagraphs' answer, in CODE POINTS, 0 .. its length.
	@return the same position as a TextIndex into the story.
*/
inline int32 IndexInStory(const std::vector<std::string>& paragraphs,
						  const std::vector<int32>& starts,
						  int32 start, int32 count, int32 base, int32 joinedOffset)
{
	// ★★★REWRITTEN 2026-08-23: IT LOOKS EVERY PARAGRAPH UP INSTEAD OF ADDING UP THE HIDDEN
	//   CHARACTERS BETWEEN THEM. The old form walked from the run's base and, at each paragraph
	//   break, added that paragraph's fExtraChars. That is right only while the document lays
	//   paragraphs out in the order the snippet lists them, and A TABLE BREAKS EXACTLY THAT: the
	//   text model keeps a table's cells AFTER the whole of the story's own text
	//   (ITableTextContent.h:41-44), so no amount of adding gets from the paragraph before a table
	//   to the one after it. MEASURED 2026-08-23: a change to the paragraph following a table
	//   selected a character inside a cell instead.
	//   ⇒ Positions come from the table `starts`, which is built once - the body by
	//     BodyParagraphStarts, the cells by asking the document (KESCMStoryCellBases). This walk
	//     only has to decide WHICH paragraph the offset falls in.
	int32 joined = 0;		// where the paragraph being looked at begins, inside the joined string
	for (int32 i = 0; i < count; ++i)
	{
		const int32 which = start + i;
		if (which < 0 || which >= static_cast<int32>(paragraphs.size())
			|| which >= static_cast<int32>(starts.size()))
			break;

		const int32 len = CountCodePoints(paragraphs[which]);

		// ⚠AT the break belongs to the paragraph BEFORE it, which is the rule the old form kept
		//   ("the offset is at or before it - nothing to add") and the paragraph-start table agrees
		//   with: the next paragraph's start is where the character AFTER the break sits.
		if (joinedOffset <= joined + len)
			return starts[which] + (joinedOffset - joined);

		joined += len + 1;		// the paragraph, and the one character JoinParagraphs puts after it
	}

	// Past the end of the run - or a run with no paragraphs of its own, which is how an insertion
	// between two paragraphs arrives. The caller's base is where the next surviving paragraph
	// begins, and that is the right answer for the empty case.
	if (count > 0)
	{
		const int32 last = start + count - 1;
		if (last >= 0 && last < static_cast<int32>(paragraphs.size())
			&& last < static_cast<int32>(starts.size()))
			return starts[last] + CountCodePoints(paragraphs[last]);
	}
	return base;
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
