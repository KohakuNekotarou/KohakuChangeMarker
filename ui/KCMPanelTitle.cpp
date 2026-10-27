//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The panel's tab name. The contract is in KCMPanelTitle.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// QueryPanelManager
#include "IControlView.h"		// a panel is a control view = what GetPanelFromWidgetID answers with
#include "IPanelMgr.h"			// GetPanelFromWidgetID / GetPaletteRefContainingPanel
#include "ISession.h"

// General includes:
#include "PaletteRefUtils.h"	// SetPaletteLabel (the label of the tab itself)
#include "PMString.h"
#include "Utils.h"				// Utils<IKCMCompareFacade>()

// Project includes:
#include "KCMUIID.h"				// kKCMPanelWidgetID
#include "KCMBoundaryID.h"		// kKCMDisplayName / KCMCompareMode
#include "IKCMCompareFacade.h"	// GetCompareMode (asking the model across the boundary)
#include "IKCMStoryEditsFacade.h"	// IsImportSource - "Word In" (the same question the list's heading asks)
#include "KCMPanelTitle.h"

namespace
{

/** Put a label on the tab. Does nothing when there is no panel, or when it is in no palette. */
void SetTabLabel(const PMString& label)
{
	// ⚠**During teardown the session can already be gone** (KCMPanelAlpha.cpp receives the pointer
	//   into a variable before using it for the same reason), so the nil is simply tested for. (The
	//   entrance that passed through teardown - Restore() from Shutdown - went on 2026-10-27; the
	//   header says why. The test stays: it costs nothing, and a caller may fire at any time.)
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;

	// Not owned (a Get, not a Query). It is nil until the panel has been opened once, which is the
	// ordinary state right after startup ＝ a caller may fire at any time.
	//
	// ★★**Why not the "only if it is visible" entry point.** Elsewhere this plug-in takes the panel
	//   with `IPanelMgr::GetVisiblePanel` (KCMGetVisibleOwnPanel), which answers nil for a panel
	//   that is not on screen. **The tab name is the one thing that cannot use it** ---- the label
	//   belongs to the palette rather than to the panel’s contents, and it stays on screen when
	//   those contents are not visible (a collapsed palette is exactly "the tab strip and nothing
	//   else"). The mode can still be changed from the flyout in that state.
	IControlView* panelView = panelMgr->GetPanelFromWidgetID(kKCMPanelWidgetID);
	if (panelView == nil)
		return;

	// The label belongs to **the container** (for an ordinary tabbed palette, the
	// kTabPanelContainerType that draws the tab).
	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
	if (!container.IsValid())
		return;

	PaletteRefUtils::SetPaletteLabel(container, label, PaletteRefUtils::kTitle_PanelLabel);
}

}

void KCMPanelTitle::Update()
{
	// ★The separator is a plain ASCII hyphen, as in KBS. A tab is narrow, and a full-width dash
	//   looks stretched there. Keeping to ASCII also stays clear of the problem where a non-ASCII
	//   literal in a .cpp without a BOM is read as CP932.
	// ⚠**GUARDED, because KCMRefreshPanel calls this now** (2026-09-17) - from every comparison
	//   notification, not only from a menu press - and `Utils<T>()->M()` dereferences before anything
	//   can be tested ([[utils-boss-facade-access]]).
	Utils<IKCMCompareFacade> compare;
	if (!compare)
		return;

	PMString title(kKCMDisplayName);
	title.Append(" - ");
	// ★The shorter wording is used: the menu says "Pixel Changes" / "Story Changes" /
	// "Resources Changes", but a tab has no room for that.
	// ⚠**A switch, not a ternary** (2026-09-09). As `== kKCMModeStory ? "Story" : "Pixel"` the tab
	//   called the Resources mode "Pixel" - a label that is not merely unhelpful but wrong, and the
	//   one place a reader looks to see which comparison is running.
	switch (compare->GetCompareMode())
	{
		case kKCMModeStory:
		{
			// ★"Word Import" WHILE A WORD IMPORT IS COMPARED (2026-10-24 night, the author: "during a Word import, make the
			//   panel's title something like Word In, so that it shows" - then "isn't Word In odd English?": Word Import,
			//   the plain name of what is going on). The Source is the copy with the .docx files' words (an import reads
			//   .docx only). Asked as the list's heading asks it for "Proposed Edits" (KCMStorySection.cpp -
			//   IKCMStoryEditsFacade::IsImportSource: armed, and the Source an import's copy), so the tab and the heading
			//   cannot say two things; a Stop takes both back to "Story".
			Utils<IKCMStoryEditsFacade> edits;
			title.Append((edits && edits->IsImportSource()) ? "Word Import" : "Story");
			break;
		}
		case kKCMModeResources:	title.Append("Resources");	break;
		case kKCMModeTrack:		title.Append("Track");		break;
		default:				title.Append("Pixel");		break;
	}
	// ⚠A palette label is treated as **a candidate translation key** as well ＝ without clearing the
	//   translatable flag, a string table that happens to hold the same key would swap it for
	//   another word.
	title.SetTranslatable(kFalse);

	SetTabLabel(title);
}

// End, KCMPanelTitle.cpp.
