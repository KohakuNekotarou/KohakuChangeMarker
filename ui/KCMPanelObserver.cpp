//========================================================================================
//
//  KCMPanelObserver.cpp
//
//  The IObserver of the ChangeMarker panel.
//  ★Start/Clear, the print toggle and 25%/75% moved to the flyout menu, so the panel now holds
//    only the Target:/Source: document labels, the Prev/Next buttons, the status line and the
//    illustration. What is left here is (a) the Prev/Next presses, (b) reflecting the real state
//    in AutoAttach (never writing a fixed default ＝ [[panel-autoattach-read-real-state]]) and
//    (c) the entry points that update what the panel shows (KCMRefreshPanel / KCMSetStatus /
//    KCMSetNavPosition / KCMGetVisibleOwnPanel).
//  ★"Running the Start/Stop itself", which used to be part of (c), moved to KCMComparisonRun.cpp
//    ＝ **this file only drives what the panel displays**.
//  The Target:/Source: labels and the ON/OFF icon reflect the armed state, which is shared across
//  the application, so reopening the panel still shows the right thing.
//
//  Modelled on SnippetRunner's panel observer (SnipRunPanelWidgetObserver.cpp).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "ISubject.h"
#include "ITextControlData.h"
#include "ITriStateControlData.h"
#include "IBooleanControlData.h"
#include "ISession.h"				// GetExecutionContextSession (can be nil during teardown, so the type is named explicitly)
#include "IApplication.h"			// QueryApplication
#include "IPanelMgr.h"				// QueryPanelManager / GetVisiblePanel (updating the panel from outside)
// (IActiveContext.h was removed: this file never asks for the active context. It was a leftover
//  from before the split, when "get the db from the active document" lived here.)
#include "IDocument.h"
#include "IDocumentList.h"

// General includes:
#include "CObserver.h"
#include "widgetid.h"				// kTrueStateMessage / kFalseStateMessage
#include "IDataBase.h"				// GetSysFile (the full path of Target/Source; dereferenced only after the liveness check)
#include "SDKFileHelper.h"			// IDFile -> a path string (Target/Source are shown as full paths)

// Project includes:
#include "KCMUIID.h"
#include "IKCMStatusTextData.h"	// ★the message area is self-drawn ＝ four pieces are written, not one string
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// ★the way to ask the model for the armed state and the status
								//  string. Reading is GetSessionStatus, writing is StoreSessionStatus
								//  ---- the latter came here when the halves became two .pln, because a
								//  free function of the model cannot be linked from another one.
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMChangeNav.h"			// KCMGotoNextChange / KCMGotoPrevChange (the Prev and Next buttons)
// ★The six functions that start and clear a comparison moved to KCMComparisonRun.cpp, and the
//   includes only they used (KCMScrollMap.h / KCMDrawEventHandler.h / KCMOversetApply.h /
//   PersistUtils.h) went with them. ⇒ This file is UI that drives **the panel display alone**.
#include "KCMPanelState.h"		// KCMLoadPanelStateIfPresent (the main route is startup, in KCMUIStartup; this is the safety net)
#include "KCMPanelTitle.h"		// KCMPanelTitle::Update (write the current mode on the tab when the panel opens)
#include "KCMPanelAlpha.h"		// KCMAttachPanelVisibilityObserver / KCMApplyAllPanelTranslucency
									// (re-apply the translucency when the panel is shown again).
									// ⚠This said KCMApplyPanelTranslucency once; what this file calls is
									// **the All one**. KCMPanelAlpha.h had already corrected the same
									// mix-up on its side, and **its sibling here had been left**.
#include "KCMPathDisplay.h"		// KCMPathForDisplay (show the Target:/Source: paths with "/" separators)
#include "KCMStorySection.h"		// KCMUpdateStorySectionLabel (the count in the heading is part of the armed-state display)
#include "KCMStoryTree.h"			// KCMStoryTreeRebuild (what the list holds changes with the armed state too)

/** Watches the widgets of the ChangeMarker panel and drives the shared overlay actions. */
class KCMPanelObserver : public CObserver
{
public:
	KCMPanelObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KCMPanelObserver() {}

	virtual void AutoAttach();
	virtual void AutoDetach();
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);

private:
	void SetWidgetAttached(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid, bool16 attach);
	void AttachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid);
	void DetachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid);

	void UpdateInfoDisplay();
};

CREATE_PMINTERFACE(KCMPanelObserver, kKCMPanelObserverImpl)

//----------------------------------------------------------------------------------------
// (★Remembering this session's status string is **the model side's** job (KCMModelNotify.cpp).
//  The reason is that app.kcmStatus -- served by the ScriptProvider, which is model-side --
//  **answers with the panel closed**, and that the panel rebuilds its widgets on every re-show.
//  ★What is left in this file is **the display alone**: KCMSetStatus hands what it wrote to the
//  model through the facade's StoreSessionStatus, and AutoAttach reads it back with
//  GetSessionStatus.
//  ⚠The contents of the message widget are persisted into the workspace, so opening the panel
//  from its icon after a restart shows **the previous session's string** ---- which is why
//  AutoAttach always overwrites it. That has not changed with the widget itself (it is a
//  self-drawn KCMStatusTextWidget now, not the stock StaticMultiLineTextWidget).)
//----------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------
// Local helpers
//----------------------------------------------------------------------------------------

// The **full path** of the document that owns db (its name when the path cannot be had).
//
// ★★It shows a path rather than a name (user’s instruction, "the panel too") for the same reason
//   as the book comparison dialog: **the two being compared are versions of one job and often
//   have the same file name**. With names alone, Target and Source read as the same string and
//   the two lines say nothing about which is which.
//
// ★★It went from "the last two components (parent folder\file name)" to the full path the same
//   day (user’s instruction: "try the full path"). **Overflowing was accepted deliberately**, so
//   fitting is left entirely to the widget and nothing is trimmed here. The line is only 208px
//   wide, so a full path usually overflows and the matching `.fr` uses kEllipsizeBeginning ＝
//   **the front is cut** (`...\new\ch01.indd`). ⚠On an overflowing line the leading "Target: "
//   goes with it -- the order of the two lines (Target above, Source below) stands in for it.
//   ★The book comparison dialog shows full paths with front ellipsis as well. **Two places, one
//   answer.**
//
// ★An unsaved document has no file (IDataBase.h:270-273 says so), and then this falls back to the
// document name.
static PMString KCMDocPathFromDB(IDataBase* db)
{
	PMString name;
	name.SetTranslatable(kFalse);
	if (db == nil)
		return name;

	// ★★**The liveness check comes first.** This db is a raw pointer to the armed Target or Source,
	//   and between the moment a document closes and `KCMHandleDocsClosed` disarming it, **there is
	//   nothing at the other end**. The rule across KCM is "a closed db may only be compared as a
	//   pointer against FindDocByDataBase, never dereferenced", and `GetSysFile()` is such a
	//   dereference.
	//   ⚠This function is called from the panel’s Update, and **the panel’s Update does run during
	//     teardown** (measured; it is why KCMDetachPanelVisibilityObserver exists).
	//   ★A db that has been shown to be alive may be dereferenced ---- which is where GetSysFile
	//     below stands.
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return name;

	IDocument* d = docList->FindDocByDataBase(db);
	if (d == nil)
		return name;			// a closed or unknown db = answer nothing, and touch nothing

	// ★The path is asked of the db directly (its liveness is established). The name, in the fallback
	// below, comes through IDocument.
	const IDFile* sysFile = db->GetSysFile();
	if (sysFile != nil)
	{
		SDKFileHelper helper(*sysFile);
		PMString path = helper.GetPath();
		if (!path.IsEmpty())
		{
			// ★The separator is shown as "/" (user’s request): in a Japanese environment "\" is drawn as
			//   the yen sign, so "...\new\ch01.indd" reads as "...¥new¥ch01.indd".
			//   The rule is in one place, KCMPathDisplay.h ---- the book comparison goes through the same
			//   function (★**three routes reach it**: these two panel lines, the two lines of the dialog,
			//   and the confirmation alert before a comparison. The head of KCMPathDisplay.h counts them;
			//   an older note here left the alert out).
			return KCMPathForDisplay(path);
		}
	}

	d->GetName(name);

	// ★A long string is not trimmed here: that is left to the widget’s ellipsize (kKCMTargetTextWidgetID
	//   / kKCMSourceTextWidgetID in KCMUI.fr), which judges by frame width rather than character
	//   count and therefore fits Japanese (full-width) text correctly too.
	//   (There used to be a character-count trim of the front here as well, while the `.fr` trims
	//    the end ＝ the two worked twice over and in opposite directions, and the point of showing
	//    the end was never achieved.)
	name.SetTranslatable(kFalse);
	return name;
}

//----------------------------------------------------------------------------------------
// Attach / detach
//----------------------------------------------------------------------------------------

void KCMPanelObserver::AutoAttach()
{
	// ★Reading the saved panel settings has been moved forward to startup (KCMUIStartup::Startup):
	//   syncing runs while stopped with the tool active, so a saved setting has to take effect
	//   before the panel is opened.
	//   This call is the safety net in case the order of the startup services ever changes (normally
	//   a no-op through the once-per-session guard, and it never rolls back a setting changed
	//   since).
	KCMLoadPanelStateIfPresent();

	// ★★Put the current mode on the tab. **This is the first moment it can be written**: the label
	//   goes to the palette, and the palette can only be reached once
	//   `IPanelMgr::GetPanelFromWidgetID` answers with a panel (during the startup restore it is
	//   still nil and KCMPanelTitle returns quietly).
	//   ⚠Unlike a widget, the label belongs to the palette and **survives a re-open**; writing it
	//     here is cheap, and it is the only place the mode restored above becomes visible.
	KCMPanelTitle::Update();

	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->AttachWidget(pcd, kKCMPrevChangeButtonWidgetID,   IBooleanControlData::kDefaultIID);
	this->AttachWidget(pcd, kKCMNextChangeButtonWidgetID,   IBooleanControlData::kDefaultIID);
	// Clicking the illustration (the ON and OFF icons, only one of them visible) opens the
	// distribution URL. A boss based on RollOverIconButtonWidget sends kTrueStateMessage through
	// ITriStateControlData on a click (the same practice as the pictureicon sample's
	// PicIcoRollOverButtonObserver).
	this->AttachWidget(pcd, kKCMIconOnWidgetID,             ITriStateControlData::kDefaultIID);
	this->AttachWidget(pcd, kKCMIconOffWidgetID,            ITriStateControlData::kDefaultIID);
	// ★The tool switch button (left of Prev). Pressing it makes this plug-in’s toolbox tool active.
	//   It is a boss of the same RollOverIconButtonWidget family, so it is received exactly like the
	//   two above.
	this->AttachWidget(pcd, kKCMToolButtonWidgetID,         ITriStateControlData::kDefaultIID);

	// (The print toggle and the 25%/75% opacity moved to the flyout menu:
	//  kKCMPopupPrintMarksActionID / kKCMPopupOpacity25ActionID / kKCMPopupOpacity75ActionID.
	//  UpdateActionStates reads the engine state (KCMGetPrintMarks / KCMGetMarkOpacity25) and shows
	//  it as a check mark, so no panel widget has to be restored here any more.)

	this->UpdateInfoDisplay();		// armed: the Target/Source names and the ON icon; not armed: no names and OFF

	// The status area is persisted into the workspace, so opening the panel from its icon after a
	// restart shows the previous session’s string. It is always overwritten here with what this
	// session has shown (empty if nothing yet).
	// ★When nothing has been shown -- the first time the panel is opened -- an English hint is put up
	//   (open the source and target documents, then choose Start from the flyout). After that the
	//   real messages overwrite the remembered one, so only the last of them survives as history.
	// ⚠**Do not use this branch as a test for "nothing has been done yet"**: showing the hint goes
	//   through KCMSetStatus and fills the remembered value, so from the second time on the else
	//   side always runs and restores the same hint (the screen looks identical either way).
	//   ★app.kcmStatus answers with the same value, so a script cannot tell "untouched" apart
	//   either.
	// ★★**It is restored as four pieces.** The area is self-drawn, and the message shown when a
	//   change row is clicked is split into heading / context / changed characters / context.
	//   Restoring it as one concatenated string would make **the colours quietly disappear just from
	//   closing and reopening the panel** -- the same sentence would look different depending on the
	//   route it took. ⇒ It is remembered in the same one place on the model side, and that place
	//   holds four pieces.
	//   ★An ordinary message fills only the middle piece, so this route does not change how it looks.
	PMString savedLabel, savedPre, savedMid, savedPost, savedRuby;
	Utils<IKCMCompareFacade>()->GetSessionStatusSegments(savedLabel, savedPre, savedMid, savedPost, savedRuby);	// ★remembered on the model side
	if (savedLabel.IsEmpty() && savedPre.IsEmpty() && savedMid.IsEmpty() && savedPost.IsEmpty())
	{
		// (the member SetStatus was a plain forwarder and was removed; this calls directly)
		KCMSetStatus("Open the target and source documents (the active one becomes the Target), then choose Start from the panel menu.");
	}
	else
	{
		KCMSetStatusSegments(savedLabel, savedPre, savedMid, savedPost, savedRuby);
	}

	// The position readout between Prev and Next, and whether the buttons are enabled, have already
	// been rebuilt from the real state by UpdateInfoDisplay above (-> KCMApplyPanelInfo ->
	// KCMRefreshNavPosition). Whatever the workspace persisted from last time is overwritten there,
	// so nothing has to be restored here.

	// Re-apply the translucency when its toggle is ON: reopening the panel replaces the top-level
	// window it is applied to (the OWL.Dock) with a different one
	// ([[win32-window-alpha-transparency]]).
	// ★There are two panel targets (our own and InDesign’s Pages panel), so all of them are looked
	//   at here: the moment our panel is rebuilt is a good moment to re-apply the other one too.
	// ★When they are OFF the call is skipped here rather than inside. Apply does not reject an OFF
	//   target (it only rejects "docked, so no window"), so calling unconditionally would make
	//   someone who uses none of this pay for a window lookup (an SDK query when the cache is
	//   stale), an alpha write and a SW_SHOWNA for the shadow. The MouseEnter / MouseLeave / hook /
	//   visibility-observer entrances all reject OFF the same way.
	//   ★Per-target OFF is skipped by KCMApplyAllPanelTranslucency itself (fixed after one ON and
	//     one OFF target in the same floating group let the OFF one write 255 over the same window
	//     and cancel the ON one). ∴ the condition here means "**if both of these panels are OFF**
	//     there is no point calling".
	//   ⚠There are **three** translucency toggles, the third being the book comparison dialog, and
	//     this condition does not look at it. **Not looking is right** ＝ a dialog is not a panel, so
	//     rebuilding the panel’s widgets leaves its window untouched.
	//   ⚠**Do not put an OFF guard inside Apply*For***: restoring 255 and re-showing the shadow at
	//     the moment the menu switches it OFF is done by that call, which names its target
	//     (the toggle route in KCMActionComponent.cpp).
	// ★This is a safety net; the real following-along is the observer in KCMPanelAlpha.cpp
	//   (kPaletteVisibilityChangedMessage).
	//   ★Note: this AutoAttach runs every time the widgets are rebuilt, so it is not a place to
	//   write fixed defaults (it only reads and reflects the current KCMGetPanelTranslucent).
	//
	// ★At startup (KCMUIStartup::Startup) the panel manager may not be up yet and the subscription
	//   may have failed, so it is attempted here as well (the IsAttached guard keeps it single).
	KCMAttachPanelVisibilityObserver();
	// (The KCMResetPanelHover call that cleared the "pointer is over it" state was removed: the test
	//  became a Win32 measurement rather than a flag, so rebuilding the widgets leaves no state to
	//  clear.)
	if (KCMGetPanelTranslucent() || KCMGetPagesPanelTranslucent())
		KCMApplyAllPanelTranslucency();
}

void KCMPanelObserver::AutoDetach()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->DetachWidget(pcd, kKCMPrevChangeButtonWidgetID,   IBooleanControlData::kDefaultIID);
	this->DetachWidget(pcd, kKCMNextChangeButtonWidgetID,   IBooleanControlData::kDefaultIID);
	this->DetachWidget(pcd, kKCMIconOnWidgetID,             ITriStateControlData::kDefaultIID);
	this->DetachWidget(pcd, kKCMIconOffWidgetID,            ITriStateControlData::kDefaultIID);
	this->DetachWidget(pcd, kKCMToolButtonWidgetID,         ITriStateControlData::kDefaultIID);	// ★detached as the pair of AutoAttach
}

// Subscribe to, or unsubscribe from, one widget of this panel.
// ★**One function with a flag**, the shape this plug-in uses for the same job elsewhere
//   (KCMLayoutSyncAttachContext in KCMViewSync.cpp, KCMSetModelChangeObserverAttached in
//   KCMModelChangeObserver.cpp). ★The two named wrappers stay: the ten call sites in AutoAttach /
//   AutoDetach read better naming their direction than carrying a kTrue / kFalse.
void KCMPanelObserver::SetWidgetAttached(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid, bool16 attach)
{
	IControlView* cv = pcd->FindWidget(wid);
	if (cv == nil)
		return;
	InterfacePtr<ISubject> subject(cv, UseDefaultIID());
	if (subject == nil)
		return;
	if (attach)
		subject->AttachObserver(this, iid);
	else
		subject->DetachObserver(this, iid);
}

void KCMPanelObserver::AttachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid)
{
	this->SetWidgetAttached(pcd, wid, iid, kTrue);
}

void KCMPanelObserver::DetachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid)
{
	this->SetWidgetAttached(pcd, wid, iid, kFalse);
}

//----------------------------------------------------------------------------------------
// Update dispatch
//----------------------------------------------------------------------------------------

void KCMPanelObserver::Update(const ClassID& theChange, ISubject* theSubject, const PMIID& /*protocol*/, void* /*changedBy*/)
{
	InterfacePtr<IControlView> cv(theSubject, UseDefaultIID());
	if (cv == nil)
		return;

	const WidgetID wid = cv->GetWidgetID();

	// ★★The tool switch button is the one widget whose kFalseStateMessage is watched as well (user
	//   report: "pressing it in the toolbox keeps them in step, but pressing it on the panel never
	//   looks held down").
	//   The cause: this widget behaves as a push button and **clears its own state at the end of
	//   handling the click**. The order was ① press raises kTrueStateMessage → ② the case below
	//   switches the tool → ③ ITool::Select sets the state to kSelected → ④ **the widget returns it
	//   to kUnselected as it closes the click**. ④ comes last, so the pressed look disappears.
	//   Choosing from the toolbox runs neither ① nor ④ ＝ it stays, which matches what the user saw.
	//   ⇒ The kFalseStateMessage that follows ④ is used to repaint from **whether the tool really is
	//     active**.
	//   ★Because it reads the real state (KCMIsOwnToolActive), a tool that did not actually switch
	//     leaves the pressed look off ＝ the look and the truth cannot part company.
	//   (Watching both kTrue and kFalse on a toggle-like widget is standard in the product code too
	//    ＝ the Layers panel.)
	if (theChange == kFalseStateMessage && wid.Get() == kKCMToolButtonWidgetID)
	{
		KCMSetToolButtonSelected(KCMIsOwnToolActive());
		return;
	}

	if (theChange == kTrueStateMessage)
	{
		switch (wid.Get())
		{
			// Prev / Next: scroll the Target view to the previous or next page worth looking at (changed,
			// Added, or not compared).
			// (Start/Stop left the panel button for the flyout item kKCMPopupStartStopActionID.)
			case kKCMPrevChangeButtonWidgetID:  KCMGotoPrevChange(); break;
			case kKCMNextChangeButtonWidgetID:  KCMGotoNextChange(); break;
			// (The print toggle and the 25%/75% opacity moved to the flyout menu and are not handled here.)
			// Clicking the illustration -> open the distribution URL in the browser.
			case kKCMIconOnWidgetID:
			case kKCMIconOffWidgetID:
				KCMOpenAboutURL();
				break;
			// ★The tool switch button -> make this plug-in’s tool (the one in the toolbox) active, exactly
			//   as clicking it in the toolbox would. The work is in KCMTool.cpp
			//   (Utils<IToolBoxUtils>()->QueryTool -> SetActiveTool).
			case kKCMToolButtonWidgetID:
			{
				// ★Report the result in the status line (user’s request). SetActiveTool answers whether the
				//   tool really became active, so a refusal does not end in silence.
				const bool16 activated = KCMActivateOwnTool();

				// ★★The name shown is **the same as the tooltip’s** (user’s instruction), and it is so because
				//   both look up the same string table key ＝ the name lives in one place, and the toolbox tool
				//   name (KCMTool::Init’s SetName), the tooltip (KCMIconTip::GetTipText) and this line cannot
				//   disagree ([[one-question-one-place]]).
				//   PMString::Translate() resolves "key -> the real string for this locale"
				//   (PMString.h:692-696).
				PMString toolName(kKCMToolStringKey);
				toolName.Translate();

				PMString msg;
				msg.SetTranslatable(kFalse);	// ★do not let the finished sentence be treated as a key again
				if (activated)
				{
					msg.Append(toolName);
					msg.Append(" selected.");
				}
				else
				{
					msg.Append("Could not select ");
					msg.Append(toolName);
					msg.Append(".");
				}
				KCMSetStatus(msg);
				break;
			}
			default: break;
		}
	}
}

//----------------------------------------------------------------------------------------
// Display helpers
//----------------------------------------------------------------------------------------

// The IControlView of the ChangeMarker panel if it is showing (nil when it is hidden or cannot be
// reached).
// ★KCMRefreshPanel / KCMSetStatus / KCMSetNavPosition below each held the whole
//   "session -> app -> panelMgr -> GetVisiblePanel" idiom, so it was brought into one place. The
//   same construction as **KCMGetVisiblePagesPanel** (declared in KCMThumbnailRefresh.h) in this
//   plug-in ＝ filling in the hole on the side that had only been solved for the Pages panel.
// ★The nil guard for the session (teardown while the application quits) is absorbed here too: all
//   three can be reached during teardown.
//   ⚠★The GROUNDS were rewritten. The old text said "all three are called from the close
//     responder", which was true before the model/UI split and is not now ---- the close clean-up
//     (KCMHandleDocsClosed) is on the model side and cannot call these three directly. Today the
//     callers are KCMRefreshPanel from the notification receiver (KCMModelChangeObserver) and
//     from KCMPeekGesture, and KCMSetNavPosition from KCMChangeNav.
//   ★**The conclusion survived**: the panel’s Update does run during teardown (measured; it is
//     why KCMDetachPanelVisibilityObserver exists ＝ the comment in KCMDocPathFromDB above).
//     ∴ the nil guard is needed. **Grounds can lapse without the conclusion lapsing**
//     ([[verify-claims-in-comments]]).
// ★It stopped being static and was published (it now lives in **KCMUIShared.h**) when a fourth
//   user appeared in another file (KCMStorySection.cpp ＝ opening and closing the Story Edits
//   section, which needs the same panel to touch its dimensions). Copying it would split the
//   decision of "which panel is meant" across two places, so publishing was the answer.
IControlView* KCMGetVisibleOwnPanel()
{
	ISession* session = GetExecutionContextSession();	// can be nil during teardown
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (app == nil)
		return nil;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return nil;
	return panelMgr->GetVisiblePanel(kKCMPanelWidgetID);
}

// One widget of the showing panel (declared in KCMUIShared.h, with the reason and the callers
// that deliberately do not use it).
IControlView* KCMFindPanelWidget(const WidgetID& id)
{
	IControlView* panel = KCMGetVisibleOwnPanel();
	if (panel == nil)
		return nil;		// the panel is hidden (or teardown is under way): there is nothing to touch

	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	return pcd != nil ? pcd->FindWidget(id) : nil;
}

// Brings the panel’s ON/OFF display (the Target/Source names, the icon, the toggle label) into
// line with the current armed state (KCMIsArmed and friends). It is a free function taking pcd so
// that the member UpdateInfoDisplay (our own panel) and the external KCMRefreshPanel (the showing
// panel) can both use it.
static void KCMApplyPanelInfo(const InterfacePtr<IPanelControlData>& pcd)
{
	if (pcd == nil)
		return;

	// ★The facade is asked several times here, so it is taken once into an InterfacePtr.
	//   `Utils.h:74-80` states it: "if you want to use a utility interface in several places, get
	//   the interface once, save it in an InterfacePtr, and call it from there", which does the
	//   QueryInterface and the Release once instead of once per call. ⚠**The official text names no
	//   number**; "three or more" is a rule of thumb of ours ([[utils-boss-facade-access]]).
	//   KCMPanelState.cpp and KCMActionComponent.cpp in this plug-in already do the same, so this
	//   evened out a split.
	// ⚠A nil check is **deliberately not added**, to keep the behaviour identical (Utils<>()-> would
	//   fall over on nil in just the same way).
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	const bool16 started = compare->IsArmed() && (compare->GetArmedTargetDB() != nil);

	// The Target:/Source: labels are always shown; the names only while armed (English, as the rest
	// of the panel is).
	PMString target("Target:"); target.SetTranslatable(kFalse);
	if (started)
	{
		target.Append(" ");
		target.Append(KCMDocPathFromDB(compare->GetArmedTargetDB()));
	}
	PMString source("Source:"); source.SetTranslatable(kFalse);
	if (started && compare->GetArmedSourceDB() != nil)
	{
		source.Append(" ");
		source.Append(KCMDocPathFromDB(compare->GetArmedSourceDB()));
	}

	IControlView* tView = pcd->FindWidget(kKCMTargetTextWidgetID);
	if (tView != nil)
	{
		InterfacePtr<ITextControlData> tcd(tView, UseDefaultIID());
		if (tcd != nil) tcd->SetString(target);
	}
	IControlView* sView = pcd->FindWidget(kKCMSourceTextWidgetID);
	if (sView != nil)
	{
		InterfacePtr<ITextControlData> tcd(sView, UseDefaultIID());
		if (tcd != nil) tcd->SetString(source);
	}

	// The icon: ON while armed, OFF otherwise (two of them stacked, with the visibility switched).
	// ★ShowView only removes the appearance and does not disable hit testing, so the hidden one
	// still catches clicks and KCMOpenAboutURL fires twice (two browser tabs). Enable is switched
	// with the visibility so that the hidden one does not react.
	IControlView* onView  = pcd->FindWidget(kKCMIconOnWidgetID);
	IControlView* offView = pcd->FindWidget(kKCMIconOffWidgetID);
	if (onView  != nil) { onView->ShowView(started ? kTrue : kFalse);  onView->Enable(started ? kTrue : kFalse); }
	if (offView != nil) { offView->ShowView(started ? kFalse : kTrue); offView->Enable(started ? kFalse : kTrue); }

	// ★★The Story Edits list and its heading are rebuilt here as well: **both of them display the
	//   armed state**, so they belong with the Target/Source labels and the icon.
	// ⚠★★Putting this on the comparison side alone (KCMDoMarkChangesDoc / KCMDoClearMarks) **is
	//   bound to go out of step** ---- Start is "compare, then arm on success" and Stop is "clear the
	//   marks, then disarm", so in both of them the armed state at the moment the list is built is
	//   **the opposite of what it becomes**. Three symptoms seen in the running application had that
	//   one cause: no count in the heading; a "No edits" row left after a Stop; no "No edits" after
	//   starting with zero edits. What always runs after the armed state changes is this function,
	//   so this is where they are brought into line.
	//   ★The calls on the comparison side are kept: Refresh Page Comparison changes the count
	//   without changing the armed state, so that side needs its own.
	KCMStoryTreeRebuild();
	KCMUpdateStorySectionLabel();

	// Whether Prev/Next are enabled, and the position readout between them (k/N, "-", or empty), are
	// all decided in one place, KCMRefreshNavPosition (enabled only while comparing and with changed
	// pages; without them, disabled and "/"; before a Start, disabled and empty). Attach, Start/Stop
	// and a document closing or switching all pass through it, so it is right from the initial state
	// onward. How the values are made is in KCMChangeNav.cpp.
	KCMRefreshNavPosition();

	// ★Bring the pressed look of the tool switch button into line with **the real state**. The panel
	//   rebuilds its widgets every time it is shown, so writing a fixed default (not selected) here
	//   would drop the pressed look whenever the panel is reopened with the tool still active
	//   ([[panel-autoattach-read-real-state]]).
	//   ★"Is it active now" is answered in one place only, KCMTool.cpp.
	IControlView* toolView = pcd->FindWidget(kKCMToolButtonWidgetID);
	if (toolView != nil)
	{
		InterfacePtr<ITriStateControlData> tsd(toolView, UseDefaultIID());
		if (tsd != nil)
		{
			// third argument kFalse = raise no notification (the reason is at KCMSetToolButtonSelected below)
			tsd->SetState(KCMIsOwnToolActive() ? ITriStateControlData::kSelected
												 : ITriStateControlData::kUnselected, kTrue, kFalse);
		}
	}

	// (Start/Stop left the panel button for the dynamic label of the flyout item
	//  kKCMPopupStartStopActionID (UpdateActionStates), so no button label is set here any more.)
}

//========================================================================================
// KCMSetToolButtonSelected (declared in KCMUIShared.h)
//   Shows the panel’s tool switch button as pressed or not pressed. It looks sunken like a
//   toolbox tool slot because the `.fr` gives that widget kADBEIconSuiteButtonDrawWellType.
//
//   ★Callers (measured):
//       - KCMTool::Select   ... the tool became active
//       - KCMTool::Deselect ... the tool stood down
//       - **the Update in this file (kFalseStateMessage)** ... repainting after the push button
//         cleared its own state (the reason is in that comment)
//   ★However many they are, **the answer comes from one place**: each of them asks
//     KCMIsOwnToolActive() before passing it on, so there is no route by which the panel and the
//     toolbox can disagree ([[one-question-one-place]]). Whether the tool is chosen in the
//     toolbox, on the panel button, by shortcut or from a script, ITool::Select is always
//     called.
//========================================================================================
void KCMSetToolButtonSelected(bool16 selected)
{
	IControlView* cv = KCMFindPanelWidget(kKCMToolButtonWidgetID);
	if (cv == nil)
		return;		// the panel is hidden (or teardown is under way): there is nothing to touch

	InterfacePtr<ITriStateControlData> tsd(cv, UseDefaultIID());
	if (tsd == nil)
		return;

	// ★★The third argument, notifyOfChange, is kFalse (ITriStateControlData.h:52).
	//   ⚠Left kTrue, the state change raises kTrueStateMessage, this observer’s Update calls
	//     KCMActivateOwnTool back → SetActiveTool → ITool::Select → here again, and round it goes.
	//     This only **reflects** the real state, so no notification is wanted.
	tsd->SetState(selected ? ITriStateControlData::kSelected : ITriStateControlData::kUnselected, kTrue, kFalse);
	cv->ForceRedraw();		// the pressed look should be visible at once (do not wait for the next event loop)
}

void KCMPanelObserver::UpdateInfoDisplay()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	KCMApplyPanelInfo(pcd);
}

//========================================================================================
// KCMRefreshPanel (declared in KCMUIShared.h)
//   If a ChangeMarker panel is showing, brings its ON/OFF display into line with the current
//   armed state. Does nothing while the panel is hidden (AutoAttach reflects the real state when
//   it is next opened).
//   ⚠**Not called from the close responder.** That clean-up is on the model side and cannot link
//   a UI free function; the notification receiver (KCMModelChangeObserver) calls this instead,
//   as does KCMPeekGesture. (The same correction is written out above KCMGetVisibleOwnPanel.)
//========================================================================================
void KCMRefreshPanel()
{
	// ★the nil guard for the session (teardown) is inside KCMGetVisibleOwnPanel as well
	IControlView* panel = KCMGetVisibleOwnPanel();
	if (panel == nil)
		return;		// the panel is hidden (or teardown is under way): there is nothing to touch
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	KCMApplyPanelInfo(pcd);
}

//========================================================================================
// KCMSetStatus / KCMSetStatusSegments (declared in KCMUIShared.h)
//   Updates the panel’s status line. It is published as a free function so that a caller outside
//   this file can put a message up as well. The session state is remembered even while the panel
//   is hidden, and restored when it is shown again.
//   ⚠**Not called from the close responder either** ---- see KCMRefreshPanel above.
//
// ★★The area is self-drawn now (KCMStatusTextView.cpp). There are two entrances, but **only one
//   way in to the area itself**: finding the widget, writing to it and having it repainted all
//   live in one place below.
//========================================================================================
namespace
{

/* KCMWriteStatusToPanel
   Writes the four pieces into the message area. Does nothing while the panel is hidden (on the
   next show, AutoAttach restores from the remembered value).

   ⚠★★**Invalidate has to be called here.** With the stock static text,
     ITextControlData::SetString did it (its second argument, invalidate, defaults to kTrue, and
     its @param reads "specifies whether the control should be redrawn"). A self-drawn area is
     **written to as a plain data holder**, so nobody knows the screen has gone stale. This is
     where "I wrote it and nothing changed" comes from.
*/
void KCMWriteStatusToPanel(const PMString& label, const PMString& pre,
							 const PMString& mid, const PMString& post, const PMString& ruby,
							 bool16 forceRedrawNow)
{
	IControlView* panel = KCMGetVisibleOwnPanel();
	if (panel == nil)
		return;		// the panel is hidden (or teardown is under way): there is nothing to touch
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;
	IControlView* cv = pcd->FindWidget(kKCMStatusTextWidgetID);
	if (cv == nil)
		return;
	InterfacePtr<IKCMStatusTextData> data(cv, UseDefaultIID());
	if (data == nil)
		return;

	data->SetSegments(label, pre, mid, post, ruby);
	cv->Invalidate();

	// When a blocking stretch of work (a comparison loop, say) follows immediately, an Invalidate
	// does not reach the screen until the next event loop. This draws synchronously right away so
	// that a "busy" message is actually seen.
	if (forceRedrawNow)
		panel->ForceRedraw(nil, kTrue);
}

}	// anonymous namespace

void KCMSetStatus(const PMString& s, bool16 forceRedrawNow)
{
	// ★It is remembered on the model side (KCMModelNotify.cpp). Restoring after the panel is hidden
	//   and shown again, and the answer app.kcmStatus gives, both come from that one place.
	//   ⚠No notification is raised here ---- this function is also **on the receiving end** of one,
	//   so it would go round in a circle.
	Utils<IKCMCompareFacade>()->StoreSessionStatus(s);

	// ★An ordinary message is passed as **the middle piece**: it is drawn in one colour and looks
	//   exactly like the stock static text used to, which is why not one of the many call sites had
	//   to change.
	const PMString kNothing;
	KCMWriteStatusToPanel(kNothing, kNothing, s, kNothing, kNothing, forceRedrawNow);
}

// A message written out where it is used (declared in KCMUIShared.h, with the reason).
void KCMSetStatus(const char* s, bool16 forceRedrawNow)
{
	PMString msg(s);
	msg.SetTranslatable(kFalse);
	KCMSetStatus(msg, forceRedrawNow);
}

void KCMSetStatusSegments(const PMString& label, const PMString& pre,
							const PMString& mid, const PMString& post, const PMString& ruby)
{
	// ★Remembered in exactly the same one place as above. Joining the pieces into a single string is
	//   done on the model side, so app.kcmStatus answers "heading + newline + body" ＝ what this area
	//   shows.
	Utils<IKCMCompareFacade>()->StoreSessionStatusSegments(label, pre, mid, post, ruby);

	// ★forceRedrawNow is not passed: this route is a row click, with no blocking work behind it
	KCMWriteStatusToPanel(label, pre, mid, post, ruby, kFalse);
}

// (★KCMGetSessionStatus and KCMClearSessionStatus moved to **KCMModelNotify.cpp, on the model
//  side**, so that the place holding the string and the place answering for it (app.kcmStatus,
//  whose ScriptProvider is model-side as well) are the same. This file drives the display only.
//  ⚠Since the halves became two .pln, the UI **cannot call those two directly** ＝ it goes
//  through GetSessionStatus / ClearSessionStatus on IKCMCompareFacade.)

//========================================================================================
// KCMSetNavPosition (declared in KCMUIShared.h)
//   Updates the position readout between Prev and Next (kKCMNavPosTextWidgetID, "3/12" for
//   instance) together with whether the two buttons are enabled. Does nothing while the panel is
//   hidden (KCMRefreshNavPosition reflects the real state when it is shown again). Deciding the
//   values is gathered in the caller, KCMRefreshNavPosition.
//========================================================================================
void KCMSetNavPosition(const PMString& posText, bool16 navButtonsEnabled)
{
	IControlView* panel = KCMGetVisibleOwnPanel();
	if (panel == nil)
		return;		// the panel is hidden (or teardown is under way): there is nothing to touch
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;

	// The readout. ★The invalidate is done by SetString itself (the second argument of
	//   ITextControlData::SetString defaults to kTrue, and its @param reads "specifies whether the
	//   control should be redrawn"). But an invalidate alone does not reach the screen until the
	//   next event loop, so ForceRedraw draws it at once, which is what makes the change at a Start
	//   and the new value on Next/Prev appear immediately (IControlView.h:281-286, "Redraws the
	//   invalid region directly"; user report: "1/5 does not update immediately"). A duplicate
	//   Invalidate() that stood here was removed.
	IControlView* cv = pcd->FindWidget(kKCMNavPosTextWidgetID);
	if (cv != nil)
	{
		InterfacePtr<ITextControlData> tcd(cv, UseDefaultIID());
		if (tcd != nil)
		{
			tcd->SetString(posText);
			cv->ForceRedraw();
		}
	}

	// Whether Prev/Next are enabled (with no changed pages they cannot be pressed -- user’s
	// instruction).
	IControlView* prevView = pcd->FindWidget(kKCMPrevChangeButtonWidgetID);
	IControlView* nextView = pcd->FindWidget(kKCMNextChangeButtonWidgetID);
	if (prevView != nil) prevView->Enable(navButtonsEnabled);
	if (nextView != nil) nextView->Enable(navButtonsEnabled);
}

// End, KCMPanelObserver.cpp.
