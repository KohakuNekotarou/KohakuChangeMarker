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

#endif // __KESCMBookDialog_h__

// End, KESCMBookDialog.h.
