//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  A STEP LOG FOR THE RESOURCES MODE, OFF BY DEFAULT. One switch, one file, shared by the
//  snapshot, the parse and the diff (until 2026-09-12 each of the three carried its own copy of
//  these ten lines, two of them writing to the same file under different names).
//
//  ⚠kKCMResourceLogging MUST STAY kFalse in anything shipped: it opens, appends to and closes a
//    file at every step, and the path exists only on the author's machine. Turn it on for one
//    build while chasing a fault in this mode, then turn it back off.
//
//  ★WHY IT EXISTS: A CRASH TAKES THE RETURN VALUE WITH IT. Twice on 2026-09-09 a function in this
//    mode killed InDesign outright - the first parse dereferenced a nil service registry, and
//    ExportINX was handed a cloned database - and the crash report could say only which function.
//    The log said which STEP: "the last line is `about to call ExportINX`, and Reset() came back
//    fine" is what ruled Reset() out and produced the clone guard in KCMResourceSnapshot.cpp. A
//    file survives the process; a string being returned does not.
//
//========================================================================================
#ifndef __KCMResourceLog_h__
#define __KCMResourceLog_h__

#include "BaseType.h"

#include <stdio.h>

static const bool16 kKCMResourceLogging = kFalse;

static const char* const kKCMResourceLogPath =
	"C:/Users/user/Desktop/plugin_sdk_21.0.0.192/work/kcm-resource-log.txt";

/** Appends one line. Does nothing while the switch is off. */
inline void KCMResourceLog(const char* text)
{
	if (!kKCMResourceLogging)
		return;
	FILE* f = nil;
	if (::fopen_s(&f, kKCMResourceLogPath, "a") == 0 && f != nil)
	{
		::fprintf(f, "%s\n", text);
		::fclose(f);
	}
}

/** Appends one line with a number after it. Does nothing while the switch is off. */
inline void KCMResourceLogNum(const char* text, int32 n)
{
	if (!kKCMResourceLogging)
		return;
	FILE* f = nil;
	if (::fopen_s(&f, kKCMResourceLogPath, "a") == 0 && f != nil)
	{
		::fprintf(f, "%s %d\n", text, static_cast<int>(n));
		::fclose(f);
	}
}

#endif // __KCMResourceLog_h__

// End, KCMResourceLog.h.
