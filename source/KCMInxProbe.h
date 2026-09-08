//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  THROWAWAY PROBE (2026-09-08). One question, one answer, then delete: this file, its .cpp,
//  the two IDs it needed (KCMID.h + KCMScriptingDefs.h), the Property block in KCM.fr, the
//  branch in KCMScriptProvider.cpp and the two vcxproj entries.
//
//  THE QUESTION. Can the WHOLE document be written as INX into a stream we hold in memory -
//  so that a structural comparison ("what changed that the eye cannot see") needs no file on
//  disk at all? IINXManager::ExportINX takes an IPMStream, which says yes on paper, but the
//  SDK contains NOT ONE CALLER of it - the declaration in IINXManager.h:83 is the only
//  occurrence in the whole source tree. So it is measured rather than assumed.
//
//  WHY IT MATTERS. IDML cannot answer this: it is a UCF package - thirteen files in a zip -
//  and a package does not fit in one stream. INX is the same information as a SINGLE xml
//  document (IDML is INX split into files), so if the whole document comes out of one call,
//  the "compare everything without listing what to compare" route costs no disk at all.
//
//  THE CONTROL IS PART OF THE PROBE. It also exports through ISnippetExport::ExportPageitems,
//  which HAS worked examples (codesnippets/SnpImportExportSnippet.cpp, hostadapter). If the
//  control produces bytes and ExportINX does not, the fault is ExportINX's; if NEITHER
//  produces bytes, the fault is in this file's own memory stream and says nothing about
//  ExportINX at all. Measuring one without the other could not tell those apart.
//
//========================================================================================
#ifndef __KCMInxProbe_h__
#define __KCMInxProbe_h__

#include "PMString.h"

class IScriptRequestData;	// stages 11-12 borrow the request our own property arrived on

/** Run the probe against the active document and report what happened, as plain text.

    Reads the document and writes nothing to it: every route here exports, and an export is a
    read. Nothing is written to disk by design - that is the whole question.

    The report names, for each route: whether it was reached, the ErrorCode, how many bytes came
    back, how long it took in milliseconds, and the first line of what was produced. A route that
    could not even be attempted says which step was missing, because "0 bytes" and "never ran"
    are different answers and must not share a word. */
void KCMRunInxProbe(PMString& out, IScriptRequestData* data);

#endif // __KCMInxProbe_h__

// End, KCMInxProbe.h.
