//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Opens and closes the "Story Edits" section at the bottom of the panel, growing and shrinking
//  the panel itself to match.
//
//  Written from the product implementation of the Links panel's "Link Info" section
//  (LinksUIUtils.cpp:600-783 and ToggleLinkInfoButtonObserver.cpp), which is the same feature.
//  The thing to carry over from there, and the reason this is not three lines of code:
//
//    *** RESIZING TAKES A DIFFERENT ROUTE WHEN THE PALETTE IS DOCKED. ***
//
//  A floating panel answers to IControlView::Resize. A docked one has to be told through
//  PaletteRefUtils::SetPaletteSize, and its min/max has to be recalculated first by
//  IOWLPaletteSizer::UpdateOWLPaletteSizes - "we only recalculate that during panel resize
//  usually...so we have to force a recalculation", as the comment in linksui puts it.
//
//========================================================================================

#ifndef __KESCMStorySection_h__
#define __KESCMStorySection_h__

/** Show the Story Edits section if it is hidden, hide it if it is showing, and resize the panel
	by the height of the section either way. Does nothing when the panel is not on screen.
*/
void KESCMToggleStorySection();

/** Point the toggle button at the collapsed or the expanded triangle so it matches the section's
	real state.

	Called from the button's AutoAttach as well as after a toggle: a panel that is hidden and shown
	again rebuilds its widgets, and a button that drew a fixed default would then contradict the
	section next to it.
*/
void KESCMUpdateStorySectionButtonState();

#endif // __KESCMStorySection_h__

// End, KESCMStorySection.h.
