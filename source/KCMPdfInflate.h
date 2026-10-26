//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Flate decoding (zlib RFC 1950 + deflate RFC 1951) for the PDF reader.
//
//  WHY OUR OWN (2026-10-14): the SDK has no public door that decodes Flate (source/public has no zlib.h and no
//  inflate), and the PDF reader (KCMPdfObjects.h) needs it for object streams, cross-reference streams and, later,
//  page content. Writing it here keeps third-party code out of a product that goes to Exchange. (KCM's .docx reading
//  does not help: it opens the zip with the SDK's IUCFPackageUtils, which takes a FILE - KCMDocxPackage.h.)
//
//  PURE: no InDesign header - work/kcm-pdf-test builds it outside InDesign against vectors Python's zlib wrote.
//
//  NEVER THROWS, NEVER READS OR WRITES OUT OF BOUNDS: every read is checked against the input's size, every write
//  against maxOut, and the output's growth is caught and answered as kInflateNoMemory.
//
//========================================================================================
#ifndef __KCMPdfInflate_h__
#define __KCMPdfInflate_h__

#include <cstddef>
#include <cstdint>
#include <vector>

namespace KCMPdf
{
	enum InflateResult
	{
		kInflateOK = 0,
		kInflateBadHeader,		// the zlib header is not deflate, fails its check, or asks for a preset dictionary
		kInflateBadData,		// an invalid block type, code, length or distance
		kInflateTruncated,		// the input ended before the final block did
		kInflateTooBig,			// the output would pass maxOut
		kInflateNoMemory,		// the output could not be grown
		kInflateBadChecksum		// decoded to the end, but the Adler-32 differs (the output is kept)
	};

	/** Decode `size` bytes at `data` into `out` (replaced). zlibWrapped = the 2-byte zlib header and the Adler-32 trailer
		are there (PDF's /FlateDecode); false = raw deflate.
		⚠kInflateBadChecksum leaves the whole output in `out` - writers have been known to get the trailer wrong while the
		data is sound; the caller decides. An input that ends right after the final block (no trailer) is kInflateOK. */
	InflateResult Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut, bool zlibWrapped);

	/** A short English phrase for a result, for a status line. */
	const char* InflateResultText(InflateResult r);
}

#endif // __KCMPdfInflate_h__

// End, KCMPdfInflate.h.
