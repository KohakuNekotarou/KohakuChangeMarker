//========================================================================================
//
//  KCMSentence.h
//
//  A sentence KCM says - fixed English, never a translation key - as a PMString that is marked
//  untranslatable as it is made.
//
//========================================================================================

#ifndef __KCMSentence_h__
#define __KCMSentence_h__

#include "PMString.h"

/** A fixed English sentence as a PMString, made with PMString's constructor for a string that is not a key
	(PMString.h: "Calling Translate on this string will do nothing"; SetTranslatable is in the header's
	DISCOURAGED section) - so it is marked untranslatable as it is made.
	@warning WHY NOT `x = "..."` AND SetTranslatable(kFalse): every `=` hands over the right side's mark with the
	words (PMString's swap and SetString), and `"..."` makes a key, so a mark set before the next `=` is gone after
	it. A PMString handed back by another function keeps its own mark - mark it where it is taken in.
	@warning ASCII ONLY. kEncodingASCII reads the bytes as code page 1252, and the compiler writes a narrow
	literal's other characters in the system's code page (932 on this machine - the build has no /utf-8), so a
	multiplication sign would come out as two wrong characters. A sentence that holds one stays a PMString made
	the old way and marked with SetTranslatable(kFalse). */
inline PMString KCMSentence(const char* words)
{
	return PMString(words, PMString::kEncodingASCII);
}

#endif // __KCMSentence_h__
