//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStorySection.h for what this does and where it was copied from.
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

// ----- Interfaces published under source/open -----
//
// Reached by relative path rather than by adding an include directory. The build files that would
// carry such a directory - the vcxproj and the generated ODFRC response file - both live outside
// this plug-in's repository, and the response file has no counterpart in _buildproj at all, so a
// path added there would not survive a fresh checkout. The same reasoning is written next to the
// matching include in KESCM.fr. These three headers are listed in the SDK's own OpenTestHeader.h,
// which is its statement that they are meant to compile from outside source/open.
#include "../../open/interfaces/ui/ISplitterPanelControlData.h"
#include "../../open/interfaces/ui/ISplitterPanelController.h"
#include "../../open/interfaces/ui/IOWLPaletteSizer.h"

// ----- Includes -----
#include "IconRsrcDefs.h"			// kTreeBranchCollapsedRsrcID / kTreeBranchExpandedRsrcID
#include "PaletteRef.h"
#include "PaletteRefUtils.h"		// IsPaletteFloating / GetPaletteBounds / SetPaletteSize

// ----- Project -----
#include "KESCMID.h"
#include "KESCMCore.h"				// KESCMGetVisibleOwnPanel / KESCMIsArmed
#include "KESCMStoryList.h"			// GetRowCount - the number in the heading
#include "KESCMStorySection.h"

namespace
{

// The splitter numbers its panes 0 = top, 1 = bottom (ISplitterPanelControlData.h:63).
const int32 kStorySectionPaneIndex = 1;

// How tall the section opens the first time, before it has ever been closed at a height of its own.
// Matches the lower pane's Frame in KESCM.fr, so the first open looks like what the resource says.
const int32 kStorySectionDefaultHeight = 100;

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

	It is read from the splitter's "top snap" figure, which KESCM.fr sets to the panel's designed
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

	Held on the lower pane's own widget (KESCM.fr gives kKESCMStorySectionPanelBoss a persistent
	IIntData under IID_IKESCMSAVEDSECTIONHEIGHT), which is where the Links panel keeps the same
	figure. Not a static: the panel throws its widgets away and rebuilds them whenever it is
	re-shown, so anything remembered outside them is remembered wrongly.
*/
int32 SavedSectionHeight(IControlView* sectionView)
{
	InterfacePtr<const IIntData> saved(sectionView, IID_IKESCMSAVEDSECTIONHEIGHT);
	return saved != nil ? saved->GetInt() : 0;
}

void SetSavedSectionHeight(IControlView* sectionView, int32 height)
{
	InterfacePtr<IIntData> saved(sectionView, IID_IKESCMSAVEDSECTIONHEIGHT);
	if (saved != nil)
		saved->Set(height);
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
		return;		// linksui asserts here; KESCM's rule is to give up quietly instead

	if (PaletteRefUtils::IsPaletteFloating(palette))
	{
		const PMRect frame = panelView->GetFrame();
		panelView->Resize(PMPoint(frame.Width(), frame.Height() + deltaY));
	}
	else
	{
		// Toggling a section changes what the panel's min and max size are, but those are normally
		// recalculated only during a resize - so force it before asking for one, or the request is
		// clamped to the sizes that were computed for the closed state.
		InterfacePtr<const IWidgetParent> wp(panelView, UseDefaultIID());
		if (wp != nil)
		{
			InterfacePtr<IOWLPaletteSizer> palSizer((IOWLPaletteSizer*)wp->QueryParentFor(IOWLPaletteSizer::kDefaultIID));
			if (palSizer != nil)
				palSizer->UpdateOWLPaletteSizes();
		}

		const SysRect bounds = PaletteRefUtils::GetPaletteBounds(palette);
		SysPoint newSize;
		SetSysPoint(newSize, SysRectWidth(bounds), SysRectHeight(bounds) + deltaY);
		PaletteRefUtils::SetPaletteSize(palette, newSize);
	}
}

} // anonymous namespace

/* KESCMToggleStorySection
*/
void KESCMToggleStorySection()
{
	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;

	InterfacePtr<const IPanelControlData> panelData(panel, UseDefaultIID());
	if (panelData == nil)
		return;

	InterfacePtr<ISplitterPanelControlData> splitter(panelData->FindWidget(kKESCMSplitterWidgetID), UseDefaultIID());
	if (splitter == nil)
		return;

	InterfacePtr<ISplitterPanelController> controller(splitter, UseDefaultIID());
	const int32 designedTop = DesignedTopPaneHeight(splitter);

	if (splitter->IsSinglePanelVisible())
	{
		// Closed: decide how tall to open, show the pane, then make room for it.
		int32 height = SavedSectionHeight(panelData->FindWidget(kKESCMStorySectionWidgetID));
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
			SetSavedSectionHeight(panelData->FindWidget(kKESCMStorySectionWidgetID), sectionHeight);

		splitter->SetPanelVisible(kStorySectionPaneIndex, kFalse);

		// Shrink back to the designed height rather than by the height of the section. The two are
		// the same number until the divider gets dragged, which grows the top pane past the block of
		// controls it holds; subtracting only the section would leave that dead strip behind, and it
		// would still be there every time the panel opened afterwards.
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

	KESCMUpdateStorySectionButtonState();
}

/* KESCMUpdateStorySectionButtonState
*/
void KESCMUpdateStorySectionButtonState()
{
	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;

	InterfacePtr<const IPanelControlData> panelData(panel, UseDefaultIID());
	if (panelData == nil)
		return;

	IControlView* buttonView = panelData->FindWidget(kKESCMStorySectionToggleWidgetID);
	if (buttonView == nil)
		return;

	InterfacePtr<const ISplitterPanelControlData> splitter(panelData->FindWidget(kKESCMSplitterWidgetID), UseDefaultIID());
	if (splitter == nil)
		return;

	// Only one pane showing means the section is closed. linksui picks a mirrored triangle for
	// right-to-left interfaces here; KESCM ships English and Japanese only, so there is nothing to
	// mirror and the stock pair is enough.
	buttonView->SetRsrcID(splitter->IsSinglePanelVisible() ? kTreeBranchCollapsedRsrcID
	                                                       : kTreeBranchExpandedRsrcID);
}

/* KESCMUpdateStorySectionLabel
*/
void KESCMUpdateStorySectionLabel()
{
	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;

	InterfacePtr<const IPanelControlData> panelData(panel, UseDefaultIID());
	if (panelData == nil)
		return;

	InterfacePtr<ITextControlData> label(panelData->FindWidget(kKESCMStorySectionLabelWidgetID), UseDefaultIID());
	if (label == nil)
		return;

	PMString text(kKESCMStorySectionLabelKey);
	text.Translate();

	// ★件数を出すのは比較中だけ。Stop 中は一覧そのものが空なので "(0)" は「変更が無かった」ではなく
	//   「まだ何も比べていない」を意味してしまう ---- 数字を出さないことでその取り違えを断つ
	//   (行の側でも同じ区別をしていて、Stop 中は空、比較中の0件は "No edits" の1行)。
	if (KESCMIsArmed())
	{
		text.Append(" (");
		text.AppendNumber(KESCMStoryList::GetRowCount());
		text.Append(")");
	}

	// ★組み立て終わった文はもう文字列キーではない。翻訳可のままだと、内蔵テーブルにたまたま一致した
	//   瞬間に別の文字列へ化ける(KESCM は "Source:" が「スタイルソース :」になる事故を踏んでいる)。
	text.SetTranslatable(kFalse);

	// 第2引数 invalidate = kTrue。第3引数は notifyOfChange で、ここは表示を書くだけなので kFalse
	//   ---- 誰かに知らせる変更ではない。
	label->SetString(text, kTrue, kFalse);
}

// End, KESCMStorySection.cpp.
