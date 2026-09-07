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
//  - ⚠**NO COMPARISON IS NEEDED, AND NO PAGE IS REFUSED** (2026-09-04, then 2026-09-07). Any page
//    of any open document may be ticked. The two restrictions that used to stand here went in that
//    order: first "only while a comparison is running, and only on the Target or the Source", then
//    "in the Pixel mode, only pages carrying a mark" (spec map FLG-12 -- **a page can be worth
//    marking as looked-at precisely because nothing changed on it**).
//    The answer is still built in one place, KCMCollectCheckablePageUIDs in KCMCore.h, and every
//    function here asks it -- it simply has one answer now.
//    @warning **"could a mark appear on this page" is a DIFFERENT question** and must not be
//    widened to match: it drives the Pages panel thumbnail purge.
//  - ⚠**NO LONGER SESSION ONLY** (2026-09-07). The tick and the cat paw are written INTO the
//    document as script labels on the page (KCMPageMarksDoc.h), through a command, so they are
//    undoable and travel with the file. **This set is now a CACHE of those labels**, kept because
//    the drawing side has to answer "is this page ticked" once per page per draw, on a background
//    thread as well. The registrations (Added/Removed) are still session-only.
//    ⚠★**Stop does NOT forget it** (2026-09-04). A tick outlives the comparison it was made
//    during, and can be made without one at all. It goes when the reader clears it (the flyout's
//    "Clear Checks in This Document"), when the document closes, or at shutdown -- and **what makes
//    it survive a restart is the document itself**, since it is saved with the file.
//    ⚠The private JSON store that used to answer that went on 2026-09-07 (spec map FLG-24).
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
// ⚠★**No comparison is required, and no mode rule is left** (2026-09-04, then 2026-09-07). The old
//   "grey when no comparison is running, or when this is some third document" went first, and the
//   Pixel mode's "only pages carrying a mark" went second (spec map FLG-12). Any page of any open
//   document may be ticked, and KCMCollectCheckablePageUIDs says so in one line.
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

/** The ticked pages of one document -- what a save reads. out is cleared first.
	★Added 2026-09-07 for KCMPageMarksDoc: the file-based save reaches the same set through the
	 container's GetMap, which is inside this file; a caller outside it needs a door of its own. */
void KCMPageCheckCollect(IDataBase* db, std::set<UID>& out);

/** Put one document's ticks back to exactly this set -- what a restore writes.
	★REPLACES rather than merges, for the reason KCMPawStampReplaceAll gives. */
void KCMPageCheckReplaceAll(IDataBase* db, const std::set<UID>& in);

// (The declarations of KCMPageCheckSaveToFile and KCMPageCheckLoadFromFile stood here.
//  Both went on 2026-09-07 with the two flyout items -- the .cpp says why at the same place, and
//  the spec map records the decision as FLG-24. Nothing writes a private file any more: a tick or
//  a paw goes into the document as it is made, and comes back when the document is opened.)

#endif // __KCMPageCheck_h__
