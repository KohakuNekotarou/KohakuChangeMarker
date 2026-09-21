//========================================================================================
//
//  KCMXmlDeepCompare.h
//
//  ★★★THE EXPENSIVE HALF OF THE ROUND-TRIP CHECK: the origin's internal IDML against the copy's,
//  ELEMENT BY ELEMENT, ATTRIBUTE BY ATTRIBUTE, TEXT BY TEXT (2026-09-21, the user: "is it only the
//  count of the elements? I want the contents compared too, even if it takes longer").
//
//  THE CHEAP HALF IS KCMCompareElementCounts (KCMXmlInject.h): it answers "did anything go
//  MISSING", and it is blind to a value that CHANGED - fourteen <Color> elements on each side pass
//  even when one of them is a different colour. The frame settings, the cell strokes, the table
//  styles, the insets: none of those add or remove an element, so none of them can be seen there.
//  ⇒ **The two are one instrument in two passes**, and the words the panel shows say which pass
//    found what.
//
//  WHAT IS LEGITIMATELY DIFFERENT, and therefore classified rather than reported (the user, the
//  same day: "there will be parts that differ because of file names and so on"):
//    1. **UIDS.** ImportINX renumbers every one of them. A difference whose two values are both
//       uid-shaped (u, then hex, then any number of `i<hex>` / `Row<n>` / `Column<n>` tails) is not
//       a finding. ⚠This is the reason the cheap pass counts NAMES: a uid is never a name.
//    2. **OUR OWN LABELS** - KcmOriginUid and KcmDummyStory, which KCM writes into the copy on
//       purpose (KCMXmlInject.h).
//    3. **`Language`** - 66 in a document just created, 1 in one opened from a file. It reports how
//       a document was OPENED (KCMXmlInject.h says the rest).
//    4. **THE XMP PACKET** (<MetadataPacketPreference>), measured 2026-09-21 on the first real pair
//       this ran against: the copy is a document made seconds later, so its xmp:CreateDate and
//       xmp:ModifyDate differ - and the packet's own elements come back in ANOTHER ORDER
//       (xmp:CreatorTool against xmp:PageInfo), which stopped the walk at element 591 of a document
//       whose content had not been reached yet. It says when the document was made, never what is
//       in it, so the whole subtree is skipped on both sides and counted.
//    5. **<ViewPreference>** - the copy is a document the rehydration CREATED, so its ruler units
//       are the application's defaults, not the origin's (measured: Points against Millimeters on a
//       document whose units a script had set). It is a preference of the window, not of the page.
//    6. **AN ATTRIBUTE THE ORIGIN INHERITED AND THE IMPORT WROTE OUT** - measured 2026-09-21 on
//       every cell of every copy: `TopInset` / `LeftInset` / `BottomInset` / `RightInset` /
//       `ClipContentToCell` stand in the copy with the application's default value where the origin
//       has no attribute at all (40 of them in one 8-cell document). ⚠**ONLY when the origin has
//       NONE**: the same name with two different values IS a finding. ⚠And the value cannot be
//       verified - what the origin inherited is not written anywhere - so this forgives the shape
//       "absent against a default", not a number.
//    7. **A TEXT RUN OF NOTHING BUT U+FEFF** - the tag markers of the XML backing store, where the
//       copy comes back with one more than the origin (measured: one marker against two). A marker
//       is not a character the reader typed; a run with any other character in it is compared.
//    8. **<Page LayoutRule>** - measured 2026-09-21: every page the import CREATES comes back with
//       its Liquid Layout rule "Off" where the origin says "UseMaster" (a three-page document lost
//       it on pages 2 and 3; page 1 keeps it, because the new document's own first spread is
//       reused). It decides how items move when the page geometry changes, which never happens to a
//       comparison copy. ⚠Only "Off" in the copy is forgiven; any other value is a finding.
//
//  ★★★**AND THE STORIES ARE PAIRED BY LABEL, NOT BY POSITION.** The import writes the stories in its
//  own order - measured on a document with a master-page frame, where the copy's first story was the
//  reader's body text and the origin's was the running foot. Walking by position then compares two
//  different stories and reports their text as changed, which is the loudest possible way to be
//  wrong. KCM writes the original uid into each copied story (KcmOriginUid, KCMXmlInject.h), so the
//  copy's stories are put back into the origin's order before the walk begins, and the tally says
//  how many had to move.
//
//  ⚠★★**NOTHING IS SILENTLY IGNORED.** Every classified difference is COUNTED and the tally goes
//   in the answer, so "5 known differences (uids)" is visible and a sixth kind cannot hide inside
//   it. A list of names to forgive is exactly how a check starts lying, so this file grows one only
//   where a measurement is written beside it.
//
//  PURE FUNCTIONS over bytes, std types only - no SDK type crosses this line, so
//  work/kcm-origin-test builds it with no application in the room (the same rule as KCMXmlInject).
//
//========================================================================================
#ifndef __KCMXmlDeepCompare_h__
#define __KCMXmlDeepCompare_h__

// ⚠**BaseType.h FIRST, exactly as KCMXmlInject.h does it**: bool16 / int32 / nil come from there,
//  and outside InDesign they come from the stub of the same name (work/kcm-origin-test). Leaving it
//  out built inside the plug-in and failed outside it, where VCPlugInHeaders.h is empty - which is
//  how the offline test earns its keep.
#include "BaseType.h"
#include <stddef.h>
#include <string>
#include <vector>

/** Why a difference is not a finding. kKCMXmlDiffReal is the one that is. */
enum KCMXmlDiffKind
{
	kKCMXmlDiffReal = 0,		//!< report it
	kKCMXmlDiffUid,				//!< both values are uid-shaped: the import renumbers
	kKCMXmlDiffOurLabel,		//!< KcmOriginUid / KcmDummyStory - KCM wrote it
	kKCMXmlDiffLanguage,		//!< the <Language> element: how the document was opened
	kKCMXmlDiffMetadata,		//!< the XMP packet: when the document was made, not what is in it
	kKCMXmlDiffNewDocPref,		//!< <ViewPreference>: the copy is a new document, so these are app defaults
	kKCMXmlDiffImportDefault,	//!< an attribute the origin INHERITED and the import wrote out
	kKCMXmlDiffMarkerOnly,		//!< a text run of nothing but U+FEFF tag markers
	kKCMXmlDiffNewPage			//!< <Page LayoutRule>: a page the import created comes back "Off"
};

/** One difference: where it is, what differs, and the two values. */
struct KCMXmlDifference
{
	std::string		fPath;		//!< Document/Story[2]/ParagraphStyleRange[1]/CharacterStyleRange[1]/Content[1]
	std::string		fWhat;		//!< an attribute's name, or "(text)", or "(element)"
	std::string		fInA;		//!< the origin's value ("(absent)" when it has none)
	std::string		fInB;		//!< the copy's
	int32			fKind;		//!< KCMXmlDiffKind
};

/** What the pass looked at, so that "no differences" can be told from "nothing was compared". */
struct KCMXmlDeepTally
{
	KCMXmlDeepTally() : fElements(0), fAttributes(0), fTexts(0), fUids(0), fOurLabels(0),
						fLanguage(0), fMetadata(0), fNewDocPrefs(0), fImportDefaults(0),
						fMarkers(0), fNewPages(0), fStoriesReordered(0), fRealDiffs(0),
						fDivergedAt(-1) {}
	int32	fElements;			//!< elements walked in lockstep
	int32	fAttributes;		//!< attribute values compared
	int32	fTexts;				//!< text runs compared
	int32	fUids;				//!< classified: renumbered uids
	int32	fOurLabels;			//!< classified: our own labels
	int32	fLanguage;			//!< classified: <Language>
	int32	fMetadata;			//!< classified: elements of the XMP packet, skipped on both sides
	int32	fNewDocPrefs;		//!< classified: the new document's own view preferences
	int32	fImportDefaults;	//!< classified: inherited attributes the import wrote out explicitly
	int32	fMarkers;			//!< classified: tag-marker-only text runs
	int32	fNewPages;			//!< classified: the Liquid Layout rule of pages the import created
	int32	fStoriesReordered;	//!< how many stories had to be put back in the origin's order
	int32	fRealDiffs;			//!< real differences found (outDiffs may be capped; this is not)
	int32	fDivergedAt;		//!< element index where the two shapes stopped matching, or -1
	std::string fDivergedPath;	//!< and where that was, in words
};

/** ★**THE DEEP PASS.** Walks both documents in document order. While the element names line up it
    compares every attribute and every text run; the moment they stop lining up it records where and
    STOPS - the cheap pass (element-name counts) is what names a missing subtree, and a walk that
    tries to resynchronise after a structural difference invents differences that are not there.

    @param outDiffs  the REAL differences, in document order, capped at `maxDiffs` (the tally's
                     fRealDiffs counts them all). Emptied first.
    @param outTally  what was compared and what was classified. Emptied first.
    @param maxDiffs  how many rows to fill at most (0 = no cap). A status line ends somewhere.
    @return kTrue when there is not one real difference AND the two shapes never diverged. */
bool16 KCMCompareXmlDeep(const char* a, size_t aSize, const char* b, size_t bSize,
						 std::vector<KCMXmlDifference>& outDiffs, KCMXmlDeepTally& outTally,
						 int32 maxDiffs = 40);

/** Is this the shape ImportINX gives a renumbered uid? `u11a`, `u11ai132`, `u101i119Row0`.
    ★Exposed because the caller's tests want it, and because "what counts as a uid" is a judgement
    that belongs in one place. */
bool16 KCMLooksLikeUid(const std::string& value);

#endif // __KCMXmlDeepCompare_h__
