//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  Two lists of definitions in, one list of differences out.
//
//  ★THE KEY IS DECIDED HERE AND NOWHERE ELSE. KCMResourceParse gathers the raw materials -- the
//  Self attribute, the Name attribute, and where an item sits among its own kind -- and stops
//  there on purpose. A key worked out in two files is one question answered twice, and the two
//  answers drift; that shape is behind seven of this project's real bugs.
//
//  ★★WHY A KEY IS NEEDED AT ALL. The two documents are SEPARATELY BUILT, so their UIDs have
//  nothing to do with one another: "ueb" in one file and "ueb" in the other are not the same
//  layer, and one layer can be "ueb" here and "u10f" there. What makes this mode possible is that
//  nearly everything a document DEFINES is named, and InDesign writes the name into the XML it
//  exports -- "Color/Black", "Ink/$ID/Process Cyan", "ParagraphStyle/$ID/NormalParagraphStyle".
//  Measured on a real document (2026-09-09): of the 83 kinds this mode looks at, 53 occur exactly
//  once (so the element name identifies them), 16 carry a name-shaped Self, and only Layer,
//  Section, Assignment and CrossReferenceFormat carry an opaque UID.
//
//  ★★★WHICH WAY THE TEST LEANS, AND WHY THAT IS THE DESIGN. Mistaking a NAME for a UID costs
//  almost nothing: both documents are read by the same rule, so the two items still meet -- on
//  their Name, or failing that on their position. Mistaking a UID for a NAME costs everything:
//  no two separately built documents ever share one, so every such definition comes out as Added
//  AND Removed at once and the mode reads as broken. The two mistakes are not symmetrical, so the
//  test is not either: KCMIsOpaqueSelf leans towards "opaque".
//
//========================================================================================
#ifndef __KCMResourceDiff_h__
#define __KCMResourceDiff_h__

#include "K2Vector.h"
#include "PMString.h"

#include "KCMResourceKinds.h"	// KCMResourceChangeKind. ★A types-only header, because the UI half
								// reaches this enum through IKCMResourcesFacade.h and must not be
								// able to see the model-side functions declared below.
#include "KCMResourceParse.h"	// KCMResourceItem / KCMResourceList - what is compared

/** One difference, as the panel will eventually show it. */
struct KCMResourceChange
{
	/** The element name: "ParagraphStyle", "Color", "Layer"... */
	PMString				fKind;
	/** The key both sides were paired on. What a reader sees as the definition's identity. */
	PMString				fKey;
	KCMResourceChangeKind	fWhat;
	/** The Source (older) side's text. Empty for kKCMResourceAdded.
	    ★It is kept rather than recomputed because the design puts the SOURCE value on the panel:
	    the Target value can be read off the document in front of the user, and the Source value
	    cannot be read anywhere else at all. */
	PMString				fSourceBody;
	/** The Target (newer) side's text. Empty for kKCMResourceRemoved. */
	PMString				fTargetBody;
};

typedef K2Vector<KCMResourceChange> KCMResourceChangeList;

/** What the comparison saw on the way, for the status line and the reading port.

    ★Four more cells stood here from 2026-09-09 to 2026-09-12 - the (bodies agree or not) x
    (StyleUniqueId agrees or not) grid that answered design §8-2, "can StyleUniqueId be used as a
    sieve?". The answer (one way only: a differing id proves nothing, measured `agree-other-id 1`)
    is written at the head of KCMResourceAttrDiff.h, and a measurement whose answer is written down
    does not need to be taken again on every comparison. */
struct KCMResourceDiffStats
{
	int32	fSourceItems;
	int32	fTargetItems;
	int32	fPaired;		// items that found a counterpart by key
	int32	fAdded;
	int32	fRemoved;
	int32	fChanged;

	KCMResourceDiffStats()
		: fSourceItems(0), fTargetItems(0), fPaired(0), fAdded(0), fRemoved(0), fChanged(0) {}
};

/** What pairs `item` with its counterpart in the other document.

    ⚠The "is this a UID?" test it leans on is KCMIsOpaqueSelf, declared in KCMResourceParse.h -
    the parse needs the same test to keep UIDs out of the bodies being compared, and one question
    gets one answer in one place.

    Three shapes, in the order they are tried:
      [A] no Self          -> the element name (53 kinds occur exactly once)
      [B] a named Self     -> the Self itself, which two documents genuinely share
      [C] an opaque Self   -> the kind plus the Name, or plus the position when there is no Name
                              (Section's Name can be empty) */
PMString KCMResourceKeyOf(const KCMResourceItem& item);

/** Pairs the two lists by key and reports what differs.

    ⚠★★**A RENAMED DEFINITION COMES OUT AS Added PLUS Removed** (2026-09-09, the user's decision
    after the measurements: "when you rename it, Add and Remove - that cannot be helped"). The key
    is built from the name, so a rename breaks the pair by construction. A pass on StyleUniqueId
    stood here for an afternoon and was taken out again; **the three readings that killed it are at
    the head of KCMResourceAttrDiff.h** and are worth more than the code was.

    ★**A NAME IS UNIQUE EVEN INSIDE STYLE GROUPS**, so the key does not collide (the user read it
    out of the XML: a style in a group is written `Name="スタイルグループ 1:段落スタイル 1"` - the
    group is part of the name). Two styles of the same short name in different groups pair
    correctly.

    @param source  the older document's definitions.
    @param target  the newer document's definitions.
    @param out     receives one entry per difference, Target order first, then what only the
                   Source had. Emptied first.
    @param stats   receives the counts.

    @return kFalse when a container could not grow. ⚠`out` is then EMPTY rather than partial: a
            half-built list would read as "these definitions were removed" when nothing was, and
            a wrong answer that looks like a real one is worse than no answer. Same refusal
            KCMParseResources makes for the same reason.

    ⚠It reports nothing about WHY two bodies differ - only that they do. Naming the attribute
    that moved is the panel's job (design §6), and it is deliberately not done here: this file
    would then have to parse the XML a second time, in a second place, differently. */
bool16 KCMDiffResources(const KCMResourceList& source, const KCMResourceList& target,
						KCMResourceChangeList& out, KCMResourceDiffStats& stats);

/** Compares the two documents a comparison is armed on and says what came out, for
    app.kcmResourceDiff: a summary line, a header line, and one tab-separated line per difference.

    ★It reads the SAME TWO DOCUMENTS THE PANEL DOES (KCMArmedTargetDB / KCMArmedSourceDB) rather
    than, say, the first two open documents. That is what makes a reading here mean something
    about the product rather than about the test: the Target/Source pair is chosen by the same
    Set as Target / Set as Source the user presses, and the answer moves when that choice moves.

    ⚠A header line comes back even when nothing differs. An empty answer and a missing property
    have to read differently (the second is ERR:55), and "no differences" is a real result. */
void KCMDescribeResourceDiff(PMString& out);

#endif // __KCMResourceDiff_h__

// End, KCMResourceDiff.h.
