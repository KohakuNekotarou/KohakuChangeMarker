//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the parts of a .docx, read out of the file
//
//  WHAT THIS IS FOR. KCMStoryDocx reads a story back from the PARTS of a package - plain byte
//  strings named word/document.xml and so on - and knows nothing of files or zips. This is the
//  one place that turns a .docx on disk into those parts, and it is the only piece of the docx
//  round trip that cannot be run outside InDesign: the container is opened with the SDK's own
//  IUCFPackageUtils, the library behind IDML.
//
//  ★**IUCFPackageUtils::OpenPackage(const IDFile&) + OpenStream**, the road the design names
//    (docs/superpowers/specs/2026-09-19-kcm-story-docx-roundtrip-design.md, section 5). Word saves
//    its entries deflated; the same library's script door (app.unpackageUCF) was measured to
//    inflate a Word-saved .docx byte for byte (docs/ai-notes/kcm-docx-probe-2026-09-19.md), and
//    the C++ door is measured in the stage-2 live test.
//  ⚠**NEVER UnPackageUCF**: that writes a folder nobody asked for, and a file made and deleted
//   behind the user's back is the thing they said no to (2026-09-14).
//  ⚠OpenPackage takes a FILE. A memory stream is refused by the library (measured; the IDML notes
//   say so), and nothing here needs one: the reader chose the file in a dialog.
//
//========================================================================================
#ifndef __KCMDocxPackage_h__
#define __KCMDocxPackage_h__

#include "BaseType.h"
#include "PMString.h"
#include "KCMZipStore.h"	// Entry - a part, named

#include <vector>

class IDFile;

/** The parts of a .docx this plug-in reads: word/document.xml (has to be there), word/footnotes.xml,
	word/styles.xml, and customXml/item1.xml .. item9.xml - each of the optional ones only when the
	package has it (FileExists is asked first).
	@return kFalse with a reason - the UCFErrorCode in words - when the package cannot be opened or
	  holds no document part. outParts is then empty. */
bool16 KCMReadDocxParts(const IDFile& file, std::vector<KCMZipStore::Entry>& outParts, PMString& whyNot);

#endif // __KCMDocxPackage_h__

// End, KCMDocxPackage.h.
