//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  Which of the three comparisons found something in a chapter, and the words for it.
//
//  A TYPES-AND-TEXT-ONLY HEADER, using no SDK type but uint32. Two reasons, and both are why
//  KCMStoryKinds.h exists in the same shape:
//    1. the UI half includes it -- the Change column is drawn there -- and a header the UI includes
//       must not put a model-side free function within its reach, which it could see and could not
//       link to;
//    2. the words can then be checked OUTSIDE InDesign. work/kcm-bookmodes-test builds this header
//       on its own and runs the table of cases, the same way KCMStoryRowFilter.h and
//       KCMResourceAttrDiff.h are checked.
//
//  ★THE BIT VALUES ARE PERMANENT. They are what the Change column and app.kcmBookResult are built
//  from, and a value that has shipped is a value a saved test script compares against. Add at the
//  end; never renumber. (Same rule IKCMStoryEditsFacade states for Row::fAttrKind, where 2 has now
//  shipped twice.)
//
//========================================================================================
#ifndef __KCMBookModeNames_h__
#define __KCMBookModeNames_h__

#include <string>

/** Which comparison. A chapter's result carries an OR of these. */
enum KCMBookCompareMode
{
	kKCMBookModeNone      = 0,
	kKCMBookModePixel     = 1 << 0,	// the pages, rasterised and compared pixel by pixel
	kKCMBookModeStory     = 1 << 1,	// the stories' change counters (KCMStoryStamp.h)
	kKCMBookModeResources = 1 << 2	// the document's definitions, through the XML export
};

/** The Change column's text: the modes that found something, in a FIXED order.

    ★THE ORDER IS ALWAYS Pixel, Story, Resources -- the order they are run in, and the order the
    user asked for. It does not follow the order the bits were set in, because a reader scanning a
    column of these has to be able to compare two rows at a glance.

    ⚠A MODE THAT COULD NOT BE JUDGED IS NAMED, with a '?' after it, and is never left out. That is
    the rule KCMBookResult.h states for kKCMChapterNotCompared, one level further down: "could not
    be processed" and "processed, and nothing had changed" must not share a word -- and leaving a
    failed mode out of this string is exactly making them share one, because the column would then
    read the same as it does for a mode that ran and found nothing.

    @param changed  OR of KCMBookCompareMode: the modes that found a difference.
    @param unjudged OR of KCMBookCompareMode: the modes that could not be judged.
    @return "" when both are 0. That is a real answer -- every mode ran and none found anything --
            and it is also what a chapter no mode was run on gets (Added, Deleted, NotCompared),
            which the chapter's own verdict says instead.
            std::string rather than PMString, so this file needs no SDK: the caller converts with
            SetUTF8String. Every character produced here is ASCII.
*/
inline std::string KCMBookModesString(uint32 changed, uint32 unjudged)
{
	static const struct { uint32 bit; const char* name; } kModes[] =
	{
		{ kKCMBookModePixel,     "Pixel"     },
		{ kKCMBookModeStory,     "Story"     },
		{ kKCMBookModeResources, "Resources" }
	};

	std::string out;
	for (size_t i = 0; i < sizeof(kModes) / sizeof(kModes[0]); ++i)
	{
		// Changed is asked FIRST, so a bit set in both comes out once, as changed. The two are
		// disjoint by construction -- a mode is judged or it is not -- so this only decides what
		// happens if that ever stops being true, and "Pixel Pixel?" would be the worse answer.
		const bool isChanged  = (changed & kModes[i].bit) != 0;
		const bool isUnjudged = !isChanged && (unjudged & kModes[i].bit) != 0;
		if (!isChanged && !isUnjudged)
			continue;

		if (!out.empty())
			out += " ";
		out += kModes[i].name;
		if (isUnjudged)
			out += "?";
	}
	return out;
}

#endif // __KCMBookModeNames_h__

// End, KCMBookModeNames.h.
