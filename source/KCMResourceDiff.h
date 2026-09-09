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

/** What the comparison saw on the way, for measurement rather than for display. */
struct KCMResourceDiffStats
{
	int32	fSourceItems;
	int32	fTargetItems;
	int32	fPaired;		// items that found a counterpart by key
	int32	fAdded;
	int32	fRemoved;
	int32	fChanged;
	/** ★Pairs that the KEY could not find and StyleUniqueId did - a definition that was RENAMED
	    (2026-09-09). Counted separately from fPaired because it says something the other numbers
	    cannot: how often the key, which is the whole basis of this mode, was not enough. */
	int32	fRenamed;

	// ----- ★the three numbers that answer "can StyleUniqueId be used as a sieve?" (design §8-2)
	//
	// One of them alone cannot: a sieve that is never tested against the thing it claims to
	// predict will always look right. These count the four combinations of (bodies agree or not)
	// x (ids agree or not), which is what makes the answer falsifiable.
	int32	fAgreeSameId;	// same body, same id     -- the sieve told the truth
	int32	fAgreeOtherId;	// same body, other id    -- the sieve cried wolf (wasteful, harmless)
	int32	fDifferSameId;	// ★DIFFERENT body, SAME id -- the sieve LIED. Anything but 0 here means
							//   it can never be used to skip a comparison, only to shortlist one.
	int32	fDifferOtherId;	// different body, other id -- the sieve working as intended.
							// ⚠It is counted even though it duplicates part of fChanged, because
							//   THREE OF FOUR IS NOT A MEASUREMENT: without this cell, "the sieve
							//   never lied" cannot be told apart from "the sieve never spoke".

	KCMResourceDiffStats()
		: fSourceItems(0), fTargetItems(0), fPaired(0), fAdded(0), fRemoved(0), fChanged(0),
		  fRenamed(0),
		  fAgreeSameId(0), fAgreeOtherId(0), fDifferSameId(0), fDifferOtherId(0) {}
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

    ★★★**THE ID IS ASKED BEFORE THE NAME** (2026-09-09, the user: "not by name - if there is a
    unique id, use that; paragraph ids to each other"). Two passes:
      1. StyleUniqueId, for the definitions that carry one
      2. the key, for everything left over

    ⇒ **A RENAME IS THEN ONE CHANGE, NOT TWO** (the report that started this: "renaming the style
    itself makes the display odd - it comes out split into an Add and a Remove"). The key is built
    from the name, so a rename breaks it by construction; the id does not move. Measured the same
    day on a real rename (`段落スタイル 1` -> `aaa`): the id was IDENTICAL on both sides
    (`c62347a5-b17a-4cc4-9410-0b0ef0e95adb`).
    ⚠**This corrects the note in KCMResourceParse.cpp saying the id is "reissued on every edit"** -
    a RENAME does not reissue it. That is what makes it usable here.

    ★★**AND THE NAME STILL ANSWERS EVERYTHING IT USED TO**, from pass 2: a style DELETED and
    RE-CREATED under the same name gets a new id, so pass 1 passes it over and the name pairs it -
    which is the reading a person wants there. The order was written the other way round first and
    changed after this case and the swap case below were worked through.
    ★**Only the id can tell two styles that SWAPPED names apart.** Name-first pairs each with the
    other's old self and reports two large content changes where there were two renames.

    ⚠**IT CANNOT PAIR EVERYTHING, AND THAT IS SAFE.** Only styles carry a StyleUniqueId; a renamed
    swatch or layer still comes out as Added plus Removed. And in two SEPARATELY BUILT documents no
    two ids ever agree, so pass 1 finds nothing there and the whole comparison falls through to the
    name - the one-way property the design calls a limitation (§8-2) is what makes it safe.

    @param source  the older document's definitions.
    @param target  the newer document's definitions.
    @param out     receives one entry per difference, Target order first, then what only the
                   Source had. Emptied first.
    @param stats   receives the counts, including the sieve's four combinations.

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
