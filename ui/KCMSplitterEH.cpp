//========================================================================================
//
//  KCMSplitterEH.cpp
//
//  An IEventHandler that accepts nothing. It is put on the panel's splitter bar to make it
//  **impossible to drag**.
//
//  ***** WHY MAKING SOMETHING NOT WORK NEEDS AN IMPLEMENTATION *****
//
//  The bar is part of the SplitterPanelWidget itself (KCMUI.fr), and the grabbing and dragging
//  enter through the IID_IEVENTHANDLER (kSplitterPanelEHImpl) that the stock
//  kSplitterPanelWidgetBoss carries (confirmed in a boss dump from the running application).
//  ★**There is no way to REMOVE an interface from an inherited boss**, so stopping it means
//  "override it with an implementation that answers differently" ---- the same shape as
//  KCMNoTip.cpp silencing a tooltip: that one answers with an empty string, this one answers
//  "I handled no event".
//
//  ***** WHAT IS LOST (＝ nothing but the drag) *****
//
//  This widget is a container for the upper and lower panes; the only surface of its own is the
//  bar. Each child has an event handler of its own, and an event is offered to the widget under
//  the cursor before it travels upward, so a silent container changes nothing about working with
//  what is inside it. That a plain GenericPanelWidget **carries no IID_IEVENTHANDLER at all**
//  (also from the dump) is the other half of that argument: "a container that handles nothing"
//  is the ordinary shape.
//
//  ★Why CEventHandler as the base: CEventHandler.h:36-37 recommends it "for EventFilters or
//  non-widget event handlers" and says to use DVControlEventHandler or a widget handler for a
//  widget. Not following that recommendation here is deliberate, because what is wanted is
//  **no behaviour at all**, and CEventHandler is exactly that -- every method answers kFalse
//  (CEventHandler.cpp:87-117 and the rest) ＝ the goal itself, without a line written. The DV
//  side, IID_IDVEVENTHANDLER from kBaseWidgetBoss, is **left untouched**, so the widget
//  foundation stays as it was.
//
//  ⚠Dragging UPWARDS was already stopped (the Top snap = the designed height of the upper pane;
//  Widgets.fh:418, "slider doesn't move beyond snap pos"). What was left was dragging DOWN,
//  which makes the upper pane taller than its block of fixed-coordinate controls and leaves a
//  band with nothing in it. The section's height is decided by dragging **the panel's edge** ----
//  KCMPanelView::ConstrainDimensions was written on that assumption from the start.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "CEventHandler.h"		// the entry-level IEventHandler (every method answers kFalse)

// Project includes:
#include "KCMUIID.h"

/** An IEventHandler that handles no event. It is the base class, given a name and put on a boss.

	Not one override is needed ---- CEventHandler implements every method of IEventHandler as "did
	not handle it" (kFalse), which is precisely a splitter bar that cannot be grabbed.
*/
class KCMSplitterEH : public CEventHandler
{
public:
	KCMSplitterEH(IPMUnknown* boss);
	virtual ~KCMSplitterEH();
};

CREATE_PMINTERFACE(KCMSplitterEH, kKCMSplitterEHImpl)

KCMSplitterEH::KCMSplitterEH(IPMUnknown* boss) : CEventHandler(boss)
{
}

KCMSplitterEH::~KCMSplitterEH()
{
}

// End, KCMSplitterEH.cpp.
