//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a frame's part of a story: cut out for its box, and put back from it
//
//  WHAT THIS IS FOR. "Export Pages to Word..." (2026-10-25) writes each text frame of a page as a text box holding the
//  frame's part of its story (KCMPageDocx.h). Coming back, "Import Story Text..." makes the story as it stands with
//  that part replaced by what the box holds - and hands the whole story to the import the story files already go
//  through (KCMPageImport.h). This file is the arithmetic of both directions:
//    - where a TextIndex stands among the story's paragraphs (PlaceOfIndex, PlaceOfEnd);
//    - the part itself (Cut), and a fingerprint of it (Fingerprint) - what tells, coming back, that the part is still
//      what was exported;
//    - the part replaced (Splice), every mark around it moved along: ruby, kenten, tate-chu-yoko, warichu, the tables,
//      the footnote references and the endnote marks standing in the paragraphs it touches.
//  The design is docs/superpowers/specs/2026-10-25-kcm-word-layout-export-design.md.
//
//  *** A PURE FUNCTION. No SDK type, no document. *** Tested outside InDesign with work/kcm-storydocx-test/build-page.cmd.
//
//  ★**THE STORY IS THE DOCUMENT'S SHAPE** (KCMStoryFromDocument): a table stands at a place in a paragraph, footnote
//    references and endnote marks are places too. ★**A PART IS TEXT ONLY**: no table, no note reference and no endnote
//    mark stands inside one (the export leaves such a frame out; Splice refuses the rest by name).
//
//========================================================================================
#ifndef __KCMPageSplice_h__
#define __KCMPageSplice_h__

#include "BaseType.h"
#include "KCMParaText.h"	// KCMParaAttrs - what KCMTextRead says of each paragraph
#include "KCMStoryShape.h"

#include <string>
#include <vector>

namespace KCMPageSplice
{

/** A place in a story's body: paragraph fPara of Story::fBody, before code point fOffset of its text - the
	paragraph's length is its end. */
struct Place
{
	int32	fPara;
	int32	fOffset;

	Place() : fPara(0), fOffset(0) {}
};

/** The place of the gap BEFORE model index `at`.
	@param paras, attrs, starts KCMTextRead::ReadStory's three vectors, ★BODY PARAGRAPHS ONLY (neither IsCell nor
		   IsFootnote), in order - the filter KCMStoryFromDocument uses, so index p is Story::fBody[p].
	★A position the text does not count - a table's anchor, a note's marker, one a paragraph begins with - answers the
	  place of the character after it. ★A paragraph's break answers that paragraph's end.
	@return kFalse when `at` is past the body's last break (or the vectors disagree). */
bool16 PlaceOfIndex(const std::vector<std::string>& paras, const std::vector<KCMParaAttrs>& attrs,
					const std::vector<int32>& starts, int32 at, Place& out);

/** The place just past the last character of [start, end): the end of a frame's range.
	★A RANGE THAT ENDS WITH A PARAGRAPH'S BREAK ENDS AT THAT PARAGRAPH'S END - the break is not the frame's to change,
	  so the next paragraph is not drawn into its box. An empty range ends where it starts (PlaceOfIndex(start)). */
bool16 PlaceOfEnd(const std::vector<std::string>& paras, const std::vector<KCMParaAttrs>& attrs,
				  const std::vector<int32>& starts, int32 start, int32 end, Place& out);

/** The body's [from, to) as a story of its own: the paragraphs it touches, cut at both ends, the ruby, kenten,
	tate-chu-yoko and warichu clipped to it and counted from the piece's own start (a tate-chu-yoko's or warichu's
	value made its own characters again). No tables, no note references, no endnote marks, no notes. Places outside the
	story give an empty story. */
KCMStoryShape::Story Cut(const KCMStoryShape::Story& s, const Place& from, const Place& to);

/** A piece's fingerprint: its byte count and CRC-32 of one fixed spelling of every paragraph's words and every mark
	over them. ★The marks are in it on purpose - a ruby added in InDesign after the export must stop an old box from
	going in, exactly as a changed word does. */
void Fingerprint(const KCMStoryShape::Story& piece, int32& outBytes, uint32& outCrc);

/** `s` with the body's [from, to) replaced by `piece`'s body. The marks of the paragraphs at both ends are kept where
	they stand around the range; touching kenten of one kind, and touching tate-chu-yoko and warichu, become one (as the
	document holds them); a ruby never joins another.
	@return kFalse with a reason, `s` untouched: the piece holds a table, a footnote or an endnote mark ("text only in
	  this version"); a table, a footnote reference or an endnote mark stands inside the range; a ruby crosses one of its
	  edges; the places are not in the story or out of order. */
bool16 Splice(KCMStoryShape::Story& s, const Place& from, const Place& to, const KCMStoryShape::Story& piece,
			  std::string& whyNot);

}	// namespace KCMPageSplice

#endif // __KCMPageSplice_h__

// End, KCMPageSplice.h.
