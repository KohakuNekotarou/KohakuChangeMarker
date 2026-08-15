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

// ★KESCMGetPanelBookFile moved OUT of this header on 2026-08-15 (Stage 2, Task 9B) to
//   ui/KESCMBookPanelLookup.h. Walking the Book panel needs PaletteRefUtils (WidgetBin.lib),
//   Utils<IBookUIUtils>() and IPanelMgr - all three are the UI half's to reach, and the linker
//   said so the moment WidgetBin came off the model project. Same shape as Task 15's
//   KESCMElidePathFront and Task 4B's view lookups: OBSERVE in the UI, DECIDE in the model.

/** Resolve the comparison pair: target = the book in `panelBookFile`, source = first other open
    book. kTrue only when BOTH were found. Whichever could not be resolved is left nil, so the
    caller can word its message from which one is missing.

    ⚠`panelBookFile` is the FRONT TAB's book, observed by the UI half
    (ui/KESCMBookPanelLookup.h's KESCMGetPanelBookFile). When that observation fails the caller
    must not call this at all - handing in a blank file, or substituting the active book, is
    exactly what the old model-side code refused to do. */
bool16 KESCMResolveBookPair(const IDFile& panelBookFile, IBook*& outTarget, IBook*& outSource);

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

// ★KESCMElidePathFront moved OUT of this header on 2026-08-14 (Stage 1, Task 15). Shortening a
//   path for display is the view's business and all three callers were UI-side, so it now lives in
//   KESCMBookDialog.h -- the file this feature's own division of labour calls "the VIEW". Leaving it
//   here would have meant publishing it across the model/UI boundary for nobody's benefit.

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
