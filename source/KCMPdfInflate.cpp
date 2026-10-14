//========================================================================================
//
//  KCMPdfInflate.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including this line. The
//  offline harness answers it with a stub of its own (work/kcm-pdf-test).
#include "VCPlugInHeaders.h"

#include "KCMPdfInflate.h"

namespace
{
	// RFC 1951 3.2.5: base lengths and distances, and their extra bits.
	const uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	const uint8_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	const uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	const uint8_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
		7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
	// RFC 1951 3.2.7: the order in which the code length code lengths are sent.
	const uint8_t kClenOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

	const int kMaxBits = 15;

	/** Bits come least significant first (RFC 1951 3.1.1). A byte is loaded only when a read needs it, so after any
		read fewer than 8 bits are left, all from the last byte loaded - which is what makes AlignToByte a plain discard
		and `pos` the first byte after the deflate data once the final block has ended. */
	struct BitReader
	{
		const uint8_t* data;
		size_t size;
		size_t pos;
		uint32_t buf;
		int count;

		BitReader(const uint8_t* d, size_t n) : data(d), size(n), pos(0), buf(0), count(0) {}

		/** n in 0..16. -1 when the input ends first. */
		int Bits(int n)
		{
			while (count < n)
			{
				if (pos >= size)
					return -1;
				buf |= static_cast<uint32_t>(data[pos++]) << count;
				count += 8;
			}
			const int v = static_cast<int>(buf & ((1u << n) - 1u));
			buf >>= n;
			count -= n;
			return v;
		}

		void AlignToByte()
		{
			buf = 0;
			count = 0;
		}
	};

	/** A canonical Huffman code (RFC 1951 3.2.2): how many codes of each length, and the symbols in code order. */
	struct Huffman
	{
		uint16_t count[kMaxBits + 1];
		uint16_t symbol[288];
	};

	/** 0 = a complete code, > 0 = incomplete (allowed where the caller says), < 0 = over-subscribed (never). */
	int Build(Huffman& h, const uint8_t* lengths, int n)
	{
		for (int len = 0; len <= kMaxBits; ++len)
			h.count[len] = 0;
		for (int s = 0; s < n; ++s)
			h.count[lengths[s]]++;
		if (h.count[0] == n)
			return 0;
		int left = 1;
		for (int len = 1; len <= kMaxBits; ++len)
		{
			left <<= 1;
			left -= h.count[len];
			if (left < 0)
				return left;
		}
		uint16_t offs[kMaxBits + 1];
		offs[0] = 0;
		offs[1] = 0;
		for (int len = 1; len < kMaxBits; ++len)
			offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
		for (int s = 0; s < n; ++s)
			if (lengths[s] != 0)
				h.symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
		return left;
	}

	/** The next symbol. -1 = the input ended, -2 = no such code. */
	int Decode(BitReader& in, const Huffman& h)
	{
		int code = 0;
		int first = 0;
		int index = 0;
		for (int len = 1; len <= kMaxBits; ++len)
		{
			const int bit = in.Bits(1);
			if (bit < 0)
				return -1;
			code |= bit;
			const int count = h.count[len];
			if (code - count < first)
				return h.symbol[index + (code - first)];
			index += count;
			first += count;
			first <<= 1;
			code <<= 1;
		}
		return -2;
	}

	/** The output, with the cap and the allocation checked. Growth is geometric: a reserve of exactly what one copy
		needs would reallocate on every copy (MSVC reserves exactly what is asked). */
	struct Output
	{
		std::vector<uint8_t>& out;
		size_t maxOut;
		KCMPdf::InflateResult fail;

		Output(std::vector<uint8_t>& o, size_t m) : out(o), maxOut(m), fail(KCMPdf::kInflateOK) {}

		bool Ensure(size_t extra)
		{
			const size_t need = out.size() + extra;
			if (need > maxOut || need < out.size())
			{
				fail = KCMPdf::kInflateTooBig;
				return false;
			}
			if (out.capacity() >= need)
				return true;
			size_t want = (out.capacity() < 4096) ? 4096 : out.capacity() * 2;
			if (want < need)
				want = need;
			if (want > maxOut)
				want = maxOut;
			try
			{
				out.reserve(want);
			}
			catch (...)
			{
				fail = KCMPdf::kInflateNoMemory;
				return false;
			}
			return true;
		}

		bool Put(uint8_t b)
		{
			if (!Ensure(1))
				return false;
			out.push_back(b);
			return true;
		}

		/** A back-reference (RFC 1951 3.2.3): overlapping copies are the point, so it goes byte by byte. */
		bool Copy(size_t dist, size_t len)
		{
			if (dist == 0 || dist > out.size())
			{
				fail = KCMPdf::kInflateBadData;
				return false;
			}
			if (!Ensure(len))
				return false;
			const size_t from = out.size() - dist;
			for (size_t i = 0; i < len; ++i)
			{
				const uint8_t b = out[from + i];
				out.push_back(b);
			}
			return true;
		}
	};

	KCMPdf::InflateResult Stored(BitReader& in, Output& o)
	{
		in.AlignToByte();
		if (in.size - in.pos < 4)
			return KCMPdf::kInflateTruncated;
		const unsigned len = static_cast<unsigned>(in.data[in.pos]) | (static_cast<unsigned>(in.data[in.pos + 1]) << 8);
		const unsigned nlen = static_cast<unsigned>(in.data[in.pos + 2]) | (static_cast<unsigned>(in.data[in.pos + 3]) << 8);
		in.pos += 4;
		if (len != (~nlen & 0xFFFFu))
			return KCMPdf::kInflateBadData;
		if (in.size - in.pos < len)
			return KCMPdf::kInflateTruncated;
		if (!o.Ensure(len))
			return o.fail;
		o.out.insert(o.out.end(), in.data + in.pos, in.data + in.pos + len);
		in.pos += len;
		return KCMPdf::kInflateOK;
	}

	KCMPdf::InflateResult Codes(BitReader& in, Output& o, const Huffman& lencode, const Huffman& distcode)
	{
		for (;;)
		{
			int symbol = Decode(in, lencode);
			if (symbol == -1)
				return KCMPdf::kInflateTruncated;
			if (symbol < 0)
				return KCMPdf::kInflateBadData;
			if (symbol < 256)
			{
				if (!o.Put(static_cast<uint8_t>(symbol)))
					return o.fail;
				continue;
			}
			if (symbol == 256)
				return KCMPdf::kInflateOK;
			symbol -= 257;
			if (symbol >= 29)
				return KCMPdf::kInflateBadData;
			const int extra = in.Bits(kLenExtra[symbol]);
			if (extra < 0)
				return KCMPdf::kInflateTruncated;
			const size_t len = static_cast<size_t>(kLenBase[symbol]) + static_cast<size_t>(extra);
			const int d = Decode(in, distcode);
			if (d == -1)
				return KCMPdf::kInflateTruncated;
			if (d < 0 || d >= 30)
				return KCMPdf::kInflateBadData;
			const int dextra = in.Bits(kDistExtra[d]);
			if (dextra < 0)
				return KCMPdf::kInflateTruncated;
			const size_t dist = static_cast<size_t>(kDistBase[d]) + static_cast<size_t>(dextra);
			if (!o.Copy(dist, len))
				return o.fail;
		}
	}

	KCMPdf::InflateResult Fixed(BitReader& in, Output& o)
	{
		Huffman lencode;
		Huffman distcode;
		uint8_t lengths[288];
		int s = 0;
		for (; s < 144; ++s) lengths[s] = 8;
		for (; s < 256; ++s) lengths[s] = 9;
		for (; s < 280; ++s) lengths[s] = 7;
		for (; s < 288; ++s) lengths[s] = 8;
		Build(lencode, lengths, 288);
		for (s = 0; s < 30; ++s) lengths[s] = 5;
		Build(distcode, lengths, 30);
		return Codes(in, o, lencode, distcode);
	}

	KCMPdf::InflateResult Dynamic(BitReader& in, Output& o)
	{
		const int nlen = in.Bits(5);
		const int ndist = in.Bits(5);
		const int ncode = in.Bits(4);
		if (nlen < 0 || ndist < 0 || ncode < 0)
			return KCMPdf::kInflateTruncated;
		const int hlit = nlen + 257;
		const int hdist = ndist + 1;
		const int hclen = ncode + 4;
		if (hlit > 286 || hdist > 30)
			return KCMPdf::kInflateBadData;
		uint8_t lengths[286 + 30];
		int index = 0;
		for (; index < hclen; ++index)
		{
			const int v = in.Bits(3);
			if (v < 0)
				return KCMPdf::kInflateTruncated;
			lengths[kClenOrder[index]] = static_cast<uint8_t>(v);
		}
		for (; index < 19; ++index)
			lengths[kClenOrder[index]] = 0;
		Huffman lencode;
		Huffman distcode;
		if (Build(lencode, lengths, 19) != 0)
			return KCMPdf::kInflateBadData;			// the code length code must be complete
		index = 0;
		while (index < hlit + hdist)
		{
			const int symbol = Decode(in, lencode);
			if (symbol == -1)
				return KCMPdf::kInflateTruncated;
			if (symbol < 0)
				return KCMPdf::kInflateBadData;
			if (symbol < 16)
			{
				lengths[index++] = static_cast<uint8_t>(symbol);
				continue;
			}
			int len = 0;
			int repeat = 0;
			if (symbol == 16)
			{
				if (index == 0)
					return KCMPdf::kInflateBadData;
				len = lengths[index - 1];
				const int e = in.Bits(2);
				if (e < 0)
					return KCMPdf::kInflateTruncated;
				repeat = 3 + e;
			}
			else if (symbol == 17)
			{
				const int e = in.Bits(3);
				if (e < 0)
					return KCMPdf::kInflateTruncated;
				repeat = 3 + e;
			}
			else
			{
				const int e = in.Bits(7);
				if (e < 0)
					return KCMPdf::kInflateTruncated;
				repeat = 11 + e;
			}
			if (index + repeat > hlit + hdist)
				return KCMPdf::kInflateBadData;
			while (repeat-- > 0)
				lengths[index++] = static_cast<uint8_t>(len);
		}
		if (lengths[256] == 0)
			return KCMPdf::kInflateBadData;			// no end-of-block code
		// incomplete codes only for a single code of length 1 (the case zlib and puff allow)
		int err = Build(lencode, lengths, hlit);
		if (err < 0 || (err > 0 && hlit != lencode.count[0] + lencode.count[1]))
			return KCMPdf::kInflateBadData;
		err = Build(distcode, lengths + hlit, hdist);
		if (err < 0 || (err > 0 && hdist != distcode.count[0] + distcode.count[1]))
			return KCMPdf::kInflateBadData;
		return Codes(in, o, lencode, distcode);
	}

	uint32_t Adler32(const std::vector<uint8_t>& data)
	{
		uint32_t a = 1;
		uint32_t b = 0;
		size_t i = 0;
		const size_t n = data.size();
		while (i < n)
		{
			const size_t chunk = (n - i < 5552) ? n - i : 5552;
			for (size_t k = 0; k < chunk; ++k)
			{
				a += data[i + k];
				b += a;
			}
			a %= 65521;
			b %= 65521;
			i += chunk;
		}
		return (b << 16) | a;
	}
}	// namespace

KCMPdf::InflateResult KCMPdf::Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut, bool zlibWrapped)
{
	out.clear();
	try
	{
		if (data == nullptr && size != 0)
			return kInflateBadData;
		size_t start = 0;
		if (zlibWrapped)
		{
			if (size < 2)
				return kInflateTruncated;
			const unsigned cmf = data[0];
			const unsigned flg = data[1];
			if ((cmf & 0x0Fu) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20u) != 0)
				return kInflateBadHeader;
			start = 2;
		}
		BitReader in(data + start, size - start);
		Output o(out, maxOut);
		int last = 0;
		do
		{
			last = in.Bits(1);
			const int type = in.Bits(2);
			if (last < 0 || type < 0)
				return kInflateTruncated;
			InflateResult r = kInflateBadData;
			if (type == 0)
				r = Stored(in, o);
			else if (type == 1)
				r = Fixed(in, o);
			else if (type == 2)
				r = Dynamic(in, o);
			if (r != kInflateOK)
				return r;
		} while (last == 0);
		if (!zlibWrapped)
			return kInflateOK;
		const size_t trailer = start + in.pos;		// the trailer begins at the next byte boundary
		if (size - trailer < 4)
			return kInflateOK;						// a missing trailer is tolerated
		const uint32_t want = (static_cast<uint32_t>(data[trailer]) << 24) | (static_cast<uint32_t>(data[trailer + 1]) << 16) |
							  (static_cast<uint32_t>(data[trailer + 2]) << 8) | static_cast<uint32_t>(data[trailer + 3]);
		return (Adler32(out) == want) ? kInflateOK : kInflateBadChecksum;
	}
	catch (...)
	{
		return kInflateNoMemory;
	}
}

const char* KCMPdf::InflateResultText(InflateResult r)
{
	switch (r)
	{
	case kInflateOK:			return "decoded";
	case kInflateBadHeader:		return "not zlib data";
	case kInflateBadData:		return "damaged compressed data";
	case kInflateTruncated:		return "the compressed data is cut short";
	case kInflateTooBig:		return "the data is larger than allowed";
	case kInflateNoMemory:		return "out of memory";
	case kInflateBadChecksum:	return "decoded, but the checksum differs";
	}
	return "unknown";
}

// End, KCMPdfInflate.cpp.
