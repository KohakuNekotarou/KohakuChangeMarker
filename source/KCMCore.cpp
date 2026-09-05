//========================================================================================
//
//  KCMCore.cpp
//
//  ChangeMarker's shared operations, declared in KCMCore.h. They are plain functions so that a
//  script method and a panel widget observer drive exactly the same behaviour. The work is
//  delegated to the drawing engine (KCMDrawEventHandler) and to the peek module.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"
#include "ISession.h"			// GetExecutionContextSession (KCMIsDocDBOpen)
#include "IActiveContext.h"		// GetContextDocument (KCMActiveDoc)
#include "IApplication.h"		// QueryDocumentList (KCMIsDocDBOpen)
#include "IDocumentList.h"		// FindDocByDataBase = the pointer comparison that tests liveness
#include "IDataBase.h"
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "ITextUtils.h"				// GetPageUIDRef (KCMFramePageUID's main route)
#include "IHierarchy.h"				// passed to GetOwnerPageUID (KCMFramePageUID's fallback)
#include "IGeometry.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IMasterSpreadList.h"		// GetMasterSpreadCount / GetNthMasterSpreadUID (collecting master pages)
#include "IPageList.h"				// GetPageCount / GetNthPageUID (the document's flattened page list)
#include "IBoolData.h"				// reading a spread's hidden state (IID_IHIDESPREADBOOLDATA)
#include "SpreadID.h"				// IID_IHIDESPREADBOOLDATA (an IBoolData on kSpreadBoss; confirmed in the docs' boss list)
#include "PMString.h"
#include "PMRect.h"
#include "PMPoint.h"				// the point passed to PMRect::PointIn (included explicitly rather than indirectly)
#include "IGeometryFacade.h"		// GetItemBounds -- a page's rectangle in pasteboard coordinates (worked example: SnapTracker.cpp:610-616)
// The three view lookups moved to KCMViewLookup.cpp, and the SDK includes only they needed went
// with them (IControlView / IEventUtils / IWindow / IWindowUtils / IDocumentPresentation /
// IPanelControlData / LayoutUIID / ILayoutViewUtils / ILayoutControlData / K2Vector / PMPoint).
// **This file now includes no view header at all**, which is one of the conditions for being on
// the model side.
#include "ProgressBar.h"			// TaskProgressBar (the progress bar and Cancel for a heavy comparison)
#include "ErrorUtils.h"				// PMSetGlobalErrorCode (do not leave a global error standing after a cancel)

#include <vector>
#include <set>
#include <map>						// the pairing maps in KCMDoMarkChangesDoc (explicit, not relied on indirectly)

#include "KCMDrawEventHandler.h"   // the drawing engine and the shared statics
#include "KCMThreadSafety.h"       // needed to walk the entries when the mark colour changes and their cached ring images must be dropped
#include "KCMPeek.h"               // KCMBaseScreenOpacity
#include "KCMPageMap.h"            // KCMBuildPairing (the exclusion pairing) / KCMPageMapCollectRegistered
#include "KCMPageCheck.h"          // ⚠**nothing here calls into it any more** (2026-09-04): Stop stopped clearing the ticks and the prune was removed. Left in place because dropping an include is a change a build has to prove, not a comment
#include "KCMStoryStamp.h"         // the stories' change counters -- whether text was edited, which pixels cannot say
#include "KCMStoryList.h"          // the list of changed stories (the model the Story Edits section reads)
#include "KCMStoryDiffRun.h"       // in the Story mode, what changed inside each row
#include "KCMHideUnchanged.h"      // KCMResetHideUnchanged
#include "KCMExternalSource.h"     // KCMIsDbAlive -- "still there" includes the lent Source
// **No UI header is included here.** Everything this file used to do to the screen is now a
// KCMNotify*() call, so the comparison engine says only WHAT CHANGED and has zero dependency on
// the UI.
#include "KCMCore.h"
#include "KCMID.h"			// kKCM*Message, the notification IDs
#include "KCMModelNotify.h"	// KCMNotifyStatus / KCMNotifyDocs - the model tells the UI, it never calls it

// Declared here and defined next to KCMInvalidateDB, which it is built on: the comparison uses it
// several hundred lines before that point.
static void KCMInvalidateDocs(IDataBase* first, IDataBase* second, IDataBase* third = nil);

//========================================================================================
// Helper: every page UID in the document, flattened in the document's page order.
//
// It is **IPageList**, not a double loop over ISpreadList and ISpread. `IPageList.h:71-74` names
// itself as the route: it "caches commonly needed information about pages in the document. All the
// information is computed only when needed. It is ***much* more efficient to use this than to
// compute the same information from other sources**" -- and walking the spreads to pick up pages
// is precisely one of those "other sources".
//
// **The switch was justified by measurement.** The header's contract stops at "does not include
// master pages" (`:81`) and **says nothing about hidden spreads** (only GetPageIndex has an
// includePagesOfHiddenSpread parameter, `:104`). KCM's flattened page number has to be "the number
// it would have with nothing hidden", that being what the old/new correspondence rests on, so the
// count and the whole UID sequence were compared against the double loop **with two spreads hidden
// by Hide Unchanged**: `[pl=4 walk=4 SAME-ORDER]` -- hidden pages included, and in exactly the same
// order. Full record: docs/ai-notes/kescm-api-audit-b3-2026-08-16.md
//
// @warning it still does not include master pages (`:81` states that as the contract). Three
// callers -- the comparison below, the TSV export and Prev/Next -- say "masters are appended
// separately" and depend on that; the dependency survives the switch, its justification simply
// moved from ISpreadList's behaviour to IPageList's contract.
// @warning out is NOT cleared: callers append the masters after the ordinary pages.
//========================================================================================
void KCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out)
{
	if (db == nil)
		return;
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
		return;
	const int32 n = pageList->GetPageCount();
	if (n <= 0)
		return;
	out.reserve(out.size() + (size_t)n);
	for (int32 i = 0; i < n; ++i)
		out.push_back(pageList->GetNthPageUID(i));
}

//========================================================================================
// KCMCollectChangedPageUIDs (declared in KCMCore.h)
//   If db is one of the documents being compared (sDB/sSrcDB), append every page that could be
//   carrying a mark right now (the change ring, the overflow "/", the registered "/") to outPages
//   and answer kTrue. Otherwise touch nothing and answer kFalse.
//   **"What counts as marked" is defined here and nowhere else.** Add a kind of mark here and both
//   the comparison and the UI's thumbnail purge follow automatically.
//
//   It looks like a UI question and is not: it reads sEntries, the overflow cache and the
//   registered pages, and touches no widget and no view. Living in a UI-side file is what made
//   three model-side files include a UI header just to call it.
//
// **WHY THIS MAY WALK THE SHARED STATE WITHOUT TAKING THE LOCK.** KCMThreadSafety.h's contract is
//   "guard it because the main thread writes and **the background thread (asynchronous PDF export)
//   reads while drawing**" -- and this function is not on that background side. Count the callers
//   and they are all **main thread**:
//     - KCMCollectCheckablePageUIDs, just below, which passes straight through in the Pixel mode.
//       Its own callers are in KCMPageCheck.cpp (the toggle, the menu state, the pruning) and the
//       Load restore; **the pruning one already holds the lock** (KCMPageCheckPruneToMarked).
//     - IKCMMarkData::GetMarkablePageUIDs in KCMFacades.cpp, i.e. from the UI.
//   The writers are main-thread only too, so within one thread nothing changes underneath the
//   walk. Hence no lock.
//   **That is a property of today's callers, not of the structure.** Add one route that calls this
//   from a background thread and it becomes a **read of freed memory** at that moment (sEntries is
//   a map of raw pointers and DropAll deletes them). **Decide which thread a new caller runs on
//   before adding it.**
//========================================================================================
bool16 KCMCollectChangedPageUIDs(IDataBase* db, std::set<UID>& outPages)
{
	const bool16 overflowCacheMatches =
		(KCMDrawEventHandler::sOverflowCacheDB == KCMDrawEventHandler::sDB &&
		 KCMDrawEventHandler::sOverflowCacheSrcDB == KCMDrawEventHandler::sSrcDB);

	if (db != nil && db == KCMDrawEventHandler::sDB)
	{
		for (std::map<UID, KCMOverlayEntry*>::iterator it = KCMDrawEventHandler::sEntries.begin();
			 it != KCMDrawEventHandler::sEntries.end(); ++it)
			outPages.insert(it->first);
		if (overflowCacheMatches)
			outPages.insert(KCMDrawEventHandler::sOverflowT.begin(), KCMDrawEventHandler::sOverflowT.end());
		// Registered pages (Added = the green "/") count too. They are a separate set from sEntries
		// and the overflow one, and leaving them out means a registered page's thumbnail is not
		// purged on a re-comparison, so its green "/" does not appear at once (nor on Start).
		KCMPageMapCollectRegistered(db, outPages);
		return kTrue;
	}
	if (db != nil && db == KCMDrawEventHandler::sSrcDB)
	{
		for (std::map<UID, UID>::iterator it = KCMDrawEventHandler::sSrcPageToTarget.begin();
			 it != KCMDrawEventHandler::sSrcPageToTarget.end(); ++it)
			outPages.insert(it->first);
		if (overflowCacheMatches)
			outPages.insert(KCMDrawEventHandler::sOverflowS.begin(), KCMDrawEventHandler::sOverflowS.end());
		// Registered pages (Removed = the green "/") too, for the same reason.
		KCMPageMapCollectRegistered(db, outPages);
		return kTrue;
	}
	return kFalse;
}

//========================================================================================
// KCMCollectCheckablePageUIDs (declared in KCMCore.h, where the reasoning is)
//   It sits directly below KCMCollectChangedPageUIDs **because the two questions are confusingly
//   close**: whoever edits one should have the other in view.
//   No lock, for the same reason as above (every caller is on the main thread). The Story branch
//   reads two pointers and one enum, so it does not even walk anything.
//========================================================================================
bool16 KCMCollectCheckablePageUIDs(IDataBase* db, KCMCheckablePages& out)
{
	out.fAllPages = kFalse;
	out.fPages.clear();

	if (db == nil)
		return kFalse;

	// ★NOT ONE OF THE COMPARED DOCUMENTS = EVERY PAGE MAY BE TICKED (2026-09-04, user decision).
	//   A tick is the reader's own marker and no longer waits on a comparison: it can be put on a
	//   document nobody is comparing, and it survives Stop.
	//   **This one test covers two cases**, and they are the same case seen twice: nothing started
	//   at all (both pointers nil), and a third document open beside a running comparison. Writing
	//   it as "nothing is armed" would have answered the first and left the second refusing ticks.
	//   ⚠**This answers "may a tick be PUT here", and nothing else.** "May a tick STAY here" was a
	//   second question the same function used to answer, through the prune -- and answering both
	//   with one rule is what made a tick die when a comparison started on the document carrying
	//   it. The prune is gone (KCMPageCheck.h says where and why); a tick stays until someone
	//   clears it.
	if (db != KCMDrawEventHandler::sDB && db != KCMDrawEventHandler::sSrcDB)
	{
		out.fAllPages = kTrue;
		return kTrue;
	}

	if (KCMGetCompareMode() != kKCMModeStory)
	{
		// Pixel = only the pages carrying a mark. Whether db is one of the compared documents is
		// decided by the function called here (kFalse if it is not).
		if (!KCMCollectChangedPageUIDs(db, out.fPages))
			return kFalse;
		// **A master page may always be ticked, difference or no difference.** The Pixel rule
		// (only pages with a ring or a "/") stands for ordinary pages; masters are the exception.
		// The reason: in the Story mode every page can be ticked, masters included, so switching
		// to Pixel took that away again -- the same page could and then could not be ticked
		// depending on the mode.
		std::vector<UID> masters;
		KCMCollectMasterPageUIDs(db, masters);
		out.fPages.insert(masters.begin(), masters.end());
		return kTrue;
	}

	// Story mode = every page. That db is one of the two being compared was settled at the top, so
	// there is nothing left to test here.
	out.fAllPages = kTrue;
	return kTrue;
}

//========================================================================================
// KCMCollectMasterPageUIDs (declared in KCMCore.h)
//   Collect the master spreads' pages. The counterpart of KCMCollectPageUIDs above, and separate
//   on purpose: master spreads live in IMasterSpreadList and never appear in ISpreadList.
//   out is not cleared, so the result appends to the list of ordinary pages.
//========================================================================================
void KCMCollectMasterPageUIDs(IDataBase* db, std::vector<UID>& out)
{
	if (db == nil)
		return;
	InterfacePtr<IMasterSpreadList> masterList(db, db->GetRootUID(), UseDefaultIID());
	if (masterList == nil)
		return;
	const int32 nm = masterList->GetMasterSpreadCount();
	for (int32 m = 0; m < nm; ++m)
	{
		const UID spreadUID = masterList->GetNthMasterSpreadUID(m);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			out.push_back(spread->GetNthPageUID(p));
	}
}

//========================================================================================
// KCMIsPageOnHiddenSpread (declared in KCMCore.h) -- is this page's spread hidden?
//
// KCM's other hidden-spread tests all ask while walking an ISpreadList; asking **from a page UID**
// is this one's job.
// Page -> spread is IHierarchy::GetSpreadUID, whose contract is "the spread of this hierarchy
// node" and is not page-specific. The TSV export's MasterPageDisplay, KCMPeek and KCMChangeNav all
// ask the same way.
// A master page answers kFalse, so callers need no special case: **InDesign has no way to hide a
// master spread**, and IID_IHIDESPREADBOOLDATA is about ordinary spreads on kSpreadBoss. Whether
// the Query comes back nil or comes back kFalse, the answer is the same.
// Master pages DO reach this function -- Prev/Next walks them (KCMBuildStops adds master spread
// pages as stops, for overset and for change frames alike, and the page UID travels on to KCMGoto
// and KCMStopLabel), and a Story Edits row on a master's frame reaches it from KCMStoryJump.
//========================================================================================
bool16 KCMIsPageOnHiddenSpread(IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
	if (pageHier == nil)
		return kFalse;
	const UID spreadUID = pageHier->GetSpreadUID();
	if (spreadUID == kInvalidUID)
		return kFalse;
	// The hidden state is an IBoolData on kSpreadBoss (IID_IHIDESPREADBOOLDATA, kTrue = hidden).
	InterfacePtr<IBoolData> hideFlag(db, spreadUID, IID_IHIDESPREADBOOLDATA);
	return (hideFlag != nil && hideFlag->GetBool()) ? kTrue : kFalse;
}

//========================================================================================
// A page item's UID -> the page UID it sits on, or kInvalidUID when it sits on none (the
// pasteboard, say).
//
// There are two routes because neither alone answers. The main one,
// ITextUtils::GetPageUIDRef, is a purpose-built API that assumes a text frame; the fallback,
// IHierarchy plus ILayoutUtils::GetOwnerPageUID, is the general case. Both answers are verified to
// be kPageBoss before being returned -- GetOwnerPageUID's contract says outright that it
// **returns the spread's UID when the item is not on a page** (ILayoutUtils.h:102-107), and
// without the check that spread UID is mistaken for a page.
//
// KBS deliberately does NOT verify in the same place (KBSSearchEngine.cpp), because it wants to
// spell a hit on the pasteboard as "PB". KCM only needs "a real page or nothing", so it verifies.
// **The two differ because their purposes differ; do not "fix" one to match the other.**
//========================================================================================
UID KCMFramePageUID(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return kInvalidUID;

	// Main route: the purpose-built API, which assumes a text frame.
	const UIDRef pageRef = Utils<ITextUtils>()->GetPageUIDRef(UIDRef(db, frameUID));
	const UID pageUID = pageRef.GetUID();
	if (pageUID != kInvalidUID && db->GetClass(pageUID) == kPageBoss)
		return pageUID;

	// Fallback: IHierarchy -> GetOwnerPageUID (a spread UID when off-page). Only a real page is taken.
	InterfacePtr<IHierarchy> hier(db, frameUID, UseDefaultIID());
	if (hier != nil)
	{
		const UID owner = Utils<ILayoutUtils>()->GetOwnerPageUID(hier);
		if (owner != kInvalidUID && db->GetClass(owner) == kPageBoss)
			return owner;
	}
	return kInvalidUID;	// on no page at all (the pasteboard, say) = skip
}

// The active (front) document and its db. See the comment in KCMCore.h.
IDocument* KCMActiveDoc()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ac = session ? session->GetActiveContext() : nil;
	return ac ? ac->GetContextDocument() : nil;
}

IDataBase* KCMActiveDocDB()
{
	IDocument* doc = KCMActiveDoc();
	return doc ? ::GetUIDRef(doc).GetDataBase() : nil;
}

// Is pt (pasteboard coordinates) inside this page's own box? Asked of ordinary pages and of
// master pages by the two loops in KCMFindPageUnderMouse below, which used to spell it out twice
// -- and only one of the two copies carried the warning that follows.
//
// Getting a page's rectangle in pasteboard coordinates is the Facade's job; the worked example
// snapshot/SnapTracker.cpp:610-616 does exactly this **for a page**, and :616-617 does the
// containment test with the same official pair, **PMRect::Normalize()** (PMRect.h:622) and
// **PMRect::PointIn()** (:814-816, a closed interval compared with PMReal's epsilon).
// @warning the nil test and the Normalize both stay: the Facade guarantees neither that the item
//   has geometry nor that the rectangle comes back normalised.
// @warning **do not drop the Normalize.** PointIn is a plain comparison that assumes
//   left <= right and top <= bottom, so an un-normalised box makes it **always kFalse** -- no page
//   is ever hit, and nothing reports an error.
static bool16 KCMPageContainsPoint(IDataBase* db, UID pageUID, const PMPoint& pt)
{
	InterfacePtr<IGeometry> geo(db, pageUID, UseDefaultIID());
	if (geo == nil)
		return kFalse;

	PMRect bb = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		::GetUIDRef(geo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
	bb.Normalize();
	return bb.PointIn(pt);
}

bool16 KCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KCMPageHit& out,
                               UID onlySpreadUID)
{
	out.spreadIndex = -1; out.spreadUID = kInvalidUID; out.numPages = 0;
	out.globalPageBase = 0; out.hitPageIndex = -1; out.hitPageUID = kInvalidUID;
	out.isMaster = kFalse;
	if (targetDB == nil)
		return kFalse;
	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kFalse;
	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	const PMPoint pt(mx, my);	// what PMRect::PointIn is asked about, in both loops below

	// **The restriction is by KIND (master / ordinary) and by nothing finer.**
	// **Only master and ordinary overlap** in pasteboard coordinates (measured; see KCMCore.h).
	// **Two ordinary spreads never do** -- the evidence being that this walk covered every ordinary
	// spread for its whole life before any restriction existed and never once picked the wrong one.
	// So: viewing a master means "no ordinary spreads, and only that master"; viewing an ordinary
	// spread means "no masters, and **all** the ordinary ones". kInvalidUID walks everything.
	// @warning restricting to "the pages of that one spread" instead throws away
	//   ordinary-to-ordinary hits, and then **CMYK reads `---` and Shift+ peek does nothing on any
	//   page except the one being viewed**, even with several spreads on screen.
	// This costs **one extra Query**: the master list must now be fetched to decide the kind, where
	//   before it was only fetched if the ordinary loop missed. It is one Query on the same db's
	//   root UID, against the ISpreadList, the per-spread ISpread and the per-page IGeometry this
	//   already does -- and deciding the unit of restriction in one place is worth it
	//   ([[one-question-one-place]]).
	// The test is shaped after KCMScrollMap.cpp's KCMIsMasterSpread (which likewise avoids
	//   IMasterSpreadList::GetMasterSpreadIndex, whose header does not say what it returns for a
	//   UID that is not a master).
	InterfacePtr<IMasterSpreadList> mList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	const int32 nm = (mList != nil) ? mList->GetMasterSpreadCount() : 0;
	bool16 viewingMaster = kFalse;
	if (onlySpreadUID != kInvalidUID)
	{
		for (int32 m = 0; m < nm; ++m)
		{
			if (mList->GetNthMasterSpreadUID(m) == onlySpreadUID)
			{
				viewingMaster = kTrue;
				break;
			}
		}
	}
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(targetDB, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();

		// Skip the ordinary spreads **only while a master is being viewed** (they overlap it; the
		// reasoning is in KCMCore.h).
		// @warning keep adding to globalIndex -- the flattened page number must not depend on
		//   whether anything was skipped.
		if (onlySpreadUID != kInvalidUID && viewingMaster)
		{
			globalIndex += np;
			continue;
		}

		// Hidden spreads (Hide Unchanged Spreads, or the Pages panel's Hide Spread) are excluded
		// from hit-testing: hiding one re-lays out the visible spreads, but the hidden one's old
		// coordinates stay where they were and get hit first, which throws off the old/new
		// correspondence (the flattened page number) that peek, re-comparison and the colour
		// sampler all use.
		// The page count is still added (below), so the flattened number stays "the number it
		// would have with nothing hidden" and the correspondence with the older document holds.
		// The hidden state is an IBoolData on kSpreadBoss (IID_IHIDESPREADBOOLDATA, kTrue = hidden).
		InterfacePtr<IBoolData> hideFlag(targetDB, spreadUID, IID_IHIDESPREADBOOLDATA);
		if (hideFlag != nil && hideFlag->GetBool())
		{
			globalIndex += np;
			continue;
		}

		// Reject on **the spread's box** first. Worked example: snapshot/SnapTracker.cpp:599-600
		// asks "is the point on this spread" with GetPagesBounds + PointIn.
		// It removes the per-page measurement of every spread that cannot contain the point, and
		// peek and the colour sampler come through here **on every mouse move**.
		// @warning globalIndex must be added here too (the flattened page number cannot depend on
		//   what was skipped, or the correspondence with the older document's page list breaks).
		// @warning the question is "is it on a PAGE", so it is **GetPagesBounds**. The wider
		//   GetPagesAndItemsBounds, which includes items on the pasteboard, is what KBS's overset
		//   marker wants and is too wide here.
		PMRect spreadBounds = spread->GetPagesBounds(Transform::PasteboardCoordinates());
		spreadBounds.Normalize();
		if (!spreadBounds.PointIn(pt))
		{
			globalIndex += np;
			continue;
		}

		// Is the mouse on one of this spread's pages? (The first page hit wins.)
		for (int32 p = 0; p < np; ++p)
		{
			const UID pageUID = spread->GetNthPageUID(p);
			if (KCMPageContainsPoint(targetDB, pageUID, pt))
			{
				out.spreadIndex    = s;
				out.spreadUID      = spreadUID;
				out.numPages       = np;
				out.globalPageBase = globalIndex;
				out.hitPageIndex   = p;
				out.hitPageUID     = pageUID;
				return kTrue;
			}
		}
		globalIndex += np;
	}

	// **Master spreads are hit-tested too.** The comparison itself has handled masters (paired by
	// name, KCMBuildMasterPairing) since well before this function did, and while it did not, peek
	// and CMYK produced nothing on a master page even though the difference frames were drawn there.
	//
	// @warning **the order is not what makes this correct.** The two sets of rectangles OVERLAP
	//   (measured: with a master spread on screen and no restriction, the walk hits an ordinary
	//   page), so whichever kind is examined first, the other is misread while it is being viewed.
	//   What makes it correct is the onlySpreadUID restriction (the full reasoning is in KCMCore.h).
	//   Examining the ordinary spreads first only means a caller that passes kInvalidUID gets the
	//   same answer it always did.
	//
	// @warning no hidden-spread test here -- InDesign cannot hide a master spread, and
	//   IID_IHIDESPREADBOOLDATA is about ordinary spreads on kSpreadBoss.
	// @warning globalIndex is NOT added to -- a master is not in the flattened page list
	//   (IPageList), so it has no number.
	// mList and nm were fetched at the top of the function, because deciding the kind needs them.
	for (int32 m = 0; m < nm; ++m)
	{
		const UID msUID = mList->GetNthMasterSpreadUID(m);
		// Viewing an ORDINARY spread means no master is examined; viewing a MASTER means only that
		// one is (and there is no flattened number to add for either case).
		if (onlySpreadUID != kInvalidUID && (!viewingMaster || msUID != onlySpreadUID))
			continue;
		InterfacePtr<ISpread> ms(targetDB, msUID, UseDefaultIID());
		if (ms == nil)
			continue;
		const int32 mp = ms->GetNumPages();

		// The same two stages as for an ordinary spread: reject on the spread's box, then test each
		// page for containment.
		PMRect msBounds = ms->GetPagesBounds(Transform::PasteboardCoordinates());
		msBounds.Normalize();
		if (!msBounds.PointIn(pt))
			continue;

		for (int32 p = 0; p < mp; ++p)
		{
			const UID pageUID = ms->GetNthPageUID(p);
			if (KCMPageContainsPoint(targetDB, pageUID, pt))
			{
				out.spreadIndex    = -1;			// a master is not in the spread list
				out.spreadUID      = msUID;
				out.numPages       = mp;
				out.globalPageBase = -1;			// and has no flattened page number
				out.hitPageIndex   = p;
				out.hitPageUID     = pageUID;
				out.isMaster       = kTrue;
				return kTrue;
			}
		}
	}
	return kFalse;
}

//========================================================================================
// The shared core operations (declared in KCMCore.h).
//
// These were once written inline inside the script methods. They are plain (non-static) functions
// so that the panel's widget observer drives exactly the same behaviour. Keeping them in this
// translation unit is deliberate: it gives them direct access to the drawing engine
// (KCMDrawEventHandler) and to the file-local peek state.
//========================================================================================

/* KCMRebuildStoryEdits
	Read both documents' story counters, work out which stories differ, and put the answer on screen.

	ONE PLACE, TWO CALLERS. The full comparison below calls it, and so does "Refresh Page
	Comparison" (KCMPeek.cpp) - which does NOT go through KCMDoMarkChangesDoc but re-compares the
	selected pages on its own. Written out twice, the two would drift; and while only the comparison
	had it, a Refresh left the list showing the state before the edit. The nav position beside it is
	shared for exactly this reason.

	The list is rebuilt whole rather than patched, because stories do not divide up by page: one
	story can run across the pages that were refreshed and the pages that were not.

	Reading the counters composes nothing, so this costs a walk of the story list and no more.
*/
void KCMRebuildStoryEdits(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (targetDB == nil || sourceDB == nil)
		return;

	std::vector<KCMStoryStamp> targetStamps;
	std::vector<KCMStoryStamp> sourceStamps;
	KCMStoryEdits::CollectStamps(targetDB, targetStamps);
	KCMStoryEdits::CollectStamps(sourceDB, sourceStamps);

	// @warning the argument order is (source, target). Reversed, "added" and "removed" swap: a
	//   story that was deleted is counted as added, and the real additions are silently lost.
	std::vector<KCMStoryDiff> storyDiffs;
	KCMStoryEdits::Compare(sourceStamps, targetStamps, storyDiffs);

	// **Both documents are passed.** Every row is a Target story except a REMOVED one, which does
	// not exist in the Target, so its text, first frame and page are read from the Source. Which
	// document a row is read from is decided by its fKinds, inside Build (as are the page ordering
	// and the extraction of the leading text).
	KCMStoryList::Build(targetDB, sourceDB, storyDiffs);

	// **Only in the Story mode**, annotate each row with what changed inside it. The counters can
	// only answer "this story changed"; which words changed needs the text itself compared. In the
	// Pixel mode this is not called, so rows have no children and the list stays flat.
	// @warning it must run **after Build**. A change names its row by position in the sorted list,
	//   so running it before the order is settled attaches it to the wrong row.
	if (KCMGetCompareMode() == kKCMModeStory)
		KCMStoryDiffRun::Run(targetDB, sourceDB);

	// **Drop the rows where only formatting moved** (the reader asked for attribute changes to be
	// ignored). The counters answer "not identical", so changing a font, a colour, a style or a
	// table's rules puts a story in the list as well, up to this point.
	// **What survives is text changes, ruby and kenten.** (Kenten was reported briefly in August,
	// withdrawn, and reported again from 2026-09-01 -- so a story where only the emphasis marks
	// moved now stays, where a font-only one still drops.)
	// @warning it must be **after Build and after Run**. Earlier, a Story-mode row whose ruby alone
	//   changed still looks like an attribute-only row and is dropped just before the diff would
	//   have found that ruby.
	// The test itself is in `KCMStoryRowFilter.h` (mode-blind, and tested outside InDesign).
	// @warning in the Pixel mode no diff runs, so **a ruby-only change drops here** -- the Pixel
	//   mode reports text changes and gives up the rest.
	KCMStoryList::DropRowsWithNoContentChange();

	// Once the model is built, say so. It is safe to do with the panel closed or the section
	// collapsed (both give up quietly inside), so the caller does not have to know whether anything
	// is open.
	// The COUNT goes in the heading, not on the status line: the status area's four lines are
	// already full and one more would push `failed=N` out of the frame, and a heading can be read
	// with the section collapsed.
	// The model says only "the list was rebuilt"; where and how that shows is the UI's decision.
	KCMNotify(kKCMStoryEditsRebuiltMessage);
}

ErrorCode KCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport, bool16 allowIncremental)
{
	if (targetDB == nil || sourceDB == nil)
		return kFailure;

	// Rasterising every page can trigger the lazy recompose of a story that was never composed --
	// "asking composes it, and composing dirties the document". KCM's design rests on never
	// modifying the model and never dirtying it, so if it was clean going in it is clean coming out.
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

	// **The set of pages that carried a mark BEFORE this runs is not saved, and that costs the UI
	// the ability to purge only what changed.** Re-pairing (registering a page, say) can shift the
	// correspondence by one, and then a page that leaves the overflow set (its red "/" goes) or that
	// pairs back to "no change" (its ring goes) is in NO current-state set at all -- so asking after
	// the fact cannot recover it, and its stale thumbnail would stay on screen.
	// The UI therefore purges **every page** instead: nothing can be missed, it is merely slower in
	// proportion to the page count (KCMPurgeAllPageThumbs in KCMThumbnailRefresh.h).
	// The reason is **not** that a notification can only carry a ClassID -- it can carry more, on
	// ISubject::Change's changedBy. The reason is that **the set is not in our hands**: producing it
	// means saving it here first, which is not done.
	// Contrast the PARTIAL re-comparison (KCMRefreshComparisonCore), which DOES send its sets: it
	// decides which pages it will touch before it starts, so it has them from the outset.
	// @warning IKCMMarkData now exists, so this could be narrowed again by saving the old set here
	//   and sending it with the notification. Still to do.

	// Whether a differential re-comparison is possible: only for the register toggle
	// (allowIncremental=kTrue), and only when the document pair is the one the last comparison used
	// and its pairing is still there. Everything else (Start, the Ignore Page Number Marker toggle,
	// a different pair, no previous pairing) rasterises every page as before.
	const bool16 doIncremental =
		allowIncremental &&
		KCMDrawEventHandler::sDB == targetDB &&
		KCMDrawEventHandler::sSrcDB == sourceDB &&
		!KCMDrawEventHandler::sPrevPairTargetToSource.empty();

	// A re-comparison makes "which spreads are unchanged" stale, so any spread hidden by
	// "Hide Unchanged Spreads" is shown again first and the toggle goes off (nothing hidden, nothing
	// to do).
	KCMResetHideUnchanged(kTrue);

	// Pair the two documents' pages through the exclusion table (registered pages -- those with no
	// partner -- taken out, the rest matched in order). Used by both the differential and the full
	// path, and recorded at the end as the next run's previous pairing.
	std::vector<UID> tPages, sPages;
	KCMBuildPairing(targetDB, sourceDB, tPages, sPages);

	// Append the master spreads' pages. Without this masters are never compared at all
	// (KCMCollectPageUIDs excludes them, `IPageList.h:81`), and the only frame ever seen on a master
	// was the overset "+".
	// **Appending is all that is needed**: the comparison loop, the progress bar's total, the
	// differential cache (sPrevPairTargetToSource) and the Source-side mapping (sSrcPageToTarget)
	// all work off tPages/sPages indices, and MakeEntry sees only a page's UIDRef -- it does not
	// care whether that page is an ordinary one or a master.
	// It is NOT appended inside KCMBuildPairing: that function's contract is "the exclusion pairing
	// for ordinary pages" and it has other callers, the TSV export among them. Appending is the
	// caller's job.
	{
		std::vector<UID> tMaster, sMaster;
		KCMBuildMasterPairing(targetDB, sourceDB, tMaster, sMaster);
		tPages.insert(tPages.end(), tMaster.begin(), tMaster.end());
		sPages.insert(sPages.end(), sMaster.begin(), sMaster.end());
	}

	const size_t n = tPages.size();	// each Build function truncates to the shorter side, so both are the same length

	// The new pairing as a map (an O(1) reverse lookup for the differential path, and what gets
	// recorded at the end).
	std::map<UID, UID> newMap;
	for (size_t i = 0; i < n; ++i)
		newMap[tPages[i]] = sPages[i];

	// Comparing rasterises pages synchronously and takes time, so "Comparing changes..." goes on the
	// panel's status line before the loop and is force-redrawn so that it is actually visible while
	// the loop blocks. A differential run rasterises few pages and finishes at once, but saying so
	// does no harm.
	{
		KCMSayStatus("Comparing changes...", kTrue /*forceRedrawNow*/);
	}

	// Settle up front which pages will actually be rasterised (as indices into tPages/sPages). That
	// is the progress bar's total, and it lets the differential path decide membership once instead
	// of testing inside the rasterising loop.
	std::vector<size_t> toRaster;
	if (doIncremental)
	{
		// DIFFERENTIAL. Match the previous pairing (oldMap) against this one (newMap). A page whose
		// pair is unchanged keeps its overlay (or its "no change, no entry") and MakeEntry is not
		// called for it.
		const std::map<UID, UID>& oldMap = KCMDrawEventHandler::sPrevPairTargetToSource;

		// (1) Discard: a previously paired target whose pair has gone or whose partner changed loses
		//     its entry. MakeEntry does not remove an existing entry when nothing differs, so a page
		//     whose partner changed has to be cleared here first.
		for (std::map<UID, UID>::const_iterator it = oldMap.begin(); it != oldMap.end(); ++it)
		{
			std::map<UID, UID>::const_iterator nit = newMap.find(it->first);
			if (nit == newMap.end() || nit->second != it->second)
				KCMDrawEventHandler::DropOneEntry(it->first, it->second);
		}

		// (2) Recompute: only the targets that had no previous pair or whose partner changed. Pages
		//     whose pair is unchanged are not touched -- reusing them, and so not rasterising them,
		//     is the whole speed-up.
		for (size_t i = 0; i < n; ++i)
		{
			std::map<UID, UID>::const_iterator oit = oldMap.find(tPages[i]);
			if (oit == oldMap.end() || oit->second != sPages[i])
				toRaster.push_back(i);
		}
	}
	else
	{
		// FULL. A whole-document replacement (Start, the Ignore Page Number toggle, or a fallback).
		KCMDrawEventHandler::DropAll();
		KCMDrawEventHandler::sDB = targetDB;
		// The document is being replaced wholesale, so Prev/Next's cursor must go too: carrying the
		// old document's page UIDs over would start the walk from a wrong position when a UID
		// happens to match in the new one. (The differential path is the same document, so it does
		// not touch it.)
		// The cursor is UI-side state and cannot be discarded here; it travels on the notification
		// at the end as navReset, and the UI discards it. The condition is exactly being in this
		// else, i.e. `!doIncremental`.
		toRaster.reserve(n);
		for (size_t i = 0; i < n; ++i)
			toRaster.push_back(i);
	}

	// **The Story mode rasterises no page at all.**
	//
	//   Everything up to here is shared by both modes, and has to be -- none of it can be skipped:
	//     - the page pairing (tPages/sPages) ... peek and the original-folio badge ride on it
	//     - the overflow cache               ... which pages get a "/"
	//     - DropAll and replacing sDB        ... this is where the previous mode's frames are dropped
	//   The only difference is whether each paired page is drawn and its pixels compared, so it is
	//   that step's input, toRaster, which is emptied. The loop runs zero times and no progress bar
	//   appears (rasterCount = 0).
	//
	//   @warning the branch belongs HERE, not inside the two above: those decide WHICH PAGES to
	//     compare, and the mode decides WHETHER to compare at all. Folded in, the same condition
	//     would have to be written in both the differential and the full branch.
	if (KCMGetCompareMode() == kKCMModeStory)
		toRaster.clear();

	// A heavy comparison gets a progress bar with a Cancel. The total is "the pages about to be
	// rasterised" (for a differential run, only the ones being recomputed). The title is fixed
	// English like KCM's other strings, hence SetTranslatable(kFalse).
	// **showImmediate (the third argument) does not mean "appear if this takes a while".** kFalse
	// (the default) means "never appear" -- a 100-page comparison showed no bar at all. So the
	// decision is ours, from the threshold kKCMProgressBarMinPages (KCMConstants.h): no bar for the
	// few pages a register toggle re-compares, always one for a real comparison.
	const int32 rasterCount = (int32)toRaster.size();
	const bool8 showBar = (rasterCount >= kKCMProgressBarMinPages) ? kTrue : kFalse;
	PMString barTitle(rasterCount == 1 ? "Comparing 1 page..." : "Comparing pages...");
	barTitle.SetTranslatable(kFalse);
	TaskProgressBar progress(barTitle, rasterCount, showBar);
	progress.DisableChildProgressBars(kTrue);	// stop the rasterising internals raising bars of their own

	bool16 cancelled = kFalse;
	int32 changedCount = 0;
	int32 failedCount = 0;
	for (size_t k = 0; k < toRaster.size(); ++k)
	{
		const size_t i = toRaster[k];

		PMString item("Page ");
		item.AppendNumber((int32)(k + 1));
		item.Append(" / ");
		item.AppendNumber(rasterCount);
		item.SetTranslatable(kFalse);	// it contains numbers, so it is not for translation
		progress.DoTask(item);			// advance one (which also registers the previous one as finished)

		bool16 changed = kFalse;
		const ErrorCode mkErr =
			KCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
		if (mkErr != kSuccess)
		{
			// A page that could NOT be compared (mismatched page sizes, a failed rasterisation, out
			// of memory) must not be confused with one that did not change. It is dropped from this
			// run's pairing so that the next differential run compares it again -- left in, it would
			// be judged "pair unchanged, reuse the previous result" and the failure would set as
			// "compared, no difference". The count is reported below as failed=N.
			newMap.erase(tPages[i]);
			++failedCount;
		}
		else if (changed)
			++changedCount;

		// Test for a cancel at the safe point, having finished one page: WasCancelled pumps events,
		// so it must not be asked in the middle of a rasterisation. The kFalse argument means "do
		// not raise a global error state" -- raised, it drags subsequent commands down with it.
		// **Do not test after the LAST page.** A cancel on this route means "discard every mark and
		// go back to Stop", so catching a press that lands just after the final page **throws away
		// a comparison that is already complete** (after 100 pages, all of it). With nothing left to
		// do there is nothing to interrupt, so the test has no meaning there.
		// (The Refresh route in KCMPeek.cpp is designed to KEEP what it has already refreshed, so a
		//  press at its end costs nothing -- the status line just says "- cancelled". That one is
		//  right as it stands.)
		if (k + 1 < toRaster.size() && progress.WasCancelled(kFalse))
		{
			cancelled = kTrue;
			break;
		}
	}
	// On a differential run the pages that were not rasterised (the reused results) still count
	// towards how many pages currently differ.
	if (doIncremental && !cancelled)
		changedCount = (int32)KCMDrawEventHandler::sEntries.size();

	if (cancelled)
	{
		// Cancel: do not leave a mixture of compared and uncompared pages. Every mark is discarded
		// and the state goes back to "not compared", so that the screen never shows something where
		// "unchanged" and "not looked at yet" are indistinguishable.
		// DropAll discards the previous pairing too, so the next comparison is a full one and
		// nothing is missed differentially. **Not recording newMap here is the point.**
		KCMDrawEventHandler::DropAll();
		changedCount = 0;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// do not carry an error raised by the interruption
		// **The Story Edits list does NOT need clearing here**, although reading this function alone
		//   suggests it does (the marks all go, so a list left standing would show rows pointing into
		//   two documents that are no longer being compared, and those rows can be clicked).
		//   Opening all four callers shows it cannot happen. When this returns kFailure (i.e. a cancel):
		//     - Start (KCMStartComparisonFor) ... does not arm. Before it, nothing was armed, so the
		//       list is empty. (The book comparison's "Start Change Marker",
		//       KCMBookStartComparisonForRow, likewise Stops before it starts.)
		//     - the register toggle (KCMPageMapToggleSelectedPages) ... goes back to Stop through
		//       KCMToggleStartStop()
		//     - Load Check & Register (KCMPageCheckLoadFromFile) ... the same
		//     - the Ignore toggle (the UI's KCMActionComponent) ... the same
		//   All four end at Stop, so KCMDoClearMarks's KCMStoryList::Clear() always runs.
		//   Measured: cancelling a 30-page re-comparison at the progress bar takes the heading from
		//   "Story Edits (3)" back to "Story Edits".
		//   @warning callers are named rather than cited by line here **because line numbers go
		//     quietly wrong**: three of the four citations that used to be here had drifted, one of
		//     them by 38 lines onto a different function's entry, and one drifted the same day it
		//     was verified because an edit landed in the file it pointed at.
	}
	else
	{
		// Record this pairing for the next differential run (from both paths).
		KCMDrawEventHandler::sPrevPairTargetToSource.swap(newMap);

		// MakeEntry fills sSrcDB and the mapping as it registers a changed page, but the db itself
		// is set explicitly even when nothing changed (with no entries the Source-mark test fails on
		// emptiness, so this costs no drawing).
		// @warning no mark-state lock is needed for this assignment. The same assignment inside
		//   MakeEntry sits inside the lock, but what that lock protects is the sSrcPageToTarget
		//   beside it (a std::map, whose insert walks the tree); sSrcDB merely shares the scope.
		//   Assigning one pointer is safe whichever value the reader (the drawing) sees -- the old
		//   one means the Source frames do not appear, the new one means they do, and nothing else.
		//   Without this note the spot has already been misdiagnosed once as a missing lock.
		KCMDrawEventHandler::sSrcDB = sourceDB;
	}

	// A page that failed to rasterise must not leave an error standing either -- the same treatment
	// as the cancel above (they were once treated differently, which is one question with two
	// answers).
	// The failures are reported to the reader as failed=N, so **there is no need to report them
	// through the error state as well**; left raised, the caller's next command is dragged down with
	// it (CmdUtils.h:72-77 -- a protective shutdown, or a whole sequence rolled back).
	// @warning there are two kinds of failure and only the second can raise anything:
	//   1. mismatched page sizes (wth != wsh) ... the rasterisation itself succeeded, so nothing is raised;
	//   2. SnapshotUtilsEx::Draw failing, or out of memory ... can raise (it is inside the SDK, so
	//      there is no way to check).
	// Falling to the safe side of what cannot be measured. It is safe to clear because this function
	// does not distinguish failures in its return value (kFailure means a cancel) and reports them
	// fully in the report.
	if (failedCount > 0)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	// Rebuild the overflow ("/") cache from the current pairing. This is the one re-comparison route
	// that Start, register add/clear and the Ignore toggle all pass through, so afterwards the
	// drawing has the current overflow (and EnsureOverflowCache keeps a full walk out of each draw).
	KCMDrawEventHandler::RebuildOverflowCache();

	// ★**THE TICKS ARE NOT TOUCHED** (2026-09-04). A page losing its mark in this re-comparison
	//   used to lose its tick with it -- "the frame is gone, and the memory of having checked it
	//   goes with it". That reading died with the tick's own meaning: it marks "I have looked at
	//   this page", and looking at a page is not undone by the page turning out to be unchanged.
	//   ⚠**Two things went with the prune**: switching Story -> Pixel no longer unticks anything,
	//   and a tick made before any comparison survives the Start that follows it (which is what
	//   the removal was actually for -- without it, ticking a document and then comparing it threw
	//   the ticks away at the moment the comparison began).

	KCMInvalidateDocs(targetDB, sourceDB);	// the Source too, so its always-on frames update at once

	PMString report;
	report.SetTranslatable(kFalse);
	if (cancelled)
	{
		// Say both that it was cancelled and that the marks are gone, so the reason the frames
		// disappeared is visible.
		report.Append("comparison cancelled");
		report.AppendW(UTF32TextChar(0x0A));	// newline -> second line
		report.Append("marks cleared");
	}
	else
	{
		report.Append("marks start");
		report.AppendW(UTF32TextChar(0x0A));	// newline -> second line

		// In the Story mode, do not report how many pages were compared -- none were, and
		// "pages compared=100 changed=0" is not false but reads as "100 pages were compared and did
		// not differ". Report what was counted.
		const bool16 storyMode = (KCMGetCompareMode() == kKCMModeStory);
		if (!storyMode)
		{
			report.Append("pages compared="); report.AppendNumber((int32)n);
			report.Append(" changed="); report.AppendNumber(changedCount);
			// Pages that could not be compared are not hidden: "no frame" does not mean "unchanged"
			// for them.
			if (failedCount > 0)
			{
				report.Append(" failed="); report.AppendNumber(failedCount);
			}
		}

		// The Story Edits list. A pixel comparison answers "this page looks different" and cannot
		// tell whether the text changed or only the layout moved. The two complement each other: a
		// story can be unchanged while the page moves, and a page can look the same while the text
		// changed.
		KCMRebuildStoryEdits(targetDB, sourceDB);

		// The Story mode's report can only be built **after** that -- the counts do not exist until
		// the list does.
		if (storyMode)
		{
			const int32 storyCount = KCMStoryList::GetRowCount();
			int32 editCount = 0;
			for (int32 i = 0; i < storyCount; ++i)
			{
				const KCMStoryRow* row = KCMStoryList::GetRow(i);
				if (row != nil)
					editCount += static_cast<int32>(row->fChanges.size());
			}

			report.Append("stories changed="); report.AppendNumber(storyCount);
			report.Append(" edits="); report.AppendNumber(editCount);

			// **"Zero differences" and "could not locate them" are different things.** Counters that
			// moved with no textual difference found means formatting only, or a story that could
			// not be compared (no partner, too different, a failed length check). Printing a bare 0
			// reads as "the text did not change".
			if (storyCount > 0 && editCount == 0)
				report.Append(" (no text differences located)");
		}
	}
	outReport = report;

	// The model's work ends here. **Rebuilding the screen is one notification, and the UI's job**:
	// discarding the view-sync cache, injecting and redrawing the scrollbar strips, the Pages panel
	// thumbnails, the panel display, the Prev/Next position. The order is the listener's
	// (KCMModelChangeObserver).
	//
	// @warning navReset is `!doIncremental`. The Prev/Next position is rebuilt from the settled set
	//   of changed pages every time (Start, a differential re-comparison, register add/remove and
	//   Check all pass through here, so the walk follows the set without the reader pressing
	//   anything), but **only a full re-comparison also discards the cursor** and returns to an
	//   unvisited "1/N". Discarding it on a differential run would send the walk back to the start
	//   every time a page is registered.
	// @warning it is emitted after a cancel too -- both the marks that were built and the marks that
	//   went have to reach the screen (this function does the same clean-up either way).
	//
	// @warning **"cancelled AND differential" is the one case where navReset is kFalse** even though
	//   DropAll removed every mark, so read here alone it looks as though Prev/Next would start from
	//   a page that no longer exists. It cannot: all four callers go back to Stop afterwards
	//   (this returns kFailure), and Stop emits Cleared with navReset=kTrue, which discards the
	//   cursor there. Changing this expression to `!doIncremental && !cancelled` would put the same
	//   clean-up in two places, and one of them would eventually be fixed alone
	//   ([[one-question-one-place]]).
	KCMNotifyDocs(kKCMMarksRebuiltMessage, targetDB, sourceDB, !doIncremental);
	// (A notification telling the transparency manager to re-examine the item used to be emitted
	//  here, and was removed.
	//  @warning **that list is document data and PERSISTS in the .indd** (measured: compare, save,
	//    re-open, and it is still there, and opening does not re-validate it). Kept on for the whole
	//    duration of a comparison, it is **baked in the moment the reader saves** -- and it stays
	//    there for someone who does not have KCM at all.
	//  So it is now declared **only for the duration of an export or a print**; see section 5) of
	//  KCMRingAdornment.cpp. The flattener is only needed for those two outputs -- neither the screen
	//  nor the thumbnails need the declaration.)
	// A cancel returns kFailure. The Start route reads that return value to decide whether to arm,
	// so always returning kSuccess would leave it "armed after a cancel", with the menu stuck on Stop.
	return cancelled ? kFailure : kSuccess;
}

// Is this database still an open document's (declared in KCMCore.h)? A closed one must never be
// dereferenced, so this is a pointer comparison against IDocumentList -- and, since 2026-09-02,
// against the one lent Source (KCMExternalSource.h) -- and nothing more.
// **The nil guard on the session is required**: this is called from KCMScrollMapView::Draw and from
// the deferred thumbnail idle task, both of which can fire during the application's teardown. On a
// platform whose session is already dismantled (the Mac's Cocoa teardown order in particular) an
// unguarded dereference is a crash on quit. Not being able to resolve it means teardown is under
// way, and answering "not open" is the safe side.
bool16 KCMIsDocDBOpen(IDataBase* db)
{
	if (db == nil)
		return kFalse;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	// A nil list means teardown is under way, and "not open" is the safe side -- so the lent
	// Source is not consulted either: nothing may be drawn on it once the session is going.
	return (docList != nil) ? KCMIsDbAlive(docList, db) : kFalse;
}

// kTrue while the application is shutting down (kQuitting = after QuitCmd's Terminate,
// kShuttingDown = after the event loop stops). The close-all phase of a quit, where the reader can
// still cancel at a save prompt, is still kRunning and so kFalse -- there the ordinary full
// clean-up runs, as it does for a normal close.
// While this is kTrue the teardown order of windows and panels is platform-dependent (the Mac's
// Cocoa order is not the Windows one), so no UI work may be done: no widgets, no redraws, no idle
// tasks booked.
bool16 KCMAppIsQuitting()
{
	// The session is nil-guarded too: a function whose whole purpose is shutdown safety cannot
	// dereference blindly. Not even resolving the session means teardown is under way, so fall to
	// the safe side and answer "shutting down".
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return kTrue;
	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return kTrue;	// the application cannot be resolved either -- teardown; fall to the safe side
	const IApplication::ApplicationStateType st = app->GetApplicationState();
	return (st == IApplication::kQuitting || st == IApplication::kShuttingDown) ? kTrue : kFalse;
}

// Redraw the views of db's document, if db is not nil. The shared helper that lets Clear, the
// print-mark toggles and the peek disarm redraw both "the caller's document" (the one that was
// active when the reader acted) and "the document the marks are actually on" -- they differ when
// the Source, or an unrelated third document, is in front.
void KCMInvalidateDB(IDataBase* db)
{
	if (db == nil)
		return;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);
}

// Redraw the documents a mark change reaches, skipping the repeats. The three are typically the
// document the marks are on, the caller's own (the one that was active when the reader acted) and
// the Source; the third is left out for the two-document form.
//
// **The order matters and is not the argument order by accident**: the first is redrawn first, and
// it is meant to be the document the marks are actually on, so its appearance updates even when
// the caller's own document is a different one. Four call sites wrote this out, each with its own
// spelling of "have I already done this one", which is where a document gets redrawn twice or
// missed. nil is fine anywhere -- KCMInvalidateDB above returns at once for it.
static void KCMInvalidateDocs(IDataBase* first, IDataBase* second, IDataBase* third)
{
	KCMInvalidateDB(first);
	if (second != first)
		KCMInvalidateDB(second);
	if (third != nil && third != first && third != second)
		KCMInvalidateDB(third);
}

void KCMDoClearMarks(IDataBase* db)
{
	// The marks are the evidence for "unchanged", so with them gone any spread hidden by
	// "Hide Unchanged Spreads" is shown again and the toggle goes off (nothing hidden, nothing done).
	KCMResetHideUnchanged(kTrue);

	// Note the documents the marks were actually on before DropAll() sets sDB to nil. The caller's
	// db (whatever was active when the reader acted) may be the Source or an unrelated third
	// document, and the frames must disappear from the marked document at once -- which matters when
	// both are visible, tiled.
	// The same for the Source's always-on frames.
	IDataBase* markedDB = KCMDrawEventHandler::sDB;
	IDataBase* srcDB    = KCMDrawEventHandler::sSrcDB;

	// Stop also forgets the registrations (Added/Removed pages). Registrations can only be made on
	// the Target and Source while armed, so in practice that is those two documents, but everything
	// is cleared to be sure -- which also stops an old registration turning up when a comparison is
	// restarted with a different pair.
	KCMPageMapClearAllDocs();

	// ★THE TICKS ARE **NOT** FORGOTTEN AT STOP (2026-09-04, user decision). A tick is the reader's
	//   own marker rather than a by-product of the comparison: it can be put on any open document,
	//   it is saved and restored on its own, and dropping it here would throw away work nobody
	//   asked to discard. What clears ticks now is the flyout's "Clear Checks in This Document"
	//   (one document), closing a document (the sweep), and shutdown.
	//   ⚠**The registrations just above are a different matter and are still cleared**: they are an
	//   INPUT to the comparison, so an old Added/Removed left standing would creep into the pairing
	//   when a comparison is restarted with a different pair.

	KCMDrawEventHandler::DropAll();
	KCMDrawEventHandler::DropAllOrig();	// and the peek's cached pictures, to release the memory

	KCMInvalidateDocs(markedDB, db, srcDB);	// the Source too, so its always-on frames go at once

	// The screen half of Stop's clean-up is one notification: removing the strips, rebuilding the
	// Pages panel thumbnails, the panel display, the Prev/Next cursor and position.
	// (The shared image cache behind the thumbnails is not reached by InvalidateViews, so a
	//  Purge + ForceRedraw is needed, symmetrically with Start; the UI holds that procedure. With
	//  DropAll done there is nothing to mark, so the isThumb drawing returns early and no frame is
	//  drawn.)
	//
	// @warning **the two documents to clean up MUST travel on the notification.** DropAll has
	//   already run, so sDB and sSrcDB are nil and the UI asking KCMArmedTargetDB() gets no answer
	//   -- it cannot know whose thumbnails to rebuild. Unlike Rebuilt, this one **cannot be asked
	//   about after the fact.**
	// navReset=kTrue: Stop does not carry the walk's cursor over to the next comparison.
	KCMNotifyDocs(kKCMMarksClearedMessage, markedDB, srcDB, kTrue /*navReset*/);
	// (The matching "take it down" notification to the transparency manager was removed from here
	//  too, for the reason given at the re-comparison above.
	//  @warning **it had never actually worked**: both the raising and the lowering sent the same
	//    `kXPC_MayHaveAddedSomeXP`, and **that kind only ever adds** (measured A/B on one document:
	//    `MayHaveAdded` 1 -> 1, `kXPC_RemovedSomeXP` 1 -> 0). The old comment correctly said "call
	//    it symmetrically" and the calls WERE symmetric -- **the meaning was not**. One function
	//    usable for both directions is what took the direction out of the argument list; today
	//    KCMSetItemXPState() takes it as a parameter.)

	// The Story Edits list is forgotten as well. Left until the next comparison, it would list rows
	// pointing into two documents that are no longer being compared -- **and those rows can be
	// clicked and jumped to.**
	// The heading loses its count and goes back to "Story Edits"; the wording is decided by
	// KCMUpdateStorySectionLabel from the armed state, so the model need only say that the list
	// changed.
	KCMStoryList::Clear();
	KCMNotify(kKCMStoryEditsRebuiltMessage);
}

void KCMDoSetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db)
{
	KCMDrawEventHandler::sPrintMarks = printFlag;
	KCMDrawEventHandler::sMarkOpacity25 = opacity25Flag;
	// Bring the always-on (screen) opacity into line with the print setting at once.
	KCMDrawEventHandler::sMarkScreenOpacity = KCMBaseScreenOpacity();

	// The document the marks are actually on (sDB) goes FIRST, so that its appearance updates even
	// when the caller's db -- the active document -- is not it (the Source, or an unrelated third
	// document, being in front). Before a Start (sDB == nil) only db is redrawn, as before.
	// The Source's always-on frames follow the 25%/75% choice, so the Source is redrawn too.
	KCMInvalidateDocs(KCMDrawEventHandler::sDB, db, KCMDrawEventHandler::sSrcDB);

	// (Three notifications to the transparency manager stood here too and were removed, for the
	//  reason given above: the declaration is now made **only for the duration of an export or a
	//  print**, in section 5) of KCMRingAdornment.cpp.
	//  This toggle changes whether marks reach the output, so it does change **the answer** that
	//  declaration gives -- but the only thing that asks for that answer is an output, so there is
	//  nothing to update in advance.)
}

// The current print-mark settings, used to restore the panel's controls when it is re-shown.
bool16 KCMGetPrintMarks()
{
	return KCMDrawEventHandler::sPrintMarks;
}

bool16 KCMGetMarkOpacity25()
{
	return KCMDrawEventHandler::sMarkOpacity25;
}

// Set the mark colour (red / cyan). One flag serves both modes: the Pixel frames and the Story
// wash both read KCMDrawEventHandler::SelectedMarkColor() as they draw.
void KCMDoSetMarkColor(bool16 cyan, IDataBase* db)
{
	if (KCMDrawEventHandler::sMarkColorCyan == cyan)
		return;						// the same colour chosen again: nothing to rebuild, nothing to redraw

	KCMDrawEventHandler::sMarkColorCyan = cyan;

	// @warning **the ring images are cached and are only rebuilt when the RADIUS changes** (the call
	//   to BuildRing is guarded by `R != e->lastRadius`), so changing the colour alone would leave
	//   **the old colour standing**. Marking them "not drawn" here makes the next draw rebuild them.
	//   The Story wash needs no such treatment -- it reads the colour on every Draw, so a redraw is
	//   enough. **The same setting needs different clean-up on the side that caches and the side
	//   that does not.**
	{
		KCMMarkStateLock lock(KCMMarkStateMutex());
		for (std::map<UID, KCMOverlayEntry*>::iterator it = KCMDrawEventHandler::sEntries.begin();
		     it != KCMDrawEventHandler::sEntries.end(); ++it)
			if (it->second != nil)
				it->second->lastRadius = -1;	// -1 = not drawn (KCMOverlayEntry's own default)
	}

	// The redraw covers the same three documents as KCMDoSetPrintMarks (marked, active, Source).
	KCMInvalidateDocs(KCMDrawEventHandler::sDB, db, KCMDrawEventHandler::sSrcDB);
}

bool16 KCMGetMarkColorCyan()
{
	return KCMDrawEventHandler::sMarkColorCyan;
}

//----------------------------------------------------------------------------------------
// The comparison mode
//----------------------------------------------------------------------------------------
// **A session-wide setting, not a per-document one**, which is why it takes no db. The print-marks
// flag lives on a KCMDrawEventHandler static because it is a setting OF THE DRAWING; this one is a
// setting OF THE COMPARISON, so it lives in the translation unit that owns the comparison.
//
// @warning **a background thread reads it too** (the Story mode draws no frames, so the drawing
//   consults it). It is written only by a menu action, i.e. on the main thread, and read as a
//   single enum: of the "background threads see a different db but share the statics" cases
//   ([[model-plugin-thread-safety]]), this is one where sharing is **correct** -- every thread must
//   see the same mode.
static KCMCompareMode sCompareMode = kKCMModePixel;

KCMCompareMode KCMGetCompareMode()
{
	return sCompareMode;
}

void KCMSetCompareMode(KCMCompareMode mode)
{
	sCompareMode = mode;
}
