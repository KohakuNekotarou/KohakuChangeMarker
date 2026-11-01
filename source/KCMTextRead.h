//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Reading one story's paragraphs, and WHERE EACH OF THEM STANDS, straight from the text model.
//
//  WHY IT READS THE TEXT MODEL AND NOT A SNIPPET'S XML. The XML keeps a table's cells in the middle of the body while
//  the text model keeps them PAST THE END of it (ITableTextContent.h:41-44), so every position counted from the XML is
//  wrong after a table and the TOTALS STILL ADD UP; and it folds a footnote's words into the body ("BBFOOTBB", a string
//  that exists nowhere in the document), which silenced a whole story with one footnote (measured:
//  work/kcm-selftest/footnote/README.md). The design is
//  docs/superpowers/specs/2026-08-31-kcm-story-direct-read-design.md.
//
//  THIS FILE IS THE ONLY PART OF THE STORY DIFF THAT TOUCHES THE SDK FOR ITS TEXT, deliberately:
//  the judgement stays in KCMTextDiff and KCMStoryDiffRun. Keep this one a reader - if a decision
//  starts to form here, it belongs on the other side of that line.
//
//  @warning READING MUST NOT DIRTY EITHER DOCUMENT. The caller already holds
//   IDataBase::SaveRestoreModifiedState for both sides (KCMStoryDiffRun's targetDirtyGuard and
//   sourceDirtyGuard), and everything here runs inside them.
//
//  WHAT THIS FILE READS: the body, every table cell (nested tables included) and every footnote, each as paragraphs
//   with their PLACE (cell / footnote identity, KCMParaAttrs), and over all of it the marks: ruby (IRubyAttrStrand),
//   kenten, warichu and tate-chu-yoko (character attributes), the note references and - when asked - the objects placed
//   in the text. The walk is over THREADS (ITextModel::QueryStoryThread), which is why the three kinds of place come
//   out of one loop.
//
//  WHERE A POSITION INSIDE A PARAGRAPH COMES FROM, AND WHICH COUNT IT IS IN. A span's fStart
//   is the distance from the paragraph's own start IN THE TEXT'S COUNT - what the paragraph SHOWS,
//   with a table's own characters left out - because that is the count the panel draws in and the
//   count KCMAttrSpan declares ("the first character of the BASE TEXT, within its paragraph").
//   TakeAttrFor below is what takes the uncounted characters out.
//   ⇒ **The crossing into the document's count is a named thing**: this reader fills KCMParaAttrs::fUncountedAt, and
//     KCMParaText::ModelOffsetInParagraph is the one function that makes it (IndexInStory and KCMStoryDiffRun's
//     AddAttrChange go through it). Adding the text's offset to the paragraph's start selected a table's ANCHOR instead
//     of the character after it (measured: work/kcm-selftest/midtable). work/kescm-snippet-test checks every offset
//     across a table outside InDesign.
//
//========================================================================================

#ifndef __KCMTextRead_h__
#define __KCMTextRead_h__

#include "BaseType.h"		// int32, bool16

#include <string>
#include <vector>

#include "KCMParaText.h"	// KCMParaAttrs - the shape the downstream already reads

class ITextModel;
class UIDRef;
struct KCMTableShape;

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
		   one). **fUncountedAt is almost always empty and must still be filled**: it is what
		   lets a caller cross back into the document's count, and an empty one reads as "the two
		   counts agree", which is the right answer for every paragraph without a table inside it.
	@param outStarts OUT the same length again: where each paragraph begins, as a TextIndex.
		   NOT COUNTED - taken from the walk. That is the whole point of this file.
	@param withObjects kTrue = fill KCMParaAttrs::fObject - one span per object placed in the text, its
		   fingerprint for a value (KCMObjectPrint.h). ⚠ONLY the Story mode's comparison of a pair that shares UIDs asks
		   (KCMStoryDiffRun.cpp): a fingerprint is a SaveAll of every object, and no other reader needs one - so every other
		   caller leaves fObject empty by leaving this out.
	@param outTables OUT optional: the story's tables as this read found them (KCMReadTableShapes) - a table's index in the
		   list is the fTableOrdinal its cells carry.
	@return kFalse only when the story cannot be opened at all. **An empty story is not a
		   failure**: it answers kTrue with three empty vectors.

	⚠**ADDING A FIELD TO KCMParaAttrs MEANS FILLING IT HERE**: the reader is the only source, and a
	  field it does not fill reads as "never changed".
*/
bool16 ReadStory(const UIDRef& storyRef,
				 std::vector<std::string>& outParas,
				 std::vector<KCMParaAttrs>& outAttrs,
				 std::vector<int32>& outStarts,
				 bool16 withObjects = kFalse,
				 std::vector<KCMTableShape>* outTables = nil);

/** What stands over a story's words as one number: its ruby, kenten, warichu and tate-chu-yoko runs, each with its place and
	value - the walks ReadStory reads them by, and no paragraph cut. KCMStoryEdits' second test
	(KCMStoryStamp::fMarksPrint). The setting of a ruby is not in it: mono against group is not a change (KCMStoryChange::fRubyGroup).
	@return the print - the same number for the same marks; a nil model answers 0. */
uint64 MarksPrint(ITextModel* model);

}	// namespace KCMTextRead

#endif	// __KCMTextRead_h__

// End, KCMTextRead.h.
