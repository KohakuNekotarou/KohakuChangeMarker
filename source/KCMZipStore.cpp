//========================================================================================
//
//  KCMZipStore.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storydocx-test).
#include "VCPlugInHeaders.h"

#include "KCMZipStore.h"

namespace KCMZipStore
{

namespace
{

void Put16(std::string& out, unsigned int v)
{
	out += static_cast<char>(v & 0xFF);
	out += static_cast<char>((v >> 8) & 0xFF);
}

void Put32(std::string& out, unsigned int v)
{
	Put16(out, v & 0xFFFF);
	Put16(out, (v >> 16) & 0xFFFF);
}

const unsigned int kDosTime = 0x0000;	// 00:00:00
const unsigned int kDosDate = 0x0021;	// 1980-01-01: ((1980-1980) << 9) | (1 << 5) | 1

}	// anonymous namespace

unsigned int Crc32(const char* data, size_t size)
{
	unsigned int crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < size; ++i)
	{
		crc ^= static_cast<unsigned char>(data[i]);
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
	}
	return crc ^ 0xFFFFFFFFu;
}

void Write(const std::vector<Entry>& entries, std::string& outZip)
{
	outZip.clear();

	std::string central;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		const Entry& e = entries[i];
		const unsigned int crc = Crc32(e.fBytes.data(), e.fBytes.size());
		const unsigned int size = static_cast<unsigned int>(e.fBytes.size());
		const unsigned int nameLen = static_cast<unsigned int>(e.fName.size());
		const unsigned int localAt = static_cast<unsigned int>(outZip.size());

		// ---- local file header ------------------------------------------------------------
		Put32(outZip, 0x04034b50u);
		Put16(outZip, 20);			// version needed: 2.0
		Put16(outZip, 0);			// flags: sizes are HERE, not in a data descriptor
		Put16(outZip, 0);			// method: stored
		Put16(outZip, kDosTime);
		Put16(outZip, kDosDate);
		Put32(outZip, crc);
		Put32(outZip, size);		// compressed
		Put32(outZip, size);		// uncompressed
		Put16(outZip, nameLen);
		Put16(outZip, 0);			// extra
		outZip += e.fName;
		outZip += e.fBytes;

		// ---- its central directory record ---------------------------------------------------
		Put32(central, 0x02014b50u);
		Put16(central, 20);			// version made by
		Put16(central, 20);			// version needed
		Put16(central, 0);			// flags
		Put16(central, 0);			// method
		Put16(central, kDosTime);
		Put16(central, kDosDate);
		Put32(central, crc);
		Put32(central, size);
		Put32(central, size);
		Put16(central, nameLen);
		Put16(central, 0);			// extra
		Put16(central, 0);			// comment
		Put16(central, 0);			// disk number
		Put16(central, 0);			// internal attributes
		Put32(central, 0);			// external attributes
		Put32(central, localAt);
		central += e.fName;
	}

	const unsigned int centralAt = static_cast<unsigned int>(outZip.size());
	outZip += central;

	// ---- end of central directory ---------------------------------------------------------------
	Put32(outZip, 0x06054b50u);
	Put16(outZip, 0);				// this disk
	Put16(outZip, 0);				// disk with the central directory
	Put16(outZip, static_cast<unsigned int>(entries.size()));
	Put16(outZip, static_cast<unsigned int>(entries.size()));
	Put32(outZip, static_cast<unsigned int>(central.size()));
	Put32(outZip, centralAt);
	Put16(outZip, 0);				// comment
}

}	// namespace KCMZipStore

// End, KCMZipStore.cpp.
