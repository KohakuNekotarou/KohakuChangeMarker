//========================================================================================
//
//  KCMStoryDocx.cpp -- see the header.
//
//========================================================================================

#include "KCMStoryDocx.h"
#include "KCMTextDiff.h"		// ToCodePoints - the one walk over UTF-8, shared with the HTML writer

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

		if (KCMStoryHtml::IsInvisible(cp))
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
			   && !KCMStoryHtml::IsInvisible(cps[static_cast<size_t>(j)])
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
					"<w:rt><w:r><w:rPr><w:sz w:val=\"10\"/></w:rPr><w:t>";
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
	  piece takes the references with from < fAt <= to, and the first piece takes fAt == 0 too. */
KCMStoryHtml::Para Slice(const KCMStoryHtml::Para& p, const std::vector<int32>& byteAt,
						 int32 from, int32 to)
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
		if ((at > from && at <= to) || (at == 0 && from == 0))
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

			if (!AppendParagraph(Slice(p, byteAt, pos, at), first ? kFalse : kTrue, out, whyNot))
				return kFalse;
			if (!AppendTable(s, t, depth, out, whyNot))
				return kFalse;
			pos = at;
			first = kFalse;
		}

		// ⚠THE TAIL IS ALWAYS WRITTEN - the header says which two rules of Word's ask for it.
		if (!AppendParagraph(Slice(p, byteAt, pos, n), first ? kFalse : kTrue, out, whyNot))
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

}	// namespace KCMStoryDocx

// End, KCMStoryDocx.cpp.
