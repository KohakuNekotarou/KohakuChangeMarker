//========================================================================================
//
//  KCMReportPaws.h
//
//  A trail of cat paws across the PDF report's first page (2026-09-13, the user's ask: "on the
//  first page, cat-paw marks - footprints on a diagonal, in two colours").
//
//  The paws are REAL PAGE ITEMS of the report document - closed splines built from the same
//  five-outline table the paw stamp tool draws with (kKCMPawOutlines) and filled with the same
//  two shades (KCMPawColours) as RGB swatches made in the report document - so they print into
//  the PDF like anything else on the page, with no drawing hook involved. The report document
//  is a throwaway, so its swatch list is ours to add to.
//
//  MODEL SIDE. Returns with no interface held on the report document.
//
//========================================================================================
#ifndef __KCMReportPaws_h__
#define __KCMReportPaws_h__

#include "BaseType.h"
#include "PMRect.h"
#include "UIDRef.h"

class IDataBase;

/** Walk a trail of paws across `page` (spread coordinates) - a different walk every time: its
    two ends, its winding, its count and which colour leads are drawn from the clock, inside the
    page's free area. Left and right paws alternate on either side of the line, red and blue
    alternate too, and each paw faces the way the trail goes. `layer` is the page's content layer.
    Quietly does less when a shape or a swatch cannot be made - the report is still written. */
void KCMReportDrawPawTrail(IDataBase* reportDB, const UIDRef& layer, const PMRect& page);

#endif // __KCMReportPaws_h__

// End, KCMReportPaws.h.
