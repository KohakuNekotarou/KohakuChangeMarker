//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Two helpers the change row's actions share (2026-09-24): the characters of a range of a story, and a refusal
//  worded for the panel's message line. ★They stood in two files' anonymous namespaces (KCMRestoreAttr.cpp and
//  KCMRedoFromWord.cpp) after stage 2 B and C; a second copy was the sign to give them one home.
//
//========================================================================================

#ifndef __KCMTextWords_h__
#define __KCMTextWords_h__

#include "BaseType.h"
#include "ITextModel.h"
#include "PMString.h"
#include "TextIterator.h"
#include "WideString.h"

namespace KCMTextWords
{

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

/** A refusal for the message line: the wording, marked not translatable. */
inline void Refuse(PMString& why, const char* text)
{
	why = text;
	why.SetTranslatable(kFalse);
}

}	// namespace KCMTextWords

#endif // __KCMTextWords_h__

// End, KCMTextWords.h.
