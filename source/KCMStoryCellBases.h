//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Where each paragraph of the snippet's model actually sits in the document.
//
//  ★★★WHY THIS EXISTS AT ALL: THE SNIPPET AND THE TEXT MODEL DISAGREE ABOUT ORDER (2026-08-23).
//  In the XML a table's cells sit between the story's own <Content>, exactly where the table
//  stands. The text model does not keep them there, and says so:
//
//      "The Text content of the Table ... consists of zero or more contiguous TextStoryThreads
//       that are ALWAYS at greater TextIndex than the Text Story Thread that the Table Model is
//       anchored in."                                            -- ITableTextContent.h:41-44
//
//  So counting straight down the model - which is what every position here used to be - puts the
//  text after a table too far along (by the cells) and the cells themselves too early (by that
//  text). ⚠LengthAgrees cannot see it: it compares TOTALS, and the totals are right either way.
//  MEASURED in InDesign on 2026-08-23, on a story of three paragraphs and one 2x2 table:
//    - a change to the paragraph AFTER the table selected a character inside a cell;
//    - a change inside a cell selected the story's last character;
//    - a change in a third cell fell outside the story and selected nothing at all.
//
//  ⇒ The body is still counted (it is the one part the two agree on), and every CELL is asked of
//  the document. The road is the one Adobe calls recommended in SnpIterTableUseDictHier:
//      ITextStoryThreadDictHier -> ITextStoryThreadDict -> GetGridID(GridAddress) -> QueryThread
//      -> ITextStoryThread::GetTextStart
//
//  ★AND THE ANSWER IS CHECKED BEFORE IT IS USED, twice - see the two "refuse" points in the .cpp.
//  A wrong position looks exactly like a right one afterwards, so there is no symptom to catch
//  later; the story is refused instead, the same way LengthAgrees refuses one.
//
//========================================================================================

#ifndef __KCMStoryCellBases_h__
#define __KCMStoryCellBases_h__

#include "KCMSnippetText.h"	// KCMParaAttrs

#include <string>
#include <vector>

class UIDRef;

/** Work out where every paragraph of the model begins, as a TextIndex into the story.

	@param storyRef the story the paragraphs were read from.
	@param paragraphs every paragraph, as ExtractParagraphs returned them.
	@param attrs the matching attributes - fTableOrdinal/fCellRow/fCellCol say which are cells.
	@param outStarts one position per paragraph. ⚠Only meaningful when this returns kTrue.
	@return kFalse when the model and the document could not be matched up, in which case the
	        caller must refuse the story rather than aim anything with these positions. Reasons:
	        a different number of tables on the two sides, a table standing somewhere the model did
	        not expect, a table the model never placed at all, or a cell whose length disagrees.
	        ★A NESTED TABLE IS NO LONGER ONE OF THEM (2026-08-23): a table charges the thread it
	        stands in, and a cell is a thread, so the same reading does for both. What it costs is
	        one thing - the tables have to be put in the order the DOCUMENT keeps them, which is not
	        the order of their anchors once one of them is inside another. See EarlierBlock.
*/
bool16 KCMResolveParagraphPositions(const UIDRef& storyRef,
									  const std::vector<std::string>& paragraphs,
									  const std::vector<KCMParaAttrs>& attrs,
									  std::vector<int32>& outStarts);

#endif	// __KCMStoryCellBases_h__
