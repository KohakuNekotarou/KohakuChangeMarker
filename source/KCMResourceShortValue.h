//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What a reader sees of one Resources attribute value: everything after the last `/`, with the
//  exporter's percent-escapes undone.
//
//  ★SHARED BY BOTH PLUG-INS AS ONE INLINE FUNCTION (2026-09-13). It stood in ui/KCMResourceValue.cpp
//  while only the panel showed values; the PDF report (model side, KCMReport.cpp) shows the same
//  values and must shorten them the same way, or the report and the panel would disagree about
//  what a value "is". A header-only function with no plug-in state is the one shape both halves
//  can include ([[one-question-one-place]]).
//
//========================================================================================
#ifndef __KCMResourceShortValue_h__
#define __KCMResourceShortValue_h__

#include "PMString.h"
#include "KCMXmlPretty.h"		// KCMDecodePercentEscapes - `%3a` is a colon, not a mojibake

/** What a reader sees of one attribute value: everything after the last `/`.

	★★IDML writes a reference to another definition as a PATH, and the part that identifies it is
	the tail: `$ID/[No paragraph style]` is read as **[No paragraph style]**, `ParagraphStyle/aaa`
	as **aaa** (2026-09-09, the user: "for the Dif side, if there is a `/`, show the last one").
	The head is the namespace and the kind, and the Kind column already says the kind.

	⚠**A DISPLAY RULE, NOT A MODEL ONE.** IKCMResourcesFacade still hands out the whole value:
	that string is the document's own, and it is what pairs the two sides. This shortens one cell.
	⚠**Nothing without a `/` is touched**, and a value ENDING in `/` is left whole rather than
	  shortened to nothing.

	Used by the panel's attribute rows, the band above them, and the PDF report's Resources
	section - so the three never disagree.
*/
inline PMString KCMShortResourceValue(const PMString& value)
{
	// ★PMString's own search, not a loop over bytes: it counts CHARACTERS and is multibyte-safe,
	//   which matters because a definition can be called "見出し/大". The product uses it the same
	//   way to cut a suffix (MediaLocation.h:84, AnimationUIManagePresetsDialogObserver.cpp:308).
	const CharCounter cut = value.LastIndexOfCharacter('/');

	// No separator, or nothing after the last one: the value stands as it is (but still gets its
	// escapes read - see below). ⚠The second case is the guard that matters: shortening "a/" to ""
	// would replace a real value with a blank cell.
	PMString shortened(value);
	if (cut >= 0 && cut + 1 < value.CharCount())
		shortened.Remove(0, cut + 1);

	// ★★TRIMMED FIRST, DECODED SECOND, and the order is not arbitrary: the `/` this cut at is the
	//   exporter's own separator, written plain, while a `/` INSIDE a name arrives as `%2f`.
	//   Decoding first would manufacture a separator that the exporter deliberately escaped, and
	//   the cut would then land inside somebody's style name.
	// ★What this undoes: `スタイルグループ 1%3a段落スタイル 1` reads as `スタイルグループ 1:段落スタイル 1`
	//   (KCMXmlPretty.h carries the measurement and why it is not an encoding fault).
	shortened.SetUTF8String(KCMDecodePercentEscapes(shortened.GetUTF8String()));
	shortened.SetTranslatable(kFalse);
	return shortened;
}

#endif // __KCMResourceShortValue_h__

// End, KCMResourceShortValue.h.
