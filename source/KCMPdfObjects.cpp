//========================================================================================
//
//  KCMPdfObjects.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including this line. The
//  offline harness answers it with a stub of its own (work/kcm-pdf-test).
#include "VCPlugInHeaders.h"

#include "KCMPdfObjects.h"
#include "KCMPdfInflate.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <windows.h>		// CreateFileW / ReadFile - ⚠it defines min and max: neither is used in this file

namespace
{
	const uint32_t kMaxObjects = 4000000;			// a cross-reference larger than this is refused
	const int kMaxNesting = 64;						// arrays and dictionaries inside each other
	const int kMaxChain = 64;						// /Prev and /XRefStm hops
	const size_t kMaxStringBytes = 16u << 20;
	const size_t kMaxItems = 4000000;				// the elements of one array or dictionary
	const size_t kMaxXrefStreamBytes = 256u << 20;
	const size_t kMaxObjStmBytes = 256u << 20;
	const uint64_t kMaxStreamBytes = 512ull << 20;
	const uint64_t kMaxEndstreamScan = 64ull << 20;
	const uint64_t kNone = ~0ull;

	inline bool IsWhite(int c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
	inline bool IsDelim(int c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }
	inline bool IsRegular(int c) { return c >= 0 && !IsWhite(c) && !IsDelim(c); }
	inline int HexValue(int c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	void AppendUtf8(std::string& out, uint32_t u)
	{
		if (u < 0x80) out.push_back(static_cast<char>(u));
		else if (u < 0x800) { out.push_back(static_cast<char>(0xC0 | (u >> 6))); out.push_back(static_cast<char>(0x80 | (u & 0x3F))); }
		else if (u < 0x10000) { out.push_back(static_cast<char>(0xE0 | (u >> 12))); out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (u & 0x3F))); }
		else { out.push_back(static_cast<char>(0xF0 | (u >> 18))); out.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (u & 0x3F))); }
	}

	struct DepthGuard
	{
		int& d;
		explicit DepthGuard(int& depth) : d(depth) { ++d; }
		~DepthGuard() { --d; }
	};
}	// namespace

//----------------------------------------------------------------------------------------
// The byte cache: 64 KB pages over a Source, the last page read kept.
//----------------------------------------------------------------------------------------
class KCMPdf::ByteCache
{
public:
	explicit ByteCache(Source& s) : fSrc(s), fSize(s.Size()), fStart(kNone), fLen(0), fPage(kPage) {}
	uint64_t Size() const { return fSize; }
	/** The byte at `pos`, or -1 past the end. */
	int At(uint64_t pos)
	{
		if (pos >= fSize)
			return -1;
		if (fStart == kNone || pos < fStart || pos >= fStart + fLen)
		{
			fStart = pos - (pos % kPage);
			fLen = fSrc.ReadAt(fStart, &fPage[0], kPage);
			if (pos >= fStart + fLen)
			{
				fStart = kNone;
				fLen = 0;
				return -1;
			}
		}
		return fPage[static_cast<size_t>(pos - fStart)];
	}
	bool Matches(uint64_t pos, const char* word)
	{
		for (size_t i = 0; word[i] != 0; ++i)
			if (At(pos + i) != static_cast<unsigned char>(word[i]))
				return false;
		return true;
	}
private:
	static const size_t kPage = 65536;
	Source& fSrc;
	uint64_t fSize;
	uint64_t fStart;
	size_t fLen;
	std::vector<uint8_t> fPage;
	ByteCache(const ByteCache&);
	ByteCache& operator=(const ByteCache&);
};

namespace
{
	//------------------------------------------------------------------------------------
	// The parser (ISO 32000-1 7.2-7.3)
	//------------------------------------------------------------------------------------
	class Parser
	{
	public:
		Parser(KCMPdf::ByteCache& b, uint64_t pos) : fB(b), fPos(pos) {}
		uint64_t Pos() const { return fPos; }
		void SetPos(uint64_t p) { fPos = p; }

		void SkipWS()
		{
			for (;;)
			{
				const int c = fB.At(fPos);
				if (c < 0)
					return;
				if (IsWhite(c))
				{
					++fPos;
					continue;
				}
				if (c == '%')
				{
					for (;;)
					{
						const int d = fB.At(fPos);
						if (d < 0 || d == 10 || d == 13)
							break;
						++fPos;
					}
					continue;
				}
				return;
			}
		}

		/** The keyword `word` after white space, not followed by a regular character; consumed when it matches. */
		bool Keyword(const char* word)
		{
			SkipWS();
			const size_t n = std::strlen(word);
			if (!fB.Matches(fPos, word) || IsRegular(fB.At(fPos + n)))
				return false;
			fPos += n;
			return true;
		}

		/** A bare integer token (a '+' or '-' allowed; a real or a word is not an integer). */
		bool Integer(int64_t& out)
		{
			SkipWS();
			uint64_t p = fPos;
			bool neg = false;
			int c = fB.At(p);
			if (c == '+' || c == '-')
			{
				neg = (c == '-');
				++p;
				c = fB.At(p);
			}
			if (c < '0' || c > '9')
				return false;
			int64_t v = 0;
			int digits = 0;
			while (c >= '0' && c <= '9')
			{
				if (++digits > 18)
					return false;
				v = v * 10 + (c - '0');
				++p;
				c = fB.At(p);
			}
			if (c == '.' || IsRegular(c))
				return false;
			fPos = p;
			out = neg ? -v : v;
			return true;
		}

		bool ParseObject(KCMPdf::Object& out, int depth)
		{
			out = KCMPdf::Object();
			if (depth > kMaxNesting)
				return false;
			SkipWS();
			const int c = fB.At(fPos);
			if (c < 0)
				return false;
			if (c == '/')
			{
				out.type = KCMPdf::kObjName;
				return Name(out.text);
			}
			if (c == '(')
			{
				out.type = KCMPdf::kObjString;
				return Literal(out.text);
			}
			if (c == '<')
			{
				if (fB.At(fPos + 1) != '<')
				{
					out.type = KCMPdf::kObjString;
					return Hex(out.text);
				}
				fPos += 2;
				out.type = KCMPdf::kObjDict;
				for (;;)
				{
					SkipWS();
					const int d = fB.At(fPos);
					if (d < 0)
						return false;
					if (d == '>')
					{
						if (fB.At(fPos + 1) != '>')
							return false;
						fPos += 2;
						return true;
					}
					if (d != '/')
						return false;
					std::string key;
					if (!Name(key))
						return false;
					KCMPdf::Object value;
					if (!ParseObject(value, depth + 1))
						return false;
					if (out.items.size() >= kMaxItems)
						return false;
					out.keys.push_back(key);
					out.items.push_back(value);
				}
			}
			if (c == '[')
			{
				++fPos;
				out.type = KCMPdf::kObjArray;
				for (;;)
				{
					SkipWS();
					const int d = fB.At(fPos);
					if (d < 0)
						return false;
					if (d == ']')
					{
						++fPos;
						return true;
					}
					KCMPdf::Object item;
					if (!ParseObject(item, depth + 1))
						return false;
					if (out.items.size() >= kMaxItems)
						return false;
					out.items.push_back(item);
				}
			}
			if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')
			{
				if (!Number(out))
					return false;
				// "n g R"
				if (out.type == KCMPdf::kObjInt && out.intValue >= 0 && out.intValue <= 0xFFFFFFFFll)
				{
					const uint64_t save = fPos;
					int64_t gen = 0;
					if (Integer(gen) && gen >= 0 && gen <= 65535)
					{
						SkipWS();
						if (fB.At(fPos) == 'R' && !IsRegular(fB.At(fPos + 1)))
						{
							++fPos;
							const uint32_t num = static_cast<uint32_t>(out.intValue);
							out = KCMPdf::Object();
							out.type = KCMPdf::kObjRef;
							out.refNum = num;
							out.refGen = static_cast<uint32_t>(gen);
							return true;
						}
					}
					fPos = save;
				}
				return true;
			}
			if (Keyword("true"))
			{
				out.type = KCMPdf::kObjBool;
				out.boolValue = true;
				return true;
			}
			if (Keyword("false"))
			{
				out.type = KCMPdf::kObjBool;
				return true;
			}
			if (Keyword("null"))
				return true;
			return false;
		}

	private:
		KCMPdf::ByteCache& fB;
		uint64_t fPos;

		bool Number(KCMPdf::Object& out)
		{
			uint64_t p = fPos;
			bool neg = false;
			int c = fB.At(p);
			if (c == '+' || c == '-')
			{
				neg = (c == '-');
				++p;
				c = fB.At(p);
			}
			int64_t ip = 0;
			double dv = 0.0;
			int digits = 0;
			bool any = false;
			while (c >= '0' && c <= '9')
			{
				if (digits < 18)
					ip = ip * 10 + (c - '0');
				dv = dv * 10.0 + (c - '0');
				++digits;
				any = true;
				++p;
				c = fB.At(p);
			}
			if (c == '.')
			{
				++p;
				c = fB.At(p);
				double scale = 0.1;
				while (c >= '0' && c <= '9')
				{
					dv += (c - '0') * scale;
					scale *= 0.1;
					any = true;
					++p;
					c = fB.At(p);
				}
				if (!any)
					return false;
				out.type = KCMPdf::kObjReal;
				out.realValue = neg ? -dv : dv;
				fPos = p;
				return true;
			}
			if (!any)
				return false;
			if (digits > 18)
			{
				out.type = KCMPdf::kObjReal;
				out.realValue = neg ? -dv : dv;
			}
			else
			{
				out.type = KCMPdf::kObjInt;
				out.intValue = neg ? -ip : ip;
			}
			fPos = p;
			return true;
		}

		bool Name(std::string& out)
		{
			++fPos;		// the '/'
			out.clear();
			for (;;)
			{
				const int c = fB.At(fPos);
				if (!IsRegular(c))
					break;
				if (c == '#')
				{
					const int h1 = HexValue(fB.At(fPos + 1));
					const int h2 = HexValue(fB.At(fPos + 2));
					if (h1 >= 0 && h2 >= 0)
					{
						out.push_back(static_cast<char>(h1 * 16 + h2));
						fPos += 3;
						continue;
					}
				}
				out.push_back(static_cast<char>(c));
				++fPos;
				if (out.size() > 4096)
					return false;
			}
			return true;
		}

		bool Literal(std::string& out)
		{
			++fPos;		// the '('
			out.clear();
			int depth = 1;
			for (;;)
			{
				const int c = fB.At(fPos);
				if (c < 0)
					return false;
				++fPos;
				if (c == '\\')
				{
					const int e = fB.At(fPos);
					if (e < 0)
						return false;
					++fPos;
					switch (e)
					{
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case 'b': out.push_back('\b'); break;
					case 'f': out.push_back('\f'); break;
					case 13: if (fB.At(fPos) == 10) ++fPos; break;		// a backslash at a line end: the line continues
					case 10: break;
					default:
						if (e >= '0' && e <= '7')
						{
							int v = e - '0';
							for (int k = 0; k < 2; ++k)
							{
								const int d = fB.At(fPos);
								if (d < '0' || d > '7')
									break;
								v = v * 8 + (d - '0');
								++fPos;
							}
							out.push_back(static_cast<char>(v & 0xFF));
						}
						else
							out.push_back(static_cast<char>(e));			// \( \) \\ and anything else: the character
					}
				}
				else if (c == '(')
				{
					++depth;
					out.push_back('(');
				}
				else if (c == ')')
				{
					if (--depth == 0)
						break;
					out.push_back(')');
				}
				else if (c == 13)
				{
					out.push_back('\n');							// an end of line in a string reads as LF (7.3.4.2)
					if (fB.At(fPos) == 10)
						++fPos;
				}
				else
					out.push_back(static_cast<char>(c));
				if (out.size() > kMaxStringBytes)
					return false;
			}
			return true;
		}

		bool Hex(std::string& out)
		{
			++fPos;		// the '<'
			out.clear();
			int high = -1;
			for (;;)
			{
				const int c = fB.At(fPos);
				if (c < 0)
					return false;
				++fPos;
				if (c == '>')
					break;
				if (IsWhite(c))
					continue;
				const int v = HexValue(c);
				if (v < 0)
					return false;
				if (high < 0)
					high = v;
				else
				{
					out.push_back(static_cast<char>(high * 16 + v));
					high = -1;
				}
				if (out.size() > kMaxStringBytes)
					return false;
			}
			if (high >= 0)
				out.push_back(static_cast<char>(high * 16));
			return true;
		}
	};

	/** "endstream" at `pos` after at most a few white-space bytes. */
	bool EndStreamNear(KCMPdf::ByteCache& b, uint64_t pos)
	{
		for (int i = 0; i < 4 && IsWhite(b.At(pos)); ++i)
			++pos;
		return b.Matches(pos, "endstream");
	}

	/** When /Length cannot be trusted: the data runs to the first "endstream", less the end of line before it. */
	bool FindEndStream(KCMPdf::ByteCache& b, uint64_t data, uint64_t& length)
	{
		const uint64_t limit = (b.Size() - data > kMaxEndstreamScan) ? data + kMaxEndstreamScan : b.Size();
		for (uint64_t p = data; p + 9 <= limit; ++p)
		{
			if (b.At(p) != 'e' || !b.Matches(p, "endstream"))
				continue;
			uint64_t end = p;
			if (end > data && b.At(end - 1) == 10)
				--end;
			if (end > data && b.At(end - 1) == 13)
				--end;
			length = end - data;
			return true;
		}
		return false;
	}

	bool Unpredict(const std::vector<uint8_t>& in, size_t colors, size_t bpc, size_t columns, std::vector<uint8_t>& out, std::string& why)
	{
		size_t bpp = (colors * bpc + 7) / 8;
		if (bpp == 0)
			bpp = 1;
		const size_t rowBytes = (columns * colors * bpc + 7) / 8;
		const size_t stride = rowBytes + 1;
		const size_t rows = in.size() / stride;
		out.assign(rows * rowBytes, 0);
		std::vector<uint8_t> prev(rowBytes, 0);
		for (size_t r = 0; r < rows; ++r)
		{
			const uint8_t type = in[r * stride];
			const uint8_t* src = &in[r * stride + 1];
			uint8_t* dst = &out[r * rowBytes];
			for (size_t i = 0; i < rowBytes; ++i)
			{
				const int left = (i >= bpp) ? dst[i - bpp] : 0;
				const int up = prev[i];
				const int upLeft = (i >= bpp) ? prev[i - bpp] : 0;
				int v = src[i];
				switch (type)
				{
				case 0: break;
				case 1: v += left; break;
				case 2: v += up; break;
				case 3: v += (left + up) / 2; break;
				case 4:
				{
					const int p = left + up - upLeft;
					const int pa = std::abs(p - left);
					const int pb = std::abs(p - up);
					const int pc = std::abs(p - upLeft);
					v += (pa <= pb && pa <= pc) ? left : ((pb <= pc) ? up : upLeft);
					break;
				}
				default:
					why = "a stream's PNG predictor has an invalid row type";
					return false;
				}
				dst[i] = static_cast<uint8_t>(v & 0xFF);
			}
			std::memcpy(&prev[0], dst, rowBytes);
		}
		return true;
	}
}	// namespace

//----------------------------------------------------------------------------------------
// Sources
//----------------------------------------------------------------------------------------
KCMPdf::FileSource::FileSource(const wchar_t* path) : fHandle(INVALID_HANDLE_VALUE), fSize(0)
{
	if (path == nullptr)
		return;
	HANDLE h = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return;
	LARGE_INTEGER size;
	if (!::GetFileSizeEx(h, &size) || size.QuadPart < 0)
	{
		::CloseHandle(h);
		return;
	}
	fHandle = h;
	fSize = static_cast<uint64_t>(size.QuadPart);
}

KCMPdf::FileSource::~FileSource()
{
	if (fHandle != INVALID_HANDLE_VALUE)
		::CloseHandle(static_cast<HANDLE>(fHandle));
}

bool KCMPdf::FileSource::IsOpen() const { return fHandle != INVALID_HANDLE_VALUE; }
uint64_t KCMPdf::FileSource::Size() const { return fSize; }

size_t KCMPdf::FileSource::ReadAt(uint64_t offset, uint8_t* buf, size_t n)
{
	if (fHandle == INVALID_HANDLE_VALUE || buf == nullptr || offset >= fSize)
		return 0;
	if (n > fSize - offset)
		n = static_cast<size_t>(fSize - offset);
	size_t done = 0;
	while (done < n)
	{
		OVERLAPPED ov;
		std::memset(&ov, 0, sizeof(ov));
		const uint64_t at = offset + done;
		ov.Offset = static_cast<DWORD>(at & 0xFFFFFFFFu);
		ov.OffsetHigh = static_cast<DWORD>(at >> 32);
		const size_t left = n - done;
		const DWORD want = static_cast<DWORD>((left > 0x40000000u) ? 0x40000000u : left);
		DWORD got = 0;
		if (!::ReadFile(static_cast<HANDLE>(fHandle), buf + done, want, &got, &ov) || got == 0)
			break;
		done += got;
	}
	return done;
}

size_t KCMPdf::MemorySource::ReadAt(uint64_t offset, uint8_t* buf, size_t n)
{
	if (fData == nullptr || buf == nullptr || offset >= fSize)
		return 0;
	if (n > fSize - offset)
		n = static_cast<size_t>(fSize - offset);
	std::memcpy(buf, fData + offset, n);
	return n;
}

//----------------------------------------------------------------------------------------
// Object
//----------------------------------------------------------------------------------------
const KCMPdf::Object* KCMPdf::Object::Get(const char* key) const
{
	if (type != kObjDict && type != kObjStream)
		return nullptr;
	for (size_t i = 0; i < keys.size() && i < items.size(); ++i)
		if (keys[i] == key)
			return &items[i];
	return nullptr;
}

//----------------------------------------------------------------------------------------
// Document
//----------------------------------------------------------------------------------------
KCMPdf::Document::Document()
	: fSource(nullptr), fBytes(nullptr), fEncrypted(false), fObjStmNum(0), fObjStmSource(nullptr), fObjStmBytes(nullptr),
	  fObjStmFirst(0), fDepth(0)
{
}

KCMPdf::Document::~Document()
{
	DropObjStm();
	delete fBytes;
}

void KCMPdf::Document::DropObjStm()
{
	delete fObjStmBytes;
	fObjStmBytes = nullptr;
	delete fObjStmSource;
	fObjStmSource = nullptr;
	fObjStmNum = 0;
	fObjStmOffsets.clear();
	fObjStmData.clear();
	fObjStmFirst = 0;
}

void KCMPdf::Document::SetEntry(uint32_t num, uint8_t type, uint64_t field2, uint32_t field3)
{
	if (num >= kMaxObjects)
		return;
	if (num >= fXref.size())
	{
		Entry unset;
		unset.type = 0xFF;
		unset.field2 = 0;
		unset.field3 = 0;
		fXref.resize(static_cast<size_t>(num) + 1, unset);
	}
	// newest first: the first section to name an object decides it (a free entry hides the older ones too)
	if (fXref[num].type == 0xFF)
	{
		fXref[num].type = type;
		fXref[num].field2 = field2;
		fXref[num].field3 = field3;
	}
}

bool KCMPdf::Document::Open(Source& source, std::string& why)
{
	try
	{
		fSource = &source;
		DropObjStm();
		delete fBytes;
		fBytes = nullptr;
		fBytes = new ByteCache(source);
		fXref.clear();
		fCache.clear();
		fTrailer = Object();
		fEncrypted = false;
		const uint64_t size = fBytes->Size();
		if (size < 16)
		{
			why = "the file is too small to be a PDF";
			return false;
		}
		// the header, within the first 1024 bytes (7.5.2 lets something come before it)
		bool header = false;
		for (uint64_t i = 0; i + 5 <= size && i < 1024 && !header; ++i)
			header = fBytes->Matches(i, "%PDF-");
		if (!header)
		{
			why = "this is not a PDF (no %PDF- header)";
			return false;
		}
		// the last "startxref" in the last 4 KB
		const uint64_t floor = (size > 4096) ? size - 4096 : 0;
		uint64_t found = kNone;
		for (uint64_t i = size - 9; ; --i)
		{
			if (fBytes->At(i) == 's' && fBytes->Matches(i, "startxref"))
			{
				found = i;
				break;
			}
			if (i == floor)
				break;
		}
		if (found == kNone)
		{
			why = "the file has no startxref (it may be cut short)";
			return false;
		}
		Parser p(*fBytes, found + 9);
		int64_t xref = 0;
		if (!p.Integer(xref) || xref < 0)
		{
			why = "startxref has no offset";
			return false;
		}
		std::vector<uint64_t> seen;
		if (!ReadXrefAt(static_cast<uint64_t>(xref), 0, seen, why))
			return false;
		if (fTrailer.Get("Root") == nullptr)
		{
			why = "the trailer has no /Root";
			return false;
		}
		fEncrypted = (fTrailer.Get("Encrypt") != nullptr);
		return true;
	}
	catch (...)
	{
		why = "out of memory reading the cross-reference";
		return false;
	}
}

bool KCMPdf::Document::ReadXrefAt(uint64_t offset, int depth, std::vector<uint64_t>& seen, std::string& why)
{
	if (depth > kMaxChain)
	{
		why = "the cross-reference chain is too long";
		return false;
	}
	if (std::find(seen.begin(), seen.end(), offset) != seen.end())
		return true;		// a loop through /Prev: stop where it closes
	seen.push_back(offset);
	if (offset >= fBytes->Size())
	{
		why = "a cross-reference offset points outside the file";
		return false;
	}
	Parser p(*fBytes, offset);
	if (p.Keyword("xref"))
	{
		// the entries are held until the trailer is read: a hybrid file's /XRefStm must decide first (7.5.8.4 - the
		// table marks the compressed objects free, the hidden stream says where they are)
		struct Pending { uint32_t num; uint8_t type; uint64_t off; uint32_t gen; };
		std::vector<Pending> pending;
		for (;;)
		{
			if (p.Keyword("trailer"))
				break;
			int64_t start = 0;
			int64_t count = 0;
			if (!p.Integer(start) || !p.Integer(count) || start < 0 || count < 0 || start + count > kMaxObjects)
			{
				why = "a cross-reference table cannot be read";
				return false;
			}
			for (int64_t i = 0; i < count; ++i)
			{
				int64_t off = 0;
				int64_t gen = 0;
				if (!p.Integer(off) || !p.Integer(gen) || off < 0 || gen < 0)
				{
					why = "a cross-reference entry cannot be read";
					return false;
				}
				p.SkipWS();
				const int kind = fBytes->At(p.Pos());
				if (kind != 'n' && kind != 'f')
				{
					why = "a cross-reference entry has neither n nor f";
					return false;
				}
				p.SetPos(p.Pos() + 1);
				Pending e;
				e.num = static_cast<uint32_t>(start + i);
				e.type = (kind == 'n') ? 1 : 0;
				e.off = static_cast<uint64_t>(off);
				e.gen = static_cast<uint32_t>(gen);
				pending.push_back(e);
			}
		}
		Object trailer;
		if (!p.ParseObject(trailer, 0) || trailer.type != kObjDict)
		{
			why = "the trailer cannot be read";
			return false;
		}
		if (fTrailer.type == kObjNull)
			fTrailer = trailer;
		const Object* stm = trailer.Get("XRefStm");
		if (stm != nullptr && stm->type == kObjInt && stm->intValue > 0)
			if (!ReadXrefAt(static_cast<uint64_t>(stm->intValue), depth + 1, seen, why))
				return false;
		for (size_t i = 0; i < pending.size(); ++i)
			SetEntry(pending[i].num, pending[i].type, pending[i].off, pending[i].gen);
		const Object* prev = trailer.Get("Prev");
		if (prev != nullptr && prev->type == kObjInt && prev->intValue >= 0)
			return ReadXrefAt(static_cast<uint64_t>(prev->intValue), depth + 1, seen, why);
		return true;
	}
	// a cross-reference stream
	Object xs;
	if (!ParseIndirectAt(offset, 0xFFFFFFFFu, xs) || xs.type != kObjStream)
	{
		why = "no cross-reference at the offset startxref names";
		return false;
	}
	const Object* type = xs.Get("Type");
	if (type == nullptr || !type->IsName("XRef"))
	{
		why = "the object at startxref is not a cross-reference stream";
		return false;
	}
	if (fTrailer.type == kObjNull)
	{
		fTrailer = xs;
		fTrailer.type = kObjDict;
	}
	if (!ReadXrefStream(xs, why))
		return false;
	const Object* prev = xs.Get("Prev");
	if (prev != nullptr && prev->type == kObjInt && prev->intValue >= 0)
		return ReadXrefAt(static_cast<uint64_t>(prev->intValue), depth + 1, seen, why);
	return true;
}

bool KCMPdf::Document::ReadXrefStream(const Object& xs, std::string& why)
{
	const Object* w = xs.Get("W");
	if (w == nullptr || w->type != kObjArray || w->items.size() < 3)
	{
		why = "a cross-reference stream has no /W";
		return false;
	}
	int widths[3];
	for (int i = 0; i < 3; ++i)
	{
		const Object& wi = w->items[static_cast<size_t>(i)];
		if (wi.type != kObjInt || wi.intValue < 0 || wi.intValue > 8)
		{
			why = "a cross-reference stream's /W is invalid";
			return false;
		}
		widths[i] = static_cast<int>(wi.intValue);
	}
	const size_t rowLen = static_cast<size_t>(widths[0] + widths[1] + widths[2]);
	if (rowLen == 0)
	{
		why = "a cross-reference stream's /W is empty";
		return false;
	}
	std::vector<int64_t> index;
	const Object* idx = xs.Get("Index");
	if (idx != nullptr && idx->type == kObjArray)
	{
		for (size_t i = 0; i + 1 < idx->items.size(); i += 2)
		{
			if (idx->items[i].type != kObjInt || idx->items[i + 1].type != kObjInt)
			{
				why = "a cross-reference stream's /Index is invalid";
				return false;
			}
			index.push_back(idx->items[i].intValue);
			index.push_back(idx->items[i + 1].intValue);
		}
	}
	else
	{
		const Object* size = xs.Get("Size");
		if (size == nullptr || size->type != kObjInt)
		{
			why = "a cross-reference stream has no /Size";
			return false;
		}
		index.push_back(0);
		index.push_back(size->intValue);
	}
	std::vector<uint8_t> data;
	if (!StreamData(xs, data, why, kMaxXrefStreamBytes))
		return false;
	size_t at = 0;
	for (size_t s = 0; s + 1 < index.size(); s += 2)
	{
		const int64_t start = index[s];
		const int64_t count = index[s + 1];
		if (start < 0 || count < 0 || start + count > kMaxObjects)
		{
			why = "a cross-reference stream's /Index is out of range";
			return false;
		}
		for (int64_t i = 0; i < count; ++i)
		{
			if (at + rowLen > data.size())
				return true;	// a short stream: keep what was read
			uint64_t f[3] = { 0, 0, 0 };
			for (int k = 0; k < 3; ++k)
				for (int b = 0; b < widths[k]; ++b)
					f[k] = (f[k] << 8) | data[at++];
			const uint64_t type = (widths[0] == 0) ? 1 : f[0];
			if (type <= 2)
				SetEntry(static_cast<uint32_t>(start + i), static_cast<uint8_t>(type), f[1], static_cast<uint32_t>(f[2]));
		}
	}
	return true;
}

bool KCMPdf::Document::ParseIndirectAt(uint64_t offset, uint32_t expectNum, Object& out)
{
	Parser p(*fBytes, offset);
	int64_t num = 0;
	int64_t gen = 0;
	if (!p.Integer(num) || !p.Integer(gen) || !p.Keyword("obj"))
		return false;
	if (expectNum != 0xFFFFFFFFu && num != static_cast<int64_t>(expectNum))
		return false;
	if (!p.ParseObject(out, 0))
		return false;
	if (out.type != kObjDict || !p.Keyword("stream"))
		return true;
	// the data starts after the end of line that follows "stream" (CRLF or LF; a lone CR is tolerated)
	uint64_t data = p.Pos();
	if (fBytes->At(data) == 13)
		++data;
	if (fBytes->At(data) == 10)
		++data;
	uint64_t length = 0;
	bool lengthOK = false;
	const Object* len = out.Get("Length");
	if (len != nullptr)
	{
		Object resolved;
		if (len->type == kObjInt)
			resolved = *len;
		else if (len->type == kObjRef && fDepth < 8)
		{
			DepthGuard guard(fDepth);
			Fetch(len->refNum, resolved);
		}
		if (resolved.type == kObjInt && resolved.intValue >= 0 && data <= fBytes->Size() &&
			static_cast<uint64_t>(resolved.intValue) <= fBytes->Size() - data)
		{
			length = static_cast<uint64_t>(resolved.intValue);
			lengthOK = EndStreamNear(*fBytes, data + length);
		}
	}
	if (!lengthOK && !FindEndStream(*fBytes, data, length))
		return false;
	out.type = kObjStream;
	out.streamOffset = data;
	out.streamLength = length;
	return true;
}

bool KCMPdf::Document::LoadObjStm(uint32_t num)
{
	if (fObjStmNum == num && fObjStmBytes != nullptr)
		return true;
	Object stm;
	if (!Fetch(num, stm) || stm.type != kObjStream)
		return false;
	const Object* n = stm.Get("N");
	const Object* first = stm.Get("First");
	if (n == nullptr || first == nullptr || n->type != kObjInt || first->type != kObjInt || n->intValue < 0 || first->intValue < 0)
		return false;
	std::vector<uint8_t> data;
	std::string why;
	if (!StreamData(stm, data, why, kMaxObjStmBytes))
		return false;
	DropObjStm();
	fObjStmData.swap(data);
	fObjStmSource = new MemorySource(fObjStmData.empty() ? nullptr : &fObjStmData[0], fObjStmData.size());
	fObjStmBytes = new ByteCache(*fObjStmSource);
	fObjStmFirst = static_cast<uint64_t>(first->intValue);
	Parser p(*fObjStmBytes, 0);
	for (int64_t i = 0; i < n->intValue; ++i)
	{
		int64_t objNum = 0;
		int64_t off = 0;
		if (!p.Integer(objNum) || !p.Integer(off) || objNum < 0 || off < 0)
			break;
		fObjStmOffsets.push_back(std::make_pair(static_cast<uint32_t>(objNum), static_cast<uint64_t>(off)));
	}
	fObjStmNum = num;
	return !fObjStmOffsets.empty();
}

bool KCMPdf::Document::ObjectFromObjStm(uint32_t index, uint32_t expectNum, Object& out)
{
	if (fObjStmBytes == nullptr)
		return false;
	size_t at = index;
	if (at >= fObjStmOffsets.size() || fObjStmOffsets[at].first != expectNum)
	{
		at = fObjStmOffsets.size();
		for (size_t i = 0; i < fObjStmOffsets.size(); ++i)
			if (fObjStmOffsets[i].first == expectNum)
			{
				at = i;
				break;
			}
		if (at == fObjStmOffsets.size())
			return false;
	}
	Parser p(*fObjStmBytes, fObjStmFirst + fObjStmOffsets[at].second);
	return p.ParseObject(out, 0);
}

bool KCMPdf::Document::Fetch(uint32_t num, Object& out)
{
	out = Object();
	if (fBytes == nullptr)
		return false;
	try
	{
		std::map<uint32_t, Object>::const_iterator hit = fCache.find(num);
		if (hit != fCache.end())
		{
			out = hit->second;
			return true;
		}
		if (num >= fXref.size())
			return false;
		const Entry e = fXref[num];
		bool ok = false;
		if (e.type == 1)
			ok = ParseIndirectAt(e.field2, num, out);
		else if (e.type == 2 && fDepth < 8 && e.field2 <= 0xFFFFFFFFull)
		{
			DepthGuard guard(fDepth);
			ok = LoadObjStm(static_cast<uint32_t>(e.field2)) && ObjectFromObjStm(e.field3, num, out);
		}
		if (!ok)
		{
			out = Object();
			return false;
		}
		if (fCache.size() > 50000)
			fCache.clear();
		fCache[num] = out;
		return true;
	}
	catch (...)
	{
		out = Object();
		return false;
	}
}

bool KCMPdf::Document::Resolve(const Object& in, Object& out)
{
	if (in.type != kObjRef)
	{
		out = in;
		return true;
	}
	Object cur = in;
	for (int hop = 0; hop < 8; ++hop)
	{
		Object next;
		if (!Fetch(cur.refNum, next))
		{
			out = Object();
			return false;
		}
		if (next.type != kObjRef)
		{
			out = next;
			return true;
		}
		cur = next;
	}
	out = Object();
	return false;
}

bool KCMPdf::Document::StreamData(const Object& stream, std::vector<uint8_t>& out, std::string& why, size_t maxOut)
{
	out.clear();
	try
	{
		if (stream.type != kObjStream || fSource == nullptr)
		{
			why = "not a stream";
			return false;
		}
		if (stream.streamLength > kMaxStreamBytes)
		{
			why = "a stream is too large";
			return false;
		}
		std::vector<uint8_t> raw(static_cast<size_t>(stream.streamLength));
		if (!raw.empty() && fSource->ReadAt(stream.streamOffset, &raw[0], raw.size()) != raw.size())
		{
			why = "a stream runs past the end of the file";
			return false;
		}
		Object filter;
		Object parms;
		const Object* f = stream.Get("Filter");
		if (f != nullptr)
			Resolve(*f, filter);
		const Object* dp = stream.Get("DecodeParms");
		if (dp != nullptr)
			Resolve(*dp, parms);
		if (filter.type == kObjArray)
		{
			if (filter.items.size() > 1)
			{
				why = "a stream has more than one filter";
				return false;
			}
			const Object only = filter.items.empty() ? Object() : filter.items[0];
			filter = only;
			if (parms.type == kObjArray)
			{
				const Object p0 = parms.items.empty() ? Object() : parms.items[0];
				Resolve(p0, parms);
			}
		}
		if (filter.type == kObjNull)
		{
			if (raw.size() > maxOut)
			{
				why = "a stream is too large";
				return false;
			}
			out.swap(raw);
			return true;
		}
		if (!filter.IsName("FlateDecode") && !filter.IsName("Fl"))
		{
			why = "a stream uses the filter /" + filter.text + ", which is not read";
			return false;
		}
		std::vector<uint8_t> inflated;
		const InflateResult r = Inflate(raw.empty() ? nullptr : &raw[0], raw.size(), inflated, maxOut, true);
		if (r != kInflateOK && r != kInflateBadChecksum)
		{
			why = std::string("a stream cannot be decompressed: ") + InflateResultText(r);
			return false;
		}
		int64_t predictor = 1;
		int64_t colors = 1;
		int64_t bpc = 8;
		int64_t columns = 1;
		if (parms.type == kObjDict)
		{
			const Object* v = parms.Get("Predictor");
			if (v != nullptr && v->type == kObjInt) predictor = v->intValue;
			v = parms.Get("Colors");
			if (v != nullptr && v->type == kObjInt) colors = v->intValue;
			v = parms.Get("BitsPerComponent");
			if (v != nullptr && v->type == kObjInt) bpc = v->intValue;
			v = parms.Get("Columns");
			if (v != nullptr && v->type == kObjInt) columns = v->intValue;
		}
		if (predictor <= 1)
		{
			out.swap(inflated);
			return true;
		}
		if (predictor == 2)
		{
			why = "the TIFF predictor is not read";
			return false;
		}
		if (colors < 1 || colors > 32 || bpc < 1 || bpc > 16 || columns < 1 || columns > (1 << 24))
		{
			why = "a stream's predictor parameters are invalid";
			return false;
		}
		return Unpredict(inflated, static_cast<size_t>(colors), static_cast<size_t>(bpc), static_cast<size_t>(columns), out, why);
	}
	catch (...)
	{
		out.clear();
		why = "out of memory reading a stream";
		return false;
	}
}

std::string KCMPdf::TextStringToUtf8(const std::string& b)
{
	std::string out;
	try
	{
		if (b.size() >= 2 && static_cast<uint8_t>(b[0]) == 0xFE && static_cast<uint8_t>(b[1]) == 0xFF)
		{
			for (size_t i = 2; i + 1 < b.size(); i += 2)
			{
				uint32_t u = (static_cast<uint32_t>(static_cast<uint8_t>(b[i])) << 8) | static_cast<uint8_t>(b[i + 1]);
				if (u >= 0xD800 && u <= 0xDBFF && i + 3 < b.size())
				{
					const uint32_t lo = (static_cast<uint32_t>(static_cast<uint8_t>(b[i + 2])) << 8) | static_cast<uint8_t>(b[i + 3]);
					if (lo >= 0xDC00 && lo <= 0xDFFF)
					{
						u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
						i += 2;
					}
				}
				AppendUtf8(out, u);
			}
			return out;
		}
		if (b.size() >= 3 && static_cast<uint8_t>(b[0]) == 0xEF && static_cast<uint8_t>(b[1]) == 0xBB && static_cast<uint8_t>(b[2]) == 0xBF)
			return b.substr(3);
		for (size_t i = 0; i < b.size(); ++i)
			AppendUtf8(out, static_cast<uint8_t>(b[i]));
		return out;
	}
	catch (...)
	{
		return std::string();
	}
}

// End, KCMPdfObjects.cpp.
