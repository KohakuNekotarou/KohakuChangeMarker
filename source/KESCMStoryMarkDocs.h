//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  What is lit up, per document, and the one rule for putting two sets of it together.
//
//  ★★★TWO KINDS OF MARK EXIST AND THEY MEET HERE (2026-08-23). A STANDING mark is what the
//  "Show Marks on Target / Source" toggles and the tool's held button put up; a FLASH is the
//  pointer a jump puts up for about a second (KESCMStoryMarker). They are drawn by one adornment,
//  which is why they have to be reconciled before anything is drawn rather than while it is.
//  ⚠The rule below was written when that adornment INVERTED what was underneath (Difference
//  blending), where two marks over the same characters cancelled out and left a hole exactly where
//  both said "look here". **2026-08-24 the drawing became an opaque wash and that failure mode is
//  gone** - see the note on the rule itself for what still keeps it.
//
//  ★★THE RULE IS PER DOCUMENT, AND THAT IS THE WHOLE POINT OF THIS FILE: in a document that has
//  a standing mark, the standing mark is all there is; in a document that has none, the flash
//  shows. Nothing is ever merged between the two.
//    * The two can never fight over the same characters, because within one document only one of
//      them is ever used - the guarantee comes from the shape of the answer rather than from a
//      check somebody has to remember to write. ★What that buys since the wash replaced the
//      inversion is no longer "no hole" but "no ambiguity": a document showing every edit is not
//      also pointing at one of them.
//    * And the jump reaches the OTHER window. Until 2026-08-23 one flag answered for both
//      documents ("is a standing mark showing?"), so turning "Show Marks on Target" on silenced
//      the jump's flash in the SOURCE window too, where nothing was standing and the reader had
//      just asked to be shown something (the bug logged as A3).
//
//  ★HEADER-ONLY AND FREE OF THE SDK EXCEPT FOR TWO TYPE NAMES, WHICH IS WHAT MAKES IT TESTABLE.
//  IDataBase is never dereferenced here - it is an address used as a key - and UID only has to
//  sort. The test outside InDesign is work\kescm-markranges-test, which includes this file as it
//  stands rather than a copy that could drift.
//
//========================================================================================

#ifndef __KESCMStoryMarkDocs_h__
#define __KESCMStoryMarkDocs_h__

#include "OMTypes.h"		// UID
#include "KESCMStoryMarkRanges.h"

#include <map>

class IDataBase;

/** Which characters of which stories are lit up in ONE document: story UID -> its ranges. */
typedef std::map<UID, KESCMMarkRangeList> KESCMStoryMarkMap;

/** The same thing for both compared documents at once: database -> what is lit up in it.

	★★BOTH AT ONCE IS NOT A LUXURY - "Show Marks on Target" and "Show Marks on Source" can be on
	together, and then the newer document's edits and the older one's have to be up at the same
	time (user's request, 2026-08-22). ⚠A press, by contrast, only ever marks the window it was
	made in; it is the same structure holding one entry. */
typedef std::map<IDataBase*, KESCMStoryMarkMap> KESCMStoryMarkDocs;

/** Put the standing marks and the flash together into the one set that gets drawn.

	A document that appears in `standing` is taken from there and its flash is dropped; a document
	that does not is taken from `flash`. Empty entries (a document whose map holds no story, or a
	story whose list holds no range) are not carried over - an entry that draws nothing would still
	make the set look occupied, which is what the caller tests to decide whether anything is up.

	@param standing what the toggles and a held button are holding up. May be empty.
	@param flash what a jump wants to show for a moment. May be empty.
	@param out receives the union under the rule above. Cleared first; may not alias either input.
*/
/** Copy the stories that would actually draw something. Implementation detail of the composition
	below; the emptiness test is what keeps "this document is spoken for" from being answered by an
	entry that draws nothing. */
inline void KESCMCopyDrawableStories(const KESCMStoryMarkMap& from, KESCMStoryMarkMap& to)
{
	for (KESCMStoryMarkMap::const_iterator it = from.begin(); it != from.end(); ++it)
	{
		if (it->second.empty())
			continue;
		to[it->first] = it->second;
	}
}

inline void KESCMComposeMarkDocs(const KESCMStoryMarkDocs& standing, const KESCMStoryMarkDocs& flash,
								 KESCMStoryMarkDocs& out)
{
	out.clear();

	for (KESCMStoryMarkDocs::const_iterator doc = standing.begin(); doc != standing.end(); ++doc)
	{
		KESCMStoryMarkMap kept;
		KESCMCopyDrawableStories(doc->second, kept);
		if (!kept.empty())
			out[doc->first].swap(kept);
	}

	for (KESCMStoryMarkDocs::const_iterator doc = flash.begin(); doc != flash.end(); ++doc)
	{
		// ⚠ASKED OF WHAT SURVIVED, NOT OF THE STANDING SET. A document that was consulted and found
		//   to have nothing standing in it is absent from `out`, so its flash still shows - which is
		//   the difference between "the toggle is on here" and "the toggle was looked at".
		if (out.find(doc->first) != out.end())
			continue;			// a standing mark is up in this document, and it wins whole

		KESCMStoryMarkMap kept;
		KESCMCopyDrawableStories(doc->second, kept);
		if (!kept.empty())
			out[doc->first].swap(kept);
	}
}

#endif // __KESCMStoryMarkDocs_h__

// End, KESCMStoryMarkDocs.h.
