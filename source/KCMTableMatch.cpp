//========================================================================================
//
//  KCMTableMatch.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line.
#include "VCPlugInHeaders.h"

#include "KCMTableMatch.h"

#include <sstream>
#include <string>
#include <vector>

#include "ICommand.h"
#include "IDataBase.h"
#include "ITableCommands.h"			// rows, columns, merges, header and footer rows - the moves the import makes too
#include "ITableCopyPasteCmdData.h"	// the Source's cells into the Target's (kTableCopyPasteCmdBoss)
#include "ITableModel.h"
#include "ITextModel.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "TablesID.h"					// kTableCopyPasteCmdBoss
#include "TableTypes.h"

#include "KCMRestoreAttr.h"			// KCMAttrMarksSame - the four marks read on both sides
#include "KCMStoryKinds.h"			// the four attribute kinds
#include "KCMTableShape.h"			// KCMReadTableShapes / KCMTableShapesDiffer / KCMTableCellAt / KCMTableCellSame
#include "KCMTextWords.h"			// WordsAt

namespace
{

/** The shape of table `table` in `story`, and every shape of the story with it (for the tables nested inside). */
bool16 ShapeOf(const UIDRef& story, UID table, KCMTableShape& out, std::vector<KCMTableShape>& outAll)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil || !KCMReadTableShapes(model, outAll))
		return kFalse;
	for (size_t i = 0; i < outAll.size(); ++i)
		if (outAll[i].fDictUID == table)
		{
			out = outAll[i];
			return kTrue;
		}
	return kFalse;
}

/** How many tables of `all` stand in [from, to) - the tables nested in one cell's thread. */
int32 TablesWithin(const std::vector<KCMTableShape>& all, TextIndex from, TextIndex to)
{
	int32 n = 0;
	for (size_t i = 0; i < all.size(); ++i)
		if (all[i].fAnchorStart >= from && all[i].fAnchorStart < to)
			++n;
	return n;
}

/** Whether `s` has a merged cell anchored at (row, col) reaching rowSpan x colSpan. */
bool16 HasMerge(const KCMTableShape& s, int32 row, int32 col, int32 rowSpan, int32 colSpan)
{
	for (size_t i = 0; i < s.fMerges.size(); ++i)
		if (s.fMerges[i].fRow == row && s.fMerges[i].fCol == col
			&& s.fMerges[i].fRowSpan == rowSpan && s.fMerges[i].fColSpan == colSpan)
			return kTrue;
	return kFalse;
}

const KCMTableMatchKept* KeptAt(const std::vector<KCMTableMatchKept>& kept, int32 row, int32 col)
{
	for (size_t i = 0; i < kept.size(); ++i)
		if (kept[i].fRow == row && kept[i].fCol == col)
			return &kept[i];
	return nil;
}

void Refuse(PMString& why, const std::string& text)
{
	why.SetUTF8String(text);
	why.SetTranslatable(kFalse);
}

std::string Num(int32 n)
{
	std::ostringstream o;
	o << n;
	return o.str();
}

std::string CellName(int32 row, int32 col)
{
	return "cell (row " + Num(row) + ", column " + Num(col) + ")";
}

/** One move: refused with its name when the command did not succeed. ★The error state a failed command leaves
	standing is cleared here (CmdUtils.h:74 - processing another command with it standing is a protective shutdown);
	the caller decides what to do with the sequence. */
bool16 Made(ErrorCode err, const std::string& what, PMString& why)
{
	if (err == kSuccess)
		return kTrue;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	Refuse(why, what);
	return kFalse;
}

}	// anonymous namespace

/* KCMMatchTableToSource
*/
int32 KCMMatchTableToSource(const UIDRef& targetStory, const UIDRef& sourceStory, UID targetTable, UID sourceTable,
							std::vector<KCMTableMatchKept>& outKept, PMString& outWhy)
{
	outKept.clear();
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);

	KCMTableShape t, s;
	std::vector<KCMTableShape> tAll, sAll;
	if (!ShapeOf(targetStory, targetTable, t, tAll))
	{
		Refuse(outWhy, "the Target's table could not be read");
		return -1;
	}
	if (!ShapeOf(sourceStory, sourceTable, s, sAll))
	{
		Refuse(outWhy, "the Source's table could not be read");
		return -1;
	}

	InterfacePtr<ITableModel> table(UIDRef(targetStory.GetDataBase(), targetTable), UseDefaultIID());
	// ★THE OFFICIAL SHAPE: ITableCommands is queried from the table model (tablebasics/TblBscSuiteTextCSB.cpp,
	//   codesnippets/SnpSortTable.cpp; KCMApplyTableShape does the same)
	InterfacePtr<ITableCommands> cmds(table, UseDefaultIID());
	if (table == nil || cmds == nil)
	{
		Refuse(outWhy, "the Target's table cannot be edited");
		return -1;
	}

	// ---- 0. the cells left alone, and their words as they stand ---------------------------------------------
	//
	// ★BEFORE ANYTHING MOVES: the same cell on both sides (KCMTableCellSame) keeps its words, whatever they are -
	//   a Cell Text row of its own says what differs there, and the change history takes it back. Their words
	//   are noted now so that KCMTableReadsAsSource can say they were not touched.
	{
		InterfacePtr<ITextModel> tModel(targetStory, UseDefaultIID());
		if (tModel == nil)
		{
			Refuse(outWhy, "the Target's story could not be opened");
			return -1;
		}
		for (size_t i = 0; i < t.fCells.size(); ++i)
		{
			const KCMTableCellPlace& c = t.fCells[i];
			if (!KCMTableCellSame(t, s, c.fRow, c.fCol))
				continue;
			KCMTableMatchKept k;
			k.fRow = c.fRow;
			k.fCol = c.fCol;
			if (!KCMTextWords::WordsAt(tModel, c.fStart, c.fEnd - c.fStart, k.fWords))
			{
				Refuse(outWhy, CellName(c.fRow, c.fCol) + " of the Target's table could not be read");
				return -1;
			}
			outKept.push_back(k);
		}
	}
	int32 moves = 0;

	// ---- 1. the Target's merged cells the Source does not have, taken apart ------------------------------------
	// ★A merge the Source has too, at the same place and size, stays: taking it apart and merging it again would
	//   join its paragraphs afresh, and a cell left alone has to come out with the words it had.
	for (size_t i = 0; i < t.fMerges.size(); ++i)
	{
		const KCMTableCellShape& m = t.fMerges[i];
		if (HasMerge(s, m.fRow, m.fCol, m.fRowSpan, m.fColSpan))
			continue;
		if (!Made(cmds->UnmergeCell(GridAddress(m.fRow, m.fCol)),
				  "a merged cell of the Target's table could not be taken apart", outWhy))
			return -1;
		++moves;
	}

	// ---- 2. rows and columns as many as the Source's, at the end (the import's rule: put in after the last, taken
	//         away from the bottom / the right - design sections 8-2 and 9-1) --------------------------------------
	{
		const RowRange rows = table->GetTotalRows();
		if (s.fRows > rows.count)
		{
			if (!Made(cmds->InsertRows(RowRange(rows.start + rows.count - 1, s.fRows - rows.count), Tables::eAfter, 0.0),
					  "rows could not be put into the Target's table", outWhy))
				return -1;
			++moves;
		}
		else if (s.fRows < rows.count)
		{
			if (!Made(cmds->DeleteRows(RowRange(rows.start + s.fRows, rows.count - s.fRows)),
					  "rows could not be taken out of the Target's table", outWhy))
				return -1;
			++moves;
		}
		const ColRange cols = table->GetTotalCols();
		if (s.fCols > cols.count)
		{
			if (!Made(cmds->InsertColumns(ColRange(cols.start + cols.count - 1, s.fCols - cols.count), Tables::eAfter, 0.0),
					  "columns could not be put into the Target's table", outWhy))
				return -1;
			++moves;
		}
		else if (s.fCols < cols.count)
		{
			if (!Made(cmds->DeleteColumns(ColRange(cols.start + s.fCols, cols.count - s.fCols)),
					  "columns could not be taken out of the Target's table", outWhy))
				return -1;
			++moves;
		}
	}

	// ---- 3. header and footer rows as the Source has them -----------------------------------------------------
	//
	// ★Asked of the table AGAIN after the rows moved: a row put in after the last lands in the footer when the
	//   table has one (measured in the spike, M1a), and rows taken from the bottom take footer rows with them.
	{
		const RowRange header = table->GetHeaderRows();
		if (header.count > s.fHeaderCount)
		{
			if (!Made(cmds->ConvertToBodyRows(RowRange(header.start + s.fHeaderCount, header.count - s.fHeaderCount)),
					  "header rows of the Target's table could not be made body rows", outWhy))
				return -1;
			++moves;
		}
		else if (header.count < s.fHeaderCount)
		{
			if (!Made(cmds->ConvertToHeaderRows(RowRange(header.start + header.count, s.fHeaderCount - header.count)),
					  "rows of the Target's table could not be made header rows", outWhy))
				return -1;
			++moves;
		}
		const RowRange total = table->GetTotalRows();
		const RowRange footer = table->GetFooterRows();
		if (footer.count > s.fFooterCount)
		{
			if (!Made(cmds->ConvertToBodyRows(RowRange(footer.start, footer.count - s.fFooterCount)),
					  "footer rows of the Target's table could not be made body rows", outWhy))
				return -1;
			++moves;
		}
		else if (footer.count < s.fFooterCount)
		{
			if (!Made(cmds->ConvertToFooterRows(RowRange(total.start + total.count - s.fFooterCount, s.fFooterCount - footer.count)),
					  "rows of the Target's table could not be made footer rows", outWhy))
				return -1;
			++moves;
		}
	}

	// ---- 4. the Source's merged cells the Target does not have yet --------------------------------------------
	// ★GridArea's bottom and right are PAST the last row and column (TableTypes.h: Height() is bottomRow - topRow),
	//   the way KCMApplyTableShape and KCMReportTable merge.
	for (size_t i = 0; i < s.fMerges.size(); ++i)
	{
		const KCMTableCellShape& m = s.fMerges[i];
		if (HasMerge(t, m.fRow, m.fCol, m.fRowSpan, m.fColSpan))
			continue;
		if (!Made(cmds->MergeCells(GridArea(m.fRow, m.fCol, m.fRow + m.fRowSpan, m.fCol + m.fColSpan)),
				  "cells of the Target's table could not be merged as the Source's are", outWhy))
			return -1;
		++moves;
	}

	// ---- 5. the content and cell attributes of every cell the shape changed, from the Source -------------------
	//
	// ★THE OFFICIAL COPY OF CELLS (codesnippets/SnpCopyPasteTable.cpp), one cell at a time: the Source cell's area
	//   (its merge's span) onto the same address of the Target, cell attributes and content (ITableModel::eCells -
	//   the table's, rows' and columns' own attributes stay the Target's). The two grids agree by now, so the
	//   paste's precondition (the destination holds the pasted span) is met.
	// ⚠The snippet copies inside one document; that this command takes the Source DOCUMENT's table is what the
	//  first run on the application measures. A refusal here rolls the shape back with it.
	{
		InterfacePtr<ITableModel> from(UIDRef(sourceStory.GetDataBase(), sourceTable), UseDefaultIID());
		if (from == nil)
		{
			Refuse(outWhy, "the Source's table could not be opened");
			return -1;
		}
		for (size_t i = 0; i < s.fCells.size(); ++i)
		{
			const KCMTableCellPlace& c = s.fCells[i];
			if (KeptAt(outKept, c.fRow, c.fCol) != nil)
				continue;					// the same cell on both sides: its words are the change history's
			int32 rowSpan = 1, colSpan = 1;
			KCMTableCellSpan(s, c.fRow, c.fCol, rowSpan, colSpan);
			InterfacePtr<ICommand> copy(CmdUtils::CreateCommand(kTableCopyPasteCmdBoss));
			InterfacePtr<ITableCopyPasteCmdData> data(copy, UseDefaultIID());
			if (copy == nil || data == nil)
			{
				Refuse(outWhy, "the copy command for the cells could not be made");
				return -1;
			}
			// ★(2026-09-25 spike) eAll, not eCells: a row's height and a column's width are ROW and COLUMN attributes
			//   (kRowAttrHeightBoss / kColAttrWidthBoss - ITableGeometry.h), which eCells does not carry. Measured with
			//   eCells: a row or column put back took its NEIGHBOUR's height or width (live-rows P / Q), everything
			//   else - fills, strokes, insets, cell styles - came back.
			data->Set(::GetUIDRef(from), GridArea(c.fRow, c.fCol, c.fRow + rowSpan, c.fCol + colSpan),
					  ::GetUIDRef(table), GridAddress(c.fRow, c.fCol), ITableModel::eAll);
			if (!Made(CmdUtils::ProcessCommand(copy),
					  CellName(c.fRow, c.fCol) + " could not be copied from the Source's table", outWhy))
				return -1;
			++moves;
		}
	}
	return moves;
}

/* KCMTableReadsAsSource
*/
bool16 KCMTableReadsAsSource(const UIDRef& targetStory, const UIDRef& sourceStory, UID targetTable, UID sourceTable,
							 const std::vector<KCMTableMatchKept>& kept, std::string& outWhy)
{
	outWhy.clear();
	// Reading must not dirty either document (the reading of a table's cells composes; KCMStoryDiffRun does the same).
	IDataBase::SaveRestoreModifiedState targetGuard(targetStory.GetDataBase());
	IDataBase::SaveRestoreModifiedState sourceGuard(sourceStory.GetDataBase());

	KCMTableShape t, s;
	std::vector<KCMTableShape> tAll, sAll;
	if (!ShapeOf(targetStory, targetTable, t, tAll))
	{
		outWhy = "the Target's table could not be read back";
		return kFalse;
	}
	if (!ShapeOf(sourceStory, sourceTable, s, sAll))
	{
		outWhy = "the Source's table could not be read";
		return kFalse;
	}
	if (KCMTableShapesDiffer(t, s))
	{
		outWhy = "the rows, columns or merged cells differ";
		return kFalse;
	}
	if (t.fHeaderCount != s.fHeaderCount || t.fFooterCount != s.fFooterCount)
	{
		outWhy = "the header or footer rows differ";
		return kFalse;
	}

	InterfacePtr<ITextModel> tModel(targetStory, UseDefaultIID());
	InterfacePtr<ITextModel> sModel(sourceStory, UseDefaultIID());
	if (tModel == nil || sModel == nil)
	{
		outWhy = "a story could not be opened";
		return kFalse;
	}
	static const int32 kKinds[] = { kKCMStoryAttrRuby, kKCMStoryAttrKenten, kKCMStoryAttrWarichu, kKCMStoryAttrTcy };
	static const char* const kKindNames[] = { "ruby", "kenten", "warichu", "tate-chu-yoko" };

	for (size_t i = 0; i < s.fCells.size(); ++i)
	{
		const KCMTableCellPlace& sc = s.fCells[i];
		const KCMTableCellPlace* const tc = KCMTableCellAt(t, sc.fRow, sc.fCol);
		if (tc == nil)
		{
			outWhy = CellName(sc.fRow, sc.fCol) + " is not in the Target's table";
			return kFalse;
		}
		const int32 tLen = tc->fEnd - tc->fStart;
		WideString tWords;
		if (!KCMTextWords::WordsAt(tModel, tc->fStart, tLen, tWords))
		{
			outWhy = CellName(sc.fRow, sc.fCol) + " could not be read back";
			return kFalse;
		}

		// A cell left alone: the words it had, and nothing else asked of it.
		const KCMTableMatchKept* const keep = KeptAt(kept, sc.fRow, sc.fCol);
		if (keep != nil)
		{
			if (tWords != keep->fWords)
			{
				outWhy = CellName(sc.fRow, sc.fCol) + " was left alone but its words moved";
				return kFalse;
			}
			continue;
		}

		// A cell the shape changed: the Source's.
		const int32 sLen = sc.fEnd - sc.fStart;
		if (sLen != tLen)
		{
			outWhy = CellName(sc.fRow, sc.fCol) + ": " + Num(tLen) + " character(s) against the Source's " + Num(sLen);
			return kFalse;
		}
		WideString sWords;
		if (!KCMTextWords::WordsAt(sModel, sc.fStart, sLen, sWords))
		{
			outWhy = CellName(sc.fRow, sc.fCol) + " of the Source could not be read";
			return kFalse;
		}
		if (tWords != sWords)
		{
			outWhy = CellName(sc.fRow, sc.fCol) + ": the words differ from the Source's";
			return kFalse;
		}
		for (size_t k = 0; k < sizeof(kKinds) / sizeof(kKinds[0]); ++k)
		{
			if (!KCMAttrMarksSame(targetStory, sourceStory, kKinds[k], tc->fStart, tc->fEnd, sc.fStart, sc.fEnd))
			{
				outWhy = CellName(sc.fRow, sc.fCol) + ": the " + kKindNames[k] + " differs from the Source's";
				return kFalse;
			}
		}
		if (TablesWithin(tAll, tc->fStart, tc->fEnd) != TablesWithin(sAll, sc.fStart, sc.fEnd))
		{
			outWhy = CellName(sc.fRow, sc.fCol) + ": the tables inside it differ in number from the Source's";
			return kFalse;
		}
	}
	return kTrue;
}

// End, KCMTableMatch.cpp.
