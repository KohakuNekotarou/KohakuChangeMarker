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
//  one pair in flight there are never more than two chapters open.
//
//  This is independent of the document comparison (Start): nothing here arms anything, draws
//  anything, or touches KESCMDrawEventHandler's statics, so it can be run while a document
//  comparison is active.
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

#endif // __KESCMBookCompare_h__

// End, KESCMBookCompare.h.
