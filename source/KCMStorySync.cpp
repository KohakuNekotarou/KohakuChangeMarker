//========================================================================================
//
//  KCMStorySync.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storydocx-test).
#include "VCPlugInHeaders.h"

#include "KCMStorySync.h"
#include "KCMStoryDocx.h"		// WriteParts / Read / RejoinTables - the one format both sides go through
#include "KCMParaPairing.h"		// which paragraph goes with which
#include "KCMParaText.h"		// IsObjectCharacter / KeepTcyInsideWarichu's rule
#include "KCMTextDiff.h"		// ToCodePoints / Diff
#include "KCMZipStore.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <map>
#include <utility>

namespace KCMStorySync
{

std::string Where::Say() const
{
	char buf[64];
	if (fKind == kCell)
		std::snprintf(buf, sizeof(buf), "table %d row %d cell %d", static_cast<int>(fTable),
					  static_cast<int>(fRow), static_cast<int>(fCell));
	else if (fKind == kNote)
		std::snprintf(buf, sizeof(buf), "note %d", static_cast<int>(fNote) + 1);
	else
		std::snprintf(buf, sizeof(buf), "the body");
	return std::string(buf);
}

namespace
{

typedef std::vector<KCMStoryShape::Para> Paras;
typedef KCMTextDiff::Change Change;

std::string Num(int32 n)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(n));
	return std::string(buf);
}

bool16 SpansSame(const KCMAttrSpanList& a, const KCMAttrSpanList& b)
{
	if (a.size() != b.size())
		return kFalse;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].fStart != b[i].fStart || a[i].fLen != b[i].fLen || a[i].fValue != b[i].fValue
			|| a[i].fGroup != b[i].fGroup)
			return kFalse;
	}
	return kTrue;
}

std::vector<int32> RefPlaces(const KCMStoryShape::Para& p)
{
	std::vector<int32> out;
	for (size_t i = 0; i < p.fNoteRefs.size(); ++i)
		out.push_back(p.fNoteRefs[i].fAt);
	return out;
}

/** Everything a paragraph carries, the note references by PLACE only (their numbers are each side's own). */
bool16 ParaSame(const KCMStoryShape::Para& a, const KCMStoryShape::Para& b)
{
	return (a.fText == b.fText && SpansSame(a.fRuby, b.fRuby) && SpansSame(a.fKenten, b.fKenten)
			&& SpansSame(a.fTcy, b.fTcy) && SpansSame(a.fWarichu, b.fWarichu)
			&& RefPlaces(a) == RefPlaces(b) && a.fEndnoteAt == b.fEndnoteAt) ? kTrue : kFalse;
}

/** Where a table stands, how many rows, how many cells in each, and how far each reaches.
	★**THE HEADER FLAG IS NOT PART OF IT** (2026-09-23, the user's rule: header and footer rows are
	  rows like any other - only what happened to the cells is looked at). */
bool16 TableShapeSame(const KCMStoryShape::Table& a, const KCMStoryShape::Table& b, std::string& why)
{
	if (a.fInTable != b.fInTable || a.fInRow != b.fInRow || a.fInCell != b.fInCell)
	{
		why = "it stands somewhere else";
		return kFalse;
	}
	if (a.fRows.size() != b.fRows.size())
	{
		why = Num(static_cast<int32>(a.fRows.size())) + " row(s) became " + Num(static_cast<int32>(b.fRows.size()));
		return kFalse;
	}
	for (size_t r = 0; r < a.fRows.size(); ++r)
	{
		if (a.fRows[r].fCells.size() != b.fRows[r].fCells.size())
		{
			why = "row " + Num(static_cast<int32>(r)) + ": " + Num(static_cast<int32>(a.fRows[r].fCells.size()))
				  + " cell(s) became " + Num(static_cast<int32>(b.fRows[r].fCells.size()));
			return kFalse;
		}
		for (size_t c = 0; c < a.fRows[r].fCells.size(); ++c)
		{
			if (a.fRows[r].fCells[c].fColSpan != b.fRows[r].fCells[c].fColSpan
				|| a.fRows[r].fCells[c].fRowSpan != b.fRows[r].fCells[c].fRowSpan)
			{
				why = "row " + Num(static_cast<int32>(r)) + " cell " + Num(static_cast<int32>(c))
					  + ": cells were merged or split";
				return kFalse;
			}
		}
	}
	return kTrue;
}

/** One cell of a table, laid out on the table's grid. */
struct GridCell
{
	int32	fRow;		// the grid row - the row it is listed in
	int32	fIndex;		// its index in that row's fCells (what Table::fInCell counts)
	int32	fCol;		// the grid column
	int32	fRowSpan;
	int32	fColSpan;
};

/** A table's cells on its grid: each row's cells left to right, skipping the places a merge from above
	covers - the way InDesign counts a GridAddress. `columns` is how wide the grid is. */
void LayOut(const KCMStoryShape::Table& t, std::vector<GridCell>& out, int32& columns)
{
	out.clear();
	columns = 0;
	const size_t rows = t.fRows.size();
	std::vector< std::vector<bool16> > taken(rows);
	for (size_t r = 0; r < rows; ++r)
	{
		int32 col = 0;
		for (size_t k = 0; k < t.fRows[r].fCells.size(); ++k)
		{
			const KCMStoryShape::Cell& c = t.fRows[r].fCells[k];
			while (static_cast<size_t>(col) < taken[r].size() && taken[r][static_cast<size_t>(col)])
				++col;
			GridCell g;
			g.fRow = static_cast<int32>(r);
			g.fIndex = static_cast<int32>(k);
			g.fCol = col;
			g.fRowSpan = (c.fRowSpan > 1) ? c.fRowSpan : 1;
			g.fColSpan = (c.fColSpan > 1) ? c.fColSpan : 1;
			out.push_back(g);
			for (size_t rr = r; rr < r + static_cast<size_t>(g.fRowSpan) && rr < rows; ++rr)
			{
				if (taken[rr].size() < static_cast<size_t>(col + g.fColSpan))
					taken[rr].resize(static_cast<size_t>(col + g.fColSpan), kFalse);
				for (int32 cc = col; cc < col + g.fColSpan; ++cc)
					taken[rr][static_cast<size_t>(cc)] = kTrue;
			}
			col += g.fColSpan;
		}
		if (static_cast<int32>(taken[r].size()) > columns)
			columns = static_cast<int32>(taken[r].size());
	}
}

/** kTrue when every row of the grid is filled to `columns` - the only shape an InDesign table can have.
	Word keeps rows of different lengths (a cell given a wider span with the one beside it left). */
bool16 IsRectangular(const std::vector<GridCell>& cells, int32 rows, int32 columns)
{
	std::vector<int32> filled(static_cast<size_t>(rows > 0 ? rows : 0), 0);
	for (size_t i = 0; i < cells.size(); ++i)
		for (int32 r = cells[i].fRow; r < cells[i].fRow + cells[i].fRowSpan && r < rows; ++r)
			filled[static_cast<size_t>(r)] += cells[i].fColSpan;
	for (size_t r = 0; r < filled.size(); ++r)
		if (filled[r] != columns)
			return kFalse;
	return kTrue;
}

bool16 IsMerged(const GridCell& g)
{
	return (g.fRowSpan > 1 || g.fColSpan > 1) ? kTrue : kFalse;
}

/** kTrue when `cells` holds a cell starting at (row, col) and reaching exactly rowSpan x colSpan. */
bool16 HasCell(const std::vector<GridCell>& cells, int32 row, int32 col, int32 rowSpan, int32 colSpan)
{
	for (size_t i = 0; i < cells.size(); ++i)
		if (cells[i].fRow == row && cells[i].fCol == col && cells[i].fRowSpan == rowSpan && cells[i].fColSpan == colSpan)
			return kTrue;
	return kFalse;
}

/** kTrue when a table of `s` stands in cell `index` of row `row` of table t. */
bool16 TableStandsIn(const KCMStoryShape::Story& s, size_t t, int32 row, int32 index)
{
	for (size_t k = 0; k < s.fTables.size(); ++k)
		if (s.fTables[k].fInTable == static_cast<int32>(t) && s.fTables[k].fInRow == row && s.fTables[k].fInCell == index)
			return kTrue;
	return kFalse;
}

Step ShapeStep(int32 kind, size_t t, const GridCell* g)
{
	Step s;
	s.fKind = kind;
	s.fWhere = Where::Cell(static_cast<int32>(t), -1, -1);
	if (g != nil)
	{
		s.fGridRow = g->fRow;
		s.fGridCol = g->fCol;
		s.fGridRowSpan = g->fRowSpan;
		s.fGridColSpan = g->fColSpan;
	}
	return s;
}

/** What table t needs next to take W's shape (design section 9-1): 1 = take merges apart, 2 = add or take
	away rows and columns at the end, 3 = merge; the steps for that stage into `steps`. 0 = nothing (the
	shapes are the same). -1 = held, with `why`. ns / ws are the two stories, normalized. */
int32 PlanShape(const KCMStoryShape::Story& ns, const KCMStoryShape::Story& ws, size_t t,
				std::vector<Step>& steps, std::string& why)
{
	steps.clear();
	why.clear();
	const KCMStoryShape::Table& n = ns.fTables[t];
	const KCMStoryShape::Table& w = ws.fTables[t];
	if (n.fInTable != w.fInTable || n.fInRow != w.fInRow || n.fInCell != w.fInCell)
	{
		why = "it stands somewhere else";
		return -1;
	}
	std::vector<GridCell> nc;
	std::vector<GridCell> wc;
	int32 nCols = 0;
	int32 wCols = 0;
	LayOut(n, nc, nCols);
	LayOut(w, wc, wCols);
	const int32 nRows = static_cast<int32>(n.fRows.size());
	const int32 wRows = static_cast<int32>(w.fRows.size());
	if (wRows == 0 || wCols == 0 || nRows == 0 || nCols == 0)
	{
		why = (wRows == 0 || wCols == 0) ? "Word's table has no row or column left" : "the table has no cell";
		return -1;
	}
	// ★BEFORE ANYTHING CHANGES (re-check of S2, the matrix's H37): a shape InDesign cannot hold would be
	//   found only after rows or merges had already gone in, leaving the table half made
	if (!IsRectangular(wc, wRows, wCols))
	{
		why = "Word's table has rows of different lengths, which an InDesign table cannot";
		return -1;
	}
	// ★AND EVERY LATER STAGE'S REASON TO HOLD, asked now for the same reason: a table standing in a row or
	//  column that would go (stage 2), or in cells Word's merges would cover (stage 3)
	for (size_t i = 0; i < nc.size(); ++i)
		if ((nc[i].fRow >= wRows || nc[i].fCol >= wCols) && TableStandsIn(ns, t, nc[i].fRow, nc[i].fIndex))
		{
			why = "a table stands in a row or column that would be taken away";
			return -1;
		}
	for (size_t i = 0; i < wc.size(); ++i)
	{
		const GridCell& g = wc[i];
		if (!IsMerged(g) || HasCell(nc, g.fRow, g.fCol, g.fRowSpan, g.fColSpan))
			continue;
		for (size_t k = 0; k < nc.size(); ++k)
		{
			const GridCell& x = nc[k];
			const bool16 overlaps = (x.fRow < g.fRow + g.fRowSpan && x.fRow + x.fRowSpan > g.fRow
									 && x.fCol < g.fCol + g.fColSpan && x.fCol + x.fColSpan > g.fCol) ? kTrue : kFalse;
			if (overlaps && TableStandsIn(ns, t, x.fRow, x.fIndex))
			{
				why = "a table stands in cells that would be merged";
				return -1;
			}
		}
	}
	const bool16 addRows = (wRows > nRows) ? kTrue : kFalse;
	const bool16 addCols = (wCols > nCols) ? kTrue : kFalse;

	// ---- 1. merges taken apart: Word's does not have them, or rows / columns are about to be added
	//         next to them (InDesign copies the last row's cells into a new one), or they reach past
	//         where Word's grid ends
	for (size_t i = 0; i < nc.size(); ++i)
	{
		const GridCell& g = nc[i];
		if (!IsMerged(g))
			continue;
		bool16 keep = HasCell(wc, g.fRow, g.fCol, g.fRowSpan, g.fColSpan);
		if (addRows && g.fRow + g.fRowSpan == nRows)
			keep = kFalse;
		if (addCols && g.fCol + g.fColSpan == nCols)
			keep = kFalse;
		if (g.fRow + g.fRowSpan > wRows || g.fCol + g.fColSpan > wCols)
			keep = kFalse;
		if (keep)
			continue;
		if (TableStandsIn(ns, t, g.fRow, g.fIndex))
		{
			why = "a table stands in a merged cell that would be taken apart";
			return -1;
		}
		steps.push_back(ShapeStep(Step::kUnmerge, t, &g));
	}
	if (!steps.empty())
		return 1;

	// ---- 2. rows and columns, at the end ------------------------------------------------------------
	if (nRows != wRows || nCols != wCols)
	{
		for (size_t i = 0; i < nc.size(); ++i)
			if ((nc[i].fRow >= wRows || nc[i].fCol >= wCols) && TableStandsIn(ns, t, nc[i].fRow, nc[i].fIndex))
			{
				why = "a table stands in a row or column that would be taken away";
				return -1;
			}
		if (nRows != wRows)
		{
			Step s = ShapeStep(Step::kResizeRows, t, nil);
			s.fCount = wRows;
			steps.push_back(s);
		}
		if (nCols != wCols)
		{
			Step s = ShapeStep(Step::kResizeCols, t, nil);
			s.fCount = wCols;
			steps.push_back(s);
		}
		return 2;
	}

	// ---- 3. Word's merges made --------------------------------------------------------------------------
	for (size_t i = 0; i < wc.size(); ++i)
	{
		const GridCell& g = wc[i];
		if (!IsMerged(g) || HasCell(nc, g.fRow, g.fCol, g.fRowSpan, g.fColSpan))
			continue;
		for (size_t k = 0; k < nc.size(); ++k)
		{
			const GridCell& x = nc[k];
			if (x.fRow < g.fRow || x.fRow >= g.fRow + g.fRowSpan || x.fCol < g.fCol || x.fCol >= g.fCol + g.fColSpan)
				continue;
			if (IsMerged(x))
			{
				why = "the cells to merge are merged otherwise";
				return -1;
			}
			if (TableStandsIn(ns, t, x.fRow, x.fIndex))
			{
				why = "a table stands in cells that would be merged";
				return -1;
			}
		}
		steps.push_back(ShapeStep(Step::kMerge, t, &g));
	}
	if (!steps.empty())
		return 3;

	std::string tw;
	if (!TableShapeSame(n, w, tw))
	{
		why = tw;
		return -1;
	}
	return 0;
}

/** A table's own words, row by row - what "the same table" is judged by when the count of tables changes. */
std::string TableWords(const KCMStoryShape::Table& t)
{
	std::string out;
	for (size_t r = 0; r < t.fRows.size(); ++r)
		for (size_t c = 0; c < t.fRows[r].fCells.size(); ++c)
			for (size_t p = 0; p < t.fRows[r].fCells[c].fParas.size(); ++p)
			{
				out += t.fRows[r].fCells[c].fParas[p].fText;
				out += "\n";
			}
	return out;
}

/** How many tables stand in table t's cells, however deep. */
int32 TablesWithin(const KCMStoryShape::Story& s, size_t t)
{
	int32 n = 0;
	for (size_t k = 0; k < s.fTables.size(); ++k)
		if (s.fTables[k].fInTable == static_cast<int32>(t))
			n += 1 + TablesWithin(s, k);
	return n;
}

/** How many tables stand directly in the cells of table t. */
int32 TablesInside(const KCMStoryShape::Story& s, size_t t)
{
	int32 n = 0;
	for (size_t k = 0; k < s.fTables.size(); ++k)
		if (s.fTables[k].fInTable == static_cast<int32>(t))
			++n;
	return n;
}

/** Stage 0 (S3a, design section 10-2): when the body holds a different number of tables in N and in W,
	which of them went and which came - matched BY THEIR WORDS (the user's rule), in order - and where a
	new one goes. `now` is the document's shape, `word` Word's split one. 0 = the body's tables pair k-th
	with k-th; 1 = the steps; -1 = the story is held, with `why`. */
int32 PlanTables(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word,
				 std::vector<Step>& steps, std::string& why)
{
	steps.clear();
	why.clear();
	std::vector<size_t> nb;
	std::vector<size_t> wb;
	for (size_t t = 0; t < now.fTables.size(); ++t)
		if (now.fTables[t].fInTable < 0)
			nb.push_back(t);
	for (size_t t = 0; t < word.fTables.size(); ++t)
		if (word.fTables[t].fInTable < 0)
			wb.push_back(t);
	if (nb.size() == wb.size())
	{
		if (now.fTables.size() != word.fTables.size())
		{
			why = "a table inside a cell was added or taken away, which the import does not do yet";
			return -1;
		}
		return 0;
	}

	std::vector<std::string> nt;
	std::vector<std::string> wt;
	for (size_t i = 0; i < nb.size(); ++i)
		nt.push_back(TableWords(now.fTables[nb[i]]));
	for (size_t i = 0; i < wb.size(); ++i)
		wt.push_back(TableWords(word.fTables[wb[i]]));

	// ★BY THEIR WORDS, IN ORDER: of the longer list, the ordered choice that shares the most characters
	//   with the shorter (the paragraphs' own rule, KCMParaPairing::ChooseOrdered)
	std::vector<int32> nOfW(wb.size(), -1);
	std::vector<int32> wOfN(nb.size(), -1);
	std::vector<int32> chosen;
	if (nb.size() > wb.size())
	{
		KCMParaPairing::ChooseOrdered(wt, 0, static_cast<int32>(wt.size()), nt, 0, static_cast<int32>(nt.size()), kFalse, chosen);
		for (size_t j = 0; j < chosen.size(); ++j)
		{
			nOfW[j] = chosen[j];
			wOfN[static_cast<size_t>(chosen[j])] = static_cast<int32>(j);
		}
	}
	else
	{
		KCMParaPairing::ChooseOrdered(nt, 0, static_cast<int32>(nt.size()), wt, 0, static_cast<int32>(wt.size()), kTrue, chosen);
		for (size_t i = 0; i < chosen.size(); ++i)
		{
			wOfN[i] = chosen[i];
			nOfW[static_cast<size_t>(chosen[i])] = static_cast<int32>(i);
		}
	}

	// the tables that pair keep what stands inside them; a table that goes takes its own away
	for (size_t i = 0; i < nb.size(); ++i)
	{
		if (wOfN[i] < 0)
			continue;
		if (TablesInside(now, nb[i]) != TablesInside(word, wb[static_cast<size_t>(wOfN[i])]))
		{
			why = "a table inside a cell was added or taken away, which the import does not do yet";
			return -1;
		}
	}

	// ★AND THE WHOLE COUNT, before anything changes (re-check 2026-09-24): the tables that go take what is
	//  nested in them, the new ones hold nothing - so what is left over is a table added or taken away
	//  deeper inside a table that stays, which the next round would find only after this one had run
	{
		int32 left = static_cast<int32>(now.fTables.size());
		int32 added = 0;
		for (size_t i = 0; i < nb.size(); ++i)
			if (wOfN[i] < 0)
				left -= 1 + TablesWithin(now, nb[i]);
		for (size_t j = 0; j < wb.size(); ++j)
			if (nOfW[j] < 0)
				++added;
		if (left + added != static_cast<int32>(word.fTables.size()))
		{
			why = "a table inside a cell was added or taken away, which the import does not do yet";
			return -1;
		}
	}

	// where a new table goes: after the document paragraph that pairs with the one before it in Word
	std::vector<std::string> nParas;
	std::vector<std::string> wParas;
	for (size_t p = 0; p < now.fBody.size(); ++p)
		nParas.push_back(now.fBody[p].fText);
	for (size_t p = 0; p < word.fBody.size(); ++p)
		wParas.push_back(word.fBody[p].fText);
	std::vector<KCMParaPairing::Step> pairing;
	KCMParaPairing::Pair(nParas, wParas, pairing);
	std::vector<int32> docOfFile(wParas.size(), -1);
	for (size_t k = 0; k < pairing.size(); ++k)
		if (pairing[k].fKind == KCMParaPairing::Step::kPair && pairing[k].fFile >= 0
			&& static_cast<size_t>(pairing[k].fFile) < docOfFile.size())
			docOfFile[static_cast<size_t>(pairing[k].fFile)] = pairing[k].fDoc;

	for (size_t i = 0; i < nb.size(); ++i)
		if (wOfN[i] < 0)
		{
			Step s;
			s.fKind = Step::kDeleteTable;
			s.fWhere = Where::Cell(static_cast<int32>(nb[i]), -1, -1);
			steps.push_back(s);
		}
	for (size_t j = 0; j < wb.size(); ++j)
	{
		if (nOfW[j] >= 0)
			continue;
		const KCMStoryShape::Table& t = word.fTables[wb[j]];
		if (TablesInside(word, wb[j]) > 0)
		{
			why = "a table Word added holds a table of its own, which the import does not do yet";
			return -1;
		}
		std::vector<GridCell> cells;
		int32 cols = 0;
		LayOut(t, cells, cols);
		const int32 rows = static_cast<int32>(t.fRows.size());
		if (rows == 0 || cols == 0 || !IsRectangular(cells, rows, cols))
		{
			why = "a table Word added has no cell, or rows of different lengths";
			return -1;
		}
		Step s;
		s.fKind = Step::kInsertTable;
		s.fWhere = Where::Body();
		s.fPara = -1;
		for (int32 q = t.fParaIndex - 1; q >= 0; --q)
			if (static_cast<size_t>(q) < docOfFile.size() && docOfFile[static_cast<size_t>(q)] >= 0)
			{
				s.fPara = docOfFile[static_cast<size_t>(q)];
				break;
			}
		s.fCount = rows;
		s.fAt = cols;
		s.fNote = static_cast<int32>(j);
		steps.push_back(s);
	}
	return steps.empty() ? 0 : 1;
}

/** The tables of `s` standing in one place, in document order. */
void TablesIn(const KCMStoryShape::Story& s, int32 inTable, int32 inRow, int32 inCell, std::vector<size_t>& out)
{
	out.clear();
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		const KCMStoryShape::Table& x = s.fTables[t];
		if (x.fInTable != inTable)
			continue;
		if (inTable >= 0 && (x.fInRow != inRow || x.fInCell != inCell))
			continue;
		out.push_back(t);
	}
}

const Paras* ParasAt(const KCMStoryShape::Story& s, const Where& w)
{
	if (w.fKind == Where::kBody)
		return &s.fBody;
	if (w.fKind == Where::kNote)
		return (w.fNote >= 0 && static_cast<size_t>(w.fNote) < s.fNotes.size()) ? &s.fNotes[static_cast<size_t>(w.fNote)] : nil;
	if (w.fTable < 0 || static_cast<size_t>(w.fTable) >= s.fTables.size())
		return nil;
	const KCMStoryShape::Table& t = s.fTables[static_cast<size_t>(w.fTable)];
	if (w.fRow < 0 || static_cast<size_t>(w.fRow) >= t.fRows.size())
		return nil;
	if (w.fCell < 0 || static_cast<size_t>(w.fCell) >= t.fRows[static_cast<size_t>(w.fRow)].fCells.size())
		return nil;
	return &t.fRows[static_cast<size_t>(w.fRow)].fCells[static_cast<size_t>(w.fCell)].fParas;
}

Paras* ParasAt(KCMStoryShape::Story& s, const Where& w)
{
	return const_cast<Paras*>(ParasAt(static_cast<const KCMStoryShape::Story&>(s), w));
}

/** A position of N's paragraph carried through the character changes to W's, or -1 when a change
	takes away the character it stands before. A position AT the start of a change stays before it. */
int32 MapThrough(const std::vector<Change>& ch, int32 pos)
{
	int32 delta = 0;
	for (size_t i = 0; i < ch.size(); ++i)
	{
		if (pos <= ch[i].aStart)
			return pos + delta;
		if (pos < ch[i].aStart + ch[i].aCount)
			return -1;
		delta += ch[i].bCount - ch[i].aCount;
	}
	return pos + delta;
}

/** Where W puts the text that follows a change starting exactly at `pos` - the other place a
	reference standing at `pos` may have gone. ★**A REFERENCE AT THE START OF A CHANGE CAN LAND ON
	EITHER SIDE OF IT** (measured by the harness 2026-09-23: "X" typed where a reference stood came
	out before the reference in Word, after it in MapThrough), and the text alone cannot say which.
	-1 when no change starts at `pos`. */
int32 MapPastChangeAt(const std::vector<Change>& ch, int32 pos)
{
	int32 delta = 0;
	for (size_t i = 0; i < ch.size(); ++i)
	{
		if (pos < ch[i].aStart)
			return -1;
		if (pos == ch[i].aStart)
			return pos + delta + ch[i].bCount;
		delta += ch[i].bCount - ch[i].aCount;
	}
	return -1;
}

/** One comparison's state: the three stories and the plan being written. */
struct Run
{
	const KCMStoryShape::Story*	fNow;		// the document as read - for the tate-chu-yoko guard
	const KCMStoryShape::Story*	fN;			// the document through this format (Normalize)
	const KCMStoryShape::Story*	fW;			// Word's, in the document's table shape (Normalize)
	Plan*						fPlan;
	std::vector<int32>			fWordOfNote;	// N's note -> the W note it pairs with, or -1
	std::vector<bool16>			fTableHeld;

	Run() : fNow(nil), fN(nil), fW(nil), fPlan(nil) {}
};

void Hold(Run& run, const Where& where, int32 para, const char* what, const std::string& why)
{
	Step s;
	s.fKind = Step::kHeld;
	s.fWhere = where;
	s.fPara = para;
	s.fWhat = what;
	s.fWhy = why;
	run.fPlan->fSteps.push_back(s);
}

void AddNote(Run& run, const Where& where, int32 resultPara, int32 at, int32 wordNote)
{
	Step s;
	s.fKind = Step::kAddNote;
	s.fWhere = where;
	s.fPara = resultPara;
	s.fAt = at;
	s.fNote = wordNote;
	if (wordNote >= 0 && static_cast<size_t>(wordNote) < run.fW->fNotes.size())
		s.fParas = run.fW->fNotes[static_cast<size_t>(wordNote)];
	run.fPlan->fSteps.push_back(s);
}

void DeleteNote(Run& run, int32 nowNote)
{
	Step s;
	s.fKind = Step::kDeleteNote;
	s.fNote = nowNote;
	run.fPlan->fSteps.push_back(s);
}

/** The references of a paired paragraph: which of N's stay (returned, at W's offsets, numbered as N's
	notes), which go (kDeleteNote) and which W adds (kAddNote). Paired by their offsets with a diff -
	★rank alone pairs wrong when a reference is added at the offset one already stands at (measured
	on Word's own file, 2026-09-22 - the three-way merge this replaced said so first). A reference the
	changed words took away goes, and W's one there comes as a new note with W's words. */
std::vector<KCMStoryShape::NoteRef> PlanRefs(Run& run, const Where& where, int32 result,
											 const KCMStoryShape::Para& n, const KCMStoryShape::Para& w,
											 const std::vector<Change>& ch)
{
	std::vector<int32> wAt = RefPlaces(w);
	std::vector<int32> nAt;
	std::vector<int32> nNote;
	for (size_t i = 0; i < n.fNoteRefs.size(); ++i)
	{
		int32 m = MapThrough(ch, n.fNoteRefs[i].fAt);
		if (m < 0)
		{
			DeleteNote(run, n.fNoteRefs[i].fNote);
			continue;
		}
		// at the start of a change: W decides which side it stands on, when W has one there
		const int32 past = MapPastChangeAt(ch, n.fNoteRefs[i].fAt);
		if (past >= 0 && std::find(wAt.begin(), wAt.end(), m) == wAt.end()
			&& std::find(wAt.begin(), wAt.end(), past) != wAt.end())
			m = past;
		nAt.push_back(m);
		nNote.push_back(n.fNoteRefs[i].fNote);
	}

	std::vector<Change> d;
	if (!KCMTextDiff::Diff(nAt, wAt, d))
	{
		d.clear();
		Change all;
		all.aStart = 0; all.aCount = static_cast<int32>(nAt.size());
		all.bStart = 0; all.bCount = static_cast<int32>(wAt.size());
		d.push_back(all);
	}

	std::vector<KCMStoryShape::NoteRef> kept;
	size_t ni = 0;
	size_t wi = 0;
	for (size_t c = 0; c <= d.size(); ++c)
	{
		const size_t nEnd = (c < d.size()) ? static_cast<size_t>(d[c].aStart) : nAt.size();
		const size_t wEnd = (c < d.size()) ? static_cast<size_t>(d[c].bStart) : wAt.size();
		for (; ni < nEnd && wi < wEnd; ++ni, ++wi)
		{
			KCMStoryShape::NoteRef r;
			r.fAt = wAt[wi];
			r.fNote = nNote[ni];
			kept.push_back(r);
			const int32 wn = w.fNoteRefs[wi].fNote;
			if (nNote[ni] >= 0 && static_cast<size_t>(nNote[ni]) < run.fWordOfNote.size())
				run.fWordOfNote[static_cast<size_t>(nNote[ni])] = wn;
		}
		if (c >= d.size())
			break;
		for (int32 k = 0; k < d[c].aCount; ++k)
			DeleteNote(run, nNote[static_cast<size_t>(d[c].aStart + k)]);
		for (int32 k = 0; k < d[c].bCount; ++k)
		{
			const size_t j = static_cast<size_t>(d[c].bStart + k);
			AddNote(run, where, result, wAt[j], w.fNoteRefs[j].fNote);
		}
		ni = static_cast<size_t>(d[c].aStart + d[c].aCount);
		wi = static_cast<size_t>(d[c].bStart + d[c].bCount);
	}
	return kept;
}

/** ★WORD CANNOT HOLD A TATE-CHU-YOKO INSIDE A WARICHU (KCMParaText::KeepTcyInsideWarichu says why and
	how it was measured). One the DOCUMENT has there, whose words W still has unchanged but not the
	tate-chu-yoko, is kept - carried to W's offsets - and named. One whose words W changed is W's to
	decide, and nothing is said. */
void KeepTcy(Run& run, const Where& where, int32 nIndex, const KCMStoryShape::Para& raw,
			 const std::vector<Change>& ch, KCMStoryShape::Para& target)
{
	for (size_t i = 0; i < raw.fTcy.size(); ++i)
	{
		const KCMAttrSpan& t = raw.fTcy[i];
		bool16 underWarichu = kFalse;
		for (size_t k = 0; k < raw.fWarichu.size() && !underWarichu; ++k)
		{
			const int32 ws = raw.fWarichu[k].fStart;
			const int32 we = ws + raw.fWarichu[k].fLen;
			underWarichu = (ws < t.fStart + t.fLen && t.fStart < we) ? kTrue : kFalse;
		}
		if (!underWarichu)
			continue;
		const int32 s = MapThrough(ch, t.fStart);
		const int32 e = MapThrough(ch, t.fStart + t.fLen);
		bool16 there = kFalse;
		for (size_t k = 0; k < target.fTcy.size() && !there; ++k)
			there = (target.fTcy[k].fStart == s && target.fTcy[k].fLen == t.fLen) ? kTrue : kFalse;
		if (there)
			continue;
		// ★THE WORDS UNDER IT CHANGED IN WORD: then it is Word's edit, not something Word could not
		//   carry, and W decides (measured on the matrix 2026-09-23: A39 emptied a warichu, and a
		//   "could not be kept" named a tate-chu-yoko whose words the reader had taken out).
		if (s < 0 || e < 0 || e - s != t.fLen)
			continue;
		KCMAttrSpan kept = t;
		kept.fStart = s;
		target.fTcy.push_back(kept);
		std::sort(target.fTcy.begin(), target.fTcy.end(),
				  [](const KCMAttrSpan& x, const KCMAttrSpan& y) { return x.fStart < y.fStart; });
		Hold(run, where, nIndex, "Tcy", "a tate-chu-yoko inside a warichu was kept: Word cannot carry one there");
	}
}

/** One of N's paragraphs against the W paragraph it pairs with. `result` is its index among the
	place's paragraphs once the plan is carried out. `tablesHere`: (N's table ordinal, its offset in
	N's paragraph) for every table standing in this paragraph. */
void ComparePara(Run& run, const Where& where, int32 nIndex, int32 result,
				 const KCMStoryShape::Para& n, const KCMStoryShape::Para& raw, const KCMStoryShape::Para& w,
				 const std::vector< std::pair<int32, int32> >& tablesHere)
{
	// ★★THE TABLES' PLACES ARE PART OF THE PARAGRAPH (2026-09-24, S3b X06 - design 12-3-2): words moved from one
	//   side of a table to the other leave the text the same and the table somewhere else - "ab[T]cd" and
	//   "abcd[T]" both read "abcd", and this used to answer "the same" and plan nothing (the matrix's X06 went
	//   in as nothing, with no "!"). The cuts are the tables' offsets in each; the diff is taken piece by piece
	//   between them (DiffInPieces), so a move comes back as words put in on one side and taken out on the other.
	std::vector<int32> nCuts;
	std::vector<int32> wCuts;
	for (size_t t = 0; t < tablesHere.size(); ++t)
	{
		nCuts.push_back(tablesHere[t].second);
		wCuts.push_back(run.fW->fTables[static_cast<size_t>(tablesHere[t].first)].fOffset);
	}
	if (ParaSame(n, w) && nCuts == wCuts)
	{
		// the same references, one for one: their notes pair
		for (size_t k = 0; k < n.fNoteRefs.size() && k < w.fNoteRefs.size(); ++k)
		{
			const int32 nn = n.fNoteRefs[k].fNote;
			if (nn >= 0 && static_cast<size_t>(nn) < run.fWordOfNote.size())
				run.fWordOfNote[static_cast<size_t>(nn)] = w.fNoteRefs[k].fNote;
		}
		return;
	}

	std::vector<int32> a;
	std::vector<int32> b;
	KCMTextDiff::ToCodePoints(n.fText, &a, nil);
	KCMTextDiff::ToCodePoints(w.fText, &b, nil);
	std::vector<Change> ch;
	const bool16 diffed = nCuts.empty() ? KCMTextDiff::Diff(a, b, ch) : KCMTextDiff::DiffInPieces(a, nCuts, b, wCuts, ch);
	if (!diffed)
	{
		Hold(run, where, nIndex, "Para", "the paragraph differs too much to place the changes");
		return;
	}

	for (size_t c = 0; c < ch.size(); ++c)
	{
		for (int32 i = ch[c].aStart; i < ch[c].aStart + ch[c].aCount; ++i)
		{
			if (KCMParaText::IsObjectCharacter(a[static_cast<size_t>(i)]))
			{
				Hold(run, where, nIndex, "Para",
					 "a change would take away an object's character (an anchored frame, a variable, an index marker...)");
				return;
			}
		}
		for (int32 j = ch[c].bStart; j < ch[c].bStart + ch[c].bCount; ++j)
		{
			if (KCMParaText::IsObjectCharacter(b[static_cast<size_t>(j)]))
			{
				Hold(run, where, nIndex, "Para", "Word added an object's character, which an import cannot make");
				return;
			}
		}
		// (⛔A change CROSSING a table's place was held here for a day - 2026-09-23. It need not be: the
		//   apply cuts such a change at the table and puts each side's words on its side, by where W's
		//   table stands (ApplyParagraph / KCMParaText::CutChangeAtObjects, fTables below). Measured on
		//   the live matrix: A15, A16, H46-48 went in before the rebuild and were held by this guard.)
	}

	std::vector<int32> ends;
	for (size_t e = 0; e < n.fEndnoteAt.size(); ++e)
		ends.push_back(MapThrough(ch, n.fEndnoteAt[e]));
	if (ends != w.fEndnoteAt)
	{
		Hold(run, where, nIndex, "Para", "an endnote's mark would move, go or come - endnotes are not carried");
		return;
	}

	KCMStoryShape::Para target = w;
	target.fNoteRefs = PlanRefs(run, where, result, n, w, ch);
	if (raw.fText == n.fText)
		KeepTcy(run, where, nIndex, raw, ch, target);

	Step s;
	s.fKind = Step::kSetPara;
	s.fWhere = where;
	s.fPara = nIndex;
	s.fParas.push_back(target);
	for (size_t t = 0; t < tablesHere.size(); ++t)
	{
		const int32 ordinal = tablesHere[t].first;
		s.fTables.push_back(std::make_pair(ordinal, run.fW->fTables[static_cast<size_t>(ordinal)].fOffset));
	}
	run.fPlan->fSteps.push_back(s);
}

/** A paragraph taken out or put in whole must not carry what an import cannot take out or put in. */
bool16 HoldsObjectOrEndnote(const KCMStoryShape::Para& p)
{
	if (!p.fEndnoteAt.empty())
		return kTrue;
	std::vector<int32> cps;
	KCMTextDiff::ToCodePoints(p.fText, &cps, nil);
	for (size_t i = 0; i < cps.size(); ++i)
		if (KCMParaText::IsObjectCharacter(cps[i]))
			return kTrue;
	return kFalse;
}

/** The paragraphs of N in [nFrom, nTo) against W's in [wFrom, wTo) - a run with no table in it. */
void PairRun(Run& run, const Where& nWhere, const Where& wWhere, int32 nFrom, int32 nTo, int32 wFrom, int32 wTo,
			 int32& result)
{
	const Paras& np = *ParasAt(*run.fN, nWhere);
	const Paras& rp = *ParasAt(*run.fNow, nWhere);
	const Paras& wp = *ParasAt(*run.fW, wWhere);

	std::vector<std::string> nt;
	std::vector<std::string> wt;
	for (int32 i = nFrom; i < nTo; ++i) nt.push_back(np[static_cast<size_t>(i)].fText);
	for (int32 i = wFrom; i < wTo; ++i) wt.push_back(wp[static_cast<size_t>(i)].fText);
	std::vector<KCMParaPairing::Step> steps;
	KCMParaPairing::Pair(nt, wt, steps);

	const std::vector< std::pair<int32, int32> > noTables;
	for (size_t k = 0; k < steps.size(); ++k)
	{
		const KCMParaPairing::Step& st = steps[k];
		if (st.fKind == KCMParaPairing::Step::kPair)
		{
			const size_t ni = static_cast<size_t>(nFrom + st.fDoc);
			ComparePara(run, nWhere, nFrom + st.fDoc, result, np[ni], rp[ni], wp[static_cast<size_t>(wFrom + st.fFile)], noTables);
			++result;
		}
		else if (st.fKind == KCMParaPairing::Step::kInsert)
		{
			Step s;
			s.fKind = Step::kInsertParas;
			s.fWhere = nWhere;
			s.fPara = (st.fDoc < 0) ? nFrom - 1 : nFrom + st.fDoc;
			bool16 refused = kFalse;
			for (int32 i = 0; i < st.fCount; ++i)
			{
				KCMStoryShape::Para p = wp[static_cast<size_t>(wFrom + st.fFile + i)];
				if (HoldsObjectOrEndnote(p))
					refused = kTrue;
				for (size_t r = 0; r < p.fNoteRefs.size(); ++r)
					AddNote(run, nWhere, result + i, p.fNoteRefs[r].fAt, p.fNoteRefs[r].fNote);
				p.fNoteRefs.clear();
				s.fParas.push_back(p);
			}
			if (refused)
				Hold(run, nWhere, s.fPara, "Para", "Word added a paragraph holding an object's character or an endnote's mark");
			else
			{
				run.fPlan->fSteps.push_back(s);
				result += st.fCount;
			}
		}
		else	// kDelete
		{
			bool16 refused = kFalse;
			for (int32 i = 0; i < st.fCount; ++i)
				if (HoldsObjectOrEndnote(np[static_cast<size_t>(nFrom + st.fDoc + i)]))
					refused = kTrue;
			if (refused)
			{
				Hold(run, nWhere, nFrom + st.fDoc, "Para",
					 "Word took out a paragraph holding an object's character or an endnote's mark");
				result += st.fCount;		// they stay
				continue;
			}
			for (int32 i = 0; i < st.fCount; ++i)
			{
				const KCMStoryShape::Para& gone = np[static_cast<size_t>(nFrom + st.fDoc + i)];
				for (size_t r = 0; r < gone.fNoteRefs.size(); ++r)
					DeleteNote(run, gone.fNoteRefs[r].fNote);
			}
			Step s;
			s.fKind = Step::kDeleteParas;
			s.fWhere = nWhere;
			s.fPara = nFrom + st.fDoc;
			s.fCount = st.fCount;
			run.fPlan->fSteps.push_back(s);
		}
	}
}

/** One place: the body, a cell or a note. ★THE TABLES STANDING HERE ARE PEGS - the paragraphs that
	hold them pair with each other, k-th with k-th, and the runs between them are paired on their
	own, so no paragraph pairing ever reaches across a table. */
void ComparePlace(Run& run, const Where& nWhere, const Where& wWhere)
{
	const Paras* np = ParasAt(*run.fN, nWhere);
	const Paras* rp = ParasAt(*run.fNow, nWhere);
	const Paras* wp = ParasAt(*run.fW, wWhere);
	if (np == nil || rp == nil || wp == nil || np->size() != rp->size())
	{
		Hold(run, nWhere, -1, "Place", nWhere.Say() + " could not be lined up");
		return;
	}

	std::vector<size_t> nt;
	std::vector<size_t> wt;
	if (nWhere.fKind == Where::kBody)
	{
		TablesIn(*run.fN, -1, 0, 0, nt);
		TablesIn(*run.fW, -1, 0, 0, wt);
	}
	else if (nWhere.fKind == Where::kCell)
	{
		TablesIn(*run.fN, nWhere.fTable, nWhere.fRow, nWhere.fCell, nt);
		TablesIn(*run.fW, wWhere.fTable, wWhere.fRow, wWhere.fCell, wt);
	}
	if (nt.size() != wt.size())
	{
		Hold(run, nWhere, -1, "Table", "the number of tables in " + nWhere.Say() + " changed");
		return;
	}

	// the pegs: the paragraphs holding tables, each once, paired k-th with k-th
	std::vector<int32> nPeg;
	std::vector<int32> wPeg;
	for (size_t k = 0; k < nt.size(); ++k)
	{
		const int32 a = run.fN->fTables[nt[k]].fParaIndex;
		const int32 b = run.fW->fTables[wt[k]].fParaIndex;
		const bool16 newN = (nPeg.empty() || nPeg.back() != a) ? kTrue : kFalse;
		const bool16 newW = (wPeg.empty() || wPeg.back() != b) ? kTrue : kFalse;
		if (newN != newW)
		{
			Hold(run, nWhere, -1, "Table", "the tables in " + nWhere.Say() + " stand in different paragraphs");
			return;
		}
		if (newN)
		{
			nPeg.push_back(a);
			wPeg.push_back(b);
		}
	}

	int32 nFrom = 0;
	int32 wFrom = 0;
	int32 result = 0;
	for (size_t g = 0; g <= nPeg.size(); ++g)
	{
		const int32 nTo = (g < nPeg.size()) ? nPeg[g] : static_cast<int32>(np->size());
		const int32 wTo = (g < wPeg.size()) ? wPeg[g] : static_cast<int32>(wp->size());
		PairRun(run, nWhere, wWhere, nFrom, nTo, wFrom, wTo, result);
		if (g == nPeg.size())
			break;
		std::vector< std::pair<int32, int32> > here;
		for (size_t k = 0; k < nt.size(); ++k)
		{
			if (run.fN->fTables[nt[k]].fParaIndex == nPeg[g])
				here.push_back(std::make_pair(static_cast<int32>(nt[k]), run.fN->fTables[nt[k]].fOffset));
		}
		ComparePara(run, nWhere, nPeg[g], result, (*np)[static_cast<size_t>(nPeg[g])],
					(*rp)[static_cast<size_t>(nPeg[g])], (*wp)[static_cast<size_t>(wPeg[g])], here);
		++result;
		nFrom = nPeg[g] + 1;
		wFrom = wPeg[g] + 1;
	}
}

/** Every place's paragraph count, table by table and note by note - what Normalize checks N' against. */
bool16 SameLayout(const KCMStoryShape::Story& a, const KCMStoryShape::Story& b)
{
	if (a.fBody.size() != b.fBody.size() || a.fTables.size() != b.fTables.size() || a.fNotes.size() != b.fNotes.size())
		return kFalse;
	for (size_t t = 0; t < a.fTables.size(); ++t)
	{
		if (a.fTables[t].fRows.size() != b.fTables[t].fRows.size())
			return kFalse;
		for (size_t r = 0; r < a.fTables[t].fRows.size(); ++r)
		{
			if (a.fTables[t].fRows[r].fCells.size() != b.fTables[t].fRows[r].fCells.size())
				return kFalse;
			for (size_t c = 0; c < a.fTables[t].fRows[r].fCells.size(); ++c)
				if (a.fTables[t].fRows[r].fCells[c].fParas.size() != b.fTables[t].fRows[r].fCells[c].fParas.size())
					return kFalse;
		}
	}
	for (size_t n = 0; n < a.fNotes.size(); ++n)
		if (a.fNotes[n].size() != b.fNotes[n].size())
			return kFalse;
	return kTrue;
}

// ---- ApplyToShape's pieces ----------------------------------------------------------------------

void ApplyPlace(KCMStoryShape::Story& out, const Where& where, const Plan& plan)
{
	Paras* ps = ParasAt(out, where);
	if (ps == nil)
		return;
	const Paras before = *ps;

	std::map<int32, const KCMStoryShape::Para*> set;
	std::map<int32, const Step*> setStep;
	std::map<int32, const Paras*> insertAfter;
	std::vector<bool16> gone(before.size(), kFalse);
	bool16 any = kFalse;
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const Step& s = plan.fSteps[i];
		if (!(s.fWhere == where))
			continue;
		if (s.fKind == Step::kSetPara && !s.fParas.empty())
		{
			set[s.fPara] = &s.fParas[0];
			setStep[s.fPara] = &s;
			any = kTrue;
		}
		else if (s.fKind == Step::kInsertParas)
		{
			insertAfter[s.fPara] = &s.fParas;
			any = kTrue;
		}
		else if (s.fKind == Step::kDeleteParas)
		{
			for (int32 k = s.fPara; k < s.fPara + s.fCount && k < static_cast<int32>(gone.size()); ++k)
				gone[static_cast<size_t>(k)] = kTrue;
			any = kTrue;
		}
	}
	if (!any)
		return;

	// the tables standing in this place, found before the paragraphs move
	std::vector<size_t> tables;
	if (where.fKind == Where::kBody)
		TablesIn(out, -1, 0, 0, tables);
	else if (where.fKind == Where::kCell)
		TablesIn(out, where.fTable, where.fRow, where.fCell, tables);

	Paras result;
	std::vector<int32> newIndexOf(before.size(), -1);
	std::map<int32, const Paras*>::const_iterator head = insertAfter.find(-1);
	if (head != insertAfter.end())
		result.insert(result.end(), head->second->begin(), head->second->end());
	for (size_t i = 0; i < before.size(); ++i)
	{
		if (!gone[i])
		{
			newIndexOf[i] = static_cast<int32>(result.size());
			std::map<int32, const KCMStoryShape::Para*>::const_iterator s = set.find(static_cast<int32>(i));
			result.push_back((s != set.end()) ? *s->second : before[i]);
		}
		std::map<int32, const Paras*>::const_iterator ins = insertAfter.find(static_cast<int32>(i));
		if (ins != insertAfter.end())
			result.insert(result.end(), ins->second->begin(), ins->second->end());
	}
	*ps = result;

	for (size_t k = 0; k < tables.size(); ++k)
	{
		KCMStoryShape::Table& t = out.fTables[tables[k]];
		const int32 old = t.fParaIndex;
		if (old >= 0 && static_cast<size_t>(old) < newIndexOf.size() && newIndexOf[static_cast<size_t>(old)] >= 0)
			t.fParaIndex = newIndexOf[static_cast<size_t>(old)];
		std::map<int32, const Step*>::const_iterator s = setStep.find(old);
		if (s == setStep.end())
			continue;
		for (size_t j = 0; j < s->second->fTables.size(); ++j)
			if (s->second->fTables[j].first == static_cast<int32>(tables[k]))
				t.fOffset = s->second->fTables[j].second;
	}
}

void AllPlaces(const KCMStoryShape::Story& s, std::vector<Where>& out)
{
	out.clear();
	out.push_back(Where::Body());
	for (size_t t = 0; t < s.fTables.size(); ++t)
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				out.push_back(Where::Cell(static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c)));
	for (size_t n = 0; n < s.fNotes.size(); ++n)
		out.push_back(Where::Note(static_cast<int32>(n)));
}

/** Reading order: a place's paragraphs, and inside each its references and the tables standing in
	it, by position (a reference before a table at the same offset). */
void ReadPlace(const KCMStoryShape::Story& s, const Paras& ps, int32 inT, int32 inR, int32 inC, std::vector<int32>& order)
{
	std::vector<size_t> tables;
	TablesIn(s, inT, inR, inC, tables);
	for (size_t p = 0; p < ps.size(); ++p)
	{
		std::vector< std::pair< std::pair<int32, int32>, int32 > > events;	// ((position, 0 ref / 1 table), what)
		for (size_t r = 0; r < ps[p].fNoteRefs.size(); ++r)
			events.push_back(std::make_pair(std::make_pair(ps[p].fNoteRefs[r].fAt, 0), ps[p].fNoteRefs[r].fNote));
		for (size_t k = 0; k < tables.size(); ++k)
			if (s.fTables[tables[k]].fParaIndex == static_cast<int32>(p))
				events.push_back(std::make_pair(std::make_pair(s.fTables[tables[k]].fOffset, 1), static_cast<int32>(tables[k])));
		std::stable_sort(events.begin(), events.end(),
						 [](const std::pair< std::pair<int32, int32>, int32 >& x, const std::pair< std::pair<int32, int32>, int32 >& y)
						 { return x.first < y.first; });
		for (size_t e = 0; e < events.size(); ++e)
		{
			if (events[e].first.second == 0)
			{
				order.push_back(events[e].second);
				continue;
			}
			const int32 t = events[e].second;
			const KCMStoryShape::Table& tb = s.fTables[static_cast<size_t>(t)];
			for (size_t r = 0; r < tb.fRows.size(); ++r)
				for (size_t c = 0; c < tb.fRows[r].fCells.size(); ++c)
					ReadPlace(s, tb.fRows[r].fCells[c].fParas, t, static_cast<int32>(r), static_cast<int32>(c), order);
		}
	}
}

void RemapRefs(Paras& ps, const std::vector<int32>& newOf)
{
	for (size_t p = 0; p < ps.size(); ++p)
	{
		std::vector<KCMStoryShape::NoteRef> keep;
		for (size_t r = 0; r < ps[p].fNoteRefs.size(); ++r)
		{
			const int32 old = ps[p].fNoteRefs[r].fNote;
			if (old < 0 || static_cast<size_t>(old) >= newOf.size() || newOf[static_cast<size_t>(old)] < 0)
				continue;
			KCMStoryShape::NoteRef x = ps[p].fNoteRefs[r];
			x.fNote = newOf[static_cast<size_t>(old)];
			keep.push_back(x);
		}
		ps[p].fNoteRefs = keep;
	}
}

void RemapAllRefs(KCMStoryShape::Story& s, const std::vector<int32>& newOf)
{
	RemapRefs(s.fBody, newOf);
	for (size_t t = 0; t < s.fTables.size(); ++t)
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				RemapRefs(s.fTables[t].fRows[r].fCells[c].fParas, newOf);
}

}	// anonymous namespace

bool16 Normalize(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word,
				 KCMStoryShape::Story& outNow, KCMStoryShape::Story& outWord, std::string& whyNot)
{
	whyNot.clear();
	if (now.fTables.size() != word.fTables.size())
	{
		whyNot = "the number of tables changed (" + Num(static_cast<int32>(now.fTables.size())) + " became "
				 + Num(static_cast<int32>(word.fTables.size())) + ")";
		return kFalse;
	}
	std::vector<KCMZipStore::Entry> parts;
	std::string why;
	if (!KCMStoryDocx::WriteParts(now, 1, parts, why))
	{
		whyNot = "the document's story cannot be written in Word's format: " + why;
		return kFalse;
	}
	KCMStoryDocx::ReadResult back;
	if (!KCMStoryDocx::Read(parts, back, why))
	{
		whyNot = "the document's story does not read back from Word's format: " + why;
		return kFalse;
	}
	outNow = back.fAfter;
	KCMStoryDocx::RejoinTables(outNow, now);
	outWord = word;
	KCMStoryDocx::RejoinTables(outWord, now);
	// ★**WHETHER A ROW IS A HEADER ROW DOES NOT TRAVEL** (2026-09-23, the user's rule: header and footer
	//   rows are rows like any other - only what happened to the cells is looked at). Word's flag is
	//   made the document's here, once, so that nothing downstream ever sees it differ (measured on
	//   the matrix: H39 took a header row's flag off in Word, and the plan's check failed on it).
	for (size_t t = 0; t < outWord.fTables.size() && t < outNow.fTables.size(); ++t)
		for (size_t r = 0; r < outWord.fTables[t].fRows.size() && r < outNow.fTables[t].fRows.size(); ++r)
			outWord.fTables[t].fRows[r].fHeader = outNow.fTables[t].fRows[r].fHeader;
	if (!SameLayout(outNow, now))
	{
		whyNot = "the document's story does not come back through Word's format paragraph for paragraph";
		return kFalse;
	}
	return kTrue;
}

void Compare(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word, Plan& out,
			 bool16 reshapeTables)
{
	out = Plan();
	KCMStoryShape::Story n;
	KCMStoryShape::Story w;
	std::string why;

	// ---- stage 0: tables added or taken away (S3a, design section 10) - before anything is lined up,
	//      since everything after pairs the tables k-th with k-th
	{
		std::vector<Step> tables;
		const int32 r = PlanTables(now, word, tables, why);
		if (r < 0)
		{
			out.fStoryHeld = kTrue;
			out.fWhy = why;
			return;
		}
		if (r > 0)
		{
			out.fSteps = tables;
			return;
		}
	}

	if (!Normalize(now, word, n, w, why))
	{
		out.fStoryHeld = kTrue;
		out.fWhy = why;
		return;
	}

	Run run;
	run.fNow = &now;
	run.fN = &n;
	run.fW = &w;
	run.fPlan = &out;
	run.fWordOfNote.assign(n.fNotes.size(), -1);
	run.fTableHeld.assign(n.fTables.size(), kFalse);

	// ---- the tables first: a table whose shape changed is made Word's, one stage at a time -------------
	// ★In document order, so a nested table's parent is judged before it: one inside a held table is
	//   held with it, and named once, with its parent.
	// ★★★A SHAPE ROUND IS ONE STAGE AND NOTHING ELSE (S1/S2, design sections 8-2 and 9-1): merges taken
	//   apart, then rows and columns at the end, then Word's merges - whichever comes first among all the
	//   tables. The words are compared once every shape is Word's, against the story read again, so every
	//   number in that plan is the document's own.
	// ⚠**A TABLE BEING RESHAPED IS NOT HELD** (re-check 2026-09-23): the tables nested in it are judged
	//  too; PlanShape holds the parent when one stands in a cell the stage would move or take away.
	std::vector<Step> byStage[4];
	for (size_t t = 0; t < n.fTables.size(); ++t)
	{
		const int32 parent = n.fTables[t].fInTable;
		if (parent >= 0 && static_cast<size_t>(parent) < run.fTableHeld.size() && run.fTableHeld[static_cast<size_t>(parent)])
		{
			run.fTableHeld[t] = kTrue;
			continue;
		}
		std::string tw;
		if (TableShapeSame(n.fTables[t], w.fTables[t], tw))
			continue;
		std::vector<Step> steps;
		std::string tableWhy;
		const int32 stage = PlanShape(n, w, t, steps, tableWhy);
		if (stage > 0 && reshapeTables)
		{
			byStage[stage].insert(byStage[stage].end(), steps.begin(), steps.end());
			continue;
		}
		// ★★WITHOUT reshapeTables A SHAPE THAT COULD BE MADE IS HELD TOO (2026-09-24, the user's decision -
		//   design 11-1 item 5): the import writes under Track Changes, which records neither rows,
		//   columns nor merges, so what it cannot record it does not do. The whole table is held, its
		//   words included - pairing the words of two tables that do not have the same cells is exactly
		//   what the shape rounds were there to avoid. The rounds stay, working, for reshapeTables.
		const std::string reason = (stage > 0)
			? std::string("its rows, columns or merged cells were changed in Word - InDesign's change history cannot record that")
			: (tableWhy.empty() ? tw : tableWhy);
		run.fTableHeld[t] = kTrue;
		Hold(run, Where::Cell(static_cast<int32>(t), -1, -1), -1, "Table",
			 "table " + Num(static_cast<int32>(t)) + ": " + reason + " - that table was left as it is");
	}
	for (int32 stage = 1; stage <= 3; ++stage)
	{
		if (!byStage[stage].empty())
		{
			out.fSteps = byStage[stage];
			return;
		}
	}

	// ---- the places: the body, every cell of a table not held, then the notes that pair ---------
	ComparePlace(run, Where::Body(), Where::Body());
	for (size_t t = 0; t < n.fTables.size(); ++t)
	{
		if (run.fTableHeld[t])
			continue;
		for (size_t r = 0; r < n.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < n.fTables[t].fRows[r].fCells.size(); ++c)
			{
				const Where cell = Where::Cell(static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c));
				ComparePlace(run, cell, cell);
			}
	}
	for (size_t k = 0; k < n.fNotes.size(); ++k)
	{
		const int32 wn = run.fWordOfNote[k];
		if (wn >= 0)
			ComparePlace(run, Where::Note(static_cast<int32>(k)), Where::Note(wn));
	}
}

KCMStoryShape::Story ApplyToShape(const KCMStoryShape::Story& normalizedNow, const Plan& plan)
{
	KCMStoryShape::Story out = normalizedNow;
	if (plan.fStoryHeld)
		return out;

	// the paragraph steps, place by place (the notes' own places included, by N's numbers)
	std::vector<Where> places;
	AllPlaces(normalizedNow, places);
	for (size_t i = 0; i < places.size(); ++i)
		ApplyPlace(out, places[i], plan);

	// the notes added: a reference in the finished paragraph, the words at the end of the list
	std::vector<bool16> noteGone(out.fNotes.size(), kFalse);
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const Step& s = plan.fSteps[i];
		if (s.fKind == Step::kDeleteNote && s.fNote >= 0 && static_cast<size_t>(s.fNote) < noteGone.size())
			noteGone[static_cast<size_t>(s.fNote)] = kTrue;
		if (s.fKind != Step::kAddNote)
			continue;
		Paras* ps = ParasAt(out, s.fWhere);
		if (ps == nil || s.fPara < 0 || static_cast<size_t>(s.fPara) >= ps->size())
			continue;
		KCMStoryShape::Para& p = (*ps)[static_cast<size_t>(s.fPara)];
		KCMStoryShape::NoteRef r;
		r.fAt = s.fAt;
		r.fNote = static_cast<int32>(out.fNotes.size());
		size_t at = 0;
		while (at < p.fNoteRefs.size() && p.fNoteRefs[at].fAt <= r.fAt)
			++at;
		p.fNoteRefs.insert(p.fNoteRefs.begin() + static_cast<std::ptrdiff_t>(at), r);
		out.fNotes.push_back(s.fParas);
		noteGone.push_back(kFalse);
	}

	// the notes taken away: their references and their words go, and the rest close up
	std::vector<int32> newOf(out.fNotes.size(), -1);
	std::vector<Paras> notes;
	for (size_t k = 0; k < out.fNotes.size(); ++k)
	{
		if (noteGone[k])
			continue;
		newOf[k] = static_cast<int32>(notes.size());
		notes.push_back(out.fNotes[k]);
	}
	out.fNotes = notes;
	RemapAllRefs(out, newOf);
	return out;
}

namespace
{

/** A table on paper as a grid of owners: which cell (an index into fCells) holds each place. */
struct PaperGrid
{
	std::vector< std::vector<int32> >	fOwner;		// [row][col] -> cell id
	std::vector<KCMStoryShape::Cell>	fCells;
	std::vector< std::pair<int32, int32> >	fAt;	// cell id -> its top-left (row, col)
	std::vector<bool16>					fGone;		// cell id -> taken away (a row or column that went)
	std::vector<bool16>					fHeader;	// row -> Row::fHeader
	int32								fCols;

	PaperGrid() : fCols(0) {}

	int32 NewCell(int32 row, int32 col)
	{
		KCMStoryShape::Cell c;
		c.fParas.push_back(KCMStoryShape::Para());
		fCells.push_back(c);
		fAt.push_back(std::make_pair(row, col));
		fGone.push_back(kFalse);
		return static_cast<int32>(fCells.size()) - 1;
	}
};

/** `t` onto a grid. `idOf[row][index]` is the cell id of a row's index-th cell - for the nested tables. */
void ToPaper(const KCMStoryShape::Table& t, PaperGrid& g, std::vector< std::vector<int32> >& idOf)
{
	std::vector<GridCell> cells;
	LayOut(t, cells, g.fCols);
	g.fOwner.assign(t.fRows.size(), std::vector<int32>(static_cast<size_t>(g.fCols), -1));
	idOf.assign(t.fRows.size(), std::vector<int32>());
	for (size_t r = 0; r < t.fRows.size(); ++r)
	{
		g.fHeader.push_back(t.fRows[r].fHeader);
		idOf[r].assign(t.fRows[r].fCells.size(), -1);
	}
	for (size_t i = 0; i < cells.size(); ++i)
	{
		const GridCell& c = cells[i];
		const int32 id = static_cast<int32>(g.fCells.size());
		g.fCells.push_back(t.fRows[static_cast<size_t>(c.fRow)].fCells[static_cast<size_t>(c.fIndex)]);
		g.fAt.push_back(std::make_pair(c.fRow, c.fCol));
		g.fGone.push_back(kFalse);
		idOf[static_cast<size_t>(c.fRow)][static_cast<size_t>(c.fIndex)] = id;
		for (int32 rr = c.fRow; rr < c.fRow + c.fRowSpan && static_cast<size_t>(rr) < g.fOwner.size(); ++rr)
			for (int32 cc = c.fCol; cc < c.fCol + c.fColSpan && cc < g.fCols; ++cc)
				g.fOwner[static_cast<size_t>(rr)][static_cast<size_t>(cc)] = id;
	}
}

/** The grid back into rows of top-left cells. `rowOf` / `indexOf`: cell id -> where it is listed now. */
void FromPaper(const PaperGrid& g, KCMStoryShape::Table& t, std::vector<int32>& rowOf, std::vector<int32>& indexOf)
{
	t.fRows.clear();
	rowOf.assign(g.fCells.size(), -1);
	indexOf.assign(g.fCells.size(), -1);
	for (size_t r = 0; r < g.fOwner.size(); ++r)
	{
		KCMStoryShape::Row row;
		row.fHeader = (r < g.fHeader.size()) ? g.fHeader[r] : kFalse;
		for (size_t c = 0; c < g.fOwner[r].size(); ++c)
		{
			const int32 id = g.fOwner[r][c];
			if (id < 0 || g.fGone[static_cast<size_t>(id)] || g.fAt[static_cast<size_t>(id)] != std::make_pair(static_cast<int32>(r), static_cast<int32>(c)))
				continue;
			rowOf[static_cast<size_t>(id)] = static_cast<int32>(r);
			indexOf[static_cast<size_t>(id)] = static_cast<int32>(row.fCells.size());
			row.fCells.push_back(g.fCells[static_cast<size_t>(id)]);
		}
		t.fRows.push_back(row);
	}
}

/** Every footnote a cell's paragraphs refer to, marked gone. */
void NotesOf(const KCMStoryShape::Cell& c, std::vector<bool16>& noteGone)
{
	for (size_t p = 0; p < c.fParas.size(); ++p)
		for (size_t k = 0; k < c.fParas[p].fNoteRefs.size(); ++k)
		{
			const int32 note = c.fParas[p].fNoteRefs[k].fNote;
			if (note >= 0 && static_cast<size_t>(note) < noteGone.size())
				noteGone[static_cast<size_t>(note)] = kTrue;
		}
}

/** One shape step on paper, the way InDesign does it (spike M3-M5, M7). */
void PaperStep(PaperGrid& g, const Step& s, std::vector<bool16>& noteGone)
{
	const int32 rows = static_cast<int32>(g.fOwner.size());
	if (s.fKind == Step::kUnmerge)
	{
		if (s.fGridRow < 0 || s.fGridRow >= rows || s.fGridCol < 0 || s.fGridCol >= g.fCols)
			return;
		const int32 id = g.fOwner[static_cast<size_t>(s.fGridRow)][static_cast<size_t>(s.fGridCol)];
		if (id < 0)
			return;
		KCMStoryShape::Cell& a = g.fCells[static_cast<size_t>(id)];
		const int32 rs = a.fRowSpan > 1 ? a.fRowSpan : 1;
		const int32 cs = a.fColSpan > 1 ? a.fColSpan : 1;
		a.fRowSpan = 1;
		a.fColSpan = 1;
		for (int32 rr = s.fGridRow; rr < s.fGridRow + rs && rr < rows; ++rr)
			for (int32 cc = s.fGridCol; cc < s.fGridCol + cs && cc < g.fCols; ++cc)
				if (!(rr == s.fGridRow && cc == s.fGridCol))
					g.fOwner[static_cast<size_t>(rr)][static_cast<size_t>(cc)] = g.NewCell(rr, cc);
	}
	else if (s.fKind == Step::kMerge)
	{
		if (s.fGridRow < 0 || s.fGridRow >= rows || s.fGridCol < 0 || s.fGridCol >= g.fCols)
			return;
		const int32 id = g.fOwner[static_cast<size_t>(s.fGridRow)][static_cast<size_t>(s.fGridCol)];
		if (id < 0)
			return;
		// the covered cells' words run on in the top-left one, row by row (spike M4)
		for (int32 rr = s.fGridRow; rr < s.fGridRow + s.fGridRowSpan && rr < rows; ++rr)
			for (int32 cc = s.fGridCol; cc < s.fGridCol + s.fGridColSpan && cc < g.fCols; ++cc)
			{
				const int32 other = g.fOwner[static_cast<size_t>(rr)][static_cast<size_t>(cc)];
				if (other >= 0 && other != id && !g.fGone[static_cast<size_t>(other)])
				{
					KCMStoryShape::Cell& a = g.fCells[static_cast<size_t>(id)];
					const KCMStoryShape::Cell& b = g.fCells[static_cast<size_t>(other)];
					a.fParas.insert(a.fParas.end(), b.fParas.begin(), b.fParas.end());
					g.fGone[static_cast<size_t>(other)] = kTrue;
				}
				g.fOwner[static_cast<size_t>(rr)][static_cast<size_t>(cc)] = id;
			}
		g.fCells[static_cast<size_t>(id)].fRowSpan = s.fGridRowSpan;
		g.fCells[static_cast<size_t>(id)].fColSpan = s.fGridColSpan;
	}
	else if (s.fKind == Step::kResizeRows && s.fCount > 0)
	{
		if (s.fCount < rows)
		{
			// the bottom rows go, and the notes referred to from them (InDesign takes them silently - M2b)
			for (int32 r = s.fCount; r < rows; ++r)
				for (int32 c = 0; c < g.fCols; ++c)
				{
					const int32 id = g.fOwner[static_cast<size_t>(r)][static_cast<size_t>(c)];
					if (id >= 0 && !g.fGone[static_cast<size_t>(id)] && g.fAt[static_cast<size_t>(id)].first >= s.fCount)
					{
						NotesOf(g.fCells[static_cast<size_t>(id)], noteGone);
						g.fGone[static_cast<size_t>(id)] = kTrue;
					}
				}
			g.fOwner.resize(static_cast<size_t>(s.fCount));
			g.fHeader.resize(static_cast<size_t>(s.fCount));
		}
		else
		{
			// a row added after the last one: one plain cell per column, each one empty paragraph
			for (int32 r = rows; r < s.fCount; ++r)
			{
				g.fOwner.push_back(std::vector<int32>(static_cast<size_t>(g.fCols), -1));
				g.fHeader.push_back(kFalse);
				for (int32 c = 0; c < g.fCols; ++c)
					g.fOwner[static_cast<size_t>(r)][static_cast<size_t>(c)] = g.NewCell(r, c);
			}
		}
	}
	else if (s.fKind == Step::kResizeCols && s.fCount > 0)
	{
		if (s.fCount < g.fCols)
		{
			for (int32 r = 0; r < rows; ++r)
			{
				for (int32 c = s.fCount; c < g.fCols; ++c)
				{
					const int32 id = g.fOwner[static_cast<size_t>(r)][static_cast<size_t>(c)];
					if (id >= 0 && !g.fGone[static_cast<size_t>(id)] && g.fAt[static_cast<size_t>(id)].second >= s.fCount)
					{
						NotesOf(g.fCells[static_cast<size_t>(id)], noteGone);
						g.fGone[static_cast<size_t>(id)] = kTrue;
					}
				}
				g.fOwner[static_cast<size_t>(r)].resize(static_cast<size_t>(s.fCount));
			}
		}
		else
		{
			for (int32 r = 0; r < rows; ++r)
				for (int32 c = g.fCols; c < s.fCount; ++c)
					g.fOwner[static_cast<size_t>(r)].push_back(g.NewCell(r, c));
		}
		g.fCols = s.fCount;
	}
}

}	// anonymous namespace

KCMStoryShape::Story ReshapeOnPaper(const KCMStoryShape::Story& now, const Plan& plan)
{
	KCMStoryShape::Story out = now;
	std::vector<bool16> noteGone(out.fNotes.size(), kFalse);
	bool16 any = kFalse;

	// ---- stage 0 (S3a): tables taken away - with what they hold - and tables put in -------------------
	{
		std::vector<bool16> tableGone(out.fTables.size(), kFalse);
		bool16 anyGone = kFalse;
		for (size_t i = 0; i < plan.fSteps.size(); ++i)
			if (plan.fSteps[i].fKind == Step::kDeleteTable && plan.fSteps[i].fWhere.fTable >= 0
				&& static_cast<size_t>(plan.fSteps[i].fWhere.fTable) < tableGone.size())
			{
				tableGone[static_cast<size_t>(plan.fSteps[i].fWhere.fTable)] = kTrue;
				anyGone = kTrue;
			}
		// the tables nested in one that goes go too, however deep
		for (bool16 grew = anyGone; grew; )
		{
			grew = kFalse;
			for (size_t k = 0; k < out.fTables.size(); ++k)
			{
				const int32 parent = out.fTables[k].fInTable;
				if (!tableGone[k] && parent >= 0 && tableGone[static_cast<size_t>(parent)])
				{
					tableGone[k] = kTrue;
					grew = kTrue;
				}
			}
		}
		if (anyGone)
		{
			any = kTrue;
			std::vector<int32> newIndex(out.fTables.size(), -1);
			std::vector<KCMStoryShape::Table> kept;
			for (size_t k = 0; k < out.fTables.size(); ++k)
			{
				if (tableGone[k])
				{
					const KCMStoryShape::Table& t = out.fTables[k];
					for (size_t r = 0; r < t.fRows.size(); ++r)
						for (size_t c = 0; c < t.fRows[r].fCells.size(); ++c)
							NotesOf(t.fRows[r].fCells[c], noteGone);
					continue;
				}
				newIndex[k] = static_cast<int32>(kept.size());
				kept.push_back(out.fTables[k]);
			}
			for (size_t k = 0; k < kept.size(); ++k)
			{
				kept[k].fOrdinal = static_cast<int32>(k);
				if (kept[k].fInTable >= 0)
					kept[k].fInTable = newIndex[static_cast<size_t>(kept[k].fInTable)];
			}
			out.fTables = kept;
		}
		std::vector<int32> madeAt;		// where each table added so far made its paragraph, in the document's count
		for (size_t i = 0; i < plan.fSteps.size(); ++i)
		{
			const Step& s = plan.fSteps[i];
			if (s.fKind != Step::kInsertTable || s.fCount <= 0 || s.fAt <= 0)
				continue;
			any = kTrue;
			KCMStoryShape::Table t;
			t.fInTable = -1;
			{
				// ★A PARAGRAPH OF ITS OWN (2026-09-24, S3b design 12-1 item 2 - the user's rule): a new empty paragraph
				//   after the one it follows (or opening the body), the table at its offset 0 - what Word shows. Until
				//   that day it went at the END of that paragraph, making "A[T]" one paragraph.
				//   ⚠s.fPara counts the document's paragraphs BEFORE any table went in, so every paragraph an earlier
				//    step made at or before this place moves it on - which also keeps two tables added after the same
				//    paragraph in Word's order (the plan lists them so).
				const int32 orig = (s.fPara >= 0 && static_cast<size_t>(s.fPara) < now.fBody.size()) ? s.fPara + 1 : 0;
				int32 shift = 0;
				for (size_t m = 0; m < madeAt.size(); ++m)
					if (madeAt[m] <= orig)
						++shift;
				madeAt.push_back(orig);
				const int32 newPara = orig + shift;
				out.fBody.insert(out.fBody.begin() + newPara, KCMStoryShape::Para());
				for (size_t k = 0; k < out.fTables.size(); ++k)
					if (out.fTables[k].fInTable < 0 && out.fTables[k].fParaIndex >= newPara)
						++out.fTables[k].fParaIndex;
				t.fParaIndex = newPara;
				t.fOffset = 0;
			}
			for (int32 r = 0; r < s.fCount; ++r)
			{
				KCMStoryShape::Row row;
				for (int32 c = 0; c < s.fAt; ++c)
				{
					KCMStoryShape::Cell cell;
					cell.fParas.push_back(KCMStoryShape::Para());
					row.fCells.push_back(cell);
				}
				t.fRows.push_back(row);
			}
			// in document order among the body's tables: before the first that stands after it
			size_t at = out.fTables.size();
			for (size_t k = 0; k < out.fTables.size(); ++k)
			{
				const KCMStoryShape::Table& x = out.fTables[k];
				if (x.fInTable < 0 && (x.fParaIndex > t.fParaIndex || (x.fParaIndex == t.fParaIndex && x.fOffset > t.fOffset)))
				{
					at = k;
					break;
				}
			}
			out.fTables.insert(out.fTables.begin() + static_cast<std::ptrdiff_t>(at), t);
			for (size_t k = 0; k < out.fTables.size(); ++k)
			{
				out.fTables[k].fOrdinal = static_cast<int32>(k);
				if (k != at && out.fTables[k].fInTable >= static_cast<int32>(at))
					++out.fTables[k].fInTable;
			}
		}
	}

	for (size_t t = 0; t < out.fTables.size(); ++t)
	{
		std::vector<const Step*> mine;
		for (size_t i = 0; i < plan.fSteps.size(); ++i)
			if (plan.fSteps[i].IsShape() && plan.fSteps[i].fWhere.fTable == static_cast<int32>(t))
				mine.push_back(&plan.fSteps[i]);
		if (mine.empty())
			continue;
		any = kTrue;
		PaperGrid g;
		std::vector< std::vector<int32> > idOf;
		ToPaper(out.fTables[t], g, idOf);
		// the tables standing in this one's cells, by cell id - their cell may move in the row
		std::vector< std::pair<size_t, int32> > nested;
		for (size_t k = 0; k < out.fTables.size(); ++k)
		{
			const KCMStoryShape::Table& x = out.fTables[k];
			if (x.fInTable == static_cast<int32>(t) && x.fInRow >= 0 && static_cast<size_t>(x.fInRow) < idOf.size()
				&& x.fInCell >= 0 && static_cast<size_t>(x.fInCell) < idOf[static_cast<size_t>(x.fInRow)].size())
				nested.push_back(std::make_pair(k, idOf[static_cast<size_t>(x.fInRow)][static_cast<size_t>(x.fInCell)]));
		}
		for (size_t i = 0; i < mine.size(); ++i)
			PaperStep(g, *mine[i], noteGone);
		std::vector<int32> rowOf;
		std::vector<int32> indexOf;
		FromPaper(g, out.fTables[t], rowOf, indexOf);
		for (size_t k = 0; k < nested.size(); ++k)
		{
			const int32 id = nested[k].second;
			if (id >= 0 && static_cast<size_t>(id) < rowOf.size() && rowOf[static_cast<size_t>(id)] >= 0)
			{
				out.fTables[nested[k].first].fInRow = rowOf[static_cast<size_t>(id)];
				out.fTables[nested[k].first].fInCell = indexOf[static_cast<size_t>(id)];
			}
		}
	}
	if (!any)
		return out;

	std::vector<int32> newOf(out.fNotes.size(), -1);
	std::vector<Paras> notes;
	for (size_t k = 0; k < out.fNotes.size(); ++k)
	{
		if (noteGone[k])
			continue;
		newOf[k] = static_cast<int32>(notes.size());
		notes.push_back(out.fNotes[k]);
	}
	out.fNotes = notes;
	RemapAllRefs(out, newOf);
	// ★paragraphs moved between cells (a merge) can change which note the story's threads meet first
	RenumberNotesByThread(out);
	return out;
}

bool16 SameTableLayout(const KCMStoryShape::Story& a, const KCMStoryShape::Story& b, std::string& why)
{
	why.clear();
	if (a.fTables.size() != b.fTables.size())
	{
		why = Num(static_cast<int32>(a.fTables.size())) + " table(s) against " + Num(static_cast<int32>(b.fTables.size()));
		return kFalse;
	}
	for (size_t t = 0; t < a.fTables.size(); ++t)
	{
		std::string tw;
		if (!TableShapeSame(a.fTables[t], b.fTables[t], tw))
		{
			why = "table " + Num(static_cast<int32>(t)) + ": " + tw;
			return kFalse;
		}
	}
	return kTrue;
}

bool16 SameTablePlaces(const KCMStoryShape::Story& a, const KCMStoryShape::Story& b, std::string& why)
{
	why.clear();
	if (a.fBody.size() != b.fBody.size())
	{
		why = "the body has " + Num(static_cast<int32>(b.fBody.size())) + " paragraph(s), not "
			  + Num(static_cast<int32>(a.fBody.size()));
		return kFalse;
	}
	if (a.fTables.size() != b.fTables.size())
	{
		why = Num(static_cast<int32>(b.fTables.size())) + " table(s), not " + Num(static_cast<int32>(a.fTables.size()));
		return kFalse;
	}
	for (size_t t = 0; t < a.fTables.size(); ++t)
	{
		const KCMStoryShape::Table& x = a.fTables[t];
		const KCMStoryShape::Table& y = b.fTables[t];
		if (x.fInTable != y.fInTable || x.fInRow != y.fInRow || x.fInCell != y.fInCell
			|| x.fParaIndex != y.fParaIndex || x.fOffset != y.fOffset)
		{
			why = "table " + Num(static_cast<int32>(t)) + " stands in paragraph " + Num(y.fParaIndex) + " at "
				  + Num(y.fOffset) + ", not in paragraph " + Num(x.fParaIndex) + " at " + Num(x.fOffset);
			return kFalse;
		}
		for (size_t r = 0; r < x.fRows.size() && r < y.fRows.size(); ++r)
		{
			for (size_t c = 0; c < x.fRows[r].fCells.size() && c < y.fRows[r].fCells.size(); ++c)
			{
				if (x.fRows[r].fCells[c].fParas.size() != y.fRows[r].fCells[c].fParas.size())
				{
					why = "a cell of table " + Num(static_cast<int32>(t)) + " holds "
						  + Num(static_cast<int32>(y.fRows[r].fCells[c].fParas.size())) + " paragraph(s), not "
						  + Num(static_cast<int32>(x.fRows[r].fCells[c].fParas.size()));
					return kFalse;
				}
			}
		}
	}
	return kTrue;
}

void RenumberNotesByReading(KCMStoryShape::Story& s)
{
	std::vector<int32> order;
	ReadPlace(s, s.fBody, -1, 0, 0, order);
	std::vector<int32> newOf(s.fNotes.size(), -1);
	std::vector<Paras> notes;
	for (size_t i = 0; i < order.size(); ++i)
	{
		const int32 o = order[i];
		if (o < 0 || static_cast<size_t>(o) >= s.fNotes.size() || newOf[static_cast<size_t>(o)] >= 0)
			continue;
		newOf[static_cast<size_t>(o)] = static_cast<int32>(notes.size());
		notes.push_back(s.fNotes[static_cast<size_t>(o)]);
	}
	for (size_t k = 0; k < s.fNotes.size(); ++k)		// a note nobody refers to keeps a place at the end
	{
		if (newOf[k] >= 0)
			continue;
		newOf[k] = static_cast<int32>(notes.size());
		notes.push_back(s.fNotes[k]);
	}
	s.fNotes = notes;
	RemapAllRefs(s, newOf);
}

void RenumberNotesByThread(KCMStoryShape::Story& s)
{
	std::vector<int32> order;
	std::vector<const Paras*> places;
	places.push_back(&s.fBody);
	for (size_t t = 0; t < s.fTables.size(); ++t)
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				places.push_back(&s.fTables[t].fRows[r].fCells[c].fParas);
	for (size_t p = 0; p < places.size(); ++p)
		for (size_t i = 0; i < places[p]->size(); ++i)
			for (size_t r = 0; r < (*places[p])[i].fNoteRefs.size(); ++r)
				order.push_back((*places[p])[i].fNoteRefs[r].fNote);

	std::vector<int32> newOf(s.fNotes.size(), -1);
	std::vector<Paras> notes;
	for (size_t i = 0; i < order.size(); ++i)
	{
		const int32 o = order[i];
		if (o < 0 || static_cast<size_t>(o) >= s.fNotes.size() || newOf[static_cast<size_t>(o)] >= 0)
			continue;
		newOf[static_cast<size_t>(o)] = static_cast<int32>(notes.size());
		notes.push_back(s.fNotes[static_cast<size_t>(o)]);
	}
	for (size_t k = 0; k < s.fNotes.size(); ++k)		// a note nobody refers to keeps a place at the end
	{
		if (newOf[k] >= 0)
			continue;
		newOf[k] = static_cast<int32>(notes.size());
		notes.push_back(s.fNotes[k]);
	}
	s.fNotes = notes;
	RemapAllRefs(s, newOf);
}

}	// namespace KCMStorySync

// End, KCMStorySync.cpp.
