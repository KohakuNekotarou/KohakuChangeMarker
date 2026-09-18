//========================================================================================
//
//  KCMStoryDocx.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storyhtml-test).
#include "VCPlugInHeaders.h"

#include "KCMStoryDocx.h"
#include "KCMTextDiff.h"		// ToCodePoints - the one walk over UTF-8, shared with the HTML writer
#include "KCMXmlTree.h"		// the reading half walks Word's parts through this

#include <cstdio>

namespace KCMStoryDocx
{

namespace
{

/*	Look
	What one character carries BESIDES a ruby. Two neighbours with the same Look share a run.

	★**THE SPANS ARE KEPT BY INDEX, NOT AS FLAGS**, for the warichu's sake: Word ties the runs of
	  one "two lines in one" region together by <w:eastAsianLayout w:id>, so every run of one
	  warichu has to carry ONE id - including the runs a tate-chu-yoko inside it cuts off. The
	  id is therefore the warichu's own number whenever there is one, and the tate-chu-yoko's
	  otherwise; paragraphs are separate regions anyway, so the numbers start again in each.
*/
struct Look
{
	const std::string*	fKentenClass;	// nil, or the class KentenClassOf gave ("BlackCircle")
	int32				fTcy;			// which tate-chu-yoko span covers it, -1 for none
	int32				fWarichu;		// which warichu span covers it, -1 for none

	Look() : fKentenClass(nil), fTcy(-1), fWarichu(-1) {}

	bool16 SameAs(const Look& o) const
	{
		if ((fKentenClass == nil) != (o.fKentenClass == nil))
			return kFalse;
		if (fKentenClass != nil && *fKentenClass != *o.fKentenClass)
			return kFalse;
		return (fTcy == o.fTcy && fWarichu == o.fWarichu) ? kTrue : kFalse;
	}
};

void AppendEscaped(const std::string& utf8, size_t from, size_t to, std::string& out)
{
	for (size_t i = from; i < to && i < utf8.size(); ++i)
	{
		const char c = utf8[i];
		if (c == '&')		out += "&amp;";
		else if (c == '<')	out += "&lt;";
		else if (c == '>')	out += "&gt;";
		else if (c == '"')	out += "&quot;";	// harmless in text, needed in an attribute
		else				out += c;
	}
}

void AppendHex(int32 cp, bool16 upper, std::string& out)
{
	char buf[16] = { 0 };
	std::snprintf(buf, sizeof(buf), upper ? "%04X" : "%04x", static_cast<unsigned int>(cp));
	out += buf;
}

void AppendNumber(int32 n, std::string& out)
{
	char buf[16] = { 0 };
	std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(n));
	out += buf;
}

/** <w:rPr> for a Look, or nothing at all. The order inside is the schema's: the style first. */
void AppendRunProps(const Look& look, std::string& out)
{
	if (look.fKentenClass == nil && look.fTcy < 0 && look.fWarichu < 0)
		return;

	out += "<w:rPr>";
	if (look.fKentenClass != nil)
	{
		out += "<w:rStyle w:val=\"kenten-";
		AppendEscaped(*look.fKentenClass, 0, look.fKentenClass->size(), out);
		out += "\"/>";
	}
	if (look.fTcy >= 0 || look.fWarichu >= 0)
	{
		out += "<w:eastAsianLayout w:id=\"";
		AppendNumber((look.fWarichu >= 0) ? (look.fWarichu + 1) * 100 : look.fTcy + 1, out);
		out += "\"";
		if (look.fWarichu >= 0)
			out += " w:combine=\"1\"";
		if (look.fTcy >= 0)
			out += " w:vert=\"1\"";
		out += "/>";
	}
	out += "</w:rPr>";
}

/** One invisible character as a locked content control. ★THE TAG IS THE TRUTH - see the header. */
void AppendPlaceholder(int32 cp, std::string& out)
{
	out += "<w:sdt><w:sdtPr><w:alias w:val=\"U+";
	AppendHex(cp, kTrue, out);
	out += "\"/><w:tag w:val=\"u";
	AppendHex(cp, kFalse, out);
	out += "\"/><w:lock w:val=\"sdtContentLocked\"/></w:sdtPr><w:sdtContent><w:r><w:t>\xE2\x9F\xA6";
	AppendHex(cp, kTrue, out);
	out += "\xE2\x9F\xA7</w:t></w:r></w:sdtContent></w:sdt>";
}

void AppendNoteReference(int32 note, std::string& out)
{
	out += "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteReference w:id=\"";
	AppendNumber(note + 1, out);		// ids -1 and 0 are Word's own separators; the notes start at 1
	out += "\"/></w:r>";
}

bool16 IsBreakOrTab(int32 cp)
{
	return (cp == 0x000A || cp == 0x0009) ? kTrue : kFalse;
}

/** Whether a character has to travel as a placeholder rather than as itself.

	KCMStoryHtml::IsInvisible is the rule, and XML adds two characters to it: U+FFFE and U+FFFF
	are not characters XML 1.0 allows at all, and ONE of them anywhere makes the whole package
	unreadable - HTML shrugs at them, which is why the shared rule does not name them. (The
	control characters, which XML forbids too, are IsInvisible's already.) */
bool16 NeedsPlaceholder(int32 cp)
{
	if (KCMStoryHtml::IsInvisible(cp) || cp == 0xFFFE || cp == 0xFFFF)
		return kTrue;

	// ★TWO MORE, FOUND ON 2026-09-19 BY ASKING "IS THE ESCAPING ENTIRELY SOUND?" and written as
	//   failing tests first:
	//   - a LONE SURROGATE (U+D800..U+DFFF). Text that has been through a bad conversion can hold
	//     one; it is not an XML character, and one of them makes the package unreadable.
	//   - U+000D. It "never arrives" - it IS the paragraph boundary - and if it ever did, an XML
	//     parser would turn it into a line feed without a word. A placeholder says it was there.
	if ((cp >= 0xD800 && cp <= 0xDFFF) || cp == 0x000D)
		return kTrue;

	return kFalse;
}

/*	AppendRuns
	The characters [from, to) of a paragraph, as runs.

	⚠**inRuby: A PLACEHOLDER CANNOT STAND IN A <w:rubyBase>** - the schema takes runs there and
	 nothing else - so the paragraph is refused rather than the character dropped.
	★A forced break and a tab are asked about BEFORE IsInvisible: the break is one of its
	 characters, and here it is spelt as itself.
*/
bool16 AppendRuns(const std::string& text, const std::vector<int32>& cps,
				  const std::vector<int32>& byteAt, const std::vector<Look>& looks,
				  int32 from, int32 to, bool16 inRuby, std::string& out, std::string& whyNot)
{
	int32 i = from;
	while (i < to)
	{
		const size_t at = static_cast<size_t>(i);
		const int32 cp = cps[at];

		if (IsBreakOrTab(cp))
		{
			out += "<w:r>";
			AppendRunProps(looks[at], out);
			out += (cp == 0x000A) ? "<w:br/></w:r>" : "<w:tab/></w:r>";
			++i;
			continue;
		}

		if (NeedsPlaceholder(cp))
		{
			if (inRuby)
			{
				whyNot = "a ruby stands over an invisible character (U+";
				AppendHex(cp, kTrue, whyNot);
				whyNot += "), which a Word ruby cannot hold";
				return kFalse;
			}
			AppendPlaceholder(cp, out);
			++i;
			continue;
		}

		int32 j = i + 1;
		while (j < to
			   && !IsBreakOrTab(cps[static_cast<size_t>(j)])
			   && !NeedsPlaceholder(cps[static_cast<size_t>(j)])
			   && looks[static_cast<size_t>(j)].SameAs(looks[at]))
			++j;

		const size_t b0 = static_cast<size_t>(byteAt[at]);
		const size_t b1 = (static_cast<size_t>(j) < byteAt.size())
						  ? static_cast<size_t>(byteAt[static_cast<size_t>(j)]) : text.size();
		out += "<w:r>";
		AppendRunProps(looks[at], out);
		out += "<w:t xml:space=\"preserve\">";
		AppendEscaped(text, b0, b1, out);
		out += "</w:t></w:r>";
		i = j;
	}
	return kTrue;
}

/** Give every character of each span its number, clipped to the paragraph. */
void PaintSpans(const KCMAttrSpanList& spans, int32 n, std::vector<Look>& looks, bool16 warichu)
{
	for (size_t k = 0; k < spans.size(); ++k)
	{
		const int32 from = (spans[k].fStart > 0) ? spans[k].fStart : 0;
		const int32 to = (spans[k].fStart + spans[k].fLen < n) ? spans[k].fStart + spans[k].fLen : n;
		for (int32 i = from; i < to; ++i)
		{
			if (warichu)
				looks[static_cast<size_t>(i)].fWarichu = static_cast<int32>(k);
			else
				looks[static_cast<size_t>(i)].fTcy = static_cast<int32>(k);
		}
	}
}

}	// anonymous namespace

bool16 WriteParagraphContent(const KCMStoryHtml::Para& p, std::string& out, std::string& whyNot)
{
	std::vector<int32> cps;
	std::vector<int32> byteAt;
	KCMTextDiff::ToCodePoints(p.fText, &cps, &byteAt);
	const int32 n = static_cast<int32>(cps.size());

	// ---- what each character carries ---------------------------------------------------------
	std::vector<Look> looks(static_cast<size_t>(n));

	// The classes are settled BEFORE anything is written, so a kind with no name refuses the
	// paragraph whole instead of leaving half of it in `out`.
	std::vector<std::string> kentenClasses(p.fKenten.size());
	for (size_t k = 0; k < p.fKenten.size(); ++k)
	{
		if (!KCMStoryHtml::KentenClassOf(p.fKenten[k].fValue, kentenClasses[k]))
		{
			whyNot = "a kenten kind this format cannot name: " + p.fKenten[k].fValue;
			return kFalse;
		}
	}
	for (size_t k = 0; k < p.fKenten.size(); ++k)
	{
		const int32 from = (p.fKenten[k].fStart > 0) ? p.fKenten[k].fStart : 0;
		const int32 to = (p.fKenten[k].fStart + p.fKenten[k].fLen < n)
						 ? p.fKenten[k].fStart + p.fKenten[k].fLen : n;
		for (int32 i = from; i < to; ++i)
			looks[static_cast<size_t>(i)].fKentenClass = &kentenClasses[k];
	}
	PaintSpans(p.fTcy, n, looks, kFalse);
	PaintSpans(p.fWarichu, n, looks, kTrue);

	// ⚠**A READING IS TEXT TOO, AND IT HAS NOWHERE TO PUT A PLACEHOLDER**: a <w:rt> takes a run and
	//  nothing else. A reading holding a character that cannot travel as itself would otherwise go
	//  into the XML raw - and a control character there makes the whole package unreadable (found
	//  by reading this again on 2026-09-19, and true: the test was written first and failed).
	for (size_t k = 0; k < p.fRuby.size(); ++k)
	{
		std::vector<int32> reading;
		KCMTextDiff::ToCodePoints(p.fRuby[k].fValue, &reading, nil);
		for (size_t c = 0; c < reading.size(); ++c)
		{
			if (NeedsPlaceholder(reading[c]) || IsBreakOrTab(reading[c]))
			{
				whyNot = "a ruby's reading holds a character that cannot be written as itself (U+";
				AppendHex(reading[c], kTrue, whyNot);
				whyNot += ")";
				return kFalse;
			}
		}
	}

	std::vector<int32> rubyOf(static_cast<size_t>(n), -1);
	for (size_t k = 0; k < p.fRuby.size(); ++k)
	{
		const int32 from = (p.fRuby[k].fStart > 0) ? p.fRuby[k].fStart : 0;
		const int32 to = (p.fRuby[k].fStart + p.fRuby[k].fLen < n) ? p.fRuby[k].fStart + p.fRuby[k].fLen : n;
		for (int32 i = from; i < to; ++i)
		{
			if (rubyOf[static_cast<size_t>(i)] < 0)
				rubyOf[static_cast<size_t>(i)] = static_cast<int32>(k);
		}
	}

	// ---- the walk ------------------------------------------------------------------------------
	//
	// Written into a string of its own, so that a refusal half way leaves `out` as it was found.
	std::string made;
	const std::vector<KCMStoryHtml::NoteRef>& refs = p.fNoteRefs;
	size_t ref = 0;
	int32 i = 0;

	while (i < n)
	{
		while (ref < refs.size() && refs[ref].fAt <= i)
		{
			AppendNoteReference(refs[ref].fNote, made);
			++ref;
		}

		const int32 k = rubyOf[static_cast<size_t>(i)];
		if (k >= 0)
		{
			int32 j = i;
			while (j < n && rubyOf[static_cast<size_t>(j)] == k)
				++j;

			if (ref < refs.size() && refs[ref].fAt < j)
			{
				whyNot = "a footnote reference stands inside a ruby's base text, which a Word ruby cannot hold";
				return kFalse;
			}

			const std::string& reading = p.fRuby[static_cast<size_t>(k)].fValue;
			made += "<w:r><w:ruby><w:rubyPr><w:rubyAlign w:val=\"distributeSpace\"/><w:hps w:val=\"10\"/>"
					"<w:hpsRaise w:val=\"18\"/><w:hpsBaseText w:val=\"21\"/><w:lid w:val=\"ja-JP\"/></w:rubyPr>"
					"<w:rt><w:r><w:rPr><w:sz w:val=\"10\"/></w:rPr><w:t xml:space=\"preserve\">";	// a reading's own spaces are its own
			AppendEscaped(reading, 0, reading.size(), made);
			made += "</w:t></w:r></w:rt><w:rubyBase>";
			if (!AppendRuns(p.fText, cps, byteAt, looks, i, j, kTrue, made, whyNot))
				return kFalse;
			made += "</w:rubyBase></w:ruby></w:r>";
			i = j;
			continue;
		}

		int32 j = i + 1;
		while (j < n && rubyOf[static_cast<size_t>(j)] < 0
			   && !(ref < refs.size() && refs[ref].fAt == j))
			++j;
		if (!AppendRuns(p.fText, cps, byteAt, looks, i, j, kFalse, made, whyNot))
			return kFalse;
		i = j;
	}

	// The references standing at the paragraph's end (and any whose place is past it).
	for (; ref < refs.size(); ++ref)
		AppendNoteReference(refs[ref].fNote, made);

	out += made;
	return kTrue;
}

namespace
{

const char* const kContinuedProps = "<w:pPr><w:pStyle w:val=\"kcm-continued\"/></w:pPr>";

// A table inside a table inside a table... The data cannot really do this (a nested table comes
// later in Story::fTables than the one it stands in), so this only stops a malformed Story from
// walking for ever.
const int32 kDeepestNesting = 32;

/** The characters [from, to) of a paragraph, with every span that reaches into them, re-based.

	★**A NOTE REFERENCE AT A CUT BELONGS TO THE PIECE BEFORE IT**: the pieces are cut where tables
	  stand, and a reference at that very place is written in front of the table - once. So a
	  piece takes the references with from < fAt <= to, and the FIRST piece takes fAt == 0 too.
	  ⚠**"FIRST" IS SAID BY THE CALLER, NOT READ OFF from == 0**: a table standing at the very head
	   of its paragraph makes a first piece that is [0, 0) and a second that ALSO starts at 0, and
	   both took the reference (2026-09-19 - found by reading, and a failing test before the fix).
	⚠A RUBY CUT BY A TABLE comes out as two rubies with the same reading, one on each side. That is
	  what the page shows, and it is not what the Story said - so the export's own check (stage 2)
	  will refuse such a story rather than let it round-trip into two. */
KCMStoryHtml::Para Slice(const KCMStoryHtml::Para& p, const std::vector<int32>& byteAt,
						 int32 from, int32 to, bool16 first)
{
	KCMStoryHtml::Para piece;

	const size_t n = byteAt.size();
	const size_t b0 = (static_cast<size_t>(from) < n) ? static_cast<size_t>(byteAt[static_cast<size_t>(from)])
													  : p.fText.size();
	const size_t b1 = (static_cast<size_t>(to) < n) ? static_cast<size_t>(byteAt[static_cast<size_t>(to)])
													: p.fText.size();
	piece.fText = p.fText.substr(b0, b1 - b0);

	const KCMAttrSpanList* const lists[4] = { &p.fRuby, &p.fKenten, &p.fTcy, &p.fWarichu };
	KCMAttrSpanList* const made[4] = { &piece.fRuby, &piece.fKenten, &piece.fTcy, &piece.fWarichu };
	for (int32 which = 0; which < 4; ++which)
	{
		for (size_t k = 0; k < lists[which]->size(); ++k)
		{
			KCMAttrSpan span = (*lists[which])[k];
			const int32 s0 = (span.fStart > from) ? span.fStart : from;
			const int32 s1 = (span.fStart + span.fLen < to) ? span.fStart + span.fLen : to;
			if (s1 <= s0)
				continue;
			span.fStart = s0 - from;
			span.fLen = s1 - s0;
			made[which]->push_back(span);
		}
	}

	for (size_t k = 0; k < p.fNoteRefs.size(); ++k)
	{
		const int32 at = p.fNoteRefs[k].fAt;
		if ((at > from && at <= to) || (first && at <= from))
		{
			KCMStoryHtml::NoteRef ref = p.fNoteRefs[k];
			ref.fAt = at - from;
			piece.fNoteRefs.push_back(ref);
		}
	}
	return piece;
}

/** One <w:p>. An empty one with nothing to say about itself is <w:p/>. */
bool16 AppendParagraph(const KCMStoryHtml::Para& piece, bool16 continued, std::string& out,
					   std::string& whyNot)
{
	std::string content;
	if (!WriteParagraphContent(piece, content, whyNot))
		return kFalse;

	if (content.empty() && !continued)
	{
		out += "<w:p/>";
		return kTrue;
	}
	out += "<w:p>";
	if (continued)
		out += kContinuedProps;
	out += content;
	out += "</w:p>";
	return kTrue;
}

/*	Slot
	One <w:tc> of a row as Word wants it: an anchor cell of the Story's, or a cell a vertical merge
	from above covers.
*/
struct Slot
{
	int32	fCell;		// index into Row::fCells, or -1 for a covered cell
	int32	fSpan;		// grid columns
	bool16	fRestarts;	// an anchor that reaches down into the rows below

	Slot() : fCell(-1), fSpan(1), fRestarts(kFalse) {}
};

struct Carry
{
	int32	fRowsLeft;
	int32	fSpan;

	Carry() : fRowsLeft(0), fSpan(1) {}
};

/** Lay one row out over the grid. `carry` is the merges reaching down from the rows above, by the
	grid column each starts at, and is updated for the row below. @return the columns the row used. */
int32 LayRowOut(const KCMStoryHtml::Row& row, std::vector<Carry>& carry, std::vector<Slot>& outSlots)
{
	outSlots.clear();

	int32 col = 0;
	size_t next = 0;
	for (;;)
	{
		if (static_cast<size_t>(col) < carry.size() && carry[static_cast<size_t>(col)].fRowsLeft > 0)
		{
			Slot covered;
			covered.fSpan = carry[static_cast<size_t>(col)].fSpan;
			--carry[static_cast<size_t>(col)].fRowsLeft;
			outSlots.push_back(covered);
			col += covered.fSpan;
			continue;
		}

		if (next >= row.fCells.size())
		{
			// The anchors are used up. A merge reaching down further to the right still needs its
			// covered cell; a ragged gap before it is left a gap.
			size_t further = static_cast<size_t>(col);
			while (further < carry.size() && carry[further].fRowsLeft <= 0)
				++further;
			if (further >= carry.size())
				break;
			col = static_cast<int32>(further);
			continue;
		}

		const KCMStoryHtml::Cell& cell = row.fCells[next];
		Slot anchor;
		anchor.fCell = static_cast<int32>(next);
		anchor.fSpan = (cell.fColSpan > 0) ? cell.fColSpan : 1;
		anchor.fRestarts = (cell.fRowSpan > 1) ? kTrue : kFalse;

		if (carry.size() < static_cast<size_t>(col + anchor.fSpan))
			carry.resize(static_cast<size_t>(col + anchor.fSpan));
		if (anchor.fRestarts)
		{
			carry[static_cast<size_t>(col)].fRowsLeft = cell.fRowSpan - 1;
			carry[static_cast<size_t>(col)].fSpan = anchor.fSpan;
		}

		outSlots.push_back(anchor);
		col += anchor.fSpan;
		++next;
	}
	return col;
}

bool16 AppendBlocks(const KCMStoryHtml::Story& s, const std::vector<KCMStoryHtml::Para>& paras,
					int32 inTable, int32 inRow, int32 inCell, int32 depth, std::string& out,
					std::string& whyNot);

bool16 AppendTable(const KCMStoryHtml::Story& s, size_t index, int32 depth, std::string& out,
				   std::string& whyNot)
{
	const KCMStoryHtml::Table& table = s.fTables[index];

	// ---- the grid, settled before a byte is written: <w:tblGrid> comes first in the markup ------
	std::vector< std::vector<Slot> > rows(table.fRows.size());
	int32 columns = 1;
	{
		std::vector<Carry> carry;
		for (size_t r = 0; r < table.fRows.size(); ++r)
		{
			const int32 used = LayRowOut(table.fRows[r], carry, rows[r]);
			if (used > columns)
				columns = used;
		}
	}
	const int32 columnWidth = 9000 / columns;		// twips; Word re-fits them, this is a start

	out += "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders>"
		   "<w:top w:val=\"single\" w:sz=\"4\"/><w:left w:val=\"single\" w:sz=\"4\"/>"
		   "<w:bottom w:val=\"single\" w:sz=\"4\"/><w:right w:val=\"single\" w:sz=\"4\"/>"
		   "<w:insideH w:val=\"single\" w:sz=\"4\"/><w:insideV w:val=\"single\" w:sz=\"4\"/>"
		   "</w:tblBorders></w:tblPr><w:tblGrid>";
	for (int32 c = 0; c < columns; ++c)
	{
		out += "<w:gridCol w:w=\"";
		AppendNumber(columnWidth, out);
		out += "\"/>";
	}
	out += "</w:tblGrid>";

	for (size_t r = 0; r < table.fRows.size(); ++r)
	{
		out += "<w:tr>";
		if (table.fRows[r].fHeader)
			out += "<w:trPr><w:tblHeader/></w:trPr>";

		for (size_t k = 0; k < rows[r].size(); ++k)
		{
			const Slot& slot = rows[r][k];
			out += "<w:tc><w:tcPr><w:tcW w:w=\"";
			AppendNumber(columnWidth * slot.fSpan, out);
			out += "\" w:type=\"dxa\"/>";
			if (slot.fSpan > 1)
			{
				out += "<w:gridSpan w:val=\"";
				AppendNumber(slot.fSpan, out);
				out += "\"/>";
			}
			if (slot.fCell < 0)
				out += "<w:vMerge/>";
			else if (slot.fRestarts)
				out += "<w:vMerge w:val=\"restart\"/>";
			out += "</w:tcPr>";

			if (slot.fCell < 0)
			{
				out += "<w:p/>";
			}
			else if (!AppendBlocks(s, table.fRows[r].fCells[static_cast<size_t>(slot.fCell)].fParas,
								   static_cast<int32>(index), static_cast<int32>(r), slot.fCell,
								   depth + 1, out, whyNot))
			{
				return kFalse;
			}
			out += "</w:tc>";
		}
		out += "</w:tr>";
	}
	out += "</w:tbl>";
	return kTrue;
}

bool16 AppendBlocks(const KCMStoryHtml::Story& s, const std::vector<KCMStoryHtml::Para>& paras,
					int32 inTable, int32 inRow, int32 inCell, int32 depth, std::string& out,
					std::string& whyNot)
{
	if (depth > kDeepestNesting)
	{
		whyNot = "the tables are nested deeper than this format writes";
		return kFalse;
	}

	if (paras.empty())
	{
		out += "<w:p/>";		// a cell of Word's cannot be empty
		return kTrue;
	}

	for (size_t i = 0; i < paras.size(); ++i)
	{
		const KCMStoryHtml::Para& p = paras[i];
		std::vector<int32> byteAt;
		KCMTextDiff::ToCodePoints(p.fText, nil, &byteAt);
		const int32 n = static_cast<int32>(byteAt.size());

		int32 pos = 0;
		bool16 first = kTrue;

		// Story::fTables is in document order, so the ones standing in this paragraph come out
		// in the order of their places.
		for (size_t t = 0; t < s.fTables.size(); ++t)
		{
			const KCMStoryHtml::Table& table = s.fTables[t];
			if (table.fInTable != inTable || table.fParaIndex != static_cast<int32>(i))
				continue;
			if (inTable >= 0 && (table.fInRow != inRow || table.fInCell != inCell))
				continue;

			int32 at = table.fOffset;
			if (at < pos)	at = pos;
			if (at > n)		at = n;

			if (!AppendParagraph(Slice(p, byteAt, pos, at, first), first ? kFalse : kTrue, out, whyNot))
				return kFalse;
			if (!AppendTable(s, t, depth, out, whyNot))
				return kFalse;
			pos = at;
			first = kFalse;
		}

		// ⚠THE TAIL IS ALWAYS WRITTEN - the header says which two rules of Word's ask for it.
		if (!AppendParagraph(Slice(p, byteAt, pos, n, first), first ? kFalse : kTrue, out, whyNot))
			return kFalse;
	}
	return kTrue;
}

}	// anonymous namespace

bool16 WriteBlocks(const KCMStoryHtml::Story& s, const std::vector<KCMStoryHtml::Para>& paras,
				   int32 inTable, int32 inRow, int32 inCell, std::string& out, std::string& whyNot)
{
	// Into a string of its own: a refusal from deep inside a cell leaves `out` as it was found.
	std::string made;
	if (!AppendBlocks(s, paras, inTable, inRow, inCell, 0, made, whyNot))
		return kFalse;
	out += made;
	return kTrue;
}

namespace
{

const char* const kXmlDeclaration = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
const char* const kWordNamespace = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
const char* const kStoryTagNamespace = "urn:kohaku:kcm:story:1";

/*	The built-in kenten kinds, and the nearest of Word's four emphasis marks for each.

	⚠**THIS LIST IS KCMStoryHtml.cpp's kKentenLooks, A SECOND TIME** - that one is in an anonymous
	 namespace with CSS beside each name, this one has Word's marks. What says the two still agree
	 is a test, not a comment: work/kcm-storydocx-test, TestKentenNames, puts every name below
	 through KCMStoryHtml's KentenValueOfClass and KentenClassOf.
	★The look is only the nearest one - sesame marks are Word's comma, the hollow ones its circle,
	 everything else its dot. The NAME carries the kind (the header says why).
*/
struct KentenStyle
{
	const char*	fClass;
	const char*	fWordMark;
};

const KentenStyle kBuiltInKenten[] =
{
	{ "BlackSesameDot",		"comma" },
	{ "WhiteSesameDot",		"comma" },
	{ "BlackCircle",		"dot" },
	{ "WhiteCircle",		"circle" },
	{ "SmallBlackCircle",	"dot" },
	{ "SmallWhiteCircle",	"circle" },
	{ "BlackTriangle",		"dot" },
	{ "WhiteTriangle",		"circle" },
	{ "Bullseye",			"circle" },
	{ "Fisheye",			"circle" }
};
const size_t kBuiltInKentenCount = sizeof(kBuiltInKenten) / sizeof(kBuiltInKenten[0]);

void AppendKentenStyle(const std::string& cls, const char* wordMark, std::string& out)
{
	out += "<w:style w:type=\"character\" w:customStyle=\"1\" w:styleId=\"kenten-";
	AppendEscaped(cls, 0, cls.size(), out);
	out += "\"><w:name w:val=\"kenten-";
	AppendEscaped(cls, 0, cls.size(), out);
	out += "\"/><w:basedOn w:val=\"DefaultParagraphFont\"/><w:qFormat/><w:rPr><w:em w:val=\"";
	out += wordMark;
	out += "\"/></w:rPr></w:style>";
}

bool16 WriteStyles(const KCMStoryHtml::Story& s, std::string& out, std::string& whyNot)
{
	out = kXmlDeclaration;
	out += "<w:styles ";
	out += kWordNamespace;
	// ★THE DEFAULTS: 10.5pt, and East Asian text that is JAPANESE. The ruby written above gives its
	//   own sizes against a 10.5pt base (hpsBaseText 21), so without this the base is Word's 10pt
	//   and the two disagree; and without the language Word breaks the lines of a Japanese story
	//   by another language's rules. No font is named: that is the reader's Word's business.
	out += ">"
		   "<w:docDefaults><w:rPrDefault><w:rPr><w:sz w:val=\"21\"/><w:szCs w:val=\"21\"/>"
		   "<w:lang w:val=\"en-US\" w:eastAsia=\"ja-JP\"/></w:rPr></w:rPrDefault></w:docDefaults>"
		   "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/>"
		   "<w:qFormat/></w:style>"
		   "<w:style w:type=\"character\" w:default=\"1\" w:styleId=\"DefaultParagraphFont\">"
		   "<w:name w:val=\"Default Paragraph Font\"/><w:uiPriority w:val=\"1\"/><w:semiHidden/></w:style>"
		   "<w:style w:type=\"paragraph\" w:customStyle=\"1\" w:styleId=\"kcm-continued\">"
		   "<w:name w:val=\"kcm-continued\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/></w:style>";

	for (size_t i = 0; i < kBuiltInKentenCount; ++i)
		AppendKentenStyle(kBuiltInKenten[i].fClass, kBuiltInKenten[i].fWordMark, out);

	// The custom marks this story uses, sorted: the order the story happens to use them in must
	// not reach the bytes.
	std::vector<std::string> values;
	KCMStoryHtml::CollectKentenValues(s, values);
	std::vector<std::string> custom;
	for (size_t v = 0; v < values.size(); ++v)
	{
		std::string cls;
		if (!KCMStoryHtml::KentenClassOf(values[v], cls))
		{
			whyNot = "a kenten kind this format cannot name: " + values[v];
			return kFalse;
		}
		bool16 builtIn = kFalse;
		for (size_t i = 0; i < kBuiltInKentenCount && !builtIn; ++i)
			builtIn = (cls == kBuiltInKenten[i].fClass) ? kTrue : kFalse;
		if (!builtIn)
			custom.push_back(cls);
	}
	for (size_t a = 1; a < custom.size(); ++a)			// a handful at most: an insertion sort
	{
		for (size_t b = a; b > 0 && custom[b] < custom[b - 1]; --b)
			custom[b].swap(custom[b - 1]);
	}
	for (size_t c = 0; c < custom.size(); ++c)
	{
		if (c == 0 || custom[c] != custom[c - 1])
			AppendKentenStyle(custom[c], "dot", out);
	}

	out += "</w:styles>";
	return kTrue;
}

/** Tick off every note the paragraphs refer to. kFalse for a reference to a note that is not there. */
bool16 TickReferences(const std::vector<KCMStoryHtml::Para>& paras, std::vector<bool16>& seen,
					  std::string& whyNot)
{
	for (size_t i = 0; i < paras.size(); ++i)
	{
		for (size_t k = 0; k < paras[i].fNoteRefs.size(); ++k)
		{
			const int32 note = paras[i].fNoteRefs[k].fNote;
			if (note < 0 || static_cast<size_t>(note) >= seen.size())
			{
				whyNot = "a reference points at footnote ";
				AppendNumber(note + 1, whyNot);
				whyNot += ", which the story does not have";
				return kFalse;
			}
			// ⚠TWICE IS AS BAD AS NEVER: two <w:footnoteReference> with one id is not a document Word
			//  can keep, and InDesign cannot make one either - so this is a damaged Story, refused.
			if (seen[static_cast<size_t>(note)])
			{
				whyNot = "footnote ";
				AppendNumber(note + 1, whyNot);
				whyNot += " is referred to more than once";
				return kFalse;
			}
			seen[static_cast<size_t>(note)] = kTrue;
		}
	}
	return kTrue;
}

/** Word cannot keep a note nothing refers to, so every note has to be reached from the text. */
bool16 EveryNoteIsReferredTo(const KCMStoryHtml::Story& s, std::string& whyNot)
{
	std::vector<bool16> seen(s.fNotes.size(), kFalse);

	if (!TickReferences(s.fBody, seen, whyNot))
		return kFalse;
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
		{
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
			{
				if (!TickReferences(s.fTables[t].fRows[r].fCells[c].fParas, seen, whyNot))
					return kFalse;
			}
		}
	}

	for (size_t n = 0; n < seen.size(); ++n)
	{
		if (!seen[n])
		{
			whyNot = "footnote ";
			AppendNumber(static_cast<int32>(n) + 1, whyNot);
			whyNot += " has no reference in the text, and Word cannot keep a note without one";
			return kFalse;
		}
	}
	return kTrue;
}

/** <w:footnotes ...>...</w:footnotes>, with no XML declaration in front of it. */
bool16 WriteFootnotesElement(const KCMStoryHtml::Story& s, std::string& out, std::string& whyNot)
{
	out = "<w:footnotes ";
	out += kWordNamespace;
	out += ">"
		   "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
		   "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/>"
		   "</w:r></w:p></w:footnote>";

	for (size_t n = 0; n < s.fNotes.size(); ++n)
	{
		out += "<w:footnote w:id=\"";
		AppendNumber(static_cast<int32>(n) + 1, out);
		out += "\">";

		// A note with no paragraph at all still gets one: its own mark has to stand somewhere.
		const size_t count = s.fNotes[n].empty() ? 1 : s.fNotes[n].size();
		for (size_t i = 0; i < count; ++i)
		{
			std::string content;
			if (i < s.fNotes[n].size() && !WriteParagraphContent(s.fNotes[n][i], content, whyNot))
				return kFalse;

			out += "<w:p>";
			if (i == 0)
				out += "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteRef/></w:r>";
			out += content;
			out += "</w:p>";
		}
		out += "</w:footnote>";
	}
	out += "</w:footnotes>";
	return kTrue;
}

void AddPart(const char* name, const std::string& bytes, std::vector<KCMZipStore::Entry>& parts)
{
	KCMZipStore::Entry e;
	e.fName = name;
	e.fBytes = bytes;
	parts.push_back(e);
}

}	// anonymous namespace

namespace
{

/** The two elements that ARE the story - <w:document> and, when it has notes, <w:footnotes> - with
	no XML declaration in front of either. Everything that can refuse a story refuses it here. */
bool16 WriteStoryElements(const KCMStoryHtml::Story& s, std::string& outDocument,
						  std::string& outFootnotes, std::string& whyNot)
{
	outDocument.clear();
	outFootnotes.clear();

	if (!EveryNoteIsReferredTo(s, whyNot))
		return kFalse;

	outDocument = "<w:document ";
	outDocument += kWordNamespace;
	outDocument += "><w:body>";
	if (!WriteBlocks(s, s.fBody, -1, 0, 0, outDocument, whyNot))
		return kFalse;
	outDocument += "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>";
	if (s.fVertical)
		outDocument += "<w:textDirection w:val=\"tbRl\"/>";
	outDocument += "</w:sectPr></w:body></w:document>";

	if (!s.fNotes.empty() && !WriteFootnotesElement(s, outFootnotes, whyNot))
		return kFalse;
	return kTrue;
}

/** "<bytes>-<crc32, 8 hex digits>" of the two elements, one after the other. */
void FingerprintOf(const std::string& documentElement, const std::string& footnotesElement,
				   std::string& out)
{
	const std::string both = documentElement + footnotesElement;
	char buf[40] = { 0 };
	std::snprintf(buf, sizeof(buf), "%u-%08x", static_cast<unsigned int>(both.size()),
				  KCMZipStore::Crc32(both.data(), both.size()));
	out = buf;
}

}	// anonymous namespace

bool16 Fingerprint(const KCMStoryHtml::Story& s, std::string& outFingerprint, std::string& whyNot)
{
	outFingerprint.clear();

	std::string documentElement;
	std::string footnotesElement;
	if (!WriteStoryElements(s, documentElement, footnotesElement, whyNot))
		return kFalse;

	FingerprintOf(documentElement, footnotesElement, outFingerprint);
	return kTrue;
}

bool16 WriteParts(const KCMStoryHtml::Story& s, int32 uid, const std::string& documentNameUtf8,
				  std::vector<KCMZipStore::Entry>& outParts, std::string& whyNot)
{
	outParts.clear();
	whyNot.clear();

	const bool16 hasNotes = s.fNotes.empty() ? kFalse : kTrue;

	// ---- everything that can refuse, before any part is made -----------------------------------
	std::string documentElement;
	std::string footnotesElement;
	if (!WriteStoryElements(s, documentElement, footnotesElement, whyNot))
		return kFalse;

	std::string styles;
	if (!WriteStyles(s, styles, whyNot))
		return kFalse;

	// ---- the parts -------------------------------------------------------------------------------
	std::string types = kXmlDeclaration;
	types += "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
			 "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
			 "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
			 "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
			 "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
			 "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>";
	if (hasNotes)
		types += "<Override PartName=\"/word/footnotes.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/>";
	types += "<Override PartName=\"/customXml/itemProps1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.customXmlProperties+xml\"/>"
			 "</Types>";
	AddPart("[Content_Types].xml", types, outParts);

	std::string rootRels = kXmlDeclaration;
	rootRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
				"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
				"</Relationships>";
	AddPart("_rels/.rels", rootRels, outParts);

	AddPart("word/document.xml", std::string(kXmlDeclaration) + documentElement, outParts);

	std::string docRels = kXmlDeclaration;
	docRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
			   "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
			   "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
			   "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml\" Target=\"../customXml/item1.xml\"/>";
	if (hasNotes)
		docRels += "<Relationship Id=\"rId4\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes\" Target=\"footnotes.xml\"/>";
	docRels += "</Relationships>";
	AddPart("word/_rels/document.xml.rels", docRels, outParts);

	AddPart("word/styles.xml", styles, outParts);

	std::string settings = kXmlDeclaration;
	settings += "<w:settings ";
	settings += kWordNamespace;
	// ★★TRACKING IS ON, AND PROTECTED AS "TRACKED CHANGES ONLY" (2026-09-19, the user's pick): the
	//   import tells Word's changes from everybody else's by Word's own revision marks, so an editor
	//   who switches tracking off by accident costs themselves that (the import then compares the
	//   whole text - Fingerprint, in the header). Word's protection of this kind makes the switch
	//   unavailable. ⚠NO PASSWORD, deliberately: it is there to stop an accident, not a person -
	//   whoever means to lift it can, from Word's own Review tab.
	//   The order is the schema's (CT_Settings): trackRevisions, then documentProtection.
	settings += "><w:trackRevisions/><w:documentProtection w:edit=\"trackedChanges\" w:enforcement=\"1\"/>";
	if (hasNotes)
		settings += "<w:footnotePr><w:footnote w:id=\"-1\"/><w:footnote w:id=\"0\"/></w:footnotePr>";
	settings += "</w:settings>";
	AddPart("word/settings.xml", settings, outParts);

	if (hasNotes)
		AddPart("word/footnotes.xml", std::string(kXmlDeclaration) + footnotesElement, outParts);

	// ---- the tag: which story this is, and a fingerprint of it as written ------------------------
	//
	// ★★★**NOT A WORD OF THE STORY IS IN HERE** (the user's decision, 2026-09-19, going back on the
	//   "origin" this part held for half a day - the whole story a second time, hidden). A file is
	//   handed on, and used again for something else; text nobody can see would travel with it,
	//   and would still name the old story after the visible one had been replaced.
	//   WHAT WORD CHANGED IS TOLD BY WORD'S OWN REVISION MARKS. The fingerprint is what says
	//   whether those marks are the whole truth: the import rebuilds the story as it stood (the
	//   deletions put back, the insertions left out), takes ITS fingerprint, and compares. The same
	//   -> only Word's changes are shown. Different -> tracking was off for some of the editing,
	//   or the changes were accepted, or the file holds something else by now: the import then
	//   compares the whole text, the way the HTML import does, and says that it did.
	// ⚠**NO LINE BREAK AFTER THE DECLARATION.** Word does not copy this part through a save, it
	//  parses it and writes it out again, and what it writes has none (measured 2026-09-19: the
	//  part came back two bytes shorter, the CRLF and nothing else). Written the way Word writes
	//  it, the part is the same bytes before and after - which is what lets a test say so.
	std::string fingerprint;
	FingerprintOf(documentElement, footnotesElement, fingerprint);

	std::string tag = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><kcm:story xmlns:kcm=\"";
	tag += kStoryTagNamespace;
	tag += "\" uid=\"";
	AppendNumber(uid, tag);
	tag += "\" document=\"";
	AppendEscaped(documentNameUtf8, 0, documentNameUtf8.size(), tag);
	tag += "\" format=\"1\" fingerprint=\"";
	tag += fingerprint;
	tag += "\"/>";
	AddPart("customXml/item1.xml", tag, outParts);

	// The item's id is fixed: it names the KIND of part, and a fresh one per file would be the one
	// thing making two exports of a story differ.
	std::string props = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\r\n"
						"<ds:datastoreItem ds:itemID=\"{5B0F4C3A-1D2E-4F60-8A7B-9C0D1E2F3A4B}\" "
						"xmlns:ds=\"http://schemas.openxmlformats.org/officeDocument/2006/customXml\">"
						"<ds:schemaRefs><ds:schemaRef ds:uri=\"";
	props += kStoryTagNamespace;
	props += "\"/></ds:schemaRefs></ds:datastoreItem>";
	AddPart("customXml/itemProps1.xml", props, outParts);

	std::string itemRels = kXmlDeclaration;
	itemRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
				"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps\" Target=\"itemProps1.xml\"/>"
				"</Relationships>";
	AddPart("customXml/_rels/item1.xml.rels", itemRels, outParts);

	return kTrue;
}

bool16 Write(const KCMStoryHtml::Story& s, int32 uid, const std::string& documentNameUtf8,
			 std::string& outDocx, std::string& whyNot)
{
	outDocx.clear();

	std::vector<KCMZipStore::Entry> parts;
	if (!WriteParts(s, uid, documentNameUtf8, parts, whyNot))
		return kFalse;

	KCMZipStore::Write(parts, outDocx);
	return kTrue;
}

//========================================================================================
//  THE READING HALF
//========================================================================================

namespace
{

/** "269" -> 269. kFalse for anything that is not a plain decimal number. */
bool16 ParseDecimal(const std::string& text, int32& out)
{
	if (text.empty() || text.size() > 10)
		return kFalse;
	int32 value = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] < '0' || text[i] > '9')
			return kFalse;
		value = value * 10 + (text[i] - '0');
	}
	out = value;
	return kTrue;
}

}	// anonymous namespace

bool16 ReadTag(const std::string& customXmlPart, Tag& out, std::string& whyNot)
{
	out = Tag();
	whyNot.clear();

	KCMXmlTree tree;
	if (!tree.Parse(customXmlPart.data(), customXmlPart.size(), whyNot))
		return kFalse;
	const int32 root = tree.Root();
	if (root < 0 || !tree.Is(root, kStoryTagNamespace, "story"))
		return kTrue;			// somebody else's part: present kFalse, and nothing wrong

	out.fPresent = kTrue;

	const std::string* format = tree.Attr(root, "format");
	if (format == nil || !ParseDecimal(*format, out.fFormat))
	{
		whyNot = "the story tag names no format";
		return kFalse;
	}
	if (out.fFormat != 1)
	{
		whyNot = "the story tag is of format ";
		AppendNumber(out.fFormat, whyNot);
		whyNot += ", and this build reads format 1";
		return kFalse;
	}

	const std::string* uid = tree.Attr(root, "uid");
	if (uid == nil || !ParseDecimal(*uid, out.fUid) || out.fUid <= 0)
	{
		whyNot = "the story tag's uid is not a number";
		return kFalse;
	}

	const std::string* document = tree.Attr(root, "document");
	if (document != nil)
		out.fDocument = *document;
	const std::string* fingerprint = tree.Attr(root, "fingerprint");
	if (fingerprint == nil || fingerprint->empty())
	{
		whyNot = "the story tag carries no fingerprint";
		return kFalse;
	}
	out.fFingerprint = *fingerprint;
	return kTrue;
}

}	// namespace KCMStoryDocx

// End, KCMStoryDocx.cpp.
