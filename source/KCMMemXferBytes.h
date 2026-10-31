//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  An IXferBytes that keeps everything in memory, so a snippet can be exported without a file
//  ever existing.
//
//  StreamUtil::CreateMemoryStreamWrite takes an IXferBytes, and the SDK ships no usable
//  implementation of one: public/libs/publiclib/strings/WideString.cpp:37 includes
//  "MemXferBytes.h", but that header appears nowhere in the SDK, so the class it names is
//  product-internal and cannot be reached from plug-in code. **TWO samples implement one**, and
//  they are the same design twice over: hostadapter/IDHAMemoryXferBytes (a char* buffer, 256KB to
//  start) and printmemorystream/PrtMemMemoryXferBytes (a uint8* buffer, 1MB to start), each
//  growing itself through its own resizeBuffer. This is that idea rewritten over std::vector so
//  the buffer grows by itself, and with the written bytes handed out directly - reading them back
//  through a second stream, the way the samples do, buys nothing when the caller wants the bytes.
//
//  ⚠**NO EXCEPTION LEAVES Write.** std::vector grows by throwing, and this buffer is written from
//   inside InDesign's own exporter and IPMPersist::SaveAll: an exception crossing InDesign's code
//   takes the application down (memory k2-scoped-ptr-and-array - the drawing side's whole CMYK
//   pages are new (std::nothrow) in a K2::scoped_array on the same grounds). A buffer that cannot
//   grow is a failed write: nothing transferred, and the stream stays failed - Seek does not clear
//   it. ⇒ **WHOEVER KEEPS THE BYTES READS GetStreamState AFTER THE LAST WRITE**: part of a story
//   comes out of an export that says nothing. (KFC's and KIDMCP's copies of this class do the same.)
//
//========================================================================================

#ifndef __KCMMemXferBytes_h__
#define __KCMMemXferBytes_h__

#include "IXferBytes.h"

#include <vector>

/** A memory-backed IXferBytes for exporting to a stream that never touches disk.
	@ingroup KCM
*/
class KCMMemXferBytes : public IXferBytes
{
public:
	KCMMemXferBytes();

	/** See IXferBytes::Read. Reads what is left from the current position; a request for more
		than that transfers what there is and leaves the stream at end-of-stream.
	*/
	virtual uint32 Read(void* buffer, uint32 num);

	/** See IXferBytes::Write. Grows the buffer as needed, so a caller never has to size it. A buffer that
		cannot grow transfers nothing and fails the stream (the head of this file).
	*/
	virtual uint32 Write(void* buffer, uint32 num);

	/** See IXferBytes::Seek. Seeking past the end is allowed and does not grow the buffer -
		the next Write is what grows it, which is the behaviour a stream expects.
	*/
	virtual uint64 Seek(int64 numberOfBytes, SeekFromWhere fromHere);

	/** See IXferBytes::Flush. Nothing is buffered elsewhere, so this does nothing. */
	virtual void Flush();

	/** See IXferBytes::GetStreamState. */
	virtual StreamState GetStreamState();

	/** See IXferBytes::SetEndOfStream. Truncates to the current position. */
	virtual void SetEndOfStream();

	/** Back to nothing written, a failed write forgotten - a second attempt on the same buffer. */
	void Clear();

	/** The bytes written so far. Not null-terminated - always pair it with GetSize().
		@return pointer to the first byte, or nil when nothing has been written.
	*/
	const char* GetData() const;

	/** How many bytes GetData points at. */
	uint32 GetSize() const;

private:
	std::vector<char> fBuffer;
	uint32 fPosition;
	StreamState fStreamState;
};

#endif // __KCMMemXferBytes_h__

// End, KCMMemXferBytes.h.
