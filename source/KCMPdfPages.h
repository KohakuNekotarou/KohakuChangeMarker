//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the PDF reader's page layer: how many pages a PDF has, each page's boxes and rotation,
//  and the InDesign page each one was exported from.
//
//  WHAT IT READS: the page tree (ISO 32000-1 7.7.3 - /Kids followed in order; /MediaBox, /CropBox and /Rotate
//  inherited from the /Pages above; a node met twice is read once; a tree deeper than 64 is refused); each page's boxes
//  (14.11.2 - a missing box falls back to the one around it, and a box is bounded by the one around it); and the private
//  data InDesign writes on every page it exports, /PieceInfo /InDesign: /PageUIDList - the uid of the InDesign page this
//  PDF page was made from, two for a spread - and /DocumentID (measured 2026-10-14,
//  docs/ai-notes/kcm-pdf-as-source-2026-10-14.md section 1).
//  REFUSES: a file that is not a PDF, an encrypted one, one whose cross-reference cannot be followed, one with no page.
//
//  PURE: no InDesign header - work/kcm-pdf-test reads crafted and real PDFs with it outside InDesign.
//
//========================================================================================
#ifndef __KCMPdfPages_h__
#define __KCMPdfPages_h__

#include <cstdint>
#include <string>
#include <vector>

#include "KCMPdfObjects.h"

namespace KCMPdf
{
	/** A rectangle in PDF points, lower-left (x0, y0) to upper-right (x1, y1). */
	struct Box
	{
		double x0, y0, x1, y1;
		Box() : x0(0), y0(0), x1(0), y1(0) {}
		Box(double a, double b, double c, double d) : x0(a), y0(b), x1(c), y1(d) {}
		double Width() const { return x1 - x0; }
		double Height() const { return y1 - y0; }
	};

	struct PageInfo
	{
		Box media;
		Box crop;						// the media box when the page names none; bounded by it
		Box trim;						// the crop box when the page names none; bounded by it
		bool hasTrim;					// the page names a /TrimBox of its own: InDesign's "crop to trim" refuses a page that
										//  does not (measured 2026-10-14) where the spec falls back - KCMPdfDoc crops it to the crop box
		int rotate;						// 0, 90, 180 or 270
		std::vector<uint32_t> uids;		// /PieceInfo /InDesign /PageUIDList in key order; empty when there is none
		std::string documentID;			// /PieceInfo /InDesign /DocumentID as UTF-8; empty when there is none

		PageInfo() : hasTrim(false), rotate(0) {}
		/** The trim box as the page is shown: a quarter turn swaps its sides. */
		double ShownWidth() const { return (rotate == 90 || rotate == 270) ? trim.Height() : trim.Width(); }
		double ShownHeight() const { return (rotate == 90 || rotate == 270) ? trim.Width() : trim.Height(); }
	};

	struct Info
	{
		std::vector<PageInfo> pages;
		/** Every page carries at least one InDesign page uid and all of them the same /DocumentID - the uids of ONE
			document, which is when they may stand for the pages' identity (a book exported whole holds several). */
		bool HasPageIds() const;
		/** Some page carries uids, but not every page does or they come from different documents. */
		bool HasMixedPageIds() const;
	};

	/** Whether the file's first 1024 bytes hold "%PDF-". Reads those bytes and nothing else. */
	bool LooksLikePdf(const wchar_t* path);

	/** The pages of the PDF at `path` / in `source`. kFalse with `why` (an English phrase) when it is refused. Never throws. */
	bool ReadInfo(const wchar_t* path, Info& out, std::string& why);
	bool ReadInfoFrom(Source& source, Info& out, std::string& why);
}

#endif // __KCMPdfPages_h__

// End, KCMPdfPages.h.
