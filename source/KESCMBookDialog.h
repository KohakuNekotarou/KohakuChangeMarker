//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
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
#ifndef __KESCMBookDialog_h__
#define __KESCMBookDialog_h__

#include "PMString.h"

#include <vector>

#include "KESCMBookResult.h"	// KESCMChapterResult - what the list holds

/** Open the book comparison dialog.

    Nothing here guards against a second copy appearing: IDialogMgr enforces "one at a time" for
    modeless dialogs whatever the allowMultipleCopies argument says (IDialogMgr.h:67). */
void KESCMOpenBookDialog();

/** Hand the dialog everything it shows: which two books were compared, the summary, and the rows.

    ***** ONE call, made once, by the run that produced all of it. ***** The four things describe a
    single comparison, and the dialog paints them together when it opens (KESCMBookRun.cpp calls
    this and then opens the dialog).

    ***** The paths are STORED, not looked up again. ***** Until 2026-08-12 the dialog re-resolved
    the front tab of the Book panel every time it opened, which meant the labels described the
    CURRENT tab while the rows below them described the run - two answers to "which books is this
    about?", and a modeless dialog gives the user all the time in the world to change the front tab
    between them. Storing what was actually compared makes the disagreement impossible rather than
    unlikely.

    ***** And it all outlives the dialog window. ***** Held in the module, not in the widgets, so
    that closing the dialog does not throw away a comparison that took real time to compute -
    reopening it (kCacheDialog) finds it still here. */
void KESCMBookDialogSetResult(const PMString& targetPath, const PMString& sourcePath,
                              const PMString& summary,
                              const std::vector<KESCMChapterResult>& rows);

/** What the chapter list is showing right now. The tree's hierarchy adapter reads this and
    nothing else.

    ⚠★THIS IS NOT THE WHOLE COMPARISON. Since 2026-08-13 the chapters that came back NoChange are
    left out of it at the user's request, so this holds the chapters WORTH LOOKING AT rather than all
    of them. Anything that needs every chapter must go to KESCMGetBookResultText (which app.kcmBookResult
    returns) - that one is still built over the full set.
    ★Row indices therefore index THIS list, not the comparison's. The double click and the row's
      context menu both go through it (KESCMBookOpen.cpp's RowAt), so they stay consistent by
      construction - but a new caller holding an index from the comparison would be pointing at the
      wrong chapter.
    ⚠ The count in the summary line is still the full one, which is what makes an empty list mean
      "nothing changed" rather than "nothing ran". */
const std::vector<KESCMChapterResult>& KESCMBookDialogRows();

#endif // __KESCMBookDialog_h__

// End, KESCMBookDialog.h.
