//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  The bytes an in-memory stream is written into.
//
//  The SDK does not ship MemXferBytes (public/libs/publiclib/strings/WideString.cpp includes the
//  header, and that header exists nowhere in the SDK), so IXferBytes is implemented here. The
//  shape comes from sdksamples/hostadapter/IDHAMemoryXferBytes.
//
//  ★IT GROWS WITH A NOTHROW BUFFER, not std::string and not std::vector. Both of those throw on
//  a failed allocation, and this runs on the MODEL side, which is reached from export and draw
//  paths -- an exception crossing an event boundary takes InDesign down. The throwaway probe that
//  measured this route used std::string and said in its own comment that this was the thing to
//  rewrite if the route was ever kept (KCMInxProbe.cpp, "Throwaway licence"). This is that
//  rewrite, and it is the reason K2::scoped_array is here rather than a container.
//
//  A failed allocation is reported, not thrown: Write() sets the stream state to bad and keeps
//  what it already had, so the caller can tell "the export is short" from "the export is whole"
//  by asking IsWhole(). Half a document that claims to be whole is the one outcome a comparison
//  must never see.
//
//========================================================================================
#ifndef __KCMResourceBytes_h__
#define __KCMResourceBytes_h__

#include "IXferBytes.h"
#include "K2SmartPtr.h"

/** A growable byte sink for StreamUtil::CreateMemoryStreamWrite that never throws.

    Write() appends at the current position. Read() and Seek() are implemented because IXferBytes
    declares them and the SAX parser reads the same buffer back in the next task; nothing else
    uses them. */
class KCMResourceBytes : public IXferBytes
{
public:
	KCMResourceBytes();
	virtual ~KCMResourceBytes();

	// ----- IXferBytes
	virtual uint32		Read(void* buffer, uint32 num);
	virtual uint32		Write(void* buffer, uint32 num);
	virtual uint64		Seek(int64 numberOfBytes, SeekFromWhere fromHere);
	virtual void		Flush()				{}
	virtual StreamState	GetStreamState()	{ return fState; }
	virtual void		SetEndOfStream()	{}

	// ----- what the caller reads afterwards

	/** How many bytes were written. */
	size_t				Size() const		{ return fSize; }

	/** The bytes themselves, valid until this object dies. nil when nothing was written.
	    ⚠Not null-terminated: it is a byte range, and Size() is its length. */
	const char*			Bytes() const		{ return fData.get(); }

	/** kTrue when every Write() so far was kept. kFalse means an allocation failed and the
	    content is SHORT -- treat it as a failure, never as a smaller document. */
	bool16				IsWhole() const		{ return fState == kStreamStateGood; }

	/** Throws away the content and starts again. */
	void				Reset();

private:
	/** Makes room for at least `need` bytes. kFalse when the allocation failed. */
	bool16				Reserve(size_t need);

	K2::scoped_array<char>	fData;
	size_t					fSize;		// how many bytes are live
	size_t					fCapacity;	// how many are allocated
	size_t					fPos;		// the stream position
	StreamState				fState;
};

#endif // __KCMResourceBytes_h__

// End, KCMResourceBytes.h.
