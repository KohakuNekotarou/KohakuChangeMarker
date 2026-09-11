//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  What is done to the XML of a task-start copy BEFORE it is handed to ImportINX. Two things,
//  both measured on 2026-09-12 (docs/ai-notes/kcm-inx-rehydration-2026-09-12.md):
//
//   1. A SACRIFICIAL FIRST RANGE. ImportINX drops the first <ParagraphStyleRange> of every story
//      whole (13,106 characters came back as 6,298; a five-paragraph story lost exactly its first
//      paragraph). The rule is mechanical, so a range that says KCMDUMMY is put first and is all
//      that goes. Why it happens is not known; what is known is that this makes the text whole.
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

/** Copy xml[0..size) into out with the two injections above.

    @param outStories  how many <Story> elements were labelled (and given a sacrificial range).
    @param outSpreads  how many <Spread> elements were labelled.
    @return kTrue when the whole copy was written. kFalse when the sink refused, or when a
            <Story or <Spread open tag has no closing '>' (malformed input). */
bool16 KCMInjectForRehydration(const char* xml, size_t size, KCMByteSink& out,
							   int32* outStories, int32* outSpreads);

/** "ufe" -> 0xfe. The Self of a story, spread or page is "u" + the UID in lower-case hex.
    @return kFalse for anything else ("d", "", "ug", "u"). */
bool16 KCMParseSelfUid(const char* text, size_t length, uint32& outUid);

#endif // __KCMXmlInject_h__

// End, KCMXmlInject.h.
