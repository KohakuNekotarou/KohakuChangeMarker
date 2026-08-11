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

/** Open the book comparison dialog.

    Nothing here guards against a second copy appearing: IDialogMgr enforces "one at a time" for
    modeless dialogs whatever the allowMultipleCopies argument says (IDialogMgr.h:67). */
void KESCMOpenBookDialog();

#endif // __KESCMBookDialog_h__

// End, KESCMBookDialog.h.
