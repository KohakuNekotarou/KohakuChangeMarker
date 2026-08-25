//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStorySection.h for what this does and where it was copied from.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// ----- Interfaces -----
#include "IControlView.h"
#include "IIntData.h"				// how the remembered section height is read and written
#include "IPanelControlData.h"
#include "ITextControlData.h"		// the section heading's text
#include "ISession.h"				// GetExecutionContextSession (nil during teardown)
#include "IApplication.h"			// QueryApplication
#include "IPanelMgr.h"				// QueryPanelManager / GetPaletteRefContainingPanel
#include "IWidgetParent.h"			// QueryParentFor - the palette sizer lives ABOVE the panel
#include "IMonitorInfo.h"			// GetBestScreenRect - is the grown panel still on the screen?

// ----- Interfaces published under source/open -----
//
// Reached by relative path rather than by adding an include directory. The build files that would
// carry such a directory - the vcxproj and the generated ODFRC response file - both live outside
// this plug-in's repository, and the response file has no counterpart in _buildproj at all, so a
// path added there would not survive a fresh checkout. The same reasoning is written next to the
// matching include in KCMUI.fr. These three headers are listed in the SDK's own OpenTestHeader.h,
// which is its statement that they are meant to compile from outside source/open.
#include "../../open/interfaces/ui/ISplitterPanelControlData.h"
#include "../../open/interfaces/ui/ISplitterPanelController.h"
#include "../../open/interfaces/ui/IOWLPaletteSizer.h"

// ----- Includes -----
#include "IconRsrcDefs.h"			// kTreeBranchCollapsedRsrcID / kTreeBranchExpandedRsrcID
#include "PaletteRef.h"
#include "PaletteRefUtils.h"		// IsPaletteFloating / GetPaletteBounds / SetPaletteSize

// ----- Project -----
#include "KCMUIID.h"
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// IsArmed(2026-08-13・分割 第1段 Task 11 で Facade 経由へ。
									//  KCMGetVisibleOwnPanel は Task 5 で KCMUIShared.h へ移っている)
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "IKCMStoryEditsFacade.h"	// GetRowCount - the number in the heading (Facade since 2026-08-13, Task 14)
#include "KCMStorySection.h"

namespace
{

// The splitter numbers its panes 0 = top, 1 = bottom (ISplitterPanelControlData.h:63).
const int32 kStorySectionPaneIndex = 1;

// How tall the section opens the first time, before it has ever been closed at a height of its own.
// Matches the lower pane's Frame in KCMUI.fr, so the first open looks like what the resource says.
// ★The list's own share is the 100; the heading band above it is added on rather than taken out of
//   it, so making the band thicker never costs a row (KCMUIID.h holds the one copy of its height).
const int32 kStorySectionDefaultHeight = 100 + kKCMStoryHeaderHeight;

/** The whole panel's height, measured off the splitter - which fills the panel edge to edge.
*/
int32 WholePanelHeight(ISplitterPanelControlData* splitter)
{
	InterfacePtr<const IControlView> splitterView(splitter, UseDefaultIID());
	if (splitterView == nil)
		return 0;

	return ::ToInt32(splitterView->GetFrame().Height());
}

/** How tall the section is right now: the splitter's own height less where its bar sits.
	Same expression as LinksUIUtils.cpp:638-639.
*/
int32 CurrentSectionHeight(ISplitterPanelControlData* splitter)
{
	const int32 whole = WholePanelHeight(splitter);
	return whole > 0 ? whole - splitter->GetSplitterEdge() : 0;
}

/** The one height the top pane is allowed to be.

	It is read from the splitter's "top snap" figure, which KCMUI.fr sets to the panel's designed
	height on purpose. The widgets up there are a fixed block laid out at fixed coordinates, so the
	top pane has exactly one correct size, and that number belongs in the resource next to the
	layout it describes rather than repeated here.

	The same figure is what stops the divider being dragged up into the controls: with snapping
	turned off, "slider doesn't move beyond snap pos" (Widgets.fh:418). Dragging DOWN is still
	allowed, as far as the section's own minimum - which is why closing has to aim at this height
	rather than just subtract the section.

	Callers treat a non-positive answer as "no designed height to aim at" and fall back, rather than
	resize the panel to nothing.
*/
int32 DesignedTopPaneHeight(ISplitterPanelControlData* splitter)
{
	return splitter->GetSplitterSnapTop();
}

/** The height the section was left at when it was last closed, or 0 if it never has been.

	Held on the lower pane's own widget (KCMUI.fr gives kKCMStorySectionPanelBoss a persistent
	IIntData under IID_IKCMSAVEDSECTIONHEIGHT), which is where the Links panel keeps the same
	figure. Not a static: the panel throws its widgets away and rebuilds them whenever it is
	re-shown, so anything remembered outside them is remembered wrongly.
*/
int32 SavedSectionHeight(IControlView* sectionView)
{
	InterfacePtr<const IIntData> saved(sectionView, IID_IKCMSAVEDSECTIONHEIGHT);
	return saved != nil ? saved->GetInt() : 0;
}

void SetSavedSectionHeight(IControlView* sectionView, int32 height)
{
	InterfacePtr<IIntData> saved(sectionView, IID_IKCMSAVEDSECTIONHEIGHT);
	if (saved != nil)
		saved->Set(height);
}

/** Make the palette recalculate what its smallest and largest sizes are.

	Opening or closing the section changes those figures, and so does moving the divider - but they
	are normally worked out only while a panel is being resized. linksui forces the recalculation in
	BOTH places for that reason, and its comment names the cause: "moving the splitter affects our
	min/max panel size, and the PanelMgr only recalculates those on Resize...so we have to force a
	recalculation" (LinksUIUtils.cpp:656-659 for a docked palette, :736-739 for a floating one).

	The sizer lives ABOVE the panel, so it is asked for through the widget parent chain.
*/
void UpdatePaletteSizeLimits(IControlView* panelView)
{
	InterfacePtr<const IWidgetParent> wp(panelView, UseDefaultIID());
	if (wp == nil)
		return;

	InterfacePtr<IOWLPaletteSizer> palSizer((IOWLPaletteSizer*)wp->QueryParentFor(IOWLPaletteSizer::kDefaultIID));
	if (palSizer != nil)
		palSizer->UpdateOWLPaletteSizes();
}

/** Pull the panel back up if growing it pushed its bottom edge off the screen.

	Copied from linksui's ForceBottomOfPanelOnMonitor (LinksUIUtils.cpp:613-629), which runs right
	after the floating panel is resized: the section is opened by GROWING downwards, so a panel
	sitting near the bottom of the display would otherwise put its new rows where nobody can see or
	drag them. The +2 and the "which screen am I on" question (GetBestScreenRect handles more than
	one monitor) are both from there.

	★The shrunk size goes through ConstrainDimensions as well - it must not undercut the section's
	own minimum, and only KCMPanelView knows what that is.
	⚠linksui leaves this out under ID_COCOA_ENABLE with a FIXME; KCM is Windows-only (the Mac port
	was dropped 2026-08-07), so there is nothing to leave out here.
*/
void KeepPanelOnScreen(IControlView* panelView)
{
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> theApp(session != nil ? session->QueryApplication() : nil);
	if (theApp == nil)
		return;

	InterfacePtr<const IMonitorInfo> monInfo(theApp, UseDefaultIID());
	if (monInfo == nil)
		return;

	const SysRect panelBBox  = panelView->GetBBox();
	const SysRect globalBBox = ::ToSys(panelView->WindowToGlobal(panelBBox));
	const GSysRect monRect   = monInfo->GetBestScreenRect(globalBBox);
	if (SysRectBottom(monRect) >= SysRectBottom(globalBBox))
		return;		// still on the screen - nothing to do

	const PMReal amtToShrink = PMReal(SysRectBottom(globalBBox) - SysRectBottom(monRect) + 2);
	PMPoint newSize(PMReal(SysRectWidth(panelBBox)), PMReal(SysRectHeight(panelBBox)) - amtToShrink);
	newSize = panelView->ConstrainDimensions(newSize);
	panelView->Resize(newSize);
}

/** Grow (positive) or shrink (negative) the panel by this many pixels.

	***** THE ROUTE DEPENDS ON WHETHER THE PALETTE FLOATS. ***** Resizing the view works while the
	palette is floating; docked, it is constrained and the request has to go to the palette itself.

	Expressed as a delta on both routes, which is a deliberate departure from linksui: that one
	shrinks by a delta but grows to an absolute figure (LinksUIUtils.cpp:663 vs :746). A docked
	palette is not the same height as the panel inside it - it carries the tab strip - so feeding
	the panel's height to SetPaletteSize would lose that difference on every open. A delta cannot.
*/
void ResizePanelByDelta(IControlView* panelView, int32 deltaY)
{
	ISession* session = GetExecutionContextSession();		// nil while the app is tearing down
	InterfacePtr<IApplication> theApp(session != nil ? session->QueryApplication() : nil);
	if (theApp == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(theApp->QueryPanelManager());
	if (panelMgr == nil)
		return;

	PaletteRef palette = panelMgr->GetPaletteRefContainingPanel(panelView);
	if (!palette.IsValid())
		return;		// linksui asserts here; KCM's rule is to give up quietly instead

	if (PaletteRefUtils::IsPaletteFloating(palette))
	{
		// ★PUTTING THE SIZE THROUGH ConstrainDimensions IS THE CALLER'S JOB - it is not something
		//   Resize does on the way in. IControlView.h:174-176: "Before resizing a widget, THE CLIENT
		//   CAN ASK if the size makes sense by calling this method", and linksui calls it explicitly
		//   before the Resize it makes (LinksUIUtils.cpp:626-627).
		//   Why it matters here: KCMPanelView::ConstrainDimensions is where this panel's size rules
		//   live (closed = exactly the top pane's height; open = top pane + the section's minimum).
		//   Going round it left the same rules being decided in two places - they agreed, but only
		//   until one of them was edited (2026-08-11, block 15 audit A-2).
		const PMRect frame = panelView->GetFrame();
		PMPoint newSize(frame.Width(), frame.Height() + deltaY);
		newSize = panelView->ConstrainDimensions(newSize);
		panelView->Resize(newSize);

		// Opening grows the panel DOWNWARDS, so make sure that did not put its bottom off the screen.
		KeepPanelOnScreen(panelView);
	}
	else
	{
		// A docked palette clamps a resize request to the min/max that were computed for the state it
		// is LEAVING, so those have to be recomputed before asking (see UpdatePaletteSizeLimits).
		UpdatePaletteSizeLimits(panelView);

		const SysRect bounds = PaletteRefUtils::GetPaletteBounds(palette);
		SysPoint newSize;
		SetSysPoint(newSize, SysRectWidth(bounds), SysRectHeight(bounds) + deltaY);
		PaletteRefUtils::SetPaletteSize(palette, newSize);
	}
}

} // anonymous namespace

/* KCMToggleStorySection
*/
void KCMToggleStorySection()
{
	IControlView* panel = KCMGetVisibleOwnPanel();
	if (panel == nil)
		return;

	InterfacePtr<const IPanelControlData> panelData(panel, UseDefaultIID());
	if (panelData == nil)
		return;

	InterfacePtr<ISplitterPanelControlData> splitter(panelData->FindWidget(kKCMSplitterWidgetID), UseDefaultIID());
	if (splitter == nil)
		return;

	InterfacePtr<ISplitterPanelController> controller(splitter, UseDefaultIID());
	const int32 designedTop = DesignedTopPaneHeight(splitter);

	if (splitter->IsSinglePanelVisible())
	{
		// Closed: decide how tall to open, show the pane, then make room for it.
		int32 height = SavedSectionHeight(panelData->FindWidget(kKCMStorySectionWidgetID));
		if (height <= 0)
			height = kStorySectionDefaultHeight;
		if (height < splitter->GetSplitterSnapBottom())
			height = splitter->GetSplitterSnapBottom();

		splitter->SetPanelVisible(kStorySectionPaneIndex, kTrue);
		ResizePanelByDelta(panel, height);
	}
	else
	{
		// Open: measure BEFORE hiding - once the pane is gone the splitter no longer describes it.
		const int32 sectionHeight = CurrentSectionHeight(splitter);
		const int32 wholeHeight = WholePanelHeight(splitter);
		if (sectionHeight > 0)
			SetSavedSectionHeight(panelData->FindWidget(kKCMStorySectionWidgetID), sectionHeight);

		splitter->SetPanelVisible(kStorySectionPaneIndex, kFalse);

		// Shrink back to the designed height rather than by the height of the section.
		// ⚠THE REASON THIS WAS WRITTEN IS GONE, AND THE CODE IS RIGHT ANYWAY. It used to say "the
		//   two are the same number until the divider gets dragged, which grows the top pane past
		//   the block of controls it holds" - true until 2026-08-12, when the divider was made
		//   undraggable (kKCMSplitterPanelBoss in KCMUI.fr answers the mouse with an event handler
		//   that does nothing). With no drag, the top pane cannot grow, so aiming at designedTop and
		//   subtracting sectionHeight now reach the same figure and the branch below is a belt on
		//   top of braces. ★Kept because what it defends against is the top pane being some other
		//   height than designed, and nothing in this file is the only thing that can set the
		//   splitter edge - SetPanelVisible and SyncPanelsToSplitter both move it, and neither has
		//   been measured to leave it alone.
		// ★MEASURED 2026-08-18 (bug recheck B-U4): open 303 -> closed 185 -> reopened 303, where 185
		//   is exactly the designed top pane. So the closing arithmetic lands on the number it aims
		//   at, and the reopen restores the height the section was closed at (SavedSectionHeight).
		if (designedTop > 0 && wholeHeight > designedTop)
			ResizePanelByDelta(panel, designedTop - wholeHeight);
		else if (designedTop <= 0)
			ResizePanelByDelta(panel, -sectionHeight);	// no designed height to aim at; do what linksui does
	}

	// Put the divider where the top pane is its designed height, in both directions. linksui instead
	// derives the divider from the height the section asked for (LinksUIUtils.cpp:724), which hands
	// any shortfall to the top pane - right for a panel whose upper half is a resizable list, wrong
	// here, where the upper half is a fixed block of controls. If a docked palette could not grow
	// all the way, the section takes the shortfall instead.
	if (controller != nil && designedTop > 0 && splitter->GetSplitterEdge() != designedTop)
		controller->SetSplitterEdge(designedTop);

	if (controller != nil)
		controller->SyncPanelsToSplitter(kTrue, kFalse);

	// ★The divider has just been moved, and that changes what the panel's min and max sizes are -
	//   which nothing recalculates on its own (see UpdatePaletteSizeLimits). linksui forces it here
	//   for the floating case (LinksUIUtils.cpp:736-739); KCM sets the splitter edge on BOTH routes,
	//   so it is asked for once, here, rather than inside either branch (2026-08-11, block 15 audit
	//   A-3 - the floating route had no recalculation at all).
	UpdatePaletteSizeLimits(panel);

	KCMUpdateStorySectionButtonState();
}

/* KCMUpdateStorySectionButtonState
*/
void KCMUpdateStorySectionButtonState()
{
	IControlView* panel = KCMGetVisibleOwnPanel();
	if (panel == nil)
		return;

	InterfacePtr<const IPanelControlData> panelData(panel, UseDefaultIID());
	if (panelData == nil)
		return;

	IControlView* buttonView = panelData->FindWidget(kKCMStorySectionToggleWidgetID);
	if (buttonView == nil)
		return;

	InterfacePtr<const ISplitterPanelControlData> splitter(panelData->FindWidget(kKCMSplitterWidgetID), UseDefaultIID());
	if (splitter == nil)
		return;

	// Only one pane showing means the section is closed. linksui picks a mirrored triangle for
	// right-to-left interfaces here; KCM ships English and Japanese only, so there is nothing to
	// mirror and the stock pair is enough.
	buttonView->SetRsrcID(splitter->IsSinglePanelVisible() ? kTreeBranchCollapsedRsrcID
	                                                       : kTreeBranchExpandedRsrcID);
}

/* KCMUpdateStorySectionLabel
*/
void KCMUpdateStorySectionLabel()
{
	IControlView* panel = KCMGetVisibleOwnPanel();
	if (panel == nil)
		return;

	InterfacePtr<const IPanelControlData> panelData(panel, UseDefaultIID());
	if (panelData == nil)
		return;

	InterfacePtr<ITextControlData> label(panelData->FindWidget(kKCMStorySectionLabelWidgetID), UseDefaultIID());
	if (label == nil)
		return;

	PMString text(kKCMStorySectionLabelKey);
	text.Translate();

	// ★件数を出すのは比較中だけ。Stop 中は一覧そのものが空なので "(0)" は「変更が無かった」ではなく
	//   「まだ何も比べていない」を意味してしまう ---- 数字を出さないことでその取り違えを断つ
	//   (行の側でも同じ区別をしていて、Stop 中は空、比較中の0件は "No edits" の1行)。
	if (Utils<IKCMCompareFacade>()->IsArmed())
	{
		text.Append(" (");
		text.AppendNumber(Utils<IKCMStoryEditsFacade>()->GetRowCount());
		text.Append(")");
	}

	// ★組み立て終わった文はもう文字列キーではない。翻訳可のままだと、内蔵テーブルにたまたま一致した
	//   瞬間に別の文字列へ化ける(KCM は "Source:" が「スタイルソース :」になる事故を踏んでいる)。
	text.SetTranslatable(kFalse);

	// 第2引数 invalidate = kTrue。第3引数は notifyOfChange で、ここは表示を書くだけなので kFalse
	//   ---- 誰かに知らせる変更ではない。
	label->SetString(text, kTrue, kFalse);
}

// End, KCMStorySection.cpp.
