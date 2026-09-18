//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a story's text as a Word document (.docx)
//
//  WHAT THIS IS FOR. The same round trip KCMStoryHtml serves, for a reader whose editor is Word:
//  the stories go out as .docx, are edited there with Word's own revision tracking switched on,
//  and come back through the Import mode - which then shows ONLY what was changed in Word, because
//  Word's revision marks say what that was, and the import merges those changes onto the document
//  as it stands now. (When the marks are not the whole truth, the whole text is compared instead:
//  Fingerprint, below, is how the two are told apart.) The design is
//  docs/superpowers/specs/2026-09-19-kcm-story-docx-roundtrip-design.md and it, not this header,
//  is where the decisions and their reasons live.
//
//  *** THE STORY IS KCMStoryHtml's STORY. *** That struct knows nothing of HTML - it is the body,
//  the tables, the notes and the spans over them - so this is a second spelling of the same thing
//  and every rule upstream and downstream of the format is shared: what is carried, what an
//  invisible character is (KCMStoryHtml::IsInvisible), what a kenten value is called.
//
//  *** EVERYTHING HERE IS A PURE FUNCTION. No SDK type, no document, no file. *** Built and run
//  outside InDesign in work/kcm-storydocx-test (build.cmd). What Word makes of the result is
//  measured there too, with Word itself (word_open.ps1) - docs/ai-notes/kcm-docx-probe-2026-09-19.md
//  is the measurement every spelling below rests on.
//
//  ⚠**THIS IS THE WRITING HALF ONLY** (stage 1 of the plan, docs/superpowers/plans/2026-09-19-kcm-
//   story-docx-stage1-write.md). Until Read exists, an export cannot check itself the way the HTML
//   one does (Write, Read, Same), so a .docx written by this build is for LOOKING AT, not for
//   handing to somebody to edit.
//
//  HOW EACH THING IS SPELT (all of it measured in Word 2007; newer Words are still to be measured):
//    a paragraph            <w:p>, its text in <w:t xml:space="preserve">
//    a forced line break    <w:br/> - Shift+Enter itself, so NOT a placeholder
//    a tab                  <w:tab/>
//    any other invisible    a LOCKED content control, <w:sdt>, whose <w:tag> is the code point
//      character            ("ufffc"). ★THE TAG IS THE TRUTH; the text inside is decoration, and Word
//                           re-cuts it into runs of its own. Word refuses, silently and completely,
//                           a deletion that reaches across one.
//    ruby                   <w:ruby>, one element per span. A span over several characters is a
//                           GROUP reading and one over a single character is MONO, so the way the
//                           elements are cut IS the setting - the rule the HTML format has.
//    kenten                 a character style NAMED for the kind, kenten-BlackTriangle. Word has four
//                           emphasis marks and InDesign has ten and custom ones: the name carries the
//                           truth and the style's <w:em> only the nearest look. Every built-in kind
//                           is in styles.xml, used or not, so a kind only InDesign has can be APPLIED
//                           in Word, from the styles list.
//    tate-chu-yoko          <w:eastAsianLayout w:vert="1">
//    warichu                <w:eastAsianLayout w:combine="1">
//    a footnote             footnotes.xml plus <w:footnoteReference> in the text. ⚠The HTML format
//                           does not carry where the reference stands; this one has to, because Word
//                           cannot hold a note without it (KCMStoryHtml::NoteRef).
//
//========================================================================================
#ifndef __KCMStoryDocx_h__
#define __KCMStoryDocx_h__

#include "BaseType.h"
#include "KCMStoryHtml.h"	// Story, Para, NoteRef - the shape both formats write
#include "KCMZipStore.h"	// Entry - a part of the package

#include <string>
#include <vector>

namespace KCMStoryDocx
{

/** The inside of one <w:p> - no <w:pPr>: runs, rubies, placeholders and note references.

	@return kFalse, with a reason, when the paragraph cannot be written in this format:
	  - a ruby standing over an invisible character (a <w:rubyBase> takes runs only, so the
	    placeholder has nowhere to go - and dropping the character is the one thing not done);
	  - a footnote reference standing inside a ruby's base text, for the same reason;
	  - a kenten value KCMStoryHtml::KentenClassOf cannot name. */
bool16 WriteParagraphContent(const KCMStoryHtml::Para& p, std::string& out, std::string& whyNot);

/** A run of paragraphs with the tables standing among them: the body, or one cell.

	★**A TABLE STANDS BETWEEN PARAGRAPHS AND BELONGS TO THE ONE BEFORE IT**, exactly as in the HTML
	  format: "AB" with a table after the A is <w:p>A</w:p>, the <w:tbl>, and then B in a paragraph
	  whose style is kcm-continued - the reader's sign to join it back on.
	⚠**UNLIKE THE HTML FORMAT, THE CONTINUED HALF IS ALWAYS WRITTEN, EMPTY OR NOT.** Two of Word's
	  own rules ask for it: a cell has to END with a paragraph (so a nested table cannot be a cell's
	  last thing), and two tables that touch are joined into one. An empty continued paragraph
	  joins back on as nothing, so it costs the round trip nothing.
	★A MERGED CELL: Story's rows hold the ANCHORS only, and Word wants a cell in every row a
	  vertical merge covers, so the covered ones are made up here (<w:vMerge/>, an empty paragraph).

	@param paras    the body's paragraphs, or one cell's.
	@param inTable  -1 for the body, else the index in s.fTables of the table that cell is in -
	                with inRow and inCell, this is how KCMStoryHtml::Table says where it stands.
	@param out      appended to - and left exactly as it was when this answers kFalse.
	@return kFalse with a reason: WriteParagraphContent's refusals, from however deep. */
bool16 WriteBlocks(const KCMStoryHtml::Story& s, const std::vector<KCMStoryHtml::Para>& paras,
				   int32 inTable, int32 inRow, int32 inCell, std::string& out, std::string& whyNot);

/** A fingerprint of a story: "<bytes>-<crc32>" of what Write makes of it.

	★★★**WHAT IT IS FOR: TO KNOW WHETHER WORD'S REVISION MARKS ARE THE WHOLE TRUTH.** The import
	  rebuilds the story as it stood when it was written - the deletions put back, the insertions
	  left out - and takes ITS fingerprint:
	    the same as the file's tag  ->  the marks account for every change made in Word, so ONLY
	                                    those changes are shown, however much the document has
	                                    been edited in InDesign since (the design, section 1-1);
	    different                   ->  tracking was off for some of the editing, or the changes
	                                    were accepted, or the file was used again for something
	                                    else. The import then compares the WHOLE text against the
	                                    document, the way the HTML import does, and says so - the
	                                    user's rule (2026-09-19): never refused, and what goes in
	                                    is theirs to choose, one change at a time, as always.
	★**IT WORKS BECAUSE Write IS DETERMINISTIC**: the same Story is the same bytes, so a story read
	  back and written again has the fingerprint it had. ⚠Whatever breaks that - a date, an id that
	  counts up, an order taken from a hash table - breaks this, silently, into "always different".
	★IT IS OF THE STORY ALONE: the uid and the document's name are beside it, not in it.
	⚠NOT A SECRET AND NOT A SIGNATURE. It tells two honest files apart; it is not there to stop
	  anybody who sets out to fool it.

	@return kFalse with a reason when the story cannot be written in this format at all. */
bool16 Fingerprint(const KCMStoryHtml::Story& s, std::string& outFingerprint, std::string& whyNot);

/** Every part of the package, in the order they are zipped.

	  [Content_Types].xml, _rels/.rels, word/document.xml, word/_rels/document.xml.rels,
	  word/styles.xml, word/settings.xml, word/footnotes.xml (ONLY when the story has notes - and
	  then nothing else speaks of footnotes either), customXml/item1.xml and its two companions.

	★★★**customXml/item1.xml IS A TAG, NOT A COPY**: the story's uid, the document's name, and a
	  FINGERPRINT of the story as written (Fingerprint, below) - and not one word of it. For half
	  a day (2026-09-19) it held the whole story a second time, hidden, as the "origin" the import
	  would compare against; the user went back on that the same day, because a file is handed on
	  and used again for other things, and text nobody can see would go with it. ★Measured: Word
	  keeps a custom XML part of a namespace of our own through a save.
	★★★**settings.xml SWITCHES REVISION TRACKING ON, AND THE IMPORT DEPENDS ON IT**: what Word
	  changed is told by Word's own <w:ins> and <w:del>. The fingerprint is what says whether they
	  are the whole truth - Fingerprint says how, and what happens when they are not.
	★**styles.xml HOLDS EVERY BUILT-IN KENTEN KIND, USED OR NOT**, plus one style for each custom
	  mark the story uses, in a fixed order: the same story is the same bytes.

	@param uid               the story's UID - the pairing, as the file name is for the HTML format.
	@param documentNameUtf8  the document's name, for the import's "is this the right document?".
	@return kFalse with a reason: WriteBlocks' refusals, a note nothing refers to (Word cannot
	  keep one), or a reference to a note the story does not have. */
bool16 WriteParts(const KCMStoryHtml::Story& s, int32 uid, const std::string& documentNameUtf8,
				  std::vector<KCMZipStore::Entry>& outParts, std::string& whyNot);

/** The same, zipped: the bytes of the .docx. */
bool16 Write(const KCMStoryHtml::Story& s, int32 uid, const std::string& documentNameUtf8,
			 std::string& outDocx, std::string& whyNot);

//========================================================================================
//  THE READING HALF (stage 2, docs/superpowers/plans/2026-09-19-kcm-story-docx-stage2-read.md)
//========================================================================================

/** What customXml/item1.xml says: which story, from which document, and the fingerprint of the
	story as written. fPresent is kFalse for a custom XML part that is somebody else's - Word keeps
	other people's parts too, and one of those is not an error. */
struct Tag
{
	bool16		fPresent;
	int32		fUid;
	int32		fFormat;
	std::string	fDocument;		// UTF-8, entities decoded
	std::string	fFingerprint;	// as written: "<bytes>-<crc32>"

	Tag() : fPresent(kFalse), fUid(0), fFormat(0) {}
};

/** One custom XML part -> Tag. ★READ AS XML, NOT MATCHED AS BYTES: Word parses this part and writes
	it out again (measured 2026-09-19 - the line break after the declaration came off), so what is
	relied on is the element, its namespace and its attributes, in whatever order and spelling.
	@return kTrue with fPresent kFalse when the part is not ours; kFalse with a reason when it IS ours
	  and cannot be read - a format this build does not know, a uid that is not a number. */
bool16 ReadTag(const std::string& customXmlPart, Tag& out, std::string& whyNot);

/** Which of the two stories a revision-marked file holds.

	★★★**ONE FILE, TWO STORIES.** With Word's revision tracking on (settings.xml switches it on and
	  protects it), what an editor changed is in the file as <w:ins> and <w:del>, and the file can be
	  read either way: as the editor left it, or as it stood when it was written. The import needs
	  both - the second one's fingerprint is what says whether the marks are the whole truth
	  (Fingerprint, above), and the difference between the two is what the import shows. */
enum Side
{
	kSideAfterWord = 0,			// as Word shows it now: <w:ins> kept, <w:del> gone, the outer <w:rPr>
	kSideOriginAsWritten = 1	// as it stood when written: <w:del> put back, <w:ins> left out, the <w:rPrChange>'s <w:rPr>
};

/** One revision mark met on the way: who, and when. Word puts both on every one (measured
	2026-09-19 - <w:ins>, <w:del>, <w:rPrChange>, a paragraph mark's and a row's alike). */
struct Mark
{
	std::string	fAuthor;
	std::string	fDate;
};

/** document.xml (+ footnotes.xml, + styles.xml; either may be empty) -> one side's Story.

	★**IT REFUSES, BY NAME, WHATEVER IT DOES NOT UNDERSTAND** - a drawing, a field that is not a
	  ruby, an automatic number, an element it has never heard of - and never skips it: skipping
	  would drop somebody's words without a word (the rule KCMStoryHtml::Read keeps). What it
	  ignores is only what carries no text: bookmarks, proofing marks, Word's own formatting-change
	  records on tables, and every kind of formatting this format does not carry.
	@param outMarks  every revision mark met, whichever side is being read; nil when not wanted. */
bool16 ReadSide(const std::string& documentXml, const std::string& footnotesXml, const std::string& stylesXml,
				Side side, KCMStoryHtml::Story& out, std::vector<Mark>* outMarks, std::string& whyNot);

/** What one package holds, once read. */
struct ReadResult
{
	KCMStoryHtml::Story	fAfter;
	KCMStoryHtml::Story	fOrigin;
	Tag					fTag;		// fPresent kFalse for a file that carries none (not written by us)
	std::vector<Mark>	fMarks;		// empty = no revision mark anywhere in the file
};

/** The parts of one package - whichever the caller could fetch; word/document.xml is the one that
	has to be there - -> both sides, the tag, the marks. */
bool16 Read(const std::vector<KCMZipStore::Entry>& parts, ReadResult& out, std::string& whyNot);

/** What this spelling cannot tell apart, made the same - so that a story can be compared with
	itself read back (Same), the way the export checks itself.

	★ONE THING ONLY: a MONO reading standing over SEVERAL characters becomes GROUP. A <w:ruby> holds
	  one reading over its base, and one reading over two characters is what Word calls a group
	  ruby; the reader answers GROUP for it, and it cannot answer anything else. The HTML format
	  keeps the distinction by putting several <rt> in one <ruby> - a shape Word has not got.
	  ⚠That this is a loss the comparison does not mind is the user's own rule (2026-09-12: "mono
	   turned into group is not a change"); KCMParaText's SpansDiffer says the same. */
void SettleForThisFormat(KCMStoryHtml::Story& s);

}	// namespace KCMStoryDocx

#endif // __KCMStoryDocx_h__

// End, KCMStoryDocx.h.
