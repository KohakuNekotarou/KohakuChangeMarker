//========================================================================================
//
//  KCMColorSampler.h
//
//  Alt + left click with the tool samples the raw CMYK at the clicked point, on the side the
//  mouse is over (hover) and on its counterpart (other), and builds the two lines of text that
//  report it. Only a tiny area around the point is rasterised, at high dpi and in CMYK, and the
//  centre pixel of that is read.
//
//  The two sides are hover/other rather than a fixed target/source, because while a comparison is
//  running the two lines have to appear over the **Source window** as well as the Target one. Which
//  way round they go therefore follows the mouse, and **the first line is always the hovered side**.
//
//========================================================================================
#ifndef __KCMColorSampler_h__
#define __KCMColorSampler_h__

#include "BaseType.h"
#include "PMReal.h"		// the sampled point (pasteboard coordinates)
#include "PMString.h"
#include "OMTypes.h"	// UID (which spread the view is showing)

class IDataBase;

// hoverDB       = the document of the window the mouse is over. Its page is what actually gets
//                 hit-tested, and it is the side that appears on the **first line**.
// otherDB       = the counterpart document, resolved through the page pairing and reported on the
//                 second line. nil means solo mode and one line only: no comparison is running,
//                 or the mouse is over some third document that has nothing to do with one.
// hoverIsTarget = kTrue when the hovered side is the comparison's Target (newer), kFalse when it
//                 is the Source (older). It decides which way the pairing is resolved
//                 (KCMMapTargetToSource / KCMMapSourceToTarget) and which line gets the t/s
//                 suffix. Unused in solo mode.
// mx, my        = the point to sample, in hoverDB's **pasteboard (content) coordinates**.
// outPanel  = for the panel's status line (compact, with the t/s abbreviations, because the area
//             is narrow). @warning **do not copy its size into this comment**: the one source of
//             truth is the Frame of kKCMStatusTextWidgetID in ui/KCMUI.fr. The size was written
//             out in several files at once and went stale in all of them.
// outCursor = for drawing on the cursor itself (the label is the single letter t or s). The
//             C/M/Y/K headings are drawn separately by the bitmap cursor in KCMCmykCursor.cpp, so
//             only the rows of numbers belong in this string.
//
// **This samples the point it is given, not "wherever the mouse is".** Working out which window
//   the mouse is over and where inside it belongs to the caller (the UI): a question about windows
//   has no answer without windows, and the model plug-in cannot ask it -- a UI plug-in's boss is
//   invisible (nil) on a background thread.
// @warning **the test for having left the window that was pressed moved to the caller with it.**
//   This function used to compare KCMFindDocDbForView(view) against hoverDB and refuse. **Drop
//   that test and a different window's coordinates get read as hoverDB's page coordinates.** Both
//   callers in KCMCmykCursor.cpp apply it before arriving here.
// viewSpreadUID = **the spread that view is currently showing**.
//   @warning **it has to be observed, not guessed** -- a master spread and an ordinary spread
//     occupy the same pasteboard coordinates, so without it a master being displayed has the
//     colour of an **ordinary** page read and reported as the master's. A value does come back,
//     which is why the mistake cannot be seen. The full reasoning is in KCMCore.h.
bool16 KCMSampleCmykAt(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
                         const PMReal& mx, const PMReal& my,
                         UID viewSpreadUID,
                         PMString& outPanel, PMString& outCursor);

// The hover -> other page mapping cached while Alt + left is held down. Begin builds the mapping
// once, when the button goes down; End throws it away when it is released. In between,
// KCMSampleCmykAt reads the cache instead of rebuilding the whole pairing on every sample (up to
// 20 a second) -- the page structure cannot change while tracking. A one-off sample with no Begin
// builds it each time, as before. Solo mode (otherDB == nil) does not call these: it needs no
// pairing at all.
void KCMSampleCmykBeginDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget);
void KCMSampleCmykEndDrag();

#endif // __KCMColorSampler_h__
