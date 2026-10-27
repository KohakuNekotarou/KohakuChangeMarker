//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - UTF-8 bytes made well-formed before PMString takes them (2026-10-27).
//
//  WHY. Every UTF-8 string KCM hands to PMString goes through PMString::SetUTF8String (KCMTextWords::PMStringOfUtf8),
//  and that has two faults with bytes that are not well-formed (memory pmstring-from-utf8-bytes - publiclib's
//  PMString.cpp and ASL's unicode.hpp read whole): it THROWS std::runtime_error on four kinds (a continuation byte where
//  a character starts, a byte of 0xF8 and up, a value past U+10FFFF, a surrogate), and it reads a sequence cut short as
//  ANOTHER CHARACTER without a word (it does not look at whether the next byte is a continuation byte - "E3 81 ." eats
//  the "."). Some of those strings come from outside: the readings and the kenten names of a Word file the reader
//  imports. KFC met the first fault from a file (a thrown Load Order... lost the dialog's next press) and now checks
//  its files whole by RFC 3629's table; KCM's callers want a string either way, so here the same table REPLACES what
//  does not fit with U+FFFD, one per byte, and keeps everything else as it was.
//
//  PURE: no SDK type, so the offline harness compiles it as it is (work/kcm-utf8-test).
//
//========================================================================================
#ifndef __KCMUtf8_h__
#define __KCMUtf8_h__

#include <cstdint>
#include <string>

/** `in` with every byte that does not begin or continue a well-formed character - RFC 3629: a lead byte and its
	continuation bytes, no overlong form, no surrogate, nothing past U+10FFFF - replaced by U+FFFD (EF BF BD), one for
	each such byte; the bytes after it are read again from the next one, so a character cut short never swallows the
	character that follows. Well-formed input comes back byte for byte. */
inline std::string KCMWellFormedUtf8(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	size_t i = 0;
	while (i < in.size())
	{
		const unsigned char lead = static_cast<unsigned char>(in[i]);
		if (lead < 0x80)
		{
			out += static_cast<char>(lead);
			++i;
			continue;
		}
		size_t length = 0;
		std::uint32_t code = 0;
		std::uint32_t smallest = 0;
		if (lead >= 0xC2 && lead <= 0xDF)
		{
			length = 2;
			code = lead & 0x1F;
			smallest = 0x80;
		}
		else if (lead >= 0xE0 && lead <= 0xEF)
		{
			length = 3;
			code = lead & 0x0F;
			smallest = 0x800;
		}
		else if (lead >= 0xF0 && lead <= 0xF4)
		{
			length = 4;
			code = lead & 0x07;
			smallest = 0x10000;
		}
		// (anything else - a continuation byte, 0xC0 / 0xC1 which are always overlong, 0xF5 and up - has length 0)
		bool fits = (length != 0 && in.size() - i >= length);
		for (size_t k = 1; fits && k < length; ++k)
		{
			const unsigned char c = static_cast<unsigned char>(in[i + k]);
			if ((c & 0xC0) != 0x80)
				fits = false;
			else
				code = (code << 6) | (c & 0x3F);
		}
		if (fits && (code < smallest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)))
			fits = false;
		if (fits)
		{
			out.append(in, i, length);
			i += length;
		}
		else
		{
			out += static_cast<char>(0xEF);		// U+FFFD REPLACEMENT CHARACTER
			out += static_cast<char>(0xBF);
			out += static_cast<char>(0xBD);
			++i;
		}
	}
	return out;
}

#endif // __KCMUtf8_h__

// End, KCMUtf8.h.
