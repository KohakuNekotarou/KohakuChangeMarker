//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - the Resources mode's QR codes (2026-10-27). KCMResourceQRCode.h says what and why.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "IPageList.h"			// GetPageString - the page as the Pages panel names it

#include "KCMCore.h"				// KCMFramePageUID - a real page or nothing
#include "KCMResourceQRCode.h"

bool16 KCMIsQRCodeElement(const PMString& elementName)
{
	return (elementName == "HyperlinkQRCode" || elementName == "PlainTextQRCode" || elementName == "EmailQRCode"
			|| elementName == "TextMessageQRCode" || elementName == "VCardQRCode") ? kTrue : kFalse;
}

UID KCMUIDOfSelf(const PMString& self)
{
	const int32 length = static_cast<int32>(self.CharCount());
	if (length < 2 || self.GetWChar(0).GetValue() != 'u')
		return kInvalidUID;
	uint32 value = 0;
	for (int32 i = 1; i < length; ++i)
	{
		const uint32 c = self.GetWChar(i).GetValue();
		uint32 digit = 0;
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else
			return kInvalidUID;		// a suffix ("u10aGradientStop0") or a name: not one object's UID
		if (value > 0x0FFFFFFF)
			return kInvalidUID;		// longer than any UID
		value = value * 16 + digit;
	}
	return UID(value);
}

void KCMResolveQRCodePlaces(IDataBase* db, KCMResourceList& items)
{
	if (db == nil)
		return;
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	for (size_t i = 0; i < items.size(); ++i)
	{
		KCMResourceItem& item = items[i];
		if (item.fFrameSelf.IsEmpty())
			continue;
		const UID frame = KCMUIDOfSelf(item.fFrameSelf);
		const UID page = (frame != kInvalidUID) ? KCMFramePageUID(db, frame) : kInvalidUID;
		PMString where;
		if (page != kInvalidUID && pageList != nil)
		{
			// The same call Export Pages to Word names its files with (KCMPageExport.cpp): the page as the panel shows it,
			// hidden spreads counted.
			PMString name;
			pageList->GetPageString(page, &name, kFalse, kFalse, kDefaultPageType, kTrue, kTrue);
			where = "p.";
			where.Append(name);
		}
		else
			where = "pasteboard";
		where.SetTranslatable(kFalse);
		item.fWhere = where;
	}
}

PMString KCMQRCodeDisplayKey(const KCMResourceItem& item)
{
	PMString key;
	key.SetTranslatable(kFalse);
	if (!item.fWhere.IsEmpty())
	{
		key.Append(item.fWhere);
		key.Append(", ");
	}
	if (!item.fName.IsEmpty())
		key.Append(item.fName);
	else
	{
		// The frame's UID in the scripting DOM's spelling (pageItems.itemByID(253)), not the INX's ("ufd").
		key.Append("frame ");
		key.AppendNumber(static_cast<int32>(KCMUIDOfSelf(item.fFrameSelf).Get()));
	}
	return key;
}

// End, KCMResourceQRCode.cpp.
