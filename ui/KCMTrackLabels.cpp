//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) UI - see KCMTrackLabels.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDTime.h"
#include "Utils.h"
#include "WideString.h"				// IDTime::DateToString

#include "IKCMCompareFacade.h"		// GetMarkColorCyan - which of the two Mark colours the flyout chose
#include "KCMConstants.h"			// kKCMRingR.. / kKCMRingAltR.. - the Mark colour's two values (KCMDrawEventHandler::SelectedMarkColor reads the same)
#include "KCMTrackLabels.h"

namespace
{
PMString FromUtf16(const char16_t* s, int32 n)
{
	PMString p;
	p.SetXString(reinterpret_cast<const UTF16TextChar*>(s), n);
	p.SetTranslatable(kFalse);
	return p;
}

void AppendTwoDigits(PMString& s, int32 n)
{
	if (n < 10)
		s.Append("0");
	s.AppendNumber(n);
}
}	// namespace

PMString KCMTrackKindSign(int32 kind)
{
	switch (kind)
	{
		case 1:  { PMString s("+"); s.SetTranslatable(kFalse); return s; }
		case 2:  { PMString s("-"); s.SetTranslatable(kFalse); return s; }
		case 3:  return FromUtf16(u"»", 1);	// »
		default: return FromUtf16(u"≠", 1);	// ≠
	}
}

PMString KCMTrackKindWord(int32 kind)
{
	PMString s((kind == 1) ? "Inserted" : (kind == 2) ? "Deleted" : (kind == 3) ? "Moved" : "Replaced");
	s.SetTranslatable(kFalse);
	return s;
}

PMString KCMTrackTimeLabel(uint64 t)
{
	// The shape of KFC's KFCShowChanges.cpp (RunLabel): the OS's short date, then H:MM:SS in local time.
	const IDTime when(t);
	WideString date;
	int32 hour = 0, minute = 0, second = 0;
	PMString label;
	if (when.DateToString(date, true /*short*/) && when.GetTime(nil, nil, nil, &hour, &minute, &second))
	{
		label = PMString(date);
		label.Append(" ");
		label.AppendNumber(hour);
		label.Append(":");
		AppendTwoDigits(label, minute);
		label.Append(":");
		AppendTwoDigits(label, second);
	}
	label.SetTranslatable(kFalse);
	return label;
}

RealAGMColor KCMTrackColour(bool16 hasColour, uint8 r, uint8 g, uint8 b)
{
	if (hasColour)
		return RealAGMColor(r / 255.0, g / 255.0, b / 255.0);
	// The Mark colour: the same two constants KCMDrawEventHandler::SelectedMarkColor answers with, chosen by the same
	// flag (read through the facade - the UI half cannot reach the handler's static).
	Utils<IKCMCompareFacade> compare;
	const bool16 cyan = (compare && compare->GetMarkColorCyan()) ? kTrue : kFalse;
	return cyan ? RealAGMColor(kKCMRingAltR / 255.0, kKCMRingAltG / 255.0, kKCMRingAltB / 255.0)
				: RealAGMColor(kKCMRingR / 255.0, kKCMRingG / 255.0, kKCMRingB / 255.0);
}

PMString KCMTrackAuthorName(const PMString& name)
{
	PMString s = name.IsEmpty() ? PMString("(no name)") : name;
	s.SetTranslatable(kFalse);
	return s;
}

// End, KCMTrackLabels.cpp.
