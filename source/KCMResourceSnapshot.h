//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  One document in, one XML document out, held in memory. Nothing is written to disk.
//
//  ★IINXManager::ExportINX IS CALLED HERE AND NOWHERE ELSE. The SDK contains not one caller of
//  it -- the declaration in IINXManager.h is the only occurrence in the whole source tree -- so
//  everything below was established by measurement rather than read
//  (docs/ai-notes/inx-document-as-xml-2026-09-09.md). Three of those findings are not guessable:
//
//    1. A POLICY IS REQUIRED. Passing nil crashes InDesign (EXCEPTION_ACCESS_VIOLATION inside
//       ExportINX). The policy that yields the WHOLE document is kActionExportPolicyBoss --
//       named for actions, which no amount of reading the name would have suggested. It was
//       found by trying all fourteen bosses that implement IID_IINXEXPORTPOLICY.
//
//    2. docElement->Reset() MUST BE CALLED FIRST. Without it an unsaved edit does not appear in
//       the export at all (IDOMElement.h:52-56 -- "DOM elements CACHE INFORMATION during use").
//       With it, the CURRENT state comes out without saving, which is what lets a document that
//       is being edited be compared at all.
//
//    3. IINXExportPolicy is FORWARD-DECLARED ONLY in the SDK, so it cannot be held in an
//       InterfacePtr of its own type. The C cast below is the product's own idiom for this
//       (open/components/incopyimport/import/InCopyImportProvider.cpp:413).
//
//  Measured cost: 376,004 bytes in 78-125ms for a four-page document, with ZERO bytes written to
//  disk. Two exports of the same unchanged document are byte-for-byte identical, even across
//  sessions -- there is no timestamp or counter in the output that moves on its own.
//
//========================================================================================
#ifndef __KCMResourceSnapshot_h__
#define __KCMResourceSnapshot_h__

#include "PMString.h"

class IDocument;
class KCMResourceBytes;

/** Writes doc's whole structure into `out` as one XML document, in memory.

    The document is only read. An export is a read, no command is issued, and the document is not
    dirtied -- which is what makes this safe to call on a document the user is editing.

    @param doc     the document to photograph. Its UNSAVED state is what comes out.
    @param out     receives the bytes. Emptied first.
    @param whyNot  on kFalse, a short English reason naming the step that failed. "0 bytes" and
                   "never ran" must never share a word, so each failure says which one it was.
    @return kTrue when a whole export was produced. kFalse means there is nothing usable in
            `out` -- a short export is a failure, not a smaller document. */
bool16 KCMTakeResourceSnapshot(IDocument* doc, KCMResourceBytes& out, PMString& whyNot);

/** Takes a snapshot of the ACTIVE document and says what came out, in one line, for
    app.kcmResourceSnapshot: "376004 bytes, 78 ms" or "FAILED: <which step>".

    It lives here rather than in the ScriptProvider because deciding what a snapshot IS -- and
    therefore what is worth saying about one -- belongs with the snapshot. */
void KCMDescribeResourceSnapshot(PMString& out);

#endif // __KCMResourceSnapshot_h__

// End, KCMResourceSnapshot.h.
