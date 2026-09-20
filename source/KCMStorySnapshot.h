//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - a CHANGED STORY as it stood when the comparison read it:
//  the story's own INX, with the document's style roots in it. One per story, kept for as
//  long as the comparison lasts.
//
//  ★WHY THIS SHAPE (2026-09-20, the user): the first try kept a SNIPPET PER CHANGED TABLE, and the
//  user saw further - "at the start of the comparison, hold an InCopy-format snippet for each story
//  that has changes and go on using that; refresh the story, refresh the snippet - that is easier
//  when we come to extend it". And the extension is named: "little by little I want to be able to
//  put back more - paragraph styles, character styles, variables". Every one of those is IN a
//  story's INX, or in the document-level roots the same export carries, so ONE text per story is
//  the floor they all stand on - where a snippet per table would have held the frame's dressing and
//  the style groups over again for every table in the same story.
//
//  ⚠**TAKEN BY THE DIFF, READ BY EVERYONE ELSE** - the two doors are separate on purpose. The whole
//   value of this text is that it is the story AS THE COMPARISON SAW IT; a reader that could take
//   one would silently take the story as the reader has since edited it, and nothing downstream
//   could tell the difference. So Take is called from KCMStoryDiffRun and Peek from everywhere else.
//
//  ⚠**IT IS NOT WHAT A RESTORE PUTS BACK.** The live story is exported AGAIN at the moment of a
//   restore, because a column width and a table style move no text counter - no re-diff runs, and
//   anything kept from earlier can be quietly out of date (KCMTableRestore, the A-2 of 2026-09-20).
//   This is the older side's dressing and the answers that must come from the comparison's moment.
//
//  LIFETIME, exactly as the user set it:
//   - a WHOLE comparison (KCMStoryDiffRun::Run) clears it: every story is being read again;
//   - REFRESHING ONE STORY drops that story's alone, and the fold takes it again;
//   - Stop, the origin being parked or released, the close sweep and the model's shutdown clear it -
//     the same three call sites KCMSourceCacheClear has.
//
//  (It replaces KCMTargetSnapshot, which held the Target's WHOLE internal IDML for the length of a
//   comparison and lasted one day: by the evening its only reader was the style groups.)
//
//========================================================================================

#pragma once
#ifndef __KCMStorySnapshot_h__
#define __KCMStorySnapshot_h__

#include "BaseType.h"
#include "OMTypes.h"		// UID

#include <map>
#include <string>

class IDataBase;

/** The story's INX, taken now if none is held. ⚠**CALLED FROM THE DIFF ONLY** - see the header.
    @return the text, or nil when the export failed. Valid until this story is dropped or cleared. */
const std::string* KCMStorySnapshotTake(IDataBase* db, UID story);

/** What is held for this story, taking nothing. nil when none is. */
const std::string* KCMStorySnapshotPeek(UID story);

/** ★"the cell whose id is now <key> WAS Task Start's cell <value>" - what a restore left behind, for
    one table (its ordinal in KCMTextRead's order).

    A snippet import REPACKS a table's cell ids (0,1,4,5 -> 0,1,2,3, measured 2026-09-20), so once a
    table has been put back it shares no id with Task Start's and the pairing would fall back to the
    words. The restore knows the answer at the moment it writes: it labels the cells of the snippet
    it builds, reads the labels back out of the SCRATCH document and clears them there, so nothing of
    ours is ever copied into the reader's document (the user's design). What it learned is kept here.
    @return nil when this table has not been put back during this comparison. */
const std::map<std::string, std::string>* KCMStorySnapshotGetCellIds(UID story, int32 ordinal);

/** Keep what a restore learned about one table's cells. Replaces whatever was kept for it. */
void KCMStorySnapshotPutCellIds(UID story, int32 ordinal, const std::map<std::string, std::string>& wasTaskStart);

/** Forget one story's INX - what REFRESHING THAT STORY does, so that the fold takes it again.
    ⚠★**WHAT A RESTORE LEARNED ABOUT ITS CELLS IS KEPT** (2026-09-20, found re-reading this before
     the live run). The two are not the same kind of fact: the INX is the story AS THE COMPARISON
     READ IT, and a refresh is a new reading - while "the cell standing here was Task Start's cell
     <id>" is about the cells THEMSELVES, which a re-diff does not touch. Dropping both would have
     wiped the map every time, because a restore ends by refreshing its own story. */
void KCMStorySnapshotDropStory(UID story);

/** Forget what a restore learned about ONE table's cells - what an Undo the Restore does, having
    just put a different table there. ⚠**The "it has been through an import" mark below is NOT
    dropped with it**: that is the whole point of the pair. */
void KCMStorySnapshotDropCellIds(UID story, int32 ordinal);

/** ★★★**THIS TABLE HAS BEEN WRITTEN BY AN IMPORT DURING THIS COMPARISON** - so the ids its cells
    carry now were handed out by that import and mean NOTHING to Task Start (measured 2026-09-20: a
    snippet import repacks them, 0,1,4,5 -> 0,1,2,3).

    ⚠★★★**WHY A SEPARATE MARK FROM THE TRANSLATION ABOVE** (found on the running application,
     2026-09-20 evening). "Restore -> Undo the Restore -> Restore" put the THIRD ROW's cells into the
     second row and lost what the reader had written, while reporting "2 cell(s) keep what you wrote
     in them". The undo drops the translation - rightly, the table standing there is a different one -
     and the pairing then fell back to the RAW ids of a table that had been through two imports. The
     guard "a cell the map does not name cannot vote" only ever ran when a map was there.
     ⇒ the fact that the ids are meaningless has to outlive the map that explained them. */
void KCMStorySnapshotMarkTableImported(UID story, int32 ordinal);

/** kTrue once MarkTableImported has been called for this table in this comparison. */
bool16 KCMStorySnapshotTableWasImported(UID story, int32 ordinal);

/** Forget every story's INX, keeping what restores learned about cells - what a WHOLE comparison
    does (KCMStoryDiffRun::Run). ⚠★★★The same rule as DropStory, and the same trap: a full Refresh
    Comparison goes through Run, so clearing both here wiped the cell ids of every table that had
    been put back - measured on the running application, 2026-09-20, AFTER the DropStory version of
    this mistake had already been found and fixed. **"How the comparison read" and "what the cells
    are" are two different lifetimes, and every door has to be asked which one it is closing.** */
void KCMStorySnapshotDropAllStories();

/** Forget everything, both kinds. Stop, the origin being parked or released, the close sweep, the
    shutdown - the real end of a comparison, and the only place the cell ids stop being true. */
void KCMStorySnapshotClear();

#endif // __KCMStorySnapshot_h__

// End, KCMStorySnapshot.h.
