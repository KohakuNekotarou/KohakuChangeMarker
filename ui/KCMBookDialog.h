//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  Book comparison: the dialog that shows the result.
//
//  ***** Modeless. ***** The user keeps working with the document while it is open, which is what
//  makes it possible to add "click a row to open that chapter" later - a modal dialog could never
//  offer that, because nothing outside it can be touched until it closes.
//
//  ⚠ A modeless dialog does NOT resize its panel: the window grows and the contents stay put.
//  Measured 2026-07-17 over three builds, with EVE at every layer and with kResizeDialogBoss. So
//  the view resource is laid out at a fixed, generous size and the dialog is created with
//  kDontAllowUserResize - a handle that does nothing is worse than no handle at all.
//
//========================================================================================
#ifndef __KCMBookDialog_h__
#define __KCMBookDialog_h__

#include "PMString.h"

#include <vector>

#include "KCMBookResult.h"	// KCMChapterResult - what the list holds

/** Open the book comparison dialog.

    Nothing here guards against a second copy appearing: IDialogMgr enforces "one at a time" for
    modeless dialogs whatever the allowMultipleCopies argument says (IDialogMgr.h:67). */
void KCMOpenBookDialog();

/** Hand the dialog everything it shows: which two books were compared, the summary, and the rows.

    ***** ONE call, made once, by the run that produced all of it. ***** The four things describe a
    single comparison, and the dialog paints them together when it opens (KCMBookRun.cpp calls
    this and then opens the dialog).

    ***** The paths are STORED, not looked up again. ***** Until 2026-08-12 the dialog re-resolved
    the front tab of the Book panel every time it opened, which meant the labels described the
    CURRENT tab while the rows below them described the run - two answers to "which books is this
    about?", and a modeless dialog gives the user all the time in the world to change the front tab
    between them. Storing what was actually compared makes the disagreement impossible rather than
    unlikely.

    ***** And it all outlives the dialog window. ***** Held in the module, not in the widgets, so
    that closing the dialog does not throw away a comparison that took real time to compute -
    reopening it (kCacheDialog) finds it still here.

    ⚠ IT ALSO DROPS THE ROW INDEX THE ROW MENU STASHED (KCMBookSetMenuRow(-1)). Replacing the rows
    is exactly the moment an index into them stops meaning anything, and one caller reads that index
    without going through the list at all - see the call for the measurement. Anything else that ever
    replaces or drops rows has to do the same. */
void KCMBookDialogSetResult(const PMString& targetPath, const PMString& sourcePath,
                              const PMString& summary,
                              const std::vector<KCMChapterResult>& rows);

/** What the chapter list is showing right now. Three readers, all of them in the list's own
    machinery: the hierarchy adapter (how many rows), the row widget manager (what is IN each row)
    and KCMBookOpen.cpp's RowAt (what a click on row N is about).
    ⚠ It said "the tree's hierarchy adapter reads this and nothing else" until 2026-08-18 (bug
      recheck B-U5) - the widget manager was there from the same day the adapter was.

    ⚠★THIS IS NOT THE WHOLE COMPARISON. Since 2026-08-13 the chapters that came back NoChange are
    left out of it at the user's request, so this holds the chapters WORTH LOOKING AT rather than all
    of them. Anything that needs every chapter must go to KCMGetBookResultText (which app.kcmBookResult
    returns) - that one is still built over the full set.
    ★Row indices therefore index THIS list, not the comparison's. The double click and the row's
      context menu both go through it (KCMBookOpen.cpp's RowAt), so they stay consistent by
      construction - but a new caller holding an index from the comparison would be pointing at the
      wrong chapter.
    ⚠ The count in the summary line is still the full one, which is what makes an empty list mean
      "nothing changed" rather than "nothing ran". */
const std::vector<KCMChapterResult>& KCMBookDialogRows();

/** Shutdown: empty the four things above, so the plug-in unloads with no live heap buffer in a
    static PMString. Called from KCMUIStartup::Shutdown (KCMUIStartup.cpp), alongside the other
    statics this half keeps. Touches no widget - the dialog is long gone by then - so its position
    among the teardown steps does not matter. */
void KCMBookDialogShutdown();

/* ⚠ KCMElidePathFront IS GONE (2026-08-15). It shortened a path in C++ - "...\New\a.indb" - for
   the alert and for these two lines, and nothing shortens them now: the whole path goes in and the
   WIDGET elides it, which is how the panel's Target:/Source: lines always worked (user's call the
   same day: "make the paths match the way the panel puts them out"). How a path is spelled is
   decided in one place, KCMPathDisplay.h.

   ★AND THE REASON IT USED TO BE NEEDED IS FIXED AT THE SOURCE. The dialog's width came from these
     lines: EVE treats the width in a .fr as a MINIMUM ("we treat the width in the .fr file as a
     minimum width" - Using EVE, Example 2) and resizes static text to fit its label, so a full path
     made the window 593px wide (measured) against the 400 its frames ask for. Handing the widget a
     pre-shortened string hid that; it did not fix it. The fix is kKCMBookPathTextWidgetBoss in
     KCMUI.fr - an EveInfo implementation that answers "the size the resource wrote" - after which
     the window measured 400px with the full path in it. */

#endif // __KCMBookDialog_h__

// End, KCMBookDialog.h.
