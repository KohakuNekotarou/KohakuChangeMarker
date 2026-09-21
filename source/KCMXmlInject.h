//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  What is done to the XML of a task-start copy BEFORE it is handed to ImportINX. Two things:
//
//   1. A DUMMY STORY, AND A FRAME OF ITS OWN. ImportINX swallows EXACTLY ONE text insertion per
//      import - the SECOND in file order, counted across stories, with an <XmlStory> counting as
//      one insertion however many ranges it has (measured 2026-09-12, thirteen variants through a
//      throwaway file probe). The whole-document export always writes the document's own
//      <XmlStory> - the XML backing store - first, so the second insertion is THE FIRST <Story>
//      IN THE FILE, and that one is the reader's.
//      ★So a dummy story of one paragraph is written in front of it, with an ordinary frame far
//      out on the pasteboard, and IT is swallowed instead. Whatever survives of it is then
//      deleted outright - the FRAME is deleted, which takes the story with it (KCMRehydrate.cpp,
//      DeleteDummyStory; a deleted paragraph would leave an empty story standing, and the shape
//      check counts stories).
//      ★★★WHAT IT COSTS TO GET THIS WRONG (measured 2026-09-20, with nothing injected at all):
//      the story that takes the drop loses EVERYTHING IN IT. A document whose single story held a
//      nested table came back with **Table 2->0, Cell 8->0, Row 4->0, Column 4->0, Content 11->1**
//      - the skeleton of the table, not merely the words in its cells - while 292 OTHER element
//      names were identical. A story with no table in it fares better but not well: the
//      paragraph's container survives and its text does not.
//      ⚠★★★**THE SAME BYTES OPENED AS A .idml FILE LOSE NOTHING**, with a dummy or without one
//       (measured the same day, both ways, on the same nested-table origin). What drops the
//       insertion is ImportINX's own route - not the IDML, and not the bytes.
//      ⚠**FILE ORDER IS NOT THE ORDER THE STORIES WERE MADE IN** (measured 2026-09-20): a frame
//       created second came out first in the XML and was the one that lost its text. Nothing may
//       predict which story is at risk from how the document was built.
//      ⚠WHAT STOOD HERE UNTIL 2026-09-20 was a different scheme with the same purpose: a
//       sacrificial range at the head of EVERY story, plus a decoy <XmlStory> of two ranges in
//       front of the first one. It worked (measured 2026-09-19: cells came back whole), but it
//       left a forged backing store in the copy and a token paragraph in every story, and it
//       rested on an unexplained detail - a decoy of ONE range absorbed nothing. The user's
//       design replaced it: one ordinary story, one ordinary frame, deleted when it has done its
//       work. ⚠**The per-story sacrificial ranges went with it**, on the reasoning that a seat
//       which is already taken does not need a second occupant.
//      ⚠★★★**THAT REASONING WAS INCOMPLETE, AND 2026-09-21 MEASURED WHERE**: when the document
//       holds a table the import bites a SECOND time, on the first <Content> runs of the FIRST
//       table in file order (up to two), ONCE PER IMPORT - not per story, not per cell, not per
//       table. A document with no table came back whole; every document with one lost exactly
//       those runs. ⇒ The dummy story now carries a DECOY TABLE (kDecoyTable in the .cpp) so that
//       its table is the first one, and the second bite lands there. ★★★This is also what made a
//       nested table "vanish": it sat in the first cell of the first table, which is the bitten
//       position; moved into the second table it came back entire. Eleven documents, the rule and
//       the correction: docs/ai-notes/kcm-inx-roundtrip-and-nested-tables-2026-09-20.md §11.
//
//   2. A LABEL NAMING THE ORIGINAL UID. The import renumbers everything, and the Story mode pairs
//      stories by UID. <Story Self="ufe"> carries the old UID in its Self, so a script label
//      KcmOriginUid=ufe is written into the COPY, right after the open tag. The rehydrated story
//      then answers extractLabel("KcmOriginUid") with "ufe" while the user's document is untouched.
//      Spreads get the same label (the peek needs to find one spread). PAGES are labelled here
//      too (2026-09-13), but ⚠**the import drops every page label** (measured: all in the bytes,
//      none in the copy), so the copy's pages are named AFTER the import from the spread -> pages
//      table below (KCMCollectSpreadPages; the writer is KCMRehydrate.cpp, LabelCopyPages). That
//      is what lets a task-start copy pair by identity (KCMPagePairRule.h) like any other Source.
//      ⚠The element the new document was born with is reused by the import and loses its label
//      (measured on the first spread, 2026-09-12); the write-back matches that spread by position.
//
//  PURE FUNCTIONS over bytes. No SDK type but bool16/int32/uint32, so work/kcm-origin-test builds
//  them outside InDesign. Nothing here allocates: the caller supplies the sink (in the plug-in a
//  KCMResourceBytes, which never throws).
//
//========================================================================================
#ifndef __KCMXmlInject_h__
#define __KCMXmlInject_h__

#include "BaseType.h"
#include <stddef.h>
#include <string>
#include <vector>

/** Where the injected copy is written. Write returns kFalse when it could not keep the bytes. */
class KCMByteSink
{
public:
	virtual ~KCMByteSink() {}
	virtual bool16 Write(const char* bytes, size_t count) = 0;
};

/** The label key the rehydrated copy carries. The value is the element's Self ("ufe"). */
extern const char* const kKCMOriginUidLabelKey;

/** ★The label the DUMMY STORY carries (value "1"), and the only way to find it after the import.
    ⚠**The dummy is emptied by the import - that is what it is for** - so by the time anything
    looks for it, its token is gone and only the label is left (measured 2026-09-20: the copy came
    back with two stories, the reader's whole and the dummy's length 0). A deletion that looked for
    the words found nothing and left the dummy standing. */
extern const char* const kKCMDummyStoryLabelKey;

/** The words of the dummy story's one paragraph: ONE token, so that a story made of exactly this
    can be recognised after the import and deleted, frame and all (see 1. above).
    ★THE TOKEN IS MADE FRESH FOR EVERY REHYDRATION (the user's ask, 2026-09-12): 32 hex digits of
    a random nonce, and nothing else (KCMRehydrate.cpp, NewSacrificialToken).

    ⚠★★★THE NAME CAME OFF ON 2026-09-15 (the user's ask: "just the random part"), and the reason
    was already written here: a token with a WORD in it can be a paragraph of the reader's own. The
    earlier text said that about a constant "KCMDUMMY" and then kept "KCMDUMMY-" as a prefix, which
    left the same objection standing in a smaller form - a document ABOUT this plug-in is exactly
    where "KCMDUMMY-" gets typed. **32 hex digits nobody has seen cannot be typed at all**, so
    dropping the name makes the token strictly safer, not merely tidier.
    ★And nothing depended on the prefix: the deletion compares the WHOLE token character by
      character (KCMRehydrate.cpp, DeleteDummyStory), never its first nine characters.

    The same string is handed to the injection and to the deletion, so the two cannot disagree.

    ⚠There is no constant here any more. It was #define kKCMSacrificialPrefix "KCMDUMMY-"; emptying
      it would have left a macro that expands to nothing, which is a thing to read and wonder about
      rather than a thing that does work. The token is built in one place and that place is named
      above. */

/** Copy xml[0..size) into out with the two injections above.

    @param sacrificialText  the token of the dummy story's paragraph (ASCII, non-empty, no XML
                            specials - the caller makes it of hex digits: NewSacrificialToken).
    The dummy story (1. above) is written in front of the first <Story that has a Self, and its
    frame in front of the first </Spread> - after every page and page item that spread holds.
    @param outStories  how many <Story> elements were labelled. ⚠The dummy is not among them: it is
                       written, not labelled, and nothing counts it.
    @param outSpreads  how many <Spread> elements were labelled.
    @param outPages    (optional) how many <Page> elements were labelled - the pages of the master
                       spreads included, since a <Page is a <Page wherever it sits.
    @return kTrue when the whole copy was written. kFalse when the sink refused, when the token is
            empty, or when a <Story, <Spread or <Page open tag has no closing '>' (malformed input). */
bool16 KCMInjectForRehydration(const char* xml, size_t size, const char* sacrificialText,
							   KCMByteSink& out, int32* outStories, int32* outSpreads,
							   int32* outPages = nil);

/** One <Spread> of the origin's XML and the Self uids of its <Page> children, in document order.
    ⚠★★★WHY THIS EXISTS (measured 2026-09-13): ImportINX DROPS THE LABEL OF EVERY PAGE. The
    injection above labels the <Page> elements (the offline probe on a real file shows every one
    labelled), the copy comes back with its spreads and stories labelled - and not one page. So a
    copy's pages cannot name the origin's pages through the XML; they are named AFTER the import
    instead: this table, read off the very bytes that were imported, says which origin page stood
    at which index of which spread, and KCMRehydrate.cpp (LabelCopyPages) writes that as the
    page's KcmOriginUid label into the copy. The first spread, which the import reuses and leaves
    unlabelled, is matched by its position (the first <Spread> in the file). */
struct KCMXmlSpreadPages
{
	uint32				fSpread;	// the <Spread>'s Self uid
	std::vector<uint32>	fPages;		// its <Page> children's Self uids, in order
};

/** Collect the <Spread> elements and their <Page> children from xml[0..size). Pages of a
    <MasterSpread> are not collected (a <MasterSpread is not a <Spread, and its pages pair by
    name). A <Spread or <Page without a parsable Self is skipped; a <Page outside any <Spread is
    ignored. kFalse only for nil input. */
bool16 KCMCollectSpreadPages(const char* xml, size_t size, std::vector<KCMXmlSpreadPages>& out);

/** "ufe" -> 0xfe. The Self of a story, spread or page is "u" + the UID in lower-case hex.
    @return kFalse for anything else ("d", "", "ug", "u"). */
bool16 KCMParseSelfUid(const char* text, size_t length, uint32& outUid);

/** ★★★**COUNT EVERY ELEMENT NAME IN a AND IN b, AND REPORT THE NAMES WHOSE COUNTS DIFFER.**
    The round-trip check (KCMOriginIdml.h, KCMVerifyOriginRoundTrip) stands on this one function.

    ★★**WHY COUNTS, AND NOT A DIFF** (measured 2026-09-20). A textual diff of two designmaps is
    useless: **ImportINX renumbers every UID it brings in**, so `Self="ufe"` and everything pointing
    at it differ on the two sides of a perfectly good round trip. Counting element names sidesteps
    that entirely - **a UID is never an element name** - while still catching the failure that
    matters, which is things going MISSING. Measured on the day: an untouched copy of a document
    with a nested table came back with Table 2->0, Cell 8->0, Row 4->0, Column 4->0, Content 11->1,
    **and 292 other element names identical**. The two numbers that moved named the fault exactly.

    ⚠★★**ONE NAME IS EXCLUDED: `Language`.** A freshly created document carries 66 of them and a
    document opened from a file carries 1 (measured 2026-09-09, and again on 2026-09-20 where it
    accounted for +6,982 bytes of a copy that was otherwise whole). It reports how a document was
    OPENED, not what is in it - which is why the Resources mode drops it too. Leaving it in would
    make every single round trip report a difference, and a check that always fails is a check
    nobody reads.

    ⚠**WHAT THIS CANNOT SEE**: a value that CHANGED. Fourteen colours on each side pass even if one
    of them is a different colour, because both sides have fourteen <Color> elements. This is the
    cheap sieve - "did anything go missing" - and the Resources mode is the expensive one that
    compares definitions attribute by attribute. **Neither replaces the other.**

    ⚠**NO SDK TYPE CROSSES THIS LINE** (the head of this file says why: work/kcm-origin-test builds
    these functions with no application in the room). The caller turns the rows into words.

    @param outDiffs  one row per element name whose counts differ, ★**largest difference first**:
                     the biggest loss is what a reader needs in the first few words, and a status
                     line has to end somewhere. Emptied first.
    @param outSame   (optional) how many element names appear the same number of times on both.
    @return kTrue when outDiffs comes back EMPTY - every counted element name appears the same
            number of times on both sides. */
struct KCMElementCount
{
	std::string	fName;
	int32		fInA;
	int32		fInB;
};

bool16 KCMCompareElementCounts(const char* a, size_t aSize, const char* b, size_t bSize,
							   std::vector<KCMElementCount>& outDiffs, int32* outSame = nil);

/** What the origin's <DocumentPreference> says about the page setup - the part a document has to
    be CREATED with, because ImportINX does not apply it to a document that already exists.
    ★MEASURED 2026-09-12 evening: a non-facing origin came back as a facing copy, its single pages
    laid out as left-hand pages, so every frame (spread coordinates in the XML) sat half a page to
    the right of where it belonged - page 1 "changed" in a document nobody had touched, and the
    peek laid the wrong picture over the page. The copy is made with these values now
    (KCMRehydrate.cpp, NewDocumentLike), the way SDKLayoutHelper::CreateDocument makes one. */
struct KCMDocSetupFromXml
{
	double	fPageWidth;		// points; valid when fHasSize
	double	fPageHeight;
	bool16	fFacingPages;	// valid when fHasFacing
	int32	fBinding;		// ILayoutUtils' DocPageBinding: -1 default, 0 left-to-right, 1 right-to-left; valid when fHasBinding
	bool16	fHasSize;
	bool16	fHasFacing;
	bool16	fHasBinding;
	KCMDocSetupFromXml()
		: fPageWidth(0), fPageHeight(0), fFacingPages(kFalse), fBinding(-1),
		  fHasSize(kFalse), fHasFacing(kFalse), fHasBinding(kFalse) {}
};

/** Read the first <DocumentPreference ...> element's PageWidth / PageHeight / FacingPages /
    PageBinding into out. Attributes that are absent or unreadable leave their fHas* kFalse.
    @return kFalse when there is no <DocumentPreference element at all (out is then untouched). */
bool16 KCMReadDocumentPreference(const char* xml, size_t size, KCMDocSetupFromXml& out);

#endif // __KCMXmlInject_h__

// End, KCMXmlInject.h.
