//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMMemXferBytes.h for why this exists rather than the SDK's own MemXferBytes.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include <cstring>

// Project includes:
#include "KESCMMemXferBytes.h"

/* Constructor
*/
KESCMMemXferBytes::KESCMMemXferBytes()
	: fPosition(0),
	  fStreamState(kStreamStateGood)
{
}

/* Destructor
*/
KESCMMemXferBytes::~KESCMMemXferBytes()
{
}

/* Read
*/
uint32 KESCMMemXferBytes::Read(void* buffer, uint32 num)
{
	if (buffer == nil || num == 0)
		return 0;

	const uint32 stored = static_cast<uint32>(fBuffer.size());
	if (fPosition >= stored)
	{
		fStreamState = kStreamStateEOF;
		return 0;
	}

	uint32 available = stored - fPosition;
	uint32 toTransfer = num;
	if (toTransfer > available)
	{
		toTransfer = available;
		fStreamState = kStreamStateEOF;
	}

	std::memcpy(buffer, &fBuffer[fPosition], toTransfer);
	fPosition += toTransfer;
	return toTransfer;
}

/* Write
   Seeking past the end and then writing is legal for a stream, so the gap is zero-filled
   rather than refused - resize() does that for us.
*/
uint32 KESCMMemXferBytes::Write(void* buffer, uint32 num)
{
	if (buffer == nil || num == 0)
		return 0;

	const uint32 end = fPosition + num;
	if (end > fBuffer.size())
		fBuffer.resize(end, 0);

	std::memcpy(&fBuffer[fPosition], buffer, num);
	fPosition = end;
	return num;
}

/* Seek
*/
uint64 KESCMMemXferBytes::Seek(int64 numberOfBytes, SeekFromWhere fromHere)
{
	if (fStreamState == kStreamStateEOF)
		fStreamState = kStreamStateGood;

	int64 target = 0;
	switch (fromHere)
	{
	case kSeekFromStart:
		target = numberOfBytes;
		break;
	case kSeekFromCurrent:
		target = static_cast<int64>(fPosition) + numberOfBytes;
		break;
	case kSeekFromEnd:
		target = static_cast<int64>(fBuffer.size()) + numberOfBytes;
		break;
	}

	// A negative position is not representable; clamping to the start is what the caller can
	// still work with, and it keeps the memcpy above in range whatever it was asked for.
	if (target < 0)
		target = 0;

	fPosition = static_cast<uint32>(target);
	return fPosition;
}

/* Flush
*/
void KESCMMemXferBytes::Flush()
{
}

/* GetStreamState
*/
StreamState KESCMMemXferBytes::GetStreamState()
{
	return fStreamState;
}

/* SetEndOfStream
*/
void KESCMMemXferBytes::SetEndOfStream()
{
	if (fPosition < fBuffer.size())
		fBuffer.resize(fPosition);
}

/* GetData
*/
const char* KESCMMemXferBytes::GetData() const
{
	if (fBuffer.empty())
		return nil;
	return &fBuffer[0];
}

/* GetSize
*/
uint32 KESCMMemXferBytes::GetSize() const
{
	return static_cast<uint32>(fBuffer.size());
}

// End, KESCMMemXferBytes.cpp.
