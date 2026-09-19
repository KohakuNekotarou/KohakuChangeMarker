//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - "Restore Source Text" and "Undo the Restore" on a TABLE row.
//
//  A Table row (KCMStoryChange::kTable, fKind == kReplace) is a table whose shape - rows, columns,
//  merged cells - differs from Task Start's. Words cannot put a table back; the whole table is
//  replaced, and the reader's own edits to cells that exist on both sides are written back into
//  the table that arrives (the user, 2026-09-19 night: "be clever about the cells that were not
//  added or removed but whose words changed").
//
//  THE ROAD (docs/superpowers/specs/2026-09-19-kcm-table-row-design.md §3-3, rebuilt 2026-09-20):
//     1. the live table's XML is exported from the story AS IT STANDS and kept as a snippet (redo)
//     2. Task Start's table is cut out of the origin's bytes, and the live cells' CONTENTS are
//        merged into it as text (KCMMergeTableCells) - shape from Task Start, contents kept
//     3. a scratch document imports the result (KCMScratchDoc) - NEVER the Target
//     4. kCopyStoryRangeCmdBoss copies the table over the live one's anchor range
//     5. the scratch document is closed; two re-checks say it is gone and the Target gained nothing
//  "Undo the Restore" runs 3-4-5 with the kept snippet, and drops it.
//
//  ★★★**NOTHING IS WRITTEN INTO CELLS AFTERWARDS** (2026-09-20, the user's design: "the snippet is
//   text… the cells that changed can be told from the difference"). The earlier road replaced the
//   table and then wrote each kept cell's words back through the model, which meant naming cells by
//   position - and a cell that had been EMPTIED has no width, so the position named the NEXT cell and
//   a new row's words went into an old cell. In XML every <Cell> carries Name="col:row". The merge is
//   exact, it carries the cell's FORMATTING as well as its words, and a cell holding a nested table or
//   an anchored object travels too (the words road had to refuse those).
//
//  ★**THE UNDO STEP IS 4 ALONE - NOT 3 AND NOT 5.** Making and closing a document inside the command
//   sequence would put them into what Ctrl+Z takes back; the step the reader asked for is "the table
//   went back". So the scratch document is opened before the sequence begins and closed after it ends.
//
//  ⚠Table + and Table − offer no menu (kKCMWriteBlockedPlaces): the user's call - "to remove a
//   table, select it and delete it". Task Start only, as every write here is.
//
//========================================================================================

#pragma once
#ifndef __KCMTableRestore_h__
#define __KCMTableRestore_h__

#include "BaseType.h"
#include "PMString.h"

struct KCMStoryChange;

/** Restore the table of `change` (merged index `which` of row `nth`) from Task Start. Called by
    RestoreOne once it has done the checks every restore shares (mode, index, write block).
    @param standalone kTrue for one press (its own undo step, its own re-diff); kFalse inside a bulk
           run, which then records outDone at outSlot itself.
    @return kTrue when the table was put back; outMessage says what happened. */
bool16 KCMRestoreTable(int32 nth, int32 which, const KCMStoryChange& change, bool16 standalone,
					   PMString& outMessage, KCMStoryChange* outDone, int32* outSlot);

/** Put the live table back from the snippet the restore kept. Called by KCMUndoRestoreChange once
    it has found the replaced record and seen it still standing. */
bool16 KCMUndoRestoreTable(int32 nth, int32 which, const KCMStoryChange& change, PMString& outMessage);

#endif // __KCMTableRestore_h__

// End, KCMTableRestore.h.
