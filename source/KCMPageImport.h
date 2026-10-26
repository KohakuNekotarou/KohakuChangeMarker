//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a page file's boxes, put back into their stories
//
//  WHAT THIS IS FOR. "Import Story Text..." reads a page file ("Export Pages to Word...", KCMPageExport.h) as boxes:
//  each one frame's part of one story, as Word now holds it. Once the import has its copy of the document (the Task
//  Start in memory it pours into), this makes each story whose boxes can go in a WHOLE story again - the copy's story
//  with every box's range replaced by the box's words - and adds it to the set beside the story files' stories. From
//  there the import goes on exactly as for a story file: the copy is made that story, and the Story comparison shows
//  what it would change in the document. The design is docs/superpowers/specs/2026-10-25-kcm-word-layout-export-design.md.
//
//  ★**A BOX GOES BACK WHERE ITS PART STILL IS WHAT WAS EXPORTED** - words and marks, by the fingerprint the page tag
//    carries (KCMPageSplice::Fingerprint): first at the range it was exported from, then at the range its frame holds now
//    (words added in an EARLIER frame move every range after it).
//  ★★**AND A BOX THAT IS ITS WHOLE STORY GOES BACK OVER IT EVEN WHEN INDESIGN CHANGED IT** (2026-10-26, the author's B:
//    "simply compare the document's text with Word's"): since that day the export writes only frames that are their
//    story's only one and show all of it, and marks each box so (whole="1"). Such a box is put back over the story's
//    whole body - nothing has to be found - and the comparison then shows what InDesign changed since the export as
//    proposals back to the file's words; nothing is written until Apply. Refused instead: a frame threaded since, and a
//    story that holds a table or a note now. A box of 2026-10-25 (no whole) keeps the fingerprint's rule: a "!" row.
//  ★**ANOTHER DOCUMENT'S FILE IS REFUSED BY ITS IDS** (2026-10-26 - KCMPageDocx::Page::fDocumentId): a document made from
//    the same template numbers its stories alike, so a uid alone does not tell; the name is not asked (it can change).
//  ★**WHAT CANNOT GO IN IS NAMED, BOX BY BOX** ("Page", the file and the box's number): the story's own file chosen too,
//    a box the file itself refuses, a story the document does not have, a part that changed, two boxes over one text, a
//    box holding what this version does not put back (a table, a note).
//
//========================================================================================
#ifndef __KCMPageImport_h__
#define __KCMPageImport_h__

#include "BaseType.h"

#include <vector>

#include "KCMStoryTextImport.h"		// KCMStoryTextSet, KCMPageBoxEdit, KCMImportRefusal

class IDataBase;

/** inOutSet.fBoxes, made stories of `copyDB` (the import's copy): each story whose boxes can go in is added to fUids /
	fStories / fFileNames - its shape in the split form a story file reads back in (KCMStoryDocx::SettleForThisFormat) -
	and fBoxes is emptied. Reads `copyDB`, and `documentDB` (the document the copy was taken of) for its ids only.
	@param outRefusals one "!" row per box that cannot go in (fKind "Page").
	@param outBoxesIn the boxes that went in; @param outStoriesIn the stories they made. */
void KCMSpliceBoxesIntoSet(IDataBase* copyDB, IDataBase* documentDB, KCMStoryTextSet& inOutSet,
						   std::vector<KCMImportRefusal>& outRefusals, int32& outBoxesIn, int32& outStoriesIn);

#endif // __KCMPageImport_h__

// End, KCMPageImport.h.
