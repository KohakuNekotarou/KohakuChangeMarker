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
#include "IPanelControlData.h"
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
#include "KESCMCore.h"				// KESCMGetVisibleOwnPanel
#include "KESCMStorySection.h"

namespace
{

// The splitter numbers its panes 0 = top, 1 = bottom (ISplitterPanelControlData.h:63).
const int32 kStorySectionPaneIndex = 1;

// How tall the section opens. Matches the lower pane's Frame in KESCM.fr, so the first open looks
// like what the resource describes. Remembering the height the user last left it at comes later.
const int32 kStorySectionDefaultHeight = 100;

/** How tall the section is right now: the splitter's own height less where its bar sits.
	Same expression as LinksUIUtils.cpp:638-639.
*/
int32 CurrentSectionHeight(ISplitterPanelControlData* splitter)
{
	InterfacePtr<const IControlView> splitterView(splitter, UseDefaultIID());
	if (splitterView == nil)
		return 0;

	return ::ToInt32(splitterView->GetFrame().Height()) - splitter->GetSplitterEdge();
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

	if (splitter->IsSinglePanelVisible())
	{
		// Closed: show the pane first, then make room for it.
		splitter->SetPanelVisible(kStorySectionPaneIndex, kTrue);
		ResizePanelByDelta(panel, kStorySectionDefaultHeight);
	}
	else
	{
		// Open: measure BEFORE hiding - once the pane is gone the splitter no longer describes it.
		const int32 sectionHeight = CurrentSectionHeight(splitter);
		splitter->SetPanelVisible(kStorySectionPaneIndex, kFalse);
		ResizePanelByDelta(panel, -sectionHeight);
	}

	InterfacePtr<ISplitterPanelController> controller(splitter, UseDefaultIID());
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

// End, KESCMStorySection.cpp.
