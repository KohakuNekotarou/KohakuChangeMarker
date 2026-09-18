//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a story's text as a Word document (.docx)
//
//  WHAT THIS IS FOR. The same round trip KCMStoryHtml serves, for a reader whose editor is Word:
//  the stories go out as .docx, are edited there with Word's own revision tracking switched on,
//  and come back through the Import mode - which then shows ONLY what was changed in Word, because
//  the file carries the story as it stood when it was written (the "origin" part) and the import
//  merges Word's changes onto the document as it stands now. The design is
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

}	// namespace KCMStoryDocx

#endif // __KCMStoryDocx_h__

// End, KCMStoryDocx.h.
