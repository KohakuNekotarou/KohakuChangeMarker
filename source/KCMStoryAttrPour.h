//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the ruby and the kenten of an edited file, poured into the copy
//
//  WHAT THIS IS FOR. KCMStoryTextImport pours the reader's edited WORDS into the task-start copy.
//  Until 2026-09-16 that was all it poured: the file carried ruby and kenten (KCMStoryHtml does
//  the whole round trip for both), the copy never received them, and so "I only changed the
//  ruby" produced a comparison with nothing in it at all. This file is the other half - the
//  reader's ask of 2026-09-16: "ruby only, the base and the ruby together, and kenten too".
//
//  ★★★**THE WORDS GO FIRST AND THE ATTRIBUTES FOLLOW A RE-READ.** Writing text moves every
//  position after it, so the attributes cannot be planned from the same reading of the story that
//  the text was written from. The import re-reads the story it has just poured words into and
//  calls this per paragraph - which is also what makes the guard below possible.
//
//  ★★★**A PARAGRAPH IS ONLY TOUCHED WHEN ITS WORDS ALREADY MATCH THE FILE'S.** That is not a
//  safety net bolted on afterwards; it is the thing that makes the two sides' offsets mean the
//  same. A ruby's fStart is an offset into the paragraph's text, so doc[3] and file[3] are the
//  same character exactly when the two strings are the same string. Where the words did NOT go in
//  - a paragraph the text pass refused, or a write that failed half way - the offsets disagree and
//  an attribute written from them would land on the wrong characters, silently. So it is refused,
//  with a reason, and the reader is told. (The same shape as KCMStoryRestore's rule that an
//  attribute change in a paragraph whose words changed too is refused: the words go back first.)
//
//  *** THE PLANNING IS A PURE FUNCTION AND LIVES ELSEWHERE. *** KCMParaText::PlanSpanChanges works
//  out what to take off and what to put on, and it is measured outside InDesign
//  (work/kcm-storyhtml-test). What is left here is the part that needs the SDK: turning an offset
//  in the text into a position in the document, and calling the writers.
//
//========================================================================================
#ifndef __KCMStoryAttrPour_h__
#define __KCMStoryAttrPour_h__

#include "BaseType.h"		// int32 / bool16 / TextIndex
#include "PMString.h"

#include <string>

#include "KCMStoryHtml.h"	// KCMStoryHtml::Para - and, through it, KCMParaAttrs

class ITextModel;

/** Make one paragraph's ruby, kenten, tate-chu-yoko and warichu (both 2026-09-17) match the file's.

	Nothing is written when the two already agree, which is the ordinary case: a reader who edited
	one word of a long document leaves every reading in it untouched, and a reading rewritten is a
	reading whose LOOK has been thrown away (the three attributes that ARE a reading are written,
	the twenty-seven that are its look are not).

	@param model the copy's story. ⚠**NEVER THE READER'S OWN DOCUMENT** - an import does not change
		   that (KCMStoryRestore). (⛔The door was called "Change to Imported Text" until 2026-09-20.)
	@param paraStart where this paragraph begins, as a TextIndex, from the re-read.
	@param docAttrs the paragraph's attributes as the copy now carries them - fRuby, fKenten and
		   fUncountedAt are read.
	@param docText the paragraph's text as the copy now reads. **The attributes are written only
		   when this equals file.fText**; see the file header for why that is the enabling
		   condition and not a mere precaution.
	@param file the same paragraph as the reader's file has it.
	@param whyNot filled when something was turned away or a write failed.
	@param outRefused kTrue when this paragraph's attributes could not all be written. ⚠**REFUSED
		   AND WRITTEN ARE NOT EXCLUSIVE**, the same way they are not in ApplyParagraph: a command
		   that fails in the middle leaves the writes that went in ahead of it.
	@return how many attribute writes went in.
*/
int32 KCMPourParagraphAttributes(ITextModel* model, TextIndex paraStart,
								 const KCMParaAttrs& docAttrs, const std::string& docText,
								 const KCMStoryHtml::Para& file,
								 PMString& whyNot, bool16& outRefused);

#endif // __KCMStoryAttrPour_h__

// End, KCMStoryAttrPour.h.
