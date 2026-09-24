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

	KCMTableMatchKept() : fRow(0), fCol(0) {}
};

/** Gives the Target's table `targetTable` (a dictionary uid in `targetStory`'s database) the shape of the Source's
	`sourceTable` (likewise, in `sourceStory`'s): the Target's merged cells the Source does not have taken apart,
	rows and columns made as many as the Source's (put in after the last, taken away from the bottom - the import's
	rule), header and footer rows made the Source's, the Source's merged cells merged - and then every cell that is
	NOT the same cell on both sides (`outKept` names the ones that are, with their words as they stood) given the
	Source's content and cell attributes (kTableCopyPasteCmdBoss, cell by cell).
	★Called INSIDE the caller's command sequence; it opens none. A step that fails stops the work with its reason in
	  `outWhy` and the error state cleared - the caller rolls the sequence back (KCMTableReadsAsSource then says no).
	@return how many moves were made (shape moves and cells copied); -1 when a move could not be made, and then
		outWhy says which. */
int32 KCMMatchTableToSource(const UIDRef& targetStory, const UIDRef& sourceStory, UID targetTable, UID sourceTable,
							std::vector<KCMTableMatchKept>& outKept, PMString& outWhy);

/** Whether the Target's table now reads as the match promised: the Source's rows, columns, merged cells
	(KCMTableShapesDiffer), header and footer row counts; in every cell NOT in `kept` the Source's characters
	(KCMTextWords::WordsAt), ruby / kenten / warichu / tate-chu-yoko (KCMAttrMarksSame) and number of tables standing
	inside; and in every cell in `kept` the words it held before. ★Read from the two documents, never from what a
	row remembers. Neither document is dirtied by the reading. kFalse with the first difference in `outWhy` (UTF-8). */
bool16 KCMTableReadsAsSource(const UIDRef& targetStory, const UIDRef& sourceStory, UID targetTable, UID sourceTable,
							 const std::vector<KCMTableMatchKept>& kept, std::string& outWhy);

#endif // __KCMTableMatch_h__

// End, KCMTableMatch.h.
