//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarkerUI (KCMUI)
//
//  Which book is showing in the FRONT tab of the Book panel.
//
//  ***** WHY THIS FILE EXISTS: THE LINKER ASKED FOR IT. *****
//
//  This code lived in the model half (KCMBookPair.cpp) until 2026-08-15. Stage 2 Task 9B took
//  WidgetBin.lib off the model project - the one project setting guide vol1-06 names for model
//  plug-ins - and the linker answered with exactly one unresolved symbol:
//
//      KCMBookPair.obj : error LNK2019: unresolved external symbol
//      "PaletteRefUtils::IsPaletteVisible(class PaletteRef const &)"
//      referenced in function "KCMGetPanelBookFile(class IDFile &)"
//
//  Stage 1 had already written this down as a known debt (IKCMBookFacade.h: "KNOWN, AND LEFT TO
//  STAGE 2: ResolveBookPair reaches into the UI itself"), but nothing forced the issue until the
//  library came off. That is the point of Task 9B: Grep misses what nobody thought to search for,
//  the linker does not.
//
//  ***** ONE SYMBOL WAS NAMED; THREE UI DEPENDENCIES MOVED. *****
//
//    PaletteRefUtils::IsPaletteVisible  - a static in WidgetBin.lib -> the linker sees it
//    Utils<IBookUIUtils>()              - resolved at RUN TIME -> returns nil in a background
//                                         thread and is silent everywhere else
//    IPanelMgr (QueryPanelManager)      - same shape as IBookUIUtils
//
//  So the count of link errors is NOT the count of problems. The two run-time lookups would have
//  compiled, linked and shipped, and then answered nil the first time a PDF export ran them on a
//  background thread - with no warning, no assert and no log entry.
//
//  ***** THE SPLIT IS "OBSERVE IN THE UI, DECIDE IN THE MODEL". *****
//
//  The same line Task 4B drew for the peek and the CMYK sampler. This file OBSERVES (walks the
//  registered panels and reports a file); KCMResolveBookPair still DECIDES (that file's book is
//  the Target, the first other open book is the Source). The rules did not move and no behaviour
//  changed - the caller now asks this first and hands the answer over the boundary.
//
//========================================================================================
#ifndef __KCMBookPanelLookup_h__
#define __KCMBookPanelLookup_h__

#include "IDFile.h"

/** The book whose tab is in FRONT in the Book panel.
    kFalse when no front tab can be identified: the panel is iconised, its palette is closed, or
    no book is open.

    ***** This deliberately does NOT fall back to the active book. ***** IBookManager's active book
    does not follow tab switches - it only changes when a chapter is touched - so falling back
    would silently compare a book the user is not looking at. KBS does fall back at this point,
    because a search that picks the wrong book merely reads; a comparison's entire meaning is
    which two books it was run on.

    Pass the result to IKCMBookFacade::ResolveBookPair. An empty/failed answer means "do not
    start" - the facade is not called at all, which is what the old model-side code did when this
    walk returned kFalse. */
bool16 KCMGetPanelBookFile(IDFile& outFile);

#endif // __KCMBookPanelLookup_h__

// End, KCMBookPanelLookup.h.
