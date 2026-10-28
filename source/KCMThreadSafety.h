//========================================================================================
//
//  KCMThreadSafety.h
//
//  What a kModelPlugIn needs in order to survive being called on a background thread. KCM
//  became one in stage 2, and from that moment **draw events are delivered to BG threads too**
//  (measured). Only three things live here:
//
//    1. KCMIsMainThread()   -- "are we on the main thread right now"
//    2. KCMIsSameDoc()      -- **do two IDataBase* point at the same document**
//    3. KCMMarkStateMutex() -- the lock over the shared mark state
//
//  --------------------------------------------------------------------------------------
//  WHY RAW POINTER COMPARISON IS NOT ENOUGH
//
//    Guide vol1-07, "Rules for thread safety": "InDesign's multithreading environment
//    provides a separate execution context (**a cloned copy of the database**) for each
//    thread."
//
//    Measured:
//      MAIN  db=...23FB4A80  sDB=...23FB4A80  entries=2 firstUID=258 class=1295
//      *BG*  db=...295BE390  sDB=...23FB4A80  entries=2 firstUID=258 class=1295
//
//    =>  (1) the BG db is a **clone with a different pointer** and can never equal sDB
//        (2) but **UIDs survive the cloning** -- the same page comes back from the BG db
//        (3) statics **are shared** across threads (entries=2 is visible from both)
//
//    So "is this the same document" has to be asked of the FILE (IDataBase::GetSysFile).
//    **That is the same conclusion as [[uidref-reuse-after-close]]** (a closed document's
//    pointer gets reused and can match a different document) -- one fix answers both.
//
//  --------------------------------------------------------------------------------------
//  THE OFFICIAL SHAPES THIS FOLLOWS (read off the SDK; not following them splits the idiom)
//
//    - a boost::mutex held in a static member and taken with scoped_lock
//        = sdksamples/hyphenator's HypPerformanceData (non-recursive there), whose vcxproj
//          links `$(MODEL_PLUGIN_LINKLIST);$(BoostThreadLib)` -- the same line as KCM's
//    - per-thread values (re-entrancy guards and the like) through IDThreading::ThreadLocal
//      / ThreadLocalManagedObject
//        = InCopyDocFileHandler's tl_DBList (**a list of IDataBase* held thread-locally to
//          prevent re-entry** -- the same use as KCM's rasterising flag, down to the tl_ prefix)
//    - the UI half writes no synchronisation at all
//        = linksui and layerpanel contain **no IsMainThreadDomain and no mutex whatsoever**
//          => the evidence behind fixing **the model side only**.
//
//========================================================================================
#ifndef __KCMThreadSafety_h__
#define __KCMThreadSafety_h__

#include "BaseType.h"		// bool16, kTrue/kFalse

#include <boost/thread/recursive_mutex.hpp>	// the official shape = hyphenator's HypPerformanceData (non-recursive there)

class IDataBase;

//----------------------------------------------------------------------------------------
// Are we on the main thread? A thin wrapper over IDThreading::IsMainThreadDomain()
// (bool -> bool16).
// @warning **"do nothing on BG" is right only for the code that TEARS STATE DOWN.** Stopping
//   the DRAWING side on a background thread would defeat the point of stage 2 itself: marks
//   in the PDF export.
//----------------------------------------------------------------------------------------
bool16 KCMIsMainThread();

//----------------------------------------------------------------------------------------
// Do two IDataBase* mean "the same document"?
//   - same pointer -> true (main's ordinary path decides here, at no added cost)
//   - different pointers are still true when GetSysFile() names the same file
//     => **true for the background's clone DB as well**, which is the whole point
//   - **when either side has no file, compare the documents' xmp.did instead** (below)
//   - false only when either side is nil, or an ID is empty (or cannot be read on this thread)
//
//     **THE UNSAVED-DOCUMENT PATH.** Before it existed this returned false as soon as a file
//       was missing, so **comparing two documents that had never been saved produced no marks
//       at all on the background thread (the asynchronous PDF export)** -- they appeared on
//       screen, so the defect took the shape **"the screen and the export disagree"**: the
//       hole stage 2 was meant to close stayed open for unsaved documents only.
//
//     Measured (two unsaved documents compared, asynchronous PDF export):
//       (1) **an unsaved document still has a value** -- GetSysFile() is nil, xmp.did:... is not
//       (2) **Target and Source hold different values** (a3097be8... vs 6322d72a...)
//           => usable as an identity
//       (3) **the BG clone DB matches main exactly** (different db pointer, same ID)
//       (4) sDB on the BG thread is still the pointer main put there (statics are shared)
//
//     **THE EXTERNAL DOOR (2026-10-29, the author's call "A").** Until then the ID came from
//       IDataBase::GetDocumentID, which says of itself "FOR INTERNAL USE ONLY / FOR EXTERNAL
//       USE : Recommended to use IAdobeMediaMgmtMetaData::GetDocumentID" - chosen on 2026-08-16
//       because it is reachable straight from an IDataBase*, and kept against the rule of
//       2026-09-02 (nothing marked internal in a plug-in that goes to Exchange). Now the
//       external door reads it - which INSTANTIATES the document's root, so each thread reads
//       only a database of its own: the main thread its documents (and keeps each unsaved one's
//       ID, met in every screen draw and at Start), a background thread the clone it exports
//       (KCMSetExportCloneOnThisThread) - and a main-thread document's ID there comes from what
//       main kept. A document a background export compares against that main never met (one
//       never drawn, never armed) answers "not the same": no marks, never a cross-thread read.
//       Record: docs/ai-notes/hdr-reread-own-diff1029-2026-10-29.md section 2.
//
//     @warning **the cost only rises for unsaved documents**: a saved one still ends at the
//       file comparison, and main's ordinary path decides on the pointer test - after a map
//       lookup for an unsaved document, the price of keeping its ID for the background.
//----------------------------------------------------------------------------------------
bool16 KCMIsSameDoc(IDataBase* a, IDataBase* b);

//----------------------------------------------------------------------------------------
// The clone this execution context is exporting, for KCMIsSameDoc's background reading - set
// by KCMBeginExportOn (a book's next document too) and cleared by KCMEndExportOnThisThread;
// nil when nothing is being exported here.
//----------------------------------------------------------------------------------------
void KCMSetExportCloneOnThisThread(IDataBase* db);

//----------------------------------------------------------------------------------------
// A document is closing (main thread, kBeforeCloseDoc - KCMDocResponder.cpp): the ID kept for
// it goes, so an address that comes back as another document is not taken for this one.
//----------------------------------------------------------------------------------------
void KCMForgetDocumentId(IDataBase* db);

//----------------------------------------------------------------------------------------
// The lock over KCM's shared state. What it covers:
//   - the mark entries (sEntries) and the overflow sets, in KCMDrawEventHandler
//   - the registered-page set and the check-tick set (KCMDocUidSet takes this lock inside
//     its own readers and writers)
//   - the Story mode's marks (KCMStoryMarker takes this same lock)
//
// **Why they need it**: main writes them and **BG -- the asynchronous PDF export -- reads
// them while drawing**:
//   - sEntries is a map of **raw pointers** that DropAll() deletes
//     => a Stop on main while BG is reading it is a **read of freed memory**
//   - the page sets (KCMDocUidSet) are std::map/std::set
//     => a find() on BG while main is rotating the tree inside insert corrupts it
//   (Guide vol1-07: "InDesign will behave inconsistently and **may randomly crash**")
//
// **TWO SHARED CONTAINERS ARE NOT COVERED**, and what keeps them safe is where their readers
// live -- not the lock:
//   - (`sOversetPages` / `sOversetLocs` were guarded here. Find Overset went on 2026-09-08 and
//     both are gone. What follows described them; it is kept because the REASONING still
//     applies to any set the main thread swaps wholesale while the drawing reads it.)
//     Same shape as the sets above: a swap while
//     something is reading corrupts it.
//   Their readers are (⚠**the list below is `sOversetPages`'s**. Measured 2026-08-31:
//   `sOversetLocs` has (b) ALONE -- one Facade getter, GetOversetLocations -- and no reader in
//   the drawing at all. It is listed here with its neighbour because it is swapped in the same
//   breath, not because it has the same exposure; ★do not read (a) or (c) as covering it, and
//   do not conclude from "no reader in the drawing" that a new one would be safe):
//     (a) **model side, inside the drawing**: `wantOversetThumb = isThumb && sOversetOn &&
//         sOversetDB != nil && !sOversetPages.empty()` ---- **`isThumb` is the FIRST term**, so
//         on BG (the asynchronous export, where isThumb is false) short-circuit evaluation
//         never touches the set. The block that calls count() sits in that same branch.
//     (b) **UI side, through the Facade**: IsOversetPage, GetOversetPageCount,
//         GetOversetPageUIDs and GetOversetLocations -- the scrollbar map, Prev/Next and the
//         flyout's counts. **A kUIPlugIn boss is invisible from a background thread**, so none
//         of these can be reached from one.
//     (c) **model side, main thread only**: KCMApplyOversetForDoc reads the new set straight
//         back (KCMOversetApply.cpp), to hand the UI the pages whose "+" may have changed. It
//         is the writer reading what it wrote, four lines after its own swap, on the thread
//         that wrote it -- safe for a THIRD reason, and listed because the rule below is to
//         count places. **It was missing from this list until the waste sweep measured it**,
//         in a paragraph that ends by saying to count places rather than containers.
//   @warning **none of those three is a designed defence**: (a) is the order of the terms in
//     one condition, (b) is which plug-in the caller happens to live in, and (c) is that one
//     function happening to be main-thread-only. The day the "+" is drawn anywhere other than
//     a thumbnail -- canvas, print, export -- **these two go under the lock**. So does
//     reordering that condition.
//
// **"Covered" and "not covered" are counted per PLACE THAT TOUCHES, not per container.** A
//   covered container was once read in an uncovered place: the `anyMarkableContent` gate at
//   the top of DrawSpreadMarks read sEntries and the overflow sets **outside the lock**, on a
//   line **every BG draw passes**, while main wrote those same containers under it in
//   DropAll() (delete+clear), MakeEntry (insert) and swap(). **Only the writers were guarded,
//   and one reader stood outside** -- the mirror image of "guarding only the side that throws
//   away is worthless". That gate takes the lock now, and has grown to six terms: the
//   page-map and check-tick readers are in it too, and each of those takes the lock itself.
//   **Count the places that touch, not the names of the containers** -- counting by container
//   is exactly how the third read of the same one stays out of sight.
//
// **Why it is a recursive_mutex** (nearly paid for once): the drawing loop takes the lock and
//   then calls KCMPageMapIsRegistered() / KCMPageCheckIsChecked() **per page**, and those take
//   the lock themselves, inside KCMDocUidSet. With a non-recursive boost::mutex -- the
//   hyphenator's shape -- that is **a double lock on one thread, an immediate deadlock**.
//   Being able to call without knowing whether the caller already holds the lock won.
//   @warning the price is that deep nesting goes unnoticed. **Do not hold the lock through a
//     long operation** still applies -- one spread's drawing, a few ms, is the target.
//
// Use:  KCMMarkStateLock lock(KCMMarkStateMutex());
//----------------------------------------------------------------------------------------
boost::recursive_mutex& KCMMarkStateMutex();
typedef boost::recursive_mutex::scoped_lock KCMMarkStateLock;

#endif // __KCMThreadSafety_h__
