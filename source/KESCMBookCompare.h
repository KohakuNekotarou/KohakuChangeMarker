//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: running it.
//
//  Walks the chapter pairs KESCMBookPair built, opens each pair windowless, decides whether that
//  chapter changed, and closes again - one pair at a time.
//
//  ***** ONE PAIR AT A TIME, never all of them first. ***** IBookUtils' own documentation for the
//  book-side open says that when it runs out of databases it closes documents it opened earlier,
//  which would invalidate UIDRefs a "open everything, then compare" design is still holding. With
//  one pair in flight only two chapters are ever opened at once - the exception being a chapter
//  that REFUSED to close, which stays open and is counted into the report's "left open".
//
//  This is independent of the document comparison (Start): nothing here arms or disarms anything,
//  creates or drops a mark entry, or touches sDB / sSrcDB, so it can be run while a document
//  comparison is active.
//  ⚠It is not true that this touches none of KESCMDrawEventHandler's statics, which is what this
//   paragraph claimed until 2026-08-18: rasterising a page sets the thread-local rasterising guard
//   (KESCMRasterizingGuard), exactly as the document comparison's own MakeEntry does, so a draw
//   event re-entering during the snapshot paints no marks into it. It is set and cleared inside one
//   Draw and carries nothing between chapters. The shared folio-rect cache is written too (with
//   refresh=kTrue) - and every chapter close empties it through KESCMHandleDocsClosed, so the
//   document comparison re-measures its own rects on the next compare. Neither is state a running
//   comparison depends on; "touches nothing" was the wrong way to say "disturbs nothing".
//
//========================================================================================
#ifndef __KESCMBookCompare_h__
#define __KESCMBookCompare_h__

#include "PMString.h"

#include <vector>

#include "KESCMBookResult.h"

class IBook;

/** Compare two books chapter by chapter.

    outChapters comes back with one entry per chapter, each answered (Changed / NoChange /
    ChapterAdded / ChapterDeleted / Failed). outReport is the one-line summary for the status
    area, which always states how many chapters were looked at - so "nothing listed" can never be
    mistaken for "nothing could be opened". */
ErrorCode KESCMCompareBooks(IBook* target, IBook* source,
                            std::vector<KESCMChapterResult>& outChapters,
                            PMString& outReport);

/** The last comparison as one block of text: one line per chapter, "name<TAB>state", and a failed
    chapter adds "<TAB>why". Empty until something has been compared.

    ***** Kept in the module, so it answers whether or not a panel is open. ***** Same reason
    app.kcmStatus keeps its line in KESCMCore rather than in the panel widget: a comparison run
    from a script has to be readable from a script, with nothing on screen. This is what
    app.kcmBookResult returns. */
void KESCMGetBookResultText(PMString& out);

/** Shutdown only: empty the stored text, so the module's static PMString has no live heap buffer to
    free when the plug-in unloads (Mac's unload order differs from Windows').

    ★Same contract, and the same one-line body, as KESCMClearSessionStatus and
    KESCMStoryList::ShutdownCleanup. Added 2026-08-18 (bug recheck B8): the shutdown service lists
    every static container it empties and says in so many words that a PMString is the kind that
    must not be missed - and this one, which holds a line per chapter, was not on the list. */
void KESCMClearBookResultText();

#endif // __KESCMBookCompare_h__

// End, KESCMBookCompare.h.
