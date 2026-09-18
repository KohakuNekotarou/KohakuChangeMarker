//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a story's text as HTML, and back
//
//  WHAT THIS IS FOR. The reader exports a document's stories, edits them outside InDesign (in an
//  editor, or in a browser, or by handing the file to somebody else), and imports them again. The
//  import never writes into their document: it builds the edited text into the task-start copy and
//  lets the existing Story comparison show it, one change at a time, so nothing goes in that the
//  reader has not looked at. The design is docs/superpowers/specs/2026-09-15-kcm-story-text-
//  roundtrip-design.md and it, not this header, is where the decisions and their reasons live.
//
//  *** EVERYTHING HERE IS A PURE FUNCTION. No SDK type, no document, no file. ***
//  That is deliberate and it is the whole reason the format can be trusted: the writer and the
//  reader are built and run OUTSIDE InDesign, against each other, in work/kcm-storyhtml-test
//  (build.cmd - one command, about a second). The property being tested is one sentence:
//
//      Read(Write(x)) == x
//
//  A round trip that loses nothing is not something a comparison can be asked about afterwards -
//  a document that comes back subtly different reads as "the user edited it". So it is settled
//  here, where a failure is a failing test rather than a wrong mark on somebody's page.
//  (KCMXmlInject.h is the same shape for the same reason; work/kescm-snippet-test and
//  work/kcm-attrdiff-test are the other two harnesses, and this one's stub headers come from them.)
//
//  *** THE TEXT IS KCMTextRead's TEXT. *** fText below is a paragraph exactly as ReadStory reports
//  it, invisible characters included. Turning U+FFFC into <span class="ufffc"></span> is the
//  WRITER's business and turning it back is the READER's; nothing upstream or downstream has to
//  know those characters were ever markup. The four characters ReadStory takes OUT (U+0016 and
//  U+0017 for tables, U+0004 and U+0005 for note references) are not in fText either - the tables
//  carry their own places, and a note's reference is not carried at all (Para says why).
//
//  *** WHAT IS CARRIED (2026-09-16): body paragraphs, ruby, kenten, the invisible characters,
//  footnotes and tables - nested ones included. *** ⚠**THIS PARAGRAPH SAID "BODY PARAGRAPHS ONLY"
//  UNTIL 2026-09-16**, which was true when it was written (2026-09-15) and was left behind by
//  Tasks 2 to 5 of the plan it names; the harness has had passing tests for all five since.
//  ★Tate-chu-yoko and warichu (2026-09-17) travel as <span class="tate-chu-yoko"> and
//  <span class="warichu">, the ON/OFF of each and nothing of its settings.
//  What is deliberately NOT carried is written down where it is decided: a note's REFERENCE
//  position (Para), and an ENDNOTE's words, which live in a story of their own (Story).
//  Read REFUSES, with a reason, any element it does not understand - it does not skip it. Skipping
//  would drop the reader's words without telling anybody, which is the one failure this whole file
//  exists to prevent.
//
//========================================================================================
#ifndef __KCMStoryHtml_h__
#define __KCMStoryHtml_h__

#include "BaseType.h"		// int32, bool16, kTrue / kFalse
#include "KCMParaText.h"	// KCMAttrSpan / KCMAttrSpanList - ruby travels in the shape it already has

#include <string>
#include <vector>

namespace KCMStoryHtml
{

/** Where one footnote's reference stands in a paragraph (2026-09-19).

	★★**THE DOCX FORMAT CARRIES THIS AND THE HTML ONE DOES NOT.** Word cannot hold a footnote
	  without its reference standing in the body, so KCMStoryDocx writes it; the HTML decision of
	  2026-09-16 (Para, below) stands exactly as it was - KCMStoryHtml::Write never writes this,
	  Read never fills it, and Same looks at it only when asked to.
	fAt counts fText's code points, 0 to the paragraph's length: the reference stands BEFORE the
	character at fAt, and at the length it ends the paragraph. fNote indexes Story::fNotes. */
struct NoteRef
{
	int32	fAt;
	int32	fNote;

	NoteRef() : fAt(0), fNote(0) {}
};

/** One paragraph, in the shape both sides use.

	fText is the paragraph's text as KCMTextRead reports it - see the file header. fRuby is
	KCMParaAttrs::fRuby unchanged, so a reading that survives the trip is the same object the
	comparison already knows how to talk about.

	⚠**WHERE A NOTE'S REFERENCE STANDS IS NOT CARRIED** (the user's decision, 2026-09-16). The
	  reader of these files edits the WORDS of a note, and a marker in the body is not a word:
	  it was written as <sup><a href="#n1">1</a></sup> and it is written no more. What that
	  removes is a whole class of defect - the <sup>'s number was the body's own running count
	  while the <li>'s id was the note's ordinal, and the two disagreed the moment a note lived
	  in a table cell or an InDesign NOTE took an ordinal (measured: a document whose only
	  reference pointed at the wrong one of its two notes). */
struct Para
{
	std::string			fText;
	KCMAttrSpanList		fRuby;		// as KCMParaAttrs::fRuby
	KCMAttrSpanList		fKenten;	// as KCMParaAttrs::fKenten - fValue is the KIND's name
	KCMAttrSpanList		fTcy;		// as KCMParaAttrs::fTcy - fValue is the characters it covers
	KCMAttrSpanList		fWarichu;	// as KCMParaAttrs::fWarichu (2026-09-17) - the same, for a warichu
	std::vector<NoteRef>	fNoteRefs;	// in order of fAt. DOCX ONLY - see NoteRef; HTML neither writes nor reads it
};

/** The default mark, for an <em> that names none. See kKentenDefaultValue's comment. */
extern const char* const kKentenDefaultValue;

/** The class this format writes for one kenten value, WITHOUT the "kenten-" in front
	("BlackCircle", "Custom-203b", "Kind7"), and the value that such a class means.

	*** THE CLASS CARRIES THE TRUTH AND THE STYLESHEET ONLY CARRIES THE LOOK. *** InDesign's kind
	names travel verbatim, so a kind this build has never heard of - KCMTextRead answers "Kind7"
	for one - survives the trip untouched. A CUSTOM mark is the kind plus a character, and the
	character is written as its code point in lowercase hex, exactly as an invisible character is
	(uXXXX): one rule, spelled the same way twice, and no second notation to remember.

	*** AND THE ELEMENT IS PLAIN <em>, WHICH IS THE POINT. *** A kenten IS stress emphasis, CSS has
	text-emphasis-style for exactly this, and the stylesheet this file writes says so - so the
	document explains its own convention to anybody who opens it. The reader's side of that is
	kKentenDefaultValue: an <em> carrying no class of ours is still a kenten, because somebody
	asked for emphasis in the ordinary way and meant it.

	@warning a custom mark is BMP-only, and that is InDesign's limit rather than this format's -
	  the attribute behind it is an int16 (KCMTextRead says so where it reads it). */
bool16 KentenClassOf(const std::string& value, std::string& outClass);
bool16 KentenValueOfClass(const std::string& cls, std::string& outValue);

/** kTrue when cp has to be written as <span class="uXXXX"></span> rather than as itself.

	*** A RULE, NOT A LIST. *** The control characters, the invisible formatting ones, the object
	replacement character and the private use area. Nothing enumerates which characters InDesign
	has: a version that invents a new one is written and read correctly by code that was never
	changed, because the class in the markup is the code point itself.

	@warning U+000D never reaches here - it is the paragraph boundary, written as </p><p> - and
	  U+0009 TAB is written as itself, because it is a character anybody can type. Everything else
	  invisible, the forced line break U+000A included, becomes a span. */
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
	  fInCell then say which cell, counted the way the <td>s of that row run - the same count
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
	int32				fInCell;	// and which cell of that row, in the order its <td>s run
	bool16				fSplitsPara;
	std::vector<Row>	fRows;

	Table() : fOrdinal(0), fParaIndex(0), fOffset(0), fInTable(-1), fInRow(0), fInCell(0),
			  fSplitsPara(kFalse) {}
};

/** A whole story: its body, the tables standing in it, and its footnotes' own paragraphs.

	★**A FOOTNOTE IS AN <li> OF THE <ol> THAT FOLLOWS THE BODY**, holding its own paragraphs -
	  which carry nothing of their own, so adding one is copying a <p> and a copied <p> cannot end
	  up in the wrong note. The ORDER of the items is the pairing: the first <li> is note 1.
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

	⚠**A CELL'S AND A NOTE'S MARKS COUNT.** A sheet built from the body alone leaves a custom mark
	 inside a table drawing nothing: the <em> is there, its class is there, and the page shows
	 nothing at all. */
void CollectKentenValues(const Story& s, std::vector<std::string>& inOutSeen);

/** The stylesheet: the layout, the faces for the invisible characters, EVERY built-in kenten kind,
	one rule for each custom mark among `kentenValues`, and the vertical setting.

	★**THE BUILT-IN KINDS ARE ALL THERE, USED OR NOT.** The reader edits these files by hand, and
	  somebody who types <em class="kenten-BlackCircle"> into one has to see a circle on reload.
	★**THE TEXT IS 1.5 TIMES THE BROWSER'S OWN SIZE** (the user's request, 2026-09-15). The measure
	  stays in em, so a line still holds the same 40 characters; only the characters grow.
	⚠**THE ORDER OF `kentenValues` DOES NOT REACH THE BYTES.** Two exports of one document have to
	 produce the same file, or a folder diffs against itself. */
void WriteStylesheet(const std::vector<std::string>& kentenValues, std::string& outCss);

/** Story -> a complete HTML document. uid goes into <title>. Never fails.

	★**THE LOOK IS IN THE FILE** (2026-09-16, the user's decision, going back on the folder-wide
	  stylesheet of the day before): each document carries its own <style>, built from the marks
	  that document actually uses. A file mailed on its own, or pasted into a chat, still shows its
	  kenten and its invisible characters - and looking at it in a browser is how the reader checks
	  what they have edited.
	@warning ***NO BYTE ORDER MARK IS PRODUCED HERE.*** The design asks the FILE to carry one, and
	  the file is written by KCMStoryTextExport, which puts it on. A BOM in this string would sit in
	  front of the doctype, where a document is supposed to start. Read skips one if it finds it. */
void Write(const Story& s, int32 uid, std::string& out);

/** kTrue when the two stories are the same in every way this format carries.

	★★★**THIS IS WHAT LETS A WRITE CHECK ITSELF** (2026-09-16). The exporter writes a story, reads
	  the bytes straight back, and compares - all of it in memory, with no SDK type and no document
	  anywhere near it - so a story this format cannot carry is refused BEFORE its file is written,
	  rather than after somebody has spent a day editing it.
	★**AND IT IS THE HARNESS'S CHECK TOO**: Read(Write(x)) == x is the property the whole file
	  exists to keep, and this is that sentence as a function.
	@param outWhy where the first difference is, in words a status line can show.
	@param withNoteRefs kTrue compares Para::fNoteRefs as well. ⚠**kFalse FOR EVERYTHING HTML**: that
	  format does not carry a reference's place, so a story read back from it has none, and asking
	  would refuse every story with a footnote in it. KCMStoryDocx's own check passes kTrue. */
bool16 Same(const Story& a, const Story& b, std::string& outWhy, bool16 withNoteRefs = kFalse);

/** HTML -> Story. kFalse with a reason when the markup cannot be read at all.

	*** THE READER IS DELIBERATELY TOLERANT IN ONE DIRECTION ONLY. *** Anything it does not
	recognise as one of this format's own tags is TEXT - "1 < 2" and "a/b" need no escaping and get
	none. What it will not do is guess: an element it knows the name of but cannot yet build stops
	the read with a reason, rather than being skipped as though the reader had written nothing.

	@param html may or may not start with a BOM, and may use CRLF or LF - a file that has been
	  round-tripped through a chat window has lost both, and must still read. */
bool16 Read(const char* html, size_t size, Story& out, std::string& whyNot);

}	// namespace KCMStoryHtml

#endif // __KCMStoryHtml_h__

// End, KCMStoryHtml.h.
