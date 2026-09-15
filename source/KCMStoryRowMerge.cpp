//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The merge itself. The reasoning is all in KCMStoryRowMerge.h; this is a two-finger walk.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "KCMStoryRowMerge.h"

void KCMStoryRowMerge::Merge(const std::vector<TextIndex>& liveStarts,
							 const std::vector<TextIndex>& doneStarts,
							 std::vector<Slot>& out)
{
	out.clear();
	out.reserve(liveStarts.size() + doneStarts.size());

	size_t live = 0;
	size_t done = 0;
	while (live < liveStarts.size() || done < doneStarts.size())
	{
		// ★"<=" IS THE TIE RULE, and it is the whole of it: at equal starts the done one is
		//   taken first. Written as one condition rather than as a separate equality test so
		//   that there is one place to read the rule off (KCMStoryRowMerge.h says why it is
		//   this way round).
		const bool16 takeDone = (live >= liveStarts.size())
			|| (done < doneStarts.size() && doneStarts[done] <= liveStarts[live]);

		if (takeDone)
		{
			out.push_back(Slot(kTrue, static_cast<int32>(done)));
			++done;
		}
		else
		{
			out.push_back(Slot(kFalse, static_cast<int32>(live)));
			++live;
		}
	}
}

// End, KCMStoryRowMerge.cpp.
