//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  See KCMResourceBytes.h for why this does not use a container.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "KCMResourceBytes.h"

#include <new>
#include <string.h>

/** The first allocation. Measured on real documents: a whole document comes out at 376-388KB, so
    starting at 512KB means the ordinary case allocates once and never copies. Growing doubles,
    so a document several times larger costs a handful of copies rather than hundreds. */
static const size_t kKCMResourceFirstBlock = 512u * 1024u;

KCMResourceBytes::KCMResourceBytes()
	: fSize(0), fCapacity(0), fPos(0), fState(kStreamStateGood)
{
}

KCMResourceBytes::~KCMResourceBytes()
{
}

void KCMResourceBytes::Reset()
{
	fData.reset();
	fSize = 0;
	fCapacity = 0;
	fPos = 0;
	fState = kStreamStateGood;
}

bool16 KCMResourceBytes::Reserve(size_t need)
{
	if (need <= fCapacity)
		return kTrue;

	size_t want = (fCapacity == 0) ? kKCMResourceFirstBlock : fCapacity;
	while (want < need)
	{
		const size_t doubled = want * 2u;
		if (doubled < want)				// size_t wrapped: the request is absurd
			return kFalse;
		want = doubled;
	}

	char* fresh = new (std::nothrow) char[want];
	if (fresh == nil)
		return kFalse;					// reported through fState by the caller; never thrown

	if (fSize > 0 && fData.get() != nil)
		::memcpy(fresh, fData.get(), fSize);

	fData.reset(fresh);					// scoped_array deletes the old block with delete[]
	fCapacity = want;
	return kTrue;
}

uint32 KCMResourceBytes::Read(void* buffer, uint32 num)
{
	if (buffer == nil || num == 0 || fPos >= fSize)
		return 0;

	const size_t left = fSize - fPos;
	const size_t take = (static_cast<size_t>(num) < left) ? static_cast<size_t>(num) : left;
	::memcpy(buffer, fData.get() + fPos, take);
	fPos += take;
	return static_cast<uint32>(take);
}

uint32 KCMResourceBytes::Write(void* buffer, uint32 num)
{
	if (buffer == nil || num == 0)
		return 0;

	const size_t end = fPos + static_cast<size_t>(num);
	if (end < fPos)						// size_t wrapped
	{
		fState = kStreamStateFailure;
		return 0;
	}
	if (!Reserve(end))
	{
		// ★Nothing is half-written: the caller is told 0 bytes went in AND the state is bad, so
		//   a short buffer cannot be mistaken for a smaller document.
		fState = kStreamStateFailure;
		return 0;
	}

	::memcpy(fData.get() + fPos, buffer, num);
	fPos = end;
	if (fPos > fSize)
		fSize = fPos;
	return num;
}

uint64 KCMResourceBytes::Seek(int64 numberOfBytes, SeekFromWhere fromHere)
{
	int64 want = numberOfBytes;
	if (fromHere == kSeekFromCurrent)
		want += static_cast<int64>(fPos);
	else if (fromHere == kSeekFromEnd)
		want += static_cast<int64>(fSize);

	if (want < 0)
		want = 0;
	if (want > static_cast<int64>(fSize))
		want = static_cast<int64>(fSize);

	fPos = static_cast<size_t>(want);
	return fPos;
}

// End, KCMResourceBytes.cpp.
