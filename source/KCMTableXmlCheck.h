//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - "Match the Source", read back as XML (2026-09-25).
//
//  WHAT THIS IS FOR. The user's rule for Match the Source: "put the rows back from the Source, then check with
//  INX that nothing is wrong - and if anything is, nothing comes back". The words, the four marks and the kind of
//  every cell are checked by KCMTableReadsAsSource; everything ELSE a table carries - row heights, column widths,
//  cell styles, fills, strokes, insets, the footnotes, anchored frames, index entries, text variables, links,
//  conditions, notes and XML tags inside a cell - has no reader of its own in KCM. INX (IINXManager::ExportINX,
//  IDML's own XML) writes all of it, because IDML is the scripting DOM written out: this compares three cuts of
//  the <Table> element (KCMCutTableXmlById) instead.
//
//  WHAT IS COMPARED WITH WHAT:
//    - what the match TOUCHED is compared with the SOURCE's table: every cell that was not left alone (the cells
//      a row or column put back, or a merge changed), the rows and columns those cells stand in, and the table's
//      own attributes (the paste - ITableModel::eAll - writes the table's, the row's and the column's attributes
//      together with the cell's);
//    - what it did NOT touch is compared with the TARGET'S OWN TABLE BEFORE THE MATCH: the cells left alone (their
//      words are the change history's, not the match's), and the rows and columns holding only such cells.
//
//  WHAT IS NOT A DIFFERENCE, each measured on the application (live-rows X, 2026-09-25):
//    - `Self` - which object this is. A cell put back is a new object, with a new id;
//    - a value that is uid-shaped on BOTH sides (KCMLooksLikeUid) - a reference to an object by its id: an
//      anchored frame's ParentStory, an XML element's XMLContent. Both sides refer to an object that was made
//      anew, or to the same one; what it refers to is compared where that object itself stands;
//    - PageReference's `Id` - the index marker's serial number in the document (1 in the Source, 2 once the
//      marker was pasted back: the document had handed out 1 already).
//  Anything else that differs is a difference, and the first one is named.
//
//  PURE: std types only, so work/kcm-tablexml-test runs it with no application in the room.
//
//========================================================================================

#ifndef __KCMTableXmlCheck_h__
#define __KCMTableXmlCheck_h__

#include "BaseType.h"

#include <string>
#include <vector>

/** Whether the Target's table, after the match, reads as the match promised - see the file comment.
	@param after  the Target's <Table ...>...</Table> after the match
	@param source the Source's
	@param before the Target's, before anything moved
	@param keptCells the Names ("column:row", IDML's own) of the cells the match left alone
	@param outWhy the first difference, in words (UTF-8); empty when there is none
	@return kTrue when there is none. kFalse also when a text does not parse as one <Table> element. */
bool16 KCMTableXmlMatches(const std::string& after, const std::string& source, const std::string& before,
						  const std::vector<std::string>& keptCells, std::string& outWhy);

#endif // __KCMTableXmlCheck_h__

// End, KCMTableXmlCheck.h.
