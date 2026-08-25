//========================================================================================
//
//  KCMPanelTitle.h
//
//  Shows **which mode the comparison is in** on the panel's tab (user's instruction):
//    "Kohaku Change Marker - Pixel" / "Kohaku Change Marker - Story"
//
//  ★★Modelled on the file of the same name in KBS (`KBS/source/KBSPanelTitle.cpp`), which shows
//    its search scope as "- Document" / "- Book" -- the user asked for it "like the document and
//    book in KBS". ⇒ The mechanism is copied whole ([[follow-official-implementation-first]],
//    applied in-house).
//
//  ★**The label belongs to the palette -- the container that draws the tab -- not to the panel.**
//    Take the container with `IPanelMgr::GetPaletteRefContainingPanel`, then
//    `PaletteRefUtils::SetPaletteLabel(..., kTitle_PanelLabel)`.
//
//  ⚠**The plain name cannot be read back**, so this side has to spell it out.
//    (`PaletteRefUtils::GetPaletteLabel` answers empty for a palette that has never been shown,
//     and `IWindow::GetTitle` answers only "the value last SET" ＝ neither of them knows the
//     original name.)
//    ⇒ The plain name is built from `kKCMDisplayName`, the one definition of the display name.
//
//  ★**Callers** -- each of them only "writes the current state", so any of them may run at any
//    time and as often as it likes:
//     - the mode was switched (KCMApplyCompareMode in KCMActionComponent)
//     - the panel was shown (KCMPanelObserver::AutoAttach; the widgets are rebuilt every time,
//       but **the tab label belongs to the palette and survives** ---- until the first time the
//       panel is shown there is nowhere to write, and the function below returns quietly on
//       panelView==nil, which is what this call is for)
//     - the saved settings were restored (KCMPanelState)
//     - the plug-in is shutting down and the name goes back (KCMUIStartup)
//    ⚠**Do not write a count here.** It said "three places" while there were four -- the
//      settings restore arrived later and this line did not follow.
//
//========================================================================================

#ifndef __KCMPanelTitle_h__
#define __KCMPanelTitle_h__

namespace KCMPanelTitle
{
	/** Write the current compare mode onto the tab. Does nothing while there is no panel (safe to
	    call as often as you like). */
	void Update();

	/** Put the plain name back on the tab. Called from the shutdown path. */
	void Restore();
}

#endif // __KCMPanelTitle_h__
