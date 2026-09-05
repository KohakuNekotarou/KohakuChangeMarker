//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Reading one story's paragraphs, and WHERE EACH OF THEM STANDS, straight from the text model.
//
//  ★★★WHY THIS EXISTS: THE OLD ROUTE KEPT TWO SETS OF BOOKS. It read the text out of the snippet
//  XML - the parser lived in what was then called KCMSnippetText.h - where a table's cells sit in
//  the middle of the body, while the text model keeps them PAST THE END of it
//  (ITableTextContent.h:41-44). Every position counted from the XML is therefore wrong after a
//  table, and the TOTALS STILL ADD UP, which is why that route had to pay for ComputedLength,
//  LengthAgrees, CellRunLength and the whole of KCMStoryCellBases - and still refused entire
//  stories when the two sides disagreed.
//
//  ⚠MEASURED 2026-08-31: a story with ONE FOOTNOTE produces no differences at all. The parser
//   does not know <Footnote>, so it folds the note's text into the body ("BBFOOTBB", a string
//   that exists nowhere in the document), the length no longer matches, and CompareOneStory
//   refuses the story. Neither a body edit nor a footnote edit came out.
//   Control: the same edit without a footnote reported edits=1.
//   Full record: work/kcm-selftest/footnote/README.md
//
//  ⇒ Reading 0..TotalLength() is the same question asked where the answer already lives.
//  ★THE OLD ROUTE IS GONE (2026-09-03, plan Task 7): KCMStoryXml, KCMStoryCellBases, the XML
//   parser that stood in KCMParaText.h and the parallel run that measured this reader against
//   them - the header keeps only the shapes and the pure functions, which is what its name says now. The
//   footnote fault above is what the switch was measured on (edits 0 -> 1 on the fn and fnonly
//   pairs, 2026-09-01), and the design that decided it is
//   docs/superpowers/specs/2026-08-31-kcm-story-direct-read-design.md.
//
//  ★THIS FILE IS THE ONLY PART OF THE STORY DIFF THAT TOUCHES THE SDK FOR ITS TEXT, deliberately:
//  the judgement stays in KCMTextDiff and KCMStoryDiffRun. Keep this one a reader - if a decision
//  starts to form here, it belongs on the other side of that line.
//
//  @warning READING MUST NOT DIRTY EITHER DOCUMENT. The caller already holds
//   IDataBase::SaveRestoreModifiedState for both sides (KCMStoryDiffRun's targetDirtyGuard and
//   sourceDirtyGuard), and everything here runs inside them.
//
//  ★WHAT THIS FILE READS: the body, every table cell (nested tables included) and every footnote,
//   each as paragraphs with their PLACE (cell / footnote identity, KCMParaAttrs), and over all of
//   it the RUBY (IRubyAttrStrand) and the KENTEN (kTAKentenKindBoss). The walk is over THREADS
//   (ITextModel::QueryStoryThread), which is why the three kinds of place come out of one loop.
//
//  ★★★WHERE A POSITION INSIDE A PARAGRAPH COMES FROM, AND WHICH COUNT IT IS IN. A span's fStart
//   is the distance from the paragraph's own start IN THE TEXT'S COUNT - what the paragraph SHOWS,
//   with a table's own characters left out - because that is the count the panel draws in and the
//   count KCMAttrSpan declares ("the first character of the BASE TEXT, within its paragraph").
//   TakeAttrFor below is what takes the uncounted characters out.
//   ⚠**THIS PARAGRAPH SAID "IN THE MODEL'S COUNT" UNTIL 2026-09-04, and it was never true** - the
//    subtraction in TakeAttrFor has been there since the reader was written. It mattered because
//    everything downstream then had to cross back into the document's count to ask about a mark, a
//    jump or a selection, and nothing did: they added the text's offset to the paragraph's start.
//    Measured the same day - a table standing inside a paragraph made the one reported change of
//    work/kcm-selftest/midtable select the table's ANCHOR instead of the character after it.
//   ⇒ **The crossing is now a named thing**: this reader fills KCMParaAttrs::fUncountedAt, and
//     KCMParaText::ModelOffsetInParagraph is the one function that goes from the text's count
//     into the document's. IndexInStory and KCMStoryDiffRun's AddAttrChange both go through it.
//   ★It is measured again, too, and outside InDesign: work/kescm-snippet-test builds the header
//    with a paragraph carrying a table and checks every offset across it, WITH a calibration case
//    proving the naive answer would have differed. The parallel run that used to measure this went
//    with the XML route in 2026-09-03; this replaces the part of it that mattered here.
//
//========================================================================================

#ifndef __KCMTextRead_h__
#define __KCMTextRead_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// nil. @warning BaseType.h does NOT define it (the same note as KCMParaText.h)

#include <string>
#include <vector>

#include "KCMParaText.h"	// KCMParaAttrs - the shape the downstream already reads

class UIDRef;

namespace KCMTextRead
{

/** Reads one story's paragraphs straight from its text model.

	@param storyRef the story to read.
	@param outParas OUT one entry per paragraph, UTF-8, WITHOUT the trailing break character -
		   the shape the diff downstream has always read.
	@param outAttrs OUT the same length as outParas: the PLACE (fTableOrdinal / fCellRow /
		   fCellCol, or kNotACell for body text; fFootnoteOrdinal, or kNotAFootnote), one fRuby
		   span per ruby run over the paragraph and one fKenten span per stretch of one kind -
		   ⚠the cells' and the footnotes' included - and fUncountedAt, the positions the document
		   counts inside this paragraph and the text does not (a table standing in the middle of
		   one). ★**fUncountedAt is almost always empty and must still be filled**: it is what
		   lets a caller cross back into the document's count, and an empty one reads as "the two
		   counts agree", which is the right answer for every paragraph without a table inside it.
	@param outStarts OUT the same length again: where each paragraph begins, as a TextIndex.
		   ★NOT COUNTED - taken from the walk. That is the whole point of this file.
	@return kFalse only when the story cannot be opened at all. **An empty story is not a
		   failure**: it answers kTrue with three empty vectors.

	⚠**ADDING A FIELD TO KCMParaAttrs MEANS FILLING IT HERE**, and there is no longer a parallel
	  run to say when one was left empty (it did exactly that for the ruby on 2026-09-01). The
	  reader is the only source now; a field it does not fill reads as "never changed".
*/
bool16 ReadStory(const UIDRef& storyRef,
				 std::vector<std::string>& outParas,
				 std::vector<KCMParaAttrs>& outAttrs,
				 std::vector<int32>& outStarts);

}	// namespace KCMTextRead

#endif	// __KCMTextRead_h__

// End, KCMTextRead.h.
