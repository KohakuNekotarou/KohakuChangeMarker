//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - how the document KCM makes from a PDF is laid out in spreads (2026-10-14, the export
//  check's design docs/superpowers/specs/2026-10-14-kcm-pdf-export-check-design.md 3.3).
//
//  A PDF page exported as a spread becomes one spread of its pages (it is then placed once, across them). Pages exported
//  one by one go back into the spreads they came from, by their places in them (/PageTransformationMatrixList, measured
//  2026-10-14 = work/kcm-pdf-live2/measure.md: a facing spread's spine is 0 - a left page at -width, a right page at 0 -
//  and a page of a document of single pages is centred, at -width/2), the binding read from the first page at a side
//  (alone on the right = bound left to right). Anything else is a spread of one page, as before 2026-10-14.
//
//  ★THE SPREADS ARE FOR THE EYE ONLY: every page is compared with the page of its own InDesign uid, so a spread put
//   together the wrong way (a document starting with a two-page spread is read as bound the other way) moves pages on
//   the screen and changes no comparison.
//
//  PURE: no InDesign header - work/kcm-pdf-test tests it outside InDesign.
//
//========================================================================================
#ifndef __KCMPdfSpreads_h__
#define __KCMPdfSpreads_h__

#include <cstddef>
#include <cstdint>
#include <vector>

#include "KCMPdfPages.h"

namespace KCMPdf
{
	/** One page of the document KCM makes from a PDF. */
	struct LayoutPage
	{
		size_t		pdfPage;	// 0-based, into Info::pages: the PDF page shown on it
		size_t		part;		// which of that PDF page's uids it is (0 for a page of one)
		double		width;		// points, as the page is shown
		double		height;
		uint32_t	uid;		// the InDesign page it came from; 0 when the PDF carries none

		LayoutPage() : pdfPage(0), part(0), width(0), height(0), uid(0) {}
	};

	/** A spread of that document, its pages left to right, all of one size. spansPdfPage = ONE PDF page exported as a
		spread: placed once, across the spread; otherwise every page places its own PDF page. */
	struct LayoutSpread
	{
		std::vector<LayoutPage>	pages;
		bool					spansPdfPage;

		LayoutSpread() : spansPdfPage(false) {}
	};

	/** The spreads of the document made from `info`, in order. One spread per PDF page when the PDF carries no places. */
	std::vector<LayoutSpread> LayoutSpreads(const Info& info);
}

#endif // __KCMPdfSpreads_h__

// End, KCMPdfSpreads.h.
