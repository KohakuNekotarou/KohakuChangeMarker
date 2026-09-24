//========================================================================================
//
//  KCMTableShape.cpp -- the InDesign side of KCMTableShape.h: reading a story's tables.
//
//  ★THE WALK IS KCMTextRead's (BuildCellIndex), which is Adobe's own (SnpIterTableUseDictHier):
//  a dictionary IS a table exactly when an ITableModel can be got from it, and the tables are put
//  in the order their cell blocks begin, which is the order fTableOrdinal counts in. Keeping the
//  same walk here is what lets a shape's fOrdinal name the same table the diff's cells name.
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

	InterfacePtr<ITextStoryThreadDictHier> hier(model, UseDefaultIID());
	if (hier == nil)
		return kTrue;				// no hierarchy at all: a story with nothing but a body

	IDataBase* const db = ::GetDataBase(hier);
	if (db == nil)
		return kFalse;

	// ★THE SAME TABLES LEFT OUT AS KCMTextRead LEAVES OUT (2026-09-24), or fOrdinal here and the
	//   cells' table ordinal there would name different tables (KCMSkippedText.h).
	KCMSkippedText skipped;
	skipped.Build(model);

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
		shape.fRows = rows.count;
		shape.fCols = cols.count;

		for (int32 r = rows.start; r < rows.start + rows.count; ++r)
		{
			for (int32 c = cols.start; c < cols.start + cols.count; ++c)
			{
				const GridAddress addr(r, c);
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
				if (thread == nil)
					continue;
				KCMTableCellPlace place;
				place.fRow = r;
				place.fCol = c;
				place.fStart = thread->GetTextStart();
				place.fEnd = thread->GetTextEnd();
				shape.fCells.push_back(place);
			}
		}

		found.push_back(std::make_pair(dict->GetThreadBlockTextRange().Start(nil), shape));
	}

	std::sort(found.begin(), found.end(), EarlierBlock);
	for (size_t i = 0; i < found.size(); ++i)
	{
		found[i].second.fOrdinal = static_cast<int32>(i);
		out.push_back(found[i].second);
	}
	return kTrue;
}

// End, KCMTableShape.cpp.
