//========================================================================================
//
//  KCMChangedPagesTSV.h
//
//  The way in to the flyout item "Export Changed Pages...". It writes the changed pages of the
//  current comparison (after Start) as tab-separated text in the same style as KESCL's "Save
//  Check Report" (UTF-8 + BOM + CRLF), so it can be pasted straight into Excel or Notepad.
//  Two columns = Page / Type (Changed / Inserted / Deleted, English throughout).
//
//  Where the data comes from -- all of it the "as compared" record KCMDrawEventHandler holds,
//  plus the manual registrations:
//    - Changed  = sEntries (pages whose changed-pixel count is > 0)
//    - Inserted = sOverflowT (no counterpart because the documents differ in page count,
//                 Target side) + manually registered "Added" pages
//    - Deleted  = sOverflowS (the same on the Source side) + manually registered "Removed" pages
//  @warning **those three lines were always written this way, but the implementation used to
//    call KCMBuildPairing and work the insertions and deletions out from the CURRENT document
//    structure.** The cached sets are frozen at the moment of the comparison, so unless the
//    user re-compared after adding pages, the list named pages as Inserted that carry no "/" on
//    screen. The implementation now matches this declaration: **the list is a copy of what the
//    screen says.**
//  Overset (sOverset*) is never consulted (the user's specification).
//
//  The implementation is in KCMChangedPagesTSV.cpp.
//
//========================================================================================
#ifndef __KCMChangedPagesTSV_h__
#define __KCMChangedPagesTSV_h__

// Save the current comparison's changed pages to a TSV file. With nothing started (sDB = nil),
// or with no changes, nothing is written and the reason comes back in outMessage.
//
// **The message is RETURNED, not posted.** A TSV export naturally answers with "did it work,
//   and where did it go", and has no reason to raise a notification; this half is the model,
//   and the caller (the flyout item "Export Changed Pages...", which is UI) does the showing.
//   **Success is silent** -- outMessage comes back empty, and only failures fill it in (KESCL's
//   convention). Called from the DoAction of kKCMPopupExportChangedPagesActionID.
void KCMExportChangedPagesTSV(PMString& outMessage);

// Shutdown only: empty the export message's file-static PMString, so the static destructor at
// plug-in unload is handed no live heap buffer (Mac's unload order differs from Windows').
// The shutdown service lists every static container it empties and says a PMString is the kind
// that must not be missed; this one holds the last export's line, including a full path, so
// after one run it always holds something. Idempotent -- just call it.
void KCMClearExportMessage();

#endif // __KCMChangedPagesTSV_h__
