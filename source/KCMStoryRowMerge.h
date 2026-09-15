//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Merging the changes the diff has just found with the ones the reader has already replaced,
//  into the one order the panel shows them in (the Import mode, 2026-09-15).
//
//  ★WHY THERE IS A FILE FOR THIS. The Import mode keeps a replaced change in the list instead of
//  dropping it, so the list the panel draws comes from two places: KCMStoryDiffRun's fresh
//  comparison, and the row's own record of what was replaced. Putting them in one order is a
//  rule - ascending by start, a tie putting the replaced one first - and a rule of that shape is
//  the sort of thing that is wrong in one direction for months without anyone seeing it.
//
//  ★★IT TOUCHES NOTHING OF INDESIGN, AND THAT IS THE POINT. Nothing here needs a document, so
//  work/kcm-storyhtml-test builds it straight and runs the rule in about a second. Everything
//  that does need a document stays in KCMStoryDiffRun. (KCMStoryRowFilter.h is a free function
//  for the same reason, and KCMTextDiff and KCMStoryHtml are already in that harness.)
//
//========================================================================================

#ifndef __KCMStoryRowMerge_h__
#define __KCMStoryRowMerge_h__

// ⚠INCLUDED BY NAME rather than left to whatever came first. Inside the plug-in the precompiled
// header has already answered bool16 / int32 / TextIndex / kTrue / kFalse; in the test harness
// nothing has, and this header is included there directly. An ordering dependency here would
// break the harness the day a caller changed its include order - the same fix work/'s OMTypes.h
// stub records for KCMParaText.h.
#include "BaseType.h"

#include <vector>

/** Putting the live changes and the replaced ones into one order.
	@ingroup KCM
*/
namespace KCMStoryRowMerge
{
	/** Where one slot of the merged list comes from. */
	struct Slot
	{
		bool16	fDone;		// kTrue = the replaced list, kFalse = the live diff
		int32	fIndex;		// index into whichever of the two lists fDone names

		Slot() : fDone(kFalse), fIndex(0) {}
		Slot(bool16 done, int32 index) : fDone(done), fIndex(index) {}
	};

	/** Merge two ALREADY SORTED lists of start positions into one order.

		@param liveStarts	fTargetStart of each change the diff found, ascending.
		@param doneStarts	fTargetStart of each replaced change, ascending, IN THE SAME
			COORDINATES - both are read after the story was diffed again, which is what makes
			them comparable at all.
		@param out			one slot per input element, in order. Cleared first.

		★★**A TIE PUTS THE DONE ONE FIRST.** A replacement is already in the text, so at the same
		start it is the thing standing there, and the live change is what the diff found beside
		it. What matters is that the rule is the SAME every time; this way round is the one that
		keeps a replaced row from appearing to move when a later edit adds a change at its start.

		⚠**ASCENDING INPUT IS THE CALLER'S PROMISE.** Neither list is sorted here: the diff
		produces its changes in text order already, and the replaced list is appended to in the
		order the reader worked. Sorting them here would hide the day one of those stops being
		true, which is a thing worth finding out about rather than papering over.
	*/
	void Merge(const std::vector<TextIndex>& liveStarts,
			   const std::vector<TextIndex>& doneStarts,
			   std::vector<Slot>& out);
}

#endif // __KCMStoryRowMerge_h__

// End, KCMStoryRowMerge.h.
