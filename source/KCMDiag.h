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
//  AND THE SCRIPT TEST DOORS (2026-10-26, the author: "cut them out when it goes to the Exchange" - the KCM full test,
//  work\kcm-regress): the 13 script properties and methods added since the published 1.5.0 (app.kcmStoryRows,
//  app.kcmApplyChange and the rest) are declared in KCM.fr and served in KCMScriptProvider.cpp inside #ifdef KCM_DIAG -
//  ODFRC is given -d KCM_DIAG by the same test build (KCMOdfrcDefines in the vcxproj). A shipping build publishes the
//  registered seven alone (app.kcmStatus, app.kcmBookResult, the four story counters, the tool's enumerator) - checked by
//  work\kcm-regress\full\ship_check.py, which reads the build's resources for the doors' names and its .pln for "kcm-diag".
//
//  Why it exists: the Track Changes mode (2026-10-05) reads InDesign's tracked-change records (and made a copy
//  with every change rejected, until 2026-10-27) and follows an Undo / Redo at idle - all of it out of sight. A
//  trace of what was read and decided is the instrument when a live run disagrees with the list.
//
//  AND FAULT SWITCHES. KCM_DIAG_FAULT("name") is true while the file %TEMP%\kcm-diag-fault-<name> exists - a
//  test creates it and deletes it again - so a test build can reach a state the product only gets into after
//  something ELSE has failed. Asked at each call, so the switch takes effect at once. In a build without
//  KCM_DIAG it is the constant false. The switches in use (add one line here for each one added):
//    copy-fail   KCMCopyStore::Take keeps no copy (2026-10-24): Apply This Change still goes in, and the "=" row's
//                Restore from Copy is greyed and its message says no copy was kept.
//    step-fail   a write of the Story Edits facade that went in is taken as NOT gone in (EndStep, KCMFacades.cpp): the
//                single step's abort is measured (2026-10-25, tests-m T-M3).
//    batch-fail  the same for a write after the first one of a batch (RunBatch, KCMFacades.cpp): the batch's roll-back.
//    place-pair-off  the Story rows are built WITHOUT pairing stories by their first frame's place (KCMStoryPairByPlace,
//                2026-10-26): a test run with it on shows the Added and Removed rows the pairing exists to join, so a
//                pass without it is known to have measured the pairing (tests-place.ps1 -Armed).
//    focus-off   no "=" record gets a text focus (KCMRecordFoci::Make, 2026-10-26): the records are placed by their
//                readings alone, as before foci - so the full test's equal-follow suite (E12) shows what the foci add.
//                The trace's FOCUS lines (make / move / let / lost / follow / carry) say what the foci did.
//    page-whole-unplaced  a page file's WHOLE box finds no place for its story's body (WholeStoryAt, KCMPageImport.cpp,
//                2026-10-26 night): the refusal a story read unlike the export would get - "cannot be read the way the
//                export reads it", not "an endnote" (table-move-pages PG12).
//    track-read-slow  a SLOW switch (KCM_DIAG_SLOW): each story of the Track mode's reading takes 200 ms longer
//                (KCMTrackRead::Build, 2026-10-27), so its progress bar comes up and its Cancel can be pressed. Without
//                it the bar is out of reach: 15,000 tracked changes were read in 0.2 s, and the bar waits three seconds
//                (track-rf1 RF1-1..5).
//    story-diff-slow  the same for the Story mode's comparison: each story of KCMStoryDiffRun::Run 200 ms longer
//                (2026-10-27) - 50 changed stories were compared in 0.4 s (story-cancel SC-1..5).
//    note-delete-fail  KCMDeleteNoteAt takes no marker out (2026-10-27, the re-inspection's M18): a footnote Word took away
//                stays, as when InDesign refuses the DeleteCmd - and the notes after it must keep their own words
//                (work/note-scripts/2026-10-27-kcm-defect-m18/note_delete_probe.ps1).
//    (none else in use - counted 2026-10-26: step-fail and batch-fail had been in use without a line here.
//     ⛔cmyk-cursor-no-hide stood here on 2026-10-27 for one night: Alt + left's CMYK cursor put up without the
//     ICursorMgr Hide/Show, to measure both on one build. Without it the checkmark went straight to the numbers and
//     nothing else showed, so the Hide/Show went - and the switch with it (KCMTracker::BeginTracking).
//     ⛔track-describe-only / track-no-colour stood here on 2026-10-05 for one afternoon, to bisect an undo
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
#include <thread>		// std::this_thread::sleep_for - KCM_DIAG_SLOW

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

// While the fault switch <name> is on, waits `ms` on the calling thread - a slow switch, for a job that is otherwise
// over before what watches it (a progress bar's three seconds) can be reached. (See the header.)
inline void KCMDiagSlow(const char* name, int ms)
{
	if (KCMDiagFault(name))
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

#define KCM_DIAG_SLOW(name, ms) KCMDiagSlow(name, ms)

#else

#define KCM_DIAG_LOG(...) ((void)0)
#define KCM_DIAG_FAULT(name) false
#define KCM_DIAG_SLOW(name, ms) ((void)0)

#endif // KCM_DIAG

#endif // __KCMDiag_h__

// End, KCMDiag.h.
