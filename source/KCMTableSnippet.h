//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - a TABLE as a page-item snippet built from XML.
//
//  ★WHY (2026-09-19 night → 2026-09-20, the user's decision): "Restore Source Text" on a Table row
//  puts the whole table back from Task Start, and "Undo the Restore" puts the live one back again.
//  A table cannot be written as text (its styles, widths and merges would be lost), and the user
//  did not want a hidden document kept open to hold one ("窓無しドキュメントを開いておくのは嫌") - and
//  then found the better road: "InCopy 形式の Story のスニペットからテキストで表の最少部分を持ってきて、
//  最少のページのスニペットを作ってしまう… それができたら非表示のドキュメントを作る必要がなくなる".
//
//  MEASURED 2026-09-19 (docs/ai-notes/kcm-table-snippet-xml-2026-09-19.md):
//   - a page-item snippet cut down to its PI header, <Document>, ONE <Spread> holding ONE <TextFrame>
//     and the <Story> holding the <Table> imports (page.place) and gives the table back, 8 KB;
//   - ⚠the table's and cell's STYLE GROUPS (RootCellStyleGroup, RootTableStyleGroup) have to be in
//     it, or the style references fall silently to "[No table style]"; with them the table lands on
//     the document's EXISTING style of that name (no copy is made);
//   - a story's own INX (ExportINX with the story as root) and the Task Start's internal IDML both
//     hold the <Story> in the same DOM, so the <Table> can be CUT out of either as text.
//
//  So: cut the <Table> (nested tables counted, in document order = KCMTextRead's ordinal) and the
//  two style groups out of an INX text, dress them in the template below, and the result is a
//  snippet a scratch document can import (KCMScratchDoc) - from which kCopyStoryRangeCmdBoss copies
//  the table over the live one (KCMTableRestore).
//
//========================================================================================

#pragma once
#ifndef __KCMTableSnippet_h__
#define __KCMTableSnippet_h__

#include "BaseType.h"
#include "OMTypes.h"		// UID

#include <string>

class IDataBase;
class KCMMemXferBytes;

/** Cut the `ordinal`-th <Table …>…</Table> (0-based, document order, nested tables counted) out of
    the <Story Self="u<hex>"> of an INX/IDML text. kFalse when the story or the table is not there. */
bool16 KCMCutTableXml(const char* xml, size_t size, UID storyUID, int32 ordinal, std::string& outTable);

/** The style groups a table refers to, cut out whole: <RootCellStyleGroup …>…</RootCellStyleGroup>
    and <RootTableStyleGroup …>…</RootTableStyleGroup>. Empty for a text that has neither. */
void KCMCutTableStyleGroups(const char* xml, size_t size, std::string& outGroups);

/** ★★★TASK START'S TABLE, WITH THE LIVE CELLS' CONTENTS KEPT (2026-09-20, the user's design: "the
    snippet is text, and both sides' snippets are in hand - the cells that changed can be told from
    the difference, and so can the ones that did not").

    The result is `olderTableXml` with, for every cell BOTH tables have at the same address whose
    contents differ, the contents taken from `liveTableXml`. So the SHAPE that comes back is Task
    Start's - its rows, columns, merges, and each cell's start tag with its style and spans - while
    what the reader wrote inside a cell that was neither added nor removed stays where it is.

    ★WHY THE ADDRESS IS EXACT HERE and was not before: every <Cell> in IDML carries Name="col:row",
    so an EMPTY cell is named as plainly as a full one. The earlier road asked the model which
    paragraph stood at a position, and an emptied cell has no width - it answered with the NEXT
    cell, and the restore wrote a new row's words into the old cell (measured 2026-09-20).
    ⚠Only the OUTERMOST cells are walked: a nested table's cells sit inside one of these and travel
     with it, contents and all - which is how a cell holding a table or an anchored object is kept
     at all (the words road could not carry one).
    ⚠What this cannot answer: a row or column added ABOVE shifts every address below it, so the same
     Name is a different cell on the two sides. Nothing in the two texts distinguishes that case.

    @param outKept how many cells kept their live contents.
    @return kFalse when Task Start's text could not be walked; `outMerged` is then empty. */
bool16 KCMMergeTableCells(const std::string& olderTableXml, const std::string& liveTableXml,
						  std::string& outMerged, int32& outKept);

/** The minimal page-item snippet: the PI header, <Document>, the style groups, one <Spread> with one
    <TextFrame>, and one <Story> whose only content is `tableXml`. UTF-8. */
void KCMBuildTableSnippet(const std::string& tableXml, const std::string& styleGroups, std::string& outSnippet);

/** The live story as INX text (IINXManager::ExportINX with the story as the root - the call
    KCMPdfSpike's S17.8 measured), into `out`. kFalse when the export failed. */
bool16 KCMExportStoryInx(IDataBase* db, UID storyUID, KCMMemXferBytes& out);

#endif // __KCMTableSnippet_h__

// End, KCMTableSnippet.h.
