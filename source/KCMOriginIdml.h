//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a document's internal IDML (its designmap)
//
//  ★Mostly the ORIGIN's, which is what the file was written for; since 2026-09-20 the ACTIVE
//  document's too (KCMSaveActiveDocXml, at the foot of this header), so that the two can be
//  compared as XML rather than as four numbers. The steps are the same either way, which is the
//  reason they live together.
//
//  WHAT THIS IS FOR. Task Start keeps one snapshot of the document in memory (KCMOrigin). Until
//  2026-09-15 that was a bare INX - what IINXManager::ExportINX writes. It is now kept as a
//  DESIGNMAP instead: the same XML tree, with the two things that make it an IDML's designmap.xml.
//
//      <?aid ... type="action"   ...?>     ->   type="document"
//      <Document DOMVersion="21.0" ...>    ->   <Document xmlns:idPkg="...packaging" DOMVersion=...
//
//  ★★★THE TREE IS NOT TOUCHED. One word in a processing instruction, one attribute inserted; the
//  183,218 bytes between them are the bytes ExportINX wrote. It is a change of label, not of
//  content - which is why nothing that reads the snapshot had to change.
//
//  WHY BOTHER, when the content is identical either way: **the saved thing is then an IDML in its
//  own right.** InDesign's own IDML validation accepts it (kSaveBackImportValidationBoss answered
//  0 where the action validation answered 90370 - measured 2026-09-15), so it can be handed to an
//  outside tool, wrapped in a container, or kept, without a conversion step standing in between.
//  The user's stated use is "to build a diff outside InDesign and put it back".
//
//  ⚠★★★AND IT STILL GOES BACK THE SAME WAY. ImportINX takes the designmap AS IT STANDS - measured
//    (S24): err 0, and the document it builds is identical to the one an untouched INX builds
//    beyond its name. **No conversion on the way out, and none on the way back.**
//
//  WHAT WAS CHECKED BEFORE THIS WAS WRITTEN (mechanically, not by reasoning):
//    - the three readers of the snapshot key off ELEMENT NAMES only - "<Story ", "<Spread ",
//      "<Page ", "<DocumentPreference ", "<ParagraphStyleRange", "<Properties>", "<Label>",
//      "<Content>", "<KeyValuePair Key=" - and not one of them looks at the processing
//      instruction or at <Document>'s attributes. The rewrite cannot reach any of them.
//    - ValidateINX is called NOWHERE in the product (only in the spike), so the action
//      validation's refusal of a designmap (90370) never comes up on a live path.
//    - the Resources mode compares EVERY attribute of every element, so xmlns:idPkg WOULD have
//      shown up there: it is excluded in KCMResourceParse.cpp, beside the three exclusions that
//      were already there, with the measurement that says an INX carries no xmlns at all.
//
//========================================================================================
#ifndef __KCMOriginIdml_h__
#define __KCMOriginIdml_h__

#include "BaseType.h"
#include "PMString.h"

class IDFile;
class KCMResourceBytes;

/** Turn an INX into a designmap, in place.

    Rewrites the processing instruction's type and inserts the idPkg namespace on <Document>.
    Nothing else in the bytes is read or written.

    @param bytes  the snapshot, as ExportINX left it. Replaced by the designmap on success.
    @param whyNot when kFalse, which of the two anchors was not found.
    @return kFalse and `bytes` UNCHANGED when either anchor is missing - ★a snapshot that could
            not be labelled is still a perfectly good INX, and every reader of it keys off element
            names, so refusing to label it costs nothing and corrupting it would cost everything.
*/
bool16 KCMInxToDesignmap(KCMResourceBytes& bytes, PMString& whyNot);

/** Write the held origin out as a REAL IDML package - a file InDesign will open as a document.

    ★WHAT MAKES THIS THREE ENTRIES AND NOT THIRTEEN. A real IDML cuts the document into a
    designmap plus a dozen referenced parts, and the obvious reading is that the split is required.
    It is not: measured 2026-09-14, InDesign opens a package whose designmap.xml was never cut, and
    every element of the test document came back. So the package is

        mimetype                 (UCF writes it itself, from the mime argument - 43 bytes)
        META-INF/container.xml   (253 bytes, fixed, copied out of a real package byte for byte)
        designmap.xml            (the origin, which is already in that shape)

    ⚠And the split is not merely unnecessary - it is the thing that CANNOT be put back: handing
      ImportINX a designmap with <idPkg:* src="..."/> references crashes InDesign inside JBX.APLN,
      the plug-in that resolves them, because ImportINX takes one stream and has nowhere to fetch
      the other files from (measured 2026-09-15, report at work/kcm-crash-2026-09-14-s25.xml).
      ⇒ ★A package written here is one this plug-in can also READ BACK. A cut one would not be.

    ⚠createManifest is kFalse on purpose: a real IDML has no manifest.xml.

    @param file   where to write. The caller names it, as with KCMOriginSaveRaw.
    @param whyNot when non-zero, what failed.
    @return ★THE SAME STATUS NUMBERS KCMOriginSaveRaw USES, and for the same reasons - 0 written,
            1 no origin is held, 2 the file could not be created, 3 it could not be written. One
            scale, one place to read it, one set of numbers for the script side to test.
*/
int32 KCMOriginSaveIdml(const IDFile& file, PMString& whyNot);

/** Write the ACTIVE document's OWN internal IDML - its designmap - to a file.

    ★★**WHY (2026-09-20, the user's ask)**: "Task Start makes an internal IDML; the menu makes a
    document out of it as it is; can THAT document's internal IDML be made too? Then they can be
    compared." Until now only the origin could be written out (KCMOriginSaveRaw / KCMOriginSaveIdml),
    so a rehydrated copy could be checked only by its SHAPE - four numbers, spreads / pages /
    stories / text length. With both sides written as XML, **what the import dropped can be named
    element by element instead of counted.**

    ★**IT IS THE SAME TWO STEPS TASK START TAKES**, in the same order, which is what makes the two
    files the same KIND of thing and therefore comparable: KCMTakeResourceSnapshot (ExportINX into
    memory, nothing written to disk) and then KCMInxToDesignmap (one word in the processing
    instruction, one attribute on <Document>; the tree is not touched).

    ⚠★★★**THE TWO ARE NOT EXPECTED TO BE BYTE-IDENTICAL, AND THAT IS NOT A FAULT**: ImportINX
     RENUMBERS every UID it brings in - which is why a comparison copy has to carry its origin's uid
     in a <Properties><Label> at all - so `Self="ufe"` and everything that points at it differ on
     the two sides. ★**What is known to be stable**: two exports of the same unchanged document are
     byte-for-byte identical, even across sessions, because nothing in the output moves on its own
     (no timestamp, no counter - KCMResourceSnapshot.h). ⇒ **the noise is the renumbering and the
     document's own identity; anything else that differs is the import's doing.**

    ⚠**A designmap, not a package**: the bytes go out raw, the way KCMOriginSaveRaw writes the
     origin, because the point is to read and diff them. KCMOriginSaveIdml is the one that adds a
     container.

    @param file   where to write. The caller names it, as with KCMOriginSaveRaw.
    @param whyNot when non-zero, what failed.
    @return ★THE SAME STATUS SCALE KCMOriginSaveRaw USES - 0 written, 1 nothing could be
            photographed (no active document, or the export failed - whyNot says which), 2 the file
            could not be created, 3 it could not be written. ⚠4 ("the argument could not be read")
            is the script provider's, not this function's.
*/
int32 KCMSaveActiveDocXml(const IDFile& file, PMString& whyNot);

// (⛔"Open Task Start as IDML" was written here for an hour on 2026-09-20, writing the origin out
//  and opening the FILE. The user's decision moved it: the menu shows a REHYDRATION, not a file,
//  because the copy whose contents are in question is the one made in memory. It lives in
//  KCMRehydrate.h now.
//  ⚠★★And the rule it moved under has since changed once more, the same evening: it was "the copy
//   the comparison makes, sacrificial paragraphs left in", and it is now "the held bytes UNTOUCHED
//   - no injection at all, nothing written into the copy afterwards" (the user: "without the
//   sacrificial text - just make a document out of it doing nothing to it"). KCMRehydrate.h,
//   `untouched`, says what that skips. **Both decisions are the user's; neither is re-proposed.**)

#endif // __KCMOriginIdml_h__

// End, KCMOriginIdml.h.
