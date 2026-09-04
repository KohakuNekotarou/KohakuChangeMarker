//========================================================================================
//
//  KCMPageCheck.h
//
//  The way in to the "Check" feature. Select pages in the Pages panel, then the context-menu
//  toggle "Check" puts a "seen" mark on them and takes it off again. A checked page shows a blue
//  tick in the middle of its Pages panel thumbnail, drawn as vector strokes so no font is involved.
//
//  - Completely separate from the registrations (KCMPageMap's Added/Removed): this set is the
//    user's own marker, for whatever they want to keep track of.
//  - Only usable while a comparison is running (armed) and the selected document is the Target or
//    the Source; the menu is greyed out otherwise.
//  - **Which pages can be ticked depends on the mode**: in the Pixel mode only pages that carry a
//    mark (a frame or a "/"), in the Story mode **every page**. The answer is built in one place,
//    KCMCollectCheckablePageUIDs in KCMCore.h, and every function here asks it.
//    @warning **keeping "marked pages only" in the Story mode makes the menu item disappear**:
//    that mode builds no sEntries, so the candidate set is all but empty. That is why "could a
//    mark appear on this page" and "may this page be ticked" are two separate questions.
//  - Session only: nothing is written to the document file, so nothing is dirtied.
//    ⚠★**Stop does NOT forget it** (2026-09-04). A tick outlives the comparison it was made
//    during, and can be made without one at all. It goes when the reader clears it (the flyout's
//    "Clear Checks in This Document"), when the document closes, or at shutdown -- and it can be
//    written to KCM's own JSON and read back, which is what makes it survive a restart.
//  - The tick is drawn in two places, both in KCMDrawEventHandler: the Pages panel thumbnail (the
//    isThumb branch) and the middle of the page in the layout view (a much larger tick). On
//    screen it is always visible on both the Target and the Source; in print and PDF only while
//    "Print comparison marks" is on. Its opacity follows the panel's 25%/75% choice.
//
//========================================================================================
#ifndef __KCMPageCheck_h__
#define __KCMPageCheck_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <map>				// ⚠**this header itself no longer needs it** -- it was the prune's "pages it unticked", per document, and the prune went on 2026-09-04. Kept until a build can show which includers were leaning on it
#include <set>

#include "KCMPageMap.h"	// KCMPageToggleState -- the answer shape shared with Register (the type only)

class IDataBase;

// Runs the Pages panel context-menu toggle "Check": ticks or unticks the selected pages (any
// unticked one ticks them all, all ticked unticks them all). The outcome goes to the panel's
// status line, and the toggled pages' thumbnails are refreshed at once so the tick shows.
// The body is in KCMPageCheck.cpp.
void KCMPageCheckToggleSelectedPages();

// How that toggle (kCustomEnabling) should look right now. fEnabled is grey when nothing is
// selected, or when the selection holds no page that may be ticked; fTick is All when every
// eligible page is ticked and Some when only part of them are.
// ⚠★**No comparison is required** (2026-09-04) -- the old "grey when no comparison is running, or
//   when this is some third document" is gone. Any page of any open document may be ticked; the
//   mode's rule (Pixel = only marked pages) survives for the two documents being compared, and
//   that difference lives in KCMCollectCheckablePageUIDs alone.
// @warning **fRole is not used** -- Check's label never changes. The menu itself is not touched
// here, exactly as on the Register side.
KCMPageToggleState KCMPageCheckGetToggleState();

// The liveness sweep run after documents close (called from KCMHandleDocsClosed). Drops the ticks
// of closed documents, state only. **A closed database is never dereferenced** (pointer
// comparison against FindDocByDataBase, nothing more).
void KCMPageCheckSweepClosedDocs();

// Forget every document's ticks.
// ⚠★**Stop no longer calls this** (2026-09-04): a tick outlives the comparison it was made during.
//   The one caller left is shutdown. To clear ONE document -- what the flyout's "Clear Checks in
//   This Document" does -- call KCMPageCheckClearDoc below.
// Only empties the map; no pointer is touched.
void KCMPageCheckClearAllDocs();

// Drop ONE document's ticks -- the flyout item "Clear Checks in This Document" (2026-09-04).
// It notifies the pages it cleared, so the Pages panel's thumbnails follow, and invalidates the
// document so the layout view does too.
// ⚠**The page set has to be read before the ticks go**, which is why this lives here rather than
//   being spelled out by the caller: afterwards no one can say which pages carried a tick.
// @return how many ticks were dropped (0 for a nil db, or for a document holding none).
int32 KCMPageCheckClearDoc(IDataBase* db);

// (KCMPageCheckPruneToMarked was declared here and REMOVED on 2026-09-04, together with its two
//  call sites -- the end of KCMDoMarkChangesDoc and the partial re-comparison in KCMPeek.cpp.)
//  It unticked the pages that had stopped carrying a mark, so that "the frame is gone, and the
//  memory of having checked it goes with it".
//  ★**That reading died with the tick's own meaning**: a tick says "I have looked at this page",
//    and looking at a page is not undone by the page turning out to be unchanged.
//  ⚠**By the end it was destroying work**: ticking a document nobody was comparing and then
//    comparing THAT document threw the ticks away the moment the comparison began, and loading a
//    saved set into a compared document lost the same ticks in the same way -- both silently.
//  Nothing replaced it, and nothing needs to: a tick on a deleted page is never drawn (the drawing
//    walks the spread's real pages), never loaded back (Load walks them too), and goes with its
//    document at close.
//  The `outUnchecked` parameter went with it -- there are no unticked pages to report any more.

// Is pageUID (in db) ticked? kFalse when db is nil or that document has no ticks. The drawing
// side (KCMDrawEventHandler's isThumb branch) decides whether to draw the tick with it.
bool16 KCMPageCheckIsChecked(IDataBase* db, UID pageUID);

// Does db hold any ticked page at all -- existence only. The drawing side's early out uses it
// (anyMarkableContent in KCMDrawEventHandler::DrawSpreadMarks).
bool16 KCMPageCheckHasAny(IDataBase* db);

// The flyout item "Save Check & Register": writes the ticks, the registrations (Added/Removed =
// green "/") and the cat-paw stamps of **THE ACTIVE DOCUMENT** to KCMPageChecks.json (version 3),
// a JSON file of KCM's own directly in the roaming preferences folder. The key is the document's
// full file path, the value the arrays checks[], registered[] and paws[]. It **merges into** an
// existing file: only that one document's record is replaced, and what was saved for every other
// document is left alone. The path written to goes to the status line.
// ★**The active document, comparison or no comparison** (2026-09-04). It used to demand a running
//   one and then write both compared documents -- which, once a tick and a paw could exist without
//   a comparison, meant state that could not be saved at all.
// ⚠**A document holding nothing of ours is not written at all**; the file is not even read, so its
//   saved record survives. That is deliberate: "I cleared this document's marks" and "I have not
//   Loaded them back yet" are indistinguishable from here, and only one of them wants the record
//   gone. ⇒ **Save can never delete a record.**
// An unsaved document (no path) says so and does nothing. The body is in KCMPageCheck.cpp.
void KCMPageCheckSaveToFile();

// The flyout item "Load Check & Register". It reads that JSON back and, for **THE ACTIVE
// DOCUMENT** -- the same rule Save follows, deliberately, since a state that can be saved but not
// loaded back is worse than either rule on its own:
//   (1) applies the registrations first (KCMPageMapReplaceRegistered), then re-compares once --
//       ★**only when that document is part of a running comparison**, because otherwise its
//       registrations change nothing that is on screen. The re-comparison rebuilds the pairing and
//       refreshes the Added/Removed "/" thumbnails with it, and what it re-compares is the ARMED
//       PAIR, not the active document on its own.
//   (2) restores the ticks and the cat-paw stamps afterwards, replacing that document's current
//       ones. A tick comes back where the page may still be ticked -- which, for a document nobody
//       is comparing, means every page. A paw comes back if its page still exists, and that is the
//       whole test: a paw never depended on a comparison.
// How much was restored goes to the status line. An old v1 file (a "pages" array) is accepted
// leniently as checks, and a v2 file simply carries no paws. The body is in KCMPageCheck.cpp.
void KCMPageCheckLoadFromFile();

#endif // __KCMPageCheck_h__
