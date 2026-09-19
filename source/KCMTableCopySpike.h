//========================================================================================
//
//  KCMTableCopySpike.h -- THE EXPERIMENT that has to answer before a "Table" row can be
//  put back from the Task Start copy.
//
//  WHAT IS BEING DECIDED (2026-09-19 night, the user): a table whose shape changed since Task
//  Start - rows, columns, merged cells - is to be ONE row of Story Edits ("Table"), and
//  "Restore Source Text" on it is to put the WHOLE table back, formatting and styles included,
//  from the rehydrated Task Start copy. Text alone cannot do that (a table rebuilt from words
//  loses its table style, its widths, its merges).
//
//  THE ROUTE THIS FILE MEASURES: kCopyStoryRangeCmdBoss, which the SDK's own snippet
//  (SnpManipulateTextModel.cpp:76) uses "to copy text, tables and inline graphics from one story
//  to another". The source is the copy's story and the range of the table's anchor characters;
//  the destination is the live story and the range of ITS table's anchor characters, replaced.
//
//  ⚠NOTHING HERE IS SETTLED, and the open questions are written into the reading it hands back:
//      1. does the command accept a source story in ANOTHER DATABASE (the copy is a document of
//         its own, closed again when this call returns),
//      2. does the table arrive with its style references mapped BY NAME into the live document,
//      3. what GetAnchorTextRange covers - the anchor alone, or the anchor plus the one
//         kTextChar_TableContinued per further row - and whether the range has to be widened.
//  What the table looks like afterwards (rows, columns, cell words, table style) is read from
//  OUTSIDE, by the script that called this, so the reading here stays about the command.
//
//  ★THE WAY IN is app.kcmProbeTableCopy(storyRow, tableOrdinal) (KCM.fr, KCMScriptProvider.cpp):
//    the story row as app.kcmStoryRows numbers it, the table as KCMTextRead numbers them (0..).
//  ⚠IT WRITES INTO THE ACTIVE DOCUMENT - one command, one Ctrl+Z. A Task Start has to be held.
//  ⚠A SPIKE: retire it (ScriptID 'eKGz', element +35) once the Table row is built on the answer.
//
//========================================================================================

#pragma once
#ifndef __KCMTableCopySpike_h__
#define __KCMTableCopySpike_h__

#include "BaseType.h"

class PMString;

/** Copy table `tableOrdinal` of the Task Start copy's story over the same-numbered table of the
    live story that row `storyRow` names, and describe every step in `out`, one line each. */
void KCMProbeTableCopy(int32 storyRow, int32 tableOrdinal, PMString& out);

#endif // __KCMTableCopySpike_h__

// End, KCMTableCopySpike.h.
