//========================================================================================
//
//  KCMReportPlace.h -- one page of a document, into the report, WITHOUT A FILE.
//
//  WHAT IT REPLACES. The Before/After report used to write both documents out to temporary
//  PDFs under %TEMP% and place pages of those files with SDKLayoutHelper::PlaceFileInFrame,
//  which takes an IDFile - so a file on disk was the only way in. The user asked for that to
//  stop (2026-09-14), with one constraint that decided the shape of everything here: "raster
//  is hard to read when you zoom in", so it has to stay VECTOR.
//
//  THE ROUTE, all of it measured before any of it was written
//  (docs/ai-notes/kcm-report-without-temp-files-2026-09-14.md):
//
//      the page UID + the items on it + the master's items
//                 --kPDFExportItemsCmdBoss, IID_IPMUNKNOWNDATA-->  an IPMStream in memory
//                 --IImportProvider::ImportThis-->                 a page item in the report
//                 --kAddToHierarchyCmdBoss-->                      on the report's layer
//                 --ITransformFacade::TransformItems-->            in the box the report gives
//
//  ★THE THREE THINGS THAT MAKE IT WORK, none of them obvious, each one measured:
//    1. THE PAGE UID MUST BE IN THE LIST. Without it the box is the items' bounding box
//       (530x737 on the test page); with it, the page's own size (595x841) - which is what
//       SetCropTo(kCropToMedia) bought on the old route.
//    2. THE MARKS COME FROM sMarksOnPage. kPDFExportItemsCmdBoss draws the items it is handed
//       and never draws the spread, and the marks are drawn once per SPREAD - so without that
//       flag the PDF comes out byte-identical whether the marks are on or off. Handing it the
//       spread instead does bring them, but then the box is the whole spread, TWO PAGES WIDE
//       on a facing-pages document, and the report shows one changed page.
//    3. WHAT ImportThis HANDS BACK HAS NO PARENT. It is a page item in the report's database
//       and nothing else - putting it on a layer and moving it into the box is this file's job.
//
//  ⚠WHY NOT CARRY THE ITEMS THEMSELVES (a snippet), which would be even more vector than a
//    PDF: the report holds the OLD and the NEW side by side, and a style of the same name can
//    hold different values in the two documents (the user's observation, 2026-09-14). Page
//    items brought across take their style NAMES with them, so the two sides would fight over
//    one definition and the picture would be wrong. A PDF has no styles - what it carries is
//    the result - so the two sides cannot interfere.
//
//========================================================================================

#pragma once
#ifndef __KCMReportPlace_h__
#define __KCMReportPlace_h__

#include "PMRect.h"
#include "UIDRef.h"

class PMString;

/** Put one page of `sourceDB` into the report, inside `box`, with no file anywhere.

	@param sourceDB   IN the document the page belongs to (the report's Source or its Target).
	@param pageUID    IN the page. Its own UID goes into the export list, which is what makes
	                     the picture page-sized rather than item-sized.
	@param layer      IN the report layer the imported item is added to.
	@param box        IN where it goes, in that layer's parent coordinates - the same rectangle
	                     the old route handed PlaceFileInFrame.
	@param withMarks  IN kTrue for the Before side: the comparison marks are drawn into the PDF.
	                     ⚠For the length of the export this raises **three** things and puts all
	                     three back whatever happens (ScopedMarkFlags): sPrintMarks, sMarksOnPage,
	                     and sMarkScreenOpacity - the last because sMarksOnPage takes the SCREEN
	                     route, which blits at that value, and it holds 1.0 (opaque) while nothing
	                     is being shown. Without it the marks reach the PDF fully opaque whatever
	                     the panel says - measured, and spotted by the user, on 2026-09-14.
	@param why        OUT what went wrong, when it did.
	@return kTrue when the page is in the report. */
bool16 KCMPlacePageIntoReport(IDataBase* sourceDB, UID pageUID, const UIDRef& layer,
							  const PMRect& box, bool16 withMarks, PMString& why);

#endif // __KCMReportPlace_h__

// End, KCMReportPlace.h.
