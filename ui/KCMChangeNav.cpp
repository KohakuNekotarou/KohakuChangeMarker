//========================================================================================
//
//  KCMChangeNav.cpp
//
//  The Next/Prev walk over "the pages whose contents changed" (declared in KCMChangeNav.h).
//  The same idea as KESCL sending a search hit to the centre of the panorama; here the subject is
//  KCM's pages worth looking at -- the pages of the Target (sDB) that carry a change mark
//  (sEntries). Added/Removed (registered) and Overflow (uncompared) pages come from pages being
//  added or removed rather than from a change, and at the user's request they are not walked.
//
//  THE LIST OF STOPS IS REBUILT EVERY TIME, so it always follows a recomparison that moved the
//  marks; the place in it is kept as the CURRENT PAGE UID rather than an index, so it is not lost
//  when the list changes. The order is the document's own page order.
//
//  The move itself is: take the page's rectangle in pasteboard coordinates
//  (Facade::IGeometryFacade::GetItemBounds) and hand its centre to
//  KCMQueryPanorama()->ScrollContentLocationToFrameCenter(). The zoom is not touched -- the
//  centring happens at whatever magnification is in use.
//  The document walked is always the Target, so it is the Target's layout views
//  (GetAllLayoutViews(db)) that scroll: pressing the button with the Source in front still moves
//  the Target's view behind it, which is what the user asked for.
//
//  THE SOURCE FOLLOWS ALONG: the Source's views scroll to the page that corresponds to the
//  Target's (KCMSourcePageForTarget), staying behind, position only. Page numbers shifted by an
//  addition or a deletion do not matter, because the pairing table (KCMBuildPairing) returns the
//  right counterpart. A page with no direct counterpart on the Source -- an Added or Overflow one
//  -- is drawn to the counterpart of the nearest paired page, so what appears is the
//  neighbourhood of where it was inserted.
//  THE SOURCE'S MAGNIFICATION IS MATCHED TOO: the Target's effective zoom (GetXScaleFactor(kTrue))
//  is read and applied to the Source's views with MakeZoomCmd (kZoomToCmdBoss), the same way the
//  viewport synchroniser does it.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IControlView.h"
#include "IPanorama.h"
#include "ILayoutViewUtils.h"	// GetAllLayoutViews(db)
#include "ILayoutUIUtils.h"		// MakeZoomCmd (kZoomToCmdBoss), the official route to another document's zoom
#include "IPanelControlData.h"
#include "IDocument.h"
#include "IPagesSubPanelController.h"	// ScrollPanelToSpread (a page uid is allowed, and the header says so)
#include "PagesPanelID.h"		// kPagesPanelWidgetID / kLayoutPagesSubPanelWidgetID / IID_IPAGESSUBPANELCONTROLLER
#include "ICommand.h"			// the zoom command
#include "CmdUtils.h"			// ProcessCommand
#include "IGeometry.h"
#include "IDataBase.h"
#include "IHierarchy.h"			// GetSpreadUID (page to spread, for the switch made before scrolling)
#include "ILayoutControlData.h"	// GetSpreadRef (which spread a view is showing) / GetDocument
#include "ILayoutCmdData.h"		// what kSetSpreadCmdBoss wants: the document and the view
#include "SpreadID.h"			// kSetSpreadCmdBoss (the command that puts a spread in a layout view)
#include "ErrorUtils.h"			// PMSetGlobalErrorCode / GlobalErrorStatePreserver -- a failed
								// spread switch or zoom must not take later commands down with
								// it, nor escape this file
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "UIDList.h"			// SetItemList (the spread to switch to)
#include "IPageList.h"			// GetPageString / kDefaultPageType (the page label, "Page: 1" and so on)
#include "IGeometryFacade.h"	// GetItemBounds (a page rectangle in pasteboard coordinates; modelled on SnapTracker)
#include "PMRect.h"
#include "PMPoint.h"			// PBPMPoint (a typedef of PMPoint)
#include "PMString.h"
#include "Utils.h"
#include "K2Vector.h"

#include <map>
#include <set>			// the pages of the ordinary spreads (telling an overset on a master apart)
#include <vector>

#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMViewSync.h"			// KCMGetLayoutSync (with Sync on, the companion scrolling is left to it)
#include "IKCMCompareFacade.h"		// GetActiveDocDB / GetCompareMode
#include "IKCMMarkData.h"			// reading the comparison result (changed pages, changed cell counts, overset places)
#include "KCMViewLookup.h"		// KCMQueryPanorama
#include "KCMOversetScan.h"		// KCMOversetLoc, the position of an overset "+" place
#include "KCMThumbnailRefresh.h"	// KCMGetVisiblePagesPanel (the shared way to get the visible Pages panel)
#include "IKCMStoryEditsFacade.h"	// GetFirstFrameUID (the first frame of "the same story" on
									// the Source side) / GetStoryStartPoint (where its text
									// begins)
#include "KCMStoryNav.h"			// the stops of the Story Changes mode (the leaves of the list) and how they are travelled to
#include "KCMChangeNav.h"

// One stop of the walk. A change stop is a page with a frame on it, scrolled to the page's
// centre; an overset stop is an overflow "+" place, scrolled to its pasteboard point (the way KBS
// does it). pageUID drives the order, the label and the Source's companion move; pb only means
// anything on an overset stop.
struct KCMNavStop
{
	UID			pageUID;
	bool16		isOverset;			// kFalse = a change (a frame), kTrue = an overset ("+")
	PBPMPoint	pb;					// the overset "+" point (only on an overset stop)
	int32		oversetOrd;			// this overset stop's number within its page (from 0), for finding it again
	int32		oversetCountOnPage;	// how many oversets that page has (with one, the label carries no (n))

	// A stop of the Story Changes mode. The two above point at a PAGE; this one points at A LEAF
	// OF THE STORY EDITS LIST -- one edit, or a row that has no children.
	// @warning pageUID IS NOT USED here (it stays kInvalidUID): which page it lands on is not
	//   known until the jump happens (an edit further down a threaded story is not on the page of
	//   that story's first frame; KCMStoryJumpToChange resolves it with GetStoryFrameAt). So a
	//   Story stop goes through NONE of the page-based work -- the hidden-page test, KCMStopLabel,
	//   KCMSyncCompanionViews -- see the branch in KCMGoto below.
	bool16		isStory;
	int32		storyRow;			// the row number in KCMStoryList (the vocabulary the facade is given)
	int32		storyChange;		// which change of that row; -1 = the row itself, which has no children
	UID			storyUID;			// which story it is, for finding it again (see sNavStoryUID below)

	KCMNavStop() : pageUID(kInvalidUID), isOverset(kFalse), oversetOrd(0), oversetCountOnPage(0),
					 isStory(kFalse), storyRow(-1), storyChange(-1), storyUID(kInvalidUID) {}
};

// How the stop last visited is identified. Holding its CONTENT -- page, kind, and number within
// the page -- rather than an index is what lets the place survive the list being rebuilt. When
// what it points at is gone, KCMFindCurrentStop returns -1 and the walk starts again from the
// front or the back.
static UID    sNavPageUID    = kInvalidUID;
static bool16 sNavIsOverset  = kFalse;
static int32  sNavOversetOrd = 0;

// Where the walk stands on a Story stop. IT IS REMEMBERED BY STORY, NOT BY ROW NUMBER, for
// exactly the reason above: a "Refresh Story Comparison" rebuilds that row's children and the
// next comparison rebuilds the whole list. Remembered by story, it goes on pointing at the same
// edit as children come and go, and when that edit is gone it is simply not found and the walk
// starts from the front or the back.
static bool16 sNavIsStory     = kFalse;
static UID    sNavStoryUID    = kInvalidUID;
static int32  sNavStoryRow    = -1;
static int32  sNavStoryChange = -1;

// "Standing at the entrance" means THE WALK HAS NOT YET GONE to the stop the anchor above points
// at. It happens only when a parent row with children was selected: such a row is not a stop
// (KCMStoryNav.h), so the walk stands at THE ENTRANCE TO ITS FIRST CHILD instead. The next Next
// goes TO that stop rather than past it; Prev goes to the one before.
// It is the same rule that shows "1/N" the moment a comparison starts -- and that case is
// expressed the same way, as "there is no anchor yet" (cur < 0).
static bool16 sNavStoryAtEntry = kFalse;

//----------------------------------------------------------------------------------------
// The document being walked: the Target while a comparison is running, or, with no comparison
// but Find Overset on, the document it scanned. nil when there is nothing to walk.
//----------------------------------------------------------------------------------------
static IDataBase* KCMNavDoc()
{
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());
	if (marks->GetMarkedTargetDB() != nil)
		return marks->GetMarkedTargetDB();
	if (marks->GetOversetOn() && marks->GetOversetDB() != nil)
		return marks->GetOversetDB();
	return nil;
}

//----------------------------------------------------------------------------------------
// Build the list of stops in the document's page order, each page contributing its change (the
// frame) first and then each of its overset "+" places, at the user's request.
//   - a change stop needs a comparison to be running on this document and the page to be in
//     sEntries. It scrolls to the page's centre. Added/Removed and Overflow pages come from pages
//     being added or removed and are still not included.
//   - an overset stop needs Find Overset to be on and to have scanned this same document. One per
//     "+" place, in the order the scan found them, scrolled to the "+" point the way KBS does it.
//   When the comparison and the overset scan are on DIFFERENT documents, the overset places are
//   not mixed in, because the page order they are in would stop meaning anything. (An overset
//   scan on its own makes its document the one being walked, and only its places are walked.)
//----------------------------------------------------------------------------------------
// Append the overset "+" places on pageUID as stops, in scan order, numbering them within the
// page and recording how many there are.
// Both the ordinary-page loop and the master-page top-up call this, so the numbering is not
// written twice.
// locs is the caller's single copy of "the overset places as they are now": asking across the
// boundary once per page would copy the same thing over and over, so it is reused for the whole
// walk.
static void KCMAppendOversetStopsForPage(UID pageUID, const std::vector<KCMOversetLoc>& locs,
										   std::vector<KCMNavStop>& out)
{
	std::vector<size_t> onPage;
	for (size_t j = 0; j < locs.size(); ++j)
		if (locs[j].pageUID == pageUID)
			onPage.push_back(j);
	const int32 cnt = (int32)onPage.size();
	for (int32 k = 0; k < cnt; ++k)
	{
		const KCMOversetLoc& loc = locs[onPage[k]];
		KCMNavStop s; s.pageUID = pageUID; s.isOverset = kTrue; s.pb = loc.pb;
		s.oversetOrd = k; s.oversetCountOnPage = cnt;
		out.push_back(s);
	}
}

/** The stops ONE PAGE contributes, in the order the walk expects: its frame first, then its
	overflow places. ★Both page loops go through this, so the order cannot drift apart between
	ordinary pages and master pages - which is what the note at the master loop asks for. */
static void KCMAppendStopsForPage(UID pageUID, bool16 changeHere, bool16 oversetHere,
                                    IKCMMarkData* marks, const std::vector<KCMOversetLoc>& locs,
                                    std::vector<KCMNavStop>& out)
{
	// 1) That page's change (its frame): the page centre.
	if (changeHere && marks->HasEntryForPage(pageUID))
	{
		KCMNavStop s; s.pageUID = pageUID; s.isOverset = kFalse;
		out.push_back(s);
	}
	// 2) That page's overset "+" places, one at a time in scan order.
	if (oversetHere)
		KCMAppendOversetStopsForPage(pageUID, locs, out);
}

static void KCMBuildStops(std::vector<KCMNavStop>& out)
{
	out.clear();
	IDataBase* navDB = KCMNavDoc();
	if (navDB == nil)
		return;
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());

	// THE STORY CHANGES MODE HAS NO CHANGE (FRAME) STOPS AT ALL: it rasterises no page, so
	// sEntries stays empty (KCMCore.cpp empties toRaster). What it walks instead are THE LEAVES OF
	// THE STORY EDITS LIST; the rule is in KCMStoryNav.h.
	// @warning `== kKCMModeStory` is deliberate. Written as `!= kKCMModePixel`, a future third mode
	//   that builds no frames would SILENTLY FALL IN HERE. KCMPeek.cpp carries the same warning on
	//   its own `== kKCMModePixel` test, for the mirror-image reason.
	const bool16 storyMode   = (Utils<IKCMCompareFacade>()->GetCompareMode() == kKCMModeStory);
	const bool16 changeHere  = (!storyMode && marks->GetMarkedTargetDB() == navDB);	// frames are only mixed in on the comparison Target
	const bool16 oversetHere = (marks->GetOversetOn() && marks->GetOversetDB() == navDB);

	// The overset places are needed in more than one block below, so one copy is taken here and
	// shared by all of them.
	std::vector<KCMOversetLoc> locs;
	if (oversetHere)
		marks->GetOversetLocations(locs);

	// 0) The Story leaves go FIRST.
	//    THEY ARE NOT INTERLEAVED IN PAGE ORDER. The list has an order of its own -- page order,
	//      with the deleted rows moved to the end (KCMStoryList::Build) -- so slotting them in per
	//      page would make THE ORDER ON SCREEN AND THE ORDER OF Prev/Next DISAGREE. Whoever
	//      presses the button is looking at the list, so the list wins.
	//    The overflow "+" places still follow after these, so FIND OVERSET REMAINS USABLE IN THE
	//      STORY MODE. With it off, the N of "k/N" is simply the number of edits in the list.
	//    The test is `marks->GetMarkedTargetDB() == navDB`, the same question changeHere asks: the
	//      list is something the comparison built, so it only means anything when the document
	//      being walked is the comparison's Target (never on a document that was only scanned for
	//      overflow).
	if (storyMode && marks->GetMarkedTargetDB() == navDB)
	{
		std::vector<KCMStoryNavStop> storyStops;
		KCMBuildStoryNavStops(storyStops);
		for (size_t i = 0; i < storyStops.size(); ++i)
		{
			KCMNavStop s;
			s.isStory     = kTrue;
			s.storyRow    = storyStops[i].fRow;
			s.storyChange = storyStops[i].fChange;
			s.storyUID    = storyStops[i].fStoryUID;
			out.push_back(s);
		}
	}

	// marks was queried into an InterfacePtr above, so it is used as it stands rather than asked
	// for again (Utils.h says to do that when a utility interface is used more than once).
	std::vector<UID> flat;
	marks->GetAllPageUIDs(navDB, flat);
	for (size_t i = 0; i < flat.size(); ++i)
		KCMAppendStopsForPage(flat[i], changeHere, oversetHere, marks, locs, out);

	// GetAllPageUIDs returns THE DOCUMENT'S ORDINARY PAGES ONLY. Master spreads are kept in a
	// separate IMasterSpreadList and never appear in the loop above, so they are topped up below.
	// (Its body moved from a double loop over ISpreadList onto IPageList, which does not include
	// master pages either -- IPageList says so by contract. The premise is unchanged.)
	// THE COLLECTION ITSELF IS LEFT ALONE: it is shared with the comparison's page pairing
	// (KCMBuildPairing), where mixing master pages in would change what is being compared. Adding
	// them here is the right place. They go on AFTER every ordinary page, so the meaning of the
	// page order is not broken.
	std::set<UID> covered(flat.begin(), flat.end());

	// 3) The pages of the master spreads.
	//    The overset ones were added after a report that an overset on a master was found but
	//      could not be reached: it was detected, the "+" showed on the thumbnail, and the stop
	//      list dropped it entirely.
	//    The changed ones (frames) were added in the same place afterwards: once masters were part
	//      of the comparison, the same shape of defect -- "a frame appears but Prev/Next will not
	//      go there" -- could happen to them too.
	//    They follow the same "[frame, then overset...] per page" order the ordinary loop does;
	//      gathering the overflows separately would separate a master's frame from its overflow
	//      when there is more than one master.
	if (changeHere || oversetHere)
	{
		std::vector<UID> masters;
		marks->GetMasterPageUIDs(navDB, masters);	// the InterfacePtr queried above
		for (size_t i = 0; i < masters.size(); ++i)
		{
			const UID u = masters[i];
			if (covered.find(u) != covered.end())
				continue;			// already taken above (a master cannot turn up there, but do not add it twice)
			covered.insert(u);
			KCMAppendStopsForPage(u, changeHere, oversetHere, marks, locs, out);
		}
	}

	// 4) Any overset left over -- on a page belonging to neither an ordinary spread nor a master
	//    one -- goes on the end. This is the safety net kept after masters were picked up in 3):
	//    what would make it unnecessary is the premise that every page UID belongs to one or the
	//    other, and nothing in this code can guarantee that. Better shown than dropped.
	if (oversetHere)
	{
		std::vector<UID> extra;		// in scan order, without duplicates
		for (size_t j = 0; j < locs.size(); ++j)
		{
			const UID pu = locs[j].pageUID;
			if (covered.find(pu) != covered.end())
				continue;			// an ordinary or master page, taken above
			bool16 already = kFalse;
			for (size_t e = 0; e < extra.size() && !already; ++e)
				if (extra[e] == pu)
					already = kTrue;
			if (!already)
				extra.push_back(pu);
		}
		for (size_t e = 0; e < extra.size(); ++e)
			KCMAppendOversetStopsForPage(extra[e], locs, out);
	}
}

// The index of the stop the walk is standing on, or -1 when it is not in the list.
static int32 KCMFindCurrentStop(const std::vector<KCMNavStop>& stops)
{
	for (size_t i = 0; i < stops.size(); ++i)
	{
		// A different kind is a different stop, with nothing more to check.
		// @warning WITHOUT THIS SPLIT THE PAGE-SIDE TEST WOULD ALSO MATCH A STORY STOP: a Story
		//   stop's pageUID stays kInvalidUID, so two "stops with no page" would be taken for each
		//   other (the anchor is kInvalidUID too while it holds its initial value).
		if (stops[i].isStory != sNavIsStory)
			continue;

		if (stops[i].isStory)
		{
			// STORY, ROW AND EDIT MUST ALL AGREE for this to be the same stop.
			// Note that all three are checked even though the story UID alone would do. The list
			// cannot hold the same story UID on two rows: KCMStoryStamp.h defines
			// kKCMStoryKindAdded as "no story with this UID on the source side" and
			// kKCMStoryKindRemoved as "none on the target side", so a UID present on both sides is
			// always paired and becomes neither row.
			// All three are kept anyway because (a) that uniqueness rests on how the pairing is
			// implemented, and it is better not to have to know that here, (b) the row number is
			// the vocabulary the facade is given and is held either way, and (c) IF ANY OF THEM
			// DRIFTS THE STOP IS SIMPLY NOT FOUND and the walk starts from the front -- it fails
			// safe.
			// Mixing the row number in cannot break it: the order of the rows changes only on a
			// NEW COMPARISON, and that is when KCMResetNav throws the anchor away. A "Refresh
			// Story Comparison" rebuilds one row's children and leaves the order alone
			// (IKCMStoryEditsFacade::RefreshRow says so).
			if (stops[i].storyUID == sNavStoryUID && stops[i].storyRow == sNavStoryRow &&
				stops[i].storyChange == sNavStoryChange)
				return (int32)i;
			continue;
		}

		if (stops[i].pageUID == sNavPageUID && stops[i].isOverset == sNavIsOverset &&
			(!stops[i].isOverset || stops[i].oversetOrd == sNavOversetOrd))
			return (int32)i;
	}
	return -1;
}

//----------------------------------------------------------------------------------------
// The effective zoom of document db: GetXScaleFactor(kTrue) -- the effective scale with the
// monitor PPI taken in, 1.0 being 100% -- from the panorama of the first layout view found.
// -1 when it has no view. This is the same quantity the viewport synchroniser works in, so it can
// go straight to MakeZoomCmd.
//----------------------------------------------------------------------------------------
static PMReal KCMReadDocZoom(IDataBase* db)
{
	if (db == nil)
		return PMReal(-1.0);
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<IPanorama> pano(KCMQueryPanorama(views[i]));
		if (pano != nil)
			return pano->GetXScaleFactor(kTrue);
	}
	return PMReal(-1.0);
}

//----------------------------------------------------------------------------------------
// Put the spread being aimed at into the view, before scrolling.
//
// itemUID MAY BE A PAGE OR A PAGE ITEM. IHierarchy::GetSpreadUID returns "the spread of this
// hierarchy node" by contract, which says nothing about pages in particular. So a frame that is
// on no page at all -- one out on the pasteboard -- can still have its spread brought up before
// anything is measured. That generality is what the Story Edits row jump needs; the older caller
// that passes a page (KCMScrollDocToItemCenter) behaves exactly as it did.
//
// SCROLLING TO A PASTEBOARD POINT ASSUMES THE VIEW IS ALREADY SHOWING THAT POINT'S SPREAD. To a
// view showing a different one, that coordinate is somewhere else, or nowhere. A master spread
// makes this obvious: it is not part of the continuous pasteboard the ordinary spreads share, so
// no amount of scrolling will ever arrive -- what is reached is empty pasteboard. KBS hit exactly
// this shape of defect (its row locator read "PA", correctly, and clicking it went nowhere), and
// the remedy is the same: EnsureSpreadInView from KBS's KBSJump.cpp, ported.
//
// THE TEST IS "IS IT A DIFFERENT SPREAD", NOT "IS IT A MASTER". Adobe writes it that way:
// snapshot/SnapTracker.cpp compares ::GetUIDRef(spread) with ILayoutControlData::GetSpreadRef()
// and issues the command whenever they differ, with no special case for masters anywhere. KBS
// first narrowed it to "only for masters" and was corrected back to the official form.
//
// READ THE COORDINATES AFTERWARDS, not before (SnapTracker's own "Re-calculate the starting
// point"). KCMScrollDocToItemCenter below comes through here before it reads any geometry. The
// overset "+" point is the one exception: it was fixed with ::InnerToPasteboardMatrix at scan
// time and is therefore VIEW-INDEPENDENT, which is why it stays valid across the switch and does
// not have to be scanned again.
//
// The command is kSetSpreadCmdBoss plus ILayoutCmdData; SnapTracker's CreateAndProcessSetSpreadCmd
// is a complete worked example.
// Unlike KBS, KCM applies this to EVERY layout view of the document, so that a Split Window or
// several windows all end up in the same place (the same scope KCMScrollDocToPBPoint uses).
// A view that cannot be handled is skipped silently: it is left with the scroll alone, as before,
// which is no worse.
//----------------------------------------------------------------------------------------
// KCMEnsureViewShowsSpread (declared in KCMChangeNav.h) -- one view's worth.
// It is factored out per view because the synchroniser (KCMViewSync.cpp) needs the same judgement
// to move a view onto the other document's master spread, and that judgement belongs in one
// place. KCMEnsureSpreadInView below merely hands it to every view of a db.
bool16 KCMEnsureViewShowsSpread(IControlView* view, IDataBase* db, UID spreadUID)
{
	if (view == nil || db == nil || spreadUID == kInvalidUID)
		return kFalse;
	InterfacePtr<ILayoutControlData> layout(view, UseDefaultIID());
	if (layout == nil)
		return kFalse;
	if (layout->GetSpreadRef().GetUID() == spreadUID)
		return kFalse;	// already showing it -- the usual case, and the cheapest way out

	// The command names a view, so what it is given is THAT VIEW'S OWN document (as in KBS and
	// SnapTracker).
	IDocument* const viewDoc = layout->GetDocument();
	if (viewDoc == nil)
		return kFalse;
	// A UID means nothing outside the db it came from. It should be the same one; check before
	// passing it on.
	if (::GetDataBase(viewDoc) != db)
		return kFalse;

	// Do not let a failed spread switch escape this function. PMSetGlobalErrorCode(kSuccess)
	// below, on its own, WOULD ALSO CLEAR AN ERROR THAT WAS ALREADY SET ON THE WAY IN; the official
	// way to avoid that is ErrorUtils' GlobalErrorStatePreserver.
	// It is constructed after every early return, immediately before a command is actually issued
	// -- none of those exits disturbs anything.
	GlobalErrorStatePreserver setSpreadErrorState;

	InterfacePtr<ICommand> setSpreadCmd(CmdUtils::CreateCommand(kSetSpreadCmdBoss));
	if (setSpreadCmd == nil)
		return kFalse;
	InterfacePtr<ILayoutCmdData> cmdData(setSpreadCmd, UseDefaultIID());
	if (cmdData == nil)
		return kFalse;
	cmdData->Set(::GetUIDRef(viewDoc), layout);
	setSpreadCmd->SetItemList(UIDList(db, spreadUID));
	if (CmdUtils::ProcessCommand(setSpreadCmd) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// the scroll goes on; later commands must not be taken down with this
		return kFalse;
	}
	return kTrue;
}

static void KCMEnsureSpreadInView(IDataBase* db, UID itemUID)
{
	if (db == nil || itemUID == kInvalidUID)
		return;

	InterfacePtr<IHierarchy> itemHier(db, itemUID, UseDefaultIID());
	if (itemHier == nil)
		return;
	const UID spreadUID = itemHier->GetSpreadUID();
	if (spreadUID == kInvalidUID)
		return;

	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	for (int32 i = 0; i < (int32)views.size(); ++i)
		KCMEnsureViewShowsSpread(views[i], db, spreadUID);	// a nil view is dropped inside
}

//----------------------------------------------------------------------------------------
// Scroll every layout view of document db so that pbPoint (in pasteboard coordinates) ends up in
// the centre of the view.
//   With applyZoom > 0, each view's effective zoom is brought to applyZoom before the centring
//   (this is how the Source is matched to the Target's magnification). The zoom goes through the
//   same official command the UI's zoom field uses, kZoomToCmdBoss via MakeZoomCmd.
//   (ILayoutViewUtils::ZoomLayoutViews cannot be called directly: it has no effect on another
//   document's views -- the same reason the viewport synchroniser gives.) A view already at that
//   zoom is left alone.
//   With applyZoom <= 0 the zoom is not touched and only the position changes, as before.
// kTrue when at least one view was scrolled.
//----------------------------------------------------------------------------------------
static bool16 KCMScrollDocToPBPoint(IDataBase* db, const PBPMPoint& pbPoint, PMReal applyZoom = PMReal(-1.0))
{
	if (db == nil)
		return kFalse;

	// Do not let a failed zoom escape this function (the reason is on the same declaration in
	// KCMEnsureViewShowsSpread).
	// @warning the save happens in the constructor either way, so this is built even when
	//   applyZoom <= 0 -- but that path issues no command and moves no error state, so the
	//   destructor only writes the same value back.
	GlobalErrorStatePreserver scrollZoomErrorState;

	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	// ★Held across the loop: the zoom command below is built once per view (Utils.h:74-80).
	InterfacePtr<ILayoutUIUtils> layoutUIUtils(Utils<ILayoutUIUtils>().QueryUtilInterface());
	bool16 any = kFalse;
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		IControlView* view = views[i];
		if (view == nil)
			continue;
		InterfacePtr<IPanorama> pano(KCMQueryPanorama(view));
		if (pano == nil)
			continue;

			// Match the zoom, but only when one was asked for and it differs from what is there.
			// The centring happens after the zoom, so it lands correctly at the new magnification
			// -- the same order the viewport synchroniser uses.
		if (applyZoom > PMReal(0.0))
		{
			const PMReal cur = pano->GetXScaleFactor(kTrue);
			if (abs(cur - applyZoom) > PMReal(0.0001))
			{
				InterfacePtr<ICommand> zoomCmd(layoutUIUtils->MakeZoomCmd(view, applyZoom));
				if (zoomCmd == nil || CmdUtils::ProcessCommand(zoomCmd) != kSuccess)
				{
					// The zoom is a convenience for looking at where the walk landed, so the scroll
					// carries on when it fails. But carrying the error state forward would take
					// later commands down with it, so it is cleared and the walk continues -- the
					// same form KCMEnsureSpreadInView uses when its command fails.
					ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				}
			}
		}

			// The newer name is used. IPanorama calls ScrollViewCenterTo "an obsolete name for
			// ScrollContentLocationToFrameCenter" and says new code should call the latter and
			// that the former "will go away in a future release". They do the same thing (the
			// inline form of the new name simply calls the old one).
			// The other place on the KCM side that uses the new name is KCMViewSync.cpp, at the
			// end of KCMSyncOtherDocViewportsTo (KCMStoryJump.cpp quotes it in a comment as well).
			// Note that KCMPeek.cpp does not call this API at all -- something a grep can be
			// re-run on at any time, which is why it is put that way rather than as a line count.
		pano->ScrollContentLocationToFrameCenter(pbPoint, kTrue /*forceRedraw*/);
		any = kTrue;
	}
	return any;
}

//----------------------------------------------------------------------------------------
// Scroll every layout view of document db so that the centre of itemUID's rectangle ends up in
// the centre of the view.
// itemUID MAY BE A PAGE OR A PAGE ITEM (GetItemBounds answers for either). The callers are:
//   - the Prev/Next walk (KCMGoto) ........................ passes a page
//   - the Source's companion move (KCMSyncCompanionViews) . passes the Source page the pairing
//                                                           table gave it
//   - the Story Edits row jump (the fallback inside KCMScrollDocToStoryStart) ... passes a frame
// Delegates to the pasteboard-point form above (inner centre, converted).
//----------------------------------------------------------------------------------------
static bool16 KCMScrollDocToItemCenter(IDataBase* db, UID itemUID, PMReal applyZoom = PMReal(-1.0))
{
	if (db == nil || itemUID == kInvalidUID)
		return kFalse;

	// Bring the spread up FIRST. Without it, a page on a master spread cannot be reached by
	// scrolling at all (see KCMEnsureSpreadInView above). The geometry is read afterwards -- the
	// coordinates from before a switch are not to be trusted, which is the order SnapTracker uses
	// as well.
	KCMEnsureSpreadInView(db, itemUID);

	InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
	if (itemGeo == nil)
		return kFalse;	// a UID with no geometry cannot be measured (the caller reports it could not move)

	// Getting a rectangle in pasteboard coordinates is the facade's job; snapshot/SnapTracker.cpp
	// does exactly the same thing TO A PAGE -- query IGeometry, reject nil, hand its ::GetUIDRef to
	// GetItemBounds. (The older code built the same answer out of GetPathBoundingBox +
	// ::InnerToPasteboardMatrix + a Transform of its own.)
	// The nil test above stays: what the older code guaranteed as a side effect -- that this UID
	// really has geometry -- the facade does not, and the model does the same test in the same
	// order.
	// The BoundsKind is PathBounds, which means what the old GetPathBoundingBox meant. SnapTracker
	// uses OuterStrokeBounds, but a page has no stroke width and a frame's stroke sits
	// symmetrically on the four sides of its rectangle, so THE CENTRE COMES OUT THE SAME EITHER
	// WAY -- the one whose meaning fits is used.
	const PMRect box = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		::GetUIDRef(itemGeo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
	return KCMScrollDocToPBPoint(db,
		PBPMPoint((box.Left() + box.Right()) / PMReal(2.0), (box.Top() + box.Bottom()) / PMReal(2.0)),
		applyZoom);
}

//----------------------------------------------------------------------------------------
// Decide which page the Source should show for a given page of the Target.
//   - an ordinary paired page ... looked straight up in the pairing table (KCMBuildPairing).
//     Page numbers shifted by an addition or a deletion do not matter: the table leaves the
//     registered pages out and matches the rest in order, so what comes back is the counterpart
//     with the shift already absorbed. That is what makes additions and deletions work.
//   - an Added or Overflow page, one that exists only on the Target and so has no direct
//     counterpart ... drawn to the counterpart of the nearest paired page, preferring the one
//     BEFORE it when the distance is equal (the near side of where it was inserted). What the
//     Source window then shows is roughly where this new page went in the older version.
//   kInvalidUID when nothing is found, and then the caller leaves the Source alone.
//----------------------------------------------------------------------------------------
static UID KCMSourcePageForTarget(IDataBase* targetDB, IDataBase* sourceDB, UID targetPageUID)
{
	if (targetDB == nil || sourceDB == nil)
		return kInvalidUID;

	std::vector<UID> tPages, sPages;
	Utils<IKCMMarkData>()->GetPagePairing(targetDB, sourceDB, tPages, sPages);	// paired (registrations left out, the shift absorbed)

	// Look for the direct counterpart, Target to Source.
	std::map<UID, UID> t2s;
	for (size_t i = 0; i < tPages.size(); ++i)
	{
		if (tPages[i] == targetPageUID)
			return sPages[i];
		t2s[tPages[i]] = sPages[i];
	}

	// No counterpart (Added or Overflow): draw to the Source counterpart of the nearest paired
	// page in page order.
	std::vector<UID> flat;
	Utils<IKCMMarkData>()->GetAllPageUIDs(targetDB, flat);
	int32 idx = -1;
	for (size_t i = 0; i < flat.size(); ++i)
		if (flat[i] == targetPageUID) { idx = (int32)i; break; }
	if (idx < 0)
		return kInvalidUID;

	const int32 n = (int32)flat.size();
	for (int32 d = 1; d < n; ++d)
	{
		if (idx - d >= 0)	// prefer the one before it, the near side of where it was inserted
		{
			std::map<UID, UID>::const_iterator it = t2s.find(flat[idx - d]);
			if (it != t2s.end())
				return it->second;
		}
		if (idx + d < n)
		{
			std::map<UID, UID>::const_iterator it = t2s.find(flat[idx + d]);
			if (it != t2s.end())
				return it->second;
		}
	}
	return kInvalidUID;
}

//----------------------------------------------------------------------------------------
// Scroll the Pages panel to the spread of the page as well, on a best-effort basis.
//   The Pages panel lists the pages of THE ACTIVE DOCUMENT, so it is only moved when the active
//   document is db. (With the Source in front and Prev/Next pressed, the panel is showing the
//   Source's list, and handing it a Target page UID would mean nothing -- so nothing is done.)
//   The route is the same as KCMThumbnailRefresh's: IPanelMgr -> GetVisiblePanel
//   (kPagesPanelWidgetID) -> FindWidget(kLayoutPagesSubPanelWidgetID) ->
//   IPagesSubPanelController. ScrollPanelToSpread takes a page UID directly; its header says so
//   ("spread or page uid").
//----------------------------------------------------------------------------------------
static void KCMScrollPagesPanelToPage(IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return;

	// Is it the active document's page list that is on screen? If not, leave it alone.
	// The db is obtained through GetActiveDocDB (IActiveContext::GetContextDocument). "Which
	// document is the Pages panel showing" is the same question the model's page-map selection
	// asks, so it is asked through the same door. The older code used GetFrontDocument, whose
	// contract is "the document associated with the frontmost LAYOUT presentation" -- which can
	// differ from the active document.
	if (Utils<IKCMCompareFacade>()->GetActiveDocDB() != db)
		return;

	// Getting the panel goes through the shared helper in KCMThumbnailRefresh.h.
	IControlView* panel = KCMGetVisiblePagesPanel();
	if (panel == nil)
		return;	// nothing to do while the panel is hidden (it will be built normally when next opened)
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;
	IControlView* subView = pcd->FindWidget(kLayoutPagesSubPanelWidgetID);
	if (subView == nil)
		return;
	// A MASTER PAGE IS NOT THIS SUB-PANEL'S BUSINESS (kLayoutPagesSubPanelWidgetID), so it is not
	// passed on; jumping to an overset on a master still works through the spread switch, and only
	// the panel following it is given up. Whether a page is an ordinary one is decided by whether
	// IPageList knows it (GetPageIndex >= 0).
	// @warning THIS RELIES ON THE DEFAULT kTrue OF THE SECOND ARGUMENT, includePagesOfHiddenSpread.
	//   What is wanted here is "is this not a master", and whether it is hidden has nothing to do
	//   with it -- a page hidden by Hide Unchanged is still an ordinary page, so kTrue is right.
	//   Setting it to kFalse would silently turn this into "the Pages panel does not follow to a
	//   hidden page". The dependency is written down because it is a dependency.
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil || pageList->GetPageIndex(pageUID) < 0)
		return;
	InterfacePtr<IPagesSubPanelController> ctrl(subView, UseDefaultIID());
	if (ctrl != nil)
		ctrl->ScrollPanelToSpread(UIDRef(db, pageUID));
}

//----------------------------------------------------------------------------------------
// Build the percentage string shown to the user, out of the changed cell count and the page's
// total cell count.
//   1% and over        -> a whole number    e.g. "12%"
//   0.05% up to 1%     -> one decimal place e.g. "0.4%"
//   under 0.05%        -> "<0.1%" (rounded to one place it would read "0.0%", which would be
//                         taken to mean "nothing changed")
// An empty string when total <= 0 or changed <= 0, and then the caller appends nothing.
// THE DECIMAL POINT IS WRITTEN BY HAND: PMString's real number formatting can produce a "," for
// it depending on the locale. Working in integer per-mille and assembling the digits fixes the
// notation and leaves no room for a locale to change it.
//----------------------------------------------------------------------------------------
static PMString KCMFormatChangeRatio(int32 changed, int32 total)
{
	PMString out; out.SetTranslatable(kFalse);
	if (total <= 0 || changed <= 0)
		return out;

	// per-mille, rounded. changed <= total, so this is at most 1000; the int64 cannot overflow
	const int32 permille = (int32)(((int64)changed * 1000 + total / 2) / total);

	if (permille >= 10)			// 1% and over: round the whole-number percentage separately
		out.AppendNumber((int32)(((int64)changed * 100 + total / 2) / total));
	else if (permille >= 1)		// 0.05% up to 1%: one decimal place ("0" + "." + one digit)
	{
		out.AppendNumber(permille / 10);
		out.Append(".");
		out.AppendNumber(permille % 10);
	}
	else						// under 0.05%: rounding gives 0, so the floor is spelled out instead
		out.Append("<0.1");

	out.Append("%");
	return out;
}

//----------------------------------------------------------------------------------------
// Build the label for where the walk landed, shown in the panel's message line.
//   a change (a frame)                 = "Page: <n>, Change <percentage>"
//                                        e.g. Page: 1, Change 12% / Page: 4, Change 0.4%
//   an overset, one on the page        = "Page: <n> Overset"        e.g. Page: 1 Overset
//   an overset, several on the page    = "Page: <n> (k) Overset"    e.g. Page: 1 (2) Overset
//                                        (k counts from 1)
// The percentage is separated with ", Change " rather than a space so that what the percentage is
// OF can be read off; the overset form already carries the word "Overset" and keeps its space.
// The (k) and Overset are separated by single spaces. The page number comes from
// IPageList::GetPageString -- with the section, and AS THE PAGES PANEL NUMBERS IT.
//
// THE SEVENTH ARGUMENT, bIncludePagesOfHiddenSpread, IS kTrue AND NOT kFalse. InDesign holds two
// page numbers (measured on a live build): kFalse gives the folio actually printed on the page,
// which counts past hidden spreads. This label is where a human is told which page to look at
// next, and they will go and find it in the Pages panel -- so it is spelled the way the Pages
// panel spells it (kTrue).
//   The TSV export (PageDisplay in KCMChangedPagesTSV.cpp) and the Story Edits label (PageLabel in
//   KCMStoryJump.cpp) were brought into line for the same reason. THE THREE ARE ONE QUESTION --
//   "how is this page spelled to a human" -- so fixing one alone makes the panel and the export
//   disagree while anything is hidden.
// @warning the folio exclusion rectangle (KCMPageNumberMarker.cpp) is right to stay kFalse: that
//   one measures the ink of the digits actually printed. It is a different question and must not
//   be brought into line.
// The percentage goes on a change stop only (an overset is an overflow, and has nothing to do
// with how much changed). When no value can be produced -- the entry cannot be read, say -- none
// is appended and the label is as it was. The specification is docs/ai-notes/kescm-change-ratio.md.
//----------------------------------------------------------------------------------------
static PMString KCMStopLabel(IDataBase* db, const KCMNavStop& stop)
{
	PMString label; label.SetTranslatable(kFalse);
	label.Append("Page: ");

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	PMString numStr; numStr.SetTranslatable(kFalse);
	if (pageList != nil)
		pageList->GetPageString(stop.pageUID, &numStr, kTrue, kFalse, kDefaultPageType, kTrue, kTrue);
	if (numStr.NumUTF16TextChars() > 0)
		label.Append(numStr);
	else if (stop.isOverset)
		label.Append("Master");	// the catch-all for an overset on a master page: more informative
								// than "?".
								// @warning THIS IS NOT REACHED IN PRACTICE. GetPageString returns
								//   a prefix for a master page too (measured: a Japanese build
								//   produced "Page: A Overset"), so numStr does not come back
								//   empty. It is kept as a safety net, not as an expected value
								//   -- DO NOT WRITE A TEST THAT EXPECTS "Master".
	else
		label.Append("?");	// a page whose number cannot be read (does not normally happen)

	if (stop.isOverset)
	{
		if (stop.oversetCountOnPage > 1)	// the (k) goes on only when the page has more than one (counting from 1)
		{
			label.Append(" (");			// spaced out, since "Page: 1(2)" is hard to read
			label.AppendNumber(stop.oversetOrd + 1);
			label.Append(")");
		}
		label.Append(" Overset");	// a space, then Overset
	}
	else
	{
		// A change stop: append how much of that page changed (e.g. "Page: 3, Change 12%"). The
		// numerator is the changed cell count taken during the comparison; the denominator is that
		// page's low-resolution cell count (w * h, straight from the entry's image dimensions).
		int32 changedCells = 0, totalCells = 0;
		if (Utils<IKCMMarkData>()->GetChangeCells(stop.pageUID, changedCells, totalCells))
		{
			const PMString ratio = KCMFormatChangeRatio(changedCells, totalCells);
			if (ratio.NumUTF16TextChars() > 0)
			{
				label.Append(", Change ");	// name what the percentage is of ("Page: 3, Change 12%")
				label.Append(ratio);
			}
		}
	}

	// A PAGE ON A HIDDEN SPREAD CANNOT BE SCROLLED TO (measured: pressing the button leaves
	// activePage where it was). The stop stays in the walk, and the reason the screen does not
	// move is said here instead.
	// @warning THIS NOTE IS PART OF THE SAME CHANGE THAT MADE THE NUMBER kTrue ABOVE. While the
	//   label was on the printed folio (kFalse), a hidden page had no number and came out as
	//   "Page: #" -- the oddity itself was the signal that it could not be reached. Numbering it
	//   the way the Pages panel does makes it look like an ordinary "Page: 2", so without saying
	//   why, all that is left is a button that appears to do nothing.
	// The status line is English in every locale (the display policy is in KCMID.h; the only two
	// things shown in Japanese are How to Use and the Hide Unchanged confirmation).
	// The mark is spelled "(Hide)", the same as in the TSV's Page column (PageDisplay in
	// KCMChangedPagesTSV.cpp) -- one state is not spelled two ways.
	if (db != nil && Utils<IKCMMarkData>()->IsPageOnHiddenSpread(db, stop.pageUID))
		label.Append(" (Hide)");

	return label;
}

//----------------------------------------------------------------------------------------
// Once the Target has moved, bring the views around it along: the Pages panel and the Source
// window.
//
// THE ONLY CALLER IS THE Prev/Next WALK (KCMGoto). The Story Edits row jump does NOT come through
// here -- it moves the Target only (the user's decision; "Sync Layout Views" is the way to see
// the Source as well). It is kept as a function of its own because matching the zoom, standing
// aside when Sync is on, and resolving the page through the pairing table are the three parts of
// the answer to "what does bringing it along mean", and folding them into the walk itself would
// make that unreadable.
// A kInvalidUID pageUID -- a frame out on the pasteboard -- gives nothing to draw either the
// Source or the Pages panel to, so neither is moved. The Target's move was the caller's and
// stands on its own.
//----------------------------------------------------------------------------------------
static void KCMSyncCompanionViews(IDataBase* navDB, UID pageUID)
{
	if (navDB == nil || pageUID == kInvalidUID)
		return;

	// The Pages panel follows to the page as well (only while navDB is the one in front;
	// best-effort).
	KCMScrollPagesPanelToPage(navDB, pageUID);

	// The Source follows to the corresponding page too, while a comparison is running -- staying
	// behind, position only. Page numbers shifted by an addition or a deletion are handled by the
	// pairing table, and a page with no counterpart (Added or Overflow) is drawn to the nearest
	// one. The Source's magnification is matched to the Target's as well. On an overset stop it is
	// still "that page's" corresponding Source page that is used.
	// Best-effort: with no Source view, or no counterpart to be found, the move on navDB's side
	// still stands.
	IDataBase* sourceDB = Utils<IKCMMarkData>()->GetMarkedSourceDB();
	if (sourceDB != nil && sourceDB != navDB)
	{
		const UID srcPage = KCMSourcePageForTarget(navDB, sourceDB, pageUID);
		if (srcPage != kInvalidUID)
		{
				// WITH "Sync Layout Views" ON, THE SOURCE'S VIEW IS NOT SCROLLED HERE. The sync
				// observer (KCMViewSync.cpp) already mirrors navDB's -- the Target's -- scroll onto
				// the Source, page offset included. Scrolling the Source by hand on top of that
				// gets mirrored BACK onto the Target, and an overset "+" scroll ends up cancelled
				// out by the page centring (found by the user: with sync off, the jump to the
				// overset worked). With sync on, the Target's scroll -- the caller's -- is what
				// Sync carries over.
				// The Pages panel is not Sync's business, so it follows either way.
				// THE SOURCE IS NOT MOVED TO A HIDDEN PAGE EITHER, for the same reason as the
				// Target: the spread does not switch, the scroll takes effect all the same, and it
				// ends up somewhere unrelated. Hide Unchanged hides the corresponding spread in
				// both documents, so when the Target's page is hidden its counterpart usually is
				// too.
				// @warning the Pages panel below still follows: which page it is remains worth
				//   showing.
			if (!KCMGetLayoutSync() &&
			    !Utils<IKCMMarkData>()->IsPageOnHiddenSpread(sourceDB, srcPage))
			{
				const PMReal targetZoom = KCMReadDocZoom(navDB);	// the effective zoom (<= 0 leaves the zoom alone)
				KCMScrollDocToItemCenter(sourceDB, srcPage, targetZoom);
			}
				// With the Source in front, the Pages panel is showing the Source's list, so it
				// follows to the corresponding page there. (The helper's own "is this the document
				// in front" guard means this call does nothing while navDB is in front.)
			KCMScrollPagesPanelToPage(sourceDB, srcPage);
		}
	}
}

//----------------------------------------------------------------------------------------
// Scroll document db's views so that THE VERY BEGINNING of storyUID ends up in the centre.
//
// WHAT IS AIMED AT IS WHERE THE TEXT BEGINS, not the centre of the frame (the user's decision):
// in a tall frame the centre is somewhere in the middle of the text, and what is worth reading is
// the beginning. The point comes from IKCMStoryEditsFacade::GetStoryStartPoint (backed by
// KCMStoryStartPoint in KCMStoryList.cpp), which is the mirror image of KCMLastPlacedOutport, the
// one that puts up the overset "+": it goes through the same three coordinate systems in the same
// order.
// Getting to the point works exactly as the overset one does: BRING THE SPREAD UP FIRST, then
// scroll to the pasteboard point -- scrolling to a pasteboard point assumes the view is already
// showing that point's spread (see KCMEnsureSpreadInView above).
// When no point can be had -- nothing has been composed yet, say -- it falls back to the centre
// of the frame (what it did before). outFrame reports the frame actually landed on, which is what
// the Pages panel's own move uses to resolve a page.
//
// PASS focusIndex TO AIM AT THAT CHARACTER instead of the start of the story (the user asked for
// "the very first part of what changed to move to the middle of the layout view"). When a long
// story has changed near its end, jumping to its beginning POINTS AT THE STORY WITHOUT POINTING
// AT THE CHANGE. The point is where the caret would stand: GetStoryPointAt.
// @warning WHEN IT CANNOT BE HAD, THIS FALLS BACK TO THE BEGINNING SILENTLY. Overset, unplaced
//   and uncomposed all amount to "that character is nowhere on any page right now", while the row
//   itself is still correct -- showing the story beats moving nothing at all.
//----------------------------------------------------------------------------------------
static bool16 KCMScrollDocToStoryStart(IDataBase* db, UID storyUID, UID fallbackFrameUID,
	UID& outFrame, PMReal applyZoom = PMReal(-1.0), TextIndex focusIndex = kInvalidTextIndex)
{
	if (focusIndex != kInvalidTextIndex)
	{
		PBPMPoint focusPb;
		if (Utils<IKCMStoryEditsFacade>()->GetStoryPointAt(db, storyUID, focusIndex, focusPb))
		{
			// THE FRAME USED TO BRING THE SPREAD UP IS THE ONE THE CALLER PASSED, and it has to be
			// the frame that contains the change. PASTEBOARD COORDINATES ARE PER SPREAD, so
			// aiming at the point with the wrong spread up does not land "a little off" -- IT
			// LANDS ON A DIFFERENT PAGE.
			// Both callers now ask the same question through GetStoryFrameAt. (One of them used to
			// pass GetFirstFrameUID, THE STORY'S FIRST FRAME, which sent the old version's window
			// somewhere unrelated whenever the change was further down a threaded story.)
			outFrame = fallbackFrameUID;
			KCMEnsureSpreadInView(db, fallbackFrameUID);
			return KCMScrollDocToPBPoint(db, focusPb, applyZoom);
		}
		// otherwise fall through to "the beginning" below (one fallback, in one place)
	}

	UID startFrame = kInvalidUID;
	PBPMPoint startPb;
	if (Utils<IKCMStoryEditsFacade>()->GetStoryStartPoint(db, storyUID, startFrame, startPb))
	{
		outFrame = startFrame;
		KCMEnsureSpreadInView(db, startFrame);
		return KCMScrollDocToPBPoint(db, startPb, applyZoom);
	}

	outFrame = fallbackFrameUID;
	return KCMScrollDocToItemCenter(db, fallbackFrameUID, applyZoom);
}

//----------------------------------------------------------------------------------------
// The walk itself: dir = +1 for next, -1 for previous, wrapping at either end. The position
// readout ("3/12") goes to the widget between Prev and Next; the label of where it landed
// ("Page: 1, Change 12%" and the like) goes to the message line. A stop is either a change (the
// page's centre) or an overset (its "+" pasteboard point, the way KBS does it).
//----------------------------------------------------------------------------------------
static void KCMGoto(int32 dir)
{
	IDataBase* navDB = KCMNavDoc();
	if (navDB == nil)
	{
		KCMSetStatus("Start a comparison or run Find Overset first.");
		return;
	}

	std::vector<KCMNavStop> stops;
	KCMBuildStops(stops);
	if (stops.empty())
	{
		KCMSetStatus("Nothing to review.");
		KCMRefreshNavPosition();	// nothing to walk: "/" and dead buttons (normally unreachable, they are already disabled)
		return;
	}

	// Find where the walk stands, by content. When it is not there -- the first press, or the
	// previous stop is gone -- next starts at the front and previous at the back.
	int32 cur = KCMFindCurrentStop(stops);
	int32 next;
	if (cur < 0)
	{
		next = (dir > 0) ? 0 : (int32)stops.size() - 1;
		// DROP THE ENTRANCE FLAG TOO. With the anchor itself gone -- that row was refreshed and
		// left with no children, say -- "which stop's entrance" means nothing on its own.
		// Nothing reads it while cur < 0 today, so this causes no harm as it stands; leaving an
		// incorrect state in place because nothing reads it is what breaks on the day one
		// condition changes.
		sNavStoryAtEntry = kFalse;
	}
	else if (sNavStoryAtEntry && stops[cur].isStory)
	{
		// STANDING AT THE ENTRANCE means a parent row with children was selected. The anchor
		// points at that row's first child, BUT THE WALK HAS NOT GONE THERE: what the click on the
		// row jumped to was the start of the story, not the first change inside it.
		// So NEXT GOES TO THAT STOP rather than past it -- whoever selected the parent and pressed
		// Next does not have its first change skipped -- and PREVIOUS GOES TO THE STOP BEFORE IT,
		// stepping out in front of the entrance.
		// This is the same idea as cur < 0 above (nothing walked yet, the "1/N" a comparison shows
		// as it starts); the only difference is that selecting a row says WHICH stop's entrance it
		// is.
		next = (dir > 0) ? cur : cur - 1;
		if (next < 0) next = (int32)stops.size() - 1;	// previous from the first entrance wraps to the end
	}
	else
	{
		next = cur + dir;
		if (next < 0)                        next = (int32)stops.size() - 1;	// previous from the first wraps to the end
		else if (next >= (int32)stops.size()) next = 0;						// next from the last wraps to the front
	}
	const KCMNavStop& stop = stops[next];

	// A STOP OF THE STORY CHANGES MODE GOES THROUGH NONE OF THE PAGE WORK BELOW.
	// How it travels, the mark it flashes and what goes in the message line are all THE SAME
	// IMPLEMENTATION A CLICK ON THE LIST ROW USES (KCMStoryNav.cpp into KCMStoryJump.cpp). The
	// user asked for "the same behaviour as selecting a StoryEdit row", and the only way to keep
	// that is BY NOT REBUILDING IT HERE.
	// @warning KCMStopLabel and KCMSyncCompanionViews below are deliberately NOT called:
	//   1. the label ("Page: 3, Change 12%") is a proportion of changed cells from the pixel
	//      comparison, and a Story stop has no denominator. Worse, the jump has ALREADY filled the
	//      message line (with the old side's text for a change, or "Page: 3" for a row), so
	//      writing here would overwrite and erase it.
	//   2. the Source window and the Pages panel are brought along by KCMGotoStoryFrame itself,
	//      and it lines them up on THE SAME STORY rather than on a page (the user's point). Adding
	//      the page-based one on top of it loses sight of the very story the jump is there to
	//      show.
	// The hidden-spread handling is not brought over either -- KCMGotoStoryFrame has already
	// decided that case ("leave the layout alone, move the Pages panel, return kTrue"), and that
	// judgement is not written down twice.
	// THE ANCHOR MOVES ON WHETHER OR NOT THE JUMP SUCCEEDED. A row whose story is unplaced says
	// so for itself, but it is still a stop -- not advancing would leave the walk STUCK ON IT with
	// no way past. (The page side does refuse to advance when it cannot scroll, because ITS
	// failures mean "that page is not there any more", the kind that disappears when the list is
	// rebuilt.)
	if (stop.isStory)
	{
		KCMStoryNavStop storyStop;
		storyStop.fRow      = stop.storyRow;
		storyStop.fChange   = stop.storyChange;
		storyStop.fStoryUID = stop.storyUID;
		KCMGotoStoryNavStop(storyStop);

		// THE ANCHOR IS NOT SET HERE. It is set on the jump side (KCMStoryJump.cpp calling
		// KCMNoteStoryStop), and a click on a row and an arrow key walking the list both go
		// through there -- setting it here as well would have two places deciding where the walk
		// stands.
		// That side sets it AS SOON AS THE ROW IS KNOWN TO EXIST, so the anchor advances even for
		// a row that could not be jumped to (an unplaced story, a hidden page) and the walk never
		// gets stuck.
		KCMRefreshNavPosition();		// "k/N" and the button states, closing the same way the page side does
		return;
	}

	// An overset scrolls to its "+" point (the way KBS does it); a change scrolls to the page's
	// centre.
	// THE ANCHOR IS UPDATED ONLY AFTER A SUCCESSFUL SCROLL: advancing it on a failure would leave
	// the readout "k/N" stale AND the next walk starting from somewhere it never reached.
	// The overset path scrolls straight to a pasteboard point, so the spread is brought up here
	// (the page-centre path does the same thing inside KCMScrollDocToItemCenter). Without it an
	// overflow on a master spread cannot be reached and the view lands on empty pasteboard --
	// reported by the user, and the same remedy KBS uses.
	// THE LAYOUT VIEW IS NOT MOVED TO A PAGE ON A HIDDEN SPREAD.
	// @warning measured: jumping to a hidden page leaves THE SPREAD UNCHANGED WHILE THE SCROLL
	//   STILL TAKES EFFECT (kSetSpreadCmdBoss cannot bring up a hidden spread, so the view stays
	//   where it is and slides to the hidden page's pasteboard coordinates) -- the screen moves
	//   somewhere unrelated. "If it cannot go, it does not move" matches what the person pressing
	//   the button expects.
	// THE STOP ITSELF STAYS IN THE WALK (the user asked for that): the anchor update, the "k/N"
	// readout, the status line label -- with "(Hide)" on the end -- and the Pages panel move all
	// still happen below. The screen does not move, but which page changed can be read off the
	// panel and the status line.
	const bool16 stopHidden = Utils<IKCMMarkData>()->IsPageOnHiddenSpread(navDB, stop.pageUID);
	bool16 ok = kTrue;		// while hidden, "did not scroll" counts as success (so the early return below is not taken)
	if (!stopHidden)
	{
		if (stop.isOverset)
			KCMEnsureSpreadInView(navDB, stop.pageUID);
		ok = stop.isOverset ? KCMScrollDocToPBPoint(navDB, stop.pb)
		                    : KCMScrollDocToItemCenter(navDB, stop.pageUID);
	}
	if (!ok)
	{
		KCMSetStatus("Could not scroll.");
		KCMRefreshNavPosition();	// rebuild the readout and the buttons from the state that did not move
		return;
	}
	sNavPageUID    = stop.pageUID;
	sNavIsOverset  = stop.isOverset;
	sNavOversetOrd = stop.oversetOrd;
	// Back on the page side, so the Story anchor is void, kind and all (the counterpart of the
	// branch above).
	// THE ENTRANCE FLAG GOES WITH IT: left set, it would sit there as kTrue after the walk stood
	// at a Story stop's entrance and then went out to an overflow place with Prev. Nothing reads
	// it while sNavIsStory is false, but "nothing reads it, so it is fine" breaks on the day one
	// condition changes.
	sNavIsStory      = kFalse;
	sNavStoryAtEntry = kFalse;

	// Where it landed goes to the message line (e.g. "Page: 1, Change 12%" / "Page: 1 Overset" /
	// "Page: 1 (2) Overset" -- see KCMStopLabel). The k/N position is a separate widget, filled in
	// by KCMRefreshNavPosition below.
	KCMSetStatus(KCMStopLabel(navDB, stop));

	// Bring the views around it along: the Pages panel and the Source window.
	KCMSyncCompanionViews(navDB, stop.pageUID);

	// The position goes to the widget between Prev and Next ("3/12", as in KESCL). The anchor was
	// updated above, so the shared function rebuilds "k/N" from the current list of stops -- the
	// value and the button states are assembled in one place.
	KCMRefreshNavPosition();
}

//========================================================================================
// KCMGotoNextChange / KCMGotoPrevChange (declared in KCMChangeNav.h)
//========================================================================================
void KCMGotoNextChange() { KCMGoto(+1); }
void KCMGotoPrevChange() { KCMGoto(-1); }

//========================================================================================
// KCMGotoStoryFrame (declared in KCMChangeNav.h)
//========================================================================================
bool16 KCMGotoStoryFrame(IDataBase* db, UID frameUID, UID pageUID, UID storyUID,
	TextIndex focusIndex, TextIndex sourceFocusIndex)
{
	// The facade is asked more than once here, so it is queried into an InterfacePtr first
	// (Utils.h says to do that rather than pay for a query per call). It is at the very top of the
	// function because the hidden-page test immediately below already uses it.
	InterfacePtr<IKCMMarkData> marks(Utils<IKCMMarkData>().QueryUtilInterface());

	// THE LAYOUT VIEW IS NOT MOVED TO A PAGE ON A HIDDEN SPREAD -- exactly the judgement Prev/Next
	// (KCMGoto) makes. Fixing that one WITHOUT FIXING THIS ONE would leave "trying to go to a
	// hidden page" doing nothing from the panel's buttons and going somewhere unrelated from a
	// Story Edits row: one question with two answers.
	// The Pages panel still follows: which page it is remains worth showing. The row's label
	// (PageLabel in KCMStoryJump.cpp) puts "(Hide)" on the end, so the reason nothing moved does
	// reach the user.
	// THE RETURN VALUE IS kTrue -- not "could not go" but "DECIDED NOT TO GO". The caller
	// (KCMStoryJumpToRow) spells kFalse as "Could not scroll.", so returning kFalse here would
	// report a failure that did not happen.
	if (marks->IsPageOnHiddenSpread(db, pageUID))
	{
		KCMScrollPagesPanelToPage(db, pageUID);
		return kTrue;
	}

	// To the character when a change was named, and to the start of the story when it was not --
	// NOT to the centre of the frame (see KCMScrollDocToStoryStart above).
	UID landedFrame = kInvalidUID;
	if (!KCMScrollDocToStoryStart(db, storyUID, frameUID, landedFrame, PMReal(-1.0), focusIndex))
		return kFalse;

	// The Pages panel goes to the page of THE FRAME ACTUALLY LANDED ON (a frame on no page does
	// nothing inside). It is resolved from where the jump landed rather than from the pageUID the
	// row remembers: WHEN THE FIRST FRAME HAS NO PARCEL PLACED IN IT, what is landed on is the
	// next frame, which can be on another page. What is shown and what happened should not
	// disagree.
	KCMScrollPagesPanelToPage(db, (landedFrame != kInvalidUID) ? marks->GetFramePageUID(db, landedFrame) : pageUID);

	// ***** THE SOURCE COMES ALONG TOO -- but lined up on THE STORY, not on the page. *****
	//
	// This is where it differs from Prev/Next (KCMSyncCompanionViews). What that one points at is
	// a page, so looking the page up in the pairing table is enough. What THIS row points at is A
	// STORY, and THE SAME STORY CAN BE IN A DIFFERENT PLACE IN THE TWO VERSIONS (the user's point;
	// of course it can, once the layout changes). So the Source is given the first frame of the
	// same story UID and centred on that -- going through a page number loses sight of the very
	// "story that moved" this feature exists to show.
	// What makes matching by UID possible is the premise the whole feature rests on: A SAVE-AS
	// CARRIES THE STORY UID OVER (measured; the section "WHY TWO VERSIONS CAN BE MATCHED AT ALL"
	// in KCMStoryStamp.h). A story with no counterpart on the Source -- an Added row -- comes back
	// as kInvalidUID, and then only the Target moves.
	IDataBase* sourceDB = marks->GetMarkedSourceDB();
	if (sourceDB != nil && sourceDB != db && storyUID != kInvalidUID)
	{
		// THE OLD SIDE NEEDS A DIRTY GUARD OF ITS OWN. Passing sourceFocusIndex can bring THE OLD
		// DOCUMENT'S COMPOSITION up to date, in order to produce that character's position and the
		// frame carrying it, and composing dirties a document
		// (IKCMStoryEditsFacade::GetStoryPointAt / GetStoryFrameAt).
		// The guard for the new side is the caller's, but THE OLD DOCUMENT IS TOUCHED ONLY HERE,
		// so this function holds that one itself.
		// On the path that passes no focus (a parent story row) nothing is composed and the guard
		// does nothing.
		IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

		UID srcFrame = Utils<IKCMStoryEditsFacade>()->GetFirstFrameUID(sourceDB, storyUID);

			// THE OLD SIDE ALSO ASKS FOR "THE FRAME CONTAINING THE CHANGE".
			// GetFirstFrameUID above returns THE STORY'S FIRST FRAME, which is right when what the
			// row points at is the story itself (a parent row) -- but USING IT TO DECIDE THE
			// SPREAD WHEN AIMING AT A CHANGE brings up the wrong spread for a change further down
			// a threaded story. Pasteboard coordinates are per spread, so the landing is not "a
			// little off": it is a different page.
			// So the same question is put to the same door the new side uses
			// (KCMStoryJumpToChange).
			// @warning when it cannot be had, the first frame stands and the behaviour falls back
			//   to what it was (overset, unplaced, and an old document that grew shorter since the
			//   comparison all arrive here).
		if (sourceFocusIndex != kInvalidTextIndex)
		{
			const UID srcFocusFrame =
				Utils<IKCMStoryEditsFacade>()->GetStoryFrameAt(sourceDB, storyUID, sourceFocusIndex);
			if (srcFocusFrame != kInvalidUID)
				srcFrame = srcFocusFrame;
		}

		if (srcFrame != kInvalidUID)
		{
				// WITH "Sync Layout Views" ON, THE SOURCE IS NOT MOVED BY HAND -- the sync observer
				// (KCMViewSync.cpp) has already mirrored the Target scroll just made onto the
				// Source. Moving it here as well doubles it, and that movement gets mirrored BACK
				// onto the Target, pushing away the very frame that was just brought up (the shape
				// that actually happened with an overset).
				// @warning with Sync on, what the Source shows is "the same coordinates as the
				//   Target", so where the story has moved it is strictly speaking somewhere else.
				//   Sync's promise -- the two windows side by side at the same coordinates -- is
				//   the user's explicit instruction, so it wins. KCMSyncCompanionViews makes the
				//   same judgement.
			if (!KCMGetLayoutSync())
			{
					// Aimed at exactly the way the Target was, zoom included.
					// THE OLD SIDE IS BROUGHT TO THE CORRESPONDING CHARACTER TOO: sourceFocusIndex
					// is the change's fSourceStart.
					// @warning THE CHARACTER POSITIONS DIFFER BETWEEN THE TWO VERSIONS, so the
					//   Target's focusIndex must NOT be reused (the character with that number in
					//   the old version is somewhere else entirely). The diff produces both sides'
					//   positions, so both are used -- one question, one answer, and these are two
					//   questions.
					// AN INSERTION HAS AN OLD-SIDE POSITION AS WELL. The caller used to pass
					// kInvalidTextIndex for one, reasoning that there is nowhere on the old side
					// to point at -- but that was about the CHARACTER, not about the PLACE, and
					// the gap where the new words went in is certainly there in the old version.
					// fSourceStart (the start of an empty range, i.e. where the caret goes) now
					// arrives for it too, so nothing branches here.
				UID srcLanded = kInvalidUID;
				KCMScrollDocToStoryStart(sourceDB, storyUID, srcFrame, srcLanded, KCMReadDocZoom(db),
										   sourceFocusIndex);
				if (srcLanded != kInvalidUID)
					srcFrame = srcLanded;
			}

				// The Pages panel is not Sync's business, so it follows either way (it only takes
				// effect inside while the Source is in front; with the Target in front, the call
				// above is the one that took effect).
			KCMScrollPagesPanelToPage(sourceDB, marks->GetFramePageUID(sourceDB, srcFrame));
		}
	}

	// THE WALK'S ANCHOR IS NOT MOVED. A row jump is a different route from Prev/Next, and
	// rewriting the anchor here would mean "press Next and it carries on from wherever the list
	// jumped to" -- behaviour that appears in the description of neither feature.
	return kTrue;
}

//========================================================================================
// KCMNoteStoryStop (declared in KCMChangeNav.h)
//   Record that the walk stands on a list row. Only the jump functions call this, and a click, an
//   arrow key and Prev/Next all go through them (the rule and the reason are on the declaration).
//========================================================================================
void KCMNoteStoryStop(int32 rowIndex, int32 changeIndex)
{
	if (rowIndex < 0)
		return;

	// What the Pixel mode walks is pages, and a list row is not among them -- touching the anchor
	// there would make clicking a row move the page walk. Its rule, that a row jump leaves the
	// anchor alone, stands.
	if (Utils<IKCMCompareFacade>()->GetCompareMode() != kKCMModeStory)
		return;

	// Whether the row exists, its storyUID, and its child count: all three come from the same
	// facade, so it is queried once.
	// ★★★THE GUARD IS Exists(), NOT A nil TEST ON THE RESULT: `QueryUtilInterface()` is
	//   `fFace->AddRef()` with no guard (Utils.h:116), so a nil is dereferenced before there is
	//   a pointer to test. Same correction as KCMCmykCursor.cpp's shutdown path.
	if (!Utils<IKCMStoryEditsFacade>().Exists())
		return;
	InterfacePtr<IKCMStoryEditsFacade> edits(Utils<IKCMStoryEditsFacade>().QueryUtilInterface());

	IKCMStoryEditsFacade::Row row;
	if (!edits->GetRow(rowIndex, row))
		return;		// a click arriving just after the list was rebuilt: that row is gone

	sNavIsStory    = kTrue;
	sNavStoryUID   = row.fStoryUID;
	sNavStoryRow   = rowIndex;		// identified together with the UID (KCMFindCurrentStop explains
									// why all three are checked)
	sNavPageUID    = kInvalidUID;	// the page-side anchor is not carried over (the kind decides, so the value goes too)
	sNavIsOverset  = kFalse;
	sNavOversetOrd = 0;

	if (changeIndex >= 0)
	{
		sNavStoryChange   = changeIndex;
		sNavStoryAtEntry  = kFalse;		// standing on that change itself
	}
	else
	{
		// The row itself was selected. WITH CHILDREN it is not a stop (KCMStoryNav.h), so the walk
		// stands AT THE ENTRANCE TO ITS FIRST CHILD -- the readout shows that child's number and
		// Next goes to it. With no children the row IS the stop, and the walk simply stands on it.
		const int32 changeCount = edits->GetChangeCount(rowIndex);
		sNavStoryChange  = (changeCount > 0) ? 0 : -1;
		sNavStoryAtEntry = (changeCount > 0) ? kTrue : kFalse;
	}

	// Rebuild the readout as well. A CLICK ON A ROW AND AN ARROW KEY HAVE NO OTHER PATH that
	// rewrites "k/N" -- moving the anchor and leaving the readout behind would leave the panel
	// disagreeing with itself.
	// Prev/Next calls this at its own exit too, so it runs twice there; since it only rebuilds
	// from the current state, both give the same value.
	KCMRefreshNavPosition();
}

// Forget where the walk stands (KCMChangeNav.h). The next Next or Prev starts from the front or
// the back of the list.
// THE READOUT IS NOT UPDATED HERE, only the anchor dropped: this is also called part-way through
// a Start, when the comparison is being swapped wholesale, so the readout is brought up to date
// by the caller with one KCMRefreshNavPosition once everything has settled.
void KCMResetNav()
{
	sNavPageUID = kInvalidUID; sNavIsOverset = kFalse; sNavOversetOrd = 0;
	// The Story side goes for the same reason: the list is rebuilt wholesale by every comparison,
	// so neither the story nor the number of the edit from the previous one means anything.
	// @warning forgetting this line would let a walk over a different pair of documents START FROM
	//   A ROW WHOSE UID HAPPENS TO MATCH -- the same shape of defect the note above describes for
	//   page UIDs.
	sNavIsStory = kFalse; sNavStoryUID = kInvalidUID; sNavStoryRow = -1; sNavStoryChange = -1;
	sNavStoryAtEntry = kFalse;
}

// See KCMChangeNav.h. Rebuild the position readout between Prev and Next out of the current list
// of stops and where the walk stands, and update whether the Prev and Next buttons are enabled
// (the value and the button states are assembled in one place, as in KESCL's UpdateNavWidgets).
// What is shown: nothing to walk -> empty / no stops -> "/" / N stops -> "k/N".
void KCMRefreshNavPosition()
{
	PMString text; text.SetTranslatable(kFalse);
	bool16 navEnabled = kFalse;

	IDataBase* navDB = KCMNavDoc();	// non-nil while a comparison is running or Find Overset is on
	if (navDB != nil)
	{
		std::vector<KCMNavStop> stops;
		KCMBuildStops(stops);
		if (stops.empty())
		{
			text.Append("/");	// no stops at all: "/" and the buttons go dead (the user's choice)
		}
		else
		{
				// Where the anchor stands, counting from 1. Nothing walked yet -- it is not in the
				// list -- counts as the front, giving "1/N".
			const int32 cur = KCMFindCurrentStop(stops);
			const int32 shown = (cur < 0) ? 1 : (cur + 1);
			text.AppendNumber(shown);
			text.Append("/");
			text.AppendNumber((int32)stops.size());
			navEnabled = kTrue;	// there is something to walk, so Prev and Next are live
		}
	}
	// Nothing to walk (navDB == nil): the text stays empty and navEnabled false, which clears

	// the readout and disables the buttons.
	KCMSetNavPosition(text, navEnabled);
}

// End of KCMChangeNav.cpp
