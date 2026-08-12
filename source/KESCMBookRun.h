//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: the command behind the flyout item. Confirm, run, show.
//
//  ***** WHY THIS FILE EXISTS (2026-08-12). ***** The comparison used to be started by a Compare
//  button inside the result dialog, so the dialog opened first and stood there empty until the
//  button was pressed. The user asked for the opposite order: choose the menu item, confirm the two
//  books in an alert, and get a dialog that already has the answer in it. That moved the whole
//  "which books, and does the user really mean it" step out of the dialog - and the dialog lost its
//  buttons with it (KESCM.fr).
//
//  The four files of this feature now divide cleanly:
//    KESCMBookPair     - WHICH two books (and which chapter pairs up with which)
//    KESCMBookCompare  - HOW they are compared
//    KESCMBookRun      - the COMMAND: confirm, run, hand the result over, open the dialog
//    KESCMBookDialog   - the VIEW: paints what it was handed
//
//========================================================================================
#ifndef __KESCMBookRun_h__
#define __KESCMBookRun_h__

/** The flyout item "Compare Books".

    Resolves the two books, asks the user to confirm them by full path, runs the comparison (with
    the progress bar and its Cancel), and opens the dialog on the result. Does nothing at all if the
    user answers no.

    Safe to call when the books cannot be resolved: it says so and stops. The menu item is greyed in
    that case anyway - both use KESCMResolveBookPair - but the grey state is decided when the menu
    is built and this runs when it is chosen, and the front tab can change in between. */
void KESCMRunBookComparison();

#endif // __KESCMBookRun_h__

// End, KESCMBookRun.h.
