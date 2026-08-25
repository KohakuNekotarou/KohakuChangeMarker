//========================================================================================
//
//  KCMBookPair.h
//
//  Book comparison: deciding WHICH two books to compare, and pairing up their chapters.
//
//  Target is the book whose tab is in FRONT in the Book panel; source is the first other open
//  book. This mirrors the document comparison's KCMResolveComparisonPair
//  (KCMComparisonRun.cpp) and exists for the same reason: "can we start?" (the menu's grey
//  state) and "start" (the action) are two questions that drift apart unless they run the same
//  resolver.
//
//========================================================================================
#ifndef __KCMBookPair_h__
#define __KCMBookPair_h__

#include "IDFile.h"
#include "PMString.h"

#include <vector>

#include "KCMBookResult.h"	// KCMChapterResult - what the pairing fills in

class IBook;

// **KCMGetPanelBookFile is not declared here**: it lives in ui/KCMBookPanelLookup.h. Walking
//   the Book panel needs PaletteRefUtils (WidgetBin.lib), Utils<IBookUIUtils>() and IPanelMgr
//   -- all three are the UI half's to reach, and the linker said so the moment WidgetBin came
//   off the model project. The division of labour throughout this feature is the same:
//   OBSERVE in the UI, DECIDE in the model.

/** Resolve the comparison pair: target = the book in `panelBookFile`, source = first other open
    book. kTrue only when BOTH were found. Whichever could not be resolved is left nil, so the
    caller can word its message from which one is missing.

	    @warning `panelBookFile` is the FRONT TAB's book, observed by the UI half
	    (ui/KCMBookPanelLookup.h's KCMGetPanelBookFile). When that observation fails the caller
	    must not call this at all -- handing in a blank file, or substituting the active book, is
	    exactly what the old model-side code refused to do. */
bool16 KCMResolveBookPair(const IDFile& panelBookFile, IBook*& outTarget, IBook*& outSource);

// **KCMBookDisplayName is not declared here either.** It has exactly ONE caller -- the fallback
//   inside KCMBookDisplayPath, in the same .cpp -- and a header entry is a promise to callers
//   who do not exist. The display it was written for went over to full paths. It is a
//   file-local helper next to its caller instead.

/** The book's FULL PATH, as the file system spells it.

	    **Why the name is not enough.** Two books being compared are usually two versions of the
	    same job, and the job keeps its file name across versions -- "New\a.indb" and "Old\a.indb"
	    both read as "a.indb". The name alone therefore identifies the pair as ONE book twice over,
	    which is the one thing the Target/Source lines exist to rule out (the user's call).

    Falls back to the display name if the book cannot name a file, so a caller never has to handle
    an empty string. */
PMString KCMBookDisplayPath(IBook* book);

// **KCMElidePathFront is gone altogether.** Shortening a path for display is the view's
//   business and all its callers were UI-side, so it moved to the UI half -- which then dropped
//   it as well; ui/KCMBookDialog.h records what replaced it. Do not bring it back here:
//   publishing it across the model/UI boundary would be for nobody's benefit.

/** Pair the two books' chapters BY POSITION -- first with first, second with second, and so
    on. That is the user's choice; file names are deliberately not matched, so renaming a
    chapter does not break the pairing, while inserting one shifts everything after it.

    A chapter with no counterpart comes back already answered - kKCMChapterAdded (target only)
    or kKCMChapterDeleted (source only) - because having no counterpart IS the answer; nothing
    about it needs opening or comparing. Everything else comes back kKCMChapterUnknown for the
    comparison to judge. */
void KCMBuildChapterPairing(IBook* target, IBook* source, std::vector<KCMChapterResult>& out);

/** What the BOOK says about its chapter at `chapterIndex` - "missing", "out of date", "in use" or
    "open", and EMPTY when the book considers the chapter normal.

	    **For failure paths only.** "Could not be opened" is one sentence for a chapter that was
	    deleted, one another user has open, and one saved by a newer version -- three faults with
	    nothing in common to do about them. IBookUtils::GetBookContentStatus tells them apart, so a
	    failed chapter can say which it was.

    Asked here rather than kept from the pairing because the pairing hands on IDFiles and lets go of
    the IBookContent: a chapter's status is only wanted when something has gone wrong, and asking
    then costs a normal run nothing. Empty is a valid answer and means "add no word". */
PMString KCMChapterStatusText(IBook* book, int32 chapterIndex);

#endif // __KCMBookPair_h__

// End, KCMBookPair.h.
