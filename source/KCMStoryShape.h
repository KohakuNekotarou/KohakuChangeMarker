//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the shape a story is read into, and the rules every spelling shares
//
//  WHAT THIS IS FOR. The reader exports a document's stories, edits them outside InDesign (in
//  Word), and imports them again. The import never writes into their document without saying so:
//  it takes a Task Start first, puts the edits in, and lets the Story comparison show them one at
//  a time. The design is docs/superpowers/specs/2026-09-19-kcm-story-docx-roundtrip-design.md.
//
//  ★**THIS HEADER IS THE "WHAT", KCMStoryDocx.h IS THE "HOW".** Until 2026-09-21 the two lived in
//   KCMStoryHtml.h, because the HTML spelling came first and the structs were written for it. The
//   HTML road was retired that day on the user's word ("Word format only"), and the structs -
//   which never knew anything about HTML - moved here rather than into the .docx writer: a shape
//   that belongs to one format is a shape the next format has to fight.
//
//  *** EVERYTHING HERE IS A PURE FUNCTION. No SDK type, no document, no file. ***
//
//  *** WHAT IS CARRIED: body paragraphs, ruby, kenten, tate-chu-yoko, warichu, the invisible
//  characters, footnotes and tables - nested ones included. *** What is deliberately NOT carried is
//  written down where it is decided: an ENDNOTE's words, which live in a story of their own (Story).
//  A reader REFUSES, with a reason, anything it does not understand - it does not skip it. Skipping
//  would drop the reader's words without telling anybody, which is the one failure this whole
//  round trip exists to prevent.
//
//========================================================================================
#ifndef __KCMStoryShape_h__
#define __KCMStoryShape_h__

#include "BaseType.h"		// int32, bool16, kTrue / kFalse
#include "KCMParaText.h"	// KCMAttrSpan / KCMAttrSpanList - ruby travels in the shape it already has

#include <string>
#include <vector>

namespace KCMStoryShape
{

/** Where one footnote's reference stands in a paragraph (2026-09-19).

	★★**THE .docx FORMAT CARRIES THIS.** Word cannot hold a footnote without its reference standing
	  in the body, so KCMStoryDocx writes it. ⚠It is compared only when asked for (Same's
	  withNoteRefs): the HTML spelling, which carried no such thing, was the reason that switch is
	  there, and it is kept because a caller that has no places to compare still has to be able to
	  ask whether two stories agree about everything else.
	fAt counts fText's code points, 0 to the paragraph's length: the reference stands BEFORE the
	character at fAt, and at the length it ends the paragraph. fNote indexes Story::fNotes. */
struct NoteRef
{
	int32	fAt;
	int32	fNote;

	NoteRef() : fAt(0), fNote(0) {}
};

/** One paragraph, in the shape every side uses.

	fText is the paragraph's text as KCMTextRead reports it - see the file header. fRuby is
	KCMParaAttrs::fRuby unchanged, so a reading that survives the trip is the same object the
	comparison already knows how to talk about. */
struct Para
{
	std::string			fText;
	KCMAttrSpanList		fRuby;		// as KCMParaAttrs::fRuby
	KCMAttrSpanList		fKenten;	// as KCMParaAttrs::fKenten - fValue is the KIND's name
	KCMAttrSpanList		fTcy;		// as KCMParaAttrs::fTcy - fValue is the characters it covers
	KCMAttrSpanList		fWarichu;	// as KCMParaAttrs::fWarichu (2026-09-17) - the same, for a warichu
	std::vector<NoteRef>	fNoteRefs;	// in order of fAt - see NoteRef
};

/** The mark meant when a format names none. ★Kept as a value of its own rather than spelt into
	the reader: a file may well say "emphasis" without saying WHICH mark, and InDesign's own
	default is the only answer that does not throw the reader's intention away. */
extern const char* const kKentenDefaultValue;

/** The short name a kenten value travels under ("BlackCircle", "Custom-203b", "Kind7"), and the
	value such a name means.

	*** THE NAME CARRIES THE TRUTH AND THE LOOK IS SOMEBODY ELSE'S BUSINESS. *** InDesign's kind
	names travel verbatim, so a kind this build has never heard of - KCMTextRead answers "Kind7"
	for one - survives the trip untouched. A CUSTOM mark is the kind plus a character, and the
	character is written as its code point in lowercase hex, exactly as an invisible character is:
	one rule, spelled the same way twice, and no second notation to remember.

	@warning a custom mark is BMP-only, and that is InDesign's limit rather than this format's -
	  the attribute behind it is an int16 (KCMTextRead says so where it reads it). */
bool16 KentenClassOf(const std::string& value, std::string& outClass);
bool16 KentenValueOfClass(const std::string& cls, std::string& outValue);

/** kTrue when cp cannot be written as itself and has to travel as a placeholder naming its code
	point.

	*** A RULE, NOT A LIST. *** The control characters, the invisible formatting ones, the object
	replacement character and the private use area. Nothing enumerates which characters InDesign
	has: a version that invents a new one is written and read correctly by code that was never
	changed, because what the placeholder names is the code point itself.

	@warning U+000D never reaches here - it is the paragraph boundary - and U+0009 TAB is written
	  as itself, because it is a character anybody can type. Everything else invisible, the forced
	  line break U+000A included, becomes a placeholder. */
bool16 IsInvisible(int32 cp);

/** One table cell. Its contents are paragraphs and the tables standing among them, exactly as
	the body's are - a cell is a small body. The tables are not held HERE (see Table::fInTable):
	keeping them in one flat list, in document order, is what lets the ordinals stay a single
	count and keeps this header free of a type that contains itself. */
struct Cell
{
	int32				fColSpan;
	int32				fRowSpan;
	std::vector<Para>	fParas;

	Cell() : fColSpan(1), fRowSpan(1) {}
};

/** One table row. fHeader marks a header row, which the reader may not change. */
struct Row
{
	bool16				fHeader;
	std::vector<Cell>	fCells;

	Row() : fHeader(kFalse) {}
};

/** One table: where it stands, and what is in it.

	★★**A TABLE STANDS EITHER IN THE BODY OR IN ONE CELL OF ANOTHER TABLE** (2026-09-16). fInTable
	  is -1 for the body, or the ordinal of the table whose cell holds this one; fInRow and
	  fInCell then say which cell, counted the way that row's cells run - the same count
	  ColumnsOfRow produces on the document's side, so a merged cell is one cell in both.
	  ⚠**fParaIndex IS RELATIVE TO WHATEVER HOLDS IT**: the body's paragraphs when fInTable is
	   -1, and that cell's paragraphs otherwise.

	@warning fSplitsPara is the case a table can stand in the MIDDLE of a paragraph - measured, and
	  the rest of the diff assumes it away (KCMTextRead::TakeAttrFor says where). */
struct Table
{
	int32				fOrdinal;
	int32				fParaIndex;
	int32				fOffset;
	int32				fInTable;	// -1 = the body, else the ordinal of the table this one is inside
	int32				fInRow;		// which row of that table
	int32				fInCell;	// and which cell of that row, in the order its cells run
	bool16				fSplitsPara;
	std::vector<Row>	fRows;

	Table() : fOrdinal(0), fParaIndex(0), fOffset(0), fInTable(-1), fInRow(0), fInCell(0),
			  fSplitsPara(kFalse) {}
};

/** A whole story: its body, the tables standing in it, and its footnotes' own paragraphs.

	★**A FOOTNOTE HOLDS ITS OWN PARAGRAPHS**, which carry nothing of their own, so adding one is
	  copying a paragraph and a copied paragraph cannot end up in the wrong note. The ORDER of
	  fNotes is the pairing: the first is note 1.
	@warning an ENDNOTE's words are not here. They live in another story (kEndnoteStoryBoss) and
	  arrive as a story of their own, with its own file. */
struct Story
{
	std::vector<Para>					fBody;
	std::vector<Table>					fTables;	// EVERY table, nested ones included, document order
	std::vector< std::vector<Para> >	fNotes;		// [n] = footnote n's paragraphs
	bool16								fVertical;	// the story is set vertically (tategaki)

	Story() : fVertical(kFalse) {}
};

/** Add to `inOutSeen` every kenten value this story uses - body, cells and notes alike - skipping
	any already there.

	⚠**A CELL'S AND A NOTE'S MARKS COUNT.** A list built from the body alone leaves a custom mark
	 inside a table with no name of its own, and a mark with no name cannot be written. */
void CollectKentenValues(const Story& s, std::vector<std::string>& inOutSeen);

/** kTrue when the two stories are the same in every way this shape carries.

	★★★**THIS IS WHAT LETS A WRITE CHECK ITSELF** (2026-09-16). The exporter writes a story, reads
	  the bytes straight back, and compares - all of it in memory, with no SDK type and no document
	  anywhere near it - so a story the format cannot carry is refused BEFORE its file is written,
	  rather than after somebody has spent a day editing it.
	★**AND IT IS THE HARNESS'S CHECK TOO**: Read(Write(x)) == x is the property the round trip
	  exists to keep, and this is that sentence as a function.
	@param outWhy where the first difference is, in words a status line can show.
	@param withNoteRefs kTrue compares Para::fNoteRefs as well. ★**KCMStoryDocx's own check passes
	  kTrue**, because that format carries where a reference stands. */
bool16 Same(const Story& a, const Story& b, std::string& outWhy, bool16 withNoteRefs = kFalse);

}	// namespace KCMStoryShape

#endif // __KCMStoryShape_h__

// End, KCMStoryShape.h.
