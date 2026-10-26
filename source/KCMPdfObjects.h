//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the PDF reader's object layer: the file, its cross-reference, its objects.
//
//  WHAT IT READS (ISO 32000-1 7.3-7.5): the trailer and the cross-reference - classic tables (7.5.4), cross-reference
//  streams with PNG predictors (7.5.8), incremental updates through /Prev and the hybrid /XRefStm (7.5.8.4) - indirect
//  objects, object streams (7.5.7), and a stream's data through /FlateDecode (KCMPdfInflate.h).
//  WHAT IT DOES NOT: decryption (an /Encrypt in the trailer is reported and nothing is read through it), filters other
//  than Flate, and repairing a file whose cross-reference cannot be followed (it is refused, with the reason).
//
//  PURE AND SAFE: no InDesign header (work/kcm-pdf-test builds it outside InDesign); every byte read is bounds-checked;
//  nesting, chain lengths, object counts and decoded sizes are capped; no exception leaves a public function.
//  ⚠NAMES: the plug-in's precompiled headers bring <windows.h> in, whose macros include GetObject - hence Fetch - and
//  the SDK has a great many k-names - hence the kObj prefix on the object kinds.
//
//  THREADS: a Document belongs to its caller; nothing static is written.
//
//========================================================================================
#ifndef __KCMPdfObjects_h__
#define __KCMPdfObjects_h__

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace KCMPdf
{
	/** Random access to the bytes of a PDF. */
	class Source
	{
	public:
		virtual ~Source() {}
		virtual uint64_t Size() const = 0;
		/** Up to `n` bytes from `offset` into `buf`; answers how many were copied (0 past the end). */
		virtual size_t ReadAt(uint64_t offset, uint8_t* buf, size_t n) = 0;
	};

	/** A file on disk, read through Win32 (KCM ships for Windows only). Opened for reading, shared with any writer. */
	class FileSource : public Source
	{
	public:
		explicit FileSource(const wchar_t* path);
		virtual ~FileSource();
		bool IsOpen() const;
		virtual uint64_t Size() const;
		virtual size_t ReadAt(uint64_t offset, uint8_t* buf, size_t n);
	private:
		void* fHandle;
		uint64_t fSize;
		FileSource(const FileSource&);
		FileSource& operator=(const FileSource&);
	};

	/** Bytes in memory - the tests, and an object stream's decoded body. The bytes must outlive it. */
	class MemorySource : public Source
	{
	public:
		MemorySource(const uint8_t* data, size_t size) : fData(data), fSize(size) {}
		virtual uint64_t Size() const { return fSize; }
		virtual size_t ReadAt(uint64_t offset, uint8_t* buf, size_t n);
	private:
		const uint8_t* fData;
		size_t fSize;
	};

	enum ObjType { kObjNull, kObjBool, kObjInt, kObjReal, kObjName, kObjString, kObjArray, kObjDict, kObjRef, kObjStream };

	/** One PDF object. An array keeps its elements in `items`; a dictionary keeps its keys (without the '/') in `keys` and
		the values in `items`, index for index; a stream is its dictionary plus where its raw data lies in the file. */
	struct Object
	{
		ObjType type;
		bool boolValue;
		int64_t intValue;
		double realValue;
		std::string text;				// kObjName: the decoded name; kObjString: the decoded bytes
		std::vector<std::string> keys;
		std::vector<Object> items;
		uint32_t refNum;
		uint32_t refGen;
		uint64_t streamOffset;			// kObjStream: the first byte of the raw data
		uint64_t streamLength;			// kObjStream: its length, resolved

		Object() : type(kObjNull), boolValue(false), intValue(0), realValue(0.0), refNum(0), refGen(0), streamOffset(0), streamLength(0) {}

		bool IsNumber() const { return type == kObjInt || type == kObjReal; }
		double Number() const { return (type == kObjInt) ? static_cast<double>(intValue) : realValue; }
		bool IsName(const char* name) const { return type == kObjName && text == name; }
		/** A dictionary's (or a stream dictionary's) value for `key`, unresolved; nullptr when it has none. */
		const Object* Get(const char* key) const;
	};

	class ByteCache;	// a page cache over a Source (KCMPdfObjects.cpp)

	class Document
	{
	public:
		Document();
		~Document();
		/** The header, the trailer and the whole cross-reference chain. kFalse, with `why`, when the file is not a PDF or
			its cross-reference cannot be followed. `source` must outlive the Document. */
		bool Open(Source& source, std::string& why);
		bool IsEncrypted() const { return fEncrypted; }
		const Object& Trailer() const { return fTrailer; }
		/** Object `num`, parsed from the file or from its object stream. kFalse when there is none. */
		bool Fetch(uint32_t num, Object& out);
		/** `in` itself, or what a reference points to (followed a few hops at most). */
		bool Resolve(const Object& in, Object& out);
		/** A stream's data, decoded (Flate, PNG predictors). kFalse with `why` for another filter or damaged data. */
		bool StreamData(const Object& stream, std::vector<uint8_t>& out, std::string& why, size_t maxOut);

	private:
		struct Entry
		{
			uint8_t type;		// 0xFF unset, 0 free, 1 at offset field2, 2 in object stream field2 at index field3
			uint64_t field2;
			uint32_t field3;
		};
		Source* fSource;
		ByteCache* fBytes;
		std::vector<Entry> fXref;
		Object fTrailer;
		bool fEncrypted;
		std::map<uint32_t, Object> fCache;
		uint32_t fObjStmNum;				// the object stream whose objects are at hand (0 = none)
		std::vector<uint8_t> fObjStmData;	// its decoded bytes
		MemorySource* fObjStmSource;		// over fObjStmData
		ByteCache* fObjStmBytes;			// over fObjStmSource - kept, so the next object of the same stream reads at once
		std::vector<std::pair<uint32_t, uint64_t> > fObjStmOffsets;
		uint64_t fObjStmFirst;
		int fDepth;

		bool ReadXrefAt(uint64_t offset, int depth, std::vector<uint64_t>& seen, std::string& why);
		bool ReadXrefStream(const Object& xrefStream, std::string& why);
		bool ParseIndirectAt(uint64_t offset, uint32_t expectNum, Object& out);
		bool LoadObjStm(uint32_t num);
		bool ObjectFromObjStm(uint32_t index, uint32_t expectNum, Object& out);
		void DropObjStm();
		void SetEntry(uint32_t num, uint8_t type, uint64_t field2, uint32_t field3);
		Document(const Document&);
		Document& operator=(const Document&);
	};

	/** A text string (ISO 32000-1 7.9.2.2: UTF-16BE with its BOM, UTF-8 with its BOM, or PDFDocEncoding) as UTF-8.
		PDFDocEncoding is read as Latin-1 (the two differ only in 0x18-0x1F and 0x80-0x9F). */
	std::string TextStringToUtf8(const std::string& bytes);
}

#endif // __KCMPdfObjects_h__

// End, KCMPdfObjects.h.
