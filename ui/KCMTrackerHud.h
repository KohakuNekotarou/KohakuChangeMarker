//========================================================================================
//
//  KCMTrackerHud.h
//
//  **While the left button is held** with the KCM tool active, one line in the **top-left** of the
//    layout view that was pressed says **what that window is to the comparison** (the on-press HUD;
//    user's instruction).
//
//  There are four wordings (the test itself is KCMTrackerHudLabel in KCMTrackerHud.cpp):
//      comparing + the Target window  -> "Target"
//      comparing + the Source window  -> "Source"
//      comparing + anything else      -> "Not in comparison"
//      stopped                        -> "Not comparing"
//    ★The other document's name is NOT shown (user's instruction). The reason is in that function.
//
//  The condition is exactly this: "the KCM tool is active and the left button is down".
//    - It never reaches print or PDF (the drawing side rejects a printing context). "Print
//      comparison marks" being ON changes nothing.
//    - It appears only in the window that was pressed (no other document window, no other view).
//    ⚠An older line here also said "it does not look at Hold to Hide Marks either". **That toggle no
//      longer exists** - it was removed and folded into the two "Always Show Marks on ..." toggles.
//      What the HUD does not look at is the mark toggles in general: it says what the window IS, not
//      what is drawn in it.
//
//  ★★It is drawn on the **Draw Event route**, the very path the comparison frames use, so **it
//    appears together with them** (the old HUD lived in the sprite layer and was drawn by a one-shot
//    timer after the press had been handled, which put it behind the frames - the user reported "it
//    takes a while to appear / it is out of step with the frames / it catches the eye", and it was
//    removed entirely).
//
//  ★★★"A draw event cannot paint in the corner of a view" was WRONG.
//    memory/layout-screen-overlay.md said "the drawing is clipped to the pasteboard", but (1) that is
//    about **kEndSpreadMessage (per spread)**, and (2) the real constraint is not clipping but **Z
//    ORDER**. Correctly:
//      kEndSpreadMessage           ... spread coordinates. Clipped to the band (spread / pasteboard)
//                                      but **in front**
//      kAfterLastSpreadDrawMessage ... pasteboard coordinates. Once per window but **behind**, so it
//                                      shows only where nothing covers the canvas
//    therefore **using both, each pixel is served by exactly one of them** and the whole view is
//    covered with no double drawing. KCM itself did this until its "toast" was removed (git 068d8fb^,
//    KCMDrawEventHandler.cpp:551-563, :900-909, :951-963) - and the knowledge went with it.
//
//========================================================================================

#ifndef __KCMTrackerHud_h__
#define __KCMTrackerHud_h__

#include "BaseType.h"
#include "PMPoint.h"

class IControlView;
class IGraphicsPort;

// The press begins (from the tracker's BeginTracking). view = the layout view that was pressed.
// ★**It asks for the repaint of that window itself**: the drawing happens on a draw event, so with
//   no repaint it would never be drawn at all (the reveal side repaints the Target window only - see
//   the Invalidate comment in KCMTrackerHud.cpp).
void KCMTrackerHudBegin(IControlView* view);

// The press ends or is abandoned (from EndTracking / AbortTracking). Safe to call twice.
// ★It asks for the repaint that clears it, after lowering the flag (symmetric with the above).
void KCMTrackerHudEnd();

// Should this draw paint the HUD? = "the button is down" and "this is the view of the window that
// was pressed". The draw handler uses it as its early-return test as well.
bool16 KCMTrackerHudWantsDraw(IControlView* view);

// Draw the HUD once.
//   gPort         ... the drawing port
//   view          ... gd->GetView() (for the zoom and the visible area)
//   spreadOffset  ... how far this port's coordinates are from the pasteboard's
//                   - kAfterLastSpreadDrawMessage (pasteboard coordinates) -> (0,0)
//                   - kEndSpreadMessage (spread coordinates)               -> that spread's offset
void KCMTrackerHudDraw(IGraphicsPort* gPort, IControlView* view, const PMPoint& spreadOffset);

// At plug-in shutdown: return the font reference it holds (the state is statics alone, so that is
// all it takes).
void KCMTrackerHudShutdown();

#endif // __KCMTrackerHud_h__

// End, KCMTrackerHud.h.
