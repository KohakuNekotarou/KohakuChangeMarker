//========================================================================================
//
//  KCMPdfSpreads.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including this line. The
//  offline harness answers it with a stub of its own (work/kcm-pdf-test).
#include "VCPlugInHeaders.h"

#include "KCMPdfSpreads.h"

#include <algorithm>
#include <cmath>

namespace
{
	// Points. InDesign writes thousandths; half a point is generous and still far below any page.
	const double kTol = 0.5;

	bool Near(double a, double b)
	{
		return std::fabs(a - b) <= kTol;
	}

	/** A PDF page exported as a spread, laid out left to right - false when it cannot be: geometry missing, the page
		turned, pages of different widths (one spread takes one page size here), places with a gap or an overlap, or widths
		that do not fill the page. It then stays one wide page, as before. */
	bool WholeSpread(const KCMPdf::PageInfo& p, size_t index, KCMPdf::LayoutSpread& out)
	{
		const size_t n = p.uids.size();
		if (n < 2 || p.widths.size() != n || p.offsets.size() != n || p.rotate != 0)
			return false;
		std::vector<size_t> order(n);
		for (size_t k = 0; k < n; ++k)
			order[k] = k;
		std::sort(order.begin(), order.end(), [&p](size_t a, size_t b) { return p.offsets[a] < p.offsets[b]; });
		double sum = 0;
		for (size_t k = 0; k < n; ++k)
		{
			const size_t i = order[k];
			if (!Near(p.widths[i], p.widths[order[0]]))
				return false;
			if (k > 0 && !Near(p.offsets[i], p.offsets[order[k - 1]] + p.widths[order[k - 1]]))
				return false;
			sum += p.widths[i];
		}
		if (!Near(sum, p.ShownWidth()))
			return false;
		out.pages.clear();
		out.spansPdfPage = true;
		for (size_t k = 0; k < n; ++k)
		{
			KCMPdf::LayoutPage lp;
			lp.pdfPage = index;
			lp.part = order[k];
			lp.width = p.widths[order[k]];
			lp.height = p.ShownHeight();
			lp.uid = p.uids[order[k]];
			out.pages.push_back(lp);
		}
		return true;
	}

	/** A page of one uid that carries its place and is not turned: it may join its neighbours in a spread. */
	bool Placed(const KCMPdf::PageInfo& p)
	{
		return p.uids.size() == 1 && p.widths.size() == 1 && p.offsets.size() == 1 && p.rotate == 0
			&& Near(p.widths[0], p.ShownWidth());
	}
}

std::vector<KCMPdf::LayoutSpread> KCMPdf::LayoutSpreads(const Info& info)
{
	std::vector<LayoutSpread> out;

	// THE BINDING, from the first page at a SIDE of its spread: a standard facing document starts with a page on its own -
	// on the right (at 0) when bound left to right, on the left (at -width) when bound right to left. A centred page
	// (-width/2: a spread of one page in a document of single pages) says nothing about it and is passed over (measured
	// 2026-10-14 - work/kcm-pdf-live2/measure.md).
	bool leftToRight = true;
	for (size_t i = 0; i < info.pages.size(); ++i)
	{
		const PageInfo& p = info.pages[i];
		if (!Placed(p))
			continue;
		if (Near(p.offsets[0], 0))
			break;							// a right page: left to right (the default)
		if (Near(p.offsets[0], -p.widths[0]))
		{
			leftToRight = false;			// a left page: right to left
			break;
		}
	}

	bool joinable = false;					// the last spread is made of placed pages of one uid
	double prevOffset = 0;					// the page joined last, in reading order
	double prevWidth = 0;
	double prevHeight = 0;
	for (size_t i = 0; i < info.pages.size(); ++i)
	{
		const PageInfo& p = info.pages[i];
		LayoutSpread whole;
		if (WholeSpread(p, i, whole))
		{
			out.push_back(whole);
			joinable = false;
			continue;
		}
		LayoutPage lp;
		lp.pdfPage = i;
		lp.width = p.ShownWidth();
		lp.height = p.ShownHeight();
		lp.uid = p.uids.empty() ? 0 : p.uids[0];
		const bool placed = Placed(p);
		if (placed && joinable && Near(lp.height, prevHeight) && Near(lp.width, prevWidth))
		{
			// Left to right, the next page abuts on the right of the one before; right to left, on its left.
			const double offset = p.offsets[0];
			if (leftToRight ? Near(offset, prevOffset + prevWidth) : Near(offset + lp.width, prevOffset))
			{
				std::vector<LayoutPage>& pages = out.back().pages;
				if (leftToRight)
					pages.push_back(lp);
				else
					pages.insert(pages.begin(), lp);
				prevOffset = offset;
				prevWidth = lp.width;
				prevHeight = lp.height;
				continue;
			}
		}
		LayoutSpread single;
		single.pages.push_back(lp);
		out.push_back(single);
		joinable = placed;
		if (placed)
		{
			prevOffset = p.offsets[0];
			prevWidth = lp.width;
			prevHeight = lp.height;
		}
	}
	return out;
}

// End, KCMPdfSpreads.cpp.
