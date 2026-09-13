//========================================================================================
//
//  KCMReportTable.h
//
//  The PDF report's page helpers and its TWO-COLUMN TABLE sections (2026-09-13, the user's ask:
//  "the Story and the Resources information too - the Source on the left, the Target on the
//  right; unchanged text pale, changed text dark; ruby if it can be done").
//
//  WHY A TABLE AND NOT TWO TEXT FRAMES. A report section is a list of PAIRS - the older side and
//  the newer side of one edit - and a pair has to stay on one line whatever the page break does.
//  Two threaded stories would drift apart the moment one side ran a line longer than the other.
//  A table row is exactly that pair, and InDesign breaks rows across pages itself, so the frames
//  only have to be threaded from page to page (kTextLinkCmdBoss) and the rows follow.
//
//  HOW A CELL IS WRITTEN. Three pieces of text - context, the change, context - are typed into
//  the cell's own story thread (ITableModel::QueryCellContentBoss -> ITextStoryThread ->
//  ITextModelCmds::InsertCmd), and then the attributes go on by range: the context at 40% tint,
//  the change at full strength, a reading as REAL ruby over the change (KCMApplyRuby, the same
//  recipe "Restore Source Text" writes into the user's document), a kenten kind as real kenten,
//  a note number as a superscript after the change.
//
//  THE PAGE HELPERS (page rectangle, typing into a new frame, appending a page) moved here from
//  KCMReport.cpp so that both the picture pages and the table sections use one set.
//
//  MODEL SIDE. Every function returns with no interface held on the report document.
//
//========================================================================================
#ifndef __KCMReportTable_h__
#define __KCMReportTable_h__

#include "BaseType.h"
#include "PMReal.h"
#include "PMRect.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

class IDataBase;
class SDKLayoutHelper;

// ---- the report's layout constants (points) --------------------------------------------------
const PMReal kKCMReportGutter     = 24.0;	// between the two pictures, and to the page edges
const PMReal kKCMReportHeaderBand = 56.0;	// the heading band above the pictures / a table
const PMReal kKCMReportCaptionH   = 26.0;	// room under the pictures (kept for the page height)
const PMReal kKCMReportHeadingPt  = 18.0;	// headings: 1.5 x the 12pt default (the user's ask, 2026-09-13)
const PMReal kKCMReportBodyPt     = 14.0;	// the table cells

/** One cell of a two-column table: context / the change / context, and what stands over or
    after the change. The report writes fPre and fPost at 40% and fMid at full strength. */
struct KCMReportCell
{
	PMString	fPre;
	PMString	fMid;
	PMString	fPost;
	PMString	fRuby;			// a reading set as REAL ruby over fMid; empty = none
	bool16		fRubyGroup;		// kTrue = group ruby, kFalse = mono (one reading per character)
	int16		fKentenKind;	// an IKentenStyle kind set as real kenten over fMid; -1 = none
	PMString	fNote;			// typed right after fMid as a SUPERSCRIPT (a footnote / endnote number); empty = none
	KCMReportCell() : fRubyGroup(kFalse), fKentenKind(-1) {}
};

/** One row, in the panel's own column order (2026-09-13, the user's ask: "the same heading as
    the panel - ID, Δ, Story - and show the ID and the Δ"): a LABEL column (the story's ID / a
    definition's kind / an attribute's name), the SIGN column (+ only in the newer, - only in
    the older, = compared and the same, ≠ differs), then the older side and the newer side.
    A heading row is written at full strength (no tint) - the story's or the definition's name
    in the older-side cell. */
struct KCMReportRow
{
	bool16			fHeading;
	PMString		fLabel;		// the first column: ID / Kind / an attribute's name
	PMString		fSign;		// the Δ column: one of KCMReportSign's strings
	KCMReportCell	fLeft;
	KCMReportCell	fRight;
	KCMReportRow() : fHeading(kFalse) {}
};

/** The four signs of the Δ column, spelled once. ⚠`≠` and `Δ` are in CP932, so they are set as
    UTF-16 rather than written as narrow literals (cpp-japanese-needs-bom - the dangerous half:
    a narrow "≠" builds and draws as something else). */
namespace KCMReportSign
{
	PMString Plus();		// "+"  only in the newer document
	PMString Minus();		// "-"  only in the older one
	PMString Equal();		// "="  compared, and the same
	PMString NotEqual();	// "≠"  differs
	PMString Delta();		// "Δ"  the column's heading
}

// ---- page helpers ------------------------------------------------------------------------------

/** How many pages the report document has (one page per spread, as it is made). */
int32	KCMReportPageCount(IDataBase* reportDB);

/** Append one spread of one page (the document's default size) at the end. */
bool16	KCMReportAddPage(IDataBase* reportDB);

/** The rectangle of the n-th page (0-based), in its spread's coordinates, and the spread's
    content layer. kFalse when the document has no such page. */
bool16	KCMReportPageAt(SDKLayoutHelper& helper, IDataBase* reportDB, int32 n, PMRect& outPageRect, UIDRef& outLayer);

/** Type `text` into a new text frame at `bounds` (spread coordinates) on `layer`, left-aligned,
    at `pointSize`. */
void	KCMReportTypeAt(SDKLayoutHelper& helper, const UIDRef& layer, const PMRect& bounds, const PMString& text, const PMReal& pointSize);

// ---- a table section ---------------------------------------------------------------------------

/** Lay `heading` and a four-column table of `rows` out from page `firstPage` (0-based; pages are
    appended as needed, for the first page as well as for every overflow). The table's first row
    is a HEADER ROW - `labelHeading` ("ID" / "Kind"), Δ, "Before", "After" - which InDesign
    repeats at the top of every page the table runs on to. Empty `rows` writes the heading and
    "(none)". outNextPage is the first page after the section. kFalse with a reason in `why`. */
bool16	KCMReportWriteTable(IDataBase* reportDB, int32 firstPage, const PMString& heading, const PMString& labelHeading,
							const std::vector<KCMReportRow>& rows, int32& outNextPage, PMString& why);

#endif // __KCMReportTable_h__

// End, KCMReportTable.h.
