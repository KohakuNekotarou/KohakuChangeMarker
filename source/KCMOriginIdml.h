//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the origin, held as IDML
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

#endif // __KCMOriginIdml_h__

// End, KCMOriginIdml.h.
