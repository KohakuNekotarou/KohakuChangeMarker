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

// ★THE TEXT SIDE OF THIS FILE IS BUILT OUTSIDE INDESIGN TOO (2026-09-20), the way KCMTableShape.h is:
//   KCMMergeTableCells, KCMCutTableStyleGroups and KCMBuildTableSnippet are functions over std::string
//   and nothing else, so work/kcm-tablemerge-test exercises the pairing - including the cases that are
//   awkward to make by hand in a document - with no application in the room. Only the two calls that
//   talk to the object model are left out of that build.
#include "BaseType.h"
#ifndef KCM_TABLESNIPPET_STANDALONE
#include "OMTypes.h"		// UID
#endif

#include <map>
#include <string>
#include <vector>

#ifndef KCM_TABLESNIPPET_STANDALONE
class IDataBase;
class KCMMemXferBytes;

// ⛔**CUTTING A TABLE OUT BY ITS ORDINAL IS GONE** (2026-09-20, with the pairing that counted tables).
//   KCMCutTableXml took the n-th <Table …> of a story; every caller moved to the cut BY ID below on
//   the day the user asked "a table has an id too - can that not say which is which?", and the
//   declaration outlived its last caller by a few hours. A position answers about the wrong table as
//   soon as another one is inserted before it, so nothing here should offer one again.

/** ★★★EVERY TABLE OF THE STORY, IN DOCUMENT ORDER, NAMED BY ITS OWN ID (2026-09-20, the user: "a
    table has an id too - can that not say which is which?"). The id is the last "i<hex>" of the
    table's Self: "u101i119" -> 0x119, and a nested table's "u101i119i0i123" -> 0x123 (both measured
    on the running application, and 0x119 is exactly what the DOM calls table.id = 281).

    ⇒ **THE ID IS THE TABLE'S UID IN THE DOCUMENT THAT WROTE THE TEXT.** Task Start's origin and the
    live story's own export are written by the SAME document, so the same table is named alike in
    both - which is what lets a comparison pair tables without counting them. Measured the same day:
    inserting a table at the START of a story moves no other table's id, removing one moves none of
    the rest, and an Undo of a removal brings the id back unchanged.
    ⚠**A SNIPPET IMPORT HANDS OUT NEW IDS** (281,291,301,311 -> 282,292,302,312), so a table KCM had
     put back shared none with Task Start's, and KCMStorySnapshot kept the translation. (⛔Both went
     with the restore on 2026-09-21: KCM puts no table back now, so every id is the document's own.)

    @return kFalse when the story is not in this text; kTrue with an empty list when it holds no table. */
bool16 KCMReadTableIdsInStory(const char* xml, size_t size, UID storyUID, std::vector<UID>& out);

/** The <Table …>…</Table> whose own id is `tableUID`, cut out of the story - the same text
    KCMCutTableXml returns for that table's ordinal, asked for by name instead of by position. */
bool16 KCMCutTableXmlById(const char* xml, size_t size, UID storyUID, UID tableUID, std::string& outTable);
#endif

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

    ★★★HOW THE TWO SIDES' CELLS ARE PAIRED (2026-09-20, measured on the running application -
    docs/ai-notes/kcm-table-cell-id-2026-09-20.md). NOT by Name: a row or column added above shifts
    every address below it, so the same Name is a different cell on the two sides. The pairing is
    built from EVIDENCE, in this order:

     1. ★**THE CELL'S OWN ID.** `<Cell Self="u101i119i4">` ends in the cell's id inside its table -
        `Cell.id` in the DOM - and that id DOES NOT MOVE when a row or a column is inserted:
            before      : 0:0[A]#0  1:0[B]#1  0:1[C]#4  1:1[D]#5
            column at 0 : 0:0[ ]#13 1:0[A]#0  2:0[B]#1  0:1[ ]#12 1:1[C]#4  2:1[D]#5
        So the cells both sides still have say, by themselves, where the insertion was - even in a
        table whose cells are all EMPTY, where nothing about the text could.
        ⚠**IDS ARE RECYCLED**: an id freed by a delete is handed to the next cell created, measured
         in the same run. So an id match is strong evidence and not a proof.
     2. **WHAT THE CELLS SAY**, when the ids answer nothing - which is what a table that has already
        been put back once looks like, because a snippet import REPACKS the ids (0,1,4,5 -> 0,1,2,3).
     3. **THE ADDRESS**, when neither answers and the two shapes have the same extents: the identity
        map, which is what this function did before any of this existed.
     4. Nothing: the shape goes back whole and no cell keeps anything (`outKept` is 0).

    Either way the votes are filtered to a STRICTLY INCREASING map in both directions, which is what
    throws a recycled id out: a vote that would make row 1 land above row 0 cannot be true.
    ⚠Only the OUTERMOST cells are walked: a nested table's cells sit inside one of these and travel
     with it, contents and all - which is how a cell holding a table or an anchored object is kept
     at all (the words road could not carry one).

    @param outKept how many cells kept their live contents.
    @param outHow which of the four roads above answered, for the sentence the reader is shown.
    @param liveWasTaskStart optional: "the live cell whose id is <key> WAS Task Start's cell
        <value>" - what a restore of THIS table left behind earlier in the same comparison. It is
        applied to the live ids before they are matched. (⛔Nothing fills it since the restore went
        on 2026-09-21; the parameter is kept because the four roads are still the four roads.)
    @param liveIdsAreStale ★**THE LIVE TABLE HAS BEEN THROUGH AN IMPORT AND NO TRANSLATION SURVIVES**
        - then road 1 is not taken at all. ⚠**Measured on the running application, 2026-09-20
        evening**: "Restore -> Undo the Restore -> Restore" wrote the THIRD ROW's cells into the
        second row, because the undo drops the translation (the table standing there IS a different
        one) while the raw ids of a twice-imported table went on voting. An id is evidence only
        while it is the id Task Start knows the cell by; once an import has handed out new ones and
        nothing records what they were, it is a number that happens to exist. Ignored when
        `liveWasTaskStart` is given, which is the case where the ids CAN be translated.
    @return kFalse when Task Start's text could not be walked; `outMerged` is then empty. */
bool16 KCMMergeTableCells(const std::string& olderTableXml, const std::string& liveTableXml,
						  std::string& outMerged, int32& outKept, std::string& outHow,
						  const std::map<std::string, std::string>* liveWasTaskStart = nil,
						  bool16 liveIdsAreStale = kFalse);

/** ★★★EVERY OUTERMOST CELL LABELLED WITH ITS OWN ID (2026-09-20, the user's design).

    A snippet import REPACKS the cells' ids (0,1,4,5 -> 0,1,2,3, measured), so a table that has been
    put back once no longer shares an id with Task Start's - and the pairing above would have to fall
    back to the words. A script label survives the import, and a cell's script label is a CELL
    ATTRIBUTE (`kCellAttrScriptLabelBoss` is the SDK's one implementer of IScriptLabel), written into
    <Cell> as <Properties><Label>, which is where IDML itself puts one.

    ⚠**THE LABEL IS NEVER MEANT TO REACH THE READER'S DOCUMENT** (the user: "delete the label KCM put
     on once the frame has been made from the snippet, and then paste"). It is read and then cleared
     in the scratch document, so what is copied into the Target carries no mark of ours - and the
     question of whether the copy would have carried a label at all is never asked.

    The value written is the cell's own id - its Self with the table's Self taken off, "i4". A cell
    that already has <Properties>, or a <Label> of its own, keeps them: ours joins the list.

    @param key the label's key, the same one the reader is given back.
    @return how many cells were labelled. */
int32 KCMLabelTableCells(std::string& tableXml, const char* key);

/** Every outermost cell of a table's XML as "col:row" -> its own id ("i4"). What a restore reads off
    the table it has just written, so that the new ids can be tied back to Task Start's. */
void KCMReadTableCellIds(const std::string& tableXml, std::map<std::string, std::string>& out);

/** The minimal page-item snippet: the PI header, <Document>, the style groups, one <Spread> with one
    <TextFrame>, and one <Story> whose only content is `tableXml`. UTF-8. */
void KCMBuildTableSnippet(const std::string& tableXml, const std::string& styleGroups, std::string& outSnippet);

#ifndef KCM_TABLESNIPPET_STANDALONE
/** The live story as INX text (IINXManager::ExportINX with the story as the root - the call
    KCMPdfSpike's S17.8 measured), into `out`. kFalse when the export failed.

    @param includeStyleRoots ★also hand the export the document's CELL and TABLE style roots as roots
        of their own, so that one small export carries the <Story> AND the two style groups a table's
        snippet needs (2026-09-20). ⚠**ExportINX takes a LIST of roots** - it always did; this passed
        one. Without the groups a table lands on "[No table style]" rather than on the document's own
        style of that name, which is why the Target's WHOLE internal IDML used to be held for a whole
        comparison just to supply them. */
bool16 KCMExportStoryInx(IDataBase* db, UID storyUID, KCMMemXferBytes& out,
						 bool16 includeStyleRoots = kFalse);
#endif // KCM_TABLESNIPPET_STANDALONE

#endif // __KCMTableSnippet_h__

// End, KCMTableSnippet.h.
