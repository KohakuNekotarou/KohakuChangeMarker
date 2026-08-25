//========================================================================================
//
//  $File: $
//
//  Owner:
//
//  $Author: $
//
//  $DateTime: $
//
//  $Revision: $
//
//  $Change: $
//
//  Copyright 1997-2012 Adobe Systems Incorporated. All rights reserved.
//
//  NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance
//  with the terms of the Adobe license agreement accompanying it.  If you have received
//  this file from a source other than Adobe, then your use, modification, or
//  distribution of it requires the prior written permission of Adobe.
//
//========================================================================================
//
// KCMUI = the UI half of Kohaku Change Marker. The model half is
// source/KCMFactoryList.h (comparison engine, drawing, the five facades).
//
// ⚠**A MISSING ENTRY FAILS IN COMPLETE SILENCE** -- an implementation that carries its own
//   `CREATE_PMINTERFACE` is simply never built when it is not listed here, and nothing warns.
//   ∴**never add to this table from memory**: grep for **both** `CREATE_PMINTERFACE` **and**
//   `CREATE_PERSIST_PMINTERFACE` and reconcile the two results one for one against this file.
//   ★The persistent form is the one that gets missed -- the tool and the self-drawn views
//   use it, and a grep for the plain form alone will not show them.
//
// ★★**THE TEMPLATE'S OWN About IS GONE AND MUST NOT COME BACK** (KCMUIActionComponent.cpp
//   with its boss, its impl, its ActionID and the KCMUI.fr entries). Two reasons:
//     ①**Its ActionID would be +0, and so is this plug-in's own About** -- the IDs were moved
//       here with their offsets preserved, so the two collide.
//     ②An ActionComponent belongs on the UI side, so KCM’s own one arrived here with the
//       split and About is a single item again.
//   ⇒ KCMActionComponent at the bottom of this file serves About along with the whole flyout.

// ---- The leaf UI parts, the first ones to come over from the model half ----
//
// ★**The impl IDs kept their `kKCM*` names** -- they were NOT renamed to `kKCMUI*`. Only the
//   numbers moved (to `kKCMUIPrefix + N` in KCMUIID.h), which is why no C++ and no comment had
//   to be rewritten. The Class blocks that name them are in KCMUI.fr, and that file includes
//   KCMUIID.h and KCMScriptingDefs.h -- nothing from the model half.
REGISTER_PMINTERFACE(KCMIconTip, kKCMIconTipImpl)		// tooltip for the panel illustration and for the tool switch button (KCMIconTip.cpp)
REGISTER_PMINTERFACE(KCMNoTip, kKCMNoTipImpl)			// an ITip that always answers empty = no tooltip (KCMNoTip.cpp)
REGISTER_PMINTERFACE(KCMSplitterEH, kKCMSplitterEHImpl)	// makes the splitter bar impossible to grab (KCMSplitterEH.cpp)
REGISTER_PMINTERFACE(KCMPanelView, kKCMPanelViewImpl)	// keeps the panel's minimum size (PalettePanelView subclass; KCMPanelView.cpp)

// ---- The rest of the UI side, which had to move in one go ----
//
// ★**It could not be staged.** The panel, the trees, the dialog and the tool call one another
//   through free functions in a cycle, so any cut leaves unresolved symbols on **both** sides.
//   Anyone slicing this plug-in further has to break that cycle first, not pick a smaller set.
// ★The impl IDs kept their `kKCM*` names here too (see the block above).

// Startup / shutdown, and the receiver of the model’s notifications
REGISTER_PMINTERFACE(KCMUIStartup, kKCMUIStartupImpl)	// startup / shutdown of the UI half (KCMUIStartup.cpp). Its model-side counterpart is KCMPeekStartup
REGISTER_PMINTERFACE(KCMModelChangeObserver, kKCMModelChangeObserverImpl)	// rebuilds the display on a notification from the model (KCMModelChangeObserver.cpp)
REGISTER_PMINTERFACE(KCMDocsClosedObserver, kKCMDocsClosedObserverImpl)	// runs the postponed cleanup once, when a batch close finishes (KCMPeekGesture.cpp)

// The on-press HUD (★screen only -- it is not meant to reach an exported PDF, so this side is right for it)
REGISTER_PMINTERFACE(KCMUIDrawEventSrvc, kKCMUIDrawEventSrvcImpl)		// the UI-only draw service (KCMUIDrawEvent.cpp)
REGISTER_PMINTERFACE(KCMUIDrawEventHandler, kKCMUIDrawEventHandlerImpl)	// its handler

// The panel itself
REGISTER_PMINTERFACE(KCMPanelObserver, kKCMPanelObserverImpl)	// watches the panel's widgets and drives what it shows (KCMPanelObserver.cpp)
REGISTER_PMINTERFACE(KCMPanelRollOver, kKCMPanelRollOverImpl)	// opaque again while the pointer is over the panel (IMouseRollOver; KCMPanelAlpha.cpp)
REGISTER_PMINTERFACE(KCMPanelVisibilityObserver, kKCMPanelVisibilityObserverImpl)	// re-applies the translucency when the panel is shown, hidden or (un)docked (same file)
REGISTER_PMINTERFACE(KCMStorySectionToggleObserver, kKCMStorySectionToggleObserverImpl)	// the open/close button of the "Story Edits" section (KCMStorySectionObserver.cpp)

// The Story Edits list (kTreeViewWidgetBoss / kTreeNodeWidgetBoss derived, so UI by construction)
REGISTER_PMINTERFACE(KCMStoryTreeAdapter, kKCMStoryTreeAdapterImpl)	// what the list holds (ListTreeViewAdapter subclass; KCMStoryTreeAdapter.cpp)
REGISTER_PMINTERFACE(KCMStoryTreeWidgetMgr, kKCMStoryTreeWidgetMgrImpl)	// builds the rows and fills them (CTreeViewWidgetMgr subclass; KCMStoryTreeWidgetMgr.cpp)
REGISTER_PMINTERFACE(KCMStoryRowEH, kKCMStoryRowEHImpl)	// row click = jump, double-click = select the whole story (KCMStoryRowEH.cpp)
REGISTER_PMINTERFACE(KCMStoryTreeEH, kKCMStoryTreeEHImpl)	// up/down move between rows and show the one they land on (KCMStoryTreeEH.cpp)
// ★★★**THE STORY MARKER AND ITS EXPIRY TIMER ARE ON THE MODEL SIDE** (KCMFactoryList.h).
//   Reason: **the UI’s File > Export > PDF runs on a background thread, and a kUIPlugIn is
//   handed no drawing at all there** (measured, no warning), so while they lived here the
//   Story marks could never reach an exported PDF.
//   ⚠**kKCMUIPrefix + 41 / + 42 stay vacant** -- only the values have to be unique
//   ([[id-prefix-256-slot-budget]]).
REGISTER_PMINTERFACE(KCMStoryCellData, kKCMStoryCellDataImpl)// the three pieces a change row's cell paints: context, changed characters, context (KCMStoryCellView.cpp)
REGISTER_PMINTERFACE(KCMStoryCellView, kKCMStoryCellViewImpl)	// ★PERSIST form. The text cell of a change row: changed characters in the theme colour, the context faded (DVControlView subclass; KCMStoryCellView.cpp)
REGISTER_PMINTERFACE(KCMStatusTextData, kKCMStatusTextDataImpl)	// the four pieces the panel's message area paints: heading, context, changed characters, context (KCMStatusTextView.cpp)
REGISTER_PMINTERFACE(KCMStatusTextView, kKCMStatusTextViewImpl)	// ★PERSIST form. The message area: wraps by itself, changed characters in the theme colour, heading and context faded (DVControlView subclass; KCMStatusTextView.cpp)

// The book comparison dialog and its chapter list (kDialogBoss derived, so UI by construction)
REGISTER_PMINTERFACE(KCMBookDialogController, kKCMBookDialogControllerImpl)	// the modeless book comparison dialog (CDialogController subclass; KCMBookDialog.cpp)
REGISTER_PMINTERFACE(KCMBookTreeAdapter, kKCMBookTreeAdapterImpl)	// what the chapter list holds (KCMBookTreeAdapter.cpp)
REGISTER_PMINTERFACE(KCMBookTreeWidgetMgr, kKCMBookTreeWidgetMgrImpl)	// builds the chapter rows and fills them (KCMBookTreeWidgetMgr.cpp)
REGISTER_PMINTERFACE(KCMBookRowEH, kKCMBookRowEHImpl)	// chapter row: double-click opens that chapter, right-click opens the row menu (KCMBookRowEH.cpp)

// Tool, tracker and cursor (kGenericToolBoss derived, so UI by construction)
REGISTER_PMINTERFACE(KCMTool, kKCMToolImpl)	// ★PERSIST form -- the tool's selected state is saved through IID_IPMPERSIST (KCMTool.cpp)
REGISTER_PMINTERFACE(KCMTracker, kKCMTrackerImpl)	// the drag engine (KCMTracker.cpp)
REGISTER_PMINTERFACE(KCMTrackerEH, kKCMTrackerEHImpl)	// its event handler
REGISTER_PMINTERFACE(KCMTrackerRegister, kKCMTrackerRegisterImpl)	// registers the tracker with the application (KCMTrackerRegister.cpp)
REGISTER_PMINTERFACE(KCMCheckCursorProvider, kKCMCursorProviderImpl)	// the tool's cursor (CToolCursorProvider subclass; KCMCursorProvider.cpp)

// Injected into the document window, view syncing, thumbnails
REGISTER_PMINTERFACE(KCMScrollMapView, kKCMScrollMapViewImpl)	// ★PERSIST form. The scroll-map strip (DVControlView subclass; KCMScrollMap.cpp)
REGISTER_PMINTERFACE(KCMLayoutSyncObserver, kKCMLayoutSyncObserverImpl)	// keeps the other layout views in step (KCMViewSync.cpp)
REGISTER_PMINTERFACE(KCMThumbIdleTask, kKCMThumbIdleTaskImpl)	// defers the Pages panel thumbnail rebuild to the next idle (KCMThumbIdleTask.cpp)

// Menus (★an ActionComponent is UI: not one of the SDK’s kModelPlugIn samples has one)
REGISTER_PMINTERFACE(KCMActionComponent, kKCMActionComponentImpl)	// runs the flyout and the context menu items (KCMActionComponent.cpp)
