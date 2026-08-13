//========================================================================================
//
//  KESCMUIShared.h
//
//  Shared UI-side API: the panel, its status line, its navigation readout, and the tool
//  button. Everything here touches widgets.
//
//  Created 2026-08-13 by splitting KESCMCore.h. Behaviour unchanged.
//
//  ★INVARIANT: no model-side file may include this header. When a model-side file needs to
//  tell the UI something, it emits a notification instead (KESCMModelNotify.h). A model
//  plug-in cannot instantiate UI bosses -- the query returns nil with no error and no
//  warning -- so this rule is what keeps the split working after Stage 2.
//
//  ⚠ The invariant does not hold yet. Task 5 created this header precisely so that the
//  violations become countable: every model-side file that includes it is one item of
//  reverse flow. The full list is docs/ai-notes/kescm-reverse-flow-ledger-2026-08-13.md,
//  and Tasks 6-10 empty it.
//
//========================================================================================

#ifndef __KESCMUIShared_h__
#define __KESCMUIShared_h__

#include "BaseType.h"
#include "PMString.h"

class IControlView;

// The plug-in's own panel if it is showing, nil if hidden. The
// "session -> app -> panelMgr -> GetVisiblePanel" idiom lives only here. Absorbs the
// shutdown path where session goes nil.
IControlView*	KESCMGetVisibleOwnPanel();

// Bring the showing panel's ON/OFF display (Target/Source names, icon, toggle label) in line
// with the current armed state. Does nothing when the panel is hidden -- AutoAttach reflects
// the real state on re-show.
void			KESCMRefreshPanel();

// Write the panel's status line. forceRedrawNow=kTrue paints immediately instead of waiting
// for the next event loop (used before a blocking comparison loop).
void			KESCMSetStatus(const PMString& s, bool16 forceRedrawNow = kFalse);

// The Prev/Next position readout ("3/12") and the enabled state of both buttons. Empty
// posText clears. Does nothing when the panel is hidden.
void			KESCMSetNavPosition(const PMString& posText, bool16 navButtonsEnabled);

// Show the tool switch button as pressed / not pressed. Called only from KESCMTool::Select
// and ::Deselect, so the toolbox, the panel button and the shortcut all pass through one
// place. Does nothing when the panel is hidden.
void			KESCMSetToolButtonSelected(bool16 selected);

// Make this plug-in's tool the active tool. Returns kTrue when it actually became active.
// Does nothing in a run configuration without a toolbox.
bool16			KESCMActivateOwnTool();

// Is this plug-in's tool the active tool right now? Used to restore the button's pressed
// look when the panel is rebuilt, instead of writing a fixed default.
bool16			KESCMIsOwnToolActive();

// Open the distribution URL from "About this plug-in" in the default browser. Called when
// the panel illustration is clicked.
void			KESCMOpenAboutURL();

#endif // __KESCMUIShared_h__
