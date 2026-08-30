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
//  @warning **THIS std::vector IS WHERE KCM'S TWO MEMORY POLICIES MEET, AND WHICH ONE APPLIES
//   HERE HAS NOT BEEN DECIDED.**
//     - The drawing side allocates with new (std::nothrow) and holds the result in
//       K2::scoped_array, on the stated grounds that MSVC's ordinary new does not return nil but
//       THROWS, and an exception crossing an event boundary brings InDesign down. The 2026-08-27
//       conversion wrote that down as a requirement rather than a habit, and said in as many words
//       that std::vector cannot stand in for it.
//     - The Story-diff engine this file belongs to is built on std containers throughout, and
//       deliberately: not touching the SDK is what lets it be compiled and tested outside
//       InDesign. KCM has no try/catch anywhere, so a bad_alloc raised by the resize below would
//       leave through the menu handler that started the comparison.
//   What makes the two defensible side by side is SIZE -- the drawing buffers are whole CMYK pages
//   (tens of MB), this one holds one story's XML -- but that is an argument, not a measurement,
//   and the 2026-08-27 pass never considered this file at all.
//   **If the argument is accepted, say so here. If it is not, the fix is a K2::scoped_array with
//   the same manual resize both Adobe samples write.**
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
	virtual ~KCMMemXferBytes();

	/** See IXferBytes::Read. Reads what is left from the current position; a request for more
		than that transfers what there is and leaves the stream at end-of-stream.
	*/
	virtual uint32 Read(void* buffer, uint32 num);

	/** See IXferBytes::Write. Grows the buffer as needed, so a caller never has to size it. */
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
