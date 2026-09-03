//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Reading one story's paragraphs, and WHERE EACH OF THEM STANDS, straight from the text model.
//
//  ★★★WHY THIS EXISTS: THE OLD ROUTE KEEPS TWO SETS OF BOOKS. KCMSnippetText reads the text out
//  of the snippet XML, where a table's cells sit in the middle of the body - but the text model
//  keeps them PAST THE END of it (ITableTextContent.h:41-44). Every position counted from the XML
//  is therefore wrong after a table, and the TOTALS STILL ADD UP, which is why the old route has
//  to pay for ComputedLength, LengthAgrees, CellRunLength and the whole of KCMStoryCellBases -
//  and still refuses entire stories when the two sides disagree.
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
//   parser in KCMSnippetText.h and the parallel run that measured this reader against them. The
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
//  ★WHERE A POSITION INSIDE A PARAGRAPH COMES FROM, AND WHAT IT ASSUMES. A ruby span's fStart is
//   the distance from the paragraph's own start, IN THE MODEL'S COUNT, and the diff turns an
//   offset back into a position with `starts[which] + (joinedOffset - joined)`
//   (KCMSnippetText::IndexInStory), the exact inverse. The two agree only while a paragraph holds
//   nothing but its text. A table's own characters CAN stand inside a paragraph (measured
//   2026-09-01, work/kcm-selftest/midtable), and TakeRubyFor / TakeAttrFor below give back the
//   uncounted characters so that a span after one lands where it was typed.
//   ⚠**Nothing measures this assumption any more.** The parallel run was the one instrument that
//     did (it compared this reader's spans with the XML route's, which counted characters), and
//     it went with the route. If a span is ever reported one character off, this is where to look.
//
//========================================================================================

#ifndef __KCMTextRead_h__
#define __KCMTextRead_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// nil. @warning BaseType.h does NOT define it (the same note as KCMSnippetText.h)

#include <string>
#include <vector>

#include "KCMSnippetText.h"	// KCMParaAttrs - the shape the downstream already reads

class PMString;
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
		   ⚠the cells' and the footnotes' included.
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
