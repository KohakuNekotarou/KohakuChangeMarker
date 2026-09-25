//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  "Match the Source" on a Table row (2026-09-25 - design section 16 of
//  docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md): the Target's table is given the SOURCE
//  document's table's SHAPE - rows, columns, merged cells, header and footer rows - and the cells that shape
//  brings or changes are given the Source's content.
//
//  ★★★**THE TABLE STAYS THE SAME TABLE** (the user: "with plan A the uid would change"). The Source's table is
//   not copied over the Target's as a whole - that makes a new table with a new id, and the Story comparison
//   pairs tables by id (KCMStoryDiffRun), so the matched table would show as one table gone and one added.
//   Instead the table that is there is RESHAPED (ITableCommands, the same moves the import makes), and the
//   Source's cells are written into ITS cells (kTableCopyPasteCmdBoss - the official snippet
//   SnpCopyPasteTable.cpp's command, the "select the cells, copy, select the target, paste" without the
//   clipboard). Its dictionary uid never changes, measured on 2026-09-20 for every one of these moves.
//
//  ★★★**ONLY THE CELLS THE SHAPE CHANGED** (the user, 2026-09-25: "when it goes back, only the parts whose cell
//   structure changed should have their content come back"). A cell that is THE SAME CELL on both sides - the
//   same address, the same merge (KCMTableCellSame) - is not touched: whatever its words are, they are the change
//   history's business (a Cell Text row of its own, "Reject This Import Change"). A cell the Source has and the
//   Target has not (a row or column put back), or one merged differently, is the shape's, and is made the
//   Source's whole. The Table row folds exactly those cells (KCMStoryDiffRun::FoldTableChanges uses the same
//   test), so what the row shows is what this rewrites.
//
//  ★★★**READ BACK, OR ROLLED BACK** (the user's rule of 2026-09-24 night): after the writes, the table is read
//   again - the Source's shape, the Source's content in every cell the shape changed (words, the four marks,
//   the tables nested inside), and IN EVERY CELL LEFT ALONE THE WORDS IT HAD BEFORE - and when anything differs
//   the caller ends its command sequence with the error state standing, which rolls the document back to before
//   it (KCMFacades' EndSequenceOrRollBack). Nothing half-matched is left in the document.
//
//  ⚠**Table + and Table − are not this file's**: a table the import put in or took out is in the change history
//   (its anchor characters are a tracked insertion or deletion - measured 2026-09-24), and "Reject This Import
//   Change" brings it back. This acts on Table ≠ - the one kind the change history cannot undo.
//
//========================================================================================

#ifndef __KCMTableMatch_h__
#define __KCMTableMatch_h__

#include <string>
#include <vector>

#include "BaseType.h"		// bool16 / int32
#include "PMReal.h"
#include "PMString.h"
#include "UIDRef.h"
#include "WideString.h"

/** A cell the match leaves alone (KCMTableCellSame on both sides before anything moved), and the words it held
	then - what KCMTableReadsAsSource checks it still holds. */
struct KCMTableMatchKept
{
	int32		fRow;
	int32		fCol;
	WideString	fWords;
	bool16		fThreadless;	// a graphic cell (KCMTableShape::fThreadless) on both sides - no words to keep (2026-09-25)

	KCMTableMatchKept() : fRow(0), fCol(0), fThreadless(kFalse) {}
};

/** ★WHAT THE TARGET'S TABLE WAS BEFORE THE MATCH MOVED ANYTHING (2026-09-25) - what the reading back compares the
	parts the match did not touch against: the cells left alone (and their words), every row's height and column's
	width READ FROM THE TABLE MODEL (ITableGeometry), and the table as INX wrote it (IDML's own XML - everything
	else a table carries). ★Two readers for one question on purpose: the user pointed at Adobe's own fix list -
	"the first column's width is no longer lost in InCopy, snippet, ICML and IDML export" (fixed in 21.6) - so an
	export is not a proof on its own, and the geometry is asked of the model as well. */
struct KCMTableMatchBefore
{
	std::vector<KCMTableMatchKept>	fKept;
	std::vector<PMReal>				fRowHeights;	// by row, from the top
	std::vector<PMReal>				fColWidths;		// by column, from the left
	std::string						fTableXml;		// the <Table> element, KCMCutTableXmlById
};

/** Gives the Target's table `targetTable` (a dictionary uid in `targetStory`'s database) the shape of the Source's
	`sourceTable` (likewise, in `sourceStory`'s): the Target's merged cells the Source does not have taken apart,
	rows and columns made as many as the Source's (put in after the last, taken away from the bottom - the import's
	rule), header and footer rows made the Source's, the Source's merged cells merged - and then every cell that is
	NOT the same cell on both sides (`outBefore.fKept` names the ones that are, with their words as they stood) given
	the Source's content and every attribute (kTableCopyPasteCmdBoss, ITableModel::eAll, cell by cell).
	★`outBefore` is filled BEFORE anything moves; when the table cannot be written as INX then, nothing is done and
	  -1 comes back: a match that could not be checked afterwards is not made (the user's rule - "if anything is
	  wrong, nothing comes back").
	★Called INSIDE the caller's command sequence; it opens none. A step that fails stops the work with its reason in
	  `outWhy` and the error state cleared - the caller rolls the sequence back (KCMTableReadsAsSource then says no).
	@return how many moves were made (shape moves and cells copied); -1 when a move could not be made, and then
		outWhy says which. */
int32 KCMMatchTableToSource(const UIDRef& targetStory, const UIDRef& sourceStory, UID targetTable, UID sourceTable,
							KCMTableMatchBefore& outBefore, PMString& outWhy);

/** Whether the Target's table now reads as the match promised - three readers, each a question the others cannot
	answer:
	 1. the text model: the Source's rows, columns, merged cells (KCMTableShapesDiffer), header and footer row counts,
	    the kind of every cell (text or graphic); in every cell NOT left alone the Source's characters
	    (KCMTextWords::WordsAt), ruby / kenten / warichu / tate-chu-yoko (KCMAttrMarksSame) and number of tables
	    standing inside; in every cell left alone the words it held before;
	 2. the table model's geometry (ITableGeometry): the Source's height for every row and width for every column the
	    match pasted into, and the height or width from before for the rest;
	 3. INX (KCMTableXmlMatches): everything else - styles, fills, strokes, insets, notes, anchored items, index
	    entries, links, variables, conditions, XML tags - the touched parts against the Source, the rest against
	    `before.fTableXml`.
	★Read from the two documents, never from what a row remembers. Neither document is dirtied by the reading.
	kFalse with the first difference in `outWhy` (UTF-8). */
bool16 KCMTableReadsAsSource(const UIDRef& targetStory, const UIDRef& sourceStory, UID targetTable, UID sourceTable,
							 const KCMTableMatchBefore& before, std::string& outWhy);

#endif // __KCMTableMatch_h__

// End, KCMTableMatch.h.
