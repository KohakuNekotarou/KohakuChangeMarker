//========================================================================================
//
//  KCMScrollMap.cpp
//
//  The scrollbar map: a narrow strip injected at run time just left of a document window's
//  (kLayoutPresentationBoss) vertical scrollbar, showing where the pages carrying a KCM frame are
//  -- Visual Studio's search marks, for pages. The whole record of how this was established
//  against the SDK is docs/ai-notes/scrollbar-minimap.md, relative to the SDK root. In brief:
//    - Where it goes: the presentation's IPanelControlData (kOWLHostedPanelControlDataImpl). The
//      scrollbar boss itself has no IPanelControlData, so the strip cannot be its child.
//    - The standard shape for building a widget at run time and injecting it is
//      open/components/linksui/LinkInfoPanelObserver.cpp (::CreateObject with a RsrcSpec of
//      kViewRsrcType, then AddWidget, SetFrame, SetFrameBinding).
//    - Following a resize is guaranteed by the framework (the contract on
//      IControlView::SetFrameBinding). The most robust binding is a copy of the vertical
//      scrollbar's own GetFrameBinding().
//    - The model for a widget boss that draws itself is customdatalinkui's
//      kCusDtLnkUITreeCViewPanelWidgetBoss (kGenericPanelWidgetBoss plus its own IID_ICONTROLVIEW).
//
//  The strip draws real data: changed pages (sEntries) in red, pages registered as Add/Remove in
//  green. It is display only -- no click-to-navigate (the user's decision), which is why it needs
//  no event handler at all.
//
//  Lifecycle: KCMScrollMapAttach on Start (a comparison begins), KCMScrollMapDetachAll on
//  Stop/Clear. The model posts a notification when a comparison starts or ends and the UI-side
//  KCMModelChangeObserver does the attaching and detaching; the flyout toggle goes through
//  KCMActionComponent. No pointer to a strip is ever held (each one is looked up with FindWidget),
//  so it is safe even once a window and its widgets are gone.
//
//  BUILD-TIME LINK DEPENDENCIES. This file needs two libraries beyond Dolly's defaults (PMRuntime
//  and Public), without which it fails to link with a long list of unresolved symbols:
//      - DV_WidgetBin ... DVControlView, the base class for a view that draws itself
//                         (#include "DVControlView.h")
//      - WidgetBin    ... building a widget at run time with ::CreateObject + kViewRsrcType +
//                         AddWidget
//  On Windows (.vcxproj) both are already in AdditionalDependencies; the copy kept in the repo is
//  under KCM/buildproj/.
//  The Mac needs neither: its equivalent is the single libPublicPlugIn.a, into which everything
//  Windows splits into four (PMRuntime / Public / WidgetBin / DV_WidgetBin) has been folded. The
//  Xcode project of customdatalinkui, the SDK sample that uses DVControlView, links only
//  InDesignModelAndUI.framework and libPublicPlugIn.a.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "IWidgetParent.h"
#include "IDocumentPresentation.h"	// IID_IDOCUMENTPRESENTATION (identifying a document window)
#include "ILayoutViewUtils.h"		// GetAllLayoutViews (both panes of a Split Window, across every window)
// (ILayoutControlData.h is deliberately not included: "which spread is this view showing" is
//  asked in one place only, KCMViewLookup's KCMQuerySpreadUIDForView, so this file queries for it
//  nowhere.)
#include "IMasterSpreadList.h"		// telling a master spread from an ordinary one (which pages go on the map)
#include "IPanorama.h"				// GetBounds (the whole scrollable extent, which is what the scrollbar represents)
#include "IGraphicsPort.h"
#include "IGeometry.h"				// page rectangles (mapped into pasteboard coordinates)
#include "IInterfaceColors.h"		// paint the background in the theme colour (kInterfacePaletteFill)
#include "ISpreadList.h"			// walking the spreads in order (so hidden ones can be left out)
#include "ISpread.h"
#include "IBoolData.h"				// reading a spread's hidden flag (IID_IHIDESPREADBOOLDATA)
#include "SpreadID.h"				// IID_IHIDESPREADBOOLDATA (an IBoolData on kSpreadBoss)

// General includes:
#include "K2Vector.h"
#include "Utils.h"
#include "CreateObject.h"			// ::CreateObject2(db, RsrcSpec)
#include "RsrcSpec.h"
#include "LocaleSetting.h"
#include "DVControlView.h"			// the base class for a view that draws itself (as in customdatalinkui)
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "LayoutUIID.h"				// kVertScrollBarWidgetID / kLayoutWidgetID (naming the layout view outright)
#include "CoreResTypes.h"			// kViewRsrcType
#include "IGeometryFacade.h"		// GetItemBounds (page rectangles in pasteboard coordinates; modelled on SnapTracker)
#include <algorithm>				// std::find (spotting a presentation already seen)
#include <vector>
#include <set>
#include <chrono>					// steady_clock, for throttling the manual Hide/Show detection.
									// A monotonic wall clock means the same thing on Windows and
									// on the Mac, whereas std::clock does not (wall time on
									// Windows, CPU time on POSIX).

// Project includes:
#include "KCMUIID.h"
#include "KCMScrollMap.h"
#include "IKCMCompareFacade.h"	// the armed state
#include "IKCMMarkData.h"			// changed / overflow / overset pages (the source of the red
									// shades) plus GetRegisteredPages (Add/Remove registrations,
									// the green marks)
#include "KCMViewLookup.h"		// KCMQuerySpreadUIDForView -- the one place that answers "which
									// spread is this view showing"'

// The strip's width in px; it sits along the left edge of the vertical scrollbar. Kept narrow on
// purpose -- clicking the bar itself is enough to move, so the strip is there to be read.
static const PMReal kKCMScrollMapWidth = 5.0;

// The opacity of a band, 0 to 1. The blend is done by hand -- colour' = a x mark + (1-a) x
// background -- which works because a band is only ever drawn on top of the background this view
// painted itself. IGraphicsPort::setopacity would do it too; mixing by hand does not depend on
// how the port composites transparency, and looks identical.
// The background follows the theme (IInterfaceColors' kInterfacePaletteFill), so the result sits
// well in both the light and the dark UI.
// This is the opacity of the frame bands (changed pages, red) and the registered ones (green).
static const PMReal kKCMScrollMapMarkAlpha     = 0.4;	// frames are meant to be clearly visible
// The opacity of an overflow "/" page (one with no counterpart): deliberately fainter than a
// frame, so the two reds are told apart.
static const PMReal kKCMScrollMapOverflowAlpha = 0.15;	// mixed well into the background
// The opacity of a Find Overset band: less of the background mixed in than a changed band, so
// the red comes out stronger.
static const PMReal kKCMScrollMapOversetAlpha  = 0.85;	// barely mixed with the background

// How far the track is pulled in, in px, on top of the arrow buttons: the map is drawn inside a
// range narrower than the inside of the buttons by this much at each end. The thumb's real range
// of travel IS narrower than that (there is padding beyond the buttons at both ends of the bar),
// and subtracting this is what lines the bands up with the thumb best. Tuned on a live build.
// Set it to 0.0 to draw right up to the inside of the arrow buttons (what it did before this
// existed). It takes effect in exactly one place, in the mapping inside Draw, where it applies to
// trackTop and trackBottom alike -- so the pull-in is always symmetrical.
static const PMReal kKCMScrollMapTrackInset = 8.0;

// Whether the scrollbar map is on: the "Show Scrollbar Map" toggle in the flyout, on by default.
// While it is off, Attach and NoticeDrawEvent return at once, so no strip is injected and no
// fingerprint is computed on every draw. Removing the strips that already exist when the toggle
// goes off is the caller's job (KCMActionComponent calls DetachAll).
static bool16 sScrollMapOn = kTrue;

//========================================================================================
// KCMScrollMapView -- the strip draws itself (an IControlView implementation)
//========================================================================================

class KCMScrollMapView : public DVControlView
{
	typedef DVControlView inherited;

public:
	KCMScrollMapView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMScrollMapView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KCMScrollMapView, kKCMScrollMapViewImpl)

// Read, at run time and from the strip's own window, the measurements that align the mapping
// with the scrollbar.
//   outArrowH  = the height of the vertical scrollbar's arrow buttons. The buttons are square, so
//                the bar's frame WIDTH is that height (captured on a live build: a 16px button
//                area on a 15px-wide bar). Deriving it from the bar means it follows a change of
//                UI scale; the public constant kCC2017SpectrumScrollBarWidth is fixed, so it is
//                not used.
//   outPanoTop / outPanoBottom = the whole scrollable extent of the panorama, in content
//                coordinates. What the full length of the scrollbar stands for is THIS, not the
//                extent of the pages (it includes the pasteboard margins above and below them).
// Either can fail to come out (a window built differently from what is assumed, or another
// coordinate system), so success is reported back and the caller falls back to the older mapping
// for whichever it did not get -- never worse than before.
// The strip is injected as a sibling of the scrollbar, so walking up to the parent finds both the
// bar and the layout view (the layout view is the only one with an IPanorama). Nothing is cached:
// every Draw looks them up again, which is what makes it safe when the window has gone.
// The layout view of the PANE this strip sits in; nil when it cannot be found.
//
// THE ONE PLACE THAT ANSWERS "which pane is this strip in". The strip is injected as a sibling of
// the vertical scrollbar, so walking up to the parent always lands in its own pane.
// @warning in a Split Window ONE PRESENTATION CARRIES TWO LAYOUT VIEWS -- ILayoutViewUtils says
//   GetAllLayoutViews "will return both layout views in a split layout view if both shown". So
//   taking "the first view that matches" per presentation can return the NEIGHBOURING PANE'S
//   answer.
//
// This used to be two answers to one question: "which spread is being shown" was answered by a
// separate function that matched on the presentation, while the panorama used for the Y
// denominator came through the path below. MEASURED, THE TWO AGREED -- with a Split Window whose
// panes showed different spreads (views [245, 238], both answers 245) -- so it was never a
// defect. But they agreed by a double coincidence: the strip always goes next to the PRIMARY
// pane's vertical scrollbar, and GetAllLayoutViews happens to return the primary pane first.
// Either of those changing would make it drift silently (with one pane on a master, the pages
// put on the map and the Y denominator would come from different panes). So while the behaviour
// was still the same, the question was given one answer.
static IControlView* KCMStripLayoutView(IControlView* strip)
{
	InterfacePtr<IWidgetParent> wp(strip, IID_IWIDGETPARENT);
	if (wp == nil)
		return nil;
	InterfacePtr<IPanelControlData> parentPanel(wp->GetParent(), UseDefaultIID());
	if (parentPanel == nil)
		return nil;

	// The layout view is looked up by WidgetID, as the shipping spellpanel/PrivateSpellingUtils.cpp
	// does for the secondary pane.
	// @warning the primary-pane line in that same function passes FindWidget a CLASS ID
	//   (kLayoutWidgetBoss) instead. kLayoutWidgetBoss (kClassIDSpace, kLayoutUIPrefix + 3) and
	//   kLayoutWidgetID (kWidgetIDSpace, the same + 3) merely happen to hold the same number, so
	//   the form to copy is the secondary pane's kLayoutWidgetID.
	// When that finds nothing, fall back to walking the siblings, so a window built differently
	// from what is assumed is never worse off than before.
	IControlView* layoutView = parentPanel->FindWidget(kLayoutWidgetID);
	if (layoutView != nil)
		return layoutView;

	const int32 numSiblings = parentPanel->Length();
	for (int32 i = 0; i < numSiblings; ++i)
	{
		IControlView* sib = parentPanel->GetWidget(i);
		if (sib == nil || sib == strip)
			continue;	// the vertical scrollbar has no panorama, so the test below drops it anyway
		InterfacePtr<IPanorama> sibPano(sib, UseDefaultIID());
		if (sibPano != nil)
			return sib;	// the first sibling with a panorama is the layout view
	}
	return nil;
}

static void KCMScrollMapProbeWindow(IControlView* strip, PMReal& outArrowH,
									  PMReal& outPanoTop, PMReal& outPanoBottom, bool16& outHasPano)
{
	outArrowH = 0;
	outPanoTop = outPanoBottom = 0;
	outHasPano = kFalse;

	InterfacePtr<IWidgetParent> wp(strip, IID_IWIDGETPARENT);
	if (wp == nil)
		return;
	InterfacePtr<IPanelControlData> parentPanel(wp->GetParent(), UseDefaultIID());
	if (parentPanel == nil)
		return;

	IControlView* sbView = parentPanel->FindWidget(kVertScrollBarWidgetID);
	if (sbView != nil)
		outArrowH = sbView->GetFrame().Width();

	// Keep the IPanorama query and its nil test: what is wanted is a view WITH A PANORAMA, not a
	// widget with a particular name -- being found by WidgetID says nothing about whether a
	// panorama is on it. Bounds that make no sense are left to the older mapping.
	InterfacePtr<IPanorama> panorama(KCMStripLayoutView(strip), UseDefaultIID());	// nil is allowed here (InterfacePtr)
	if (panorama != nil)
	{
		const PMRect bounds = panorama->GetBounds();
		if (bounds.Bottom() > bounds.Top())
		{
			outPanoTop    = bounds.Top();
			outPanoBottom = bounds.Bottom();
			outHasPano    = kTrue;
		}
	}
}

// kTrue when spreadUID is one of this db's master spreads.
// IMasterSpreadList::GetMasterSpreadIndex(UID) is deliberately NOT used: the header does not say
// what it returns for a UID that is not a master ("Return the index" is all it says). Rather than
// build a test on an unpromised negative, match them here. There are rarely many masters, so this
// is cheap.
static bool16 KCMIsMasterSpread(IDataBase* db, UID spreadUID)
{
	if (db == nil || spreadUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IMasterSpreadList> ml(db, db->GetRootUID(), UseDefaultIID());
	if (ml == nil)
		return kFalse;
	const int32 nm = ml->GetMasterSpreadCount();
	for (int32 m = 0; m < nm; ++m)
		if (ml->GetNthMasterSpreadUID(m) == spreadUID)
			return kTrue;
	return kFalse;
}

// Drawing the real data. Display only -- there is no click-to-navigate (the user's decision).
//   - the background is the theme colour (kInterfacePaletteFill)
//   - changed pages (sEntries) are filled red
//   - pages registered as Add/Remove (IKCMMarkData::GetRegisteredPages, backed on the model side
//     by KCMPageMapCollectRegistered) are filled green
//
// THE MAPPING IS RELATIVE TO THE WHOLE DOCUMENT, the way VS does it, and it is aligned to the
// real scrollbar: the vertical range is not the strip's full height but THE TRACK THE THUMB CAN
// TRAVEL (inside the arrow buttons at both ends), and the Y denominator is not the extent of the
// page rectangles but THE WHOLE SCROLLABLE EXTENT OF THE PANORAMA (IPanorama::GetBounds, which
// includes the pasteboard margins). That puts a band and the thumb on the same scale; the mapping
// section below has the detail. Each page's Y band becomes a mark (at least 3px tall).
// Nothing here depends on the scroll position or the zoom, so a redraw is only needed when the
// comparison result changes (KCMScrollMapInvalidateAll).
// Spreads that are hidden (Hide Unchanged and the like) are left out when the pages are collected
// -- see below -- so even while hiding is in use the normalisation uses only the coordinates of
// the spreads on screen and the marks line up with what is displayed.
void KCMScrollMapView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;

	AutoGSave autoGSave(gPort);

	const PMRect frame(this->GetInnerContentFrame());

	// The background: the theme colour, or a mid grey when it cannot be read. A session that has
	// gone nil during teardown is fine -- InterfacePtr(p, iid) allows a nil pointer, so colors ends
	// up nil and the grey below is used.
	PMReal bgR(0.5), bgG(0.5), bgB(0.5);
	{
		InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), IID_IINTERFACECOLORS);
		if (colors != nil)
		{
			RealAGMColor bg;
			colors->GetRealAGMColor(kInterfacePaletteFill, bg);
			bgR = bg.red; bgG = bg.green; bgB = bg.blue;
		}
	}
	gPort->setrgbcolor(bgR, bgG, bgB);
	gPort->rectpath(frame);
	gPort->fill();

	// Identify the document this strip's window belongs to (the presentation's GetDocumentUIDRef)
	// and take the marks from the Target or from the Source accordingly (the Source window shows
	// them too, at the user's request). A window that is neither, or unarmed, or already closed,
	// gets the background only.
	// The presentation is queried here ONLY to learn the document. "Which spread is being shown"
	// is a question about the PANE, and it is answered by KCMStripLayoutView.
	InterfacePtr<IWidgetParent> stripParent(this, IID_IWIDGETPARENT);
	InterfacePtr<IDocumentPresentation> stripPres(
		stripParent != nil ? (IDocumentPresentation*)stripParent->QueryParentFor(IID_IDOCUMENTPRESENTATION) : nil);
	IDataBase* const db = (stripPres != nil) ? stripPres->GetDocumentUIDRef().GetDataBase() : nil;
	// The facade is asked several times in this one Draw, so it is queried into an InterfacePtr
	// once (Utils.h says to do that rather than pay for a query per call). This runs on every
	// redraw of the strip, the same as marks below.
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	const bool16 isTarget = (db != nil && db == compare->GetArmedTargetDB());
	const bool16 isSource = (!isTarget && db != nil && db == compare->GetArmedSourceDB());
	// The Find Overset bands are independent of the comparison: the window of the scanned document
	// gets a red band on its overset pages whether or not anything is armed, so a strip appears
	// even when only the overset check is running. When it is the same document as the comparison,
	// the two reds simply overlap.
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	const bool16 isOverset = (db != nil && marks->GetOversetOn() &&
		db == marks->GetOversetDB());
	if ((!isTarget && !isSource && !isOverset) || !compare->IsDocDBOpen(db))
		return;

	// Collect every page's pasteboard Y band, in spread order and then page order.
	// HIDDEN SPREADS (Hide Unchanged Spreads, or Hide Spread from the Pages panel) ARE LEFT OUT:
	// hiding re-flows the spreads that remain on screen while a hidden one keeps its old
	// coordinates, so including them dirties the normalisation and every mark shifts (reported
	// from a live build). The hit test in KCMFindPageUnderMouse leaves them out for the same
	// reason, with the same test.
	// THIS IS DELIBERATELY NOT MOVED ONTO IPageList. For a flat list of pages IPageList is the
	// official route, and the model-side page collection was moved onto it. But IPageList INCLUDES
	// the pages of hidden spreads and offers no way to exclude them (measured; its
	// includePagesOfHiddenSpread parameter is on GetPageIndex and nowhere else). This map only
	// works if hidden pages stay off it, so it needs the ISpreadList route, where each spread can
	// be asked for its IID_IHIDESPREADBOOLDATA. The asymmetry is intentional, not an oversight.
	// WHICH PAGES GO ON THE MAP DEPENDS ON THE SPREAD THIS WINDOW IS SHOWING. A master spread
	// lives in a different coordinate space from the ordinary ones, so putting ordinary pages on
	// the map of a window showing a master gives bands that do not agree with the Y denominator
	// (the panorama's extent, which is then the master's) and land somewhere else entirely. While
	// a master is shown, only that master spread's pages go on. With no frame and no overset there
	// they simply come out empty, which is right.
	const UID shownSpread = KCMQuerySpreadUIDForView(KCMStripLayoutView(this));
	const bool16 showingMaster = KCMIsMasterSpread(db, shownSpread);

	std::vector<UID> pages;
	if (showingMaster)
	{
		// No hidden-flag test on the master side: a master spread is not what Hide Spread hides.
		InterfacePtr<ISpread> spread(db, shownSpread, UseDefaultIID());
		if (spread == nil)
			return;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			pages.push_back(spread->GetNthPageUID(p));
	}
	else
	{
		InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
		if (spreadList == nil)
			return;
		const int32 ns = spreadList->GetSpreadCount();
		for (int32 s = 0; s < ns; ++s)
		{
			const UID spreadUID = spreadList->GetNthSpreadUID(s);
			InterfacePtr<IBoolData> hideFlag(db, spreadUID, IID_IHIDESPREADBOOLDATA);
			if (hideFlag != nil && hideFlag->GetBool())
				continue;	// a hidden spread does not go on the map (scrolling cannot reach it either)
			InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
			if (spread == nil)
				continue;
			const int32 np = spread->GetNumPages();
			for (int32 p = 0; p < np; ++p)
				pages.push_back(spread->GetNthPageUID(p));
		}
	}
	if (pages.empty())
		return;

	std::vector<PMReal> tops(pages.size()), bottoms(pages.size());
	PMReal minY(0), maxY(0);
	bool16 first = kTrue;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		tops[i] = bottoms[i] = PMReal(0);
		InterfacePtr<IGeometry> geo(db, pages[i], UseDefaultIID());
		if (geo == nil)
			continue;
		// Getting a page rectangle in pasteboard coordinates is the facade's job; SnapTracker.cpp
		// does exactly this TO A PAGE. (The older code built the same answer out of
		// GetPathBoundingBox + ::InnerToPasteboardMatrix + a Transform of its own.)
		// The nil test above stays: the facade does not promise that a given UID really has
		// geometry, and the model does the same test in the same order.
		// The swap below stays too: the older code normalised the rectangle as a side effect, and
		// IGeometryFacade never says the rectangle it returns is normalised.
		const PMRect box = Utils<Facade::IGeometryFacade>()->GetItemBounds(
			::GetUIDRef(geo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
		PMReal a = box.Top(), b = box.Bottom();
		if (b < a) { PMReal t = a; a = b; b = t; }
		tops[i] = a; bottoms[i] = b;
		if (first) { minY = a; maxY = b; first = kFalse; }
		else { if (a < minY) minY = a; if (b > maxY) maxY = b; }
	}
	if (first || maxY <= minY)
		return;

	// What gets marked. The reds come from:
	//   the Target window = changed pages (sEntries) + overflow (sOverflowT, pages with no
	//     counterpart and no registration, drawn as "/")
	//   the Source window = the Source side of each changed pair (the keys of sSrcPageToTarget) +
	//     overflow (sOverflowS)
	// Green = pages registered as Add/Remove in that db. A page that is both is drawn red.
	// Bringing the overflow cache into line with the current pair of documents happens inside
	// IsOverflowPage, which also matches which pair the cache belongs to -- from here the only
	// question asked is "is this page an overflow one". That realignment does not write sDB or
	// sSrcDB, so it cannot change the answer engineMatch already holds.
	const bool16 engineMatch = isTarget ? (marks->GetMarkedTargetDB() == db)
	                                    : (marks->GetMarkedSourceDB() == db);
	std::set<UID> greens;
	marks->GetRegisteredPages(db, greens);

	// The band colours, mixed with the theme background so they read as translucent. The two reds
	// use different alphas -- a frame is meant to stand out, an overflow "/" to stay faint. Green
	// (registered) uses the frame's alpha.
	const PMReal ma = kKCMScrollMapMarkAlpha;
	const PMReal oa = kKCMScrollMapOverflowAlpha;
	const PMReal redR = ma * PMReal(0.85) + (PMReal(1.0) - ma) * bgR;	// frame (changed) = red
	const PMReal redG = ma * PMReal(0.08) + (PMReal(1.0) - ma) * bgG;
	const PMReal redB = ma * PMReal(0.08) + (PMReal(1.0) - ma) * bgB;
	const PMReal ovrR = oa * PMReal(0.85) + (PMReal(1.0) - oa) * bgR;	// overflow "/" = faint red
	const PMReal ovrG = oa * PMReal(0.08) + (PMReal(1.0) - oa) * bgG;
	const PMReal ovrB = oa * PMReal(0.08) + (PMReal(1.0) - oa) * bgB;
	const PMReal osa = kKCMScrollMapOversetAlpha;						// overset = deep red (barely mixed)
	const PMReal ovsR = osa * PMReal(0.85) + (PMReal(1.0) - osa) * bgR;
	const PMReal ovsG = osa * PMReal(0.08) + (PMReal(1.0) - osa) * bgG;
	const PMReal ovsB = osa * PMReal(0.08) + (PMReal(1.0) - osa) * bgB;
	const PMReal grnR = ma * PMReal(0.10) + (PMReal(1.0) - ma) * bgR;	// registered = green
	const PMReal grnG = ma * PMReal(0.70) + (PMReal(1.0) - ma) * bgG;
	const PMReal grnB = ma * PMReal(0.25) + (PMReal(1.0) - ma) * bgB;

	// ALIGN THE MAPPING WITH THE REAL SCROLLBAR. Mapping "the whole Y extent of the page
	// rectangles" onto "the strip's full height" disagrees with the bar on two counts:
	//   1. the thumb can only travel INSIDE the arrow buttons, while the strip was drawn over the
	//      bar's full height -- a systematic drift of +buttonHeight at the top, zero in the middle
	//      and -buttonHeight at the bottom. In the capture that showed it, the band for page 1 sat
	//      level with the up arrow, where the thumb can never be.
	//   2. the bar's full length stands for THE WHOLE SCROLLABLE EXTENT OF THE PANORAMA, not the
	//      extent of the pages (it takes in the pasteboard margins above and below them).
	// The first comes from the bar's frame width (its square buttons' height), the second from
	// IPanorama::GetBounds(); both are read at run time.
	PMReal arrowH(0), panoTop(0), panoBottom(0);
	bool16 hasPano = kFalse;
	KCMScrollMapProbeWindow(this, arrowH, panoTop, panoBottom, hasPano);

	// The bar's frame is in the parent's coordinates while frame (GetInnerContentFrame) is in the
	// strip's own. They are normally 1:1; converting through the ratio to the strip's outer height
	// covers the case where the vertical scales differ.
	const PMReal outerH = this->GetFrame().Height();
	if (outerH > 0 && frame.Height() > 0)
		arrowH = arrowH * frame.Height() / outerH;

	// Pull the track in (the value is on the declaration). The thumb's range of travel is a little
	// narrower still than the inside of the arrow buttons, so that much is taken off before the
	// mapping.
	arrowH = arrowH + kKCMScrollMapTrackInset;

	PMReal trackTop    = frame.Top() + arrowH;		// where the thumb can travel = where the map should be drawn
	PMReal trackBottom = frame.Bottom() - arrowH;
	if (trackBottom - trackTop < PMReal(8.0))		// a very short window, or no bar: give up the correction and use the full height
	{
		trackTop    = frame.Top();
		trackBottom = frame.Bottom();
	}

	// The denominator. Use the panorama's extent when it contains the extent of the pages; when it
	// does not -- another coordinate system, or a disagreement caused by hidden spreads -- fall
	// back to the extent of the page rectangles as before.
	PMReal spanTop = minY, spanBottom = maxY;
	if (hasPano && panoBottom > panoTop &&
		panoTop <= minY + PMReal(1.0) && panoBottom >= maxY - PMReal(1.0))
	{
		spanTop    = panoTop;
		spanBottom = panoBottom;
	}

	const PMReal scale = (trackBottom - trackTop) / (spanBottom - spanTop);

	// Decide each page's colour class and band coordinates (y0/y1) first, and sort the indices into
	// per-priority lists (byLevel). The two pages of a spread (4 and 5, say) share a pasteboard Y
	// band, so drawing them in plain page order lets the later page paint over the earlier one
	// (reported from a live build: with 4 overset and 5 changed, the changed colour covered the
	// overset one). Drawing the low priorities first and the high ones last means the higher
	// priority always wins. Priority within ONE page is settled by choosing a single level for it;
	// only the overlap between different pages is left to the drawing order.
	// The levels: 1 = registered (green) / 2 = overflow "/" (faint red) / 3 = changed (red) /
	// 4 = overset (deep red).
	std::vector<size_t> byLevel[5];	// [1..4] hold the page indices at that level (0 is unused); N fills in total
	std::vector<PMReal> y0s(pages.size()), y1s(pages.size());
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (bottoms[i] <= tops[i])
			continue;	// a page whose geometry could not be read
		bool16 isRed = kFalse;			// red from a frame (a changed page)
		bool16 isOverflowRed = kFalse;	// red from an overflow "/" (a fainter red than a frame)
		if (engineMatch)
		{
			if (isTarget)
				isRed = marks->HasEntryForPage(pages[i]);
			else
				isRed = marks->IsSourcePageMarked(pages[i]);
		}
		if (!isRed && marks->IsOverflowPage(db, pages[i], isTarget))
		{
			isRed = kTrue;			// only a pure overflow (not a change) gets the faint red
			isOverflowRed = kTrue;	// a changed page that is also an overflow was settled above: the frame colour wins
		}
		const bool16 isGreen = (!isRed && greens.find(pages[i]) != greens.end());
		// An overset page (when this document is the scanned one) is a strong red, to match the "+"
		// mark.
		const bool16 isOversetRed = (isOverset && marks->IsOversetPage(pages[i]));

		int32 c = 0;
		if (isOversetRed)   c = 4;					// overset wins: drawn last, so it lands on top
		else if (isRed)     c = isOverflowRed ? 2 : 3;	// changed = 3, a pure overflow "/" = 2
		else if (isGreen)   c = 1;					// registered (green)
		if (c == 0)
			continue;

		PMReal y0 = trackTop + (tops[i]    - spanTop) * scale;
		PMReal y1 = trackTop + (bottoms[i] - spanTop) * scale;
		if (y1 - y0 < PMReal(3.0))	// too thin to see: keep the centre and make it 3px
		{
			const PMReal cy = (y0 + y1) / PMReal(2.0);
			y0 = cy - PMReal(1.5);
			y1 = cy + PMReal(1.5);
		}
		if (y0 < trackTop)    y0 = trackTop;		// never level with an arrow button, where the thumb cannot go
		if (y1 > trackBottom) y1 = trackBottom;

		y0s[i] = y0;
		y1s[i] = y1;
		byLevel[c].push_back(i);	// c is 1..4 (0 was skipped above)
	}

	// Draw from the lowest priority to the highest, so the higher one comes last and wins wherever
	// two pages of a spread overlap. The colour is set once per level, and only the indices in
	// byLevel are walked, so this is N fills in total.
	for (int32 level = 1; level <= 4; ++level)
	{
		if (byLevel[level].empty())
			continue;
		switch (level)
		{
			case 1: gPort->setrgbcolor(grnR, grnG, grnB); break;	// registered = green
			case 2: gPort->setrgbcolor(ovrR, ovrG, ovrB); break;	// a pure overflow "/" = faint red
			case 3: gPort->setrgbcolor(redR, redG, redB); break;	// changed = a clear red
			case 4: gPort->setrgbcolor(ovsR, ovsG, ovsB); break;	// overset = deep red (the highest priority)
		}
		for (size_t k = 0; k < byLevel[level].size(); ++k)
		{
			const size_t i = byLevel[level][k];
			gPort->rectpath(PMRect(frame.Left(), y0s[i], frame.Right(), y1s[i]));
			gPort->fill();
		}
	}
}

//========================================================================================
// Injection and removal
//========================================================================================

// Collect, without duplicates, the document windows (presentations) behind this db's layout
// views. What lands in out is each presentation's IPanelControlData, already addref'd.
// The IControlView* values GetAllLayoutViews returns are treated as unowned, as everywhere else
// in KCM.
static void KCMCollectPresentationPanels(IDataBase* db, K2Vector<IPanelControlData*>& out)
{
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);

	K2Vector<IPMUnknown*> seen;	// identity of a presentation (all QI results for one IID, so pointers compare)
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<IWidgetParent> wp(views[i], IID_IWIDGETPARENT);
		if (wp == nil)
			continue;
		InterfacePtr<IDocumentPresentation> pres(
			(IDocumentPresentation*)wp->QueryParentFor(IID_IDOCUMENTPRESENTATION));
		if (pres == nil)
			continue;

		// The linear search is written with std::find, as the shipping code does over a K2Vector in
		// open/components/incopyfileactions/InCopyDocFileHandler.cpp.
		IPMUnknown* const presKey = (IPMUnknown*)(IDocumentPresentation*)pres;
		if (std::find(seen.begin(), seen.end(), presKey) != seen.end())
			continue;
		seen.push_back(presKey);

		InterfacePtr<IPanelControlData> panel(pres, UseDefaultIID());
		if (panel == nil)
			continue;
		panel->AddRef();
		out.push_back(panel);
	}
}

// KCMScrollMapAttach (declared in KCMScrollMap.h) -- inject a strip into each of targetDB's
// document windows.
void KCMScrollMapAttach(IDataBase* targetDB)
{
	if (!sScrollMapOn)
		return;	// with "Show Scrollbar Map" off, no strip is injected (a Start shows no map)
	if (targetDB == nil)
		return;

	K2Vector<IPanelControlData*> panels;
	KCMCollectPresentationPanels(targetDB, panels);

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// takes ownership (this is what releases it)

		// one strip per window
		if (presPanel->FindWidget(kKCMScrollMapWidgetID) != nil)
			continue;

		// Find the vertical scrollbar (FindWidget recurses through every descendant by default).
// A window without one is skipped.
		IControlView* sbView = presPanel->FindWidget(kVertScrollBarWidgetID);
		if (sbView == nil)
			continue;

			// The strip is added to the scrollbar's IMMEDIATE parent, so that it shares the bar's
			// coordinate system and follows a resize the same way.
		InterfacePtr<IWidgetParent> sbWP(sbView, IID_IWIDGETPARENT);
		if (sbWP == nil)
			continue;
		InterfacePtr<IPanelControlData> sbParentPanel(sbWP->GetParent(), UseDefaultIID());
		if (sbParentPanel == nil)
			continue;

			// Built at run time, in the standard shape linksui uses; the db is the same UI database
			// the parent widgets are in. The typed ::CreateObject2<IControlView>(db, spec) is what
			// the shipping widgetbin/treeview/CTreeViewWidgetMgr.cpp uses, line for line. That
			// form passes no IID and uses FACE::kDefaultIID, which for IControlView is
			// IID_ICONTROLVIEW -- the same interface the older C-style cast asked for.
		InterfacePtr<IControlView> strip(::CreateObject2<IControlView>(
			::GetDataBase(sbParentPanel),
			RsrcSpec(LocaleSetting::GetLocale(), kKCMUIPluginID, kViewRsrcType, kKCMScrollMapRsrcID)));
		if (strip == nil)
			continue;

		sbParentPanel->AddWidget(strip);	// appended, so it draws in front

			// Left of the bar and the same height, in the same parent-local coordinates. The binding
			// is copied from the bar (which should amount to "pinned right, stretching vertically"
			// -- what it actually holds can be read with a probe).
		const PMRect sbFrame = sbView->GetFrame();
		const PMReal stripLeft = sbFrame.Left() - kKCMScrollMapWidth;
		PMRect stripFrame(stripLeft, sbFrame.Top(), sbFrame.Left(), sbFrame.Bottom());
		strip->SetFrame(stripFrame);
		strip->SetFrameBinding(sbView->GetFrameBinding());
		strip->ShowView();
		strip->Invalidate();

			// CLAIM THE STRIP'S COLUMN FROM THE LAYOUT VIEW (a smearing fix confirmed on a live
			// build). The layout view speeds scrolling up by blitting the screen pixels sideways
			// or up and down, so a strip overlapping the view's area gets copied along with them
			// and smears. So any sibling whose right edge reaches into the strip's column -- that
			// is, the layout view -- has its right edge pulled back to the strip's left, leaving no
			// overlap at all. Only siblings that overlap the vertical band of the scrollbar are
			// touched: the horizontal scrollbar at the bottom and the ruler at the top do not
			// overlap it vertically and are left alone. Detach puts this back.
		const int32 numSiblings = sbParentPanel->Length();
		for (int32 c = 0; c < numSiblings; ++c)
		{
			IControlView* sib = sbParentPanel->GetWidget(c);
			if (sib == nil || sib == sbView || sib == (IControlView*)strip)
				continue;
			PMRect sf = sib->GetFrame();
			if (sf.Right() > stripLeft && sf.Left() < stripLeft &&
				sf.Top() < sbFrame.Bottom() && sf.Bottom() > sbFrame.Top())
			{
				sf.Right() = stripLeft;
				sib->SetFrame(sf);
				sib->Invalidate();
			}
		}
	}
}

// KCMScrollMapDetachAll (declared in KCMScrollMap.h) -- remove the strip from every window of
// every document.
void KCMScrollMapDetachAll()
{
	K2Vector<IPanelControlData*> panels;
	KCMCollectPresentationPanels(nil, panels);	// db = nil gathers every layout view

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// takes ownership (this is what releases it)

		IControlView* strip = presPanel->FindWidget(kKCMScrollMapWidgetID);
		if (strip == nil)
			continue;

		// Removed from its immediate parent panel, with deleteUID=kTrue so it goes out of the UI
		// database too -- the same form as the shipping
		// linksui/AddDeleteCaptionRowButtonObserver.cpp.
		InterfacePtr<IWidgetParent> wp(strip, IID_IWIDGETPARENT);
		if (wp == nil)
			continue;
		InterfacePtr<IPanelControlData> parentPanel(wp->GetParent(), UseDefaultIID());
		if (parentPanel == nil)
			continue;

		// Give back the width taken from the sibling (the layout view) whose right edge Attach
		// pulled in. It is identified as "the right edge sits (all but exactly) on the strip's left
		// edge, and the vertical bands overlap", and it is widened back out to the strip's right
		// edge -- the scrollbar's left.
		const PMRect stripFrame = strip->GetFrame();
		const int32 numSiblings = parentPanel->Length();
		for (int32 c = 0; c < numSiblings; ++c)
		{
			IControlView* sib = parentPanel->GetWidget(c);
			if (sib == nil || sib == strip)
				continue;
			PMRect sf = sib->GetFrame();
			const PMReal gap = abs(sf.Right() - stripFrame.Left());
			if (gap <= PMReal(0.5) &&
				sf.Top() < stripFrame.Bottom() && sf.Bottom() > stripFrame.Top())
			{
				sf.Right() = stripFrame.Right();
				sib->SetFrame(sf);
				sib->Invalidate();
			}
		}

		parentPanel->RemoveWidget(strip, kTrue, kTrue);
	}
}

// KCMScrollMapInvalidateAll (declared in KCMScrollMap.h) -- redraw every injected strip.
// The callers are spread across the UI side, and that is the point: THERE IS NO ONE PLACE WHERE A
// COMPARISON CHANGING CAN BE CAUGHT, so each independent path calls this for itself.
//   - KCMModelChangeObserver ... a full recomparison, a partial one, an overset scan, a close
//   - KCMActionComponent     ... the map toggle going on, and Find Overset
//   - KCMPeekGesture        ... a batch close finishing
//   - this file itself       ... KCMScrollMapNoticeDrawEvent below, which catches the manual
//     Hide/Show and the spread switch that none of the others can see
// Anything that becomes another such path has to call this as well.
void KCMScrollMapInvalidateAll()
{
	K2Vector<IPanelControlData*> panels;
	KCMCollectPresentationPanels(nil, panels);	// db = nil gathers every layout view

	for (int32 i = 0; i < (int32)panels.size(); ++i)
	{
		InterfacePtr<IPanelControlData> presPanel(panels[i]);	// takes ownership (this is what releases it)
		IControlView* strip = presPanel->FindWidget(kKCMScrollMapWidgetID);
		if (strip != nil)
			strip->Invalidate();
	}
}

//========================================================================================
// Detecting a manual Hide/Show Spread (riding the spread draw event, throttled)
//========================================================================================

// A fingerprint of this db's spread layout and hidden flags. Hiding, showing, or adding and
// removing spreads always changes it. A db that is nil or already closed gives 0, so once
// everything is unarmed both fingerprints settle at 0 and always compare equal.
static uint32 KCMHiddenFingerprint(IDataBase* db)
{
	if (db == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(db))
		return 0;
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return 0;
	uint32 h = 0;
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID uid = spreadList->GetNthSpreadUID(s);
		InterfacePtr<IBoolData> hideFlag(db, uid, IID_IHIDESPREADBOOLDATA);
		const uint32 hidden = (hideFlag != nil && hideFlag->GetBool()) ? 1u : 0u;
		h = h * 131u + (uid.Get() << 1) + hidden;
	}
	return h;
}

// A fingerprint of which master spread this db's windows are showing.
// What goes on the map changes with the spread on screen (while a master is shown, only that
// master's pages), and switching spreads goes through none of KCM's hooks. A redraw always
// happens, so this rides the same path the hidden flags do.
// Ordinary spreads all fold to 0: moving between them leaves the map holding every page and its
// contents unchanged, so invalidating there would only cost a redraw for nothing.
static uint32 KCMShownMasterFingerprint(IDataBase* db)
{
	if (db == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(db))
		return 0;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	uint32 h = 0;
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		// "Which spread is this view showing" is asked in one place, KCMQuerySpreadUIDForView.
		// When it cannot be answered the result is kInvalidUID, which counts as "not a master".
		const UID shown = KCMQuerySpreadUIDForView(views[i]);	// a nil view is dropped inside
		h = h * 131u + (KCMIsMasterSpread(db, shown) ? shown.Get() : 0u);
	}
	return h;
}

static std::chrono::steady_clock::time_point sHiddenCheckLast;	// when it was last checked (for the throttle)
// default-constructed time_point)
static bool16 sHiddenCheckStarted = kFalse;	// has it been checked at all? (the first one always runs, rather than comparing against a
// The fingerprint combines the hidden-flag layout with the master spread on screen. Either one
// changing changes what the map holds, so they are folded into a single number and compared as
// one.
static uint32 sHiddenFingerT = 0;			// the Target side, as last seen
static uint32 sHiddenFingerS = 0;			// the Source side, as last seen
static uint32 sHiddenFingerO = 0;			// the overset-scanned document (so a Find Overset alone still follows a hide)

// KCMScrollMapNoticeDrawEvent (declared in KCMScrollMap.h) -- the cheap check called on every
// draw event. Inside the 250 ms throttle it costs one comparison of times and returns. When the
// fingerprint has changed, the map is invalidated. (The strip sits in a column of its own and
// does not overlap the layout view, so invalidating from inside a draw event cannot loop back
// through another spread redraw into another detection.)
void KCMScrollMapNoticeDrawEvent()
{
	if (!sScrollMapOn)
		return;		// with the map off there is no strip either, so skip the per-draw fingerprint
	// An unarmed state does not return: a Find Overset on its own can still have put a strip up.
	// Both facades are asked more than once here, so each is queried into an InterfacePtr first
	// (Utils.h says to do that rather than pay for a query per call) -- this path runs on EVERY
	// draw event.
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare->GetArmedTargetDB() == nil &&
		!(marks->GetOversetOn() && marks->GetOversetDB() != nil))
		return;		// neither armed nor scanning means no strip, so the fingerprints mean nothing

	// The throttle, 250 ms. steady_clock only moves forward, so there is no wrap and no negative
	// delta to guard against. The first check always runs.
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (sHiddenCheckStarted)
	{
		const long long deltaMs =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - sHiddenCheckLast).count();
		if (deltaMs < 250)
			return;
	}
	sHiddenCheckStarted = kTrue;
	sHiddenCheckLast = now;

	IDataBase* const tDB = compare->GetArmedTargetDB();
	IDataBase* const sDB = compare->GetArmedSourceDB();
	IDataBase* const oDB = marks->GetOversetOn() ? marks->GetOversetDB() : nil;
	const uint32 ft = KCMHiddenFingerprint(tDB) * 31u + KCMShownMasterFingerprint(tDB);
	const uint32 fs = KCMHiddenFingerprint(sDB) * 31u + KCMShownMasterFingerprint(sDB);
	const uint32 fo = KCMHiddenFingerprint(oDB) * 31u + KCMShownMasterFingerprint(oDB);
	if (ft != sHiddenFingerT || fs != sHiddenFingerS || fo != sHiddenFingerO)
	{
		sHiddenFingerT = ft;
		sHiddenFingerS = fs;
		sHiddenFingerO = fo;
		KCMScrollMapInvalidateAll();	// the first time (0 -> current) runs once for nothing, which is harmless
	}
}

// -- the on/off flag ("Show Scrollbar Map" in the flyout, on by default) --------------------
// Attaching and detaching strips as the flag flips belongs to the callers; this only holds the
// value. There are two callers and only one of them does that cleanup -- the reason, and why that
// is not a defect, is on the declaration of KCMSetScrollMapEnabled in KCMScrollMap.h.
bool16 KCMGetScrollMapEnabled()      { return sScrollMapOn; }
void   KCMSetScrollMapEnabled(bool16 on) { sScrollMapOn = on; }

// End of KCMScrollMap.cpp
