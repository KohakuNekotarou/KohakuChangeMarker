//========================================================================================
//
//  KCMReport.h
//
//  The Before/After report (2026-09-13, the user's pick from the "what the task-start INX
//  makes possible" list): one PDF, landscape, one page per CHANGED page of the comparison -
//  the older version on the left (the Source, or the task-start copy rehydrated for the
//  occasion), the newer on the right with KCM's rings printed into it - and a summary page in
//  front (the two documents, the mode, the counts, the Story Edits rows, the Resources line).
//  Changed pages only (the user's decision): the pages that carry a ring, plus the added pages
//  (left side empty, "added") and the removed pages (right side empty, "removed").
//
//  HOW: both sides are exported to temporary PDFs (kPDFExportCmdBoss, the session's PDF
//  preferences, no UI, no progress bar; the After side with sPrintMarks forced on), a windowless
//  report document is built (SDKLayoutHelper::CreateDocument), the PDF pages are placed side by
//  side (kSetPDFPlacePrefsCmdBoss to pick the page, then PlaceFileInFrame), captions are typed
//  into text frames, the report is exported next to the Target as
//  "<Target name>.compare-report.pdf" (or to the Desktop when the Target was never saved), the
//  report document is closed and the temporaries deleted. Design:
//  docs/superpowers/specs/2026-09-13-kcm-before-after-report-design.md.
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
