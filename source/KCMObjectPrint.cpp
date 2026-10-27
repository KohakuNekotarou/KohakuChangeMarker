//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - an object placed in text, as the Story mode compares it (2026-10-27).
//  KCMObjectPrint.h says what and why.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IAnchoredObjectData.h"	// an inline or anchored object's settings - folded in by value
#include "IDataBase.h"
#include "IGraphicFrameData.h"		// what a frame holds - for the reader's word
#include "IHierarchy.h"
#include "IMultiColumnTextFrame.h"
#include "IPersistUIDData.h"		// IID_IQRCODEDATAUID - a QR code's EPS points at the boss holding its content
#include "IPMPersist.h"				// SaveAll - an object's own persistent bytes
#include "IPMStream.h"
#include "IQRCodeMetaData.h"		// GetEncodedString - a QR code's content
#include "ITextFrameColumn.h"		// a text frame's column - the composition, left out
#include "ITextModel.h"
#include "ITextUtils.h"				// CollectOwnedItems - the objects standing in a story

#include "EPSID.h"					// kEPSItem / kSVGItem / IID_IQRCODEDATAUID
#include "GroupID.h"				// kGroupItemBoss
#include "StreamUtil.h"				// CreateMemoryStreamWrite
#include "TextID.h"					// kInlineBoss
#include "Utils.h"

#include <cstdio>					// snprintf - the print in hexadecimal

#include "KCMMemXferBytes.h"
#include "KCMObjectPrint.h"

namespace
{

/** Bytes folded into one 64-bit FNV-1a hash - KFC's Print (KFCObjectSearch.cpp). */
class Print
{
public:
	Print() : fHash(14695981039346656037ULL) {}
	void Add(const void* data, uint32 size)
	{
		const unsigned char* const bytes = static_cast<const unsigned char*>(data);
		for (uint32 i = 0; i < size; ++i)
		{
			fHash ^= static_cast<uint64>(bytes[i]);
			fHash *= 1099511628211ULL;
		}
	}
	void AddNumber(int64 n) { this->Add(&n, sizeof(n)); }
	void AddReal(const PMReal& r) { const double d = ::ToDouble(r); this->Add(&d, sizeof(d)); }
	uint64 Hash() const { return (fHash != 0) ? fHash : 1; }

private:
	uint64	fHash;
};

/** One object's own persistent bytes (SaveAll, clearDirty kFalse - "in all other cases" than the database's own write,
	IPMPersist.h). false = nothing could be written. */
bool PrintObject(IDataBase* db, UID uid, Print& print)
{
	InterfacePtr<IPMPersist> persist(db, uid, UseDefaultIID());
	if (persist == nil)
		return false;
	KCMMemXferBytes bytes;
	try
	{
		// takeOwnership and recycleBoss both kFalse: the bytes live on this frame (StreamUtil.h).
		InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes, kFalse, kFalse));
		if (stream == nil)
			return false;
		persist->SaveAll(stream, kFalse);
		stream->Flush();
	}
	catch (...)
	{
		return false;		// a buffer that could not grow (KCMMemXferBytes.h): no print rather than part of one
	}
	if (bytes.GetStreamState() == kStreamStateFailure)
		return false;
	if (bytes.GetData() != nil && bytes.GetSize() > 0)
		print.Add(bytes.GetData(), bytes.GetSize());
	return true;
}

/** An object and what it holds - a group's members, a frame's picture or QR code or math - each by PrintObject. Not a
	text frame's column objects: they hold the composition (KCMObjectPrint.h). */
bool PrintTree(IDataBase* db, UID uid, Print& print)
{
	if (!PrintObject(db, uid, print))
		return false;
	InterfacePtr<IHierarchy> hier(db, uid, UseDefaultIID());
	if (hier == nil)
		return true;
	const int32 children = hier->GetChildCount();
	for (int32 i = 0; i < children; ++i)
	{
		const UID child = hier->GetChildUID(i);
		InterfacePtr<ITextFrameColumn> column(db, child, UseDefaultIID());
		if (column == nil && !PrintTree(db, child, print))
			return false;
	}
	return true;
}

/** The anchored object's settings (Object > Anchored Object > Options), value by value - not the kInlineBoss's bytes,
	which carry where the anchor was composed (KFC measured typing before the anchor move them). */
void PrintAnchor(IDataBase* db, UID inlineUID, Print& print)
{
	InterfacePtr<IAnchoredObjectData> anchored(db, inlineUID, UseDefaultIID());
	if (anchored == nil)
		return;
	print.AddNumber(anchored->GetPosition());
	print.AddNumber(anchored->GetSpineRelative());
	print.AddNumber(anchored->GetAnchorTypeHorizontal());
	print.AddNumber(anchored->GetObjectHorizontal());
	print.AddNumber(anchored->GetAnchorPtHorizontal());
	print.AddNumber(anchored->GetAnchorTypeVertical());
	print.AddNumber(anchored->GetObjectVertical());
	print.AddNumber(anchored->GetAnchorPtVertical());
	const PMPoint offset = anchored->GetOffset();
	print.AddReal(offset.X());
	print.AddReal(offset.Y());
	print.AddReal(anchored->GetYOffsetAbove());
	print.AddNumber(anchored->GetPinPosition());
	print.AddNumber(anchored->GetLockPosition());
}

/** A reader's word for the object, flattened to one line and kept short. */
std::string OneLine(const std::string& s)
{
	std::string out;
	for (size_t i = 0; i < s.size() && out.size() < 160; ++i)
	{
		const char c = s[i];
		out += (c == '\t' || c == '\r' || c == '\n') ? ' ' : c;
	}
	return out;
}

/** What the object is, for a reader. ★A QR code's content is told (its EPS points at the boss holding it - the shape
	KT's KTQRTab.cpp reads, IPersistUIDData under IID_IQRCODEDATAUID); a math object is an SVG item that carries its
	MathML (docs/ai-notes/kt-mathml-probe-2026-10-25.md) - told apart from a plain SVG only through the scripting DOM,
	so both are named together. */
std::string Describe(IDataBase* db, UID item)
{
	if (db->GetClass(item) == kGroupItemBoss)
		return "Group";
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	const int32 children = (hier != nil) ? hier->GetChildCount() : 0;
	for (int32 i = 0; i < children; ++i)
	{
		const UID child = hier->GetChildUID(i);
		const ClassID kind = db->GetClass(child);
		if (kind == kSVGItem)
			return "SVG / MathML";
		if (kind == kEPSItem)
		{
			InterfacePtr<IPersistUIDData> code(db, child, IID_IQRCODEDATAUID);
			if (code != nil && code->GetUID() != kInvalidUID)
			{
				InterfacePtr<IQRCodeMetaData> content(db, code->GetUID(), UseDefaultIID());
				return (content != nil) ? OneLine("QR: " + content->GetEncodedString().GetUTF8String()) : "QR";
			}
		}
	}
	InterfacePtr<IGraphicFrameData> frame(db, item, UseDefaultIID());
	if (frame != nil)
	{
		InterfacePtr<IMultiColumnTextFrame> columns(frame->QueryMCTextFrame());
		if (columns != nil)
			return "Text frame";
		return frame->HasContent() ? "Graphic" : "Frame";
	}
	return "Object";
}

}	// namespace

bool16 KCMObjectPrintOf(IDataBase* db, UID inlineUID, std::string& outValue)
{
	outValue.clear();
	if (db == nil || inlineUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IHierarchy> anchor(db, inlineUID, UseDefaultIID());
	if (anchor == nil || anchor->GetChildCount() < 1)
		return kFalse;
	const UID item = anchor->GetChildUID(0);
	if (item == kInvalidUID)
		return kFalse;

	// ★READING ONLY: SaveAll with clearDirty kFalse writes nothing back, and the caller's comparison runs inside the
	//   documents' SaveRestoreModifiedState (KCMStoryDiffRun::Run) - this adds a guard of its own for the stamp's call.
	IDataBase::SaveRestoreModifiedState guard(db);
	Print print;
	if (!PrintTree(db, item, print))
		return kFalse;
	PrintAnchor(db, inlineUID, print);

	char hex[24];
	std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(print.Hash()));
	outValue = Describe(db, item);
	outValue += '\t';
	outValue += hex;
	return kTrue;
}

std::string KCMObjectPrintDescription(const std::string& value)
{
	const std::string::size_type tab = value.find('\t');
	return (tab == std::string::npos) ? value : value.substr(0, tab);
}

uint64 KCMObjectsPrintOfStory(ITextModel* model)
{
	if (model == nil)
		return 0;
	const int32 total = model->TotalLength();
	if (total <= 1)
		return 0;
	Utils<ITextUtils> textUtils;
	if (!textUtils)
		return 0;
	OwnedItemDataList owned;
	textUtils->CollectOwnedItems(model, 0, total - 1, &owned);
	IDataBase* const db = ::GetDataBase(model);
	if (db == nil)
		return 0;
	Print fold;
	int32 objects = 0;
	for (int32 i = 0; i < static_cast<int32>(owned.size()); ++i)
	{
		if (owned[i].fClassID != kInlineBoss)
			continue;
		std::string value;
		if (!KCMObjectPrintOf(db, owned[i].fUID, value))
			continue;
		fold.AddNumber(static_cast<int64>(owned[i].fAt));
		fold.Add(value.data(), static_cast<uint32>(value.size()));
		++objects;
	}
	return (objects > 0) ? fold.Hash() : 0;
}

// End, KCMObjectPrint.cpp.
