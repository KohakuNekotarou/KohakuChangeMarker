//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  TEST-BUILD TRACING, BOTH HALVES (2026-10-05, the user: "KFC has debug code; put it into KCM too if it is
//  needed - so that it can be turned OFF for the Exchange"). The same shape as KFC's model/KFCDiag.h. One line
//  per event to %TEMP%\kcm-diag.txt, each led by the time in milliseconds.
//
//  OFF unless the build defines KCM_DIAG:
//      msbuild build\win\prj\KohakuExtendScriptChangeMarker.vcxproj /p:Configuration=Release /p:Platform=x64
//              /p:KCMExtraDefines=KCM_DIAG
//  (both projects - the model half and KohakuChangeMarkerUI - pass $(KCMExtraDefines) to the compiler;
//  work\kcm-cycle.ps1 -Diag and work\kcm-kidmcp-cycle.ps1 -Diag do it).
//  In a build without it every KCM_DIAG_LOG compiles to nothing - its arguments are not evaluated either -
//  so the shipping .pln holds no call, no format string and no file name (checked before a submission: the
//  .pln must not contain "kcm-diag"). Anything a trace needs that the product does not (a helper that walks
//  state to print it) goes inside #ifdef KCM_DIAG with it.
//  Tested outside InDesign: work\kcm-diag-test (builds the same test both ways).
//
//  Why it exists: the Track Changes mode (2026-10-05) reads InDesign's tracked-change records, makes a copy
//  with every change rejected and follows an Undo / Redo at idle - all of it out of sight. A trace of what
//  was read and decided is the instrument when a live run disagrees with the list.
//
//  AND FAULT SWITCHES. KCM_DIAG_FAULT("name") is true while the file %TEMP%\kcm-diag-fault-<name> exists - a
//  test creates it and deletes it again - so a test build can reach a state the product only gets into after
//  something ELSE has failed. Asked at each call, so the switch takes effect at once. In a build without
//  KCM_DIAG it is the constant false. The switches in use (add one line here for each one added):
//    (none in use. ⛔track-describe-only / track-no-colour stood here on 2026-10-05 for one afternoon, to bisect an undo
//     step a Refresh seemed to drop; the cause was InDesign cutting SCRIPT-made steps at the next real command - the
//     same in the Story mode and after a plain "Add Page" - so they went: memory command-history-and-undo-stack.)
//
//========================================================================================

#ifndef __KCMDiag_h__
#define __KCMDiag_h__

#ifdef KCM_DIAG

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <chrono>

// Appends one line: "<ms since the epoch> <the formatted text>". Opened and closed per line, so a crash
// right after it still leaves the line on disk.
inline void KCMDiagLog(const char* fmt, ...)
{
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return;
	char path[600] = { 0 };
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kcm-diag.txt", temp);
	free(temp);
	FILE* f = nullptr;
	if (fopen_s(&f, path, "a") != 0 || f == nullptr)
		return;
	const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	fprintf(f, "%lld ", ms);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}

#define KCM_DIAG_LOG(...) KCMDiagLog(__VA_ARGS__)

// Is the fault switch <name> on - does %TEMP%\kcm-diag-fault-<name> exist? (See the header.)
inline bool KCMDiagFault(const char* name)
{
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return false;
	char path[600] = { 0 };
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kcm-diag-fault-%s", temp, name);
	free(temp);
	FILE* f = nullptr;
	if (fopen_s(&f, path, "r") != 0 || f == nullptr)
		return false;
	fclose(f);
	return true;
}

#define KCM_DIAG_FAULT(name) KCMDiagFault(name)

#else

#define KCM_DIAG_LOG(...) ((void)0)
#define KCM_DIAG_FAULT(name) false

#endif // KCM_DIAG

#endif // __KCMDiag_h__

// End, KCMDiag.h.
