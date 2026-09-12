//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  What is done to the XML of a task-start copy BEFORE it is handed to ImportINX. Two things,
//  both measured on 2026-09-12 (docs/ai-notes/kcm-inx-rehydration-2026-09-12.md):
//
//   1. A SACRIFICIAL FIRST RANGE. ImportINX drops the first <ParagraphStyleRange> of a story
//      whole (13,106 characters came back as 6,298; a five-paragraph story lost exactly its first
//      paragraph), so a range that says KCMDUMMY is put first and is what goes.
//      ★WHAT IS ACTUALLY DROPPED (measured 2026-09-12 evening, 13 variants through a throwaway
//      app.kcmInxFileProbe; docs/ai-notes/kcm-inx-first-range-drop-cause-2026-09-12.md):
//      **EXACTLY ONE range per import - the SECOND text insertion in file order, counted ACROSS
//      stories, with an <XmlStory> counting as one insertion however many ranges it has.** The
//      whole-document export always puts the document's <XmlStory> (the XML backing store, one
//      range) before the stories, so the second insertion is the first range of the FIRST story
//      in file order - which is why it looked like "the first range of a story", and why a
//      document with a single range in a snippet-shaped file loses nothing. The XML is innocent:
//      the same bytes through ISnippetImport (the PI rewritten to type="snippet") come back
//      whole. It is the kDocElementImportBoss policy's doing; why it swallows the second
//      insertion is inside the application and was not pursued.
//      !NOT OF EVERY STORY - and now the count says why. That was measured on one-story
//      documents. With TWO stories one of them kept its sacrificial range (2026-09-12: the
//      copy came back "KCMDUMMY" + a return = 9 characters longer than the origin, and the shape
//      check refused it): only one range is ever lost, so only the first story's dummy goes. So
//      the injection is only half of the rule: after the import, a story whose first paragraph
//      is exactly the sacrificial token loses that paragraph (KCMRehydrate.cpp,
//      DeleteSurvivingDummies). ⚠The scheme stands on the XmlStory being exported BEFORE the
//      first story (measured on every export so far). Were it not, the loss would be the first
//      story's second range = its first REAL paragraph; the shape check would then refuse the
//      copy rather than hand a shortened one to the comparison.
//
//   2. A LABEL NAMING THE ORIGINAL UID. The import renumbers everything, and the Story mode pairs
//      stories by UID. <Story Self="ufe"> carries the old UID in its Self, so a script label
//      KcmOriginUid=ufe is written into the COPY, right after the open tag. The rehydrated story
//      then answers extractLabel("KcmOriginUid") with "ufe" while the user's document is untouched.
//      Spreads get the same label (the peek needs to find one spread).
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

/** Where the injected copy is written. Write returns kFalse when it could not keep the bytes. */
class KCMByteSink
{
public:
	virtual ~KCMByteSink() {}
	virtual bool16 Write(const char* bytes, size_t count) = 0;
};

/** The label key the rehydrated copy carries. The value is the element's Self ("ufe"). */
extern const char* const kKCMOriginUidLabelKey;

/** The words of the sacrificial first range: ONE token, so that a paragraph made of exactly this
    can be recognised after the import and deleted if the import left it standing (see 1. above).
    ★THE TOKEN IS MADE FRESH FOR EVERY REHYDRATION (the user's ask, 2026-09-12): this prefix
    followed by 32 hex digits of a random nonce (KCMRehydrate.cpp, NewSacrificialToken). A
    constant "KCMDUMMY" could be a paragraph of the reader's own - a document ABOUT this plug-in
    is the obvious one - and the deletion would then take a real paragraph (the shape check would
    refuse the copy, but the comparison would be lost). A nonce nobody has seen cannot be typed.
    The same string is handed to the injection and to the deletion, so the two cannot disagree. */
#define kKCMSacrificialPrefix "KCMDUMMY-"

/** Copy xml[0..size) into out with the two injections above.

    @param sacrificialText  the token of the sacrificial range (ASCII, non-empty, no XML
                            specials - the caller makes it from kKCMSacrificialPrefix + hex).
    @param outStories  how many <Story> elements were labelled (and given a sacrificial range).
    @param outSpreads  how many <Spread> elements were labelled.
    @return kTrue when the whole copy was written. kFalse when the sink refused, when the token is
            empty, or when a <Story or <Spread open tag has no closing '>' (malformed input). */
bool16 KCMInjectForRehydration(const char* xml, size_t size, const char* sacrificialText,
							   KCMByteSink& out, int32* outStories, int32* outSpreads);

/** "ufe" -> 0xfe. The Self of a story, spread or page is "u" + the UID in lower-case hex.
    @return kFalse for anything else ("d", "", "ug", "u"). */
bool16 KCMParseSelfUid(const char* text, size_t length, uint32& outUid);

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
