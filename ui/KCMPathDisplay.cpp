//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMPathDisplay.h for what is shown and why.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Project includes:
#include "KCMPathDisplay.h"

/* KCMPathForDisplay
*/
PMString KCMPathForDisplay(const PMString& path)
{
	PMString out(path);
	out.SetTranslatable(kFalse);	// a path is data - never a translation key

	// PMString has no character-replace of its own (PMString.h offers IndexOfWChar / Remove /
	// Insert and nothing between), so walk the separators. Each replacement is one character for
	// one character, so the next search can start just past the one just written.
	const PMString forwardSlash("/");
	for (CharCounter at = out.IndexOfWChar(UTF32TextChar('\\'));
		 at >= 0;
		 at = out.IndexOfWChar(UTF32TextChar('\\'), at + 1))
	{
		out.Remove(at, 1);
		out.Insert(forwardSlash, at);
	}

	return out;
}

// End, KCMPathDisplay.cpp.
