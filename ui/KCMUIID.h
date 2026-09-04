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


#ifndef __KCMUIID_h__
#define __KCMUIID_h__

#include "SDKDef.h"

// ★★**The IDs both halves have to know by the same value** -- the five facade IIDs, the
//   notification protocol IID, the seven MessageIDs and the model half's prefix -- are in
//   KCMBoundaryID.h. **This file reads the ui/ copy** (its counterpart is
//   source/KCMBoundaryID.h).
//   ⚠**Change one copy without the other and the two halves drift apart in silence.**
//   ★The kKCMPrefix (0x1EA500) defined over there is **the model half's number**, a different
//     thing from kKCMUIPrefix (0x1EA580) below. UI-only IDs are taken from kKCMUIPrefix here.
#include "KCMBoundaryID.h"

// Company: ★the values come straight from KCMBoundaryID.h. The model and the UI are **two .pln
//   of one product**, so the company name, the display name and the version must not disagree
//   ([[one-question-one-place]]).
//   ⚠The template named kSDKDefPlugInCompanyKey -- the SDK samples' own name -- which filed
//     this plug-in in the About menu under the same grouping as Adobe's samples.
//   (kKCMUICompanyKey, an alias of kKCMCompanyKey, was **deleted**: nothing referenced it -- the
//    .fr and the string table name kKCMCompanyKey itself. Template residue, as kKCMUIAuthor was.)
#define kKCMUICompanyValue	kKCMCompanyValue

// Plug-in:
#define kKCMUIPluginName	"KohakuChangeMarkerUI"			// Name of this plug-in.
// ★★★**Adobe's own words (2026-08-13)**:
//
//     "Following Prefix ID has been registered as per your request below : 0x1EA500 - 0x1EA5FF ."
//
// ★★The **upper half** of that registered 256-slot band. The lower half, 0x1EA500, belongs to
//   the model half (KohakuExtendScriptChangeMarker). Splitting one band between a model and a
//   UI plug-in is Adobe's own practice: customdatalink (0xb3300) and customdatalinkui
//   (0xb3380) are exactly this shape (measured: +0..37 and +0..17, both inside 0xb33xx). The
//   xdocbookworkflow pair steps by 16, and four plug-ins share 0x572xx.
//   ⇒ **Uniqueness is a property of the value, not of the plug-in**, so any split that does
//     not overlap is allowed.
// ⚠The old value 0x205792 -- a stand-in borrowed from the Adobe Developer Console plug-in ID --
//   is **discarded**.
#define kKCMUIPrefixNumber	0x1EA580 		// Unique prefix number for this plug-in(registered with Adobe: 0x1EA500-0x1EA5FF).
// ★★**This names the product's version**, not the template's kSDKDefPluginVersionString. The
//   value shows up in the PluginVersion resource (the plug-ins list) and in the .rc
//   FileVersion, so leaving the template value here lists the model half and the UI half as
//   **two plug-ins at different versions**.
//   The value itself belongs to KCMBoundaryID.h; the history, and what goes into the next
//   submission, are in source/KCMID.h.
#define kKCMUIVersion		kKCMVersion					// Version of this plug-in (for the About Box).
// (kKCMUIAuthor was **deleted**: template residue that nothing referenced.
//  ★The model half's kKCMAuthor had gone for the same reason long before. This file was made
//    from the DollyXs template afterwards, so residue the counterpart had already swept out
//    came back with it ＝ **a half rebuilt from a template does not inherit the cleaning the
//    other half has done.**
//  ⚠The official samples do use it, in the About box body ("...version X by <Author>",
//    BscPnl_enUS.fr:58). KCM does not, because About is one line of name and version -- **a
//    deliberate absence, not an oversight**.)

// Plug-in Prefix: (please change kKCMUIPrefixNumber above to modify the prefix.)
#define kKCMUIPrefix		RezLong(kKCMUIPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
// (The template's kKCMUIStringPrefix was **deleted**. Nothing referenced it, and nothing may:
//  **KCM's string keys carry the MODEL's prefix**, for the reason given at the head of the
//  UI-only IDs below. ⚠It was not a spare part but a trap -- spelling a key with it would have
//  changed that key's VALUE, and the table answers only the value the .fr wrote.)

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKCMUIMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKCMUIMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKCMUIPluginID, kKCMUIPrefix + 0)

//========================================================================================
// ★★★Below are the **UI-only IDs**, moved here from source/KCMID.h.
//
//  ★★Two decisions kept the move mechanical, both of them chosen to leave no judgement to make:
//
//   ① **The offsets were preserved** -- `kKCMPrefix + 8` became `kKCMUIPrefix + 8`. Not one
//      number was re-allocated, so there was no allocation to get wrong and a collision is
//      impossible (no number appears twice inside one ID space).
//      ⚠Read the other way round: **these numbers are NOT free in the model half's KCMID.h.**
//        The two bands are separate, so reusing them breaks nothing -- it only makes the
//        correspondence between the halves unreadable.
//
//   ② **The ID names kept their `kKCM*` spelling** -- they were not renamed to `kKCMUI*`.
//      ⇒ The C++ side only had to swap `#include "KCMID.h"` for `"KCMUIID.h"`; no code moved,
//        and every ID name inside the code and inside the comments stayed valid.
//
//  ⚠**String keys are the exception**: their value stays `kKCMStringPrefix`, the model's prefix.
//    **A string key has to be unique across the whole application and cannot be borrowed the way
//    a widget ID can** (guide vol2-12:71), so changing the prefix would change the key values.
//    ⇒ The string table moved across without a single character changing.
//========================================================================================

// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKCMThumbIdleTaskBoss, kKCMUIPrefix + 7)	// IIdleTask: rebuilds the Pages panel thumbnails after a close, deferred to the next idle (slot reused from the retired kKCMToastIdleTaskBoss)
DECLARE_PMID(kClassIDSpace, kKCMPanelWidgetBoss, kKCMUIPrefix + 8)	// the ChangeMarker control panel (palette)
DECLARE_PMID(kClassIDSpace, kKCMActionComponentBoss, kKCMUIPrefix + 9)	// the action component behind the About menu items
DECLARE_PMID(kClassIDSpace, kKCMIconWidgetBoss, kKCMUIPrefix + 11)	// kRollOverIconButtonBoss plus IID_ITIP (the tooltip on the panel illustration)
DECLARE_PMID(kClassIDSpace, kKCMScrollMapWidgetBoss, kKCMUIPrefix + 12)	// kGenericPanelWidgetBoss + an IControlView of our own: the page map strip beside the vertical scrollbar (slot reused from the retired kKCMLayoutSyncObserverBoss)
DECLARE_PMID(kClassIDSpace, kKCMToolBoss, kKCMUIPrefix + 13)	// kGenericToolBoss subclass: the peek tool in the toolbox (KCMTool.cpp)
DECLARE_PMID(kClassIDSpace, kKCMTrackerBoss, kKCMUIPrefix + 14)	// the tool's capturing tracker (IID_ITRACKER + IID_IEVENTHANDLER). It reveals only while the left button is held. KCMTracker.cpp
DECLARE_PMID(kClassIDSpace, kKCMTrackerRegisterBoss, kKCMUIPrefix + 15)	// registers the tracker (kLayoutWidgetBoss x tool -> tracker). KCMTrackerRegister.cpp
DECLARE_PMID(kClassIDSpace, kKCMStorySectionToggleBoss, kKCMUIPrefix + 16)	// kRollOverIconButtonBoss + IID_IOBSERVER: the open/close triangle of the "Story Edits" section at the bottom of the panel. Its artwork is borrowed from InDesign (kTreeBranchCollapsed/Expanded)
DECLARE_PMID(kClassIDSpace, kKCMStorySectionPanelBoss, kKCMUIPrefix + 17)	// kGenericPanelWidgetBoss + IID_IKCMSAVEDSECTIONHEIGHT (kPersistIntDataImpl): the lower pane itself. It remembers the height the section had when it was last closed (modelled on the product's linksui kLinkInfoPanelWidgetBoss)
DECLARE_PMID(kClassIDSpace, kKCMStoryTreeWidgetBoss, kKCMUIPrefix + 18)	// kTreeViewWidgetBoss subclass: the Story Edits list (flat, one level). It carries only the adapter and the widget manager - kTreeViewWidgetBoss already provides the controller
DECLARE_PMID(kClassIDSpace, kKCMStoryRowWidgetBoss, kKCMUIPrefix + 19)	// kTreeNodeWidgetBoss subclass: one row of the list. It carries only IID_IEVENTHANDLER (kKCMStoryRowEHImpl) = single click jumps, double click selects the whole story
DECLARE_PMID(kClassIDSpace, kKCMStoryRowCellBoss, kKCMUIPrefix + 20)	// kInfoStaticTextWidgetBoss + IID_ITIP (kKCMNoTipImpl): a cell of a row. ★The point is to TAKE the tooltip AWAY: a plain static text pops its full string up whenever it is ellipsized (measured - kStaticTextWidgetBoss carries IID_ITIP = kTextWidgetTipImpl), which is in the way on a list row, so an implementation that answers empty is put over it (user's call)
// The book comparison dialog. ★It is MODELESS = the documents stay reachable while it is open,
// which is what leaves room for "click a row to open that chapter" to be added later (a modal
// dialog closes that road). It is the stock kDialogBoss with an IDialogController of ours on
// it -- the same shape as KESCL's Jump Offset dialog.
DECLARE_PMID(kClassIDSpace, kKCMBookDialogBoss, kKCMUIPrefix + 21)
// The chapter list inside that dialog. ★**The same three pieces as the Story Edits list**: the
// tree itself (adapter + widget manager), the row, and the row's cell. The only difference is
// where it lives -- a dialog rather than a palette, so the theme is kIDDialogTheme and the font
// is the dialog one.
DECLARE_PMID(kClassIDSpace, kKCMBookTreeWidgetBoss, kKCMUIPrefix + 22)	// kTreeViewWidgetBoss subclass: the chapter list (flat, one level). It carries only the adapter and the widget manager
DECLARE_PMID(kClassIDSpace, kKCMBookRowWidgetBoss, kKCMUIPrefix + 23)	// kTreeNodeWidgetBoss subclass: one row of that list. ★Nothing is added to it yet - the slot is taken in advance for the IID_IEVENTHANDLER that a row click ("open that chapter") will need, the same order the Story Edits row boss went through
DECLARE_PMID(kClassIDSpace, kKCMBookRowCellBoss, kKCMUIPrefix + 24)	// kInfoStaticTextWidgetBoss + IID_ITIP (kKCMNoTipImpl): a cell of that row. A plain static text pops its full string up when ellipsized, which is silenced on list rows (the same call as for Story Edits)
// The bar that divides the panel. ★Its contents are the stock kSplitterPanelWidgetBoss's, with
// IID_IEVENTHANDLER alone replaced by an implementation that does nothing = **the bar cannot be
// dragged** (user's call). There is **no way to REMOVE an interface from an inherited boss**, so
// removing behaviour means "override it with an implementation that answers differently" ----
// the same shape as kKCMStoryRowCellBoss (+20) silencing a tooltip.
DECLARE_PMID(kClassIDSpace, kKCMSplitterPanelBoss, kKCMUIPrefix + 25)	// kSplitterPanelWidgetBoss + IID_IEVENTHANDLER (kKCMSplitterEHImpl): a SplitterPanelWidget whose divider cannot be grabbed
DECLARE_PMID(kClassIDSpace, kKCMUIStartupBoss, kKCMUIPrefix + 27)	// IStartupShutdown: **the UI half's** startup / shutdown. It restores the panel settings, subscribes and unsubscribes the translucency, returns the HUD's font, watches for a batch close and releases the deferred thumbnail idle task. ★Its counterpart is kKCMPeekStartupBoss on the model side (KCMID.h)
DECLARE_PMID(kClassIDSpace, kKCMUIDrawEventServiceBoss, kKCMUIPrefix + 26)	// IK2ServiceProvider + IDrwEvtHandler: the **UI-only** draw service. It carries the on-press HUD and nothing else. ★The model side has a different one, kKCMDrawEventServiceBoss (the comparison marks, KCM.fr): those have to reach print and PDF export, while this one is screen-only. kDrawEventService expects several providers (InDesign itself registers over 20)
DECLARE_PMID(kClassIDSpace, kKCMBookPathTextWidgetBoss, kKCMUIPrefix + 28)	// kStaticTextWidgetBoss + IID_IEVEINFO (kFixedSizeEVEInfoImpl): the Target:/Source: lines of the book comparison dialog. ★★EVE treats **the width in the .fr as a MINIMUM** ("We treat the width in the .fr file as a minimum width", guide Using EVE), so a full path makes the widget grow and the parent with it (measured: 593px). ⚠kEVEAlignFill does not stop it - Fill means "take the parent's width", and that parent is pushed wider by its child. ⇒ **EVE asks IID_IEVEINFO for a widget's size**, so naming an implementation that answers "the size the resource states" fixes the width and hands the shortening back to the widget's own kEllipsizeBeginning = the same behaviour as the panel's Target:/Source:. Modelled on KBS.fr's glyph box (no other use in the whole SDK, but verified in the running application)
// The text cell of a **change row** in Story Edits. ★The point is to draw only the changed
// characters in the normal colour and fade the context around them (user's request: "follow
// KBS"). A plain static text is **one colour per line**, so telling them apart means a
// self-drawn cell instead ---- the same shape as KBS's kKBSColorTextViewBoss.
// ★It is built on kGenericPanelWidgetBoss because that provides somewhere to draw and neither
//   text nor colour of its own.
//   ⇒ It has no tooltip either, so the kKCMNoTipImpl that the row cell at +20 needs is not
//     needed here ＝ **the silence comes for free**.
DECLARE_PMID(kClassIDSpace, kKCMStoryChangeCellBoss, kKCMUIPrefix + 29)	// kGenericPanelWidgetBoss + IID_ICONTROLVIEW (kKCMStoryCellViewImpl) + IID_IKCMSTORYCELLDATA (kKCMStoryCellDataImpl): the text cell of a change row. It takes three pieces (context, the changed characters, context) and draws the middle one in the theme text colour with the outer two faded toward the background (KCMStoryCellView.cpp)
// The mark that briefly lights up wherever a change row jumps to. ★★It is a **GLOBAL TEXT
// ADORNMENT** = the official mechanism for drawing **over the characters themselves**, and it
// **changes not one byte of the document** (nothing is saved and nothing lands on the undo
// stack). InDesign draws its own kinsoku violations, missing glyphs and spelling squiggles the
// same way ([[global-text-adornment]]).
// ★**Draw is handed the waxRun and its waxGlyphs**, so "does this run overlap the marked range"
//   is answered from TextOrigin and GetCharCount, and the coordinates of a part of a range come
//   from MapCharsToGlyphs ＝ **no coordinate conversion of our own**.
//   ⇒ Unlike the Draw Event route KBS uses (building a pasteboard-space rectangle by hand),
//     this rides vertical text and rotation as it is.
// ★Two pieces only: the provider the API already ships (`kGlobalTextAdornmentServiceImpl`) and
//   our own implementation. Modelled on the product's spellpanel and on KT.
// ★★★+ 30 is vacant ＝ **the Story mark boss moved to the model side** (`KCMID.h`, kKCMPrefix
//   + 32). Reason ＝ **the UI's File > Export > PDF runs on a background thread and a
//   kUIPlugIn is handed no drawing there at all** (measured, no warning), so while it lived on
//   this side there was no way to put it into an exported PDF.
//   ⚠**The numbering is not closed up** ＝ [[id-prefix-256-slot-budget]].
// The timer that withdraws that marker after about a second. ★**An IIdleTask, the same call KBS
//   made**: an `ICallbackTimer` callback is a raw function pointer that is not reference
//   counted, and the header itself says "Danger!". An IdleTask is an interface on a boss, so
//   shutdown can Release it in the ordinary way.
//   ⚠An exception to [[avoid-timers-and-idle-tasks]] ＝ **nothing else here has to disappear by
//     the wall clock.**
// ★+ 31 is vacant too ＝ that expiry timer went to the model side in the same move
//   (`KCMID.h`, kKCMPrefix + 33).
// The panel's message area. ★**It replaces the stock StaticMultiLineTextWidget**, which draws
// one string in one colour and therefore cannot say **which characters differ** inside "the
// other side" that a click on a change row brings up. ⇒ Same construction as the change row
// cell above (kGenericPanelWidgetBoss + a view of our own).
// ⚠**The difference is the number of lines** ＝ that cell is one line and can leave the
//   shortening to `PMEllipsizeString`, while this box wraps to fill its height ＝ **wrapping is
//   the one thing lost with the stock widget, so it is written out** (KCMStatusTextView.cpp).
// ★The WidgetID and the Frame are unchanged (`kKCMStatusTextWidgetID`), so nothing around it
//   moves.
DECLARE_PMID(kClassIDSpace, kKCMStatusTextWidgetBoss, kKCMUIPrefix + 32)	// kGenericPanelWidgetBoss + IID_ICONTROLVIEW (kKCMStatusTextViewImpl) + IID_IKCMSTATUSTEXTDATA (kKCMStatusTextDataImpl): the panel's message area. It takes a heading and three pieces (context, the changed characters, context), wraps them and draws them in at most two colours (KCMStatusTextView.cpp)
// The cat-paw stamp tool (2026-09-04). ★It is a **SUBTOOL of the KCM tool**: its ToolDef names
// kKCMToolBoss as the parent tool, which puts it inside that tool's press-and-hold flyout and
// **costs no toolbox slot** (ToolRecord.h:49,54,59-61,65; the SDK's only worked example is
// wavetool/WavTl.fr:264-276). The boss itself is the same shape as kKCMToolBoss above --
// kGenericToolBoss supplies the toolbox button view, IID_IPMPERSIST persists the selected state.
DECLARE_PMID(kClassIDSpace, kKCMPawToolBoss, kKCMUIPrefix + 33)
// That tool's tracker. ★A press places one paw, or lifts the one it landed on, and there is
// nothing to track afterwards, so BeginTracking answers kFalse ＝ the single-shot shape of
// sdksamples/snapshot. It derives from CTracker directly, so it carries **no sprite** -- the
// reason is written out at kKCMTrackerBoss in KCMUI.fr.
DECLARE_PMID(kClassIDSpace, kKCMPawTrackerBoss, kKCMUIPrefix + 34)
// ★The panel's tool button (2026-09-04). kKCMIconWidgetBoss with an event handler of its own,
//   because the button carries TWO tools and the second is reached by HOLDING it -- the toolbox's
//   press-and-hold, brought to the panel at the user's request.
// ⚠★★IT REPLACES kAssociatedActionEventHandlerImpl, which is what kRollOverIconButtonBoss puts on
//   IID_IEVENTHANDLER (measured in the boss dump). That implementation is what turns a click into
//   the kTrueStateMessage the panel used to listen for, so ONCE IT IS GONE THE PRESS MUST BE
//   HANDLED HERE IN FULL -- and it is (KCMToolButtonEH.cpp). ★That is not a loss but the point:
//   the state messages cannot tell a hold from a click, and worse, a button already showing
//   selected raises no kTrueStateMessage at all -- measured 2026-09-04, which is exactly why
//   pressing the button a second time did nothing.
DECLARE_PMID(kClassIDSpace, kKCMToolButtonBoss, kKCMUIPrefix + 35)
// InterfaceIDs:
// ⚠★What is here are **the IIDs that appear only on UI-side bosses**. The ones that cross the
//   boundary (the five facades plus the notification protocol) are in **KCMBoundaryID.h**,
//   where they are named from the model's `kKCMPrefix`
//   ＝ **let those values disagree and the build still succeeds while nothing happens.**
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMLAYOUTSYNCOBSERVER, kKCMUIPrefix + 0)	// the observerIID used to attach the layout view sync observer
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMDOCSCLOSEDOBSERVER, kKCMUIPrefix + 1)	// the observerIID for "a batch close has finished" (kPendingDocumentsClosedMsg)
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMPANELVISIBILITYOBSERVER, kKCMUIPrefix + 2)	// the observerIID for panel visibility changes (kPaletteVisibilityChangedMessage). It is what makes the translucency follow docking and re-opening
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMSTORYCELLDATA, kKCMUIPrefix + 4)	// IKCMStoryCellData: the container for the three pieces a change row's cell paints (context / changed characters / context). ★Row widgets are recycled, so every fill rewrites all three (ui/IKCMStoryCellData.h)
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMSTATUSTEXTDATA, kKCMUIPrefix + 5)	// IKCMStatusTextData: the container for the four pieces the panel's message area paints (heading / context / changed characters / context). ★An ordinary message fills the middle one only, and is drawn in one colour (ui/IKCMStatusTextData.h)
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMSAVEDSECTIONHEIGHT, kKCMUIPrefix + 3)	// used as an IIntData: the height in px the Story Edits section had the moment it was closed, which is the height it opens at next time. The implementation is the SDK's own kPersistIntDataImpl (modelled on linksui's IID_ISAVEDINFOPANESIZE)
// ImplementationIDs:
// ⚠Every implementation listed here has to be registered **one for one in
//   ui/KCMUIFactoryList.h** -- a missing registration fails in complete silence.
DECLARE_PMID(kImplementationIDSpace, kKCMThumbIdleTaskImpl, kKCMUIPrefix + 5)	// IIdleTask (defers the Pages panel thumbnail rebuild after a close)
DECLARE_PMID(kImplementationIDSpace, kKCMPanelObserverImpl, kKCMUIPrefix + 6)	// IObserver (the panel's widget observer)
DECLARE_PMID(kImplementationIDSpace, kKCMActionComponentImpl, kKCMUIPrefix + 7)	// IActionComponent (About)
DECLARE_PMID(kImplementationIDSpace, kKCMIconTipImpl, kKCMUIPrefix + 10)	// ITip (shows the distribution URL on the panel illustration)
DECLARE_PMID(kImplementationIDSpace, kKCMLayoutSyncObserverImpl, kKCMUIPrefix + 11)	// IObserver (layout view syncing)
DECLARE_PMID(kImplementationIDSpace, kKCMScrollMapViewImpl, kKCMUIPrefix + 12)	// IControlView (draws the scrollbar map strip; KCMScrollMap.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMToolImpl, kKCMUIPrefix + 13)	// ITool (KCMTool.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMTrackerImpl, kKCMUIPrefix + 14)	// ITracker (CTracker subclass; KCMTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMTrackerRegisterImpl, kKCMUIPrefix + 15)	// ITrackerRegister (KCMTrackerRegister.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMTrackerEHImpl, kKCMUIPrefix + 16)	// IEventHandler (CTrackerEventHandler subclass; forwards the button release during capture to EndTracking. KCMTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMCursorProviderImpl, kKCMUIPrefix + 17)	// ICursorProvider (CToolCursorProvider subclass; the check-mark cursor for as long as the tool is active. KCMCursorProvider.cpp)
// (+18 = kKCMSpriteImpl went out together with the **old sprite-based** on-press HUD.
//  ★**That number is not reused.**
//  ⚠**The on-press HUD itself is alive** -- it was rebuilt the next day on the Draw Event
//  route (KCMTrackerHud.cpp).
//  ★★The tracker boss's **IID_ISPRITE / IID_IPATHGEOMETRY are gone as well**. The old claim
//   that this "matched wavetool's boss shape" counted the wrong set: a sprite is needed by
//   **CPathCreationTracker / CLayoutTracker** subclasses, and KCM derives from CTracker
//   directly, so **not having one is the shape that matches**. The full reason is in the
//   kKCMTrackerBoss Class comment in KCMUI.fr.)
DECLARE_PMID(kImplementationIDSpace, kKCMDocsClosedObserverImpl, kKCMUIPrefix + 19)	// IObserver (runs the postponed cleanup once, when a batch close finishes. ui/KCMPeekGesture.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMPanelVisibilityObserverImpl, kKCMUIPrefix + 20)	// IObserver (re-applies the translucency when the panel visibility changes. KCMPanelAlpha.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMPanelRollOverImpl, kKCMUIPrefix + 21)	// IMouseRollOver (drops the translucency while the pointer is over the panel. KCMPanelAlpha.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMStorySectionToggleObserverImpl, kKCMUIPrefix + 22)	// IObserver (opens and closes the Story Edits section on a press of the triangle. KCMStorySectionObserver.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMStoryTreeAdapterImpl, kKCMUIPrefix + 23)	// ITreeViewHierarchyAdapter (ListTreeViewAdapter subclass. KCMStoryTreeAdapter.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMStoryTreeWidgetMgrImpl, kKCMUIPrefix + 24)	// ITreeViewWidgetMgr (CTreeViewWidgetMgr subclass. KCMStoryTreeWidgetMgr.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMNoTipImpl, kKCMUIPrefix + 25)	// ITip (always answers empty = no tooltip. KCMNoTip.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMPanelViewImpl, kKCMUIPrefix + 26)	// IControlView (PalettePanelView subclass; ConstrainDimensions keeps the panel's minimum size. KCMPanelView.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMStoryRowEHImpl, kKCMUIPrefix + 27)	// IEventHandler (TreeNodeEventHandler subclass. A Story Edits row: single click jumps, double click selects the whole story. KCMStoryRowEH.cpp)
// (kKCMUIPrefix + 28 was briefly kKCMStoryRowViewImpl, an IControlView that removed the rule
//  between rows, and was withdrawn the same day -- the rules stay, by the user's call, so the
//  implementation went with it (the account is in the row boss comment in KCMUI.fr).
//  Unlike an ActionID, an implementation number is referenced by nothing that is stored
//  outside the plug-in, so it was reused below.)
DECLARE_PMID(kImplementationIDSpace, kKCMStoryTreeEHImpl, kKCMUIPrefix + 28)	// IEventHandler (TreeViewEventHandler subclass). ★Key handling for the list **itself**: up/down move between rows and jump to the row they land on (KCMStoryTreeEH.cpp). A different thing from the row-side kKCMStoryRowEHImpl, which handles clicks
DECLARE_PMID(kImplementationIDSpace, kKCMBookDialogControllerImpl, kKCMUIPrefix + 29)	// IDialogController (CDialogController subclass). The modeless book comparison dialog: on open it fills in the names of the two books being compared (KCMBookDialog.cpp)
// (Retired: kKCMBookDialogObserverImpl (kKCMUIPrefix + 30) was the IObserver behind the Compare
//  button of the book comparison dialog. ★The button itself was removed -- the flow became
//  "confirmation alert, then OK compares" -- so the implementation file went with it and
//  IID_IOBSERVER went back to kDialogBoss's stock kCDialogObserverImpl. The slot stays
//  reserved and is not reused.)
DECLARE_PMID(kImplementationIDSpace, kKCMBookTreeAdapterImpl, kKCMUIPrefix + 31)	// ITreeViewHierarchyAdapter (ListTreeViewAdapter subclass. KCMBookTreeAdapter.cpp). The chapter list of the book comparison dialog: it answers how many rows there are
DECLARE_PMID(kImplementationIDSpace, kKCMBookTreeWidgetMgrImpl, kKCMUIPrefix + 32)	// ITreeViewWidgetMgr (CTreeViewWidgetMgr subclass. KCMBookTreeWidgetMgr.cpp). Builds and fills the chapter rows (chapter name / state)
DECLARE_PMID(kImplementationIDSpace, kKCMBookRowEHImpl, kKCMUIPrefix + 33)	// IEventHandler (TreeNodeEventHandler subclass. KCMBookRowEH.cpp). A chapter row: **double click opens that chapter**, **right click opens the row menu** (Start Change Marker). ★A single click does nothing, unlike a Story Edits row -- that one only moves around inside an open document, while this one would open documents. The work itself is in KCMBookOpen.cpp
DECLARE_PMID(kImplementationIDSpace, kKCMUIStartupImpl, kKCMUIPrefix + 38)	// IStartupShutdown (startup / shutdown of the UI half. KCMUIStartup.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMModelChangeObserverImpl, kKCMUIPrefix + 37)	// IObserver (**the UI side** of "the model notified, rebuild the display". KCMModelChangeObserver.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMUIDrawEventSrvcImpl, kKCMUIPrefix + 35)	// CServiceProvider (kDrawEventService; the UI-only draw service. KCMUIDrawEvent.cpp). ★GetThreadingPolicy is not written by hand -- CServiceProvider derives the default from the plug-in type
DECLARE_PMID(kImplementationIDSpace, kKCMUIDrawEventHandlerImpl, kKCMUIPrefix + 36)	// IDrwEvtHandler (the on-press HUD and nothing else; screen-only, it need not reach a PDF export. KCMUIDrawEvent.cpp)
// ★★+ 41 and + 42 are vacant ＝ the Story mark adornment implementation and its expiry timer
//   implementation moved to the model side (`KCMID.h`, kKCMPrefix + **51** / + **52**). The
//   reason is the one given at + 30 above.
//   ⚠These two numbers were written here as + 50 / + 51 and had to be corrected: + 50 is
//     kKCMStoryMarkFacadeImpl, taken in the same move, and these are one further along.
//     **A move always involves copying numbers across, so open what you copied them into and
//     count.**
DECLARE_PMID(kImplementationIDSpace, kKCMStoryCellViewImpl, kKCMUIPrefix + 39)	// IControlView (DVControlView subclass). The text cell of a **change row** in Story Edits: the changed characters in the theme text colour, the context on either side faded toward the background (KCMStoryCellView.cpp). ★Modelled on KBS's KBSColorTextView, which highlights the matched part of a search hit
DECLARE_PMID(kImplementationIDSpace, kKCMStoryCellDataImpl, kKCMUIPrefix + 40)	// IKCMStoryCellData (a non-persistent container for the three pieces; it lives on the same boss as the cell above. KCMStoryCellView.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMStatusTextViewImpl, kKCMUIPrefix + 43)	// IControlView (DVControlView subclass). The panel's message area: it wraps to fill the box, draws the changed characters in the theme text colour and fades the heading and the context (KCMStatusTextView.cpp). ★**The PERSIST form** -- this widget is built from the panel's .fr, so it has to be persistent like the IID_ICONTROLVIEW of the kGenericPanelWidgetBoss it is built on
DECLARE_PMID(kImplementationIDSpace, kKCMStatusTextDataImpl, kKCMUIPrefix + 44)	// IKCMStatusTextData (a non-persistent container for the four pieces; it lives on the same boss as the area above. KCMStatusTextView.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMSplitterEHImpl, kKCMUIPrefix + 34)	// IEventHandler (CEventHandler subclass ＝ the base whose every method just answers kFalse). It makes the panel's divider take no presses, so it cannot be dragged (KCMSplitterEH.cpp)
// The cat-paw stamp tool's three implementations (2026-09-04). ★The tool and its tracker are
// the same pair as +13 / +14 / +16 above; only what the tracker does with the press differs.
DECLARE_PMID(kImplementationIDSpace, kKCMPawToolImpl, kKCMUIPrefix + 45)	// ITool (the cat-paw stamp tool. KCMPawTool.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMPawTrackerImpl, kKCMUIPrefix + 46)	// ITracker (CTracker subclass; one left press places a paw, or lifts the one under it. KCMPawTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMPawTrackerEHImpl, kKCMUIPrefix + 47)	// IEventHandler (CTrackerEventHandler subclass, the companion of the tracker above -- a bare subclass, as kKCMTrackerEHImpl is. KCMPawTracker.cpp)
DECLARE_PMID(kImplementationIDSpace, kKCMToolButtonEHImpl, kKCMUIPrefix + 49)	// IEventHandler (CEventHandler subclass; the panel tool button's press-and-hold. KCMToolButtonEH.cpp). ⚠It REPLACES the stock kAssociatedActionEventHandlerImpl, so it owns the whole press -- see kKCMToolButtonBoss
DECLARE_PMID(kImplementationIDSpace, kKCMPawCursorProviderImpl, kKCMUIPrefix + 48)	// ICursorProvider (CToolCursorProvider subclass; the pink paw shown while the stamp tool is active. KCMPawCursorProvider.cpp). ★Simpler than the KCM tool's, which has two states (black while armed, outlined while stopped): a paw can be placed at any time, so it has nothing to say about the comparison's state
// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKCMAboutActionID, kKCMUIPrefix + 0)
DECLARE_PMID(kActionIDSpace, kKCMPanelWidgetActionID, kKCMUIPrefix + 1)	// show / hide the panel (Window menu)
DECLARE_PMID(kActionIDSpace, kKCMPopupAboutThisActionID, kKCMUIPrefix + 2)	// "About this plug-in" on the panel flyout
DECLARE_PMID(kActionIDSpace, kKCMPopupAboutScriptActionID, kKCMUIPrefix + 3)	// (retired, reserved) the old "About Scripting" flyout item. The slot stays reserved
DECLARE_PMID(kActionIDSpace, kKCMPopupUsageActionID, kKCMUIPrefix + 4)	// "How to Use" on the panel flyout
// +5 and +6 were free (they were kKCMPopupTestSplitActionID, the Split Test menu, and
//   kKCMPopupSplitTargetActionID, "Split Target on Start" -- how that one worked is kept in
//   docs/ai-notes/kescm-split-target-mechanism.md). They now carry the two "Set as" items.
//   ⚠**Reusing a retired slot is only safe because neither was shortcut-assignable**
//   (kSDKDefInvisibleInKBSCEditorFlag, as these two are): a shortcut the reader had saved
//   against the old ActionID would otherwise now fire the new item.
DECLARE_PMID(kActionIDSpace, kKCMPopupSetTargetActionID, kKCMUIPrefix + 5)	// ★"Set as Target" on the panel flyout (a plain command): the active document becomes the comparison's Target. Live only while NOT comparing, and only with an active document. The choice survives a Stop and is dropped when that document closes. kCustomEnabling. KCMActionComponent.cpp -> IKCMCompareFacade::SetChosenTargetToActive
DECLARE_PMID(kActionIDSpace, kKCMPopupSetSourceActionID, kKCMUIPrefix + 6)	// ★"Set as Source" on the panel flyout (a plain command): the active document becomes the Source (the older version). Same enabling and the same lifetime as +5. ★**Choosing the same document for both is allowed** -- the panel shows it on both lines -- and the Start is what refuses it, with a message (KCMToggleStartStop)
DECLARE_PMID(kActionIDSpace, kKCMPopupHideUnchangedActionID, kKCMUIPrefix + 7)	// "Hide Unchanged Spreads" check toggle on the panel flyout (ON = hide the spreads with no change)
DECLARE_PMID(kActionIDSpace, kKCMPopupShowOldNumsActionID, kKCMUIPrefix + 8)	// "Show Original Page Numbers" check toggle on the panel flyout (the badge with the number a page had before hiding, while marks show or printing is on)
DECLARE_PMID(kActionIDSpace, kKCMPopupSyncViewsActionID, kKCMUIPrefix + 9)	// "Sync Layout Views" check toggle on the panel flyout (keeps the other documents' views at the same position and zoom)
DECLARE_PMID(kActionIDSpace, kKCMPopupShowSrcMarksActionID, kKCMUIPrefix + 10)	// "Always Show Marks on Source" check toggle on the panel flyout (marks stay visible on the Source document, in OPP too, and print). ★Default OFF, and **Start does not touch it** -- the setting is saved with the panel settings and restored at startup, so a Start that overwrote it would erase the saved choice
DECLARE_PMID(kActionIDSpace, kKCMPageMapToggleActionID, kKCMUIPrefix + 11)	// "Register as Added/Removed Pages" toggle on the Pages panel page context menu (RtMenuPagesPanel): registers the selected pages as having no counterpart, or clears that. The check mark and the dynamic label come from kCustomEnabling. KCMPageMap.cpp
DECLARE_PMID(kActionIDSpace, kKCMPopupIgnorePageNumActionID, kKCMUIPrefix + 12)	// "Ignore Page Number Marker" check toggle on the panel flyout (ON = frames holding an automatic page number marker are left out of the comparison. ★Default OFF = the initial value of sIgnorePageNumberMarker. KCMPageNumberMarker.cpp)
DECLARE_PMID(kActionIDSpace, kKCMPopupStartStopActionID, kKCMUIPrefix + 13)	// "Start / Stop" at the head of the panel flyout (begins and clears the comparison; it used to be a toggle button). The name changes between Start and Stop with the armed state, through kCustomEnabling + SetNthActionName (KCMToggleStartStop in KCMPanelObserver.cpp)
DECLARE_PMID(kActionIDSpace, kKCMPopupPrintMarksActionID, kKCMUIPrefix + 14)	// "Print comparison marks" check toggle on the panel flyout (it used to be a checkbox on the panel). ON = the marks print, and show on screen at all times. KCMTogglePrintMarks in KCMPanelObserver.cpp
DECLARE_PMID(kActionIDSpace, kKCMPopupOpacity25ActionID, kKCMUIPrefix + 15)	// "Marks opacity 25%" on the panel flyout (it used to be an opacity radio on the panel). Mutually exclusive with 75%, the selected one carrying the check. KCMSetMarkOpacity25 in KCMPanelObserver.cpp
DECLARE_PMID(kActionIDSpace, kKCMPopupOpacity75ActionID, kKCMUIPrefix + 16)	// "Marks opacity 75%" on the panel flyout (mutually exclusive with 25%)
DECLARE_PMID(kActionIDSpace, kKCMPopupSep1ActionID, kKCMUIPrefix + 17)	// flyout: the separator below Start (a MenuDef path ending in ":-". No ActionDef and no DoAction are needed - only a unique ID)
DECLARE_PMID(kActionIDSpace, kKCMPopupSep2ActionID, kKCMUIPrefix + 18)	// flyout: the separator above How to Use
// kKCMPopupHoldToHideMarksActionID (kKCMUIPrefix + 19) was the "Hold to Hide Marks" toggle, and
//   it was removed (user's decision).
//   ★The reason ＝ **"keep the marks visible" had become an exact duplicate of "Always Show
//     Marks on Target" (+45)** -- the drawing side had turned into `sAlwaysShowMarks ||
//     sTgtMarksOn`, which is the proof.
//   What was peculiar to it, "hide them while the button is held", was folded into **the
//   standard behaviour whenever either "Show Marks on ..." toggle is ON** ＝ one rule: while
//   the button is held, the state is inverted.
//   ⇒ No feature was lost. ⚠**Slot +19 is not reused.**
// kKCMPopupPanelShortcutActionID (kKCMUIPrefix + 20) went with the middle-button gestures. The
// slot was reused:
DECLARE_PMID(kActionIDSpace, kKCMPopupAlignViewsActionID, kKCMUIPrefix + 20)	// "Align Other Views to Active" on the panel flyout (a plain command). It sets the other documents' layout views to the active (frontmost) view's position and zoom, once. While Started, the page Add/Remove correction is applied. Shortcut-assignable (kKCMPanelMenuActionArea + VisibleInKBSC). The work is KCMAlignOtherViewsToActiveNow in ui/KCMViewSync.cpp
DECLARE_PMID(kActionIDSpace, kKCMPopupScrollMapActionID, kKCMUIPrefix + 21)	// "Show Scrollbar Map" check toggle on the panel flyout (ON = a strip beside each document window's vertical scrollbar maps where the changes are. Default ON; the state is sScrollMapOn in KCMScrollMap.cpp)
DECLARE_PMID(kActionIDSpace, kKCMPopupSavePanelStateActionID, kKCMUIPrefix + 22)	// "Save Panel Settings" on the panel flyout (a plain command, not a check). It writes the current settings toggles to a private JSON file and shows the saved path. They are read back at startup (KCMUIStartup::Startup). KCMPanelState.cpp
DECLARE_PMID(kActionIDSpace, kKCMPopupSep3ActionID, kKCMUIPrefix + 23)	// flyout: the separator below Refresh Overset (a MenuDef path ending in ":-"; no ActionDef needed). Its position is kKCMSep3MenuItemPosition below
DECLARE_PMID(kActionIDSpace, kKCMPageCheckToggleActionID, kKCMUIPrefix + 24)	// "Check" toggle on the Pages panel page context menu (RtMenuPagesPanel): puts a check mark on the selected pages, or takes it off. Only while Started; cleared on Stop. The check mark and the enabling come from kCustomEnabling. ★**Which pages can be checked depends on the mode**: in Pixel only pages that carry a mark, in Story any page. The answer lives in one place, the model's KCMCollectCheckablePageUIDs. KCMPageCheck.cpp, and the check itself is drawn by the isThumb branch of KCMDrawEventHandler
DECLARE_PMID(kActionIDSpace, kKCMPopupSaveChecksActionID, kKCMUIPrefix + 25)	// "Save Check & Register" on the panel flyout (a plain command). It merges the current checks and Added/Removed registrations of Target and Source into a private JSON file (KCM\KCMPageChecks.json, v2) and shows the saved path. KCMPageCheck.cpp
DECLARE_PMID(kActionIDSpace, kKCMPopupLoadChecksActionID, kKCMUIPrefix + 26)	// "Load Check & Register" on the panel flyout (a plain command). Enabled only while Started: it applies the registrations from that file to both documents, recompares, then restores the checks (still only where a mark is). KCMPageCheck.cpp
// kKCMPopupPagesPanelShortcutActionID (kKCMUIPrefix + 27) went with the middle-button gestures,
//   together with its "Invoke Pages Panel Shortcut" toggle. The slot stays reserved.
DECLARE_PMID(kActionIDSpace, kKCMPageMapSepActionID, kKCMUIPrefix + 28)	// Pages panel page context menu (RtMenuPagesPanel): the separator above the KCM items (Register / Check). A MenuDef path ending in ":-" - no ActionDef and no DoAction, only a unique ID. It sets the KCM items apart from InDesign's own
DECLARE_PMID(kActionIDSpace, kKCMToolActionID, kKCMUIPrefix + 29)	// the ActionID for the toolbox tool-select shortcut (referenced by the ToolDef; no ActionDef needed - the toolbox framework provides the action)
DECLARE_PMID(kActionIDSpace, kKCMPageRefreshCompareActionID, kKCMUIPrefix + 30)	// "Refresh Page Comparison" on the Pages panel page context menu (RtMenuPagesPanel): recompares the selected pages and updates their marks and thumbnails. Enabled only while Started, ★**in the Pixel mode**, and with the Target document in front - otherwise the item disappears (kCustomEnabling). ★It is absent in the Story mode because that mode rasterises no page, so pressing it would change nothing on screen; a Story refresh is on the row context menu instead (kKCMStoryRowRefreshActionID). KCMRefreshComparisonForSelectedPages in KCMPeek.cpp
DECLARE_PMID(kActionIDSpace, kKCMPopupFindOversetActionID, kKCMUIPrefix + 31)	// "Find Overset" check toggle on the panel flyout (ON = scan the active document and put a large cross on every page with overset text. Independent of the comparison. kCustomEnabling. KCMActionComponent.cpp / KCMOversetScan.cpp)
DECLARE_PMID(kActionIDSpace, kKCMPopupRefreshOversetActionID, kKCMUIPrefix + 32)	// "Refresh Overset" on the panel flyout (a plain command). Enabled only while Find Overset is ON (greyed otherwise) = rescan the active document and redo the crosses. kCustomEnabling
DECLARE_PMID(kActionIDSpace, kKCMPopupOversetSepActionID, kKCMUIPrefix + 33)	// flyout: the separator above the Find Overset group (a MenuDef path ending in ":-"; no ActionDef and no DoAction, only a unique ID)
DECLARE_PMID(kActionIDSpace, kKCMPopupExportChangedPagesActionID, kKCMUIPrefix + 34)	// "Export Changed Pages..." on the panel flyout (a plain command). Enabled only during a comparison (sDB != nil) = write the list of changed pages as TSV (new / old / kind). KCMChangedPagesTSV.cpp
// (+35 = kKCMPopupHudActionID, "Show HUD", went out with the feature itself.
//  ★**That number is not reused** ＝ a shortcut assignment is stored in .indk by the NUMERIC
//  ActionID, so handing the number to another feature makes an existing assignment fire that
//  other feature.)
DECLARE_PMID(kActionIDSpace, kKCMPopupTranslucentPanelActionID, kKCMUIPrefix + 36)	// "Translucent Panel" check toggle on the panel flyout (ON = this panel is translucent while floating. ★Windows only. ★While docked it can be ticked but does nothing - the flag is set and takes effect once the panel floats again. Default OFF. KCMPanelAlpha.cpp)
DECLARE_PMID(kActionIDSpace, kKCMPopupTranslucentPagesActionID, kKCMUIPrefix + 37)	// "Translucent Pages Panel" check toggle on the panel flyout (ON = **InDesign's own Pages panel** is translucent while floating. The same machinery as +36 with a different target, found by WidgetID (kPagesPanelWidgetID). ★Windows only, ★no effect while docked. Default OFF. KCMPanelAlpha.cpp)
// ★kKCMUIPrefix + 38 is where a "Translucent Toolbox" toggle stood; it was added and withdrawn
//   the same day on the user's call.
//   **The number is not reused** ＝ shortcut settings (.indk) store an action by its numeric
//   ActionID, so handing an assigned number to another feature makes that shortcut fire
//   something unrelated. Exactly as with +35 above.
DECLARE_PMID(kActionIDSpace, kKCMPopupCompareBooksActionID, kKCMUIPrefix + 39)	// "Compare Books" on the panel flyout (a plain command). ★The book whose tab is in front in the Book panel is the Target, the first other open book is the Source, and every chapter (document) is judged changed or unchanged. ★It is completely independent of the document comparison (Start): it does not arm, creates no mark entries and does not touch sDB / sSrcDB (⚠it does raise tl_Rasterizing while rasterising, the same discipline as MakeEntry; the reason is in the matching paragraph of KCMBookCompare.h). kCustomEnabling (only when two books are there and the front tab can be identified). KCMBookCompare.cpp, with the pair resolved in KCMBookPair.cpp
// The context menu of a chapter row in the book comparison dialog. ★It is not part of the panel
// flyout but **a popup over the row**, so it lives in the MenuDef subtree kKCMBookRowMenuName.
// The action itself cannot tell **which row** was pressed (only an ActionID reaches it), so the
// row is noted by KCMBookSetMenuRow at the moment of the right click ＝ the same construction as
// KBS's result rows.
DECLARE_PMID(kActionIDSpace, kKCMBookRowStartActionID, kKCMUIPrefix + 40)	// chapter row context menu, "Start Change Marker" = open that chapter's Target and Source documents in windows and start the comparison, stopping a running one first (KCMBookOpen.cpp). ★Greyed on a row whose two files are not both there (ChapterAdded / ChapterDeleted / a missing file) through kCustomEnabling -> KCMBookRowCanStart
DECLARE_PMID(kActionIDSpace, kKCMPopupTranslucentBookDialogActionID, kKCMUIPrefix + 41)	// ★"Translucent Dialog" check toggle on the panel flyout (user's request: "let the dialog be translucent too"). **The same implementation** as +36/+37 (KCMPanelAlpha.cpp) with a different target. ⚠The target is not a panel, so **only the source of the window differs**: the dialog hands its own window over through KCMSetBookDialogWindow (the panel manager can be asked by WidgetID; a dialog is not in there). ★Unlike the panel toggles there is no docking, so it has no "ticked but ineffective" state. Default OFF
DECLARE_PMID(kActionIDSpace, kKCMPopupModePixelActionID, kKCMUIPrefix + 42)	// ★"Compare mode > Pixel Changes" on the flyout. The default: rasterise the pages and compare the pixels, which is what this plug-in originally did. Mutually exclusive with Story Changes, the selected one carrying the check (kCustomEnabling + kSelectedAction, the shape of Marks opacity 25%/75%). KCMActionComponent.cpp
DECLARE_PMID(kActionIDSpace, kKCMPopupModeStoryActionID, kKCMUIPrefix + 43)	// ★"Compare mode > Story Changes" on the flyout. Compare the text of the stories, paragraph then character, and list them in Story Edits as a tree (parent = story, children = the changes). ★This mode draws no comparison rings, and **only Hide Unchanged Spreads is greyed**. ⚠What is greyed is not "pressing it does nothing" but "pressing it breaks something": the scroll map, Export Changed Pages and Prev/Next all keep working here, because what they rest on (the page correspondence, the registered pages, the overflow "/") is built in this mode too. Hide Unchanged is the exception - with sEntries empty it would hide every spread except those with a registration or an overflow
DECLARE_PMID(kActionIDSpace, kKCMStoryRowRefreshActionID, kKCMUIPrefix + 44)	// ★"Refresh Story Comparison" on a Story Edits row context menu (user's request: "update the comparison of just that story"). It re-runs the text diff for that story alone and replaces its children with the current state (IKCMStoryEditsFacade::RefreshRow -> KCMStoryDiffRun::RunOne). ★Once the differences are gone **the row stays and only the children go**: what it answers is "what differs now", not "does this row still belong here". ★kCustomEnabling makes it live **only in the Story mode** (greyed in Pixel, while stopped, and on an Added row). ⚠It is the only item in its menu, so greyed means **the menu does not appear at all** (the same as the chapter row menu)

DECLARE_PMID(kActionIDSpace, kKCMPopupShowTgtMarksActionID, kKCMUIPrefix + 45)	// ★"Always Show Marks on Target" check toggle on the panel flyout (user's request: "have the marks show without pressing the tool button, in the pixel mode and the story mode both"). It pairs with the Source version (+10): while ON, the Target document's marks stay on screen. ★In Pixel that is the comparison ring (sTgtMarksOn -> alwaysScreen), in Story the coloured ground under the changed characters (ui/KCMStoryPressMarks.cpp -> the model's KCMStoryMarkBuild) ＝ one toggle with a meaning in both modes. ⚠Screen only: print and PDF are decided by "Print comparison marks" alone (deliberately asymmetric with the Source version, which does print). ★Default OFF, and Start does not touch it (as with the Source version, the setting is saved and restored at startup)
// ★★**Taking a new ActionID: use the next number after the highest one declared above, and
//   check it against this list of slots that must NOT be reused.**
//     +19  Hold to Hide Marks  ) removed features whose shortcut assignments may still exist
//     +35  Show HUD            ) in a user's .indk, which stores an action by its NUMBER
//     +38  Translucent Toolbox )
//     +27  Invoke Pages Panel Shortcut (reserved)
//   ⚠**A "next free: +N" line used to stand here and rotted twice**, the second time while
//     itself carrying the instruction to keep it up to date. A list that is appended to is
//     safe; a number that has to be re-counted is not.)
DECLARE_PMID(kActionIDSpace, kKCMPopupColorRedActionID,  kKCMUIPrefix + 46)	// ★"Mark colour > Red" on the panel flyout (user's request: "let the menu choose red or blue"). Mutually exclusive with Cyan, the selected one carrying the check (kCustomEnabling + kSelectedAction, the shape of Marks opacity 25%/75%). ★The default. KCMActionComponent.cpp -> IKCMCompareFacade::SetMarkColor
DECLARE_PMID(kActionIDSpace, kKCMPopupColorCyanActionID, kKCMUIPrefix + 47)	// ★"Mark colour > Cyan" on the flyout. ⚠★★This **replaces an automatic switch by background**: the ring used to turn cyan by itself wherever the pixels underneath were reddish (kKCMRedBgDom). It went for two reasons -- the user's call ("the user can just choose"), and **the Story mode cannot read the pixels underneath**, so the same automatic test was impossible there and the colour would have been decided differently in the two modes. ★The choice applies to both the Pixel ring and the Story ground (both pass through KCMDrawEventHandler::SelectedMarkColor)

DECLARE_PMID(kActionIDSpace, kKCMPawToolActionID, kKCMUIPrefix + 48)	// the tool-select shortcut of the cat-paw stamp tool, named by its ToolDef. ★No ActionDef is needed -- the toolbox framework provides a tool's own selection action (the same as +29 for the KCM tool)
DECLARE_PMID(kActionIDSpace, kKCMClearPawsActionID, kKCMUIPrefix + 49)	// "Clear Cat Paws in This Document" on the panel flyout (a plain command; Task 6 of the paw stamp plan). Greyed where the active document holds no paw, through kCustomEnabling

// (The template's spare //DECLARE_PMID(kActionIDSpace, kKCMActionID, kKCMUIPrefix + 41) was
//  **deleted**. ⚠★★It was not inert: **+41 is taken** (kKCMPopupTranslucentBookDialogActionID
//  above), so uncommenting it declares one ActionID twice. **A commented-out declaration carries
//  a number, and a number goes stale exactly the way a written total does.**)

// WidgetIDs:
DECLARE_PMID(kWidgetIDSpace, kKCMPanelWidgetID, kKCMUIPrefix + 0)
DECLARE_PMID(kWidgetIDSpace, kKCMTargetTextWidgetID, kKCMUIPrefix + 1)
DECLARE_PMID(kWidgetIDSpace, kKCMSourceTextWidgetID, kKCMUIPrefix + 26)
// +27 and +28 are free (they were kKCMStartButtonWidgetID and kKCMClearButtonWidgetID; start
//   and clear were merged into one toggle button, which has since gone as well)
// +29 is free (it was kKCMPrintCheckWidgetID; the print on/off checkbox became the flyout item
//   kKCMPopupPrintMarksActionID and left the panel)
// +30..+32 are free (they were kKCMOpacityClusterWidgetID / kKCMOpacity25RadioWidgetID /
//   kKCMOpacity75RadioWidgetID; the 25%/75% radios became the flyout items
//   kKCMPopupOpacity25ActionID / kKCMPopupOpacity75ActionID and left the panel)
// +33 is free (it was kKCMHintTextWidgetID; the description left the panel for the flyout item
//   "How to Use")
DECLARE_PMID(kWidgetIDSpace, kKCMIconOnWidgetID, kKCMUIPrefix + 34)
DECLARE_PMID(kWidgetIDSpace, kKCMIconOffWidgetID, kKCMUIPrefix + 35)
DECLARE_PMID(kWidgetIDSpace, kKCMStatusTextWidgetID, kKCMUIPrefix + 36)
// +37 was kKCMToggleButtonWidgetID (the start/clear button, which became the flyout item
//   kKCMPopupStartStopActionID) and has been reused for:
DECLARE_PMID(kWidgetIDSpace, kKCMNavPosTextWidgetID, kKCMUIPrefix + 37)	// the "3/12" position readout between Prev and Next (a centred static text; KCMChangeNav.cpp writes it through KCMSetNavPosition)
DECLARE_PMID(kWidgetIDSpace, kKCMPrevChangeButtonWidgetID, kKCMUIPrefix + 38)	// "< Prev" = scroll to the previous page worth looking at (KCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKCMNextChangeButtonWidgetID, kKCMUIPrefix + 39)	// "Next >" = scroll to the next page worth looking at (KCMChangeNav.cpp)
DECLARE_PMID(kWidgetIDSpace, kKCMScrollMapWidgetID, kKCMUIPrefix + 40)	// the scrollbar map strip, injected at run time beside a document window's vertical scrollbar (KCMScrollMap.cpp)
DECLARE_PMID(kWidgetIDSpace, kKCMToolWidgetID, kKCMUIPrefix + 41)	// the WidgetID of the tool button in the toolbox (KCMTool::InitWidget)
DECLARE_PMID(kWidgetIDSpace, kKCMToolButtonWidgetID, kKCMUIPrefix + 42)	// ★the tool switch button inside the panel (left of Prev, 32x22). Pressing it makes kKCMToolBoss the active tool (KCMActivateOwnTool). A different thing from +41 above, which is the toolbox slot
// ★The cat-paw stamp tool's toolbox widget (2026-09-04). It is kept **next to the two tool IDs
//   above rather than in numeric order**, because that is where a reader looks for it; the
//   number itself is the next free one (+65), the ones between belonging to the Story Edits
//   list. ⚠A subtool occupies no slot of its own in the toolbox, but the widget the tool builds
//   in the flyout still needs an ID (KCMPawTool::InitWidget).
DECLARE_PMID(kWidgetIDSpace, kKCMPawToolWidgetID, kKCMUIPrefix + 65)
// ★The stamp tool's half of the panel's tool button (2026-09-04). ⚠It is NOT a second button:
//   this widget and kKCMToolButtonWidgetID share ONE frame and only the tool that is current is
//   shown, the way the toolbox shows one slot for a tool and its subtools ("one place, two tools"
//   -- the user's words). Pressing the one on show swaps to the other.
//   ★Two widgets rather than one whose picture changes, because that is how this panel already
//     switches artwork (kKCMIconOnWidgetID / kKCMIconOffWidgetID) -- an icon widget's resource is
//     fixed when the panel is built.
DECLARE_PMID(kWidgetIDSpace, kKCMPawToolButtonWidgetID, kKCMUIPrefix + 66)
// ★The "Story Edits" section. The panel is divided by a SplitterPanelWidget and the lower pane
//   lists the stories whose text was edited. Modelled on the "Link Info" section of the
//   product's linksui.
DECLARE_PMID(kWidgetIDSpace, kKCMSplitterWidgetID, kKCMUIPrefix + 43)			// the SplitterPanelWidget that divides the panel (Widgets.fh:462 / kSplitterPanelWidgetBoss)
DECLARE_PMID(kWidgetIDSpace, kKCMTopPaneWidgetID, kKCMUIPrefix + 44)			// upper pane = a GenericPanelWidget holding everything the panel used to hold. ★It is the one named as "the widget not to resize"
DECLARE_PMID(kWidgetIDSpace, kKCMStorySectionWidgetID, kKCMUIPrefix + 45)		// lower pane = the Story Edits section itself (hidden to start with; inside it are the heading band, a rule and the list)
DECLARE_PMID(kWidgetIDSpace, kKCMStorySectionToggleWidgetID, kKCMUIPrefix + 46)	// the open/close triangle. ★It sits **in the upper pane**: in the lower one it would disappear with the section and there would be no way to open it again
DECLARE_PMID(kWidgetIDSpace, kKCMStoryTreeWidgetID, kKCMUIPrefix + 47)		// the Story Edits list itself (filling the lower pane)
DECLARE_PMID(kWidgetIDSpace, kKCMStoryRowTextWidgetID, kKCMUIPrefix + 48)	// row, left: the beginning of the text
DECLARE_PMID(kWidgetIDSpace, kKCMStoryRowKindWidgetID, kKCMUIPrefix + 49)	// row, right: what kind of change it is (Text / Attr / Other / Added)
DECLARE_PMID(kWidgetIDSpace, kKCMStorySectionLabelWidgetID, kKCMUIPrefix + 50)	// next to the triangle in the upper pane: "Story Edits (3)". The count is appended at run time
DECLARE_PMID(kWidgetIDSpace, kKCMStoryRowWidgetID, kKCMUIPrefix + 51)		// the row template itself. ★This is what GetWidgetTypeForNode answers
DECLARE_PMID(kWidgetIDSpace, kKCMStoryChangeRowWidgetID, kKCMUIPrefix + 63)	// ★the template of a **change row** (the second level). ★It **has to differ from +51 above**: the framework decides from the ID GetWidgetTypeForNode answers whether a widget can be recycled, and answering the same one hands a story row's widget to a change row while scrolling. ⚠**Its two cells reuse +48 and +49** -- widget IDs need be unique only among the descendants of one widget (guide vol2-12). The book rows share the same two
DECLARE_PMID(kWidgetIDSpace, kKCMStoryRubyRowWidgetID, kKCMUIPrefix + 64)	// ★the template of a **ruby change row**. It has to differ from +63 for the same reason: the ID GetWidgetTypeForNode answers is what decides whether a widget can be recycled. ⚠Without a separate one, the taller ruby widget is handed to an ordinary change row and **the rows overlap** (the height belongs to the widget, so an unchanged ID means the tree does not replace it)
DECLARE_PMID(kWidgetIDSpace, kKCMStoryRowUIDWidgetID, kKCMUIPrefix + 52)	// ★row, far left: the story's UID in decimal (user's request: "UID, text, and the changed part"). It is what lets a row be followed by eye: two stories with the same opening words are still told apart
// ★The column headings of the list (user's request: "put something like UID / Text / change
//   along the top"). They sit **inside the lower pane, above the tree**, not inside the tree
//   itself, so they stay put while the rows scroll.
//   ★All three are given **the same x coordinates and the same binding as the cells of a row**
//     (KCMUI.fr). That is the only thing keeping the columns aligned; move one side alone and a
//     resizable panel shows it.
// ★★★**The four of them declare no ID** ＝ the `.fr` names `kInvalidWidgetID` (0) for each.
//   **Not giving an ID to a widget nothing refers to is the official practice**: the SDK has
//   over ten `.fr` files with `kInvalidWidgetID` in the WidgetId field (basicdialog,
//   customconditionaltextui, framelabelui and others). ★**0 is exempt from uniqueness, so any
//   number of them may sit under one parent** ---- measured: `framelabelui/FrmLblUI.fr` has ten
//   in one file, four of them (:301/:322/:336/:356) **under the same parent**.
//   ⇒ Nothing refers to these four, from C++ or from anywhere else in the `.fr` (grepped in
//     full), so they had no reason to carry a number. **The freed +53..+56 may be reused**
//     (widget IDs are not written into `.indk`, so the "never reuse a vacated slot" rule that
//     binds ActionIDs does not apply).
//   ⚠**If one of them ever has to be referred to, declare it here again and put the name back
//     into the `.fr` in place of `kInvalidWidgetID`.**
//     ★The contrast is `kKCMTopPaneWidgetID`: the `.fr` itself names it as "the widget not to
//     resize", so it cannot be 0. **"No reference from C++" is not enough -- count the
//     references inside the `.fr` as well.**
//   ⚠**What the four were is kept here**, so that someone reading the `.fr` can find out what a
//     widget with ID 0 is:
//     +53  heading, left: "UID"    (the same 8..48 and kBindLeft as the row's
//          kKCMStoryRowUIDWidgetID)
//     +54  heading, middle: "Story" (the same 52..154 and kBindLeft|kBindRight as the row's
//          kKCMStoryRowTextWidgetID ＝ this is the column that grows)
//     +55  heading, right: "Change" (the same 154..216 and kBindRight, right-aligned, as the
//          row's kKCMStoryRowKindWidgetID)
//     +56  the 1px rule between the headings and the list ＝ **an
//          ErasablePrimaryResourcePanelWidget one pixel high, erased with
//          kInterfaceSeparatorColor** (the erase IS the line). ⚠The stock RuleWidget
//          (Widgets.fh:887 / kRuleWidgetBoss) was tried first and **parsed, built and drew
//          nothing** (the account is in the `.fr`)

// The book comparison dialog. ★OK and Cancel use the stock WidgetIDs (kOKButtonWidgetID /
// kCancelButton_WidgetID), so what has to be declared here is the dialog itself.
DECLARE_PMID(kWidgetIDSpace, kKCMBookDialogWidgetID, kKCMUIPrefix + 57)
DECLARE_PMID(kWidgetIDSpace, kKCMBookTargetTextWidgetID, kKCMUIPrefix + 58)	// "Target: new.indb" (the book whose tab is in front)
DECLARE_PMID(kWidgetIDSpace, kKCMBookSourceTextWidgetID, kKCMUIPrefix + 59)	// "Source: old.indb" (the first other open book)
DECLARE_PMID(kWidgetIDSpace, kKCMBookCompareButtonWidgetID, kKCMUIPrefix + 60)	// (retired) the old "Compare" button. ★The button itself was removed -- the flow became "confirmation alert, then OK compares" -- so **nothing refers to this ID**, but it is kept declared together with its label key kKCMBookCompareKey, the enUS table row and the note in KCMUI.fr, so that the set can be restored together. ★The number is not reused. ⚠This line used to claim it was **the only** declared-but-unreferenced ID "measured mechanically". ★★**That was already untrue when it was written**: kKCMPopupAboutScriptActionID (+3) had been retired-but-reserved all along, and a re-measurement on 2026-09-04 found three of them -- that one, this one, and kKCMClearPawsActionID (+49), a slot booked in advance for the paw stamp's "clear" item. ⇒ ★★★**Do not read a count off any line here; measure it -- and mind the two traps that made the first measurement wrong.** (1) Take the names only from lines that BEGIN with DECLARE_PMID: a commented-out declaration is not a declaration (there is one in the ActionID block). (2) Strip comments before counting occurrences: a name mentioned in prose -- **this sentence included** -- is not a reference, and counting it hides exactly the ID it names. A name left with one occurrence in the stripped text is unreferenced. The label key had said "retired" from the start while this one still read as live
DECLARE_PMID(kWidgetIDSpace, kKCMBookStatusTextWidgetID, kKCMUIPrefix + 61)	// the status line (a summary of the comparison; it always includes the number of chapters)
DECLARE_PMID(kWidgetIDSpace, kKCMBookTreeWidgetID, kKCMUIPrefix + 62)		// the chapter list itself (the largest part of the dialog)
// ★A second status line (the hint that a right click on a changed chapter starts Change Marker)
//   **declares no ID** ＝ the `.fr` names `kInvalidWidgetID`. It is a fixed sentence that the
//   `.fr` carries as its initial text and C++ never touches, so it had no reason to carry a
//   number (the same reasoning as the four column headings above).
// ★★★**The three below SHARE their WidgetIDs with the Story Edits rows** -- not one new number
//   is spent.
//   The grounds ＝ **a widget ID does not have to be unique across the application.** Guide
//   vol2-12:71 says so, in contrast with string keys, which do:
//     "widget identifiers need be unique only within the list of descendants of a given widget,
//      so ... you can reuse a widget identifier (for example, across different dialog boxes or
//      panels you own)"
//   The mechanism backs it up: IPanelControlData::FindWidget(WidgetID, searchLevels) **only
//   walks its own subtree** and is not a global "widget ID -> widget" registry
//   (IPanelControlData.h:89).
//   In practice: the stock kOKButtonWidgetID / kCancelButton_WidgetID are shared by files all
//   over source/sdksamples (the block above uses them already).
//   Here, "the panel's Story Edits list" and "this dialog's chapter list" are in **different
//   subtrees**, so they cannot collide.
//   ⚠**Only child widgets may be shared.** The WidgetID of the **root** of a panel or a dialog
//     has to be unique, because IPanelMgr::GetPanelFromWidgetID looks it up application-wide.
//   ⚠The correspondence is kept role for role (row template to row template, left cell to left
//     cell, right cell to right cell). Shifting it would only confuse the reader.
DECLARE_PMID(kWidgetIDSpace, kKCMBookRowWidgetID, kKCMUIPrefix + 51)		// = the same value as kKCMStoryRowWidgetID. The row template itself. ★This is what GetWidgetTypeForNode answers
DECLARE_PMID(kWidgetIDSpace, kKCMBookRowNameWidgetID, kKCMUIPrefix + 48)	// = the same value as kKCMStoryRowTextWidgetID. Row, left: the chapter file name (with " - reason" appended only when it failed)
DECLARE_PMID(kWidgetIDSpace, kKCMBookRowStateWidgetID, kKCMUIPrefix + 49)	// = the same value as kKCMStoryRowKindWidgetID. Row, right: the verdict (Changed / NoChange / ChapterAdded / ChapterDeleted / Failed). Fixed width, right-aligned
//====================================================================================
// ★★MIND THE CEILING -- this prefix owns "+0 .. +127" (★128 slots **per ID space**)
//
//  The band Adobe issued is **0x1EA500 - 0x1EA5FF, 256 slots**, split in half:
//      model (KCM) = 0x1EA500 / **UI (KCMUI) = 0x1EA580 ＝ this file**
//  ∴ what can be taken from kKCMUIPrefix runs to **+127**.
//  ⚠**+128 is 0x1EA600 ＝ the band KBS uses.** Reach it and two plug-ins claim the same ID; the
//    object model construction at startup ("Completing object model") then **rejects it** ＝ not
//    a quiet misbehaviour but a plug-in that does not load.
//
//  ⚠**Do not trust a "highest offset in use" written here** -- one stood in this block and was
//    two behind by the time it was read. Count the declarations above; the widget space is the
//    fullest of them and still has well over half its slots free.
//
//  When a widget is added, work through these in order:
//
//    (a) ★★★**Do not take a new number at all** -- **a widget ID does not have to be unique
//        across the application.** "Unique **among the descendants of one widget**" is enough,
//        and **it may be reused in a different dialog or panel** (guide vol2-12:71 says so in
//        contrast with string keys, which must be globally unique).
//        Mechanism = IPanelControlData::FindWidget only walks its own subtree (:89).
//        In practice = the stock kOKButtonWidgetID is shared by files all over source/sdksamples.
//        ⇒ **It spends no ID space at all**, so try this first.
//        ⚠But the WidgetID of a **root** has to be unique: IPanelMgr::GetPanelFromWidgetID looks
//          it up application-wide (a panel body or a dialog body cannot be reused).
//        ⚠A widget persists state into saved data, so **the first time a shared form goes in,
//          check it in the running application.**
//
//    (b) **The holes: +3..+25 (never used -- the template's spare slots), and +2, +27..+33,
//        +53..+56 (used and dropped; what each one was is written where it was declared).**
//        ⚠For the dropped ones, look at the git history before reusing one (a widget ID can
//        appear in the workspace, i.e. the panel layout, that a user has saved).
//        ★**This is the only place the free numbers are listed.** A second copy of the +3..+25
//        part stood below as commented-out declarations until 2026-08-29, and the two disagreed.
//        ⚠★★★**+65 IS ALSO A DROPPED NUMBER, AND IT IS THE ONE THE INSTRUCTION ABOVE HANDS YOU.**
//        In the KESCM era the book row's two cells had IDs of their own (+64 name / +65 state);
//        they were then made to share the Story row's +48 / +49, and +65 fell out of use. The
//        highest declaration is now +64, so "take the next number after the highest one declared"
//        lands exactly on it. **Take +3 instead.** (Measured 2026-08-29 by listing every
//        DECLARE_PMID(kWidgetIDSpace, ...) in the whole history of both halves: the set ever
//        declared is 0, 1, 2, 26, 27..33 and 34..65 -- so +3..+25 really are untouched, and +65
//        really was touched.)
//
//    (c) If that is still not enough, **obtain a second prefix** (no existing ID moves, and 256
//        fresh slots arrive with compatibility to released versions intact). The proper way is
//        to ask wwds@adobe.com to issue a prefix ID.
//
//  The full record = docs/ai-notes/guide-gs-04-object-model-read-2026-08-12.md §1
//                    docs/ai-notes/guide-vol2-12-ui-fundamentals-read-2026-08-12.md §0 (for (a))
//====================================================================================
// (+2 was in use 2026-08-13..2026-08-19 for kKCMBookHintTextWidgetID; that widget now names
//  kInvalidWidgetID in the .fr, so +2 is FREE again. See the note where it was declared.)
// (Twenty-three of the template's spare lines stood here -- //DECLARE_PMID(kWidgetIDSpace,
//  kKCMWidgetID, kKCMUIPrefix + 3 .. + 25) -- saying a second time what (b) above says: free.)

// "About Plug-ins" sub-menu:
#define kKCMAboutMenuKey			kKCMStringPrefix "kKCMAboutMenuKey"
#define kKCMAboutMenuPath		kSDKDefStandardAboutMenuPath kKCMCompanyKey

// (kKCMPluginsMenuKey / Path, for the old "Plug-ins" sub-menu, were removed as unused. Where the
//  panel appears in the menus is kKCMPanelWindowMenuName below.)

// ★THE WINDOW MENU'S NAME FOR THE PANEL, WITH A SUB-MENU IN FRONT OF IT.
//
//  PanelList.fh spells this out on the panelName field: "Panel name(used for Window menu). Can
//  also specify a submenu here, as in "MyWindowSubmenu:MyPanelName"" - so a colon buys a level.
//  InDesign's own panels are full of it (Window > Styles > Character Styles is stored exactly like
//  this, as "StylesSubmenu:CharStyles_Menu"), and the sub-menu part is a STRING KEY like any other,
//  so kKCMCompanyKey resolves to "Kohaku Plug-Ins" the same way it does on the Plug-Ins side.
//
//  => Window > Kohaku Plug-Ins > Kohaku Change Marker.
#define kKCMPanelWindowMenuName			kKCMCompanyKey kSDKDefDelimitMenuPath kKCMPanelTitleKey

// (The panel used to be placed on the Plug-Ins menu instead, through kKCMPanelPluginsMenuPath and
//  kKCMPanelPluginsMenuPosition. Both were removed on 2026-08-27 with the move to the Window menu:
//  the PanelList's alternate path is empty now, and they had no other reader.
//
//  ⚠KEEP THIS IN MIND IF ONE IS EVER PUT BACK: a MenuDef path names the menu that HOLDS the item,
//  while panelName names the ITEM. With panelName filled in, a path ending in kKCMPanelTitleKey
//  turns that last component into a SUB-MENU holding a single item - measured on this panel the
//  same day: "Plug-Ins > Kohaku Plug-Ins > Kohaku Change Marker > Kohaku Change Marker". The same
//  mistake is on record in Kohaku InDesign MCP, 2026-08-24, for the same reason.)

// Menu item keys:
// Other StringKeys:
#define kKCMAboutBoxStringKey	kKCMStringPrefix "kKCMAboutBoxStringKey"
#define kKCMRepoURL			"https://github.com/KohakuNekotarou/KohakuChangeMarker"// the distribution URL. ★Two users, and About is not one of them: the panel illustration opens it when clicked (KCMOpenAboutURL) and its tooltip shows it (KCMIconTip.cpp). About itself is one line of name and version
// (kKCMAboutScriptMenuKey / kKCMScriptHelpStringKey went with the "About Scripting" item.)
#define kKCMUsageMenuKey		kKCMStringPrefix "kKCMUsageMenuKey"	// the menu name of "How to Use" on the panel flyout (the body reuses kKCMHintKey)
#define kKCMHideUnchangedMenuKey	kKCMStringPrefix "kKCMHideUnchangedMenuKey"	// the menu name of the "Hide Unchanged Spreads" toggle on the panel flyout
#define kKCMShowOldNumsMenuKey	kKCMStringPrefix "kKCMShowOldNumsMenuKey"	// the menu name of the "Show Original Page Numbers" toggle on the panel flyout
#define kKCMSyncViewsMenuKey		kKCMStringPrefix "kKCMSyncViewsMenuKey"	// the menu name of the "Sync Layout Views" toggle on the panel flyout
#define kKCMAlignViewsMenuKey		kKCMStringPrefix "kKCMAlignViewsMenuKey"	// the menu name of "Align Other Views to Active" (a plain command) on the panel flyout
// ★★**Two display names were changed and nothing else** (user's request: an English name that
//   says "always visible" at a glance). "Show Marks on Target" / "Show Marks on Source" became
//   "Always Show Marks on ...".
//   ⚠**Only the strings in KCMUI_enUS.fr changed** -- the ActionIDs, the string keys, the saved
//   keys ("showTgtMarks" / "showSrcMarks") and the menu positions were all left alone, so an
//   assigned shortcut and a saved panel setting both keep working.
//   ⚠**Why "Hold" is not in the name** is at the top of KCMUI_enUS.fr.
#define kKCMShowSrcMarksMenuKey	kKCMStringPrefix "kKCMShowSrcMarksMenuKey"	// the menu name of the "Always Show Marks on Source" toggle on the panel flyout
#define kKCMShowTgtMarksMenuKey	kKCMStringPrefix "kKCMShowTgtMarksMenuKey"	// the menu name of the "Always Show Marks on Target" toggle on the panel flyout
#define kKCMPageMapToggleMenuKey	kKCMStringPrefix "kKCMPageMapToggleMenuKey"	// the default menu name of the "Register as Added/Removed Pages" toggle on the Pages panel context menu (while it is shown, UpdateActionStates swaps it for Added on the Target and Removed on the Source)
#define kKCMPageCheckMenuKey		kKCMStringPrefix "kKCMPageCheckMenuKey"	// the menu name of the "Check" toggle on the Pages panel context menu
#define kKCMPageRefreshCompareMenuKey	kKCMStringPrefix "kKCMPageRefreshCompareMenuKey"	// the menu name of "Refresh Page Comparison" on the Pages panel context menu
#define kKCMIgnorePageNumMenuKey	kKCMStringPrefix "kKCMIgnorePageNumMenuKey"	// the menu name of the "Ignore Page Number Marker" toggle on the panel flyout
// (kKCMHoldToHideMarksMenuKey went with that toggle - see the note at ActionID +19.)
#define kKCMScrollMapMenuKey		kKCMStringPrefix "kKCMScrollMapMenuKey"	// the menu name of the "Show Scrollbar Map" toggle on the panel flyout
#define kKCMSavePanelStateMenuKey	kKCMStringPrefix "kKCMSavePanelStateMenuKey"	// the menu name of "Save Panel Settings" on the panel flyout
#define kKCMSaveChecksMenuKey		kKCMStringPrefix "kKCMSaveChecksMenuKey"	// the menu name of "Save Check & Register" on the panel flyout
#define kKCMLoadChecksMenuKey		kKCMStringPrefix "kKCMLoadChecksMenuKey"	// the menu name of "Load Check & Register" on the panel flyout
#define kKCMFindOversetMenuKey	kKCMStringPrefix "kKCMFindOversetMenuKey"	// the menu name of the "Find Overset" toggle on the panel flyout
#define kKCMRefreshOversetMenuKey	kKCMStringPrefix "kKCMRefreshOversetMenuKey"	// the menu name of "Refresh Overset" on the panel flyout
#define kKCMExportChangedPagesMenuKey	kKCMStringPrefix "kKCMExportChangedPagesMenuKey"	// the menu name of "Export Changed Pages..." on the panel flyout
#define kKCMCompareBooksMenuKey	kKCMStringPrefix "kKCMCompareBooksMenuKey"	// the menu name of "Compare Books" on the panel flyout (compare two books chapter by chapter)
#define kKCMSetTargetMenuKey		kKCMStringPrefix "kKCMSetTargetMenuKey"	// ★the menu name of "Set as Target" on the panel flyout (the active document becomes the comparison's Target)
#define kKCMSetSourceMenuKey		kKCMStringPrefix "kKCMSetSourceMenuKey"	// ★the menu name of "Set as Source" on the panel flyout (the active document becomes the older version)
#define kKCMBookDialogTitleKey	kKCMStringPrefix "kKCMBookDialogTitleKey"	// the title of the book comparison dialog
#define kKCMBookCompareKey		kKCMStringPrefix "kKCMBookCompareKey"		// (retired) the label of the old "Compare" button. The button was removed, so nothing refers to it, but it is kept together with its enUS table row so that the set can be restored together
#define kKCMBookReadyKey			kKCMStringPrefix "kKCMBookReadyKey"			// the status line before a comparison. ★What reaches it now is only "the dialog was opened without a comparison ever having been run" - otherwise the summary overwrites it
#define kKCMBookHintKey			kKCMStringPrefix "kKCMBookHintKey"			// ★the second status line: the hint that a right click on a row starts the comparison. **It is a fixed sentence**, carried by the .fr as initial text and never written by C++ (unlike the summary, it does not change from run to run)
// ★The confirmation alert of the book comparison (user's instruction: "put up an alert on
//   Compare..., and compare when OK is pressed").
//   ⚠**It is English in every locale** -- the user asked for that, so it is not among the
//     strings KCMLoc.h switches at run time. **Of the pair below, only kKCMBookNoPairKey is
//     Japanese** (ui/KCMLoc.h lists what this half holds).
#define kKCMBookCompareConfirmKey	kKCMStringPrefix "kKCMBookCompareConfirmKey"	// the first line of the "these two will be compared" alert (the full paths of target: / source: follow it)
#define kKCMBookNoPairKey			kKCMStringPrefix "kKCMBookNoPairKey"			// the warning when two books could not be resolved (normally unreachable, since the menu item is greyed)
#define kKCMBookRowStartMenuKey	kKCMStringPrefix "kKCMBookRowStartMenuKey"	// the "Start Change Marker" item on a chapter row context menu
// The chapter row context menu. KCMBookRowEH::RButtonDn puts the MenuDef subtree of this name up
// at the cursor through IMenuManager::HandlePopupMenu -- the same mechanism as the row menus of
// the product's Links and Layers panels, and the same construction as KBS's result rows
// (kKBSResultRowMenuName) and KESCL's report rows.
// ★The name of the root never reaches the screen, so a plain literal will do; it needs no
// translation key.
#define kKCMBookRowMenuName		"KCMRtMenuBookRow"
#define kKCMStoryRowRefreshMenuKey	kKCMStringPrefix "kKCMStoryRowRefreshMenuKey"	// the "Refresh Story Comparison" item on a Story Edits row context menu
// The Story Edits row context menu. The same mechanism as the chapter menu above:
// KCMStoryRowEH::RButtonDn puts the MenuDef subtree of this name up at the cursor through
// HandlePopupMenu.
// ★Its root name never reaches the screen either, so a plain literal will do.
#define kKCMStoryRowMenuName		"KCMRtMenuStoryRow"
#define kKCMTranslucentPanelMenuKey	kKCMStringPrefix "kKCMTranslucentPanelMenuKey"	// the menu name of the "Translucent Panel" toggle on the panel flyout
#define kKCMTranslucentPagesPanelMenuKey	kKCMStringPrefix "kKCMTranslucentPagesPanelMenuKey"	// the menu name of the "Translucent Pages Panel" toggle on the panel flyout (its target is InDesign's own Pages panel)
#define kKCMTranslucentBookDialogMenuKey	kKCMStringPrefix "kKCMTranslucentBookDialogMenuKey"	// the menu name of the "Translucent Dialog" toggle on the panel flyout (its target is the book comparison dialog)
// (kKCMTranslucentToolboxMenuKey went with its feature. Unlike an ActionID a string key is not
//  stored outside the plug-in, so there is nothing to reserve and the line was simply deleted.)

// The action area for shortcut-assignable actions (the same shape as KESCL). Pass
// kKCMPanelMenuActionArea in the area field of an ActionDef and let the StringTable resolve it
// to kKCMPanelMenuActionAreaValue, and those actions appear under Product Area "Palette Menus"
// in Edit > Keyboard Shortcuts as "Kohaku Change Marker: <name>".
// The leading "KBSCE " is the prefix convention for a KBSC editor area key. No default shortcut
// is shipped; the user assigns them.
// ※This is a display label and nothing more. A shortcut itself is held by IShortcutManager
//   against the ActionID plus a context string (IShortcutManager.h, AddShortcut /
//   GetActionIDOfShortcut), so renaming this does not detach an existing assignment.
#define kKCMPanelMenuActionArea		"KBSCE Palette Menus: Kohaku Change Marker: "
#define kKCMPanelMenuActionAreaValue	"Palette Menus:Kohaku Change Marker"

// Panel: the name of the internal flyout (popup) menu, and its menu path.
#define kKCMInternalPopupMenuNameKey	kKCMStringPrefix "kKCMInternalPopupMenuNameKey"
#define kKCMPopupMenuPath				kKCMInternalPopupMenuNameKey

// The "Marks opacity" submenu on the flyout (25% / 75% inside it). The name is an English
// literal (KCM menu names are English in every locale); the children point at
// kKCMOpacitySubmenuPath as their parent menu path.
// The parent node is declared in the MenuDef with actionID 0 and a path ending in the delimiter
// (kSDKDefDelimitMenuPath) -- the same practice as Adobe's own
// open/components/buttonui FormFieldUIMenu.fr and incopyexportui.
#define kKCMOpacitySubmenuName		"Marks opacity"
#define kKCMOpacitySubmenuPath		kKCMPopupMenuPath kSDKDefDelimitMenuPath kKCMOpacitySubmenuName

// The "Mark colour" submenu; its children (Red / Cyan) point at this path as their parent.
// ★Built exactly like Marks opacity above: two mutually exclusive children, the selected one
// carrying the check.
#define kKCMColorSubmenuName			"Mark colour"
#define kKCMColorSubmenuPath			kKCMPopupMenuPath kSDKDefDelimitMenuPath kKCMColorSubmenuName

// The "Compare mode" submenu on the flyout (Pixel Changes / Story Changes inside it).
// ★Same shape as "Marks opacity" above (parent with actionID 0; mutually exclusive children,
//   the selected one checked).
// ★★**Why a submenu**: the flyout already holds more than a dozen items, and with the
//   comparison settings and the display settings at the same level it stops being readable
//   which of them says **what is compared** and which says **how it is shown**.
#define kKCMCompareModeSubmenuName	"Compare mode"
#define kKCMCompareModeSubmenuPath	kKCMPopupMenuPath kSDKDefDelimitMenuPath kKCMCompareModeSubmenuName

// The panel's string keys. The values are in the StringTable of **KCMUI_enUS.fr**, and every
// locale reads that one table (every row of the LocaleIndex points at index_enUS). The jaJP
// table was retired and the strings that speak Japanese moved to the run-time switch in
// KCMLoc.h, which lists them.
#define kKCMPanelTitleKey		kKCMStringPrefix "kKCMPanelTitleKey"
#define kKCMTargetLabelKey	kKCMStringPrefix "kKCMTargetLabelKey"	// the panel's "Target:" label. A literal would collide with the system translation, so it has a key of its own
#define kKCMSourceLabelKey	kKCMStringPrefix "kKCMSourceLabelKey"	// the panel's "Source:" label. The literal "Source:" turns into a style-source phrase in a Japanese locale, so this one needs a key of its own too
#define kKCMStartButtonKey	kKCMStringPrefix "kKCMStartButtonKey"	// the default menu name of the "Start / Stop" flyout item (Start when not running). While it is shown, UpdateActionStates swaps Start and Stop by the armed state (the key is inherited from the old toggle button caption)
#define kKCMPrintCheckKey		kKCMStringPrefix "kKCMPrintCheckKey"	// the menu name of the "Print comparison marks" toggle on the flyout (inherited from the old panel checkbox caption)
// ★The body of the alert shown **only when "Print comparison marks" is switched ON**
//   (user's instruction).
//   ⚠**English in every locale** -- it is not among the strings KCMLoc.h switches (the
//     instruction said English), so there is no Japanese counterpart.
//   ⚠**It is raised in the UI-side DoAction and nowhere else.** The toggle itself,
//     KCMTogglePrintMarks(), is on the model side, and the startup restore of the panel
//     settings (KCMPanelState) goes through that same function -- put the alert there and
//     **it appears on every launch**.
#define kKCMPrintMarksOnKey	kKCMStringPrefix "kKCMPrintMarksOnKey"
#define kKCMOpacity25Key		kKCMStringPrefix "kKCMOpacity25Key"	// the child item name inside "Marks opacity" (= "25%")
#define kKCMOpacity75Key		kKCMStringPrefix "kKCMOpacity75Key"	// the child item name inside "Marks opacity" (= "75%")
#define kKCMColorRedKey		kKCMStringPrefix "kKCMColorRedKey"	// the child item name inside "Mark colour" (= "Red")
#define kKCMColorCyanKey		kKCMStringPrefix "kKCMColorCyanKey"	// the child item name inside "Mark colour" (= "Cyan")
#define kKCMModePixelKey		kKCMStringPrefix "kKCMModePixelKey"	// the child item name inside "Compare mode" (= "Pixel Changes")
#define kKCMModeStoryKey		kKCMStringPrefix "kKCMModeStoryKey"	// the child item name inside "Compare mode" (= "Story Changes")
#define kKCMPrevChangeKey		kKCMStringPrefix "kKCMPrevChangeKey"	// the caption of the "< Prev" button on the panel (English everywhere)
#define kKCMNextChangeKey		kKCMStringPrefix "kKCMNextChangeKey"	// the caption of the "Next >" button on the panel (English everywhere)
#define kKCMHintKey			kKCMStringPrefix "kKCMHintKey"
// ★**The second part of the How to Use text.** DoUsage concatenates it after kKCMHintKey into
//   one alert, so **the reader sees one piece of writing**. It holds everything from "comparing
//   books" on (books -> overset -> the list of changed stories -> print/PDF -> the disclaimer).
//   ⚠**Why it is split ＝ odfrc caps the length of one string in a StringTable**
//     ([[odfrc-long-string-limit]]). The single text measured 3,904B against a single-line
//     figure of 4,004B that had gone through, leaving about 100B -- not enough for the book
//     section. **The cap is per string**, so splitting in two clears it.
//   ★★**The seam was chosen as the place to put the book section** (user's instruction: before
//     "finding overset").
//     ⇒ Part one = the opening through refreshing a page comparison (2,708B); part two = from
//     there to the end (2,113B). **Both have room; add to the shorter one.**
//   ⚠**The Japanese side (kHint / kHint2 in ui/KCMLoc.h) is split at the same point. Do not
//     move one without the other.**
#define kKCMHint2Key			kKCMStringPrefix "kKCMHint2Key"
#define kKCMToolStringKey		kKCMStringPrefix "kKCMToolStringKey"	// the tool name in the toolbox (its tooltip). English in every locale
#define kKCMPawToolStringKey	kKCMStringPrefix "kKCMPawToolStringKey"	// the cat-paw stamp tool's name in the flyout (its tooltip). ★English in every locale, as the line above: the jaJP string table was retired on 2026-08-05 and Japanese now comes from ui/KCMLoc.h at run time -- a tool name is not one of the strings that file carries

// The strings of the Story Edits section. ⚠**Do not use the word "text" in them** -- the list
// carries changes that are not text, so a phrase like "No text edits" would not be true.
#define kKCMStorySectionLabelKey	kKCMStringPrefix "kKCMStorySectionLabelKey"	// the section heading (the count is appended by C++)
#define kKCMStoryNoEditsKey		kKCMStringPrefix "kKCMStoryNoEditsKey"		// the single line shown when there is no change
#define kKCMStoryKindTextKey		kKCMStringPrefix "kKCMStoryKindTextKey"		// row, right: characters changed
#define kKCMStoryKindAttrKey		kKCMStringPrefix "kKCMStoryKindAttrKey"		// row, right: an attribute changed (an applied style, an override or a table rule among them)
#define kKCMStoryKindOtherKey		kKCMStringPrefix "kKCMStoryKindOtherKey"	// row, right: none of the above (rare in practice; see KCMStoryStamp.h)
#define kKCMStoryKindAddedKey		kKCMStringPrefix "kKCMStoryKindAddedKey"	// row, right: nothing on the Source side to compare against
// ★Row, right: nothing on the Target side ＝ **a story that was in the old document and is gone
//   from the new one**.
//   ⚠This row alone is **a row of the Source document**, and clicking it moves the Source
//     window only (KCMStoryJump.cpp).
// ⚠★**The word on screen is "Deleted" while the key and the enum say Removed** (the user's
//   choice). "Delete" on its own is the bare verb and reads as an instruction, which does not
//   match "Added" beside it. The key was not renamed with it because **a string key is not a
//   display string**: renaming would touch the enum, the model and the UI to change four
//   letters that never reach the screen.
#define kKCMStoryKindRemovedKey	kKCMStringPrefix "kKCMStoryKindRemovedKey"	// row, right: nothing on the Target side ("Deleted")
// ★Row, right: "the texts were compared and there is no difference". ⚠**Not "unchanged"** -- the
//   counters did move, or there would be no row at all. What it says is that the words are the
//   same.
//   ⇒ It tells a row that has been brought back into agreement (Refresh Story Comparison) apart
//     from one that could not be compared in the first place.
#define kKCMStoryKindNoneKey		kKCMStringPrefix "kKCMStoryKindNoneKey"		// row, right: no difference in the text
#define kKCMStoryKindRubyKey		kKCMStringPrefix "kKCMStoryKindRubyKey"		// row, right: the ruby changed while the text did not. ★It names the case rather than reporting the counter-derived "Attr"
#define kKCMStoryKindKentenKey	kKCMStringPrefix "kKCMStoryKindKentenKey"	// row, right: the emphasis marks changed while the text did not. Same shape as the ruby key above and for the same reason - "Attr" would be true and useless. ⚠The word is the typographic term, not a translation of it: the panel is English (KCM's convention) and "Kenten" is what the Kenten panel this reader compares against is called
// ⚠★★**The kenten (emphasis dot) key was removed** ＝ what Story Edits reports is text changes
//   and ruby, and nothing else (user's decision). The key that existed for one day became a
//   string nobody asked for the moment the comparison behind it was stopped, so the two went
//   together (with the matching row in KCMUI_enUS.fr).
//   ★The side that reads kenten out of a snippet is still there (KCMSnippetText.h), so bringing
//     it back needs that one comparison and this key.

// The column headings of the list. ★**Do not reuse the words from inside it**: the second
// heading is "Story" rather than "Text" and the third is "Change" rather than "Kind" (the
// user's call). The reason is collision -- "Text" is one of the VALUES in the third column, so
// heading the second one "Text" would put the same word twice in one row meaning two different
// things.
#define kKCMStoryColUIDKey		kKCMStringPrefix "kKCMStoryColUIDKey"		// heading, left: the story's UID
#define kKCMStoryColTextKey		kKCMStringPrefix "kKCMStoryColTextKey"		// heading, middle: the opening of the text
#define kKCMStoryColKindKey		kKCMStringPrefix "kKCMStoryColKindKey"		// heading, right: what kind of change it is

// PNG icon resources (compiled into the plug-in; nothing ships beside the .pln).
#define kKCMIconOnResID	1001
#define kKCMIconOffResID	1002
#define kKCMPaletteIconResID	1003	// the small dock tab icon shown when the panel is collapsed

// The view resource ID of the scrollbar map strip (kViewRsrcType; built at run time with
// ::CreateObject2<IControlView>. KCMScrollMap.cpp).
#define kKCMScrollMapRsrcID	1010

// The view resource ID of the Story Edits row template (kViewRsrcType; built one row at a time
// with CreateObjectNoInit. KCMStoryTreeWidgetMgr.cpp).
#define kKCMStoryRowRsrcID	1011

// The view resource ID of the book comparison dialog (kViewRsrcType); KCMBookDialog.cpp names it
// in a RsrcSpec. ★It continues the numbering of the two above. Modelled on KESCL's Jump Offset
// dialog (which uses kSDKDefDialogResourceID).
#define kKCMBookDialogRsrcID	1012

// The view resource ID of the row template in that dialog's chapter list (kViewRsrcType; built
// one row at a time with CreateObjectNoInit. KCMBookTreeWidgetMgr.cpp). ★Built like the Story
// Edits row, differing only in having two columns and using the dialog font.
#define kKCMBookRowRsrcID	1013

// ★The template of a **change row** (the second level) in Story Edits. Copied from the story row
// and cut down to the **two** cells it needs -- the text and the sign; the UID cell belongs to a
// story row alone. ⚠Its cells **start further right, and that offset IS the indent of the
// level**.
// ⚠**Do not add the indent in code**: the reason is at ApplyIndentToWidget in
//   KCMStoryTreeWidgetMgr.cpp (row widgets are recycled, so "move it N to the right"
//   accumulates).
#define kKCMStoryChangeRowRsrcID	1014

// ★The template of a **ruby change row** in Story Edits. The same two cells as the change row,
// differing only in being **twice as tall** -- the upper line is where the reading goes, above
// the characters it belongs to (KCMStoryCellView.cpp divides the cell in half).
// ⚠The cell boss and the WidgetIDs are the change row's, reused: only the Frame heights differ,
// so there is no reason for a second implementation ("draw it on two lines" reaches the cell at
// run time).
#define kKCMStoryRubyRowRsrcID	1015

// The row height of the chapter list. ★As with kKCMStoryRowHeight below, **both the .fr and the
// C++ read this one constant** (the row resource's Frame, the tree's scroll increment,
// GetNodeWidgetHeight).
// ★Unlike the 19 below, this is 22, the SDK's own kCC2016PanelTreeNodeHeight
// (StdHeightWidthConstants.h:50). The palette list is 19 because **the palette font was
// measured**; the dialog font was not ＝ **where nothing was measured, follow the standard.**
// The product's AutoCorrect preferences list builds its in-dialog list from the same constant
// (AutoCorrectPrefsPanel_enUS.fr:288).
#define kKCMBookRowHeight	22

// The row height of the list. ★**Both the .fr and the C++ read this one constant** (Adobe's
// StdHeightWidthConstants.h has the same shape) ＝ the row resource's Frame, the tree's scroll
// increment and GetNodeWidgetHeight all state one fact. The value is 19, the same as KBS's
// kKBSResultRowHeight ＝ **measured with the palette font**, not the SDK's
// kCC2016PanelTreeNodeHeight (22).
#define kKCMStoryRowHeight	19

// ★★The height of a ruby change row alone. Putting the reading **above** its base characters
// **and at the same character size** (user's instruction) takes two lines.
// It is 38 rather than twice 19 because 19 is "one line (18px) + 1px": two lines are 18x2 + 2.
// The 18.0 line advance is measured with FontInfoGetDVAFontMetrics (ascent + descent +
// leading); PMMeasureString("Ag") answers 19.0, which is 1px too much (memory
// kescm-status-text-selfdrawn holds that measurement).
// ⚠★**Rows now differ in height, so ITreeViewMgr::ChangeRoot can no longer be passed kTrue** --
//   that argument is a promise that every row widget is the same height, and breaking it makes
//   the tree mis-measure itself (KCMStoryTreeRebuild).
// ⚠The C++ is not the only reader: the row resource in the .fr (kKCMStoryRubyRowRsrcID) writes
//   the same value.
#define kKCMStoryRubyRowHeight	38

// The height of the heading band of the list (a 14px label + a 1px rule + 3px of padding).
// ★As with the row heights, **both the .fr and the C++ read this one constant** ＝ thicken the
//   band and the tree's position and the section's minimum and default heights all move with it
//   (KCMUI.fr / KCMStorySection.cpp).
#define kKCMStoryHeaderHeight	18

// ★★**The panel's minimum size** (user's instruction: "make what we have now the minimum, and
//   let the panel be resizable"). PanelList is kIsResizable, so the floor is kept by
//   KCMPanelView::ConstrainDimensions.
// - Width = what used to be the fixed width. Everything inside is bound to the edges, so growing
//   is free; narrower than this and the status area is unreadable while the list rows are
//   nothing but ellipses.
// - Height = the designed height of the top pane. ★**While Story Edits is closed this is the
//   ceiling as well** -- closed, the panel is a block of fixed-coordinate controls, so
//   stretching it only adds an empty band at the bottom.
//   While it is open, the floor is "the top pane + the section's minimum (the Bottom snap in the
//   .fr)", and the C++ asks the splitter for the actual snap value rather than writing that
//   number in a second place.
#define kKCMPanelMinWidth		224
#define kKCMPanelTopPaneHeight	185

// The resource ID of the check-mark cursor. It is the CursorID of a CursorSpec, and HOTC(this ID)
// gives the hotspot (the check mark's vertex, which is the point the click is taken at).
// ★The image is a PNGC resource rather than a drawing callback (KCM_Check_10_18.png, with
//   @2x = +kHIDPICrsrOffset and @3to2x = +kHIDPI150CrsrOffset). That cut off the source of the
//   rubbish seen on press: the base class re-fetches the modal cursor, which ran the callback
//   again. KCMCursorProvider.cpp / KCMUI.fr.
#define kKCMCheckCursorResID	1020

// The resource ID of the CMYK readout cursor used by Alt + left ("compare colour"). Its HOTC is
// the check cursor's (10,18), but **the CursorID is deliberately separate**: sharing one made
// the cursor cache confuse the 24x24 check with the 150x60 readout, and rubbish showed for one
// frame at the start of a colour comparison (reported by the user). KCMPeek.cpp.
#define kKCMCmykCursorResID	1021

// The second resource ID of the CMYK readout cursor, alternated with the one above. While
// dragging, the numbers are updated by **re-installing a kFalse spec** -- a dynamic kTrue spec
// showed an uninitialised buffer at the instant it was set, which was the real source of the
// rubbish in the first frame, so those are gone entirely -- and the two IDs are used in turn so
// that re-installing the same spec is never taken as a no-op (InstallCmykCursor in
// KCMTracker.cpp). The HOTC is the same (10,18), so switching does not move the pointer.
#define kKCMCmykCursor2ResID	1022

// The resource ID of the inactive check cursor (outlined: black edge, white body). While the
// tool is active it is shown wherever the black one is not, saying **"the tool does nothing
// here"** (the rule itself is KCMToolCursorShouldBeBlack in KCMCmykCursor.cpp).
// ★A white outline rather than grey: grey was hard to tell apart, so the two were made
//   inverses of each other (user's instruction).
// The CursorID is separate for the same cache reason as the two above, and because switching
// then needs no ClearCache. The HOTC is (10,18), as for the black one, and the image is a PNGC
// resource (KCM_CheckOff_10_18.png, with @2x / @3to2x). KCMCursorProvider.cpp / KCMUI.fr.
#define kKCMCheckCursorInactiveResID	1023

// The dedicated toolbox icon of the KCM tool (32x32 normal / 64x64 = +kHIDPIIconOffset). It used
// to borrow the panel icon (kKCMIconOnResID) until artwork of its own arrived
// (KCM_Tool_32.png/_64.png, supplied by the user). There is no dark version, so PNGAD points at
// the light artwork as well.
// The cat-paw stamp tool's cursor: a pink paw with a dark rim, hot spot at its centre (10,10) --
// the point the paw is placed at, so the cursor shows where it will land. A PNGC resource, not a
// drawing callback, for the reason written at the check cursor above.
// ★Its own CursorID, separate from 1020..1023, so switching tools really changes the spec (the
//   cursor cache is keyed by ID -- sharing one is what once mixed up the check and the readout).
#define kKCMPawCursorResID	1024

#define kKCMToolIconResID	1030

// The toolbox icon of the cat-paw stamp tool (32x32 normal / 64x64 = +kHIDPIIconOffset), drawn
// by work/kcm-make-paw-icons.ps1. There is no dark version, so PNGAD points at the light
// artwork, exactly as the line above.
// ★The ToolDef resource is declared with this same number (a resource ID is a namespace per
//   type, so the ToolDef and the PNGs may share it -- the KCM tool does the same).
#define kKCMPawToolIconResID	1031

// The PANEL button's copies of those two icons, each carrying the little FLYOUT TRIANGLE a toolbox
// slot wears when it has subtools -- the sign that holding the button down offers another tool.
// ★★They are separate resources, and have to be: THE TOOLBOX DRAWS ITS OWN MARK on 1030/1031, so a
//   single image with the triangle already in it would show two of them there. The panel draws no
//   mark of its own, which is why it needs one drawn in.
// ★Made by work/kcm-make-paw-icons.ps1 (Add-FlyoutMark), which reads 1030/1031's artwork and
//   writes a copy -- the user's own KCM_Tool_32/64.png is never modified.
#define kKCMToolPanelIconResID	1032
#define kKCMPawToolPanelIconResID	1033

// Menu item positions.
// ⚠**There is no written running order of the flyout here any more.** One stood in this place
//   and rotted: it listed a toggle that had been removed, missed five items that had been
//   added, and gave one position to the wrong item. A second copy of an order that only the
//   #defines below decide cannot be kept true ([[one-question-one-place]]).
//   ⇒ **Read the values below in ascending order: that IS the flyout.**
// ※Menu names are English in every locale. The separators are Sep1 / OversetSep / Sep3 / Sep2.
#define kKCMStartStopMenuItemPosition		9.0	// "Start / Stop" at the head of the flyout. Its name follows the armed state between Start and Stop
#define kKCMSetTargetMenuItemPosition		9.02	// ★"Set as Target" -- **directly under Start, above Compare Books**: choosing the two documents is part of starting a comparison, so it reads Start / choose / choose
#define kKCMSetSourceMenuItemPosition		9.03	// ★"Set as Source", right below its Target counterpart (the pair reads new-then-old, as the two "Always Show Marks on" toggles do)
#define kKCMSep1MenuItemPosition			9.1	// the separator below Start (a path ending in ":-")
#define kKCMCompareModeSubmenuMenuItemPosition	9.15	// ★the "Compare mode" submenu (Pixel Changes / Story Changes). **Right after Sep1, above the display toggles**: what is compared is settled before how it is shown, and the order carries that
#define kKCMModePixelSubMenuItemPosition		1.0	// inside "Compare mode": Pixel Changes (checked when selected)
#define kKCMModeStorySubMenuItemPosition		2.0	// inside "Compare mode": Story Changes (exclusive with Pixel)
// -- the display toggles --
// (9.20 is free: it belonged to "Hold to Hide Marks", which was removed.)
#define kKCMIgnorePageNumMenuItemPosition	9.22	// check toggle "Ignore Page Number Marker"
#define kKCMOpacitySubmenuMenuItemPosition	9.24	// the "Marks opacity" submenu (25% / 75% inside it)
#define kKCMPrintMarksMenuItemPosition	9.26	// check toggle "Print comparison marks"
#define kKCMOpacity25SubMenuItemPosition	1.0	// inside "Marks opacity": 25% (checked when selected)
#define kKCMOpacity75SubMenuItemPosition	2.0	// inside "Marks opacity": 75% (exclusive with 25%)

#define kKCMColorSubmenuMenuItemPosition	9.25	// the "Mark colour" submenu (Red / Cyan). ★Directly below Marks opacity and above Print ＝ **colour and strength sit together** (both are how a mark looks)
#define kKCMColorRedSubMenuItemPosition	1.0	// inside "Mark colour": Red (the default; checked when selected)
#define kKCMColorCyanSubMenuItemPosition	2.0	// inside "Mark colour": Cyan (exclusive with Red)
// (9.27 is free: it belonged to "Show HUD", which went with its feature.)
#define kKCMShowOldNumsMenuItemPosition	9.28	// check toggle "Show Original Page Numbers"
#define kKCMShowTgtMarksMenuItemPosition	9.29	// check toggle "Always Show Marks on Target" (★directly above the Source one, so the pair reads new-then-old)
#define kKCMShowSrcMarksMenuItemPosition	9.30	// check toggle "Always Show Marks on Source"
#define kKCMScrollMapMenuItemPosition		9.32	// check toggle "Show Scrollbar Map"
#define kKCMSyncViewsMenuItemPosition		9.34	// check toggle "Sync Layout Views"
#define kKCMTranslucentPagesPanelMenuItemPosition	9.36	// check toggle "Translucent Pages Panel" (★Windows only; it makes InDesign's OWN Pages panel translucent while floating)
// (9.37 is free: it belonged to "Translucent Toolbox", which went with its feature.)
#define kKCMTranslucentPanelMenuItemPosition	9.38	// check toggle "Translucent Panel", the last of the display toggles (★Windows only; the panel itself while floating)
#define kKCMTranslucentBookDialogMenuItemPosition	9.39	// check toggle "Translucent Dialog" (★Windows only; the book comparison dialog). The last of the three Translucent items
// -- the Overset group --
#define kKCMOversetSepMenuItemPosition	9.40	// the separator above the Find Overset group (a path ending in ":-")
#define kKCMFindOversetMenuItemPosition	9.42	// check toggle "Find Overset" (crosses on the overset pages of the active document)
#define kKCMRefreshOversetMenuItemPosition	9.44	// plain command "Refresh Overset" (live only while the toggle is ON = rescan)
// -- the plain commands --
#define kKCMSep3MenuItemPosition			9.50	// the separator below Refresh Overset (a path ending in ":-"); the plain commands go below it
#define kKCMAlignViewsMenuItemPosition	9.52	// plain command "Align Other Views to Active", first of the group
#define kKCMHideUnchangedMenuItemPosition	9.54	// check toggle "Hide Unchanged Spreads". ⚠It once shared 9.54 with Compare Books, and **two items at one value leave the order to the MenuDef registration order alone**; Compare Books has since moved up under Start
#define kKCMSavePanelStateMenuItemPosition	9.56	// plain command "Save Panel Settings"
#define kKCMSaveChecksMenuItemPosition	9.58	// plain command "Save Check & Register"
#define kKCMLoadChecksMenuItemPosition	9.60	// plain command "Load Check & Register"
#define kKCMExportChangedPagesMenuItemPosition	9.53	// plain command "Export Changed Pages..." (the list of changed pages as TSV), directly below Align
// ★★Compare Books sits at 9.05, **between Start (9.0) and the separator Sep1 (9.1)** ＝ no rule
//   falls between it and Start, so the two items that **begin** a comparison read as one group
//   (user's instruction: "one higher" and then "just below Start").
//   ⚠The older reasoning -- "it is a route independent of the document comparison, so it belongs
//     with the plain commands rather than with Start" -- **was withdrawn** by that call.
#define kKCMCompareBooksMenuItemPosition	9.05	// plain command "Compare Books" (compare two books chapter by chapter), directly below Start
// Positions inside the chapter row context menu of the book comparison dialog. ★That menu holds
// one item, so the value itself carries no meaning (it is a different tree from the panel
// flyout, under kKCMBookRowMenuName).
#define kKCMBookRowStartMenuItemPosition	1.0		// chapter row context menu: "Start Change Marker"
#define kKCMStoryRowRefreshMenuItemPosition	1.0	// Story Edits row context menu: "Refresh Story Comparison" (a different subtree, so it may share 1.0 with the chapter row)
// -- the informational items, at the end --
#define kKCMSep2MenuItemPosition			9.95	// the separator above How to Use (a path ending in ":-")
#define kKCMUsageMenuItemPosition			10.0	// "How to Use"
// Positions inside the Pages panel page context menu (internal name RtMenuPagesPanel, confirmed
// in the running application). The KCM items go after InDesign's own. The internal name is an
// untranslated key, so it works in every locale.
#define kKCMPageMapSepMenuItemPosition	2999.0	// the separator directly above the KCM items (Register 3000.0 / Check 3001.0), setting them apart from InDesign's own
#define kKCMPageMapToggleMenuItemPosition	3000.0
#define kKCMPageCheckMenuItemPosition		3001.0	// "Check", directly after Register
#define kKCMPageRefreshCompareMenuItemPosition	3002.0	// "Refresh Page Comparison", directly after Check
#define kKCMAboutThisMenuItemPosition		12.0	// "About this plug-in", at the end (11.0 was About Scripting, which has been removed)


// Initial data format version numbers
#define kKCMUIFirstMajorFormatNumber  RezLong(1)
#define kKCMUIFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKCMUICurrentMajorFormatNumber kKCMUIFirstMajorFormatNumber
#define kKCMUICurrentMinorFormatNumber kKCMUIFirstMinorFormatNumber

#endif // __KCMUIID_h__
