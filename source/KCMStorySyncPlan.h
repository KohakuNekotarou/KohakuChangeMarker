//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What an import has to do to one story to make it Word's (2026-09-23, the user's rule: "Word is
//  the one that counts" - the story ends up exactly as Word shows it, and what was changed in
//  InDesign after the export is written over). KCMStorySync decides it; KCMStorySyncApply carries it
//  out. ★**THIS FILE IS THE CONTRACT BETWEEN THE TWO, AND NOTHING ELSE**: every decision is made
//  on the deciding side, and the side that writes asks nothing of its own - it does what a Step says,
//  or names where it could not. Design: docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md.
//
//========================================================================================

#ifndef __KCMStorySyncPlan_h__
#define __KCMStorySyncPlan_h__

#include "BaseType.h"
#include "KCMStoryShape.h"

#include <string>
#include <utility>
#include <vector>

namespace KCMStorySync
{

/** Where a run of paragraphs stands: the body, one table cell, or one footnote's own paragraphs.
	★**EVERY NUMBER IS THE DOCUMENT'S** (N's) - its table ordinals, its note ordinals - so the side
	  that writes can find the place by reading the document, with nothing to translate. */
struct Where
{
	enum Kind { kBody = 0, kCell = 1, kNote = 2 };

	int32	fKind;
	int32	fTable;		// kCell: the table's ordinal in N (Story::fTables)
	int32	fRow;		// kCell: its row (-1 in a Held step that names the whole table)
	int32	fCell;		// kCell: which cell of that row, in the order the row's cells run
	int32	fNote;		// kNote: the note's ordinal in N

	Where() : fKind(kBody), fTable(-1), fRow(0), fCell(0), fNote(-1) {}

	static Where Body() { return Where(); }
	static Where Cell(int32 t, int32 r, int32 c) { Where w; w.fKind = kCell; w.fTable = t; w.fRow = r; w.fCell = c; return w; }
	static Where Note(int32 n) { Where w; w.fKind = kNote; w.fNote = n; return w; }

	bool operator==(const Where& o) const
	{
		return fKind == o.fKind && fTable == o.fTable && fRow == o.fRow && fCell == o.fCell && fNote == o.fNote;
	}

	/** "the body", "table 0 row 1 cell 2", "note 3" - for a "!" row. */
	std::string Say() const;
};

/** One thing to do. What each field means depends on the kind - see the kind. */
struct Step
{
	enum Kind
	{
		/** Paragraph fPara of fWhere becomes fParas[0] (words, ruby, kenten, tate-chu-yoko, warichu).
			fParas[0].fNoteRefs holds only the references that STAY, numbered as N's notes; the notes
			added and taken away are Steps of their own. fTables: the tables standing in this
			paragraph (N's ordinal, the offset they stand at afterwards). */
		kSetPara = 0,
		/** fParas go in after paragraph fPara of fWhere (-1 = before the first). They carry no note
			reference: an added note is a kAddNote of its own. */
		kInsertParas = 1,
		/** fCount paragraphs from fPara of fWhere go. */
		kDeleteParas = 2,
		/** A footnote is made at code point fAt of paragraph fPara of fWhere, and holds fParas.
			★fPara counts the place's paragraphs AFTER every paragraph step of this plan has been
			  carried out - it is where the reference stands in the finished story. */
		kAddNote = 3,
		/** N's footnote fNote goes, its reference with it. */
		kDeleteNote = 4,
		/** Nothing is done here, and the reader is told: fWhat ("Story", "Table", "Para", "Tcy",
			"Place") and fWhy. fPara is -1 when the whole place (or table) is meant. */
		kHeld = 5,
		/** Table fWhere.fTable (N's ordinal; fRow and fCell -1) is made fCount rows long, AT ITS END: rows
			are added after the last one or taken away from the bottom, and the words go by position
			(the user's rule, design section 1-6). ★**ONLY IN A FIRST ROUND**: a plan that holds one holds
			nothing else, and the side that writes reads the story again and compares once more when it is
			done (design section 8-2) - a row taken away renumbers the notes and moves the cells, and the
			second comparison is what keeps every number the document's own. */
		kResizeRows = 6,
		/** Table fWhere.fTable is made fCount columns wide, at its right-hand end - kResizeRows' rule
			for columns (S2, design section 9). A shape step: see kResizeRows. */
		kResizeCols = 7,
		/** The merged cell whose top-left is grid (fGridRow, fGridCol) in table fWhere.fTable, reaching
			fGridRowSpan x fGridColSpan, is taken apart. Its words stay in the top-left cell (spike M5).
			The first of the three shape stages (design section 9-1). */
		kUnmerge = 8,
		/** The cells of grid [fGridRow, +fGridRowSpan) x [fGridCol, +fGridColSpan) in table
			fWhere.fTable, each a plain one by then, become one. Their words run on in it (spike M4) until
			the words round makes them Word's. The last of the three shape stages. */
		kMerge = 9,
		/** Body table fWhere.fTable (N's ordinal) goes, whatever it holds - its footnotes, its anchored
			objects and the tables nested in it (the user's rule, design section 10-1). Stage 0: a round of
			tables added and taken away comes before every other (design section 10-2). */
		kDeleteTable = 10,
		/** A body table of fCount rows by fAt columns, each cell one empty paragraph, goes in at the END of
			N's body paragraph fPara (before its return) - or at the head of the body when fPara is -1.
			fNote is Word's ordinal for it, which orders two tables put in at one place. Stage 0. */
		kInsertTable = 11
	};

	int32									fKind;
	Where									fWhere;
	int32									fPara;
	int32									fCount;
	int32									fAt;
	int32									fNote;
	std::vector<KCMStoryShape::Para>		fParas;
	std::vector< std::pair<int32, int32> >	fTables;	// kSetPara: (N's table ordinal, its offset afterwards)
	std::string								fWhat;
	std::string								fWhy;
	// kUnmerge / kMerge: the cell's place in the table's GRID (rows and columns as InDesign counts
	// them, a merged cell's covered places included) and how far it reaches (S2, 2026-09-23)
	int32									fGridRow;
	int32									fGridCol;
	int32									fGridRowSpan;
	int32									fGridColSpan;

	Step() : fKind(kSetPara), fPara(-1), fCount(0), fAt(0), fNote(-1),
			 fGridRow(0), fGridCol(0), fGridRowSpan(1), fGridColSpan(1) {}

	/** kTrue for the four steps that change a table's shape - a round of those alone (design 9-1). */
	bool16 IsShape() const
	{
		return (fKind == kResizeRows || fKind == kResizeCols || fKind == kUnmerge || fKind == kMerge
				|| fKind == kDeleteTable || fKind == kInsertTable) ? kTrue : kFalse;
	}
};

/** Everything for one story, in document order (the body, then the cells in table order, then the
	notes). The side that writes decides its own order - from the back - and must not need this one. */
struct Plan
{
	std::vector<Step>	fSteps;
	bool16				fStoryHeld;		// nothing of this story is touched; fWhy says why
	std::string			fWhy;

	Plan() : fStoryHeld(kFalse) {}

	int32 Count(int32 kind) const
	{
		int32 n = 0;
		for (size_t i = 0; i < fSteps.size(); ++i)
			if (fSteps[i].fKind == kind)
				++n;
		return n;
	}

	/** kTrue when this plan is a shape round (design 9-1): carry it out, read again, compare again. */
	bool16 IsShapeRound() const
	{
		for (size_t i = 0; i < fSteps.size(); ++i)
			if (fSteps[i].IsShape())
				return kTrue;
		return kFalse;
	}
};

}	// namespace KCMStorySync

#endif // __KCMStorySyncPlan_h__

// End, KCMStorySyncPlan.h.
