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

	@warning fNoteAt holds the offsets where a note REFERENCE stands, in the text's own count, and
	  fNoteNum the number the page prints at each - the two are always the same length.
	  ***THE OFFSET IS WHERE THE MARKER WAS, NOT WHERE ITS SPAN IS.*** KCMParaAttrs::fFootnote
	  reports a span sitting on the character BEFORE the marker (KCMTextRead::ScanNotes takes the
	  marker out of the text, so a span standing on it would measure nothing and be dropped), which
	  means whoever fills fNoteAt from that span has to add fLen. Read fStart as the marker's place
	  and every reference comes out one character early. */
struct Para
{
	std::string			fText;
	KCMAttrSpanList		fRuby;		// as KCMParaAttrs::fRuby
	std::vector<int32>	fNoteAt;	// offsets (code points) where a note reference stands
	std::vector<int32>	fNoteNum;	// same length: the number the page prints
};

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

	@warning an ENDNOTE's words are not here. They live in another story (kEndnoteStoryBoss) and
	  arrive as a story of their own, with its own file - only the reference is in this one. */
struct Story
{
	std::vector<Para>					fBody;
	std::vector<Table>					fTables;	// in document order
	std::vector< std::vector<Para> >	fNotes;		// [n] = footnote n's paragraphs
};

/** Story -> a complete HTML document. uid goes into <title>. Never fails.

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
