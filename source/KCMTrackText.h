//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - a tracked change's words: its sign, its kind, its time, its author's name.
//
//  ONE PLACE FOR BOTH HALVES (2026-10-27): the panel (ui/KCMTrackLabels.cpp forwards here - the row's sign, the message
//   area's heading, the author row) and the Before/After report's Tracked Changes table (KCMReport.cpp). They lived in the
//   UI half (2026-10-05) until the report - a model file - needed the same words. Header-only, because the UI half
//   compiles no model .cpp.
//
//========================================================================================
#ifndef __KCMTrackText_h__
#define __KCMTrackText_h__

#include "IDTime.h"
#include "PMString.h"
#include "WideString.h"			// IDTime::DateToString

#include "KCMTrackPlan.h"		// KCMTrackChangeKind

namespace KCMTrackText
{
	/** A PMString of n UTF-16 units, untranslatable. */
	inline PMString FromUtf16(const char16_t* s, int32 n)
	{
		PMString p;
		p.SetXString(reinterpret_cast<const UTF16TextChar*>(s), n);
		p.SetTranslatable(kFalse);
		return p;
	}

	/** The Delta column's sign: NOT EQUAL TO (U+2260) a replacement, "+" an insertion, "-" a deletion, the right-pointing
		double angle quotation mark (U+00BB) a move. The two outside ASCII are written as UTF-16 escapes, and this file
		stays ASCII (cpp-japanese-needs-bom: a narrow literal of them builds and draws as something else). */
	inline PMString KindSign(int32 kind)
	{
		switch (kind)
		{
			case kKCMTrackInsert:	{ PMString s("+"); s.SetTranslatable(kFalse); return s; }
			case kKCMTrackDelete:	{ PMString s("-"); s.SetTranslatable(kFalse); return s; }
			case kKCMTrackMove:		return FromUtf16(u"\u00BB", 1);
			default:				return FromUtf16(u"\u2260", 1);
		}
	}

	/** The message area's word: Replaced / Inserted / Deleted / Moved. */
	inline PMString KindWord(int32 kind)
	{
		PMString s((kind == kKCMTrackInsert) ? "Inserted" : (kind == kKCMTrackDelete) ? "Deleted"
				 : (kind == kKCMTrackMove) ? "Moved" : "Replaced");
		s.SetTranslatable(kFalse);
		return s;
	}

	/** A record's time as the OS's short date and the local time with seconds - the shape of KFC's KFCShowChanges.cpp
		(RunLabel). Empty when the time cannot be read. */
	inline PMString TimeLabel(uint64 t)
	{
		const IDTime when(t);
		WideString date;
		int32 hour = 0, minute = 0, second = 0;
		PMString label;
		if (when.DateToString(date, true /*short*/) && when.GetTime(nil, nil, nil, &hour, &minute, &second))
		{
			label = PMString(date);
			label.Append(" ");
			label.AppendNumber(hour);
			label.Append(minute < 10 ? ":0" : ":");
			label.AppendNumber(minute);
			label.Append(second < 10 ? ":0" : ":");
			label.AppendNumber(second);
		}
		label.SetTranslatable(kFalse);
		return label;
	}

	/** An author's name for the list: "(no name)" when the record holds none - or only spaces (2026-10-25, the author's
		answer to Review Focus 2: InDesign refuses an empty user name but takes " ", and the author row drew as a colour
		square with nothing after it). Spaces, tabs, no-break and ideographic spaces count as nothing; anything else is a
		name and is shown as written. */
	inline PMString AuthorName(const PMString& name)
	{
		bool16 visible = kFalse;
		const WideString w(name);
		for (WideString::const_iterator it = w.begin(); it != w.end() && !visible; ++it)
		{
			const UniCodePoint c = *it;
			visible = (c != 0x0020 && c != 0x0009 && c != 0x00A0 && c != 0x3000) ? kTrue : kFalse;
		}
		PMString s = visible ? name : PMString("(no name)");
		s.SetTranslatable(kFalse);
		return s;
	}
}

#endif // __KCMTrackText_h__

// End, KCMTrackText.h.
