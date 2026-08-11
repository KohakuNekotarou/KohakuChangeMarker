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

class IPanelControlData;

/** Open the book comparison dialog.

    Nothing here guards against a second copy appearing: IDialogMgr enforces "one at a time" for
    modeless dialogs whatever the allowMultipleCopies argument says (IDialogMgr.h:67). */
void KESCMOpenBookDialog();

/** Fill the Target/Source lines from the CURRENT front tab, and enable or disable Compare.

    Called when the dialog opens and again immediately before each comparison. ***** Both times,
    on purpose. ***** The dialog is modeless, so the user can change the front tab while it is
    open; refreshing just before the run is what guarantees that the names on screen and the books
    actually compared are the same two. */
void KESCMBookDialogUpdateTargets(IPanelControlData* panelData);

/** Put a line in the dialog's status area. */
void KESCMBookDialogSetStatus(IPanelControlData* panelData, const PMString& message);

/** What the chapter list is showing right now, and how to replace it.

    ***** The rows outlive the dialog window. ***** They are held in this module, not in the tree
    widget, so that closing the dialog does not throw away a comparison that took real time to
    compute - reopening it (kCacheDialog) finds them still here. The tree's hierarchy adapter reads
    this and nothing else.

    The comparison's summary line is kept separately (KESCMGetBookResultText). These are the same
    facts in a different shape: a list needs rows, and re-parsing a sentence back into rows would be
    two answers to one question. */
const std::vector<KESCMChapterResult>& KESCMBookDialogRows();
void KESCMBookDialogSetRows(const std::vector<KESCMChapterResult>& rows);

#endif // __KESCMBookDialog_h__

// End, KESCMBookDialog.h.
