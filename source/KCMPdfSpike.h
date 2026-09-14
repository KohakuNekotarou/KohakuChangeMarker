//========================================================================================
//
//  KCMPdfSpike.h -- THE EXPERIMENT that has to answer before the Before/After report can
//  lose its temporary files.
//
//  WHAT THE REPORT DOES TODAY (KCMReport.cpp). Each side goes out to a temporary PDF under
//  %TEMP% (kPDFExportCmdBoss; the Before side with sPrintMarks forced on, which is what puts
//  the comparison marks into the picture), and SDKLayoutHelper::PlaceFileInFrame puts one
//  page of that file into the report document. PlaceFileInFrame takes an IDFile, so a FILE
//  on disk was the only way in.
//
//  WHAT THE USER ASKED FOR (2026-09-14): "can this be done without making a temporary file?
//  ... vector would be best ... it can take as long as it needs, and the code may change as
//  much as it needs - this feature could be the star of the plug-in".
//
//  THE ROUTE THIS FILE MEASURES (docs/ai-notes/kcm-report-without-temp-files-2026-09-14.md):
//
//      a page   --kPDFExportItemsCmdBoss, IID_IPMUNKNOWNDATA-->  an IPMStream held in memory
//      that stream --IImportProvider::ImportThis-->              a page item in another document
//
//  ⚠NOTHING HERE IS SETTLED. This file exists to MEASURE, not to ship, and every question it
//    asks is written into the answer it hands back - so a reading says WHICH STEP failed
//    rather than "it did not work". The three that were still open when the route was found:
//      1. can kPDFExportItemsCmdBoss write a whole PAGE (its name says "Items", and the
//         guide's only example hands it selected page items),
//      2. does IID_IDRWEVTHANDLER on that same boss carry the comparison marks into it,
//      3. what does ImportThis hand back, and where does the item land.
//
//  ★★★WHAT THE RUNS ANSWERED (2026-09-14), because these lines are what the report is built on:
//      1. The page UID alone writes an EMPTY sheet; the page UID **with** the items writes the
//         page at its own size (595x841) - which is what kCropToMedia buys today, without a file.
//      2. YES, ONCE THE SPREAD IS IN THE LIST. The marks are drawn once per SPREAD
//         (KCMRingAdornment.cpp:498-504 turns back for every iShape that is not one), and a list
//         of items has no spread in it. Add the spread: +3,031 bytes with sPrintMarks on.
//      3. A page item with NO PARENT - the caller places it.
//
//  ★THE WAY IN is the script method app.kcmProbePdfRoute() (KCM.fr, KCMScriptProvider.cpp),
//    which returns the whole reading as one string, a line per step.
//  ⚠IT READS THE ACTIVE DOCUMENT AND WRITES NOTHING TO IT. What it imports goes into a
//    windowless document of its own, which is closed again before the answer comes back.
//
//========================================================================================

#pragma once
#ifndef __KCMPdfSpike_h__
#define __KCMPdfSpike_h__

class PMString;

/** Run the whole experiment on the active document's first page and describe every step,
    one line each, in `out`. Answers with a single line saying so when there is no active
    document to measure. Leaves no document open and no file behind. */
void KCMProbePdfRoute(PMString& out);

#endif // __KCMPdfSpike_h__

// End, KCMPdfSpike.h.
