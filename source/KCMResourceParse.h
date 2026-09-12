//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  The XML from KCMResourceSnapshot, cut down to the things this mode compares.
//
//  ★THE RULE IS A BLACKLIST, NOT A WHITELIST (user's decision, 2026-09-09). Everything directly
//  under <Document> is compared EXCEPT the four kinds named in KCMIsExcludedResource. The reason
//  is coverage: a whitelist silently loses whatever nobody remembered to list, and a blacklist
//  can only ever show something twice. Measured -- the first whitelist draft ("styles, swatches
//  and layers") would have looked at 3 of the 83 kinds that are actually there, losing kinsoku,
//  mojikumi, text variables, cross references, index options, tables of contents, trap presets,
//  numbering lists, named grids and 41 document preferences.
//
//  ★THE EXCLUSIONS ARE NOT A JUDGEMENT CALL EITHER: they are what the other two modes already
//  see. Adobe's own split agrees. IDML files a document into Spreads/ (which Pixel compares),
//  Stories/ (which Story compares) and Resources/ + designmap.xml (which is this mode), and the
//  87 kinds under <Document> divide between them with nothing left over:
//      Resources/Preferences.xml  41      Spreads/ + MasterSpreads/   2   <- Pixel
//      Resources/Styles.xml        7      Stories/                    1   <- Story
//      Resources/Graphic.xml       6      XML/BackingStory.xml        1   <- ours (see below)
//      Resources/Fonts.xml         2      XML/Tags.xml                1   <- ours
//      designmap.xml              25      (not in IDML at all)        1   <- the XMP packet
//
//  ★XmlStory IS OURS despite the name (user's decision, 2026-09-09). It is the backing store --
//  the invisible story every document has, holding XML elements that are NOT PLACED on a page,
//  plus the document element, the DTD, comments and processing instructions. Story mode compares
//  placed body text, so none of that is anywhere in its view.
//
//  ★THE XMP PACKET IS NOT OURS. 91.6% of it is the two 512x512 thumbnails InDesign's home screen
//  shows; the rest is timestamps, UUIDs, and summaries the XML already carries in full (measured:
//  its Colorants list held 3 of the document's 14 colours).
//
//========================================================================================
#ifndef __KCMResourceParse_h__
#define __KCMResourceParse_h__

#include "K2Vector.h"
#include "PMString.h"

class KCMResourceBytes;

/** One definition, as the comparison sees it. */
struct KCMResourceItem
{
	/** The element name: "ParagraphStyle", "Color", "Layer", "DocumentPreference"... */
	PMString	fKind;

	/** The Self attribute exactly as the XML carried it, or empty when the element has none.

	    ⚠IT IS NOT THE KEY, and the distinction is the whole reason this field is raw. What pairs
	    an item with its counterpart in the other document is decided in ONE place --
	    KCMResourceKeyOf, in KCMResourceDiff.h -- because Self alone is the wrong answer for the
	    handful of kinds whose Self is an opaque UID, which two separately built documents can
	    never share. Deciding it here as well would be the same question answered in two files,
	    which is the shape that produced seven real bugs in this project already. This file
	    supplies the materials; the key is made from them there. */
	PMString	fSelf;

	/** The Name attribute, or empty when the element has none.

	    ★It is what pairs the kinds whose Self is an opaque UID (measured 2026-09-09: Layer,
	    Section, Assignment and CrossReferenceFormat are the only ones), so it is collected for
	    every element rather than for those four -- a list of kinds would be a whitelist, and the
	    next document is allowed to have a kind this one did not. */
	PMString	fName;

	/** Which one this is among the items of the SAME kind, counting from 0.

	    The last resort for a key: Section's Name can be empty, and "the nth section" is then the
	    closest thing to a pair there is.

	    ⚠The order is the order items CLOSE in, not the order they open in, because items nest: a
	    style is filed before the style group that contains it. It pairs two documents correctly
	    all the same -- both are read by this same rule -- but it is not "document order" and
	    should not be described as such. */
	int32		fOrdinal;

	/** The element's attributes and children, flattened back to text. This is what "Changed" is
	    decided on. ⚠Deliberately NOT the byte count: measured, editing a style's pointSize from
	    24Q to 48Q left the document's size unchanged to the byte, because the two numbers happen
	    to render the same length. A size is not a fingerprint.

	    ⚠StyleUniqueId is KEPT OUT of this: InDesign REISSUES IT ON EVERY EDIT (a pure rename
	    included), so left in, two styles with identical settings would compare as different and
	    every edited style would report a change even after it was edited back. It was held in a
	    field of its own from 2026-09-09 to 2026-09-12 to measure whether it could serve as a cheap
	    sieve; the answer - one way only - is at the head of KCMResourceAttrDiff.h, and the field
	    went with the measurement. */
	PMString	fBody;
};

typedef K2Vector<KCMResourceItem> KCMResourceList;

/** kTrue when `value` is one of InDesign's opaque UIDs ("ueb", "u13f") rather than a name.

    ★★A UID IS NOT CONTENT, and this is the test that keeps it out of one. Two separately built
    documents never share a UID, so any UID left in an item's body makes that item differ every
    single time. Measured 2026-09-09: two layers of the same name, added the same way to the two
    documents, came back as Changed because one was "u13c" and the other "u13f" - and the document
    element came back as Changed because its ActiveLayer pointed at them.

    ⚠The test is on the VALUE'S SHAPE, never on the attribute's name. A list of attribute names
    would be a whitelist, and the next document is allowed to hold a reference this one did not.

    ⚠It leans towards "opaque" on purpose; the reasoning is in KCMResourceDiff.h, where the same
    test decides how an item is paired.

    ★★TWO SHAPES ANSWER YES, not one (the second was added 2026-09-10): a bare UID ("u13f"), and
    A UID WITH A SUFFIX ("u10aGradientStop0", "u18ColorGroupSwatch0", "ua8BuildingBlock0"). The
    second was read as a name until it was measured, which made every element carrying one report
    as Added and Removed at the same time in any pair whose UIDs had drifted apart. The full
    account, and why the control run could not see it, is at the head of the function. */
bool16 KCMIsOpaqueSelf(const PMString& value);

/** kTrue when `value` is a UID, or a whitespace-separated list of nothing but UIDs.

    ★The list form is not a refinement of the above but a second measured case: the document
    element's StoryList reads "u100 u119 ud0", and treated as a name it made <Document> differ
    between any two documents that have ever existed. */
bool16 KCMIsOpaqueReference(const PMString& value);

/** The element names this mode does NOT look at, because another mode does.

    ⚠Adding a name here makes the mode blind to something, and nothing will report the loss.
    Removing one only makes a change show up in two places. When in doubt, do not add.

    ⚠★★Spread and MasterSpread are NOT in here, and that is not an oversight - they are handled
    by the three below instead, which is a filter rather than a skip. */
bool16 KCMIsExcludedResource(const PMString& elementName);

/** The two subtrees that hold page items. They are not compared as items of their own: what they
    carry is geometry, and geometry is what Pixel photographs. */
bool16 KCMIsSpreadContainer(const PMString& elementName);

/** Elements inside a spread that carry a Self but are not page items - the spread itself, a
    <Page> (whose Name is the folio, and folios belong to <Section>), the flattener preference. */
bool16 KCMIsSpreadStructureElement(const PMString& elementName);

/** ★★★WHAT IS TAKEN FROM A PAGE ITEM, and it is deliberately almost nothing (2026-09-10, the
    user's request: "I want to see a page item's lock change too").

    A page item carries forty-odd attributes and nearly all of them are drawn - position, size,
    colour, applied style. A rendered page SHOWS those, so Pixel owns them, and taking them here
    would put a second row against every object somebody moved. What is left is the short list of
    things that are true of an object without being visible in it.

    ⚠The script label is not an attribute and is not decided here: it lives in a <Label> subtree
    and is collected separately, as a pseudo-attribute named "ScriptLabel.<key>".
    ⚠Nonprinting is deliberately absent: switching it changes what the plate carries, so the
    pixels move and Pixel reports it. */
bool16 KCMPageItemAttributeWanted(const PMString& attributeName);

/** Cuts `xml` into one KCMResourceItem per definition.

    ★A definition is an element directly under <Document> OR any element carrying a Self, however
    deep. The second half matters: the styles hang inside RootParagraphStyleGroup and its four
    siblings rather than under <Document>, so without it a new paragraph style comes back as "the
    root style group changed" and ten edited styles come back as that same single line. Measured
    on 2026-09-09; the full argument is at the head of the .cpp.

    @param xml     the bytes KCMTakeResourceSnapshot produced.
    @param out     receives the items, in the order they finish (a nested item before the one
                   containing it). Emptied first.
    @param whyNot  on kFalse, a short English reason.
    @return kTrue when the XML parsed. ⚠An EMPTY list with kTrue cannot happen for a real
            document (every document carries dozens of preferences), so a caller that sees one
            should treat it as a fault rather than as "nothing to compare". */
bool16 KCMParseResources(const KCMResourceBytes& xml, KCMResourceList& out, PMString& whyNot);

#endif // __KCMResourceParse_h__

// End, KCMResourceParse.h.
