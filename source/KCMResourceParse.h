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

	/** What pairs this item with its counterpart in the OTHER document.

	    Measured on real documents (2026-09-09), keys fall into three groups:
	      [A] 53 kinds occur exactly once and carry no Self at all (DocumentPreference,
	          MarginPreference, ViewPreference...) -> the element name IS the key.
	      [B] 16 kinds carry a name-shaped Self ("Color/Black", "Ink/$ID/Process Cyan",
	          "ParagraphStyle/$ID/NormalParagraphStyle") -> Self is the key, and it matches
	          across documents built separately. That is what makes this mode possible at all.
	      [C] the rest. A Self beginning with 'd' is also name-based ("dABullet0",
	          "dTextVariablen...TV XRefChapterNumber") so it behaves like [B]. Genuinely opaque
	          UIDs are only Layer, Section, Assignment and CrossReferenceFormat. */
	PMString	fKey;

	/** The element's attributes and children, flattened back to text. This is what "Changed" is
	    decided on. ⚠Deliberately NOT the byte count: measured, editing a style's pointSize from
	    24Q to 48Q left the document's size unchanged to the byte, because the two numbers happen
	    to render the same length. A size is not a fingerprint.

	    ⚠StyleUniqueId is KEPT OUT of this (see fUniqueId). */
	PMString	fBody;

	/** The StyleUniqueId attribute, held apart from fBody. Empty when the element has none.

	    ★It is kept out of fBody because InDesign REISSUES IT ON EVERY EDIT: left in, two styles
	    with identical settings would compare as different, and every edited style would report a
	    change even after it was edited back. Held apart, it becomes useful instead of harmful --
	    a cheap sieve saying "this definition was edited at some point", the same role
	    ITextModel::GetChangeCount plays for a story.
	    ⚠"Same id therefore same contents" is NOT verified. Use it to shortlist, never to
	    conclude that two definitions agree. */
	PMString	fUniqueId;
};

typedef K2Vector<KCMResourceItem> KCMResourceList;

/** The element names this mode does NOT look at, because another mode does.

    ⚠Adding a name here makes the mode blind to something, and nothing will report the loss.
    Removing one only makes a change show up in two places. When in doubt, do not add. */
bool16 KCMIsExcludedResource(const PMString& elementName);

/** Cuts `xml` into one KCMResourceItem per definition directly under <Document>.

    @param xml     the bytes KCMTakeResourceSnapshot produced.
    @param out     receives the items, in document order. Emptied first.
    @param whyNot  on kFalse, a short English reason.
    @return kTrue when the XML parsed. ⚠An EMPTY list with kTrue cannot happen for a real
            document (every document carries dozens of preferences), so a caller that sees one
            should treat it as a fault rather than as "nothing to compare". */
bool16 KCMParseResources(const KCMResourceBytes& xml, KCMResourceList& out, PMString& whyNot);

#endif // __KCMResourceParse_h__

// End, KCMResourceParse.h.
