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
//  and fNoteAt carry those places instead.
//
//  *** WHAT IS IMPLEMENTED SO FAR (2026-09-15): BODY PARAGRAPHS ONLY. *** Ruby, the invisible
//  characters, notes and tables arrive in Tasks 2 to 5 of the plan, each with its failing test
//  first. Until then Read REFUSES, with a reason, any element it does not yet understand - it does
//  not skip it. Skipping would drop the reader's words without telling anybody, which is the one
//  failure this whole file exists to prevent.
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

/** One table cell. Its text is paragraphs, exactly as the body's is. */
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

/** One table, and where it stands in the story.

	@warning fSplitsPara is the case a table can stand in the MIDDLE of a paragraph - measured, and
	  the rest of the diff assumes it away (KCMTextRead::TakeAttrFor says where). */
struct Table
{
	int32				fOrdinal;
	int32				fParaIndex;
	int32				fOffset;
	bool16				fSplitsPara;
	std::vector<Row>	fRows;

	Table() : fOrdinal(0), fParaIndex(0), fOffset(0), fSplitsPara(kFalse) {}
};

/** A whole story: its body, the tables standing in it, and its footnotes' own paragraphs.

	★**A FOOTNOTE'S PARAGRAPHS ARE WRITTEN LIKE ANY OTHERS**, after the body and its tables,
	  each one <p class="note1">...</p> - kNoteClassPrefix plus the note's number. The class is
	  the only thing that says which note a paragraph belongs to, so it is what Read looks at.
	@warning an ENDNOTE's words are not here. They live in another story (kEndnoteStoryBoss) and
	  arrive as a story of their own, with its own file. */
struct Story
{
	std::vector<Para>					fBody;
	std::vector<Table>					fTables;	// in document order
	std::vector< std::vector<Para> >	fNotes;		// [n] = footnote n's paragraphs
};

/** The file a folder of exported stories keeps its look in, and the name every one of those files
	links to.

	★**ONE NAME, IN ONE PLACE.** The writer puts it in the <link> and the exporter writes the file
	  under it. A folder where those two disagreed would open with no styling and no error at all -
	  the kenten would have no marks and the invisible characters no faces, and nothing would say
	  why. */
extern const char* const kStylesheetName;

/** The class a footnote's paragraph wears, WITHOUT the number: "note1", "note2".

	★**ONE NAME, IN ONE PLACE**, like kStylesheetName above: the writer puts it on and the reader
	  takes it off. The number is the note's own, counting from 1, so it reads the way the page
	  prints it - and a reader editing these files by hand can move a paragraph between notes by
	  changing one digit. */
extern const char* const kNoteClassPrefix;

/** Add to `inOutSeen` every kenten value this story uses - body, cells and notes alike - skipping
	any already there.

	★★**THE SHEET BELONGS TO THE FOLDER, SO THE COLLECTING DOES TOO.** A custom mark is a character
	  out of the document, which no fixed list can hold, so the stories themselves have to be asked.
	  The exporter asks each story as it writes it and hands the whole answer to WriteStylesheet
	  once, at the end.
	⚠**A CELL'S AND A NOTE'S MARKS COUNT.** A sheet built from the body alone leaves a custom mark
	 inside a table drawing nothing: the <em> is there, its class is there, and the page shows
	 nothing at all. */
void CollectKentenValues(const Story& s, std::vector<std::string>& inOutSeen);

/** The stylesheet itself: the layout, the faces for the invisible characters, EVERY built-in
	kenten kind, and one rule for each custom mark among `kentenValues`.

	★**THE BUILT-IN KINDS ARE ALL THERE, USED OR NOT.** One sheet serves the whole folder, and the
	  reader edits these files by hand - somebody who types <em class="kenten-BlackCircle"> into one
	  of them has to see a circle when the page reloads.
	★**THE TEXT IS 1.5 TIMES THE BROWSER'S OWN SIZE** (the user's request, 2026-09-15). The measure
	  stays in em, so a line still holds the same 40 characters; only the characters grow.
	⚠**THE ORDER OF `kentenValues` DOES NOT REACH THE BYTES.** Two exports of one document have to
	 produce the same file, or a folder diffs against itself. */
void WriteStylesheet(const std::vector<std::string>& kentenValues, std::string& outCss);

/** Story -> a complete HTML document. uid goes into <title>. Never fails.

	★**THE LOOK IS NOT IN HERE ANY MORE** (2026-09-15): the document links kStylesheetName instead
	  of carrying a <style> of its own, so the reader changes one file rather than thirty.
	⚠A file taken OUT of its folder still imports perfectly - nothing on the reading side looks at
	 the stylesheet - but a browser then shows each kenten as the plain italic an <em> means to it,
	 and each invisible character as the nothing an empty <span> means to it.
	@warning ***NO BYTE ORDER MARK IS PRODUCED HERE.*** The design asks the FILE to carry one, and
	  the file is written by KCMStoryTextExport, which puts it on. A BOM in this string would sit in
	  front of the doctype, where a document is supposed to start. Read skips one if it finds it. */
void Write(const Story& s, int32 uid, std::string& out);

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
