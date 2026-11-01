//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What is lit up, per document, and the one rule for putting two sets of it together.
//
//  **TWO KINDS OF MARK EXIST AND THEY MEET HERE.** A STANDING mark is what the "Always Show
//  Marks on Target / Source" toggles, "Print comparison marks" and the tool's held button put up; a FLASH
//  is the pointer a jump puts up for about a second (KCMStoryMarker). They are drawn by one adornment, which is
//  why they have to be reconciled before anything is drawn rather than while it is.
//
//  **THE RULE IS PER DOCUMENT, AND THAT IS THE WHOLE POINT OF THIS FILE:** in a document that
//  has a standing mark, the standing mark is all there is; in a document that has none, the
//  flash shows. Nothing is ever merged between the two.
//    * The two can never fight over the same characters, because within one document only one
//      of them is ever used -- the guarantee comes from the shape of the answer rather than from
//      a check somebody has to remember to write. What that buys is no ambiguity: a document
//      showing every edit is not also pointing at one of them.
//    * And the jump reaches the OTHER window: with "Always Show Marks on Target" on, the flash still goes up in the
//      SOURCE window, where nothing is standing and the reader has just asked to be shown something. One flag for
//      both documents ("is a standing mark showing?") would silence it.
//
//  **HEADER-ONLY AND FREE OF THE SDK EXCEPT FOR TWO TYPE NAMES (AND kMaxInt32), WHICH IS WHAT MAKES IT
//  TESTABLE.** IDataBase is never dereferenced here -- it is an address used as a key -- and UID
//  only has to sort. The test outside InDesign is work\kescm-markranges-test, which includes
//  this file as it stands rather than a copy that could drift.
//
//========================================================================================

#ifndef __KCMStoryMarkDocs_h__
#define __KCMStoryMarkDocs_h__

#include "OMTypes.h"		// UID
#include "KCMStoryMarkRanges.h"

#include <map>

class IDataBase;

/** Which characters of which stories are lit up in ONE document: story UID -> its ranges. */
typedef std::map<UID, KCMMarkRangeList> KCMStoryMarkMap;

/** The same thing for both compared documents at once: database -> what is lit up in it.

	**BOTH AT ONCE IS NOT A LUXURY** -- "Always Show Marks on Target" and "Always Show Marks on
	Source" can be on together, and then the newer document's edits and the older one's have to be
	up at the same time. */
typedef std::map<IDataBase*, KCMStoryMarkMap> KCMStoryMarkDocs;

/** Copy one document into the composed set, keeping only what would actually draw something.

	**THE EMPTINESS TEST IS THE WHOLE OF IT, AND IT IS ASKED TWICE:** a story whose list holds no
	range is left out, and a document all of whose stories were left out does not appear at all.
	What the caller tests to decide "this document is spoken for" would otherwise be answered by
	an entry that draws nothing.
	Both halves of the composition below call this. */
inline void KCMAddDrawableStories(KCMStoryMarkDocs& out, IDataBase* db,
								   const KCMStoryMarkMap& from)
{
	KCMStoryMarkMap kept;
	for (KCMStoryMarkMap::const_iterator it = from.begin(); it != from.end(); ++it)
	{
		if (it->second.empty())
			continue;
		kept[it->first] = it->second;
	}

	if (!kept.empty())
		out[db].swap(kept);
}

/** Put the standing marks and the flash together into the one set that gets drawn.

	A document that appears in `standing` is taken from there and its flash is dropped; a document
	that does not is taken from `flash`. Empty entries (a document whose map holds no story, or a
	story whose list holds no range) are not carried over - an entry that draws nothing would still
	make the set look occupied, which is what the caller tests to decide whether anything is up.

	@param standing what the toggles and a held button are holding up. May be empty.
	@param flash what a jump wants to show for a moment. May be empty.
	@param out receives the union under the rule above. Cleared first; may not alias either input.
*/
inline void KCMComposeMarkDocs(const KCMStoryMarkDocs& standing, const KCMStoryMarkDocs& flash,
								 KCMStoryMarkDocs& out)
{
	out.clear();

	for (KCMStoryMarkDocs::const_iterator doc = standing.begin(); doc != standing.end(); ++doc)
		KCMAddDrawableStories(out, doc->first, doc->second);

	for (KCMStoryMarkDocs::const_iterator doc = flash.begin(); doc != flash.end(); ++doc)
	{
		// @warning **ASKED OF WHAT SURVIVED, NOT OF THE STANDING SET.** A document that was consulted
		//   and found to have nothing standing in it is absent from `out`, so its flash still shows --
		//   which is the difference between "the toggle is on here" and "the toggle was looked at".
		if (out.find(doc->first) != out.end())
			continue;			// a standing mark is up in this document, and it wins whole

		KCMAddDrawableStories(out, doc->first, doc->second);
	}
}

/** How far a set reaches: the whole marked span across every story of every document, which is a free
	way to refuse most runs before asking any of them which story they belong to (that question costs a
	Query). The span of nothing refuses every run, so there is no state in which it means nothing. */
struct KCMMarkSpan
{
	TextIndex	fLowest;	// the first marked character anywhere
	TextIndex	fHighest;	// one past the last

	KCMMarkSpan() : fLowest(kMaxInt32), fHighest(0) {}

	/** Do the characters [runStart, runEnd) lie wholly before or after everything that is marked? */
	bool16 Misses(TextIndex runStart, TextIndex runEnd) const
	{
		return (runEnd <= fLowest || runStart >= fHighest) ? kTrue : kFalse;
	}
};

/** Put every story's list into the one shape the marker draws from (KCMMergeMarkRanges), and say how far
	the whole set reaches.

	What the merge buys is the cost of drawing (one fill per stretch instead of one per edit) and the sorted,
	non-overlapping list the binary searches rely on (KCMStoryMarkRanges.h).

	@param docs [in,out] each list rewritten in place. What goes in may overlap and be in any order.
	@return the span of what came out.
*/
inline KCMMarkSpan KCMMergeMarkDocs(KCMStoryMarkDocs& docs)
{
	KCMMarkSpan span;
	for (KCMStoryMarkDocs::iterator doc = docs.begin(); doc != docs.end(); ++doc)
	{
		for (KCMStoryMarkMap::iterator story = doc->second.begin(); story != doc->second.end(); ++story)
		{
			KCMMarkRangeList& ranges = story->second;
			KCMMergeMarkRanges(ranges);
			if (ranges.empty())
				continue;				// nothing but empty ranges: there is no first or last to read

			if (ranges.front().fFrom < span.fLowest)
				span.fLowest = ranges.front().fFrom;
			if (ranges.back().fTo > span.fHighest)
				span.fHighest = ranges.back().fTo;
		}
	}
	return span;
}

#endif // __KCMStoryMarkDocs_h__

// End, KCMStoryMarkDocs.h.
