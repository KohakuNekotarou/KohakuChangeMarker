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
//  (LinksUIUtils.cpp:606-783 and ToggleLinkInfoButtonObserver.cpp), which is the same feature.
//  ⚠The start of that range said 600 until 2026-08-18 (bug recheck B-U4), which is inside the
//  function BEFORE it; 606 is where the section's own constants begin.
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

	The section reopens at the height it was closed at. That height is kept on the section's own
	widget and persists across restarts (kKESCMStorySectionPanelBoss in KCMUI.fr). Closing always
	returns the panel to its designed height rather than subtracting the section, so the top pane
	cannot be left taller than the block of controls it holds.
	⚠That last sentence used to say "so a dragged divider cannot leave a dead strip behind", which
	stopped being the reason on 2026-08-12 when the divider was made undraggable. The behaviour is
	unchanged and is still what is wanted - see the note at the closing branch in the .cpp.

	★MEASURED 2026-08-18 (bug recheck B-U4): open 303 -> closed 185 -> reopened 303 px.
*/
void KESCMToggleStorySection();

/** Point the toggle button at the collapsed or the expanded triangle so it matches the section's
	real state.

	Called from the button's AutoAttach as well as after a toggle: a panel that is hidden and shown
	again rebuilds its widgets, and a button that drew a fixed default would then contradict the
	section next to it.
*/
void KESCMUpdateStorySectionButtonState();

/** Put the current row count in the section heading: "Story Edits (3)" while a comparison is
	running, or the bare "Story Edits" when none is.

	★The count lives in the heading rather than on the status line because the status line has no
	room left: its box holds four lines and they are all spoken for, so one more would push
	"failed=N" out of sight. The heading also keeps the count readable while the section is closed,
	which the status line could not do for a list nobody can see.

	Does nothing when the panel is not on screen.
*/
void KESCMUpdateStorySectionLabel();

#endif // __KESCMStorySection_h__

// End, KESCMStorySection.h.
