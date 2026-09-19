//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - the SHAPE of a table: rows, columns, merged cells - and
//  whether two shapes differ.
//
//  ★WHY THIS EXISTS (2026-09-19 night, the user's decision): a table whose shape differs from
//  Task Start's - a row or column added or removed, cells merged or unmerged - is shown as ONE
//  row of Story Edits ("Table") rather than as one row per cell paragraph, and "Restore Source
//  Text" on it puts the whole table back. "Same shape or not" is the question that decides
//  which of the two the reader sees, so it is answered in one place, over plain data.
//
//  ★THE COMPARISON NEEDS NO INDESIGN. KCMTableShapesDiffer, KCMTableShapeSignature and
//  KCMTableShapeWord are inline functions over plain fields, so that they can be built and
//  checked outside the application (work/kcm-tableshape-test, the shape of
//  work/kescm-rowfilter-test). Only KCMReadTableShapes, in the .cpp, talks to the model.
//
//  ⚠THE ORDER OF THE TABLES IS KCMTextRead's: by the start of each table's block of cells, which
//  is the order KCMParaAttrs::fTableOrdinal counts in - nested tables included. A shape's fOrdinal
//  is an index into that order on ONE side; pairing a table with the other side's is by ordinal
//  (the spec accepts that a table added in the MIDDLE shifts the ones after it).
//
//========================================================================================

#pragma once
#ifndef __KCMTableShape_h__
#define __KCMTableShape_h__

#include "BaseType.h"		// int32 / bool16 (the stub in work/kcm-tableshape-test outside InDesign)

#include <sstream>
#include <string>
#include <vector>

#ifdef KCM_TABLESHAPE_STANDALONE
// Outside InDesign the two SDK types below are not available; the compare never touches them.
typedef int32 TextIndex;
#else
#include "OMTypes.h"		// UID
#include "TextID.h"			// TextIndex
#endif

/** A merged cell: its anchor address and how far it reaches. Only cells whose span is not 1x1
    are listed - a plain grid has none. */
struct KCMTableCellShape
{
	int32	fRow;
	int32	fCol;
	int32	fRowSpan;
	int32	fColSpan;

	KCMTableCellShape() : fRow(0), fCol(0), fRowSpan(1), fColSpan(1) {}
};

/** Where one anchor cell's text stands in the model - what a mark, a jump or a write needs. */
struct KCMTableCellPlace
{
	int32		fRow;
	int32		fCol;
	TextIndex	fStart;		///< the cell thread's first character
	TextIndex	fEnd;		///< one past its last (the cell's final return included)

	KCMTableCellPlace() : fRow(0), fCol(0), fStart(0), fEnd(0) {}
};

/** One table, as far as the Table row and its restore need to know it. */
struct KCMTableShape
{
	int32	fOrdinal;		///< which table of the story, in KCMTextRead's order (0..)
	int32	fRows;
	int32	fCols;
	std::vector<KCMTableCellShape>	fMerges;	///< the merged cells, in (row, col) order

	// ---- facts read off the document, unused by the compare ----
#ifndef KCM_TABLESHAPE_STANDALONE
	UID		fDictUID;		///< the table's dictionary (= the table boss) in its database
#endif
	TextIndex	fAnchorStart;	///< the anchor character in the thread above
	TextIndex	fAnchorEnd;		///< one past the last continuation character (one per further row)
	std::vector<KCMTableCellPlace>	fCells;		///< every anchor cell, in (row, col) order

	KCMTableShape() : fOrdinal(-1), fRows(0), fCols(0), fAnchorStart(0), fAnchorEnd(0) {}
};

/** kTrue when the two shapes differ in rows, columns or merged cells. */
inline bool16 KCMTableShapesDiffer(const KCMTableShape& a, const KCMTableShape& b)
{
	if (a.fRows != b.fRows || a.fCols != b.fCols || a.fMerges.size() != b.fMerges.size())
		return kTrue;
	for (size_t i = 0; i < a.fMerges.size(); ++i)
	{
		const KCMTableCellShape& x = a.fMerges[i];
		const KCMTableCellShape& y = b.fMerges[i];
		if (x.fRow != y.fRow || x.fCol != y.fCol || x.fRowSpan != y.fRowSpan || x.fColSpan != y.fColSpan)
			return kTrue;
	}
	return kFalse;
}

/** The shape as one string - "2x2", or "2x2;m0,1:2x1" with the merges appended - the record a
    restore keeps so that "is the table still as I left it" is one comparison. */
inline std::string KCMTableShapeSignature(const KCMTableShape& s)
{
	std::ostringstream o;
	o << s.fRows << 'x' << s.fCols;
	for (size_t i = 0; i < s.fMerges.size(); ++i)
		o << ";m" << s.fMerges[i].fRow << ',' << s.fMerges[i].fCol
		  << ':' << s.fMerges[i].fRowSpan << 'x' << s.fMerges[i].fColSpan;
	return o.str();
}

/** What the Story column says about the change of shape, in UTF-8: "2×2→3×2" when the grid changed,
    "2×2 merged" when only the merged cells did. ⚠The two non-ASCII characters are written as UTF-8
    BYTES, never as literals (cpp-japanese-needs-bom: a literal that happens to be in CP932 builds
    and comes out wrong at run time). */
inline std::string KCMTableShapeWord(const KCMTableShape& before, const KCMTableShape& after)
{
	static const char kTimes[] = "\xC3\x97";			// U+00D7 MULTIPLICATION SIGN
	static const char kArrow[] = "\xE2\x86\x92";		// U+2192 RIGHTWARDS ARROW
	std::ostringstream o;
	o << before.fRows << kTimes << before.fCols;
	if (before.fRows != after.fRows || before.fCols != after.fCols)
		o << kArrow << after.fRows << kTimes << after.fCols;
	else
		o << " merged";
	return o.str();
}

/** The one-sided word for a table with no partner: "2×2" (UTF-8). */
inline std::string KCMTableShapeAlone(const KCMTableShape& s)
{
	static const char kTimes[] = "\xC3\x97";
	std::ostringstream o;
	o << s.fRows << kTimes << s.fCols;
	return o.str();
}

/** The cell at (row, col) among a shape's anchor cells, or nil. */
inline const KCMTableCellPlace* KCMTableCellAt(const KCMTableShape& s, int32 row, int32 col)
{
	for (size_t i = 0; i < s.fCells.size(); ++i)
		if (s.fCells[i].fRow == row && s.fCells[i].fCol == col)
			return &s.fCells[i];
	return nil;
}

#ifndef KCM_TABLESHAPE_STANDALONE
class ITextModel;

/** Every table of the story, in KCMTextRead's order (by the start of its cells' block), with its
    shape, its anchor range and every anchor cell's thread range. kFalse when a dictionary or a
    table could not be opened; a story with no tables answers kTrue with an empty list. */
bool16 KCMReadTableShapes(ITextModel* model, std::vector<KCMTableShape>& out);
#endif

#endif // __KCMTableShape_h__

// End, KCMTableShape.h.
