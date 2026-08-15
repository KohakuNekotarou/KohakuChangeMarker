//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: what a row of the chapter list DOES.
//
//  The list itself only reports. These two actions are what turn a verdict into something the user
//  can look at:
//    * double click  -> open that chapter (both sides)
//    * right click   -> "Start Change Marker" - open both sides AND compare them
//
//  Both live here rather than in the event handler, because the handler's job is deciding WHICH
//  click this is (that part is delicate enough on its own - see KESCMBookRowEH.cpp) and this one's
//  is deciding what a chapter row means. Keeping them apart is what lets the context menu's action
//  and the double click share the same answer about a row.
//
//========================================================================================
#ifndef __KESCMBookOpen_h__
#define __KESCMBookOpen_h__

#include "BaseType.h"

/** Remember / read which row the context menu was popped over.

    ***** A pop-up menu's action has no idea what was under the pointer. ***** The action fires
    later, from the menu, with nothing but its ActionID - so the row has to be recorded when the
    menu is raised and read back when the item is chosen. KBS's result rows work exactly this way
    (Check All / Uncheck All act on "the row the menu was popped over").

    -1 means "no row", which is what both the enabling test and the action treat as nothing to do. */
void	KESCMBookSetMenuRow(int32 rowIndex);
int32	KESCMBookMenuRow();

/** Can a comparison be started from this row? ★kTrue for a CHANGED chapter only
    (user's call, 2026-08-12) - and, being changed, one that names both of its files.

    The item answers "where did it change?", so it is offered only where that question has an answer:
    a one-sided chapter has nothing to compare, NoChange would draw no marks, and Failed /
    NotCompared were never judged. ⚠ InDesign shows no menu at all when every item in it is
    disabled (measured), so on those rows a right click produces nothing - which is what was asked
    for. */
bool16	KESCMBookRowCanStart(int32 rowIndex);

/** Open the chapter this row is about - BOTH sides - in windows.

    ★A row of this list is a statement about a PAIR ("this chapter changed"), so both halves are
    opened (user's call, 2026-08-12). A chapter that exists on one side only has one side to open,
    and that is not a failure. The TARGET is opened last, so it is the one left in front. */
void	KESCMBookOpenChapterForRow(int32 rowIndex);

/** Open both sides of this chapter and run the document comparison on them.

    Target = the target book's chapter, Source = the source book's chapter, stated explicitly rather
    than resolved from what happens to be in front (KESCMStartComparisonFor takes them as arguments
    for this reason). ⚠ A comparison already running is STOPPED first - its marks belong to a
    different pair of documents, and leaving them up while arming a new pair is the one state this
    plug-in must never be in. */
void	KESCMBookStartComparisonForRow(int32 rowIndex);

#endif // __KESCMBookOpen_h__

// End, KESCMBookOpen.h.
