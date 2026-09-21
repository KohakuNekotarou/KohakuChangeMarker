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
//  ★★★**TABLE + AND TABLE − COME BACK TOO** (2026-09-20, the user: "I want to be able to put them
//   back"; the earlier "to remove a table, select it and delete it" was withdrawn the same day).
//   Two more roads, each the other's mirror:
//     Table + (kInsert - the table is only in this version): the restore REMOVES it, by deleting its
//        anchor character; InDesign takes the table with the character (measured). Its undo brings
//        the table back from the snippet the restore kept, at a destination of no width.
//     Table − (kDelete - the table is only in Task Start): the restore INSERTS Task Start's table at
//        the caret where it stood, clamped to the story. Its undo removes it again.
//
//  ★★★**WHICH TABLE IS WHICH IS ASKED BY THE TABLE'S OWN ID** (2026-09-20, the user: "is it looking
//   at tables by position? a table has an id too"). Every lookup here is TableById, never an
//   ordinal: a table inserted before another one renumbers it, and a restore that went by number
//   would put one table's shape into another. The id is KCMTableShape::fDictUID on the model's side
//   and the last step of the Self on the XML's, and they are the same number (KCMTableSnippet.h).
//  ⚠**A TABLE THIS FILE WRITES GETS A NEW ID**, because it arrives through a snippet import - so
//   what it IS is recorded at the moment it lands (KCMStorySnapshotPutTableId) and the next
//   comparison reads it back. Without that line a table just put back would be called a table added
//   here, with Task Start's one still missing.
//
//  Task Start only, as every write here is.
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
    ⚠**The three bulk parameters went on 2026-09-20 and came back on 2026-09-21** with "Restore All
     in This Story" (the user asked for that one back; "Restore All Stories" did not come with it).
    @return kTrue when the table was put back; outMessage says what happened. */
bool16 KCMRestoreTable(int32 nth, int32 which, const KCMStoryChange& change, bool16 standalone,
					   PMString& outMessage, KCMStoryChange* outDone, int32* outSlot);

/** Put the live table back from the snippet the restore kept. Called by KCMUndoRestoreChange once
    it has found the replaced record and seen it still standing. */
bool16 KCMUndoRestoreTable(int32 nth, int32 which, const KCMStoryChange& change, PMString& outMessage);

#endif // __KCMTableRestore_h__

// End, KCMTableRestore.h.
