//========================================================================================
//
//  KCMPanelState.h
//
//  Saves and restores the "settings" toggles of the panel flyout as a private JSON file in the
//  user's roaming preferences folder. ★**Nothing is written into InDesign's own data** (neither
//  the workspace SavedData nor a document).
//
//  Where: FileUtils::GetAppRoamingDataFolder(.., "KCMPanelState.json")
//    (on Windows) %APPDATA%\Adobe\InDesign\Version XX.0\<locale>\KCMPanelState.json
//  ★No subfolder is created (user's instruction). The place is the same user area as InDesign's
//  preferences, but the file is a private one of ours and unrelated to InDesign's.
//
//  What is saved ＝ the settings toggles only. "The state of the work at this moment" -- the
//  armed state of Start/Stop, or the hidden-spread state of Hide Unchanged -- is not saved,
//  because restoring it has side effects:
//    - Marks opacity (25% or 75%)        key "opacity25"
//    - Mark colour (Red or Cyan)         key "markColorCyan" (kTrue = Cyan / kFalse = Red, the
//                                        default)
//    - Always Show Marks on Target       key "showTgtMarks"
//    - Always Show Marks on Source       key "showSrcMarks"
//      (★the two were renamed from "Show Marks on Target" / "Show Marks on Source", and **the
//       saved keys were not changed**, so an older settings file still reads)
//    - Show Original Page Numbers        key "showOldNumbers"
//    - Sync Layout Views                 key "syncLayoutViews" (default ON. What actually fires
//      a sync is Target<->Source while Started, or every document while stopped with the KCM
//      tool active ＝ see the guard in KCMSyncOtherDocViewportsTo)
//    - Show Scrollbar Map                key "scrollbarMap"
//    - Ignore Page Number Marker         key "ignorePageNumberMarker"
//    - Translucent Panel / Translucent Pages Panel / Translucent Book Dialog (★Windows only.
//      The targets are, in order, our own panel / InDesign’s Pages panel / our book comparison
//      dialog, under "translucentPanel" / "translucentPagesPanel" / "translucentBookDialog".)
//      ★What is restored is the flag alone; applying it to a window is done by the panel’s
//      AutoAttach and by the subscription to the panel visibility (at startup our panel does
//      not exist yet).
//      ★★**Measured that it IS applied right after a restart**: switch the Pages panel ON,
//      Save Panel Settings, restart InDesign, and the Pages panel comes up translucent. The
//      earlier worry -- "it is somebody else’s window, so if the workspace restores it already
//      open it may finish opening before we subscribe" -- did not happen.
//      ⚠But the observation was made **with the KCM panel open as well**, and in that case the
//      AutoAttach in KCMPanelObserver.cpp re-applies both targets, so that may be what worked
//      (whether the kPaletteVisibilityChangedMessage subscription was in time was not
//      separated out).
//      ∴ "start with the KCM panel closed and see whether the Pages panel alone turns
//      translucent" is still unmeasured. Suspect it first if a report says it does not work
//      (the remedy is the Win32 hook in KCMPanelAlpha.cpp, which re-resolves any target whose
//      window is not cached yet, but only on a window event).
//    - Compare mode (Pixel / Story)      key "compareMode". ⚠**The only non-bool**: the value is
//                                        a string ("pixel" / "story"). The reason is at
//                                        KCMJsonReadString in KCMPanelState.cpp
//
//  ★**Deliberately NOT saved** (written down here so it can be told apart from "forgot to save
//    it"):
//    - **Print comparison marks** ...... this toggle changes **what comes out on paper and in a
//      PDF**, not just what is on screen, so every launch starts from the default OFF (user's
//      instruction). ∴ marks reach an output only when they were switched ON deliberately in
//      that session. ⚠**The released 1.3.0 did save it**, so this is a change of behaviour and
//      it is written up in `source/KCMID.h`.
//    - The armed state of Start/Stop, Hide Unchanged, Find Overset ...... "the state of the work
//      at this moment" (above).
//    - The per-page ticks (Check) and the map ...... **a different persistence** with a file of
//      its own (KCMPageCheck.cpp).
//
//  ⚠★★**When a toggle is added, touch this list, the save and the restore in the same commit.**
//    A review found **three misses at once**: ① "Always Show Marks on Target" was saved and
//    restored but **missing from this list**; ② "Compare mode" was missing from it as well;
//    ③ ★**"Mark colour" (Red/Cyan) was neither saved nor restored** ＝ a miss with real
//    consequences: the choice went back to red on every restart. An earlier review had found
//    one of the same kind (Translucent Book Dialog) ＝ **left alone, this list always drifts
//    away from the code.**
//    ⇒ How to check it: count the check-style and radio-style branches in `UpdateActionStates`
//      (KCMActionComponent.cpp), decide for each whether it is a setting or a state of the work,
//      and for a setting look for it in **both places in this file**.
//    ⚠**Do not write how many keys the file holds.** That number stood here as eleven and is
//      twelve today; what has to be kept in step is the list, not a total.
//
//========================================================================================

#ifndef __KCMPanelState_h__
#define __KCMPanelState_h__

// Called from the "Save Panel Settings" flyout item. Writes the current settings toggles to the
// JSON file and shows the full path in a modal dialog (or says so if the write failed).
// The implementation is in KCMPanelState.cpp.
void	KCMSavePanelState();

// Reads the saved JSON file if there is one and applies it to the toggles (does nothing if there
// is not).
// ★When: at startup (KCMUIStartup::Startup). Syncing now runs while stopped with the tool active
//   as well, so a saved setting (Sync OFF in particular) has to take effect before the panel is
//   opened. Everything it restores is an engine-side flag or subscription, dependent on neither
//   the panel nor a document, which is what makes startup safe.
// ★An internal guard runs it once per session, so the existing call from the panel’s AutoAttach
//   stays as a no-op safety net and cannot roll back a setting changed since. The implementation
//   is in KCMPanelState.cpp.
void	KCMLoadPanelStateIfPresent();

#endif // __KCMPanelState_h__
