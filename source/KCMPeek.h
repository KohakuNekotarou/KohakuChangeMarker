//========================================================================================
//
//  KCMPeek.h
//
//  The tool's "peek". Holding a modifier and the tool's left button shows the older version of the
//  spread under the cursor -- or samples its CMYK -- and releasing puts everything back. This file
//  owns the peek state (the armed target and source databases, the held-down flags) and the
//  startup/shutdown service. The arm, disarm and state accessors are declared in KCMCore.h.
//
//  Three areas that belong to the UI live elsewhere and are named here only so they can be found:
//    - viewport synchronisation (Sync Layout Views / Align Other Views) -> KCMViewSync.h
//    - the Alt + left CMYK cursor                                       -> KCMCmykCursor.h
//    - gesture recognition and what is shown while held (RevealBegin/End) -> KCMPeekGesture.h
//  What stays here is the model's half -- the comparison, the armed state, the close sweep -- and
//  the KCMPeekStartup service that starts and ends it.
//
//========================================================================================
#ifndef __KCMPeek_h__
#define __KCMPeek_h__

#include "BaseType.h"
#include "PMReal.h"
#include "OMTypes.h"		// UID (which spread the view is showing)

class IDataBase;

// The "base" on-screen opacity of the permanently visible marks, decided by the print setting:
// the chosen 25%/75% while printing marks is on, and 1.0 while it is off. The body is in
// KCMPeek.cpp.
// **Three callers**:
//     - KCMDoSetPrintMarks (KCMCore.cpp) ....... the print-marks and opacity toggles, applied at once
//     - KCMTrackerRevealEnd (ui/KCMPeekGesture.cpp) ... **releasing a reveal** (a press with no
//       modifier). The branches that release a Shift-style peek only clear sShowOriginal and never
//       touch the opacity, whose held-down value SetPeekOpacity keeps separately
//     - the opacity toggle in KCMActionComponent.cpp ... the permanent marks, updated at once
//   @warning both UI callers reach it through the Facade's GetBaseScreenOpacity(), so **grep for
//   both spellings**.
PMReal KCMBaseScreenOpacity();

// Lay the older version of the spread at **the given point** over the current one (the body is in
// KCMPeek.cpp). targetDB is the document being displayed (the newer one) and sourceDB the older
// one laid over it. A spread already in the cache is reused immediately; otherwise it is
// rasterised there and then. Only ever one spread is held.
//   mx, my    = the point to peek at, in targetDB's **pasteboard (content) coordinates**
//   viewScale = that window's content -> window scale (zoom x device scale), which the
//               rasterisation resolution is derived from
//   uiZoom    = that window's UI zoom (the magnification the user sees, without the device scale).
//               **Zero or less means "the panorama could not be read"**, and then viewScale is
//               used as it stands, with no 50% floor applied.
//
// **This peeks at the point it is given, not "wherever the mouse is".** Resolving the view, its
//   panorama and the mouse's coordinates belongs to the caller (the UI).
//   **The UI observes and the model decides**: the 50% floor and the 16..300 dpi clamp are
//     resolution policy and live in the model, and the formula itself did not change when the two
//     were separated.
// The only caller is KCMTrackerBeginPeek in KCMPeekGesture.cpp, through the Facade.
//
// viewSpreadUID = **the spread that view is currently showing**.
//   @warning **it has to be observed, not guessed** -- a master spread and an ordinary spread
//     occupy the same pasteboard coordinates, so without it a master being displayed has an
//     ordinary spread peeked at instead, and **nothing appears at all**: the image that was built
//     is of an ordinary page while the spread being drawn is a master. The full reasoning is in
//     KCMCore.h.
void KCMPeekShowAt(IDataBase* targetDB, IDataBase* sourceDB,
                     const PMReal& mx, const PMReal& my,
                     const PMReal& viewScale, const PMReal& uiZoom,
                     UID viewSpreadUID);

// The last line of defence: are the armed Target and Source still in the IDocumentList (the body
// is in KCMPeek.cpp)? If either is not, KCMHandleDocsClosed() performs the full clean-up that Stop
// would (disarming included) and kFalse comes back.
// Called from KCMPeekGesture.cpp (starting a peek) and KCMCmykCursor.cpp (pressing for CMYK, the
//   cursor colour, and the liveness check during a drag). It is the guard that keeps a released
//   IDataBase from reaching the sampler or the peek.
//   @warning every caller reaches it through the Facade's ArmedDocsAlive(), so **grep for both
//   spellings**. This list had the CMYK press filed under KCMPeekGesture.cpp, where a grep for the
//   model-side name finds nothing at all.
bool16 KCMArmedDocsAlive();

// The body behind the Pages panel context-menu item "Refresh Page Comparison": re-detects the
// comparison for the selected pages and updates their frames and thumbnails. It runs only while a
// comparison is armed, in the **Pixel mode**, and with the Target as the frontmost document (see
// the note on KCMRefreshComparisonAvailable below).
// outPages = how many pages were actually re-compared, outChanged = how many of those changed,
// outCancelled = whether the progress bar's Cancel stopped it (all may be nil). The return says
// whether at least one page was processed.
// A large enough selection brings up a progress bar with Cancel. **Cancelling keeps what has been
// updated so far**: the rest simply stay as they were, which is the same state as running the
// command on a narrower selection. The page being processed when Cancel is pressed is finished, so
// a cancelled run returns kTrue with outCancelled set; kFalse means only that nothing was eligible.
// The body is in KCMPeek.cpp; KCMActionComponent.cpp calls it.
bool16 KCMRefreshComparisonForSelectedPages(int32* outPages, int32* outChanged, bool16* outCancelled = nil, int32* outFailed = nil);

// Whether that menu item should be enabled (for UpdateActionStates in KCMActionComponent.cpp):
// kTrue while a comparison is armed, in the **Pixel mode**, with the Target frontmost. Over the
// Source it is disabled, which in a context menu means the item is not shown at all.
// **Always kFalse in the Story mode**, which rasterises no page, so pressing it would change
//   nothing on screen. The Story mode's own "refresh" is the row context-menu item "Refresh Story
//   Comparison". The full reasoning is with the body in KCMPeek.cpp.
bool16 KCMRefreshComparisonAvailable();

#endif // __KCMPeek_h__
