//========================================================================================
//
//  KCMReport.h
//
//  The Before/After PDF report (2026-09-13, the user's pick from the "what the task-start INX
//  makes possible" list, widened the same day to three sections): one PDF, landscape.
//    1. a first page: the title, Before (the Source, or the task-start copy rehydrated for the
//       occasion), After (the Target), and "Before" / "After" at the foot over the two columns;
//    2. one page per CHANGED page of the Pixel comparison - the older version on the left with
//       KCM's rings printed into it, the newer on the right, and no text at all; added pages
//       have an empty left side, removed pages an empty right side;
//    3. the Story table - a two-column table, the older wording on the left and the newer on the
//       right, context pale and the change dark, ruby and kenten set for real, note numbers as
//       superscripts;
//    4. the Resources table - the same two columns per differing attribute of each definition.
//  Whatever mode the comparison ran in, the two tables are filled (the detail is borrowed for the
//  report and put back - KCMReport.cpp's StoryDetailLoan / ResourceLoan); the Pixel pages exist
//  only when the Pixel comparison ran, and the first page says so otherwise.
//
//  HOW: both sides are exported to temporary PDFs (kPDFExportCmdBoss, the session's PDF
//  preferences, no UI, no progress bar; the Before side with sPrintMarks forced on), a windowless
//  report document is built (SDKLayoutHelper::CreateDocument), the PDF pages are placed side by
//  side (kSetPDFPlacePrefsCmdBoss to pick the page, then PlaceFileInFrame), the two tables are
//  laid out by KCMReportTable.cpp (pages appended as they overflow), the report is exported where
//  the save dialog said, the report document is closed and the temporaries deleted. Designs:
//  docs/superpowers/specs/2026-09-13-kcm-before-after-report-design.md and
//  docs/superpowers/specs/2026-09-13-kcm-pdf-report-three-sections-design.md.
//
//  MODEL SIDE, no widget touched. Reached through IKCMCompareFacade::ExportBeforeAfterReport
//  from the flyout; the caller shows outMessage on the status line.
//
//========================================================================================
#ifndef __KCMReport_h__
#define __KCMReport_h__

#include "BaseType.h"
#include "PMString.h"

/** Build and write the report for the comparison that is running. kTrue when the PDF was
    written (outMessage names it), kFalse with the reason in outMessage. */
bool16 KCMExportBeforeAfterReport(PMString& outMessage);

#endif // __KCMReport_h__

// End, KCMReport.h.
