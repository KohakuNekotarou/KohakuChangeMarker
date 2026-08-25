//========================================================================================
//
//  KCMPageNumberMarker.h
//
//  Finding the text frames that carry an automatic page number (Type > Insert Special Character
//  > Markers > Current Page Number) so their area can be left out of the pixel comparison.
//
//  WHY IT EXISTS. Even when added and removed pages are registered and the rest re-pairs
//  correctly, an automatic folio numbers differently in the two versions (a deletion shifts
//  everything after it), so a page whose design is identical still prints a different number and
//  a CMYK pixel comparison goes on reporting it as changed.
//  A folio normally sits on the MASTER page and is not overridden on the page itself, so finding
//  it means looking at the applied master spread's items as well as the local ones.
//
//  How it is used: just before the CMYK comparison loop in KCMDrawEventHandler::MakeEntry, the
//  folio frames' rectangles are collected for the target page and for its partner (in page inner
//  coordinates), converted to the comparison resolution's pixel coordinates, and the pixels
//  inside them are left out of the difference test.
//
//========================================================================================
#ifndef __KCMPageNumberMarker_h__
#define __KCMPageNumberMarker_h__

#include "BaseType.h"		// bool16
#include "PMReal.h"
#include "PMRect.h"			// PMRect
#include "UIDRef.h"			// UIDRef
#include <vector>

class IDataBase;

// The flyout's "Ignore Page Number Marker" toggle. Session-only, and off by default
// (sIgnorePageNumberMarker in the implementation is where the default actually lives).
bool16	KCMGetIgnorePageNumberMarker();
void	KCMSetIgnorePageNumberMarker(bool16 on);

// Append the rectangles of the text frames that actually draw a "Current Page Number" marker on
// pageRef's page, in points with that page's inner bbox Left/Top as the origin. It does NOT clear
// outRects -- the intended use is to pile the target's and the source's frames into one list.
// Both local items and (un-overridden) master-derived items are examined.
// There is no need to call it while the toggle is off; ask KCMGetIgnorePageNumberMarker first.
// The conversion to pixel coordinates (at the comparison resolution) is the caller's, in
// KCMDrawEventHandler.cpp, where Int32Rect is already in scope.
void	KCMAppendPageNumberMarkerRects(const UIDRef& pageRef, std::vector<PMRect>& outRects);

// The cached form of the above. The green wash that shows the excluded area is drawn for every
// page on every draw event, and measuring it each time means walking every item on the page,
// every character in each frame, the master page's items, the wax and the glyph bboxes.
//   refresh=kTrue  ... always measure and update the cache (for MakeEntry, which is comparing)
//   refresh=kFalse ... return the cache if there is one, otherwise measure once and remember
// **Not only for speed.** What the green wash shows is "the area this COMPARISON excluded", so
// drawing the rectangles the comparison settled on is also the correct meaning: measuring afresh
// on every draw meant that moving a folio frame after a comparison moved the green while the
// marks stayed where the comparison had put them.
// The return is a reference into the cache. std::map is node-based, so adding other pages does
// not invalidate it; it stays valid until the same page is re-fetched with refresh=kTrue or the
// Invalidate below is called.
const std::vector<PMRect>&	KCMGetPageNumberMarkerRects(const UIDRef& pageRef, bool16 refresh);

// Throw the whole cache away. Call it when what is remembered can no longer be trusted:
//   - the Ignore Page Number Marker toggle changes ... which also gives the reader a way to force
//     a re-measure by switching it off and on;
//   - a document closes (KCMHandleDocsClosed) ... so no entry outlives its database;
//   - Shutdown ... so a static container hands no heap over at unload (KCM's rule everywhere).
// The IDataBase* in the key is for comparison only and is never dereferenced (KCM's rule).
void	KCMInvalidatePageNumberMarkerRects();

#endif // __KCMPageNumberMarker_h__
