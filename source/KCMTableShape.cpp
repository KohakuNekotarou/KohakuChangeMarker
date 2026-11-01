//========================================================================================
//
//  KCMTableShape.cpp -- the InDesign side of KCMTableShape.h: reading a story's tables.
//
//  ★THE ONE WALK OF A STORY'S TABLES, which is Adobe's own (SnpIterTableUseDictHier): a dictionary IS
//  a table exactly when an ITableModel can be got from it, and the tables are put in the order their
//  cell blocks begin, which is the order fTableOrdinal counts in. KCMTextRead numbers the cells from
//  this list, which is what lets a shape's index name the same table the diff's cells name.
//
//  ⚠A MERGED CELL IS VISITED ONCE, AT ITS ANCHOR (IsAnchor); the covered addresses have no thread
//  of their own. GetCellArea at the anchor says how far it reaches (KCMStoryTextExport.cpp reads
//  the same two counts the same way).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <algorithm>

#include "IDataBase.h"
#include "ITableModel.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"			// GetTextStart / GetTextEnd - where a cell's text stands
#include "ITextStoryThreadDict.h"		// GetAnchorTextRange - where the table's anchor stands in the body
#include "ITextStoryThreadDictHier.h"	// the walk over a story's tables
#include "PersistUtils.h"				// ::GetDataBase / ::GetUIDRef
#include "RangeData.h"					// Text::StoryRange
#include "TableTypes.h"					// GridAddress / GridArea / RowRange / ColRange

#include "KCMSkippedText.h"				// the tables the page does not set (2026-09-24)
#include "KCMTableShape.h"

namespace
{

/* EarlierBlock
   **THE ORDER IS THE THREAD BLOCK'S, NOT THE ANCHOR'S**, and that is not a detail: a nested
   table's anchor sits inside its parent's CELLS - past the whole body - so sorting by anchor puts
   it after a second top-level table that is written before it. The SDK states the rule at
   ITextStoryThreadDict::GetThreadBlockTextRange ("the location of the dictionary's thread block is
   determined by the location of the dictionary's anchor relative to other dictionaries").
   The index in this order is KCMParaAttrs::fTableOrdinal, which is what SplitRunAtPlaces tells cells
   apart by, and two versions of one document have to number the same table the same way.
*/
bool EarlierBlock(const std::pair<TextIndex, KCMTableShape>& a, const std::pair<TextIndex, KCMTableShape>& b)
{
	return a.first < b.first;
}

}	// anonymous namespace

bool16 KCMReadTableShapes(ITextModel* model, std::vector<KCMTableShape>& out)
{
	out.clear();
	if (model == nil)
		return kFalse;
	// ★THE SAME TABLES LEFT OUT AS KCMTextRead LEAVES OUT (2026-09-24), or the index here and the
	//   cells' table ordinal there would name different tables (KCMSkippedText.h).
	KCMSkippedText skipped;
	skipped.Build(model);
	return KCMReadTableShapes(model, skipped, out);
}

bool16 KCMReadTableShapes(ITextModel* model, const KCMSkippedText& skipped, std::vector<KCMTableShape>& out)
{
	out.clear();
	if (model == nil)
		return kFalse;

	InterfacePtr<ITextStoryThreadDictHier> hier(model, UseDefaultIID());
	if (hier == nil)
		return kTrue;				// no hierarchy at all: a story with nothing but a body

	IDataBase* const db = ::GetDataBase(hier);
	if (db == nil)
		return kFalse;

	std::vector<std::pair<TextIndex, KCMTableShape> > found;

	for (UID next = ::GetUIDRef(hier).GetUID(); next != kInvalidUID; next = hier->NextUID(next))
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, next, UseDefaultIID());
		if (dict == nil)
			return kFalse;
		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (table == nil)
			continue;				// the story's own dictionary

		KCMTableShape shape;
		shape.fDictUID = next;

		// The anchor range: the anchor plus one continuation character per further row - measured
		// 2026-09-19 ([0,3) for three rows), so nothing has to be widened here.
		bool16 anchored = kFalse;
		const Text::StoryRange anchor = dict->GetAnchorTextRange(&anchored);
		shape.fAnchorStart = anchor.Start(nil);
		shape.fAnchorEnd = anchor.End();
		if (skipped.Contains(shape.fAnchorStart))
			continue;				// not a table of the page

		const RowRange rows = table->GetTotalRows();
		const ColRange cols = table->GetTotalCols();
		const RowRange header = table->GetHeaderRows();
		const RowRange footer = table->GetFooterRows();
		shape.fRows = rows.count;
		shape.fCols = cols.count;
		shape.fHeaderStart = header.start;
		shape.fHeaderCount = header.count;
		shape.fFooterStart = footer.start;
		shape.fFooterCount = footer.count;

		for (int32 r = rows.start; r < rows.start + rows.count; ++r)
		{
			for (int32 c = cols.start; c < cols.start + cols.count; ++c)
			{
				const GridAddress addr(r, c);
				// A merged cell is addressed by its anchor (ITableModel.h, at GridAddress): the covered addresses have
				// no thread of their own.
				if (!table->IsValid(addr) || !table->IsAnchor(addr))
					continue;

				const GridArea area = table->GetCellArea(addr);
				const RowRange areaRows = area.GetRows();
				const ColRange areaCols = area.GetCols();
				const int32 rowSpan = (areaRows.count > 0) ? areaRows.count : 1;
				const int32 colSpan = (areaCols.count > 0) ? areaCols.count : 1;
				if (rowSpan != 1 || colSpan != 1)
				{
					KCMTableCellShape merge;
					merge.fRow = r;
					merge.fCol = c;
					merge.fRowSpan = rowSpan;
					merge.fColSpan = colSpan;
					shape.fMerges.push_back(merge);
				}

				InterfacePtr<ITextStoryThread> thread(dict->QueryThread(table->GetGridID(addr)));
				KCMTableCellPlace place;
				place.fRow = r;
				place.fCol = c;
				if (thread == nil)
				{
					// a cell with no text of its own - a graphic cell (2026-09-25; KCMTableShape::fThreadless)
					shape.fThreadless.push_back(place);
					continue;
				}
				place.fStart = thread->GetTextStart();
				place.fEnd = thread->GetTextEnd();
				shape.fCells.push_back(place);
			}
		}

		found.push_back(std::make_pair(dict->GetThreadBlockTextRange().Start(nil), shape));
	}

	std::sort(found.begin(), found.end(), EarlierBlock);
	for (size_t i = 0; i < found.size(); ++i)
		out.push_back(found[i].second);
	return kTrue;
}

// End, KCMTableShape.cpp.
