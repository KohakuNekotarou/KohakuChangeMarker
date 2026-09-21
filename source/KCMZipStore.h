//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - named byte strings, wrapped as a STORED (uncompressed) zip
//
//  WHAT THIS IS FOR. A .docx is a zip of XML parts. Word opens one whose entries are merely
//  stored - measured 2026-09-19, docs/ai-notes/kcm-docx-probe-2026-09-19.md - so writing one
//  needs no compressor, only the container's bookkeeping and a CRC-32.
//
//  *** A PURE FUNCTION. No SDK type, no file. *** Built and tested outside InDesign in
//  work/kcm-storydocx-test, like KCMStoryShape and KCMStoryDocx and for the same reason.
//
//  *** THE SAME INPUT GIVES THE SAME BYTES. *** Every entry carries one fixed date (1980-01-01,
//  the zip epoch), so two exports of one story are one file and a folder never diffs against
//  itself - the same rule KCMStoryDocx keeps for the parts it builds.
//
//  ⚠NOT IUCFPackageUtils::CreatePackage: that writes a "mimetype" entry first (it is the UCF
//   rule), and whether Word forgives one has not been measured because nothing needs it to.
//
//========================================================================================
#ifndef __KCMZipStore_h__
#define __KCMZipStore_h__

#include <string>
#include <vector>

namespace KCMZipStore
{

/** One part. fName is the path inside the package: ASCII, '/' separated, no leading slash. */
struct Entry
{
	std::string	fName;
	std::string	fBytes;
};

/** The CRC-32 zip uses (polynomial 0xEDB88320, the check value of "123456789" is 0xCBF43926). */
unsigned int Crc32(const char* data, size_t size);

/** The entries, in the order given, as one zip. Never fails.
	@warning sizes and offsets are 32-bit: a story's text is nowhere near that, and zip64 is not
	  something Word needs to be asked about for this. */
void Write(const std::vector<Entry>& entries, std::string& outZip);

}	// namespace KCMZipStore

#endif // __KCMZipStore_h__

// End, KCMZipStore.h.
