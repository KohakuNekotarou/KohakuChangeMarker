//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The small helpers the Word round trip's writers share (2026-09-24): the characters of a range of a story, a
//  refusal worded for the panel's message line, and the file's UTF-8 as the strings InDesign takes. ★Each stood
//  in two or more files' anonymous namespaces (KCMRestoreAttr.cpp, KCMApplyFromImport.cpp, KCMStoryAttrPour.cpp,
//  KCMStorySyncApply.cpp); a second copy was the sign to give them one home.
//
//========================================================================================

#ifndef __KCMTextWords_h__
#define __KCMTextWords_h__

#include "BaseType.h"
#include "ITextModel.h"
#include "PMString.h"
#include "TextIterator.h"
#include "WideString.h"

#include "KCMUtf8.h"			// KCMWellFormedUtf8 - what PMStringOfUtf8 hands SetUTF8String (2026-10-27)
#include "KCMSentence.h"		// KCMSentence - Refuse's wording, never a key

#include <string>

namespace KCMTextWords
{

/** A UTF-8 string (the shape's, the file's) as a PMString, marked not translatable.
	⚠THE VALUE IS UTF-8 AND PMString IS NOT: Append(c_str()) would put the bytes in as the platform's encoding,
	 and a custom kenten's own character came out as mojibake that way once (KCMStoryAttrPour measured it on
	 2026-09-22). SetUTF8String also marks the string non-translatable, which is what every caller wants.
	★MADE WELL-FORMED FIRST (2026-10-27 - KFC's block-19 audit, its third small item): SetUTF8String throws on some
	 broken UTF-8 and reads a sequence cut short as another character, and some of these strings come from a Word file
	 the reader imports. KCMWellFormedUtf8 (KCMUtf8.h) puts U+FFFD where a byte does not fit; well-formed bytes pass as
	 they are. */
inline PMString PMStringOfUtf8(const std::string& utf8)
{
	PMString s;
	s.SetUTF8String(KCMWellFormedUtf8(utf8));
	return s;
}

/** The same as the words a text command takes. */
inline WideString WideOfUtf8(const std::string& utf8)
{
	return WideString(PMStringOfUtf8(utf8));
}

/** The characters of [at, at+len) of `model` - kFalse when the range is not inside the story. An empty range
	answers kTrue with an empty string. */
inline bool16 WordsAt(ITextModel* model, TextIndex at, int32 len, WideString& out)
{
	out.Clear();
	if (model == nil || at < 0 || len < 0 || at + len > model->TotalLength())
		return kFalse;
	if (len > 0)
	{
		TextIterator iter(model, at);
		iter.AppendToStringAndIncrement(&out, len);
	}
	return kTrue;
}

/** A refusal for the message line: the wording, marked not translatable as it is made. `text` is ASCII
	(KCMSentence.h says why). */
inline void Refuse(PMString& why, const char* text)
{
	why = KCMSentence(text);
}

}	// namespace KCMTextWords

#endif // __KCMTextWords_h__

// End, KCMTextWords.h.
