//========================================================================================
//
//  KCMPanelTitle.h
//
//  Shows **which mode the comparison is in** on the panel's tab (user's instruction):
//    "Kohaku Change Marker - Pixel" / "- Story" / "- Resources" / "- Track", and "- Word Import" while a Word import
//    is compared (2026-10-24 night). (An "- Import" stood here for the fourth mode, gone since 2026-09-20.)
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
//     - the panel was refreshed after a model notification (KCMRefreshPanel in KCMPanelObserver) ----
//       ★the Import mode starts and ends on the MODEL side, from the menu or a script, so this is
//       the only one of these an import passes through (2026-09-17: the tab said "Pixel" throughout)
//     - the panel was shown (KCMPanelObserver::AutoAttach; the widgets are rebuilt every time,
//       but **the tab label belongs to the palette and survives** ---- until the first time the
//       panel is shown there is nowhere to write, and the function below returns quietly on
//       panelView==nil, which is what this call is for)
//     - the saved settings were restored (KCMPanelState)
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

	// ★NOTHING PUTS THE TAB BACK AT SHUTDOWN (2026-10-27 - KFC's block-13 audit, kfc-block13-inspections-2026-10-20.md
	//  §3). A label lives only in the running palette: the saved workspace names a panel by its resource KEY
	//  (OWLPaletteLeaf name="0x1EA600kKFCPanelTitleKey" for KFC's) and the session's SavedData holds no label. Measured
	//  with KT's palettelabel probe: a panel's label set at run time was in neither file after a quit, and the next
	//  start showed the panel's own name. A Restore() did it until then, first thing in the UI half's shutdown - for
	//  the fear that "a name with '- Pixel' on it stays in the workspace", which the measurement answered: it does not.
}

#endif // __KCMPanelTitle_h__
