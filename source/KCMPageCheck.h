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
//  - Session only: nothing is written to the document file, so nothing is dirtied. Stop forgets
//    all of it.
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
#include <map>				// KCMPageCheckPruneToMarked's "pages it unticked", per document
#include <set>

#include "KCMPageMap.h"	// KCMPageToggleState -- the answer shape shared with Register (the type only)

class IDataBase;

// Runs the Pages panel context-menu toggle "Check": ticks or unticks the selected pages (any
// unticked one ticks them all, all ticked unticks them all). The outcome goes to the panel's
// status line, and the toggled pages' thumbnails are refreshed at once so the tick shows.
// The body is in KCMPageCheck.cpp.
void KCMPageCheckToggleSelectedPages();

// How that toggle (kCustomEnabling) should look right now. fEnabled is grey when no comparison
// is running, when this is some third document, when nothing is selected, or when the selection
// holds no page that may be ticked; fTick is All when every eligible page is ticked and Some when
// only part of them are.
// @warning **fRole is not used** -- Check's label never changes. The menu itself is not touched
// here, exactly as on the Register side.
KCMPageToggleState KCMPageCheckGetToggleState();

// The liveness sweep run after documents close (called from KCMHandleDocsClosed). Drops the ticks
// of closed documents, state only. **A closed database is never dereferenced** (pointer
// comparison against FindDocByDataBase, nothing more).
void KCMPageCheckSweepClosedDocs();

// Forget every document's ticks. Stop (KCMDoClearMarks) calls it so that clearing a comparison
// leaves no ticks behind. Only empties the map; no pointer is touched.
void KCMPageCheckClearAllDocs();

// Called after a re-comparison. Unticks the pages that may no longer be ticked, so that "the
// frame is gone, and the memory of having checked it goes with it". The candidates come from
// KCMCollectCheckablePageUIDs, which answers with nothing unless db is one of the two documents
// being compared -- and then every tick in that document comes off. Called at the end of
// KCMDoMarkChangesDoc, before the thumbnails are refreshed.
// **Nothing comes off in the Story mode**, since every page is a candidate there.
//   @warning **switching Story -> Pixel does untick pages**: the switch re-compares, and the
//   Pixel mode's candidates are only the pages carrying a frame again. **That is correct** -- a
//   tick in the Pixel mode means "I looked at this changed page", so it must not survive on a
//   page that has no frame.
// @warning **the name still says "ToMarked"** while the meaning has widened to "keep only what
//   may still be ticked". Renaming it touches two callers and the declaration, so it belongs in
//   a commit of its own.
//
// outUnchecked (optional, nil allowed) collects **the pages actually unticked**, per document.
//   @warning **a caller that refreshes thumbnails per UID needs it**: an unticked page's picture
//     changes, but the page is in none of the sets the current state can produce once the tick
//     is gone, so **it cannot be recovered afterwards** (the same shape as the Register / tick
//     toggles; see fPagesA in KCMModelNotify.h).
//   What comes back is only what this prune unticked, which is not the same as the pages the
//     caller touched itself. **Callers add it to their own set rather than replacing it.**
//   @warning the IDataBase* keys are never dereferenced here (they only identify whose set it
//     is), and a caller must check that a document is still alive before dereferencing one --
//     this function also prunes the ticks of documents that have closed.
void KCMPageCheckPruneToMarked(std::map<IDataBase*, std::set<UID> >* outUnchecked = nil);

// Is pageUID (in db) ticked? kFalse when db is nil or that document has no ticks. The drawing
// side (KCMDrawEventHandler's isThumb branch) decides whether to draw the tick with it.
bool16 KCMPageCheckIsChecked(IDataBase* db, UID pageUID);

// Does db hold any ticked page at all -- existence only. The drawing side's early out uses it
// (anyMarkableContent in KCMDrawEventHandler::DrawSpreadMarks).
bool16 KCMPageCheckHasAny(IDataBase* db);

// The flyout item "Save Check & Register": writes the current ticks and the current
// registrations (Added/Removed = green "/") of the armed Target and Source to KCMPageChecks.json
// (version 2), a JSON file of KCM's own directly in the roaming preferences folder. The key is
// the document's full file path, the value the two arrays checks[] and registered[]. It **merges
// into** an existing file: only the two documents being compared are updated, and what was saved
// for any other document is left alone. The path written to goes to the status line. Nothing
// happens when no comparison is running, and an unsaved document (no path) is skipped.
// The body is in KCMPageCheck.cpp.
void KCMPageCheckSaveToFile();

// The flyout item "Load Check & Register", enabled only while a comparison is running. It reads
// that JSON back and, for the Target and the Source:
//   (1) applies the registrations to both documents first (KCMPageMapReplaceRegistered), then
//       re-compares once -- which rebuilds the pairing and refreshes the Added/Removed "/"
//       thumbnails with it
//   (2) restores the ticks afterwards, keeping only the saved pages that may still be ticked,
//       and replacing that document's current ticks. In the Story mode every page is a
//       candidate, so a saved tick comes back whether the page carries a mark or not.
// How much was restored goes to the status line. An old v1 file (a "pages" array) is accepted
// leniently as checks. The body is in KCMPageCheck.cpp.
void KCMPageCheckLoadFromFile();

#endif // __KCMPageCheck_h__
