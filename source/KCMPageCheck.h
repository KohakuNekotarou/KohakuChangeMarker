//========================================================================================
//
//  KCMPageCheck.h
//
//  The way in to the "Check" feature. Select pages in the Pages panel, then the context-menu
//  toggle "Check" puts a "seen" mark on them and takes it off again. A checked page shows a blue
//  tick in the middle of its Pages panel thumbnail, drawn as vector strokes so no font is involved.
//
//  - Completely separate from the registrations (KCMPageMap's Added/Removed): this set is the
//    reader's own marker, for whatever they want to keep track of.
//  - ⚠**NO COMPARISON IS NEEDED, AND NO PAGE IS REFUSED** (spec map FLG-11, FLG-12 -- **a page can
//    be worth marking as looked-at precisely because nothing changed on it**). Any page of any
//    open document may be ticked, in any mode. The answer is built in one place,
//    KCMCollectCheckablePageUIDs in KCMCore.h, and the toggle and its state ask it.
//    @warning **"could a mark appear on this page" is a DIFFERENT question** and must not be
//    widened to match: it drives the Pages panel thumbnail purge.
//  - ⚠**NOT SESSION ONLY.** The tick and the cat paw are written INTO the document as script
//    labels on the page (KCMPageMarksDoc.h), through a command, so they are undoable and travel
//    with the file. **This set is a CACHE of those labels**, kept because the drawing side has to
//    answer "is this page ticked" once per page per draw, on a background thread as well. The
//    registrations (Added/Removed) are session-only.
//    ⚠**Stop does NOT forget it** (spec map FLG-13). A tick outlives the comparison it was made
//    during, and can be made without one at all. It goes when the reader takes it off (the toggle,
//    the flyout's "Clear Checks in This Document"); closing the document or shutting down only
//    empties this cache, and **what makes it survive a restart is the document itself**, since it
//    is saved with the file.
//  - ⚠**A RE-COMPARISON DOES NOT TOUCH IT**: a tick says "I have looked at this page", and looking
//    at a page is not undone by the page turning out to be unchanged. Narrowing the ticks to the
//    marked pages threw away, silently, the ticks of a document the moment a comparison started
//    on it. Nothing needs to replace that: a tick on a deleted page is never drawn (the drawing
//    walks the spread's real pages), and the cache forgets it when it is next brought back to the
//    document.
//  - ⚠**NO PRIVATE FILE** (spec map FLG-24, the author's request): a tick or a paw goes into the
//    document as it is made, so a file beside it was a second copy of the same thing, keyed by a
//    path that broke whenever the document was moved, and a registration is an input to one
//    comparison. git log -S KCMPageCheckSaveToFile has that store (JSON v3).
//  - The tick is drawn in two places, both in KCMDrawEventHandler: the Pages panel thumbnail (the
//    isThumb branch) and the middle of the page in the layout view (a much larger tick). It shows
//    on screen on any document, Target, Source or neither (the layout view's only with "Print
//    comparison marks" on in Overprint Preview), and in print and PDF only while "Print comparison
//    marks" is on. The layout view's and print's opacity follows the panel's 25%/75% choice; the
//    thumbnail's is fixed (kKCMThumbMarkOpacity).
//
//========================================================================================
#ifndef __KCMPageCheck_h__
#define __KCMPageCheck_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <set>

#include "KCMPageFlagState.h"	// KCMPageToggleState -- the answer shape shared with Register (a header of types only)

class IDataBase;
class UIDList;			// the Pages panel's selection, as the UI read it

// Runs the Pages panel context-menu toggle "Check": ticks or unticks the selected pages (any
// unticked one ticks them all, all ticked unticks them all). The outcome goes to the panel's
// status line; the write goes to the document, and the pictures follow through the marks observer.
// `selection` is the Pages panel's, as the UI read it (masters in - KCMPageMap.h). The body is in KCMPageCheck.cpp.
void KCMPageCheckToggleSelectedPages(const UIDList& selection);

// How that toggle (kCustomEnabling) should look right now. fEnabled is grey when nothing is
// selected, or when the selection holds no page that may be ticked; fTick is All when every
// eligible page is ticked and Some when only part of them are.
// ⚠**No comparison is required, and no mode rule applies** (spec map FLG-11, FLG-12). Any page of any
//   open document may be ticked, and KCMCollectCheckablePageUIDs says so in one line.
// @warning **fRole is not used** -- Check's label never changes. The menu itself is not touched
// here, exactly as on the Register side.
KCMPageToggleState KCMPageCheckGetToggleState(const UIDList& selection);

// The liveness sweep run after documents close (called from KCMHandleDocsClosed). Drops the ticks
// of closed documents, state only. **A closed database is never dereferenced** (pointer
// comparison against FindDocByDataBase, nothing more).
void KCMPageCheckSweepClosedDocs();

// Forget every document's ticks.
// ⚠**Stop does not call this**: a tick outlives the comparison it was made during.
//   The one caller is shutdown. To clear ONE document -- what the flyout's "Clear Checks in
//   This Document" does -- call KCMPageCheckClearDoc below.
// Only empties the map; no pointer is touched.
void KCMPageCheckClearAllDocs();

// Drop ONE document's ticks -- the flyout item "Clear Checks in This Document".
// The pictures follow through the marks observer, as for the toggle.
// ⚠**The page set has to be read before the ticks go**, which is why this lives here rather than
//   being spelled out by the caller: afterwards no one can say which pages carried a tick. It is
//   read from the document (the store brought back to it first), so a page deleted since the last
//   write is not handed to the write - which then fails as a whole.
// @return how many ticks were dropped (0 for a nil db, or for a document holding none), or -1 when
//   the write failed and none were (said on the status line by KCMMarksWriteForReader).
int32 KCMPageCheckClearDoc(IDataBase* db);

// Is pageUID (in db) ticked? kFalse when db is nil or that document has no ticks. The drawing
// side (KCMDrawEventHandler's thumbnail and layout-view ticks) decides whether to draw the tick with it.
bool16 KCMPageCheckIsChecked(IDataBase* db, UID pageUID);

// Does db hold any ticked page at all -- existence only. The drawing side's early out uses it
// (anyMarkableContent in KCMDrawEventHandler::DrawSpreadMarks).
bool16 KCMPageCheckHasAny(IDataBase* db);

/** The ticked pages of one document as the store holds them -- what KCMMarksSyncFromDocument compares
	the document against, to name the pages that changed. out is cleared first. */
void KCMPageCheckCollect(IDataBase* db, std::set<UID>& out);

/** Put one document's ticks back to exactly this set -- what KCMMarksSyncFromDocument writes.
	REPLACES rather than merges: the document is the answer, so a tick it no longer carries has to go. */
void KCMPageCheckReplaceAll(IDataBase* db, const std::set<UID>& in);

#endif // __KCMPageCheck_h__
