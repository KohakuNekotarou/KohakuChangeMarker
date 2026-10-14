//========================================================================================
//
//  KCMPatchFilter.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including this line. The
//  offline harness answers it with a stub of its own (work/kcm-pdf-test).
#include "VCPlugInHeaders.h"

#include "KCMPatchFilter.h"

namespace KCMPatch
{
	size_t KeepPatches(uint8_t* mask, int32_t w, int32_t h, int32_t minCells, int32_t* queue)
	{
		if (mask == nullptr || w <= 0 || h <= 0)
			return 0;
		const int32_t n = w * h;	// a 36dpi mask: 7,776 x 7,776 at most (InDesign's largest page), well inside int32
		size_t kept = 0;
		if (minCells <= 1 || queue == nullptr)
		{
			for (int32_t i = 0; i < n; ++i)
				if (mask[i] == 1)
					++kept;
			return kept;
		}
		// The mask is its own record of where the walk has been: 2 = gathered into the patch being measured,
		// 3 = in a patch that stays (so the scan below does not walk it again); both go back to 1 / 0 at the end.
		for (int32_t start = 0; start < n; ++start)
		{
			if (mask[start] != 1)
				continue;
			int32_t head = 0, tail = 0;
			queue[tail++] = start;
			mask[start] = 2;
			while (head < tail)
			{
				const int32_t c = queue[head++];
				const int32_t cy = c / w, cx = c % w;
				for (int32_t dy = -1; dy <= 1; ++dy)
				{
					const int32_t ny = cy + dy;
					if (ny < 0 || ny >= h)
						continue;
					for (int32_t dx = -1; dx <= 1; ++dx)
					{
						const int32_t nx = cx + dx;
						if ((dy == 0 && dx == 0) || nx < 0 || nx >= w)
							continue;
						const int32_t k = ny * w + nx;
						if (mask[k] == 1)
						{
							mask[k] = 2;
							queue[tail++] = k;		// never more than n: each cell is queued once
						}
					}
				}
			}
			// queue[0, tail) is the whole patch
			const uint8_t stays = (tail >= minCells) ? 3 : 0;
			for (int32_t i = 0; i < tail; ++i)
				mask[queue[i]] = stays;
			if (stays != 0)
				kept += static_cast<size_t>(tail);
		}
		for (int32_t i = 0; i < n; ++i)
			if (mask[i] == 3)
				mask[i] = 1;
		return kept;
	}
}
