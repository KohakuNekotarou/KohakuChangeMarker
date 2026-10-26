//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the PDF export check keeps patches, not dots (2026-10-14, the live test of the export
//  check: docs/superpowers/plans/2026-10-14-kcm-pdf-export-check.md Task 9).
//
//  A document and the PDF exported from it are drawn by two different painters, and even after the export check's
//  coarse counting (kKCMExportThr / kKCMExportPoolMinCount, KCMConstants.h) a correct export leaves a few marked cells
//  on the edges of letters and around transparency - in small groups, the largest 6 cells at 160 (measured on 12 page
//  cases of 3 documents x 3 presets: work/kcm-pdf-live2/knobs6.py, knobs7.py). A real difference is a patch: a PDF
//  whose shadow and opacity were removed left one of 133 cells, a missing image one of 7,699. So the export check keeps
//  only the 8-connected patches of at least kKCMExportMinPatchCells cells.
//
//  PURE: no InDesign header - work/kcm-pdf-test tests it outside InDesign.
//
//========================================================================================
#ifndef __KCMPatchFilter_h__
#define __KCMPatchFilter_h__

#include <cstddef>
#include <cstdint>

namespace KCMPatch
{
	/** Keeps only the 8-connected patches of at least `minCells` marked cells in a `w` x `h` mask of 0 / 1 (row after
		row, no padding), clearing the cells of every smaller patch; the cells kept stay 1. `queue` is room for w * h
		int32 the caller provides - the comparison allocates with new (std::nothrow), and this allocates nothing.
		minCells <= 1 keeps every marked cell. Returns the cells kept. */
	size_t KeepPatches(uint8_t* mask, int32_t w, int32_t h, int32_t minCells, int32_t* queue);
}

#endif // __KCMPatchFilter_h__
