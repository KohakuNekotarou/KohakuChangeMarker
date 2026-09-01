//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Reading a story's TEXT and its RUBY out of the snippet XML. Nothing here touches the SDK.
//
//  **WHY THIS IS A HEADER OF ITS OWN.** It was written inside KCMStoryDiffRun.cpp, where it
//  could only ever be exercised by running a comparison inside InDesign. What it actually does
//  is turn one string into another, so it can be measured outside -- and the moment ruby
//  arrived the parsing stopped being "find <Content>" and started being a small XML reader with
//  state, which is exactly the kind of code that earns a test.
//  The test is work\kescm-snippet-test, and it includes THIS FILE as it stands -- not a copy
//  that can drift, the way KTTextDiff drifted from KCMTextDiff.
//
//  **WHY RUBY IS READ HERE AND NOT FROM THE TEXT MODEL.** The SDK has a direct route
//  (SnpPerformTextAttrRuby::GetRubyStrandInfo: IRubyAttrStrand::GetRubyRun for the run,
//  kTARubyStringBoss for the string), and this file's RubyFlag / RubyString are that same pair
//  seen through the snippet -- the flag IS the strand's run, the string IS the attribute's
//  value. The reason to read them HERE is TIME: a comparison is a photograph of one moment, and
//  the text already comes from this snippet. Reading ruby from the live model instead would put
//  two moments in one row -- the same fault the row refresh was written to prevent.
//
//  @warning **AN EMPTY RUBY STRING IS NO RUBY**, which is the official rule and not an
//   invention here: GetRubyStrandInfo turns its attribute flag off when the string it read has
//   length 0 ("if the ruby string is empty in the ruby strand, consider ruby to be off").
//
//========================================================================================

#ifndef __KCMSnippetText_h__
#define __KCMSnippetText_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// nil. @warning BaseType.h does NOT define it, and this header uses it -- without
							//  this line it only compiles when something else (VCPlugInHeaders.h) has been
							//  included first, which is exactly the hidden ordering dependency the file comment
							//  above claims it does not have.

#include <string>
#include <vector>

/** One stretch of characters carrying ONE character attribute, inside one paragraph.

	**POSITIONS ARE CODE POINTS**, counted the way InDesign counts text positions, so a number
	worked out here lines up with the paragraph offsets the diff already produces (a surrogate
	pair is one).

	**ONE TYPE FOR RUBY AND KENTEN.** They are different mechanisms in the SDK -- ruby is a
	STRAND (IRubyAttrStrand, run-based) and kenten is a set of CHARACTER ATTRIBUTES -- but what
	the panel needs of them is identical: a stretch of characters, and a value that says what is
	sitting over it. Writing the comparison twice would mean fixing it twice.
	So fValue holds the READING for ruby and the KIND for kenten ("KentenBlackCircle").
*/
struct KCMAttrSpan
{
	int32		fStart;		// first character of the base text, within its paragraph
	int32		fLen;		// how many characters the attribute covers
	std::string	fValue;		// the reading (ruby) or the kind (kenten), UTF-8. @warning never empty

	/** kTrue for GROUP ruby -- one reading spread over several base characters (琥珀 -> こはく)
		-- against MONO ruby, where each character has its own (琥 -> こ, 珀 -> はく).

		It is carried because the two are different typesetting, so turning one into the other IS
		a change even when every reading stays the same. InDesign writes it as RubyType="GroupRuby"
		and omits the attribute for mono, so mono is the default here too. The pair is the SDK's
		own: IRubyStyle::RubyKind, kRubyKind_Group / kRubyKind_Mono.
		@warning RUBY ONLY. Kenten has no such distinction -- it is per character by nature -- so
		  its spans always leave this kFalse, and the comparison then never reports a difference
		  in it. */
	bool16		fGroup;

	KCMAttrSpan() : fStart(0), fLen(0), fGroup(kFalse) {}
	KCMAttrSpan(int32 start, int32 len, const std::string& value, bool16 group = kFalse)
		: fStart(start), fLen(len), fValue(value), fGroup(group) {}
};

typedef std::vector<KCMAttrSpan> KCMAttrSpanList;

/** One table of the story, and where its first character stands.

	**WHY THE PAIR AND NOT JUST THE POSITION.** Every table's position is checked against the
	document's own answer before any cell of it is placed, and to check it you have to know which
	table you are holding. Tables used to be met in one order only -- the reader walked the body
	from the top and so did the check -- but a table inside a CELL is charged to a paragraph that
	is not finished until that table's own cells have gone by, so the order the anchors come out
	in is no longer the order the tables are numbered in.
*/
struct KCMTableAnchor
{
	int32	fOrdinal;	///< KCMParaAttrs::fTableOrdinal of the table
	int32	fIndex;		///< where its first character stands, as a TextIndex

	KCMTableAnchor() : fOrdinal(-1), fIndex(-1) {}
	KCMTableAnchor(int32 ordinal, int32 index) : fOrdinal(ordinal), fIndex(index) {}
};

/** Everything one paragraph carries OVER its characters -- the attributes a change can hide in
	while the words themselves stay identical.

	**WHY A STRUCT RATHER THAN ANOTHER OUT-PARAMETER.** Ruby was the first, kenten is the second,
	and the parser's signature would grow a parameter for each. This way the parser answers one
	thing per paragraph and a third attribute costs a field, not a new argument at every call
	site.
	@warning **what is deliberately NOT in here: applied styles.** Finding those was considered
	  and rejected. A paragraph whose text is unchanged and whose style was swapped keeps reading
	  "None".
*/
struct KCMParaAttrs
{
	KCMAttrSpanList	fRuby;

	/** ★**READ AND REPORTED AGAIN SINCE 2026-09-01** (user's call). It was compared for a day in
		August, switched off, and switched back on in the one place that decides it -
		KCMStoryDiffRun's AddAttributeChanges (called AddAttrOnlyChanges until 2026-09-01, when it
		stopped being attr-ONLY - see there). **Keeping the reading through the months it was not
		reported is what made turning it back on one call**: had the parser stopped filling this,
		the knowledge that five characters marked with one kind are ONE range - which cost a snippet
		from the user to get right, and which is the opposite of ruby, where the same five come out
		as five - would have had to be found a second time.
		@warning the value is a KIND ("BlackCircle"), never something a reader reads aloud. Whoever
		 draws it asks Change::fAttrKind first; see the note on KCMAttrSpan::fValue. */
	KCMAttrSpanList	fKenten;

	/** Characters the text model counts after this paragraph that are not in its text.

		**WHY IT EXISTS: TABLES.** A table is not made of text, but ITextModel counts it, and every
		position worked out from the XML is off by that much until it is added back. The comparison
		checks exactly this (KCMStoryDiffRun's LengthAgrees) and refuses the whole story when it
		does not add up, which is why a document with ONE table used to produce no differences.

		**WHAT A TABLE COSTS, MEASURED CHARACTER BY CHARACTER** (seven shapes read out of the
		  running document with TextIterator, printed as [index]=hex):

		      [0016] then [0017] x (BodyRowCount - 1), CONTIGUOUS, where the table stands

		  = kTextChar_Table and kTextChar_TableContinued (TextChar.h).
		  @warning **HEADER AND FOOTER ROWS COST NOTHING** -- two body rows plus a header is TWO
		    characters, not three -- a table split across two frames costs no more than one in a
		    single frame, and the cells (which live after the whole of the body) are text+break
		    each, with no row terminator among them.
		So TotalLength = text + BodyRowCount per table + sum(cell text) + 1 per cell.
		The "+1 per cell" needs nothing here -- a cell IS a paragraph, and ComputedLength already
		adds one break character per paragraph. What this field carries is the table's own run.

		@warning **the reading this replaced** said "one for the table, one at the end of every row
		  but the last, charged to the CELLS". For a table with no header row that is the same
		  TOTAL, so it passed LengthAgrees for every table ever tested -- while placing every body
		  position after a table (BodyRowCount - 1) characters too EARLY (measured in InDesign: the
		  jump lit '後章' where the change was '章節'). And with a header row the total was wrong
		  outright, so those stories silently produced no differences at all (model=32 computed=33).
		  **A TOTAL THAT AGREES SAYS NOTHING ABOUT WHERE THE CHARACTERS ARE.**
		@warning a merged cell is simply one cell fewer; nothing else changes (measured). */
	int32				fExtraChars;

	/** The same invisible characters, but standing BEFORE this paragraph rather than after it.

		**WHY BOTH ENDS ARE NEEDED.** fExtraChars above is charged to the paragraph that has just
		been finished, which is the right place for every table but one: a story that BEGINS with a
		table has no finished paragraph to charge, and the character was silently dropped. A text
		frame holding nothing but a table is the ordinary way to make a table, so this was not an
		edge: the count came out one short, LengthAgrees refused the story, and it produced no
		differences at all -- the very fault the table support was written to cure.
		MEASURED, not reasoned about (work/kescm-snippet-test): the same table with and without a
		leading paragraph must differ by exactly that paragraph's characters plus its break. It
		differed by one more.
		@warning **only the story's first paragraph can carry this**, because it is only set when
		  nothing has been finished yet. Anything walking a RUN of paragraphs may therefore leave it
		  alone -- the run's own base position has it already (see IndexInStory below). */
	int32				fLeadingChars;

	/** How many TABLES begin at those two boundaries.

		**WHY A COUNT IS NEEDED AS WELL AS A LENGTH.** The two fields above are a SUM, and a table
		now contributes a RUN of characters rather than one -- so the sum alone no longer says where
		each table's FIRST character stands. That first character is the table's anchor, and
		BodyParagraphStarts reports one of them per table so that the resolver can check them
		against the document's own answer (ITextStoryThreadDict::GetAnchorTextRange) -- which is
		what catches a story shape this reader does not understand.
		@warning **two tables sharing one boundary** cannot be placed from a sum (nothing says how
		  the characters divide between them), so no anchor is reported for that shape at all and
		  the story is refused rather than aimed wrongly. */
	int32				fExtraTables;
	int32				fLeadingTables;

	/** WHICH table begins at those two boundaries -- its fTableOrdinal -- or -1 for none.

		**WHY THE NUMBER AND NOT JUST THE COUNT.** Every table's anchor is checked against the
		document's own answer, and to check it you have to know which table you are looking at.
		While tables were only ever in the body that could be left to ORDER: the body walk met them
		from the top, and so did the reader. A NESTED table breaks that -- the paragraph carrying
		its charge is a cell's, finished only after the inner table's own cells have been pushed, so
		the order the anchors come out in is no longer the order the tables are numbered in.
		@warning only the FIRST at each boundary is kept, because a boundary carrying two is refused
		  anyway (see the counts above). */
	int32				fExtraTable;
	int32				fLeadingTable;

	/** Which table cell this paragraph IS, if it is one at all.

		**WHY IT EXISTS: THE XML AND THE TEXT MODEL DO NOT AGREE ABOUT ORDER.** In the snippet a
		table's cells sit between the story's own <Content>, exactly where the table stands. The
		text model puts them somewhere else, and says so plainly:

		    "The Text content of the Table ... consists of zero or more contiguous TextStoryThreads
		     that are ALWAYS at greater TextIndex than the Text Story Thread that the Table Model is
		     anchored in."                                       -- ITableTextContent.h, at the top

		So counting straight down the XML puts every position after a table wrong: text that follows
		the table comes out too far along (by the cells), and the cells themselves come out too
		early (by the text that follows).
		@warning LengthAgrees cannot see it -- it compares TOTALS, and the totals are right either
		 way. MEASURED: a change to the paragraph AFTER a table selected a character inside a cell
		 instead; a change inside a cell selected the last character of the story; a third one fell
		 outside the story altogether and selected nothing.

		**THEY ARE ON EVERY PARAGRAPH OF THE CELL, not just its first.** A cell can hold several:
		anyone who presses Return inside one, and EVERY MERGED CELL, because merging moves the other
		cells' paragraphs into the survivor (measured -- four cells came back as one holding
		'c0/c1/c2/c3'). The reader used to lose the identity at the <Br />, because a flush resets
		these fields, so the halves after the first looked like BODY text sitting inside a table:
		the story was refused on the cell length (7 against 3) and produced no differences at all --
		with the TOTAL agreeing (27 against 27), so no length check could have found it.

		These three fields say WHICH cell, so the position can be asked of the document instead of
		counted (ITextStoryThreadDict::QueryThread(GetGridID(GridAddress)) -> GetTextStart), which
		is the road SnpIterTableUseDictHier calls the recommended one. Reading them is the only way
		to tell a cell from a paragraph after the fact: the text of the two is indistinguishable.

		fTableOrdinal counts EVERY table in the order it appears in the story, nested ones included.
		  @warning it is therefore not a running counter: after an inner table closes, the cells that
		    follow belong to the OUTER table again -- see the stack of open tables in
		    ExtractParagraphs. */
	enum { kNotACell = -1 };

	int32				fTableOrdinal;	// kNotACell, or 0.. = which table this cell belongs to
	int32				fCellRow;		// grid row of that cell, -1 when not a cell
	int32				fCellCol;		// grid column of that cell, -1 when not a cell

	/** Which footnote of the story this paragraph stands in, kNotAFootnote when it is not in one.

		★A FOOTNOTE IS A PLACE, exactly as a cell is, and for the same reason IsCell() exists: the
		text of a footnote paragraph and of a body paragraph are indistinguishable after the fact,
		and SplitRunAtPlaces has to cut a run where the PLACE changes - otherwise one row spans a
		body edit and a footnote edit and points at neither.
		⚠A FOOTNOTE IS NOT A CELL. It has no table, no row and no column, so it gets a field of its
		  own rather than a borrowed fTableOrdinal - and a paragraph is never both.
		@warning **only the model route fills this in.** The snippet parser does not know
		  <Footnote> at all (it folds the note's text into the body, which is why a story with one
		  was refused outright), so paragraphs it produces always answer kNotAFootnote. */
	enum { kNotAFootnote = -1 };

	int32				fFootnoteOrdinal;

	KCMParaAttrs()
		: fExtraChars(0), fLeadingChars(0), fExtraTables(0), fLeadingTables(0),
		  fExtraTable(-1), fLeadingTable(-1),
		  fTableOrdinal(kNotACell), fCellRow(-1), fCellCol(-1),
		  fFootnoteOrdinal(kNotAFootnote) {}

	/** Whether this paragraph is a table cell whose position must be asked of the text model.

		**A CELL OF A NESTED TABLE IS A CELL LIKE ANY OTHER.** It used to be marked apart
		(kNestedCell) so that the resolver could refuse the story: the inner table's own characters
		were not counted, so such a story never added up, and once cells were placed by asking rather
		than by counting it could have added up and still been placed wrongly. Both reasons are gone
		-- the inner table is charged to the cell it stands in, and the document is asked about its
		cells the same way it is asked about any others. */
	bool16 IsCell() const { return fTableOrdinal >= 0; }

	/** Whether this paragraph stands inside a footnote. */
	bool16 IsFootnote() const { return fFootnoteOrdinal >= 0; }
};

namespace KCMSnippetText
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

/** How many CODE POINTS a UTF-8 string holds -- continuation bytes (10xxxxxx) are not counted.

	This is the unit the whole comparison works in, so a four-byte character counts once here
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

/** The value of one attribute of a start tag, or "" when the tag does not carry it.

	@param tag the tag WITHOUT its angle brackets, e.g. `CharacterStyleRange RubyFlag="1"`.
	@param name the attribute to look for.
	@warning matched as ` name="`, with the leading space, so that RubyString is not found inside
	  a longer attribute name that happens to end with it.
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

/** How long the element name is, when this tag opens a PAGE ITEM that stands in the text as
	one character -- and 0 when it does not.

	**ONE CHARACTER, MEASURED** (work\anchortest\probe3.jsx). Five kinds of object were anchored
	into a ten-character story and the story came out fifteen: Rectangle, Oval, Polygon,
	GraphicLine and TextFrame. A CUSTOM-positioned one cost the same as an inline one, and one
	inside a table CELL cost that cell one character -- the item stands under
	Cell > ParagraphStyleRange > CharacterStyleRange exactly as it stands in the body.

	**THE LIST IS DELIBERATE, NOT A CATCH-ALL.** Anything not named here is skipped as before,
	and a story whose length then fails to add up is REFUSED -- which is the safe way round. A
	catch-all ("everything that is not Content or Br") would silently count things that are not
	one character: <Footnote>, <Note>, <Change> and <HyperlinkTextSource> all stand in the same
	place and are containers, not objects.

	@warning Group is on the list and its CONTENTS are not: the whole element is skipped, so a
	  group holding three rectangles is one character, which is what the text model says it is.
*/
inline size_t AnchoredItemTagLen(const std::string& xml, size_t lt)
{
	static const char* const kNames[] =
	{
		"TextFrame", "Rectangle", "Oval", "Polygon", "GraphicLine",
		"Group", "Button", "MultiStateObject"
	};

	for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i)
	{
		const size_t n = std::char_traits<char>::length(kNames[i]);
		if (xml.compare(lt + 1, n, kNames[i]) != 0)
			continue;

		// @warning **THE NAME HAS TO END HERE.** Without this, "<Rectangle" also matches the
		//   opening of <RectanglePreference> and the reader would put a character where there is none.
		const char after = (lt + 1 + n < xml.size()) ? xml[lt + 1 + n] : '\0';
		if (after == ' ' || after == '/' || after == '>' || after == '\t' || after == '\n' || after == '\r')
			return n;
	}
	return 0;
}

/** One past the end of the element opening at `lt`, whose name is `nameLen` long.

	**SKIPPED WHOLE, NOT STEPPED INTO.** A real page item carries its entire geometry -- the
	anchored TextFrame measured here was 3,781 characters of <Properties> and <PathGeometry> --
	and the reader must not meet any of it.
	@warning it matters for correctness and not only for speed: this parser treats every
	 <Content> it meets as body text, so a <Content> anywhere inside the element would arrive in
	 the story.

	@warning nesting is counted, because a <Group> holds other page items with the same names.
*/
inline size_t SkipItemElement(const std::string& xml, size_t lt, size_t nameLen, size_t storyEnd)
{
	const size_t gt = xml.find('>', lt);
	if (gt == std::string::npos || gt > storyEnd)
		return storyEnd;
	if (gt > lt && xml[gt - 1] == '/')
		return gt + 1;					// <Rectangle ... />

	const std::string name(xml, lt + 1, nameLen);
	const std::string closeTag = "</" + name + ">";

	int32 depth = 1;
	size_t at = gt + 1;
	while (at < storyEnd && depth > 0)
	{
		const size_t next = xml.find('<', at);
		if (next == std::string::npos || next >= storyEnd)
			return storyEnd;

		if (xml.compare(next, closeTag.size(), closeTag) == 0)
		{
			--depth;
			at = next + closeTag.size();
			continue;
		}

		// An opening tag of the SAME name goes one level deeper - unless it closes itself.
		if (AnchoredItemTagLen(xml, next) == nameLen && xml.compare(next + 1, nameLen, name) == 0)
		{
			const size_t g2 = xml.find('>', next);
			if (g2 == std::string::npos || g2 > storyEnd)
				return storyEnd;
			if (!(g2 > next && xml[g2 - 1] == '/'))
				++depth;
			at = g2 + 1;
			continue;
		}

		at = next + 1;
	}
	return at;
}

/** Read the story's text out of the snippet: <Content> holds it, <Br /> ends a paragraph.

	Only the region between <Story and </Story> is looked at. The snippet also carries every
	object the story depends on -- inks, fonts, styles, cross-reference formats -- and some of
	those have text of their own that must not be mistaken for the story's. (Measured: the
	dependencies are more than eight tenths of the file and contribute nothing to the diff.)

	**THE ATTRIBUTES ARE COLLECTED ON THE WAY THROUGH.** Both ruby and kenten live on the
	<CharacterStyleRange> that encloses the text they sit over, so they are read when that tag
	opens and forgotten when it closes. Positions are counted in code points as the text is
	appended, so they line up with the paragraph offsets the diff produces.

	@param xml the snippet.
	@param paragraphs [out] cleared, then filled -- one entry per paragraph.
	@param attrsPerPara [out] when not nil: cleared, then filled to the SAME length as
		paragraphs, each entry holding that paragraph's ruby and kenten spans in reading order.
*/
inline void ExtractParagraphs(const std::string& xml,
							  std::vector<std::string>& paragraphs,
							  std::vector<KCMParaAttrs>* attrsPerPara)
{
	paragraphs.clear();
	if (attrsPerPara != nil)
		attrsPerPara->clear();

	const size_t storyStart = xml.find("<Story ");
	const size_t storyEnd = xml.rfind("</Story>");
	if (storyStart == std::string::npos || storyEnd == std::string::npos || storyEnd < storyStart)
		return;

	/* OpenRange
	   What the <CharacterStyleRange> the reader is inside puts over the characters it holds.

	   **THE FIVE MOVE TOGETHER, AND USED TO BE FIVE SEPARATE VARIABLES.** They are opened by one
	   tag, read by one branch and closed by another, and a nested table saves and restores all of
	   them at once -- which was five lines in each of four places, and a sixth piece of state
	   would have had to be added to every one of them. Naming the group is what makes the four
	   agree by construction.
	*/
	struct OpenRange
	{
		std::string	fRuby;			// the ruby of the range we are inside, "" for none
		bool16		fGroup;			// ...and whether that one is group ruby
		bool16		fContinues;		// ...and whether it CONTINUES the span before it (RubyFlag="2")
		bool16		fStarted;		// ...and whether a span was already opened inside THIS range
		std::string	fKenten;		// the KentenKind of that same range, "" for none

		OpenRange() : fGroup(kFalse), fContinues(kFalse), fStarted(kFalse) {}

		/** The range has ended.

			@warning **fStarted is deliberately NOT cleared here.** It says whether a span was
			  already opened inside the range, and it is the OPENING tag that sets it to kFalse --
			  which is where it belongs, since that is the moment a range has contributed nothing
			  yet. Clearing it here as well would be the same fact decided in two places
			  ([[one-question-one-place]]), and the two would have to be kept agreeing for a state
			  that nothing reads while no range is open.
		*/
		void Close()
		{
			fRuby.clear();
			fGroup = kFalse;
			fContinues = kFalse;
			fKenten.clear();
		}
	};

	std::string current;
	KCMParaAttrs currentAttrs;		// spans found so far in the paragraph being built
	OpenRange open;					// what the CharacterStyleRange being read puts over its text
	int32 paraPos = 0;					// code points appended to `current` so far
	size_t pos = storyStart;

	// **TABLES.** A table lives INSIDE the story -- its cells' <Content> sits between the
	//   story's own -- so reading every <Content> the way this used to did two things at once: it
	//   glued the cells onto whatever paragraph the table interrupted, and it made the character
	//   count disagree with ITextModel::TotalLength. The second one is what the reader saw: the
	//   comparison's LengthAgrees guard refused the whole story, so a document with a table got
	//   NO text differences at all -- and no ruby or kenten either, since those are found after
	//   it.
	//
	// **A CELL IS A PARAGRAPH.** That is all the diff needs: cells become elements of the same
	//   sequence the paragraphs are in, so a rewritten cell comes out as a change, an added row
	//   as insertions and a deleted one as deletions -- with no new machinery.
	//
	// **HOW MANY CHARACTERS A TABLE IS**, measured on five documents (0/4/6/6/8 cells) and
	//   checked against a sixth:
	//        TotalLength = text + 1 (the table's own anchor character)
	//                           + sum(cell contents) + one per cell + (rows - 1)
	//   @warning the last row has no terminator, which is why it is rows-1 and not rows. All
	//     five agreed exactly; the real document came to 52 against a measured 52.
	// **A TABLE INSIDE A TABLE.** MEASURED FIRST, on a 2x2 table whose cell holds a 2x1 one:
	//   ITextModel::TotalLength came to 42, and 42 is what the ordinary rule gives when it is
	//   applied ONCE MORE -- body 16 (its own text plus the OUTER table's two characters), outer
	//   cells 18 (one of which is the INNER table's two characters and its break), inner cells 8.
	//   So the rule is not "tables in the body"; it is "a table charges the thread it stands in",
	//     and a cell IS a thread. The same three counters do for both, and what has to be kept is
	//     the PLACE: which table a cell belongs to, and whose paragraph a nested table charges.
	int32 tableDepth = 0;				// >0 while inside a table (tables can nest)
	int32 nextTableOrdinal = 0;			// every table, nested ones included, in the order they appear
	std::vector<int32> openTables;		// the ordinal of each table now open, innermost last
	int32 cellOrdinal = KCMParaAttrs::kNotACell;	// the cell being read, kept across its breaks
	int32 cellRow = -1;
	int32 cellCol = -1;

	/* Suspended
	   The paragraph of the cell a nested table stands in, put aside while that table is read.

	   **WHY IT IS PUT ASIDE RATHER THAN FINISHED.** The parent cell's paragraph is not over: the
	   table stands inside it, and the cell may hold text after it. Finishing it here would give
	   the cell one paragraph too many, and its run would then be one break longer than the thread
	   the document reports -- which KCMStoryCellBases refuses. So everything the reader is holding
	   for that paragraph is saved, the inner table is read from a clean slate, and the paragraph
	   is picked up again at </Table>, still charged with the characters the inner table costs.
	*/
	struct Suspended
	{
		std::string			fText;
		KCMParaAttrs		fAttrs;
		int32				fParaPos;
		int32				fCellOrdinal;
		int32				fCellRow;
		int32				fCellCol;
		// @warning the character range state as well: the inner table's cells open and close ranges
		//   of their own, which would otherwise leave the parent's looking closed.
		OpenRange			fOpen;
	};
	std::vector<Suspended> suspended;

	// Finish the paragraph being built and start a new one. **ONE PLACE**, because a paragraph is
	// now ended by three different things -- a <Br />, a cell closing, and the end of the story --
	// and the three must agree about what "finish" means (decode, push, push the attributes, reset).
	struct Flush
	{
		static void Do(std::string& current, KCMParaAttrs& attrs, int32& paraPos,
					   std::vector<std::string>& paragraphs,
					   std::vector<KCMParaAttrs>* attrsPerPara)
		{
			DecodeEntities(current);
			paragraphs.push_back(current);
			if (attrsPerPara != nil)
				attrsPerPara->push_back(attrs);
			current.clear();
			attrs = KCMParaAttrs();
			paraPos = 0;
		}
	};

	// The cell a paragraph belongs to. Put on the paragraph AFTER EVERY FLUSH rather than once
	// when the cell opens, because a cell may hold more than one paragraph and a flush resets the
	// attributes -- see KCMParaAttrs::fTableOrdinal.
	struct Stamp
	{
		static void Cell(KCMParaAttrs& attrs, int32 ordinal, int32 row, int32 col)
		{
			attrs.fTableOrdinal = ordinal;
			attrs.fCellRow = row;
			attrs.fCellCol = col;
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

			// **THE WHOLE RUN AT ONCE, AND OUT OF THE TABLE'S OWN TAG.** A table costs one character
			//   per BODY row (KCMParaAttrs::fExtraChars has the measurements), and BodyRowCount is an
			//   attribute of <Table> in every snippet InDesign writes -- so the cost is known HERE,
			//   where the run belongs, instead of being pieced together from the cells as they go by.
			//   @warning header and footer rows are not in that number and must not be: they cost
			//     nothing (measured). Counting row BOUNDARIES instead counted them, which made the
			//     total wrong and refused every story holding a table with a header row.
			const std::string tableTag(xml, lt, gt - lt);
			const int32 declaredRows = ParseCount(AttrValue(tableTag, "BodyRowCount"));
			// @warning a table always occupies at least its own character. If the attribute were ever
			//   missing this keeps the reading honest for the ordinary one-row case and lets
			//   LengthAgrees refuse anything larger, rather than guessing a number.
			const int32 tableChars = (declaredRows >= 1) ? declaredRows : 1;

			// **A TABLE CHARGES THE PARAGRAPH IT STANDS IN, AND THAT PARAGRAPH IS NOT OVER.**
			//   @warning **it is not "the paragraph last finished" either**, which is what this used to
			//     charge. For a table in the body those two are the same thing whenever a <Br /> stands
			//     in front of it -- the ordinary shape -- but they part company twice:
			//       - a table at the very START of a story or of a cell has no finished paragraph to
			//         charge, and the charge went onto the paragraph being built, which the table's own
			//         FIRST CELL then became: the body walk had to read a cell's counters to find the
			//         anchor, and could no longer skip cells whole;
			//       - a table inside a cell finished last the PREVIOUS cell's paragraph, which is a
			//         different thread altogether -- the right total in the wrong place.
			//   So the paragraph the table stands in is put aside here, charged, and picked up again at
			//     </Table>. The cells read in between cannot inherit anything from it.
			//   @warning in front of its text when there is none yet (the ordinary shape -- a <Br /> or
			//     a <Cell> has just ended the previous paragraph), behind it when there is. Both land on
			//     the SAME paragraph, so its length is right either way; which end it is decides where
			//     the paragraph's text is taken to start.
			//
			// **AND ONE THING THE BODY DOES THAT A CELL MUST NOT: END THE PARAGRAPH FIRST.** In the
			//   body, text before a table and text after it have always been read as two paragraphs
			//   (measured that way: 53 against the model's 52, and 21 against 20, when an EMPTY one was
			//   invented as well -- hence the "not empty" test). A CELL cannot do that: a cell's
			//   paragraphs are found by ADJACENCY (CellRunEnd), and the inner table's own cells are
			//   about to be pushed between them -- so the two halves would no longer be a run, the run's
			//   length would come out short, and KCMStoryCellBases would refuse the story.
			//   So inside a cell the paragraph is kept whole and simply put aside.
			if (tableDepth == 0 && !current.empty())
				Flush::Do(current, currentAttrs, paraPos, paragraphs, attrsPerPara);

			Suspended held;
			held.fText = current;
			held.fAttrs = currentAttrs;
			held.fParaPos = paraPos;
			held.fCellOrdinal = cellOrdinal;
			held.fCellRow = cellRow;
			held.fCellCol = cellCol;
			held.fOpen = open;

			const int32 myOrdinal = nextTableOrdinal;
			if (held.fText.empty())
			{
				held.fAttrs.fLeadingChars += tableChars;
				if (held.fAttrs.fLeadingTables == 0)
					held.fAttrs.fLeadingTable = myOrdinal;
				++held.fAttrs.fLeadingTables;
			}
			else
			{
				held.fAttrs.fExtraChars += tableChars;
				if (held.fAttrs.fExtraTables == 0)
					held.fAttrs.fExtraTable = myOrdinal;
				++held.fAttrs.fExtraTables;
			}
			suspended.push_back(held);

			current.clear();
			currentAttrs = KCMParaAttrs();
			paraPos = 0;
			cellOrdinal = KCMParaAttrs::kNotACell;
			cellRow = -1;
			cellCol = -1;
			open = OpenRange();			// the inner table is read from a clean slate

			openTables.push_back(nextTableOrdinal++);
			++tableDepth;
			pos = gt + 1;
		}
		else if (xml.compare(lt, 8, "</Table>") == 0)
		{
			if (tableDepth > 0)
			{
				--tableDepth;
				if (!openTables.empty())
					openTables.pop_back();

				// Back to the paragraph this table stood in, exactly as it was left -- the body's when
				//   the table was a top-level one, a cell's when it was nested.
				if (!suspended.empty())
				{
					const Suspended& held = suspended.back();
					current = held.fText;
					currentAttrs = held.fAttrs;
					paraPos = held.fParaPos;
					cellOrdinal = held.fCellOrdinal;
					cellRow = held.fCellRow;
					cellCol = held.fCellCol;
					open = held.fOpen;
					suspended.pop_back();
				}
			}
			pos = lt + 8;
		}
		else if (tableDepth > 0 && xml.compare(lt, 6, "<Cell ") == 0)
		{
			// **A CELL IS A PARAGRAPH.** Nothing is pushed here -- the cell's text is collected the
			//   same way any paragraph's is, and </Cell> ends it. What this tag is read for is the ROW:
			//   Name is "column:row", and a change of row means the previous cell was the last one in
			//   its row, which is where the row's terminator character belongs.
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
				// text actually sits (KCMParaAttrs::fTableOrdinal).
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

			// @warning **nothing is charged here any more.** A row boundary used to add one character
			//   at this point, which put the table's characters among its CELLS -- the right total in
			//   the wrong place. The whole run is charged where the table stands, out of BodyRowCount;
			//   see the <Table> branch above.

			// **WHICH CELL THIS PARAGRAPH IS -- AND EVERY OTHER PARAGRAPH OF THE SAME CELL.** The
			//   identity is REMEMBERED here and stamped again after each break inside the cell (see
			//   the <Br /> branch): a cell may hold several paragraphs, and a flush resets the
			//   attributes.
			//   **THE TABLE IS THE INNERMOST ONE OPEN, WHICH A RUNNING COUNTER CANNOT SAY.** Tables
			//     are numbered in the order they appear, nested ones included, so a counter is right
			//     until an inner table closes -- and every cell after that </Table> belongs to the
			//     OUTER table again, whose number the counter has left behind. So the open tables are
			//     kept on a stack and the cell takes the top of it.
			cellOrdinal = KCMParaAttrs::kNotACell;
			cellRow = -1;
			cellCol = -1;
			if (!openTables.empty() && row >= 0 && col >= 0)
			{
				cellOrdinal = openTables.back();
				cellRow = row;
				cellCol = col;
			}
			Stamp::Cell(currentAttrs, cellOrdinal, cellRow, cellCol);

			pos = gt + 1;
		}
		else if (tableDepth > 0 && xml.compare(lt, 7, "</Cell>") == 0)
		{
			// The cell's text ends here. Its own break character is what ComputedLength adds to
			// every paragraph, which is exactly the "+1 per cell" the measurement asked for.
			Flush::Do(current, currentAttrs, paraPos, paragraphs, attrsPerPara);
			cellOrdinal = KCMParaAttrs::kNotACell;	// what follows is not this cell's
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

			// @warning measured on the DECODED text: an entity is several bytes and one character, and
			//   a ruby span put at the undecoded offset would drift by the difference.
			std::string decoded = piece;
			DecodeEntities(decoded);
			const int32 pieceLen = CountCodePoints(decoded);

			if (!open.fRuby.empty() && pieceLen > 0)
			{
				// **WHETHER THIS CONTINUES THE SPAN BEFORE IT IS INDESIGN'S ANSWER, NOT A GUESS MADE
				//   HERE.** RubyFlag says it: "1" opens a run, "2" carries the same run onto the next
				//   base character (measured on two real snippets -- group ruby こはく over 琥珀 comes
				//   out as flag 1 then flag 2, each range holding one character and the same RubyString).
				//   @warning a first attempt fused "adjacent spans with the same reading" instead, which
				//     LOOKS equivalent and is not: two mono rubies that happen to read the same (各 and
				//     々 both かく) sit next to each other with the same string, and fusing them would
				//     report one ruby where the document has two.
				// **TWO WAYS TO BE A CONTINUATION**, and both are needed:
				//   (1) the flag says so (group ruby, whose run crosses ranges);
				//   (2) this range has already contributed -- one range can hold several <Content> runs
				//     when the base text changes formatting part-way through, and that is one ruby over
				//     one stretch, not two.
				const bool16 continues = (open.fContinues || open.fStarted) ? kTrue : kFalse;
				if (continues && !currentAttrs.fRuby.empty() &&
					currentAttrs.fRuby.back().fValue == open.fRuby &&
					currentAttrs.fRuby.back().fStart + currentAttrs.fRuby.back().fLen == paraPos)
				{
					currentAttrs.fRuby.back().fLen += pieceLen;
				}
				else
				{
					currentAttrs.fRuby.push_back(KCMAttrSpan(paraPos, pieceLen, open.fRuby, open.fGroup));
				}
				open.fStarted = kTrue;
			}

			// **KENTEN JOINS ADJACENT RANGES, WHERE RUBY MUST NOT** (measured on
			//   work\Snippet_3209A15EF.idms). Ruby needed RubyFlag to tell "the same reading
			//   continues" from "a second reading that happens to read the same" -- 各 and 画 both
			//   かく sit side by side and are two rubies. Kenten has no such pair: it is one mark PER
			//   CHARACTER, so two adjacent stretches of the same kind ARE one stretch, and the range
			//   boundary between them says nothing about the document -- it is wherever some OTHER
			//   formatting happened to change.
			//   So joining is not an optimisation here, it is what makes the reading stable: without
			//     it, italicising one word inside a kenten run would split the span, which any
			//     comparison of these spans would read as a change to the marks themselves.
			//     (@warning nothing compares them today -- see fKenten -- so this is what keeps the
			//      answer right for whoever turns that back on, not something the panel depends on now.)
			if (!open.fKenten.empty() && pieceLen > 0)
			{
				if (!currentAttrs.fKenten.empty() &&
					currentAttrs.fKenten.back().fValue == open.fKenten &&
					currentAttrs.fKenten.back().fStart + currentAttrs.fKenten.back().fLen == paraPos)
				{
					currentAttrs.fKenten.back().fLen += pieceLen;
				}
				else
				{
					currentAttrs.fKenten.push_back(KCMAttrSpan(paraPos, pieceLen, open.fKenten));
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

			// @warning **RubyFlag IS NOT A BOOLEAN.** GROUP ruby is written as one range per base
			//   character, every one carrying the same RubyString, with the flag going "1" on the
			//   first and "2" on the second (measured on こはく over 琥珀). Reading it as "on = 1"
			//   drops every character of a group ruby except the first -- and the mono snippet, which
			//   has nothing but "1" in it, could never have shown that.
			// **SETTLED BY MEASUREMENT.** On two base characters, "the run continues" and "this is
			//   character number 2" predict the same file, so the first sample could not tell them
			//   apart. A FIVE-character group ruby did: こはくねこたろう over 琥珀猫太郎 came out
			//   **1, 2, 2, 2, 2** -- so the flag says OPEN or CONTINUE, and is neither a count nor a
			//   running number. Anything that is not "1" is a continuation, which is what this reads.
			// @warning an empty ruby string is no ruby, whatever the flag says: that is the official
			//   rule (GetRubyStrandInfo turns the attribute off when the string it read has length 0).
			if (!flag.empty() && flag != "0" && !str.empty())
			{
				open.fRuby = str;
				open.fGroup = (AttrValue(tag, "RubyType") == "GroupRuby") ? kTrue : kFalse;
				open.fContinues = (flag != "1") ? kTrue : kFalse;
			}
			else
			{
				open.fRuby.clear();
				open.fGroup = kFalse;
				open.fContinues = kFalse;
			}
			open.fStarted = kFalse;		// a new range has contributed nothing yet

			// **KENTEN IS ONE ATTRIBUTE ON THE RANGE** -- no flag, no run to rebuild. Measured: five
			//   characters marked with one kind come out as ONE range carrying KentenKind, where the
			//   same five characters with ruby come out as five ranges.
			// @warning **OFF IS A VALUE, NOT AN ABSENCE.** The SDK turns kenten off by putting
			//   Kenten_None into the attribute rather than removing it (SnpPerformTextAttrKenten), so a
			//   range can carry a kind that means "no mark". Both spellings are refused because only
			//   the attribute name has been seen in a real file so far -- the OFF value has not.
			const std::string kenten = AttrValue(tag, "KentenKind");
			open.fKenten = (kenten.empty() || kenten == "KentenNone" || kenten == "Kenten_None")
						 ? std::string() : kenten;

			pos = gt + 1;
		}
		else if (xml.compare(lt, 22, "</CharacterStyleRange>") == 0)
		{
			// Safe even for group ruby, which spans several ranges: each range carries its own
			//   RubyString and its own flag, so the next one re-opens what it needs.
			open.Close();
			pos = lt + 22;
		}
		else if (const size_t itemNameLen = AnchoredItemTagLen(xml, lt))
		{
			// **AN ANCHORED PAGE ITEM IS ONE CHARACTER OF THE BODY.** What the text model holds there
			//   is U+FFFC, the object replacement character, and that is what goes in -- the reader is
			//   shown an anchor sign instead, which is a decision made once where the row's text is
			//   built, not here ([[one-question-one-place]]). What this file records is what the
			//   document has.
			// **NOTHING ELSE HAS TO KNOW.** The character sits in `current` like any other, so the
			//   paragraph's length, the diff, the ruby offsets and the cell run all follow without
			//   being told. An anchor being ADDED or REMOVED therefore comes out of the ordinary text
			//   diff as "+" or "-" -- there is no separate test for it anywhere.
			// @warning **a DIFFERENCE cannot be seen this way and is not meant to be:** every anchor
			//   is the same character, so swapping a rectangle for an oval leaves the text identical.
			//   Reporting that would mean remembering WHICH item each character was -- the same shape
			//   as ruby, and deliberately not done in this first pass.
			current.append("\xEF\xBF\xBC");		// U+FFFC in UTF-8 - one code point
			paraPos += 1;
			pos = SkipItemElement(xml, lt, itemNameLen, storyEnd);
		}
		else if (xml.compare(lt, 4, "<Br ") == 0 || xml.compare(lt, 4, "<Br/") == 0)
		{
			Flush::Do(current, currentAttrs, paraPos, paragraphs, attrsPerPara);
			// **AND THE PARAGRAPH THAT FOLLOWS IS STILL IN THE SAME CELL**, if the break was inside
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

	**IT STANDS BESIDE IndexInStory ON PURPOSE.** The two are one convention seen from both ends
	-- this one says how a run's paragraphs are strung together, that one says where a position
	in the resulting string lands in the document -- and they were in different files while only
	one of them knew about the invisible characters a table adds. What came of that is below, at
	IndexInStory.

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

/** Where each ORDINARY paragraph begins, counted the way the text model counts.

	**A TABLE'S CELLS ARE NOT WHERE THE SNIPPET PUTS THEM.** In the XML a table's cells sit
	between the story's own <Content>, exactly where the table stands; the text model keeps them
	after the whole of the story's own text and says so: "TextStoryThreads that are ALWAYS at
	greater TextIndex than the Text Story Thread that the Table Model is anchored in"
	(ITableTextContent.h). So this walks the BODY only:

	  - a cell contributes nothing -- neither its text nor its break. Its entry is left at -1 for
	    KCMStoryCellBases to fill in from the document;
	  - the table's OWN RUN of characters does count -- one per body row -- because it stands in
	    the body where the table is (KCMParaAttrs::fExtraChars has the measurements).

	@warning **this is not the total.** ComputedLength still adds up everything, because that
	  total is what LengthAgrees checks against ITextModel::TotalLength. The two answer different
	  questions and both are needed.

	@param outTableAnchors where each BODY table's first character sits, and which table it is.
		The caller checks these against the document's own answer
		(ITextStoryThreadDict::GetAnchorTextRange). That is what catches a story shape this walk
		does not understand -- two tables in a row, say, where the second table's character is
		charged to a CELL of the first and so never reaches the body count. Such a story is
		refused rather than aimed wrongly.
		@warning a NESTED table is not in here: it stands in a cell, and where that cell's text
		  sits is not known until the document has been asked. KCMStoryCellBases checks those
		  the same way, once it has the answer.
*/
inline void BodyParagraphStarts(const std::vector<std::string>& paragraphs,
								const std::vector<KCMParaAttrs>& attrs,
								std::vector<int32>& starts,
								std::vector<KCMTableAnchor>& outTableAnchors)
{
	starts.assign(paragraphs.size(), -1);
	outTableAnchors.clear();

	int32 index = 0;
	for (size_t i = 0; i < paragraphs.size(); ++i)
	{
		const bool16 haveAttrs = (i < attrs.size());

		// **A CELL IS SKIPPED WHOLE -- ITS TABLES INCLUDED.** Everything about a cell is
		//   elsewhere: its text, its break, and any table standing inside it. That last one was
		//   the addition: a nested table charges the cell it stands in, and counting those
		//   characters here would push the body along by a table that is not in the body at all.
		if (haveAttrs && attrs[i].IsCell())
			continue;

		// Characters standing IN FRONT of this paragraph are a table's own, and a table's own
		// character is in the body wherever its cells end up.
		if (haveAttrs)
		{
			// ONE ANCHOR PER TABLE, at the first of the characters it stands on. @warning two tables
			//   sharing a boundary cannot be told apart from a sum, so neither is reported and the
			//   resolver refuses the story on the count.
			if (attrs[i].fLeadingTables == 1)
				outTableAnchors.push_back(KCMTableAnchor(attrs[i].fLeadingTable, index));
			index += attrs[i].fLeadingChars;
		}

		starts[i] = index;
		index += CountCodePoints(paragraphs[i]) + 1;

		if (haveAttrs)
		{
			if (attrs[i].fExtraTables == 1)
				outTableAnchors.push_back(KCMTableAnchor(attrs[i].fExtraTable, index));
			index += attrs[i].fExtraChars;
		}
	}
}

/** A run of consecutive paragraphs that all sit in the same PLACE: the story's body, or one
	cell.

	**WHY A ROW IS CUT HERE.** A cell IS a paragraph, so two paragraphs that both changed and
	  happen to be next to each other in the snippet go into ONE run of the paragraph diff --
	  even when one of them is body text and the other is inside the table. The row then reads as
	  one edit spanning the words before the table, the cell, and whatever follows, and its mark
	  covers all the unchanged text in between (measured: a row covering 22 characters for two
	  edits of one character each).
	@warning adjacent paragraphs in the same place STILL share a row -- that part is right, and a
	  cell holding several paragraphs depends on it.
	The body appears more than once when a table stands in the middle of it; those are separate
	runs, because they are not next to each other.
*/
struct ParaRegion
{
	int32	fStart;		///< first paragraph of the run
	int32	fCount;		///< how many paragraphs
	int32	fTable;		///< KCMParaAttrs::kNotACell for body text, else which table
	int32	fRow;		///< grid row when it is a cell, -1 otherwise
	int32	fCol;		///< grid column when it is a cell, -1 otherwise
	int32	fFootnote;	///< KCMParaAttrs::kNotAFootnote for body text and cells, else which footnote

	ParaRegion() : fStart(0), fCount(0), fTable(KCMParaAttrs::kNotACell), fRow(-1), fCol(-1),
				   fFootnote(KCMParaAttrs::kNotAFootnote) {}

	/** The same place - not the same paragraphs.

		⚠**THE FOOTNOTE HAS TO BE ASKED ABOUT HERE AND FILLED IN BY ParagraphRegions, OR NEITHER
		  WORKS.** If only one of the two is done, every region carries the same -1 and the runs are
		  cut exactly as they were before - the code reads as though footnotes were separated while
		  nothing separates them. */
	bool16 SamePlaceAs(const ParaRegion& other) const
	{
		return fTable == other.fTable && fRow == other.fRow && fCol == other.fCol
			&& fFootnote == other.fFootnote;
	}
};

/** The places a run of paragraphs passes through, in order. */
inline void ParagraphRegions(const std::vector<KCMParaAttrs>& attrs, int32 start, int32 count,
							 std::vector<ParaRegion>& out)
{
	out.clear();
	for (int32 i = start; i < start + count; ++i)
	{
		ParaRegion here;
		here.fStart = i;
		here.fCount = 1;
		if (i >= 0 && static_cast<size_t>(i) < attrs.size())
		{
			if (attrs[i].IsCell())
			{
				here.fTable = attrs[i].fTableOrdinal;
				here.fRow = attrs[i].fCellRow;
				here.fCol = attrs[i].fCellCol;
			}
			// ⚠NOT an `else`: the two are separate fields and a paragraph could in principle carry
			//   both (a table inside a footnote). Asking them one at a time keeps that possible.
			here.fFootnote = attrs[i].fFootnoteOrdinal;
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

/** Add one piece to the answer.

	**ONE PLACE**, because SplitRunAtPlaces below describes a piece in four different situations
	and every one of them has to fill in all four fields. Written out at each, a field added to
	RegionPair would be set at three of them and forgotten at the fourth -- and the piece that
	forgot it would still compile and still look right.
*/
inline void AppendPair(std::vector<RegionPair>& out,
					   int32 sourceStart, int32 sourceCount, int32 targetStart, int32 targetCount)
{
	RegionPair piece;
	piece.fSourceStart = sourceStart;
	piece.fSourceCount = sourceCount;
	piece.fTargetStart = targetStart;
	piece.fTargetCount = targetCount;
	out.push_back(piece);
}

/** Cut one run of the paragraph diff into one piece per PLACE (see ParaRegion).

	**WHEN IT DOES NOT CUT, IT SAYS SO BY ANSWERING WITH ONE PIECE.** Three shapes are cut:
	  - a pure insertion: every piece goes in at the same spot in the older version;
	  - a pure deletion: the mirror of it;
	  - a replacement whose two sides pass through the SAME places in the same order.
	@warning anything else -- the table itself gained or lost cells between the versions, say --
	  is left whole. There is no honest way to pair the halves up, and one row that is too wide is
	  better than several that point at the wrong cells.
*/
inline void SplitRunAtPlaces(const std::vector<KCMParaAttrs>& sourceAttrs,
							 int32 aStart, int32 aCount,
							 const std::vector<KCMParaAttrs>& targetAttrs,
							 int32 bStart, int32 bCount,
							 std::vector<RegionPair>& out)
{
	out.clear();

	std::vector<ParaRegion> aRegions;
	std::vector<ParaRegion> bRegions;
	ParagraphRegions(sourceAttrs, aStart, aCount, aRegions);
	ParagraphRegions(targetAttrs, bStart, bCount, bRegions);

	if (aCount == 0 && bRegions.size() > 1)
	{
		// Nothing of the older version is involved: every piece goes in at the same spot.
		for (size_t i = 0; i < bRegions.size(); ++i)
			AppendPair(out, aStart, 0, bRegions[i].fStart, bRegions[i].fCount);
		return;
	}

	if (bCount == 0 && aRegions.size() > 1)
	{
		for (size_t i = 0; i < aRegions.size(); ++i)
			AppendPair(out, aRegions[i].fStart, aRegions[i].fCount, bStart, 0);
		return;
	}

	if (aRegions.size() > 1 && aRegions.size() == bRegions.size())
	{
		for (size_t i = 0; i < aRegions.size(); ++i)
		{
			if (!aRegions[i].SamePlaceAs(bRegions[i]))
			{
				out.clear();				// the versions do not pass through the same places
				AppendPair(out, aStart, aCount, bStart, bCount);
				return;
			}

			AppendPair(out, aRegions[i].fStart, aRegions[i].fCount,
					   bRegions[i].fStart, bRegions[i].fCount);
		}
		return;
	}

	AppendPair(out, aStart, aCount, bStart, bCount);	// the run, left whole
}

/** Where the run of paragraphs belonging to ONE cell ends -- the index one past its last.

	**A CELL IS NOT ALWAYS ONE PARAGRAPH.** It holds one for every Return pressed in it, and a
	MERGED cell holds the paragraphs of everything merged into it. They arrive together, in
	order, all naming the same (table, row, column) -- which is what this walks.

	@warning asked about a paragraph that is not a cell, this answers with the paragraph itself,
	  so the caller gets an EMPTY run rather than a guess.
*/
inline size_t CellRunEnd(const std::vector<KCMParaAttrs>& attrs, size_t at)
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

/** How many tables the story holds, nested ones included -- one more than the highest number
	used.

	It is worked out from the paragraphs rather than reported by the reader because it has to
	agree with fTableOrdinal, and the surest way to make two numbers agree is to have only one of
	them. @warning all three places a table can leave its number are read: the cells that belong
	  to it, and the two ends of the paragraph it stands in (a table with no cells at all is not
	  a shape InDesign writes, but reading only the cells would make this quietly depend on that).
*/
inline int32 TableCount(const std::vector<KCMParaAttrs>& attrs)
{
	int32 highest = -1;
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (attrs[i].fTableOrdinal > highest)
			highest = attrs[i].fTableOrdinal;
		if (attrs[i].fLeadingTable > highest)
			highest = attrs[i].fLeadingTable;
		if (attrs[i].fExtraTable > highest)
			highest = attrs[i].fExtraTable;
	}
	return highest + 1;
}

/** How many characters such a run occupies in the text model: every paragraph AND its break.

	This is what a cell's own text story thread holds, which is why the resolver can check it
	against ITextStoryThread::GetTextStart's span before trusting the cell it found.

	**AND THE INVISIBLE CHARACTERS THE CELL CARRIES.** A cell holding a TABLE is charged that
	table's own run, the same way a body paragraph is charged a top-level table's -- and the
	thread the document reports for such a cell is that much longer. Leaving them out made the
	check disagree (2 against the document's 3) and turned the story away after everything else
	about it was right.
*/
inline int32 CellRunLength(const std::vector<std::string>& paragraphs,
						   const std::vector<KCMParaAttrs>& attrs, size_t from, size_t to)
{
	int32 length = 0;
	for (size_t i = from; i < to && i < paragraphs.size(); ++i)
	{
		length += CountCodePoints(paragraphs[i]) + 1;
		if (i < attrs.size())
			length += attrs[i].fLeadingChars + attrs[i].fExtraChars;
	}
	return length;
}

/** Where an offset into that joined string lands in the document, as a TextIndex.

	**WHY THIS IS NOT `base + offset`.** JoinParagraphs puts ONE character between two
	paragraphs; the document may not have them that close together at all. Two faults of this
	shape were found on one day:
	  (1) a table's own character and a row's terminator sit at exactly such a boundary, so a
	      change covering two ADJACENT paragraphs put the second one short by them -- MEASURED on
	      the real table snippet: at 21 where the document has 22, and at 31 against 32;
	  (2) and a table's CELLS are not between the paragraphs at all (see BodyParagraphStarts
	      above), so the distance across a table is nothing like the sum of what the snippet lists
	      there -- MEASURED: a change to the paragraph after a table selected a character inside a
	      cell instead.
	@warning SILENT, and no length check can catch either: LengthAgrees compares TOTALS, which
	  were right. Both are answered the same way: every paragraph's position is LOOKED UP, never
	  added up.

	@param paragraphs every paragraph of the story.
	@param starts one document position per paragraph -- BodyParagraphStarts for the ordinary
		ones, KCMStoryCellBases for the cells. @warning an unresolved cell (-1) would answer
		nonsense, so the caller refuses the story before asking anything of this.
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
	// **IT LOOKS EVERY PARAGRAPH UP INSTEAD OF ADDING UP THE HIDDEN CHARACTERS BETWEEN THEM.**
	//   The old form walked from the run's base and, at each paragraph break, added that
	//   paragraph's fExtraChars. That is right only while the document lays paragraphs out in
	//   the order the snippet lists them, and A TABLE BREAKS EXACTLY THAT: the text model keeps
	//   a table's cells AFTER the whole of the story's own text (ITableTextContent.h), so no
	//   amount of adding gets from the paragraph before a table to the one after it. MEASURED:
	//   a change to the paragraph following a table selected a character inside a cell instead.
	//   Positions come from the table `starts`, which is built once -- the body by
	//     BodyParagraphStarts, the cells by asking the document (KCMStoryCellBases). This walk
	//     only has to decide WHICH paragraph the offset falls in.
	int32 joined = 0;		// where the paragraph being looked at begins, inside the joined string
	for (int32 i = 0; i < count; ++i)
	{
		const int32 which = start + i;
		if (which < 0 || which >= static_cast<int32>(paragraphs.size())
			|| which >= static_cast<int32>(starts.size()))
			break;

		const int32 len = CountCodePoints(paragraphs[which]);

		// @warning AT the break belongs to the paragraph BEFORE it, which is the rule the old form
		//   kept ("the offset is at or before it -- nothing to add") and the paragraph-start table
		//   agrees with: the next paragraph's start is where the character AFTER the break sits.
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

/** True when two spans lists differ -- the question "did only the ruby change?" is this one
	asked about a paragraph whose text came out identical.

	@warning compared as an ordered list, not as a set: moving the same ruby onto different
	  characters is a change, and so is reordering two of them.
*/
inline bool16 SpansDiffer(const KCMAttrSpanList& a, const KCMAttrSpanList& b)
{
	if (a.size() != b.size())
		return kTrue;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].fStart != b[i].fStart || a[i].fLen != b[i].fLen || a[i].fValue != b[i].fValue)
			return kTrue;
		// Mono turned into group is a change even when every reading is the same: 琥珀 read as
		//   こ+はく and 琥珀 read as こはく are different typesetting, and the reader asked to see it.
		if ((a[i].fGroup != 0) != (b[i].fGroup != 0))
			return kTrue;
	}
	return kFalse;
}

}	// namespace KCMSnippetText

#endif // __KCMSnippetText_h__

// End, KCMSnippetText.h.
