//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: deciding WHICH two books to compare, and pairing up their chapters.
//
//  Target is the book whose tab is in FRONT in the Book panel; source is the first other open
//  book. This mirrors the document comparison's KESCMResolveComparisonPair
//  (KESCMPanelObserver.cpp) and exists for the same reason: "can we start?" (the menu's grey
//  state) and "start" (the action) are two questions that drift apart unless they run the same
//  resolver.
//
//========================================================================================
#ifndef __KESCMBookPair_h__
#define __KESCMBookPair_h__

#include "IDFile.h"
#include "PMString.h"

#include <vector>

#include "KESCMBookResult.h"	// KESCMChapterResult - what the pairing fills in

class IBook;

/** The book whose tab is in FRONT in the Book panel.
    kFalse when no front tab can be identified: the panel is iconised, its palette is closed, or
    no book is open.

    ***** This deliberately does NOT fall back to the active book. ***** IBookManager's active book
    does not follow tab switches - it only changes when a chapter is touched - so falling back
    would silently compare a book the user is not looking at. KBS does fall back at this point,
    because a search that picks the wrong book merely reads; a comparison's entire meaning is
    which two books it was run on. */
bool16 KESCMGetPanelBookFile(IDFile& outFile);

/** Resolve the comparison pair: target = front tab, source = first other open book.
    kTrue only when BOTH were found. Whichever could not be resolved is left nil, so the caller
    can word its message from which one is missing. */
bool16 KESCMResolveBookPair(IBook*& outTarget, IBook*& outSource);

/** The book's display name: IBook::GetBookTitleName().
    That INCLUDES the .indb extension - measured 2026-08-11, an open book called new.indb reports
    "new.indb", not "new". */
PMString KESCMBookDisplayName(IBook* book);

/** The book's FULL PATH, as the file system spells it.

    ***** Why the name is not enough. ***** Two books being compared are usually two versions of the
    same job, and the job keeps its file name across versions - "New\a.indb" and "Old\a.indb" both
    read as "a.indb". The name alone therefore identifies the pair as ONE book twice over, which is
    the one thing the Target/Source lines exist to rule out (the user's call, 2026-08-12).

    Falls back to the display name if the book cannot name a file, so a caller never has to handle
    an empty string. */
PMString KESCMBookDisplayPath(IBook* book);

/** The same path with its FRONT replaced by an ellipsis - "...\New\a.indb" - or unchanged if it is
    short enough to read whole.

    ***** ONE ANSWER, TWO PLACES THAT SHOW A PATH. ***** The confirmation alert and the dialog's
    Target/Source lines are the same two strings shown twice, and they must shorten the same way or
    the user is left comparing two different-looking claims about one file (memory
    one-question-one-place).

    ★★AND THE DIALOG NEEDS IT FOR A SECOND REASON - ITS WIDTH. Measured 2026-08-13: EVE sizes a
      static text widget to fit its own TEXT and pushes the parent out to match, so the dialog was as
      wide as the longest path it had been handed (610px for a 74-character path). Neither the child
      frames, nor the root frame, nor kEVEAlignFill, nor the window title moved it - all measured.
      Shortening the STRING is what puts the width back under the .fr's control, and it is also what
      finally gives kEllipsizeBeginning something to do (a widget grown to fit its text never elides). */
PMString KESCMElidePathFront(const PMString& path);

/** Pair the two books' chapters BY POSITION - first with first, second with second, and so on.
    That is the user's choice (2026-08-11); file names are deliberately not matched, so renaming a
    chapter does not break the pairing, while inserting one shifts everything after it.

    A chapter with no counterpart comes back already answered - kKESCMChapterAdded (target only)
    or kKESCMChapterDeleted (source only) - because having no counterpart IS the answer; nothing
    about it needs opening or comparing. Everything else comes back kKESCMChapterUnknown for the
    comparison to judge. */
void KESCMBuildChapterPairing(IBook* target, IBook* source, std::vector<KESCMChapterResult>& out);

#endif // __KESCMBookPair_h__

// End, KESCMBookPair.h.
