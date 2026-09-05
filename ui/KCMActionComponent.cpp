//========================================================================================
//
//  KCMActionComponent.cpp
//
//  The hub of every menu action in the plug-in. About, every item of the panel flyout
//  (Start/Stop, the display toggles, Save/Load, Find Overset and the rest) and the Pages panel
//  context menu (Check / Register / Refresh) have their DoAction and their UpdateActionStates
//  (dynamic labels, conditional enabling) here. The skeleton is modelled on the BasicPanel sample
//  (BscPnlActionComponent.cpp), from when this was two About items and nothing else.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "CActionComponent.h"
#include "CAlert.h"
#include "IActionStateList.h"		// UpdateActionStates (the check marks). Not derived from IPMUnknown
#include "PMString.h"

// ★Hide Unchanged Spreads (kHideSpreadCmdBoss) and applying an overset scan moved to
//   KCMHideUnchanged.cpp / KCMOversetApply.cpp with the model/UI split. The SDK includes they
//   needed (CmdUtils / ICommand / IBoolData / UIDList / ISpread / ISpreadList / SpreadID /
//   ISession / IApplication / IDocumentList / IDataBase, plus <map> and <set>) went with them.
#include <vector>					// DoFindOversetToggle (the page list handed to the thumbnail refresh)

// (The Split Target (90/10) feature was removed, and its includes with it. How it worked is kept
//  in docs/ai-notes/kescm-split-target-mechanism.md and in the git history at 69c4b07.)

// Project includes:
#include "KCMUIID.h"
#include "KCMLoc.h"		// the run-time Japanese switch (only How to Use; the Hide Unchanged
							// confirmation went to KCMHideUnchanged.cpp with its feature)
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// ★the one way the UI asks the comparison engine for anything:
									//  Start/Stop, the armed state, running a comparison, the print marks,
									//  overset and Hide Unchanged all go through it. Modelled on
									//  customconditionaltextui, which uses Utils<ICusCondTxtFacade>() alone
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "IKCMMarkData.h"			// reading the marks and the overset (the display toggles are read and written through IKCMCompareFacade)
#include "IKCMPageFlagsFacade.h"	// the Register (added/removed pages) and Check toggles, their menu
									// states, and Save/Load
#include "KCMThumbnailRefresh.h"	// KCMTryRefreshPagesPanelThumbnails (turn the frame on a Source thumbnail on and off at once)
#include "KCMViewSync.h"			// KCMGetLayoutSync / Set / KCMAlignOtherViewsToActiveNow
#include "KCMScrollMap.h"		// KCMScrollMapAttach / DetachAll / InvalidateAll (the map toggle and Find Overset)
#include "KCMPanelState.h"		// KCMSavePanelState (the "Save Panel Settings" flyout item)
#include "KCMPanelTitle.h"		// KCMPanelTitle::Update (put Pixel / Story on the tab)
#include "IKCMBookFacade.h"		// ResolveBookPair (deciding whether "Compare Books" may be enabled)
#include "KCMBookPanelLookup.h"	// KCMGetPanelBookFile (observing the front tab; a UI-side job)
#include "KCMBookRun.h"		// KCMRunBookComparison (the "Compare Books" flyout item: confirm, compare, show)
#include "KCMBookOpen.h"			// KCMBookMenuRow / CanStart / StartComparisonForRow (the "Start Change Marker" row item)
#include "KCMChangeNav.h"			// KCMRefreshNavPosition (the overset toggle changes what Prev/Next walks)
#include "KCMStoryRefresh.h"		// KCMStoryRowCanRefresh / KCMStoryRefreshMenuRow (the "Refresh Story Comparison" row item)
#include "KCMPanelAlpha.h"		// KCMGetPanelTranslucent / Set / Apply (the "Translucent Panel" flyout item)
#include "KCMStoryPressMarks.h"	// KCMStoryMarksRefresh (rebuild the always-on marks of Story mode)
// (★`IActiveContext.h` / `IDocument.h` / `PersistUtils.h` were removed: **none of them was ever
//  used**. DoAction and UpdateActionStates are handed an `IActiveContext*`, but the parameter
//  name itself is commented out, and resolving "active document -> db" went to the model side
//  (KCMActiveDoc / GetOversetScanTargetDB) with the split. ⚠The older comment on the remaining
//  include described a facility that is not used here ("GetContextDocument, resolving the active
//  document"), which read as if the resolving happened in this file.)

// ★NOTE: source/public/includes/URLUtils.h declares
// "namespace URLUtils { PUBLIC_DECL void GoToURL(...); }", but **the header and the binary do not
// agree** (the real exported name in Public.lib is a different one). Inspecting the raw symbols
// of build/win/objrx64/Public.lib, the name that can actually be linked is
// "?GoToURL@GoToURLUtils@@YAXAEBVPMString@@F@Z" = void GoToURLUtils::GoToURL(const PMString&,
// bool16); there is no URLUtils-namespace version (confirmed by the link error). The header is
// not to be trusted here, so the declaration below is written to match the binary.
namespace GoToURLUtils
{
	PUBLIC_DECL void GoToURL(const PMString& goToURL, bool16 isAGoURL);
}

// (The five pieces of "Hide Unchanged Spreads" state -- the toggle flag, the IDataBase* of each
//  side and the record of the spreads hidden -- moved to KCMHideUnchanged.cpp together with the
//  toggle that writes them, so the state cannot end up split across the two halves. All that is
//  read from here is the check mark, through the facade's GetHideUnchangedOn().)

// (KCMOversetScanTargetDB, which answers with the document an overset scan should run on, is
//  likewise asked through the facade's GetOversetScanTargetDB(). It used to be a static further
//  down this file, with a forward declaration here.)

/** The IActionComponent implementation behind the ChangeMarker plug-in's menu items.
*/
class KCMActionComponent : public CActionComponent
{
public:
	KCMActionComponent(IPMUnknown* boss) : CActionComponent(boss) {}

	/** Execute the requested menu action. */
	void DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

	/** Brings the check marks of the check-style toggles (kCustomEnabling) into line with the
	    current state. */
	virtual void UpdateActionStates(IActiveContext* ac, IActionStateList* listToUpdate, GSysPoint mousePoint = kInvalidMousePoint, IPMUnknown* widget = nil);

private:
	void DoAbout();
	void DoUsage();
	void DoFindOversetToggle();		// flyout "Find Overset": scan the active document and show or clear the crosses (a toggle)
	void DoRefreshOverset();		// flyout "Refresh Overset": only while ON = rescan the active document
};

/* Binds the C++ implementation class onto its ImplementationID. */
CREATE_PMINTERFACE(KCMActionComponent, kKCMActionComponentImpl)

/* KCMSayToggle - report a toggle on the panel's status line as "<what>: on." / "<what>: off.".

   ⚠The message is finished text, so it is marked untranslatable here -- the step each site used to
     have to remember for itself.
*/
static void KCMSayToggle(const char* what, bool16 on)
{
	PMString msg(what);
	msg.Append(on ? ": on." : ": off.");
	msg.SetTranslatable(kFalse);
	KCMSetStatus(msg);
}

/* KCMSayTranslucency - report one of the three translucency toggles.

   ⚠They say **three** things, not two: off / on / on-but-nothing-visible-yet. **Do not fold them
     into KCMSayToggle above** -- losing the third turns "ticked and nothing happened" back into a
     silence, which is the report that made it necessary. Only that third wording differs between
     the three toggles, which is what `ineffective` carries.
*/
static void KCMSayTranslucency(const char* what, bool16 on, bool16 applied, const char* ineffective)
{
	PMString msg(what);
	if (!on)
		msg.Append(": off.");
	else if (applied)
		msg.Append(": on.");
	else
	{
		msg.Append(": on - ");
		msg.Append(ineffective);
	}
	msg.SetTranslatable(kFalse);
	KCMSetStatus(msg);
}

/* KCMApplyCompareMode - switches the compare mode and, while Started, recompares on the spot.

   ★**It is a function so that the two entrances take the same steps.** Choosing Pixel or Story
   makes the same things happen and only the value differs (the same shape as Marks opacity
   25%/75% sharing one function of the facade).

   ⚠**Choosing the mode that is already set does nothing.** Someone already looking at Story who
   picks Story again would otherwise wait through a comparison for the same screen.
*/
static void KCMApplyCompareMode(KCMCompareMode mode)
{
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare == nil || compare->GetCompareMode() == mode)
		return;

	compare->SetCompareMode(mode);

	PMString msg(mode == kKCMModeStory ? "Compare mode: story changes." : "Compare mode: pixel changes.");
	msg.SetTranslatable(kFalse);

	// ★While Started, everything is compared again in the new mode. When nothing is being compared
	//   this only changes the setting ＝ it takes effect at the next Start.
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	IDataBase* const markedDB    = (marks != nil) ? marks->GetMarkedTargetDB() : nil;
	IDataBase* const markedSrcDB = (marks != nil) ? marks->GetMarkedSourceDB() : nil;
	if (markedDB != nil && markedSrcDB != nil)
	{
		PMString report;
		// ★allowIncremental is not passed ＝ every page is compared again. An incremental comparison is
		//   the tool for "the same method as last time, redo the pages whose pairing changed", and here
		//   the method itself is what changed, so not one of the previous results can be reused.
		// ★A cancellation is unwound all the way to Stop, for the same reason as Ignore Page Number
		//   Marker (the marks are already discarded, so leaving only the armed state would mean
		//   "Started with not one frame drawn").
		if (compare->MarkChanges(markedDB, markedSrcDB, report) == kSuccess)
			msg.Append(" (recompared)");
		else
		{
			compare->ToggleStartStop();
			msg.Append(" (cancelled - stopped)");
		}
	}

	// ★★Put the current mode on the tab (user’s instruction: "like the document and book in KBS").
	//   This is the one place the mode changes, so it is the one place that has to write it.
	//   (When the panel is reopened, KCMPanelObserver::AutoAttach calls the same function.)
	KCMPanelTitle::Update();

	KCMSetStatus(msg);
}

/* DoAction */
void KCMActionComponent::DoAction(IActiveContext* /*ac*/, ActionID actionID, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	switch (actionID.Get())
	{
		case kKCMAboutActionID:
		case kKCMPopupAboutThisActionID:
			this->DoAbout();
			break;

		// "Start / Stop" at the head of the flyout: the toggle that starts and clears a comparison (it
		// used to be a panel button). The work is a free function in KCMComparisonRun.cpp (it looks at
		// the armed state, starts or clears, then refreshes the panel).
		case kKCMPopupStartStopActionID:
			Utils<IKCMCompareFacade>()->ToggleStartStop();
			break;

		// ★"Set as Target" / "Set as Source" directly below Start: the active document becomes the
		//   comparison's Target / Source, so that which two documents are compared is **stated**
		//   rather than inferred from what happens to be in front at the moment Start is pressed.
		//   The choice outlives a Stop; what ends it is that document closing.
		//   ★**Which document is "active" is not decided here.** The facade's setter asks
		//     KCMActiveDoc on the model side ＝ the one place this plug-in answers that question
		//     ([[document-activation-is-presentation]]: GetNthDoc(0) and GetFrontDocument each mean
		//     something else, and the comparison must not be told a different answer from the menu).
		//   ★**The panel refresh and the status line are done here, not by the setter** ＝ the same
		//     division as KCMApplyCompareMode above: the facade changes the setting, the UI decides
		//     what the UI shows. The name of the document lands on the panel's Target:/Source: line
		//     through KCMRefreshPanel, which is why the status line does not repeat it.
		//   ⚠**Say nothing when nothing was set.** With no active document the setter refuses, and
		//     an unconditional "Target set." would be a lie -- the case is real even though the item
		//     is greyed without a document, because a menu can stand open while one closes.
		case kKCMPopupSetTargetActionID:
			if (Utils<IKCMCompareFacade>()->SetChosenTargetToActive())
			{
				KCMRefreshPanel();
				KCMSetStatus("Target set.");
			}
			break;

		case kKCMPopupSetSourceActionID:
			if (Utils<IKCMCompareFacade>()->SetChosenSourceToActive())
			{
				KCMRefreshPanel();
				KCMSetStatus("Source set.");
			}
			break;

		// Flyout "Print comparison marks": the print-marks toggle (it used to be a checkbox on the
		// panel).
		case kKCMPopupPrintMarksActionID:
			Utils<IKCMCompareFacade>()->TogglePrintMarks();
			// ★★**The marks of Story mode take this toggle as an input too**, so a rebuild is asked for
			//   here as well. While Print is ON they are on screen at all times (the same WYSIWYG as the
			//   Pixel ring), so without the request **switching the toggle would leave the Story marks
			//   unchanged**.
			//   ⚠This case was the last one not calling Refresh; it now matches the four below that do
			//     (the two opacities and Show Src/Tgt).
			KCMStoryMarksRefresh();
			// ★User’s instruction: say "these will appear in print and in exported PDFs" **only when it is
			//   switched ON**. Switching it OFF says nothing (it only puts things back; nothing is added to
			//   an output).
			//   ⚠**The alert comes after the Refresh**: a ModalAlert stops the screen, so asking for the
			//     repaint first means the marks are already right behind it.
			//   ⚠**Read the value after the toggle** (rather than computing `!GetPrintMarks()` here) ＝ do
			//     not put the same judgement in two places. If the model side does not flip it for some
			//     reason, no alert appearing is the right outcome.
			if (Utils<IKCMCompareFacade>()->GetPrintMarks())
			{
				CAlert::ModalAlert
				(
					// pass the string key (CAlert translates it), the same shape as About. English in every locale
					PMString(kKCMPrintMarksOnKey),
					kOKString,					// OK button
					kNullString,				// No second button
					kNullString,				// No third button
					1,							// Set OK button to default
					CAlert::eInformationIcon	// Information icon
				);
			}
			break;

		// Flyout "Marks opacity 25% / 75%" (radio-like): set the opacity to whichever was chosen. The
		// work is a free function on the model side (it keeps the print flag and changes only the
		// opacity).
		// ⚠★★**A Story colour ground carries the opacity it was created with baked in.** What the
		//   model’s SetMarkOpacity25 repaints is the Pixel ring, which re-reads the current value on
		//   every draw; the adornment keeps whatever value was put on it.
		//   ⇒ **Ask for a rebuild whenever the setting changes.** ★The model cannot ask: it does not
		//     know about a UI plug-in’s adornment (the dependency runs one way, UI -> model). ∴ the
		//     call belongs here.
		case kKCMPopupOpacity25ActionID:
			Utils<IKCMCompareFacade>()->SetMarkOpacity25(kTrue);
			KCMStoryMarksRefresh();
			break;
		case kKCMPopupOpacity75ActionID:
			Utils<IKCMCompareFacade>()->SetMarkOpacity25(kFalse);
			KCMStoryMarksRefresh();
			break;

		// ★"Mark colour". ⚠**Unlike the opacity above, no rebuild is asked for here.** The opacity is
		//   **carried by the mark as it was installed**, so changing it needs a rebuild; the colour is
		//   **re-read by the Story side’s Draw on every paint** (SelectedMarkColor()), so the model’s
		//   own repaint (KCMDoSetMarkColor) is enough to bring the new colour up.
		//   ★Two settings that are both "changed" need different clean-up depending on where the value
		//     is held ---- the Pixel ring image is a cache, so the model side drops that cache
		//     (KCMCore.cpp).
		case kKCMPopupColorRedActionID:
			Utils<IKCMCompareFacade>()->SetMarkColor(kFalse);
			break;
		case kKCMPopupColorCyanActionID:
			Utils<IKCMCompareFacade>()->SetMarkColor(kTrue);
			break;

		// (kKCMPopupAboutScriptActionID and DoAboutScript went with the "About Scripting" item.)

		case kKCMPopupUsageActionID:
			this->DoUsage();
			break;

		// The "Always Show Marks on Source" toggle: flip the flag and repaint the Source document.
		// In Pixel mode the decision and the drawing are the Source branch of
		// KCMDrawEventHandler::DrawSpreadMarks (⚠**not HandleDrawEvent** -- that entry point went with
		// the move to the adornment). While ON the marks show at all times, in OPP as well, and in
		// print; the opacity follows the panel’s 25%/75% choice.
		// ★Default OFF, and Start does not touch it (the setting is saved with the panel settings and
		//   restored at startup, so a Start that overwrote it would erase the saved choice).
		// ⚠★★**It drives two mechanisms, as the Target one does**: the Pixel frame is read straight
		//   from sSrcMarksOn by the drawing side, while the Story colour ground is a different
		//   mechanism (the global text adornment) and has to be asked for from here. ⇒ **Without that
		//   request, Story mode neither shows them when switched ON nor clears them when switched OFF**
		//   (the internal state does not change, so the InvalidateDB below just paints the same picture
		//   again).
		case kKCMPopupShowSrcMarksActionID:
		{
			InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
			const bool16 srcMarksOn = !compare->GetShowSourceMarks();
			compare->SetShowSourceMarks(srcMarksOn);
			// ⚠★★★**The press flag is not touched here** (removed after an independent review said so).
			//   When it was inherited from the removed Hold toggle, sSrcMarksTempHidden meant "hidden
			//   right now", so clearing it while flipping the toggle was right. **The rename on the same
			//   day changed it to "the tool’s left button is physically down"** -- and a menu action has no
			//   business claiming that it is not.
			//   ★The real consequence: assign a shortcut, press it **while the button is down**, and on
			//     release KCMTrackerRevealEnd sees GetSrcMarksPressed()==kFalse, skips the InvalidateDB and
			//     leaves the pressed frame on the Source window.
			//   ★KCMTrackerRevealBegin had already been corrected to "do not look at the toggle" ＝ this was
			//     the other half of that change, left undone ([[one-question-one-place]]).
			KCMStoryMarksRefresh();		// the Story colour ground (does nothing in Pixel mode)
			IDataBase* const srcDB = Utils<IKCMMarkData>()->GetMarkedSourceDB();
			Utils<IKCMCompareFacade>()->InvalidateDB(srcDB);
			// ★The Pages panel’s Source thumbnails are refreshed at once as well, not just the layout
			//   view. A Source frame depends on wantSrcMarks (= sSrcMarksOn) and is not forced on a
			//   thumbnail (isThumb), so without rebuilding them a frame stays after switching OFF and
			//   never appears after switching ON. The pages concerned are the Source’s changed /
			//   overflowing / registered set (what KCMCollectChangedPageUIDs answers with), which is
			//   exactly the set that can carry a frame.
			KCMTryRefreshPagesPanelThumbnails(srcDB);
			KCMSayToggle("Source marks", srcMarksOn);
			break;
		}

		// The "Always Show Marks on Target" toggle: flip the flag and repaint the Target document.
		// ★It pairs with the Source one: while ON the marks stay up without holding the tool button
		//   (user’s request, "have the marks show without pressing the tool button").
		// ★Default OFF, and Start does not touch it (the same reason as the Source one ＝ the setting is
		//   saved with the panel settings and restored at startup).
		// ⚠★★**It drives two mechanisms**: the Pixel comparison ring is read straight from sTgtMarksOn
		//   by the drawing side (alwaysScreen in KCMDrawEventHandler), while the Story colour ground is
		//   a different mechanism (the global text adornment) and is asked for from here. That is why
		//   one toggle moves both modes.
		// ⚠The Pages panel thumbnails are left alone: a thumbnail always draws the marks (isThumb), so
		//   this toggle changes nothing there (the Source one rebuilds them because ITS frame depends
		//   on wantSrcMarks).
		case kKCMPopupShowTgtMarksActionID:
		{
			InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
			const bool16 tgtMarksOn = !compare->GetShowTargetMarks();
			compare->SetShowTargetMarks(tgtMarksOn);
			// ⚠★★These two lines are the clean-up inherited from the removed "Hold to Hide Marks":
			//   ① clear the temporary hide of a press (there is no way to flip the toggle while holding
			//     the button, but a state left standing before release would read as "ON and yet nothing
			//     shows");
			//   ② **apply the base screen opacity at once** ＝ KCMBaseScreenOpacity returns 25%/75% only
			//     while printing is ON or the frames are permanently shown. ⇒ Without updating it here,
			//     the frame right after switching ON is drawn at 1.0 (opaque).
			compare->SetMarksTempHidden(kFalse);
			compare->SetMarkScreenOpacity(compare->GetBaseScreenOpacity());
			KCMStoryMarksRefresh();		// the Story colour ground (does nothing in Pixel mode)
			compare->InvalidateDB(compare->GetArmedTargetDB());
			KCMSayToggle("Target marks", tgtMarksOn);
			break;
		}

		// The "Hide Unchanged Spreads" toggle: OFF -> ON puts up a confirmation and then hides the
		// spreads with no change; ON -> OFF shows again exactly what it hid. The work is a free
		// function in KCMHideUnchanged.cpp (hiding and restoring are the model side’s job).
		case kKCMPopupHideUnchangedActionID:
			Utils<IKCMCompareFacade>()->HideUnchangedToggle();
			break;

		// Flyout "Find Overset" toggle: scan the active document and put a cross on every page with
		// overset text; OFF clears them.
		case kKCMPopupFindOversetActionID:
			this->DoFindOversetToggle();
			break;

		// Flyout "Refresh Comparison", directly under Start: compare the same two documents again,
		// in whichever mode is current ＝ what the reader wants after editing one of them. It was
		// Stop-then-Start before this existed. ★The model runs the START procedure with the armed
		// pair, so a refresh and a start cannot come to mean different things (KCMComparisonRun.h).
		case kKCMPopupRefreshCompareActionID:
			Utils<IKCMCompareFacade>()->RefreshComparison();
			break;

		// Flyout "Refresh Overset": live only while Find Overset is ON ＝ rescan the active document
		// and put them up again.
		case kKCMPopupRefreshOversetActionID:
			this->DoRefreshOverset();
			break;

		// The "Show Original Page Numbers" toggle: flip the flag and repaint, nothing more. The badge’s
		// visibility and drawing are in KCMDrawEventHandler::DrawSpreadMarks (⚠**not HandleDrawEvent**
		// -- that entry point went with the move to the adornment); it is shown under the same
		// conditions as the ring, that is with print marks ON or while the tool’s left button is held.
		// The repaint covers the Target and the Source, the two most likely to be involved in hiding
		// (any other document catches up on its next natural repaint).
		case kKCMPopupShowOldNumsActionID:
		{
			InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
			const bool16 showOldNums = !compare->GetShowOldPageNumbers();
			compare->SetShowOldPageNumbers(showOldNums);
			IDataBase* const markedDB = Utils<IKCMMarkData>()->GetMarkedTargetDB();
			Utils<IKCMCompareFacade>()->InvalidateDB(markedDB);
			if (compare->GetArmedSourceDB() != markedDB)
				Utils<IKCMCompareFacade>()->InvalidateDB(compare->GetArmedSourceDB());
			KCMSayToggle("Show original page numbers", showOldNums);
			break;
		}

		// The "Sync Layout Views" toggle: layout view syncing on or off (default ON). The work is
		// KCMSetLayoutSync (attach or detach the subscription; on ON, line them up once immediately).
		// It fires in two situations (the guard is in KCMSyncOtherDocViewportsTo):
		//   (A) while Started: between Target and Source only, with the added/removed correction;
		//   (B) while stopped with the KCM tool active: every other document follows the active one,
		//       with no correction.
		// With the toggle ON but neither situation met (stopped and the tool not active), the
		// subscription is made and the syncing is a no-op.
		case kKCMPopupSyncViewsActionID:
		{
			KCMSetLayoutSync(!KCMGetLayoutSync());
			KCMSayToggle("Sync layout views", KCMGetLayoutSync());
			break;
		}

		// "Align Other Views to Active" (a plain command, shortcut-assignable): set every other
		// document’s layout views to the active (frontmost) view’s position and zoom, once. It is
		// independent of the Sync Layout Views toggle (it works with that OFF). While Started, the
		// page Add/Remove correction is applied. The work is KCMAlignOtherViewsToActiveNow in
		// ui/KCMViewSync.cpp (the same syncing engine as the first line-up when the toggle goes ON).
		// ⚠Its home was corrected once: **it left KCMPeek.cpp with the split** while the comment still
		//   said "the work is in KCMPeek.cpp" (KCMUIID.h carried the same error ＝ two siblings).
		case kKCMPopupAlignViewsActionID:
		{
			const bool16 ok = KCMAlignOtherViewsToActiveNow();
			// kFalse means one of three things: (a) there is no frontmost layout view; (b) Started, with
			// the frontmost being a third document that is neither Target nor Source (the engine does not
			// sync it); ★(c) there is no other window to line up (only one document is open, the other
			// closed, or Target and Source are the same document). None of them lined anything up, so no
			// success message is shown.
			// ⚠(c) **did not reach the caller and was reported as success** until it was fixed. The wording
			//   was "no view to align **from**", which meant "no example to copy", so it was changed to
			//   cover all three.
			PMString msg(ok ? "Aligned other views to the active view."
			                : "Align: no other view to align (while Started, use the Target or Source view).");
			msg.SetTranslatable(kFalse);
			KCMSetStatus(msg);
			break;
		}

		// The "Show Scrollbar Map" toggle: whether a strip mapping the changed positions is shown beside
		// a document window’s vertical scrollbar (default ON). Switching ON attaches it at once to what
		// is being compared; switching OFF detaches it from every window.
		// Switching ON while nothing is armed makes the attach a no-op ＝ it appears naturally at the
		// next Start (the flag stays ON).
		case kKCMPopupScrollMapActionID:
		{
			const bool16 on = !KCMGetScrollMapEnabled();
			KCMSetScrollMapEnabled(on);
			if (on)
			{
				InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
				if (marks->GetMarkedTargetDB() != nil) KCMScrollMapAttach(marks->GetMarkedTargetDB());
				if (marks->GetMarkedSourceDB() != nil) KCMScrollMapAttach(marks->GetMarkedSourceDB());
				// with Find Overset on by itself, bring the map back to its scanned document’s window too
				if (marks->GetOversetOn() && marks->GetOversetDB() != nil)
					KCMScrollMapAttach(marks->GetOversetDB());
				KCMScrollMapInvalidateAll();
			}
			else
				KCMScrollMapDetachAll();	// take any existing strip out of every window
			KCMSayToggle("Scrollbar map", on);
			break;
		}

		// (★The "Show HUD" toggle is gone: the on-press HUD **always shows**, so there is no menu item
		//  for choosing whether it does. ⚠**The HUD itself is alive**: the sprite version was removed
		//  and it was rebuilt on the Draw Event route the next day (KCMTrackerHud.cpp, which says in the
		//  top-left whether the window pressed is the Target or the Source).)

		// The "Translucent Panel" toggle: whether this panel itself is translucent (the alpha is
		// kKCMPanelAlphaValue). ★Windows only, default OFF. It has effect in two states: floating, and
		// expanded as a drawer from an icon. Expanded inside a dock it can be ticked but nothing looks
		// different ＝ only the flag is set, and it takes effect on returning to either of those states
		// (KCMPanelObserver.cpp follows that by subscribing to kPaletteVisibilityChangedMessage). The
		// work is in KCMPanelAlpha.cpp.
		case kKCMPopupTranslucentPanelActionID:
		{
			const bool16 on = !KCMGetPanelTranslucent();
			KCMSetPanelTranslucent(on);

			// The status wording differs by whether it actually reached a window: pressing it while
			// expanded inside a dock changes nothing on screen, so the reason is put into words.
			const bool16 applied = KCMApplyPanelTranslucency();

			// ★★Switching OFF writes alpha 255 and re-shows the shadow on **that target’s current
			//   top-level window**, and when both panels are in **the same floating group** that window is
			//   shared, so the translucency of the other one -- still ON -- goes with it. Re-applying the
			//   targets that are ON takes it back (this is not called when switching ON).
			//   ⚠Found in a review: it was the piece left behind when "skip the ones that are OFF" went
			//     into KCMApplyAllPanelTranslucency, this being the restore route that **names its
			//     target**.
			if (!on)
				KCMApplyAllPanelTranslucency();

			KCMSayTranslucency("Translucent panel", on, applied, "has no effect while the panel is docked.");
			break;
		}

		// The "Translucent Pages Panel" toggle: whether **InDesign’s own Pages panel** is translucent.
		// Same machinery and same limits as Translucent Panel above; only the target differs.
		// ★The window is found from a WidgetID (kPagesPanelWidgetID, a number). A window title changes
		//   with the UI language ("Pages" / 「ページ」), so matching on the title could never reach a
		//   built-in panel. The work is in KCMPanelAlpha.cpp.
		case kKCMPopupTranslucentPagesActionID:
		{
			const bool16 on = !KCMGetPagesPanelTranslucent();
			KCMSetPagesPanelTranslucent(on);

			// The status wording differs by whether it actually reached a window: pressing it while
			// expanded inside a dock changes nothing on screen, so the reason is put into words.
			// ★The Pages panel is docked by default, so "no effect" is met more often here.
			const bool16 applied = KCMApplyPagesPanelTranslucency();

			// ★For the same reason as Translucent Panel above, the targets that are ON are re-applied only
			//   when switching OFF (in one floating group, this 255 restore takes the other one with it).
			if (!on)
				KCMApplyAllPanelTranslucency();

			KCMSayTranslucency("Translucent Pages panel", on, applied, "has no effect while the Pages panel is docked or closed.");
			break;
		}

		// (The "Translucent Toolbox" toggle was added and withdrawn the same day on the user’s call ＝
		//  changing how InDesign’s own toolbox looks is not something KCM carries. ActionID +38 stays
		//  vacant and is not reused, because .indk stores a shortcut by the numeric ActionID.)

		// The "Translucent Book Dialog" toggle: whether **our own book comparison dialog** is
		// translucent (user’s request, "let the dialog be translucent too"). The same implementation as
		// the two above (KCMPanelAlpha.cpp) with a different target.
		// ★Two things differ from those two:
		//   ① a dialog is **always floating**, so there is no "ticked but ineffective" state ⇒ nothing
		//     has to be worded as "docked, so it does nothing". The only distinction is whether the
		//     dialog is open right now.
		//   ② **no re-apply is needed when switching OFF.** The two above call
		//     KCMApplyAllPanelTranslucency because panels in one floating group make the 255 restore
		//     take the other one with it. A dialog has a window to itself, so that sharing cannot
		//     happen.
		case kKCMPopupTranslucentBookDialogActionID:
		{
			const bool16 on = !KCMGetBookDialogTranslucent();
			KCMSetBookDialogTranslucent(on);

			const bool16 applied = KCMApplyBookDialogTranslucency();

			KCMSayTranslucency("Translucent book dialog", on, applied, "takes effect the next time the dialog is open.");
			break;
		}

		// (★"Hold to Hide Marks" (+19) was removed on the user’s decision: "keep them visible" had
		//  become an exact duplicate of "Always Show Marks on Target". What was peculiar to it, hiding
		//  while the button is held, became **the standard behaviour whenever either toggle is ON** ＝
		//  one rule: while the button is held, the state is inverted.
		//  ⚠**The two pieces of clean-up this case carried moved into those two toggles**: clearing the
		//    temporary hide, and applying the base screen opacity at once (drop the second and "switched
		//    ON but the frame comes up opaque").
		//  ActionID +19 stays vacant and is not reused.)

		// The "Ignore Page Number Marker" toggle: whether frames containing an automatic page number
		// marker are left out of the comparison (the CMYK pixel difference). It flips the flag and, if a
		// comparison is already running, recompares everything so the change is visible at once -- the
		// same reason as the registration toggle.
		case kKCMPopupIgnorePageNumActionID:
		{
			InterfacePtr<IKCMCompareFacade> folio(Utils<IKCMCompareFacade>().QueryUtilInterface());
			folio->SetIgnorePageNumberMarker(!folio->GetIgnorePageNumberMarker());
			PMString msg(folio->GetIgnorePageNumberMarker() ? "Ignore page number marker: on." : "Ignore page number marker: off.");
			msg.SetTranslatable(kFalse);
			InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
			IDataBase* const markedDB    = marks->GetMarkedTargetDB();
			IDataBase* const markedSrcDB = marks->GetMarkedSourceDB();
			if (markedDB != nil && markedSrcDB != nil)
			{
				PMString report;
				// ★allowIncremental is not passed here either ＝ every page is compared again, so with many
				//   pages the progress bar offers Cancel. On a cancellation the model side discards every mark
				//   and answers kFailure. Throwing that away would leave only the armed state ＝ "Started with
				//   not one frame", so it is unwound to Stop exactly as the Start route does
				//   (KCMToggleStartStop takes its Stop branch when called while armed). Found in a self-review.
				if (Utils<IKCMCompareFacade>()->MarkChanges(markedDB, markedSrcDB, report) == kSuccess)
				{
					msg.Append(" (recompared)");
				}
				else
				{
					Utils<IKCMCompareFacade>()->ToggleStartStop();		// the marks are already discarded -> take the strip out, disarm, and be properly stopped
					msg.Append(" (cancelled - stopped)");
				}
			}
			KCMSetStatus(msg);
			break;
		}

		// ★★Flyout "Compare mode > Pixel Changes / Story Changes": switch what is compared. The same
		//   shape as "Ignore Page Number Marker" above ＝ change the setting and, if already Started,
		//   compare everything again on the spot.
		//   ⚠**The previous mode’s result is discarded.** Holding both at once would create two answers
		//     to "which of them is the screen showing" ([[one-question-one-place]]).
		case kKCMPopupModePixelActionID:
			KCMApplyCompareMode(kKCMModePixel);
			break;
		case kKCMPopupModeStoryActionID:
			KCMApplyCompareMode(kKCMModeStory);
			break;

		// Flyout "Save Panel Settings": write the current settings toggles to a private JSON file and
		// show where it went **in the panel’s status line** (the work is KCMSavePanelState in
		// KCMPanelState.cpp). Reading it back happens at startup (KCMUIStartup::Startup; the account is
		// in KCMPanelState.h).
		case kKCMPopupSavePanelStateActionID:
			KCMSavePanelState();
			break;

		// The "Register as Added/Removed Pages" toggle on the Pages panel page context menu. It
		// registers the selected pages as having no counterpart, or clears that (the work is in
		// KCMPageMap.cpp).
		// ⚠★★With only the `[none]` row selected it does nothing -- the same reason as the guard in
		//   UpdateActionStates: **the menu does not offer it, but a shortcut is another way in**, so
		//   the executing side needs the guard too.
		case kKCMPageMapToggleActionID:
			if (KCMPagesPanelSelectionHasNoRealPage())
				break;
			Utils<IKCMPageFlagsFacade>()->ToggleRegisterForSelection();
			break;

		// The "Check" toggle on the Pages panel page context menu: put a tick on the selected pages or
		// take it off (the work is in KCMPageCheck.cpp; the tick is drawn by the isThumb branch of
		// KCMDrawEventHandler).
		case kKCMPageCheckToggleActionID:
			if (KCMPagesPanelSelectionHasNoRealPage())
				break;		// ⚠the same reason as above
			Utils<IKCMPageFlagsFacade>()->ToggleCheckForSelection();
			break;

		// "Refresh Page Comparison" on the Pages panel page context menu (a plain command): recompare
		// the selected pages and update their frames and thumbnails. The work is in KCMPeek.cpp, and the
		// outcome is reported briefly in the status line.
		case kKCMPageRefreshCompareActionID:
		{
			if (KCMPagesPanelSelectionHasNoRealPage())
				break;		// ⚠do not run with only the `[none]` row selected (the same reason as the two above)
			int32 nPages = 0, nChanged = 0, nFailed = 0;
			bool16 wasCancelled = kFalse;
			if (Utils<IKCMCompareFacade>()->RefreshSelectedPages(&nPages, &nChanged, &wasCancelled, &nFailed))
			{
				PMString msg("refreshed ");
				msg.SetTranslatable(kFalse);
				msg.AppendNumber(nPages);
				msg.Append(" (changed ");
				msg.AppendNumber(nChanged);
				msg.Append(")");
					// ★Pages that could not be compared (mismatched page size, a failed rasterisation) are not
					//   hidden: their frames stay as they were, which is what tells the user they are not current.
				if (nFailed > 0)
				{
					msg.Append(" (failed ");
					msg.AppendNumber(nFailed);
					msg.Append(")");
				}
					// ★Say so when it was stopped part way: the remaining selected pages are still stale, and it
					//   must not read as "all done".
				if (wasCancelled)
					msg.Append(" - cancelled");
				KCMSetStatus(msg);
			}
			else
			{
				// The enabling test (KCMRefreshComparisonAvailable) does not look inside the selection, so with
				// nothing selected -- or with every page unsupported (registered as Added/Removed and so on) --
				// this returns kFalse having done nothing. Rather than appear unresponsive, it says "nothing was
				// recompared this time", which also stops a leftover "refreshed" line from reading as success.
				// ※A cancellation has already processed the pages up to that point and takes the branch above,
				//  so what reaches here is normally "there was nothing to do". The distinction is kept anyway.
				PMString msg(wasCancelled ? "refresh cancelled." : "refresh: no comparable pages.");
				msg.SetTranslatable(kFalse);
				KCMSetStatus(msg);
			}
			break;
		}

		// Flyout "Save Check & Register": merge the current ticks and Added/Removed registrations of the
		// Target and Source into a private JSON file and show the path in the status line (the work is in
		// KCMPageCheck.cpp).
		case kKCMPopupSaveChecksActionID:
			Utils<IKCMPageFlagsFacade>()->SaveChecksAndRegister();
			break;

		// Flyout "Load Check & Register": apply the registrations from that JSON to both documents,
		// recompare, then restore the ticks (only on pages that still carry a mark). The work is in
		// KCMPageCheck.cpp.
		// ⚠★**The menu item is always pressable.** ("Only while Started" would read as menu enabling,
		//   and UpdateActionStates below has no branch for this ActionID.) The ActionDef in the `.fr`
		//   carries kDisableIfLowMem without kCustomEnabling and says "plain command; guards inside
		//   (needs Start)" ＝ **it only means something while Started, and refusing is the work’s own
		//   job**. Save Check & Register is built the same way.
		case kKCMPopupLoadChecksActionID:
			Utils<IKCMPageFlagsFacade>()->LoadChecksAndRegister();
			break;

		// Flyout "Clear Checks in This Document": drop the active document's ticks. The work, the
		// status line and the notification that takes the ticks off the Pages panel's thumbnails
		// are all on the model side (KCMPageCheck.cpp), which is why nothing is reported here.
		// ★It exists because **Stop no longer clears the ticks** (2026-09-04): Stop used to double
		//   as the way to be rid of them all.
		case kKCMClearChecksActionID:
			Utils<IKCMPageFlagsFacade>()->ClearChecksInDoc(Utils<IKCMCompareFacade>()->GetActiveDocDB());
			break;

		// Flyout "Clear Cat Paws in This Document": the same for the cat-paw stamps.
		case kKCMClearPawsActionID:
			Utils<IKCMPageFlagsFacade>()->ClearPawsInDoc(Utils<IKCMCompareFacade>()->GetActiveDocDB());
			break;

		// Flyout "Clear Target and Source": drop both choices, so the next Start falls back to the
		// automatic rule (active document = Target, the earliest-opened other document = Source).
		// ★**The panel refresh and the status line are done here, not by the facade** -- the same
		//   division as the two "Set as" items this undoes ([[one-question-one-place]]: the facade
		//   changes the state, the UI decides what the UI shows).
		// ⚠It does not stop a running comparison, and it does not need to: the item is greyed
		//   while one is armed.
		case kKCMClearChosenActionID:
			Utils<IKCMCompareFacade>()->ClearChosenDocs();
			KCMRefreshPanel();
			KCMSetStatus("Target and Source cleared.");
			break;

		// Flyout "Export Changed Pages...": save the list of changed pages of the current comparison as
		// TSV (new page / old page / kind = changed, inserted, deleted). The work is in
		// KCMChangedPagesTSV.cpp. Enabled only while comparing; overset is not included.
		case kKCMPopupExportChangedPagesActionID:
			{
			// ★The writing itself is on the model side and **the message comes back as a return value for
			//   the UI to show**. Success is silent -- it returns empty -- and then nothing is shown.
				PMString exportMsg;
				Utils<IKCMCompareFacade>()->ExportChangedPagesTSV(exportMsg);
				if (exportMsg.CharCount() > 0)
					KCMSetStatus(exportMsg);
			}
			break;

		// Flyout "Compare Books": the book whose tab is in front in the Book panel is the Target, the
		// first other open book is the Source, and every chapter (document) is judged changed or
		// unchanged.
		// ★It is entirely independent of the document comparison (Start): it does not arm, creates no
		//   frames and touches neither sDB nor sEntries.
		// ★★The flow is **a confirmation alert, then OK compares, then the result dialog** (user’s
		//   instruction). It used to open the dialog first and run from a Compare button inside it (that
		//   button is gone).
		//   ⚠**Showing the two books before anything is pressed** has not changed as an aim -- what
		//     changed is where they are shown (from two lines of the dialog to the body of the alert)
		//     and that they are **full paths** rather than names, because so many books share a name.
		case kKCMPopupCompareBooksActionID:
			KCMRunBookComparison();
			break;

		// "Start Change Marker" on **a chapter row’s context menu** in the book comparison dialog: open
		// that chapter’s Target and Source documents in windows and start the comparison, stopping a
		// running one first.
		// ★Which row it was is noted by KCMBookSetMenuRow at the moment of the right click -- an action
		//   is handed nothing but an ActionID, so that is the only way to know which chapter is meant
		//   (the same construction as KBS’s result rows).
		case kKCMBookRowStartActionID:
			KCMBookStartComparisonForRow(KCMBookMenuRow());
			break;

		// "Refresh Story Comparison" on **a Story Edits row’s context menu**: re-run the text diff for
		// that story alone and replace its children with the current state.
		// ★Which row it was is noted the same way as for a chapter row, by KCMStorySetMenuRow at the
		//   right click.
		// ★Once the differences are gone **the row stays and only the children go** (user’s call). The
		//   outcome is reported in the status line, which is what tells "nothing happened" apart from
		//   "the differences are gone".
		case kKCMStoryRowRefreshActionID:
			KCMStoryRefreshMenuRow();
			break;

		// (The panel tool button's flyout had two cases here for a few hours on 2026-09-04. They
		//  are gone with their ActionDefs: the flyout is a Win32 popup raised by KCMToolButtonEH,
		//  and it calls KCMToolButtonPressed itself. ⚠Do not add them back without a MenuDef --
		//  an action nothing can invoke is a command in QuickApply that does nothing a reader
		//  asked for.)

		default:
			break;
	}
}

/* KCMSetCheckState - a check-style toggle: **always available, with a check while it is ON**.

   ⚠**Not every toggle is this shape**: "Hide Unchanged Spreads" is greyed unless Started, and the
     two Pages-panel toggles carry an INTERMEDIATE check (kMultiSelectedAction) when only some of
     the selected pages are flagged. Reaching for this where the answer is not simply on-or-off is
     how one of those states quietly disappears.
*/
static void KCMSetCheckState(IActionStateList* listToUpdate, int32 i, bool16 on)
{
	int16 actionState = kEnabledAction;
	if (on)
		actionState |= kSelectedAction;
	listToUpdate->SetNthActionState(i, actionState);
}

/* UpdateActionStates - a check-style toggle raises kSelectedAction while it is ON (the same
   practice as docwatch’s DocWchActionComponent::UpdateActionStates). Conditional enabling and
   dynamic labels are answered here as well: Start/Stop (the name is switched, and Start is greyed
   with fewer than two documents), Hide Unchanged Spreads (only while Started), Find Overset
   (greyed with no document to scan; always live while ON), and the conditional enabling of
   Refresh Overset, Export and the rest.
   "Register as Added/Removed Pages" on the Pages panel context menu is the one with
   selection-dependent enabling, an intermediate check and a dynamic label, so the model is asked
   "how should this look now" and the answer is written into the menu here
   (IKCMPageFlagsFacade::GetRegisterToggleState -> KCMPageMapGetToggleState / KCMPageMap.cpp).
   ⚠**Calling SetNthActionState / SetNthActionName is this UI side’s job**; the model only
   answers. (An older text here used the former name KCMPageMapUpdateToggleState and said the
   work was delegated.) */
void KCMActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);

		// ★★★**With only the `[none]` row (no master) selected, the three page items are not offered.**
		//   `[none]` has no real page, so `GetSelectedPages` falls back to "the current page" and
		//   **a tick would land on a page nobody selected** (reproduced in the running application, and
		//   reported by the user).
		//   ★The test is gathered into one function, `KCMPagesPanelSelectionHasNoRealPage()` (the reason
		//     and the measurements are where it is declared).
		//   ⚠**The executing side (DoAction) needs the same guard** ＝ an ActionID can be given a
		//     shortcut, so there is a way in that does not pass through the menu. ([[one-question-one-place]]
		//     is kept by **having one test function**; it is called from two places because there are two
		//     entrances.)
		if ((action == kKCMPageMapToggleActionID ||
		     action == kKCMPageCheckToggleActionID ||
		     action == kKCMPageRefreshCompareActionID) &&
		    KCMPagesPanelSelectionHasNoRealPage())
		{
			listToUpdate->SetNthActionState(i, kDisabled_Unselected);
			continue;
		}

		if (action == kKCMPopupStartStopActionID)
		{
			// The menu name follows the armed state (armed = Stop, not armed = Start).
			// (kSelectedAction is not raised: this switches the name itself rather than showing a check.)
			// ★The facade is asked several times, so it is taken once into an InterfacePtr (`Utils.h:74-80`),
			//   which is what the other branches of this file already do.
			InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
			const bool16 armed = compare->IsArmed() && (compare->GetArmedTargetDB() != nil);
			PMString name(armed ? "Stop" : "Start");
			name.SetTranslatable(kFalse);
			listToUpdate->SetNthActionName(i, name);
			// ★Stop is always live: clearing the marks and ending a peek must work even with no document
			//   open (the clearing branch of KCMToggleStartStop keeps its own record of the documents the
			//   marks were actually drawn in).
			//   Start needs two documents, Target and Source, so it is greyed until they are there (user’s
			//   instruction). The test goes through the same CanStartComparison() the executing side uses,
			//   so what the menu looks like and what pressing it does cannot part company.
			listToUpdate->SetNthActionState(i,
				(armed || compare->CanStartComparison()) ? kEnabledAction : kDisabled_Unselected);
		}
		// ★"Set as Target" / "Set as Source". The two share a branch because they are enabled by the
		//   same two conditions, and writing the pair twice is how the two would come to differ.
		//   ★**Live only while nothing is being compared** (user's instruction): the pair a running
		//     comparison was started on is what its marks were made from, so letting the choice move
		//     underneath it would put a name on the panel that the marks on screen do not come from.
		//     Choose, then Start ＝ the order the items sit in on the flyout.
		//   ★**And only with an active document to name**, since that is what these set. The
		//     executing side asks the same question by taking the setter's kFalse, so the grey item
		//     and the refusal cannot part company.
		//   ⚠**Not greyed when the same document is already chosen for the other one.** Choosing one
		//     document for both is allowed, and the panel showing the same name twice is the reader
		//     seeing what they asked for; what refuses is the Start, with a message naming both ways
		//     out of it (KCMToggleStartStop). ★**Both, because setting is not the only way in**: the
		//     commoner one is choosing a Source alone and pressing Start without switching documents,
		//     since the unchosen Target then resolves to that very document -- and the way out of
		//     that one is to bring the other document to the front.
		else if (action == kKCMPopupSetTargetActionID || action == kKCMPopupSetSourceActionID)
		{
			InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
			const bool16 armed = compare->IsArmed() && (compare->GetArmedTargetDB() != nil);
			listToUpdate->SetNthActionState(i,
				(!armed && compare->GetActiveDocDB() != nil) ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMPopupPrintMarksActionID)
		{
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetPrintMarks());
		}
		else if (action == kKCMPopupOpacity25ActionID)
		{
			// Radio-like: this item carries the check while 25% is in force (exclusive with 75%).
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetMarkOpacity25());
		}
		else if (action == kKCMPopupOpacity75ActionID)
		{
			// Radio-like: this item carries the check while 75% (= not 25%) is in force.
			KCMSetCheckState(listToUpdate, i, !Utils<IKCMCompareFacade>()->GetMarkOpacity25());
		}
		// ★The two "Mark colour" items. Radio-like, as Marks opacity above ＝ the one in force carries
		//   the check. **Both are always live**: they can be chosen with nothing being compared, and
		//   they apply to the next Start.
		else if (action == kKCMPopupColorRedActionID)
		{
			KCMSetCheckState(listToUpdate, i, !Utils<IKCMCompareFacade>()->GetMarkColorCyan());	// red (the default) puts the check here
		}
		else if (action == kKCMPopupColorCyanActionID)
		{
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetMarkColorCyan());
		}
		// ★The two "Compare mode" items. Radio-like, as Marks opacity above ＝ the one in force carries
		//   the check. **Both are always live**: they can be chosen with nothing being compared, and
		//   they apply to the next Start.
		else if (action == kKCMPopupModePixelActionID)
		{
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetCompareMode() == kKCMModePixel);
		}
		else if (action == kKCMPopupModeStoryActionID)
		{
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetCompareMode() == kKCMModeStory);
		}
		else if (action == kKCMPopupHideUnchangedActionID)
		{
			// ★Greyed unless Started (armed), by the user’s instruction. This feature picks "the spreads
			//   with no change" from the comparison marks (sEntries), so with nothing Started there is
			//   nothing to pick (the work has a guard at its head for the same reason). It uses the same
			//   kDisabled_Unselected as the other plain commands (Refresh Overset, Export Changed Pages).
			// ★There is no way to end up "ON and greyed and unable to get back": a Stop
			//   (**KCMDoClearMarks**) always calls ResetHideUnchanged(kTrue), which shows the hidden
			//   spreads again and turns the toggle off. So do a recomparison (**KCMDoMarkChangesDoc**) and
			//   a document closing (**KCMHandleDocsClosed**).
			//   ⚠Line numbers were dropped from those three references: all three had gone stale, and one
			//     pointed more than a thousand lines past the end of its file. ★**A name does not move.**
			// ★★**Greyed in Story mode as well.** What this hides is "a spread with not one page carrying a
			//   comparison mark (sEntries)", and a story diff creates no entry at all ---- ∴ pressing it in
			//   Story mode would mean "hide everything except the spreads with a registration or an
			//   overflow".
			//   ⚠**The safety net on the executing side does not stop it**: KCMHideUnchangedToggle only
			//     refuses when sEntries, the registrations and the overflows are **all** empty, or when
			//     every visible spread would be hidden; with one registration or one overflow it goes
			//     through and hides them. ⇒ It is refused here.
			//   ★"ON and greyed and stuck" cannot happen here either: switching mode (KCMApplyCompareMode)
			//     always recompares everything while Started, and the entry to that, KCMDoMarkChangesDoc,
			//     calls KCMResetHideUnchanged(kTrue). Not Started, IsArmed is false and it was already
			//     greyed.
			// ★The facade is asked several times, so it is taken once into an InterfacePtr (Utils.h:74-80,
			//   the same shape as the Start/Stop branch above).
			InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
			int16 actionState;
			if (!compare->IsArmed() || compare->GetCompareMode() == kKCMModeStory)
				actionState = kDisabled_Unselected;
			else
				actionState = compare->GetHideUnchangedOn() ? (kEnabledAction | kSelectedAction) : kEnabledAction;
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKCMPopupShowOldNumsActionID)
		{
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetShowOldPageNumbers());
		}
		else if (action == kKCMPopupSyncViewsActionID)
		{
			KCMSetCheckState(listToUpdate, i, KCMGetLayoutSync());
		}
		else if (action == kKCMPopupScrollMapActionID)
		{
			KCMSetCheckState(listToUpdate, i, KCMGetScrollMapEnabled());	// a check while it is ON (the default)
		}
		else if (action == kKCMPopupTranslucentPanelActionID)
		{
			// ★It can be chosen while docked (it is not greyed) -- the user’s instruction. The case where
			// pressing it has no visible result is explained by the status wording in DoAction.
			KCMSetCheckState(listToUpdate, i, KCMGetPanelTranslucent());
		}
		else if (action == kKCMPopupTranslucentPagesActionID)
		{
			// ★Same policy as above: it can be chosen with the Pages panel docked or closed.
			// (Greying it by "is it floating right now" would make the setting impossible to undo the
			//  moment the panel goes into a dock.)
			KCMSetCheckState(listToUpdate, i, KCMGetPagesPanelTranslucent());
		}
		else if (action == kKCMPopupTranslucentBookDialogActionID)
		{
			// ★Same policy as the two above: it can be chosen while the dialog is not open. A setting made
			// while it is closed takes effect the next time it opens (KCMBookDialog.cpp applies it on every
			// open).
			KCMSetCheckState(listToUpdate, i, KCMGetBookDialogTranslucent());
		}
		else if (action == kKCMPopupShowSrcMarksActionID)
		{
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetShowSourceMarks());
		}
		else if (action == kKCMPopupShowTgtMarksActionID)
		{
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetShowTargetMarks());
		}
		else if (action == kKCMPopupIgnorePageNumActionID)
		{
			KCMSetCheckState(listToUpdate, i, Utils<IKCMCompareFacade>()->GetIgnorePageNumberMarker());
		}
		else if (action == kKCMPageMapToggleActionID)
		{
			// The registration toggle on the Pages panel context menu: enabling (is anything selected),
			// the check (all registered = tick, some = intermediate) and the label (Target = Added,
			// Source = Removed). ★**The model counts; this side writes.**
			// ★★The model **only answers**; writing into the menu and holding the label strings is this
			//   side’s job. The shape of the answer is KCMPageToggleState in KCMPageMap.h.
			const KCMPageToggleState st = Utils<IKCMPageFlagsFacade>()->GetRegisterToggleState();
			if (!st.fEnabled)
			{
				listToUpdate->SetNthActionState(i, kDisabled_Unselected);
			}
			else
			{
				int16 actionState = kEnabledAction;
				if (st.fTick == kKCMPageTickAll)
					actionState |= kSelectedAction;			// all of them registered = a check
				else if (st.fTick == kKCMPageTickSome)
					actionState |= kMultiSelectedAction;	// only some registered = an intermediate check
				listToUpdate->SetNthActionState(i, actionState);

				// The dynamic label (English, as the rest of the panel UI). This is exactly what
				// IActionStateList.h:78 describes -- changing a menu name by state -- and it needs none of the
				// dynamic-menu machinery.
				// ⚠While disabled the name is left alone (the .fr default stands), as before.
				PMString name(st.fRole == kKCMPageRoleSource ? "Register as Removed Pages"
															   : "Register as Added Pages");
				name.SetTranslatable(kFalse);
				listToUpdate->SetNthActionName(i, name);
			}
		}
		else if (action == kKCMPageCheckToggleActionID)
		{
			// The "Check" toggle on the Pages panel context menu: enabling (Started, on the Target or
			// Source, with a selection) and the check (all ticked / some = intermediate). ★As with the
			// registration toggle, the model counts and this side writes.
			// ★Check has a fixed label, so fRole is not read.
			const KCMPageToggleState st = Utils<IKCMPageFlagsFacade>()->GetCheckToggleState();
			if (!st.fEnabled)
			{
				listToUpdate->SetNthActionState(i, kDisabled_Unselected);
			}
			else
			{
				int16 actionState = kEnabledAction;
				if (st.fTick == kKCMPageTickAll)
					actionState |= kSelectedAction;			// every selected page in scope is ticked (★what is in scope depends on the mode; the model’s KCMCollectCheckablePageUIDs)
				else if (st.fTick == kKCMPageTickSome)
					actionState |= kMultiSelectedAction;	// only some ticked = an intermediate check
				listToUpdate->SetNthActionState(i, actionState);
			}
		}
		else if (action == kKCMPageRefreshCompareActionID)
		{
			// "Refresh Page Comparison" on the Pages panel context menu (a plain command, not a toggle):
			// live only while Started (armed) and with the Target or Source in front; greyed otherwise.
			listToUpdate->SetNthActionState(i, Utils<IKCMCompareFacade>()->RefreshComparisonAvailable() ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMPopupFindOversetActionID)
		{
			// ★Always live while ON (it has to be possible to switch it back off and clear the crosses).
			//   While OFF it is greyed when there is no document to scan (user’s instruction). What counts
			//   as the target is decided by the same GetOversetScanTargetDB() the executing side uses ＝
			//   the Target while comparing, the active document otherwise.
			//   (It used to be always live, so pressing it with no document open said nothing but "no active
			//    document".)
			const bool16 on = Utils<IKCMMarkData>()->GetOversetOn();
			int16 actionState = (on || Utils<IKCMCompareFacade>()->GetOversetScanTargetDB() != nil) ? kEnabledAction
			                                                              : kDisabled_Unselected;
			if (on)
				actionState |= kSelectedAction;	// a check while it is ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKCMPopupRefreshCompareActionID)
		{
			// Live only while a comparison is armed AND both its documents are still open ＝ exactly
			//   when there is something to compare again. Greyed before a Start and after a Stop.
			// ★**THE SAME TWO QUESTIONS THE COMMAND ASKS** (KCMRefreshComparison), in the same order,
			//   so the grey and the command cannot end up disagreeing ([[one-question-one-place]]).
			//   ⚠ArmedDocsAlive is asked SECOND on purpose: it reads the armed state, which is only
			//   meaningful once IsArmed has said there is one.
			listToUpdate->SetNthActionState(i,
				(Utils<IKCMCompareFacade>()->IsArmed() && Utils<IKCMCompareFacade>()->ArmedDocsAlive())
					? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMPopupRefreshOversetActionID)
		{
			// Live only while Find Overset is ON (= there is something to rescan); greyed otherwise.
			listToUpdate->SetNthActionState(i, Utils<IKCMMarkData>()->GetOversetOn() ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMPopupExportChangedPagesActionID)
		{
			// Live only while comparing (a marked Target document exists) ＝ when there can be changes to
			// write out. Greyed before a Start.
			listToUpdate->SetNthActionState(i, (Utils<IKCMMarkData>()->GetMarkedTargetDB() != nil) ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMClearChecksActionID)
		{
			// ★**The same resolver the command uses** (GetActiveDocDB), so the grey and the command
			//   cannot end up meaning two different documents ([[one-question-one-place]]).
			//   Greyed where the active document holds no tick -- which, now that a tick outlives
			//   Stop, is the only thing worth asking: whether a comparison is running says nothing
			//   about whether there is anything to clear.
			IDataBase* db = Utils<IKCMCompareFacade>()->GetActiveDocDB();
			listToUpdate->SetNthActionState(i,
				Utils<IKCMPageFlagsFacade>()->PageCheckHasAny(db) ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMClearChosenActionID)
		{
			// ★**Two questions, and both are needed.** "Not while armed" is the gate the two
			//   "Set as" items carry -- a choice cannot be changed in the middle of a comparison,
			//   and this item changes the same state they do. "At least one of them is chosen" is
			//   what makes clearing mean anything; with neither set there is nothing to undo.
			//   Both are asked through the same facade the command uses, so the grey and the
			//   command cannot come to mean different things ([[one-question-one-place]]).
			InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
			const bool16 armed = compare->IsArmed() && (compare->GetArmedTargetDB() != nil);
			const bool16 anyChosen = (compare->GetChosenTargetDB() != nil) ||
			                         (compare->GetChosenSourceDB() != nil);
			listToUpdate->SetNthActionState(i,
				(!armed && anyChosen) ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMClearPawsActionID)
		{
			// The same, asked of the paws. A paw never depended on a comparison at all.
			IDataBase* db = Utils<IKCMCompareFacade>()->GetActiveDocDB();
			listToUpdate->SetNthActionState(i,
				(Utils<IKCMPageFlagsFacade>()->PawStampCount(db) > 0) ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMPopupCompareBooksActionID)
		{
			// ★It goes through the same resolver as the execution, so what the menu looks like and what
			//   pressing it does cannot part company. It is live only when "the Book panel has a front tab
			//   and another book is open as well".
			//   ⚠This runs every time the menu is opened and walks the whole panel inside. It is called only
			//     as often as the flyout is opened, which is why that is accepted (KBS walks the same way in
			//     the same place).
			//   ★★Observing the front tab is **on this side (the UI)**: walking the panels needs
			//     PaletteRefUtils / IBookUIUtils / IPanelMgr, none of which a model plug-in can touch (the
			//     linker named them the moment WidgetBin.lib came off).
			//     ⚠**The decision itself did not change** ＝ if the observation fails, the facade is not
			//       called, which is the same outcome as the model-side ResolveBookPair returning kFalse
			//       from inside, as it used to.
			IBook* target = nil;
			IBook* source = nil;
			IDFile panelBookFile;
			const bool16 canCompareBooks =
				KCMGetPanelBookFile(panelBookFile)
				&& Utils<IKCMBookFacade>()->ResolveBookPair(panelBookFile, target, source);
			listToUpdate->SetNthActionState(i,
				canCompareBooks ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKCMBookRowStartActionID)
		{
			// ★It goes through the same test as the execution (KCMBookRowCanStart), so the menu and the
			//   result cannot part company. It is live **only on a row judged Changed** (user’s
			//   instruction) that has both a Target file and a Source file. ∴ greyed covers not only the
			//   chapters that exist on one side (ChapterAdded / ChapterDeleted) and those naming no file,
			//   but **NoChange / Failed / NotCompared as well**.
			//   ⚠This is the only item on that row menu, so when it is greyed **the menu does not appear at
			//     all** (InDesign’s behaviour). That is the specification -- do not "improve" it into
			//     something that shows an empty menu.
			listToUpdate->SetNthActionState(i, KCMBookRowCanStart(KCMBookMenuRow()) ? kEnabledAction
			                                                                            : kDisabled_Unselected);
		}
		else if (action == kKCMStoryRowRefreshActionID)
		{
			// ★It goes through the same test as the execution (KCMStoryRowCanRefresh), so the menu and the
			//   result cannot part company. It is live only **in Story mode, while comparing, on a row
			//   whose story has a counterpart** (user’s instruction, "only in the story mode").
			//   ⚠This too is the only item on its row menu, so when it is greyed **the menu does not appear
			//     at all** -- that a right click in Pixel mode brings up nothing is decided by this one
			//     line.
			listToUpdate->SetNthActionState(i, KCMStoryRowCanRefresh() ? kEnabledAction
			                                                            : kDisabled_Unselected);
		}
	}
}

/* DoAbout */
void KCMActionComponent::DoAbout()
{
	CAlert::ModalAlert
	(
		// Pass the string key (CAlert translates it). ★The UI went back to English only, so the
		// run-time Japanese switch was taken out of here. The content is one line of the enUS table:
		// "<name> version x.y.z".
		PMString(kKCMAboutBoxStringKey),
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

/* (DoAboutScript went with the "About Scripting" item. The scripting API had been removed
    before that, so the item did nothing but say "No scripts are currently available.") */

/* DoUsage - the "How to Use" flyout item: shows the operating reference (what used to be the
   panel’s description text). */
void KCMActionComponent::DoUsage()
{
	// ★The text is **kept in two parts and joined here**; the reader sees one piece of writing.
	//   ⚠The split is for the English side: odfrc caps the length of one string in a StringTable,
	//     and there was no room left in the enUS kKCMHintKey for the book section
	//     ([[odfrc-long-string-limit]]). The cap is per string, so two parts clear it. **The seam is
	//     where the book section had to go** (before "finding overset"), and the second part is
	//     everything from there on, the disclaimer at its end included.
	//   ★**Each part must be translated BEFORE it is appended**: KCMLoc::Text returns **finished
	//     text** (marked untranslatable) -- the Japanese literal on a Japanese UI, the enUS table’s
	//     string otherwise -- so joining them cannot break a translation key. **Never join keys to
	//     each other.**
	PMString usage = KCMLoc::Text(kKCMHintKey, KCMJa::kHint);
	usage.Append(KCMLoc::Text(kKCMHint2Key, KCMJa::kHint2));
	usage.SetTranslatable(kFalse);

	CAlert::ModalAlert
	(
		// Finished text, not a key: Japanese on a Japanese UI, the enUS table’s English otherwise.
		// ★The how-to-use text is shown in Japanese on a Japanese UI (user’s instruction). It explains
		//   the plug-in to someone using it for the first time, so it is the exception to keeping the
		//   menus, the panel and the status line English (the same line KBS draws).
		usage,
		kOKString,					// OK button
		kNullString,				// No second button
		kNullString,				// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon	// Information icon
	);
}

// (KCMDoSplitTarget (Split Target 90/10) was removed. The whole implementation and what was
//  measured about it are kept in docs/ai-notes/kescm-split-target-mechanism.md and in the git
//  history at 69c4b07 ＝ a candidate for reuse in another plug-in.)


//========================================================================================
// Find Overset (flyout): scan one active document and put a cross on -- or clear it from -- every
// page with overset text.
// Entirely independent of the comparison. The state belongs to the model side and is read and
// written from here through IKCMMarkData / IKCMCompareFacade.
//========================================================================================

// (Resolving the active document is gathered into KCMActiveDocDB (KCMCore); the duplicate here
//  was removed.)

/* DoFindOversetToggle - the "Find Overset" flyout toggle.
   OFF -> ON: scan the target document (the Target while comparing, the active document
   otherwise) and apply the overset it finds.
   ON -> OFF: empty the set, turn the toggle off, and repaint the scanned document to clear the
   marks. */
void KCMActionComponent::DoFindOversetToggle()
{
	// ON -> OFF: clear the crosses.
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	if (marks->GetOversetOn())
	{
		IDataBase* prevDB = marks->GetOversetDB();
		// Note the page set before it goes, so the crosses can be cleared from the Pages panel
		// thumbnails.
		std::vector<UID> prevPages;
		marks->GetOversetPageUIDs(prevPages);
		Utils<IKCMCompareFacade>()->ClearOverset();
		KCMRefreshThumbnailsForPages(prevDB, prevPages);	// rebuild the thumbnails so the crosses go
		// The scrollbar map: with nothing being compared, take it out of every window; while comparing,
		// keep it and repaint the red bands alone.
		if (Utils<IKCMCompareFacade>()->IsArmed())
			KCMScrollMapInvalidateAll();
		else
			KCMScrollMapDetachAll();
		Utils<IKCMCompareFacade>()->InvalidateDB(prevDB);	// nil-safe, as the other calls are
		KCMRefreshNavPosition();	// take the overset places out of Prev/Next (leaving the comparison alone, or nothing at all)
		KCMSetStatus("Find Overset: off.");
		return;
	}

	// OFF -> ON: scan the target document (the Target while comparing, the active one otherwise)
	// and apply the result.
	IDataBase* db = Utils<IKCMCompareFacade>()->GetOversetScanTargetDB();
	if (db == nil)
	{
		KCMSetStatus("Find Overset: no active document.");
		return;
	}
	Utils<IKCMCompareFacade>()->ApplyOversetForDoc(db);

	// ★Ask whether the toggle actually went up before reporting. ApplyOversetForDoc **does nothing**
	//   when the db it is handed is not in the document list (the last line of defence against
	//   dereferencing a closed document), and then the toggle is still OFF. This used to report "on"
	//   unconditionally, so it could say "on" **while it was OFF and the flyout check was clear**.
	//   ⚠It is a rare path ＝ the db GetOversetScanTargetDB() answered with died immediately
	//     afterwards (a gap in the close sweep). Rare or not, it is the shape where the display and
	//     the truth disagree, so the state is read back and reported.
	if (!Utils<IKCMMarkData>()->GetOversetOn())
	{
		KCMSetStatus("Find Overset: document is gone.");
		return;
	}

	PMString msg("Find Overset: on (");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber(Utils<IKCMMarkData>()->GetOversetPageCount());
	msg.Append(" page(s)).");
	KCMSetStatus(msg);
}

/* DoRefreshOverset - the "Refresh Overset" flyout item. Live only while Find Overset is ON (it is
   greyed by UpdateActionStates otherwise). It rescans the active document and puts the set up
   again; if the document has changed, the previous one’s crosses are cleared too. */
void KCMActionComponent::DoRefreshOverset()
{
	if (!Utils<IKCMMarkData>()->GetOversetOn())
		return;	// inactive while OFF (a safety net; normally the menu is greyed and this is not reached)

	IDataBase* db = Utils<IKCMCompareFacade>()->GetOversetScanTargetDB();
	if (db == nil)
	{
		KCMSetStatus("Refresh Overset: no active document.");
		return;
	}
	Utils<IKCMCompareFacade>()->ApplyOversetForDoc(db);	// rescan and apply (with another document, the previous one’s marks are cleared as well) - gathered in the shared call

	// ★The read-back the ON route above does is not needed here. Even if Apply returns early because
	//   the db has died, **the toggle is already ON**, so it cannot say "on" while it is OFF; the
	//   previous count is simply reported again, which is indistinguishable from "rescanned and
	//   nothing changed". ∴ no statement is added.
	PMString msg("Refresh Overset: ");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber(Utils<IKCMMarkData>()->GetOversetPageCount());
	msg.Append(" page(s).");
	KCMSetStatus(msg);
}

// KCMOpenAboutURL (declared in KCMUIShared.h) - called when the panel illustration is clicked.
// It opens the distribution URL (kKCMRepoURL) in the default browser. ⚠**About does not carry
// that URL**: DoAbout shows one line of name and version. The other user of the URL is the
// illustration's tooltip (KCMIconTip.cpp). Nothing here touches the document model
// (it only asks the OS to launch something), so it needs no Command.
// GoToURLUtils::GoToURL is InDesign’s own utility function, which launches the default browser on
// Windows and the Mac alike through IURLAccess, the internal interface behind hyperlinks
// (PUBLIC_DECL; no boss and no IID have to be obtained).
// isAGoURL=kFalse: that flag is only for Adobe’s "go.adobe.com" short links and is not used for an
// ordinary external URL.
void KCMOpenAboutURL()
{
	PMString url(kKCMRepoURL);
	url.SetTranslatable(kFalse);
	GoToURLUtils::GoToURL(url, kFalse);
}

// End, KCMActionComponent.cpp.
