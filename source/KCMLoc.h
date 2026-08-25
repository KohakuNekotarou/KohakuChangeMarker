//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuExtendScriptChangeMarker (KCM)
//
//  Runtime Japanese switching. There is no jaJP string table: every locale reads the enUS
//  table, and only the few places that have to speak Japanese are swapped here. The test is
//  the UI language, not the feature set - PMLocaleId carries the two independently - so
//  Japanese also appears on a Roman engine running a Japanese UI.
//
//  This side owns exactly one Japanese string: kHideConfirm below, the confirmation for Hide
//  Unchanged, which stayed here because that command modifies the document. The rest live in
//  ui/KCMLoc.h, next to the code that uses them. Read that file instead of counting them
//  again here.
//
//  Which strings get a Japanese version is decided by the user asking for it, not by the
//  nature of the text: the Compare Books confirmation is deliberately English even though it
//  is a confirmation too.
//
//  ***** This file is UTF-8 with BOM ***** so that the u"..." literals stay readable as
//  Japanese. (Without the BOM, MSVC reads them as CP932.)
//
//========================================================================================

#ifndef __KCMLoc_h__
#define __KCMLoc_h__

#include <string>

#include "LocaleSetting.h"
#include "PMLocaleIds.h"
#include "PMString.h"

#include "KCMID.h"

namespace KCMLoc
{
	/** Is the UI language Japanese? */
	inline bool JapaneseUI()
	{
		return LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP;
	}

	/** Japanese text on a Japanese UI, the key translated through the enUS table otherwise.
	    The result is finished text, marked untranslatable. */
	inline PMString Text(const char* englishKey, const char16_t* japanese)
	{
		PMString s;
		if (JapaneseUI())
		{
			s.SetXString(reinterpret_cast<const UTF16TextChar*>(japanese),
				static_cast<int32>(std::char_traits<char16_t>::length(japanese)));
		}
		else
		{
			PMString k(englishKey);
			k.Translate();
			s = k;
		}
		s.SetTranslatable(kFalse);
		return s;
	}
}

// The one Japanese string that stayed on this side. Its English counterpart key
// (kKCMHideConfirmKey) is in KCMID.h and KCM_enUS.fr, which is what an English UI reads.
namespace KCMJa
{
	// ----- Confirmation for Hide Unchanged Spreads (it modifies the document) -----
	const char16_t kHideConfirm[] = u"この機能はファイルに変更を加えます。構いませんか?";

}

#endif // __KCMLoc_h__

// End, KCMLoc.h.
