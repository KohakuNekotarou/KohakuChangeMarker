//========================================================================================
//
//  KCMPanelView.cpp
//
//  The panel’s IControlView: PalettePanelView with a floor -- "no smaller than this" -- added.
//
//  ***** WHY IT BECAME NECESSARY *****
//
//  The panel used to be kNotResizable in its PanelList ＝ its edge could not be dragged and the
//  Frame in the .fr was the whole story. Once the Story Edits list went in, the user asked for
//  "the width is too short; make what we have the minimum and stop fixing the size", so it became
//  kIsResizable. Making it resizable leaves nothing to decide the floor, which is what this
//  answers ---- the framework asks **before** resizing, so refusing here stops the drag itself
//  rather than shrinking and springing back.
//
//  ***** IT IS CALLED IN THREE WAYS (measured with a temporary diagnostic build) *****
//
//  This is not "once per resize". The panel manager **asks for the min and the max by passing
//  extreme values**, so one operation brings all three of:
//    - the real resize        req=240x303     -> 240x303      (the size to allow)
//    - the minimum enquiry    req=15x15       -> 224x263      (the floor is taken as the answer)
//    - the maximum enquiry    req=32000x32000 -> 32000x32000 (open) / 32000x185 (closed)
//  ∴ **the ceiling below does not merely stop a drag; it IS the panel’s max height.** That is why
//  the max while closed is the top pane’s height.
//  ★Opening the panel alone brings three such sets (measured). Do not put heavy work in here.
//
//  ***** THE HEIGHT IS BOTH A FLOOR AND, WHILE CLOSED, A CEILING *****
//
//  With Story Edits closed the panel is the top pane alone ---- a block of fixed-coordinate
//  controls kKCMPanelTopPaneHeight tall, with nothing beneath it. Allowing it to stretch would
//  only add an empty band, so while it is closed it is held to exactly that height (user’s call).
//  While it is open it can shrink to the top pane plus the section’s minimum.
//  ★**Do not write the height as a number here**: the designed height of the top pane is decided
//    by the .fr and KCMUIID.h, and it really did move (when the cat illustration came down into
//    the band). A copy of the number here becomes a lie on that day.
//
//  ★The section’s minimum is **not written as a number** either: the splitter is asked for its
//  Bottom snap ---- the value belongs to the .fr (the SplitterPanelWidget in KCMUI.fr), and
//  KCMStorySection.cpp asks the same way for the height it opens to. Writing 60 here would split
//  one decision across two places.
//
//  ⚠★**The top pane’s height alone departs from that principle.** Here it is read from
//  kKCMPanelTopPaneHeight in KCMUIID.h, while DesignedTopPaneHeight in KCMStorySection.cpp takes
//  **the same number from the splitter’s Top snap** ---- and the comment over there says the
//  number belongs beside the resource that describes the layout and should not be repeated. The
//  two agree today (measured), so nothing is broken, but **move one without the other and the
//  target height of closing parts company with the ceiling** ⇒ **always move both**
//  ([[one-question-one-place]]).
//  What keeps this one from asking the splitter as well is below: **an answer is needed even when
//  the splitter cannot be reached**.
//
//  Modelled on KBSPanelView.cpp (which also rounds to whole rows), whose own model was KESCL and
//  before that customconditionaltextui. KCM does not round ---- the top pane is a fixed height, so
//  whether a half row shows below the list depends on where the user stopped dragging, and there
//  is no reason yet to take that in hand.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPanelControlData.h"		// FindWidget ---- looking for the splitter inside ourselves

// An interface published under source/open:
//
// Reached by a relative path rather than by adding an include directory. The reason is written
// beside the same include in KCMStorySection.cpp (the build file that could carry it lives
// outside this plug-in’s repository and would not survive a fresh checkout).
#include "../../open/interfaces/ui/ISplitterPanelControlData.h"

// General includes:
#include "PalettePanelView.h"

// Project includes:
#include "KCMUIID.h"				// kKCMPanelMinWidth / kKCMPanelTopPaneHeight

/** The panel’s view: PalettePanelView with a minimum size added. */
class KCMPanelView : public PalettePanelView
{
public:
	KCMPanelView(IPMUnknown* boss) : PalettePanelView(boss) {}
	virtual ~KCMPanelView() {}

	/** Round a requested size to one that may actually be used.
	    @param dimensions the requested size.
	    @return the size to use.
	*/
	virtual PMPoint ConstrainDimensions(const PMPoint& dimensions) const;
};

CREATE_PERSIST_PMINTERFACE(KCMPanelView, kKCMPanelViewImpl)

/* ConstrainDimensions
*/
PMPoint KCMPanelView::ConstrainDimensions(const PMPoint& desiredDimen) const
{
	PMPoint constrained = desiredDimen;

	// Width ---- the widgets inside are bound to the edges, so growing is free. Only shrinking is
	// stopped.
	if (constrained.X() < PMReal(kKCMPanelMinWidth))
		constrained.X(PMReal(kKCMPanelMinWidth));

	// The floor is at least the top pane; with the section open, its minimum is added.
	PMReal minHeight(kKCMPanelTopPaneHeight);
	bool16 sectionOpen = kFalse;

	InterfacePtr<const IPanelControlData> panelData(this, IID_IPANELCONTROLDATA);
	if (panelData != nil)
	{
		// While the panel is being assembled the splitter may not be there yet. That case is treated
		// **exactly like "closed"** ---- the floor and the ceiling are both the top pane.
		// ⚠★An older comment described this as "guard the top pane but add no ceiling"; sectionOpen
		//   stays kFalse, so **the clamp below applies as it stands**. The implementation was left
		//   alone because **a call with no reachable splitter was never observed**: a temporary
		//   diagnostic build recorded startup, four open/close cycles of the panel and two of the
		//   section, and all twenty-odd calls had splitter=1 (from the very first minimum enquiry at
		//   startup). ∴ rewriting it to "drop the ceiling" would **weaken a ceiling that really is in
		//   effect, for the sake of a situation nobody has seen**.
		InterfacePtr<const ISplitterPanelControlData> splitter(panelData->FindWidget(kKCMSplitterWidgetID), UseDefaultIID());
		if (splitter != nil)
		{
			sectionOpen = !splitter->IsSinglePanelVisible();
			if (sectionOpen)
				minHeight = PMReal(kKCMPanelTopPaneHeight + splitter->GetSplitterSnapBottom());
		}
	}

	if (constrained.Y() < minHeight)
		constrained.Y(minHeight);

	// ★While it is closed the floor is also the ceiling: any height above the top pane only
	//   stretches a band with nothing in it.
	if (!sectionOpen && constrained.Y() > PMReal(kKCMPanelTopPaneHeight))
		constrained.Y(PMReal(kKCMPanelTopPaneHeight));

	return constrained;
}

// End, KCMPanelView.cpp.
