//========================================================================================
//
//  KCMBookCompare.h
//
//  Book comparison: running it.
//
//  Walks the chapter pairs KCMBookPair built, opens each pair windowless, decides whether that
//  chapter changed, and closes again -- one pair at a time.
//
//  **ONE PAIR AT A TIME, never all of them first.** The documentation of the book-side open
//  (IBookUtils::OpenOneDocument) says that when it runs out of databases it closes documents it
//  opened earlier, which would invalidate UIDRefs an "open everything, then compare" design is
//  still holding. With one pair in flight only two chapters are ever opened at once -- the
//  exception being a chapter that REFUSED to close, which stays open and is counted into the
//  report's "left open".
//
//  This is independent of the document comparison (Start): nothing here arms or disarms
//  anything, creates or drops a mark entry, or touches sDB / sSrcDB, so it can be run while a
//  document comparison is active.
//  @warning **it does disturb two of KCMDrawEventHandler's statics** -- neither of which a
//   running comparison depends on. Rasterising a page sets the thread-local rasterising guard
//   (KCMRasterizingGuard), exactly as the document comparison's own MakeEntry does, so a draw
//   event re-entering during the snapshot paints no marks into it; it is set and cleared inside
//   one Draw and carries nothing between chapters. The shared folio-rect cache is written too
//   (with refresh=kTrue), and every chapter close empties it through KCMHandleDocsClosed, so
//   the document comparison re-measures its own rects on the next compare.
//
//========================================================================================
#ifndef __KCMBookCompare_h__
#define __KCMBookCompare_h__

#include "PMString.h"

#include <vector>

#include "KCMBookResult.h"

class IBook;

/** Compare two books chapter by chapter.

    outChapters comes back with one entry per chapter, each answered (Changed / NoChange /
    ChapterAdded / ChapterDeleted / Failed). outReport is the one-line summary for the status
    area, which always states how many chapters were looked at - so "nothing listed" can never be
    mistaken for "nothing could be opened". */
ErrorCode KCMCompareBooks(IBook* target, IBook* source,
                            std::vector<KCMChapterResult>& outChapters,
                            PMString& outReport);

/** The last comparison as one block of text: one line per chapter, "name<TAB>state", and a failed
    chapter adds "<TAB>why". Empty until something has been compared.

	    **Kept in the module, so it answers whether or not a panel is open.** Same reason the
	    panel's status line is kept in KCMModelNotify rather than in the panel widget: a
	    comparison run from a script has to be readable from a script, with nothing on screen.
	    This is what app.kcmBookResult returns. */
void KCMGetBookResultText(PMString& out);

/** Shutdown only: empty the stored text, so the module's static PMString has no live heap buffer to
    free when the plug-in unloads (Mac's unload order differs from Windows').

	    **Same contract as KCMClearSessionStatus and KCMStoryList::ShutdownCleanup**, and the same
	    caller: the shutdown service in KCMPeek.cpp lists every static container it empties, and
	    says in so many words that a PMString is the kind that must not be missed. A new static
	    holding one means a new line there as well as here. */
void KCMClearBookResultText();

#endif // __KCMBookCompare_h__

// End, KCMBookCompare.h.
