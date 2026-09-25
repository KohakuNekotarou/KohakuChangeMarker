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

#include "ITableGeometry.h"			// row heights and column widths, read from the table model itself (2026-09-25)
#include "ITableAttrAccessor.h"		// GetCellStyle - a cell's style and its priority, on both sides
#include "IGridAreaData.h"			// kSetCellStyleAndPriorityCmdBoss's area
#include "IIntData.h"					// ... its priority
#include "IUIDData.h"					// ... its cell style
#include "CelStyID.h"					// kSetCellStyleAndPriorityCmdBoss
#include "UIDList.h"
#include "KCMMemXferBytes.h"			// the INX written into memory
#include "KCMTableSnippet.h"			// KCMExportStoryInx / KCMCutTableXmlById - the table as INX wrote it
#include "KCMTableXmlCheck.h"			// KCMTableXmlMatches - the table's INX against the Source's and against before
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

/** The graphic cell (no text thread - KCMTableShape::fThreadless) anchored at (row, col), or nil. */
const KCMTableCellPlace* ThreadlessAt(const KCMTableShape& s, int32 row, int32 col)
{
	for (size_t i = 0; i < s.fThreadless.size(); ++i)
		if (s.fThreadless[i].fRow == row && s.fThreadless[i].fCol == col)
			return &s.fThreadless[i];
	return nil;
}

/** ★EVERY anchor cell of the shape - those with a text thread, then those without (graphic cells). What the paste and
	the reading back walk: until 2026-09-25 they walked fCells alone, and a graphic cell of the Source was neither
	pasted nor checked (live-rows X). `outThreadless` says which entries are graphic cells. */
void AllCells(const KCMTableShape& s, std::vector<KCMTableCellPlace>& out, std::vector<bool16>& outThreadless)
{
	out = s.fCells;
	outThreadless.assign(s.fCells.size(), kFalse);
	for (size_t i = 0; i < s.fThreadless.size(); ++i)
	{
		out.push_back(s.fThreadless[i]);
		outThreadless.push_back(kTrue);
	}
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
							KCMTableMatchBefore& outBefore, PMString& outWhy)
{
	outBefore = KCMTableMatchBefore();
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
			outBefore.fKept.push_back(k);
		}
		// ★A GRAPHIC CELL THAT IS THE SAME ON BOTH SIDES is left alone too (2026-09-25): the same address, the same
		//   span, a graphic cell in both. It has no words; the reading back asks its kind.
		for (size_t i = 0; i < t.fThreadless.size(); ++i)
		{
			const KCMTableCellPlace& c = t.fThreadless[i];
			if (ThreadlessAt(s, c.fRow, c.fCol) == nil)
				continue;
			int32 tr = 1, tc = 1, sr = 1, sc = 1;
			KCMTableCellSpan(t, c.fRow, c.fCol, tr, tc);
			KCMTableCellSpan(s, c.fRow, c.fCol, sr, sc);
			if (tr != sr || tc != sc)
				continue;
			KCMTableMatchKept k;
			k.fRow = c.fRow;
			k.fCol = c.fCol;
			k.fThreadless = kTrue;
			outBefore.fKept.push_back(k);
		}
	}

	// ---- 0b. the rest of what the reading back compares the untouched parts against, BEFORE anything moves -------
	//
	// ★Every row's height and column's width from the table model (ITableGeometry), and the table as INX writes it.
	// ★★A MATCH THAT COULD NOT BE CHECKED IS NOT MADE: without the INX from before, the cells and rows left alone
	//   cannot be shown to be untouched afterwards - so nothing moves, and the reader is told why.
	{
		InterfacePtr<ITableGeometry> geometry(table, UseDefaultIID());
		if (geometry == nil)
		{
			Refuse(outWhy, "the Target's table's geometry could not be read");
			return -1;
		}
		const RowRange rows = table->GetTotalRows();
		const ColRange cols = table->GetTotalCols();
		for (int32 r = rows.start; r < rows.start + rows.count; ++r)
			outBefore.fRowHeights.push_back(geometry->GetRowHeights(r));
		for (int32 c = cols.start; c < cols.start + cols.count; ++c)
			outBefore.fColWidths.push_back(geometry->GetColWidths(c));
		KCMMemXferBytes inx;
		if (!KCMExportStoryInx(targetStory.GetDataBase(), targetStory.GetUID(), inx)
			|| !KCMCutTableXmlById(inx.GetData(), inx.GetSize(), targetStory.GetUID(), targetTable, outBefore.fTableXml)
			|| outBefore.fTableXml.empty())
		{
			Refuse(outWhy, "the Target's table could not be written as INX before the match, so the match could not be "
						   "checked afterwards - nothing was changed");
			return -1;
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

	// ---- 3b. the merges the rows and columns just put in brought with them, taken apart --------------------------
	//
	// ★★A ROW PUT IN TAKES THE STRUCTURE OF THE ROW IT FOLLOWS, MERGES INCLUDED (the import's spike M7, 2026-09-23 -
	//   which is why the import takes apart the merges on the last row BEFORE it adds rows, design 9-1 (b)). Found on
	//   2026-09-25 the hard way: a Target whose last row was merged (the Source's too) got a merged row put in after
	//   it, one cell of the Source's plain row was pasted onto that merged cell, and InDesign CRASHED inside
	//   TABLE MODEL.RPLN (live-rows T; docs/ai-notes/kcm-match-the-source-2026-09-25.md section 3d). So the shape
	//   is read AGAIN here, and every merge it has that the Source has not - brought in, or stretched by the rows and
	//   columns put in - is taken apart; step 4 then merges what the Source merges.
	KCMTableShape now;
	{
		std::vector<KCMTableShape> nowAll;
		if (!ShapeOf(targetStory, targetTable, now, nowAll))
		{
			Refuse(outWhy, "the Target's table could not be read after its rows and columns were changed");
			return -1;
		}
		for (size_t i = 0; i < now.fMerges.size(); ++i)
		{
			const KCMTableCellShape& m = now.fMerges[i];
			if (HasMerge(s, m.fRow, m.fCol, m.fRowSpan, m.fColSpan))
				continue;
			if (!Made(cmds->UnmergeCell(GridAddress(m.fRow, m.fCol)),
					  "a merged cell the new rows or columns brought could not be taken apart", outWhy))
				return -1;
			++moves;
		}
		if (!ShapeOf(targetStory, targetTable, now, nowAll))
		{
			Refuse(outWhy, "the Target's table could not be read after its merged cells were taken apart");
			return -1;
		}
	}

	// ---- 4. the Source's merged cells the Target does not have yet --------------------------------------------
	// ★GridArea's bottom and right are PAST the last row and column (TableTypes.h: Height() is bottomRow - topRow),
	//   the way KCMApplyTableShape and KCMReportTable merge.
	// ★Asked of the table AS IT STANDS NOW (3b), not of the shape read before anything moved.
	for (size_t i = 0; i < s.fMerges.size(); ++i)
	{
		const KCMTableCellShape& m = s.fMerges[i];
		if (HasMerge(now, m.fRow, m.fCol, m.fRowSpan, m.fColSpan))
			continue;
		if (!Made(cmds->MergeCells(GridArea(m.fRow, m.fCol, m.fRow + m.fRowSpan, m.fCol + m.fColSpan)),
				  "cells of the Target's table could not be merged as the Source's are", outWhy))
			return -1;
		++moves;
	}

	// ---- 4b. ★★★NOTHING IS PASTED UNTIL THE SHAPE IS THE SOURCE'S (2026-09-25, after the crash above) ------------
	//
	// The paste writes the Source's cell over the Target's cell at the same address, and the table model does not
	// defend itself when the two are not the same cell: it CRASHED. So the Target's shape is read once more and must
	// be the Source's - rows, columns, every merged cell, header and footer rows - or nothing is pasted and the match
	// is refused (the caller rolls the whole sequence back). Whatever shape a future case produces, a mismatch now
	// ends in a refusal, never in a paste onto the wrong cells.
	{
		KCMTableShape made;
		std::vector<KCMTableShape> madeAll;
		if (!ShapeOf(targetStory, targetTable, made, madeAll))
		{
			Refuse(outWhy, "the Target's table could not be read before its cells were copied");
			return -1;
		}
		if (KCMTableShapesDiffer(made, s) || made.fHeaderCount != s.fHeaderCount || made.fFooterCount != s.fFooterCount)
		{
			Refuse(outWhy, "the table's shape could not be made the Source's, so no cell was copied into it");
			return -1;
		}
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
		std::vector<KCMTableCellPlace> sourceCells;
		std::vector<bool16> sourceThreadless;
		AllCells(s, sourceCells, sourceThreadless);		// graphic cells too (2026-09-25)
		for (size_t i = 0; i < sourceCells.size(); ++i)
		{
			const KCMTableCellPlace& c = sourceCells[i];
			if (KeptAt(outBefore.fKept, c.fRow, c.fCol) != nil)
				continue;					// the same cell on both sides: its words are the change history's
			int32 rowSpan = 1, colSpan = 1;
			KCMTableCellSpan(s, c.fRow, c.fCol, rowSpan, colSpan);
			// ★★THE CELL'S KIND FIRST (2026-09-25, live-rows X): a GRAPHIC cell of the Source pasted onto the text cell a
			//   row put in brings nothing - the paste does not change what kind of cell it writes into, and the cell
			//   stayed an empty text cell. So the Target's cell is made the Source's kind (ITableCommands::ConvertCellsType,
			//   asked first with the model's CanConvertCellsType) before anything is pasted into it.
			const GridArea area(c.fRow, c.fCol, c.fRow + rowSpan, c.fCol + colSpan);
			const CellType want = from->GetCellType(GridAddress(c.fRow, c.fCol));
			if (table->GetCellType(GridAddress(c.fRow, c.fCol)) != want)
			{
				if (!table->CanConvertCellsType(area, want))
				{
					Refuse(outWhy, CellName(c.fRow, c.fCol) + " cannot be made the Source's kind of cell (text or graphic)");
					return -1;
				}
				if (!Made(cmds->ConvertCellsType(area, want, kFalse),
						  CellName(c.fRow, c.fCol) + " could not be made the Source's kind of cell (text or graphic)", outWhy))
					return -1;
				++moves;
			}
			// ★AND THE TABLE MODEL'S OWN QUESTION, cell by cell (ITableModel.h: "Determine if a memento of mementoSpan
			//   can be pasted [at] atAnchor") - the second guard after 4b, asked of the very cells about to be written.
			if (!table->CanPaste(GridAddress(c.fRow, c.fCol), GridSpan(rowSpan, colSpan), from, GridAddress(c.fRow, c.fCol)))
			{
				Refuse(outWhy, CellName(c.fRow, c.fCol) + " of the Source cannot be pasted onto the Target's table");
				return -1;
			}
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

			// ★★THE CELL STYLE'S PRIORITY, AS THE SOURCE'S (2026-09-25, the INX read-back's first finding - live-rows
			//   H P Q S: AppliedCellStylePriority 6 against the Source's 3). The paste applies the cell's style anew,
			//   and a style applied anew is given a priority "greater than any priority of all cells that are adjacent"
			//   (ITableAttrModifier::ApplyCellStyle) - the number that decides whose stroke is drawn on an edge two
			//   cells share. So the Source's priority is put back, with the style the cell now has.
			InterfacePtr<ITableAttrAccessor> sAccess(from, UseDefaultIID());
			InterfacePtr<ITableAttrAccessor> tAccess(table, UseDefaultIID());
			if (sAccess == nil || tAccess == nil)
			{
				Refuse(outWhy, CellName(c.fRow, c.fCol) + ": the cell styles could not be read");
				return -1;
			}
			int32 sPriority = 0, tPriority = 0;
			sAccess->GetCellStyle(GridAddress(c.fRow, c.fCol), &sPriority);
			const UID tStyle = tAccess->GetCellStyle(GridAddress(c.fRow, c.fCol), &tPriority);
			if (tPriority != sPriority)
			{
				InterfacePtr<ICommand> setPriority(CmdUtils::CreateCommand(kSetCellStyleAndPriorityCmdBoss));
				InterfacePtr<IIntData> priorityData(setPriority, UseDefaultIID());
				InterfacePtr<IGridAreaData> areaData(setPriority, UseDefaultIID());
				InterfacePtr<IUIDData> styleData(setPriority, UseDefaultIID());
				if (setPriority == nil || priorityData == nil || areaData == nil || styleData == nil)
				{
					Refuse(outWhy, "the command that sets a cell style's priority could not be made");
					return -1;
				}
				priorityData->Set(sPriority);
				areaData->Set(area);
				styleData->Set(targetStory.GetDataBase(), tStyle);
				setPriority->SetItemList(UIDList(::GetUIDRef(table)));
				if (!Made(CmdUtils::ProcessCommand(setPriority),
						  CellName(c.fRow, c.fCol) + ": the cell style's priority could not be made the Source's", outWhy))
					return -1;
			}
		}
	}
	return moves;
}

/* KCMTableReadsAsSource
*/
bool16 KCMTableReadsAsSource(const UIDRef& targetStory, const UIDRef& sourceStory, UID targetTable, UID sourceTable,
							 const KCMTableMatchBefore& before, std::string& outWhy)
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
	// the two tables themselves, for what the text cannot say: the kind of every cell
	InterfacePtr<ITableModel> tTable(UIDRef(targetStory.GetDataBase(), targetTable), UseDefaultIID());
	InterfacePtr<ITableModel> sTable(UIDRef(sourceStory.GetDataBase(), sourceTable), UseDefaultIID());
	if (tTable == nil || sTable == nil)
	{
		outWhy = "a table could not be opened";
		return kFalse;
	}

	std::vector<KCMTableCellPlace> sourceCells;
	std::vector<bool16> sourceThreadless;
	AllCells(s, sourceCells, sourceThreadless);		// graphic cells too (2026-09-25)
	for (size_t i = 0; i < sourceCells.size(); ++i)
	{
		const KCMTableCellPlace& sc = sourceCells[i];
		const KCMTableCellPlace* const tc = sourceThreadless[i] ? ThreadlessAt(t, sc.fRow, sc.fCol)
															   : KCMTableCellAt(t, sc.fRow, sc.fCol);
		if (tc == nil)
		{
			outWhy = CellName(sc.fRow, sc.fCol) + (sourceThreadless[i] ? " is not a graphic cell in the Target's table"
																		  : " is not in the Target's table");
			return kFalse;
		}
		// ★(2026-09-25, live-rows X) a graphic cell that came back as an empty text cell read as "the same" - its words
		//   are empty on both sides. The kind is asked of the tables themselves.
		if (tTable->GetCellType(GridAddress(sc.fRow, sc.fCol)) != sTable->GetCellType(GridAddress(sc.fRow, sc.fCol)))
		{
			outWhy = CellName(sc.fRow, sc.fCol) + " is not the Source's kind of cell (text or graphic)";
			return kFalse;
		}
		if (sourceThreadless[i])
			continue;		// a graphic cell has no words: its kind is asked above, its content by the whole-table check
		const int32 tLen = tc->fEnd - tc->fStart;
		WideString tWords;
		if (!KCMTextWords::WordsAt(tModel, tc->fStart, tLen, tWords))
		{
			outWhy = CellName(sc.fRow, sc.fCol) + " could not be read back";
			return kFalse;
		}

		// A cell left alone: the words it had, and nothing else asked of it.
		const KCMTableMatchKept* const keep = KeptAt(before.fKept, sc.fRow, sc.fCol);
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

	// ---- 2. the geometry, from the table model itself (2026-09-25) --------------------------------------------
	//
	// ★The rows and columns a cell was pasted into carry the Source's height and width (eAll writes the row's and
	//  the column's attributes with the cell's); the others keep what they had before the match. Asked of the model,
	//  not of an export: Adobe's own fix list for 21.6 names an export that lost the first column's width.
	std::vector<bool16> rowFromSource(static_cast<size_t>(s.fRows), kFalse);
	std::vector<bool16> colFromSource(static_cast<size_t>(s.fCols), kFalse);
	for (size_t i = 0; i < sourceCells.size(); ++i)
	{
		const KCMTableCellPlace& sc = sourceCells[i];
		if (KeptAt(before.fKept, sc.fRow, sc.fCol) != nil)
			continue;
		int32 rowSpan = 1, colSpan = 1;
		KCMTableCellSpan(s, sc.fRow, sc.fCol, rowSpan, colSpan);
		for (int32 r = sc.fRow; r < sc.fRow + rowSpan && r < s.fRows; ++r)
			rowFromSource[static_cast<size_t>(r)] = kTrue;
		for (int32 c = sc.fCol; c < sc.fCol + colSpan && c < s.fCols; ++c)
			colFromSource[static_cast<size_t>(c)] = kTrue;
	}
	{
		InterfacePtr<ITableGeometry> tGeometry(tTable, UseDefaultIID());
		InterfacePtr<ITableGeometry> sGeometry(sTable, UseDefaultIID());
		if (tGeometry == nil || sGeometry == nil)
		{
			outWhy = "a table's geometry could not be read";
			return kFalse;
		}
		const PMReal kSlack(0.01);		// a hundredth of a point: what a stored length can differ by and be the same
		for (int32 r = 0; r < s.fRows; ++r)
		{
			const bool16 fromSource = (rowFromSource[static_cast<size_t>(r)] || static_cast<size_t>(r) >= before.fRowHeights.size())
				? kTrue : kFalse;
			const PMReal want = fromSource ? sGeometry->GetRowHeights(r) : before.fRowHeights[static_cast<size_t>(r)];
			const PMReal have = tGeometry->GetRowHeights(r);
			if (::ToDouble(have - want) > ::ToDouble(kSlack) || ::ToDouble(want - have) > ::ToDouble(kSlack))
			{
				std::ostringstream o;
				o << "row " << r << " is " << ::ToDouble(have) << "pt high and should be " << ::ToDouble(want)
				  << (fromSource ? "pt (the Source's)" : "pt (as it was)");
				outWhy = o.str();
				return kFalse;
			}
		}
		for (int32 c = 0; c < s.fCols; ++c)
		{
			const bool16 fromSource = (colFromSource[static_cast<size_t>(c)] || static_cast<size_t>(c) >= before.fColWidths.size())
				? kTrue : kFalse;
			const PMReal want = fromSource ? sGeometry->GetColWidths(c) : before.fColWidths[static_cast<size_t>(c)];
			const PMReal have = tGeometry->GetColWidths(c);
			if (::ToDouble(have - want) > ::ToDouble(kSlack) || ::ToDouble(want - have) > ::ToDouble(kSlack))
			{
				std::ostringstream o;
				o << "column " << c << " is " << ::ToDouble(have) << "pt wide and should be " << ::ToDouble(want)
				  << (fromSource ? "pt (the Source's)" : "pt (as it was)");
				outWhy = o.str();
				return kFalse;
			}
		}
	}

	// ---- 3. everything else, as INX writes it (2026-09-25 - KCMTableXmlCheck.h) ---------------------------------
	{
		KCMMemXferBytes tInx, sInx;
		std::string after, source;
		if (!KCMExportStoryInx(targetStory.GetDataBase(), targetStory.GetUID(), tInx)
			|| !KCMCutTableXmlById(tInx.GetData(), tInx.GetSize(), targetStory.GetUID(), targetTable, after) || after.empty())
		{
			outWhy = "the Target's table could not be written as INX after the match";
			return kFalse;
		}
		if (!KCMExportStoryInx(sourceStory.GetDataBase(), sourceStory.GetUID(), sInx)
			|| !KCMCutTableXmlById(sInx.GetData(), sInx.GetSize(), sourceStory.GetUID(), sourceTable, source) || source.empty())
		{
			outWhy = "the Source's table could not be written as INX";
			return kFalse;
		}
		std::vector<std::string> keptNames;		// IDML names a cell "column:row"
		for (size_t i = 0; i < before.fKept.size(); ++i)
			keptNames.push_back(Num(before.fKept[i].fCol) + ":" + Num(before.fKept[i].fRow));
		std::string why;
		if (!KCMTableXmlMatches(after, source, before.fTableXml, keptNames, why))
		{
			outWhy = "as INX writes it, " + why;
			return kFalse;
		}
	}
	return kTrue;
}

// End, KCMTableMatch.cpp.
