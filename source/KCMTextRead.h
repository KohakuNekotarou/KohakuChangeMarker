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
//
//  ★THIS FILE IS THE ONLY PART OF THE STORY DIFF THAT TOUCHES THE SDK FOR ITS TEXT, deliberately:
//  the judgement stays in KCMTextDiff and KCMStoryDiffRun. Keep this one a reader - if a decision
//  starts to form here, it belongs on the other side of that line.
//
//  @warning READING MUST NOT DIRTY EITHER DOCUMENT. The caller already holds
//   IDataBase::SaveRestoreModifiedState for both sides (KCMStoryDiffRun's targetDirtyGuard and
//   sourceDirtyGuard), and everything here runs inside them.
//
//  ⚠WHAT THIS FILE READS TODAY: the body only. Table cells (Task 2) and footnotes (Task 4) are
//   threads of the same model living past the body; until they are walked, a story containing
//   either will disagree with the old route - which is exactly what the parallel run below is
//   for. Plan: docs/superpowers/plans/2026-08-31-kcm-story-direct-read.md
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
		   the same shape KCMSnippetText::ExtractParagraphs produces, so the diff downstream
		   cannot tell which route filled it.
	@param outAttrs OUT the same length as outParas. **Today it holds nothing but the defaults**;
		   the ruby and the cell identity arrive in later tasks.
	@param outStarts OUT the same length again: where each paragraph begins, as a TextIndex.
		   ★NOT COUNTED - taken from the walk. That is the whole point of this file.
	@return kFalse only when the story cannot be opened at all. **An empty story is not a
		   failure**: it answers kTrue with three empty vectors.
*/
bool16 ReadStory(const UIDRef& storyRef,
				 std::vector<std::string>& outParas,
				 std::vector<KCMParaAttrs>& outAttrs,
				 std::vector<int32>& outStarts);

}	// namespace KCMTextRead

//----------------------------------------------------------------------------------------
//  THE PARALLEL RUN - **temporary, and it goes when the migration lands.**
//
//  ★★★WHY IT IS HERE AT ALL: SO THAT "THE NEW ROUTE IS RIGHT" IS A MEASUREMENT AND NOT A CLAIM.
//  With it on, every comparison runs BOTH readers over the same story and records every place
//  they disagree. When they do, which one is wrong can be settled by asking the document
//  (GetTextStart / TotalLength / a TextIterator dump) - so the migration is checked against the
//  documents themselves rather than against my reasoning about them.
//
//  ⚠OFF BY DEFAULT. It reads every story twice, and nobody should pay for that unless they are
//   measuring. app.kcmStoryReadCompare = "on" turns it on, anything else turns it off - the same
//   armed-watch convention KIDMCP uses for app.kmcpHttp.
//
//  ⚠IT MUST NOT CHANGE WHAT THE COMPARISON ANSWERS. It only observes. If it ever starts feeding
//   anything back into the result, the thing being measured is no longer the thing that ships.
//----------------------------------------------------------------------------------------

/** Is the parallel run armed? */
bool16 KCMStoryReadCompareIsOn();

/** Arms or disarms it, and clears the report either way (a stale report read after a re-arm
	would be answering about a different comparison). */
void KCMSetStoryReadCompare(bool16 on);

/** The last report, for app.kcmStoryReadCompare.

	★ASCII ONLY, and that is deliberate: it names POSITIONS and COUNTS, never the text itself.
	The words are in the document, and a report that quoted them would have to carry an encoding
	across the script boundary for no gain. */
void KCMGetStoryReadCompareReport(PMString& out);

/** Runs the new reader against what the old route produced, and records every disagreement.

	★WHY ALL THREE VALUES ARE COMPARED. The paragraphs alone would pass while the positions were
	wrong - which is exactly the fault this migration removes - and the positions alone would pass
	while a paragraph was missing. The cell identity is compared because SplitRunAtPlaces cuts
	rows by it (KCMSnippetText.h, at ParaRegion::SamePlaceAs), so a row would land in the wrong
	place without any position being wrong.

	@param which "target" or "source", for the report. ⚠ASCII, and it is printed verbatim.
*/
void KCMCompareReadRoutes(const UIDRef& storyRef,
						  const std::vector<std::string>& oldParas,
						  const std::vector<KCMParaAttrs>& oldAttrs,
						  const std::vector<int32>& oldStarts,
						  const char* which);

#endif	// __KCMTextRead_h__

// End, KCMTextRead.h.
