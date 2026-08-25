//========================================================================================
//
//  KCMPageMap.cpp
//
//  Page pairing (added / removed pages). Select pages in the Pages panel, then the context-menu
//  toggle "Register as Added/Removed Pages" registers or unregisters them as pages with no
//  counterpart. On the Target they read as "added" and on the Source as "removed", but the two
//  mean the same thing -- excluded from the pairing -- so a single UID set per document holds them.
//
//  - Reading the selection: Utils<ILayoutUIUtils>()->GetSelectedPages (the official API,
//    ILayoutUIUtils.h:183). bPagesOnly=kTrue expands a selected spread into its page UIDs; the
//    caller decides bIncludeMasters (Register passes kFalse to leave masters out -- the reason is
//    with includeMasters in KCMPageMap.h).
//    The three uses in the SDK split by purpose: the product's PageTransitionsPanelObserver.cpp:672
//    passes bPagesOnly=kFalse because it is after spread-level transitions and wants the mixture,
//    while codesnippets/SnpModifyLayoutGrid.cpp:959 and SnpInspectLayoutGrid.cpp:690 take the
//    default (kTrue). KCM pairs page by page, so kTrue is the right one here.
//    @warning do not go back to reading IUIDListControlData off kPagesPanelWidgetBoss directly:
//    that only sees a page-icon selection and comes back empty when the user selected the spread,
//    which made the menu item vanish. The panel also has two sub-panels (document pages and
//    masters), so there is no single place where "the selection" lives.
//  - The menu: **ui/KCMUI.fr** adds the toggle to the Pages panel's page context menu, whose
//    untranslated internal name is "RtMenuPagesPanel" (measured) and therefore the same in every
//    locale. The menu lives in the UI plug-in; the model's KCM.fr has no RtMenuPagesPanel in it.
//    The tick, the enabling and the dynamic label all come from kCustomEnabling ->
//    KCMPageMapGetToggleState.
//  - Lifetime: session only. Nothing is written to the document file, so nothing is dirtied.
//    When documents close, KCMHandleDocsClosed calls KCMPageMapSweepClosedDocs, which drops the
//    state without dereferencing anything.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "ILayoutUIUtils.h"		// GetSelectedPages (the official way to read the Pages panel selection)
#include "IMasterSpreadList.h"	// GetMasterSpreadCount / GetNthMasterSpreadUID / FindMasterByName
#include "IMasterSpread.h"		// GetPrefix / GetBasename (pairing master spreads by name)
#include "ISpread.h"			// GetNumPages / GetNthPageUID (the pages inside a master spread)
#include "Utils.h"
#include "UIDList.h"
#include "PMString.h"

#include <set>
#include <vector>

#include "KCMCore.h"			// KCMCollectPageUIDs / KCMCollectMasterPageUIDs / KCMArmedTargetDB / KCMArmedSourceDB
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it
#include "KCMComparisonRun.h"	// KCMToggleStartStop
#include "KCMPageMap.h"
#include "KCMDocUidSet.h"		// the shared "document -> page UID set" container (the tick uses it too)
#include "KCMID.h"				// kKCMPageFlagsChangedMessage (the notification's ID)
// This file deliberately does not include the UI's KCMThumbnailRefresh.h: rebuilding a thumbnail
// is the job of whoever receives the notification, which is the UI.

// The registered "no counterpart" pages: document database -> set of page UIDs, session only.
// An entry whose set became empty disappears at once (KCMDocUidSet's rule).
static KCMDocUidSet sRegistered;

// Helper: a linear contains over vector<UID>. **Its only job is to keep outPages free of
// duplicates**, and outPages is at most as long as the selection, so linear is fine here.
// @warning it is not what the document's whole page list is tested with -- that goes through a
// set (see KCMPageMapReadSelection below). Scanning the page list linearly instead means
// 1,000 pages x 100 selected pages = 100,000 comparisons, which is what this used to do.
static bool16 KCMVecContains(const std::vector<UID>& v, UID u)
{
	for (size_t k = 0; k < v.size(); ++k)
	{
		if (v[k] == u)
			return kTrue;
	}
	return kFalse;
}

//========================================================================================
// KCMPageMapReadSelection (declared in KCMPageMap.h) -- the shared reader of the Pages panel's
// selection. outDB is the document the selection belongs to (the active one) and outPages the
// selected page UIDs that really exist in that document's page list. kTrue when at least one
// page is valid.
// The selection comes from Utils<ILayoutUIUtils>()->GetSelectedPages:
//   - bIncludeMasters = the includeMasters parameter ... are masters read (see below)
//   - bPagesOnly = kTrue ...... a whole-spread selection (which is what the panel makes of a
//     selected pair of pages) is expanded into its page UIDs
//   - bCurrentPageOnly = kTrue is the documented fallback for the panel not being visible, which
//     hardly applies here since these menu items can only be opened from the panel itself
// The returned UIDs are checked against the document's flat page list, and duplicates dropped.
// **All three context-menu features share this**: Register (this file), Check (KCMPageCheck.cpp)
// and Refresh (KCMPeek.cpp).
//
// includeMasters (default kFalse = ordinary pages only). What it means, and how to decide which
//   to pass, is with the declaration in KCMPageMap.h -- the three features disagree, which is
//   why it is a parameter.
//   @warning **the exclusion has two stages**: besides GetSelectedPages' bIncludeMasters, the
//   list it is checked against, KCMCollectPageUIDs, holds no master either (IPageList.h:81 says
//   in as many words that the page count "does not include master pages"). Changing only one of
//   the two still drops every master, because it will not be in flatSet, so **both have to move
//   together with includeMasters**. The master pages are appended by KCMCollectMasterPageUIDs,
//   whose contract is that it does not clear its out parameter.
//========================================================================================
bool16 KCMPageMapReadSelection(IDataBase*& outDB, std::vector<UID>& outPages, bool16 includeMasters)
{
	outDB = nil;
	outPages.clear();

	// What the Pages panel shows is the active document, and the contract is that its database is
	// handed over inside the UIDList (ILayoutUIUtils.h:178, "UIDList must be set up with proper
	// database").
	// The database comes from KCMActiveDocDB() (= IActiveContext::GetContextDocument), which is
	//   also how the official GetSelectedPages example reaches it
	//   (codesnippets/SnpModifyLayoutGrid.cpp; the product's
	//   dynamicdocumentsui/PageTransitionsPanelObserver.cpp goes through ILayoutControlData instead).
	//   @warning do not go back to Utils<ILayoutUIUtils>()->GetFrontDocument(): its contract is
	//   the document of the frontmost *layout* presentation (ILayoutUIUtils.h, GetFrontDocument),
	//   which can differ from the active document while a story editor window is in front -- the
	//   selected UIDs would then be read against a different document than the panel is showing.
	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
		return kFalse;

	// **WHY A UI-SIDE Utils IS STILL CALLED FROM THE MODEL PLUG-IN**
	//
	//  `ILayoutUIUtils` comes, as its name says, **from the UI plug-in**, and the guide
	//  (vol1-07) states that a UI plug-in's boss cannot be instantiated on a background thread --
	//  it comes back nil. It is called here anyway because **this function was measured never to
	//  be reached from a background thread**:
	//
	//    - Every caller enters through the Facade (IKCMPageFlagsFacade) from **a Pages panel
	//      context-menu action in the UI**: the Register toggle and its state, the Check toggle
	//      and its state (KCMPageCheck.cpp), and Refresh (KCMPeek.cpp).
	//    - What runs on a background thread is the drawing pass
	//      (KCMDrawEventHandler::DrawSpreadMarks), and the only page-map entry points it calls
	//      are the two readers **KCMPageMapIsRegistered and KCMPageMapHasAnyRegistered**
	//      (KCMBuildPairing is reached only through RebuildOverflowCache, which refuses to run
	//      off the main thread). None of them arrives here.
	//
	//  @warning **that is "does not reach it today", not "cannot reach it".** Any of the
	//    following makes this the first thing to revisit:
	//      (1) reading the selection from the drawing pass  (2) calling this from a new route
	//      (3) supporting InDesign Server (the .fr says `{ kInDesignProduct }` only, so the
	//          plug-in is not even loaded there).
	//  The shape to move to is already settled: **the UI observes, the model decides** -- the UI
	//    reads the selection and the model receives a UIDList as a parameter.
	UIDList sel(db);
	// @warning **selecting the `[None]` row (the "no master" entry) leaves the selection empty and
	//   makes `GetSelectedPages` fall back on "the current page"** (measured). KCM then reads that
	//   as "this page was selected" and **ticks a page the user never chose** -- running Check
	//   with `[None]` selected put a tick on page 1 on the real thing.
	//   **Passing kFalse for the third parameter does not fix it, it widens the hole** (tried and
	//     reverted): kFalse means "use every page of the current spread", which made Register
	//     misfire the same way.
	//   The header's contract is "kTrue = use the current page only when the panel is not
	//     visible", but the implementation also treats it as **a fallback for an empty
	//     selection**, which the contract does not say.
	//   Telling `[None]` apart therefore needs some entry point other than this API (unsolved).
	Utils<ILayoutUIUtils>()->GetSelectedPages(sel, includeMasters, kTrue /*currentPageOnly*/, kTrue /*pagesOnly*/);

	// What is being searched is the document's whole page list, so it goes into a set (see the
	// comment on KCMVecContains above).
	std::vector<UID> flat;
	KCMCollectPageUIDs(db, flat);
	if (includeMasters)
		KCMCollectMasterPageUIDs(db, flat);	// masters are appended; the out list is not cleared
	const std::set<UID> flatSet(flat.begin(), flat.end());
	const int32 n = sel.Length();
	for (int32 i = 0; i < n; ++i)
	{
		const UID u = sel[i];
		if (flatSet.count(u) > 0 && !KCMVecContains(outPages, u))
			outPages.push_back(u);
	}
	if (outPages.empty())
		return kFalse;

	outDB = db;
	return kTrue;
}

// What to call the pages of this document in the status line: "added" on the Target, "removed"
// on the Source, and the generic "added/removed" when no comparison is running or the document
// is an unrelated one.
static const char* KCMPageMapRoleWord(IDataBase* db)
{
	if (db != nil && db == KCMArmedTargetDB())
		return "added";
	if (db != nil && db == KCMArmedSourceDB())
		return "removed";
	return "added/removed";
}

//========================================================================================
// KCMPageMapToggleSelectedPages (declared in KCMPageMap.h)
//   Runs the context-menu toggle. Any unregistered page among the selection registers them all;
//   all registered unregisters them all -- the standard toggle behaviour that goes with the tick.
//   The outcome, and this document's registration total, go to the panel's status line (which
//   survives in the session even while the panel is hidden, and shows on the next reveal).
//========================================================================================
void KCMPageMapToggleSelectedPages()
{
	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KCMPageMapReadSelection(db, pages))
		return;		// kCustomEnabling should already have greyed the menu out; belt and braces

	// Registering is only possible while a comparison is running (armed) and the selected
	//   document is the Target or the Source. KCMPageMapGetToggleState's answer will have greyed
	//   the menu out already, but the rule is enforced here too.
	if (!KCMIsArmed() || (db != KCMArmedTargetDB() && db != KCMArmedSourceDB()))
		return;

	const bool16 anyUnregistered = sRegistered.AnyNotIn(db, pages);

	// The panel's status area is small in both width and lines, and it is kDontEllipsize, so
	// nothing is shortened for you: keep the message to one short line.
	// @warning **do not copy its size into this comment.** The one source of truth is the Frame
	//   of kKCMStatusTextWidgetID in ui/KCMUI.fr -- measure it there. The size was written out in
	//   several files at once and went stale in all of them, each holding a different old number
	//   ([[one-question-one-place]]).
	// @warning the area belongs to the UI, and the model can only reach it through a
	//   notification. **The length is still decided here**: the sender is the only one who can
	//   shorten it, since the receiver has nothing to do but cut, and a number cut in half reads
	//   as a different number ([[ellipsis-in-status-line-breaks-numbers]]).
	PMString msg;
	msg.SetTranslatable(kFalse);
	if (anyUnregistered)
	{
		for (size_t i = 0; i < pages.size(); ++i)
			sRegistered.Insert(db, pages[i]);
		msg.Append("+");
		msg.AppendNumber((int32)pages.size());
		msg.Append(" ");
		msg.Append(KCMPageMapRoleWord(db));
	}
	else
	{
		for (size_t i = 0; i < pages.size(); ++i)
			sRegistered.Erase(db, pages[i]);
		msg.Append("-");
		msg.AppendNumber((int32)pages.size());
		msg.Append(" ");
		msg.Append(KCMPageMapRoleWord(db));
	}

	// The total is counted after the change (Erase has already dropped the document's entry when
	// unregistering emptied it, so 0 comes back).
	msg.Append(", total ");
	msg.AppendNumber(sRegistered.CountIn(db));

	// While a comparison is already running, re-compare straight away so the changed pairing is
	// reflected at once -- without this, changing a registration after a comparison showed
	// nothing until the next one. Nothing happens when no comparison is running; the next Start
	// picks the registrations up by itself.
	// The report string is deliberately dropped in favour of a short suffix: the status area is
	// small and appending the report overflows it.
	bool16 recompared = kFalse;
	if (KCMIsArmed() && KCMArmedTargetDB() != nil && KCMArmedSourceDB() != nil)
	{
		// An incremental re-comparison (allowIncremental=kTrue). Registering and unregistering
		// changes no page's content, only the pairing, so pages whose partner is unchanged reuse
		// the previous result and are not rasterised again; only pages that gained, lost or
		// changed a partner are recomputed. The larger the document, the more this saves.
		// **Always look at the return value.** A registration change shifts the partner of every
		//   page after it, so even an incremental run can rasterise enough pages for the progress
		//   bar to offer Cancel. On cancellation KCMDoMarkChangesDoc has already thrown every
		//   mark away (kFailure); carrying on regardless would leave a running comparison with
		//   no frames at all while reporting "(recompared)", which is a lie.
		//   The answer is the same as for a cancelled Load Check & Register (KCMPageCheck.cpp):
		//   go all the way back to Stop. The registration change made here is thrown away with
		//   it, which is exactly what Stop is specified to do.
		PMString report;
		if (KCMDoMarkChangesDoc(KCMArmedTargetDB(), KCMArmedSourceDB(), report, kTrue /*allowIncremental*/) != kSuccess)
		{
			KCMToggleStartStop();		// armed, so this takes the Stop branch: marks, registrations
										// and ticks dropped, disarmed, panel updated
			PMString cmsg("Recompare cancelled");
			cmsg.SetTranslatable(kFalse);
			KCMNotifyStatus(cmsg, kTrue /*forceRedrawNow*/);
			return;
		}
		msg.Append(" (recompared)");
		recompared = kTrue;
	}

	// Tell the UI which pages were toggled, so their thumbnails are purged per UID. Only two
	// cases need it:
	//   - no re-comparison ran (nothing is armed, say) ... there is no other refresh route
	//   - unregistering ... an unregistered page leaves sRegistered, and it is in neither
	//     sEntries nor the overflow sets (while it was registered the pairing left it out
	//     altogether), so **it is in none of the sets the current state can produce**. This is
	//     the only place it can be caught.
	//   Registering with a re-comparison behind it is skipped on purpose: the toggled pages are
	//   in sRegistered by then, and the re-comparison's own KCMCollectChangedPageUIDs (which
	//   includes the registered pages) has already purged and redrawn them. Sending it again
	//   only rasterises the same pages twice and makes the panel flicker.
	// @warning today the unregister route IS that double: a full re-comparison sends **no page
	//   set at all**, so the UI purges **every page** of the document (the branch in
	//   ui/KCMModelChangeObserver.cpp taken when fPagesA is nil). The picture is right; the cost
	//   is a second rasterisation and a flicker.
	//   **The notification stays anyway**: KCMDoMarkChangesDoc still has "take the snapshot again
	//   and send it with the notification" outstanding, and the moment that lands, per-UID purging
	//   comes back and **this is the only route that can catch an unregistered page**. (Delete
	//   this block only together with that change, never on its own.)
	// The page set travels on the notification itself -- ISubject::Change's third parameter,
	//   changedBy, carries it (see KCMModelNotify.h). Whether the set can miss a page whose
	//   picture changed is answered by the two cases above.
	if (!recompared || !anyUnregistered)
	{
		const std::set<UID> touched(pages.begin(), pages.end());
		KCMNotifyPages(kKCMPageFlagsChangedMessage, db, touched);
	}

	KCMNotifyStatus(msg);
}

//========================================================================================
// KCMPageMapGetToggleState (declared in KCMPageMap.h)
//   **Only answers** how the kCustomEnabling toggle should look right now.
//   - no document page in the selection (nothing selected, or masters only) -> disabled
//   - every selected page registered -> All; only some of them -> Some (the mixed tick)
//   - fRole says whether the active document is the Target or the Source, which is what the
//     caller picks the label from
//
//   **This no longer takes an IActionStateList.** Writing to the menu (SetNthActionState /
//   SetNthActionName) and the label strings belong to ui/KCMActionComponent.cpp, because the
//   menu is the UI's job (the reasoning is with KCMPageToggleState in KCMPageMap.h). What is
//   left here is the counting.
//========================================================================================
KCMPageToggleState KCMPageMapGetToggleState()
{
	KCMPageToggleState st;	// disabled by default

	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KCMPageMapReadSelection(db, pages))
		return st;

	// Registering is only possible while a comparison is running (armed) and the selected
	//   document is the Target or the Source; anything else is greyed out. That covers a Pages
	//   panel showing no comparison at all, and one showing some third document.
	if (!KCMIsArmed() || (db != KCMArmedTargetDB() && db != KCMArmedSourceDB()))
		return st;

	const int32 regCount = sRegistered.CountIn(db, pages);

	st.fEnabled = kTrue;
	if (regCount == (int32)pages.size())
		st.fTick = kKCMPageTickAll;		// all registered = a tick
	else if (regCount > 0)
		st.fTick = kKCMPageTickSome;		// only some = the mixed tick

	// What the label is picked from. Past the guard above, db can only be the Target or the
	//   Source, so there is no third, generic ("Added/Removed") label here -- that branch was
	//   unreachable. kKCMPageRoleNone survives only as the default while fEnabled is kFalse.
	st.fRole = (db == KCMArmedTargetDB()) ? kKCMPageRoleTarget : kKCMPageRoleSource;
	return st;
}

//========================================================================================
// KCMPageMapSweepClosedDocs (declared in KCMPageMap.h)
//   The liveness sweep run right after documents close (from KCMHandleDocsClosed). Drops the
//   registrations of closed documents, state only. **A closed database is never dereferenced**
//   -- pointer comparison against FindDocByDataBase and nothing more, the same way as KCM's
//   other close-up work. Dropping them promptly also leaves the least room to confuse a closed
//   document with a new one that reused its address.
//========================================================================================
void KCMPageMapSweepClosedDocs()
{
	sRegistered.SweepClosedDocs();	// the container owns both the shutdown nil guards and the
									// no-dereference rule (KCMDocUidSet.cpp)
}

// (KCMPageMapClearAll was removed: it had no caller, and this file declared one that did not
//  exist -- "called from Stop". What Stop calls is KCMPageMapClearAllDocs below.)

//========================================================================================
// KCMPageMapClearAllDocs (declared in KCMPageMap.h)
//   Forget every document's registrations. Stop (KCMDoClearMarks) calls it so that clearing a
//   comparison leaves no Added/Removed registrations behind either. Only empties the map; no
//   pointer is touched, so nothing can be dereferenced.
//========================================================================================
void KCMPageMapClearAllDocs()
{
	sRegistered.ClearAllDocs();
}

//========================================================================================
// KCMPageMapIsRegistered (declared in KCMPageMap.h)
//========================================================================================
bool16 KCMPageMapIsRegistered(IDataBase* db, UID pageUID)
{
	return sRegistered.Contains(db, pageUID);
}

//========================================================================================
// KCMPageMapHasAnyRegistered (declared in KCMPageMap.h)
//========================================================================================
bool16 KCMPageMapHasAnyRegistered(IDataBase* db)
{
	return sRegistered.HasAny(db);
}

//========================================================================================
// KCMPageMapCollectRegistered (declared in KCMPageMap.h)
//   Add every registered (Added/Removed = green "/") page UID of db into out; out is not
//   cleared, so this merges into an existing changed/overflow set. The registered pages are held
//   apart from sEntries and the overflow sets, so leaving them out of the set the thumbnails are
//   purged for means the green "/" does not appear until something else redraws it.
//========================================================================================
void KCMPageMapCollectRegistered(IDataBase* db, std::set<UID>& out)
{
	sRegistered.CollectInto(db, out);	// out is not cleared (the container's contract)
}

//========================================================================================
// KCMPageMapReplaceRegistered (declared in KCMPageMap.h)
//   Replace db's registrations wholesale with pages (the setter "Load Check & Register" uses).
//   It only rewrites sRegistered: no re-comparison and no thumbnail refresh, because the caller
//   sets both documents first and then re-compares once. An empty pages drops the entry.
//========================================================================================
void KCMPageMapReplaceRegistered(IDataBase* db, const std::vector<UID>& pages)
{
	sRegistered.Replace(db, pages);		// empty drops the entry itself (the container's contract)
}

//========================================================================================
// KCMBuildPairing (declared in KCMPageMap.h)
//   Take each document's flat page list (KCMCollectPageUIDs), drop the registered pages -- the
//   ones with no counterpart -- and pair what is left in order. Registered pages are skipped and
//   everything after them closes up, which is the whole point: without it the two documents are
//   simply zipped together and every page after an insertion is compared against the wrong one.
//   Pages that fall off the end because the documents hold different numbers of pages (not
//   registered, but with no partner left) go into outOverflowTargetPages /
//   outOverflowSourcePages when those are supplied.
//========================================================================================
void KCMBuildPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages,
	std::vector<UID>* outOverflowTargetPages, std::vector<UID>* outOverflowSourcePages)
{
	outTargetPages.clear();
	outSourcePages.clear();
	if (outOverflowTargetPages) outOverflowTargetPages->clear();
	if (outOverflowSourcePages) outOverflowSourcePages->clear();
	if (targetDB == nil || sourceDB == nil)
		return;

	std::vector<UID> tFlat, sFlat;
	KCMCollectPageUIDs(targetDB, tFlat);
	KCMCollectPageUIDs(sourceDB, sFlat);

	std::vector<UID> tFiltered, sFiltered;
	tFiltered.reserve(tFlat.size());
	sFiltered.reserve(sFlat.size());
	for (size_t i = 0; i < tFlat.size(); ++i)
		if (!KCMPageMapIsRegistered(targetDB, tFlat[i]))
			tFiltered.push_back(tFlat[i]);
	for (size_t i = 0; i < sFlat.size(); ++i)
		if (!KCMPageMapIsRegistered(sourceDB, sFlat[i]))
			sFiltered.push_back(sFlat[i]);

	const size_t n = (tFiltered.size() < sFiltered.size()) ? tFiltered.size() : sFiltered.size();
	outTargetPages.assign(tFiltered.begin(), tFiltered.begin() + n);
	outSourcePages.assign(sFiltered.begin(), sFiltered.begin() + n);
	if (outOverflowTargetPages && tFiltered.size() > n)
		outOverflowTargetPages->assign(tFiltered.begin() + n, tFiltered.end());
	if (outOverflowSourcePages && sFiltered.size() > n)
		outOverflowSourcePages->assign(sFiltered.begin() + n, sFiltered.end());
}

//========================================================================================
// KCMBuildMasterPairing (declared in KCMPageMap.h)
//   Pairs master spreads by name and lays out the pages of the pairs that matched, in order.
//   **The rule differs from KCMBuildPairing above** (position there, name here), which is why
//   this is a separate function; the reasoning is with the declaration in the header.
//   Registered pages are deliberately not excluded here: registrations are made from the Pages
//   panel selection, and Register is the one feature that calls the reader
//   (KCMPageMapReadSelection) with includeMasters=kFalse, so no master page can be in the
//   registered set to begin with.
//   @warning **that assumption is what makes this function correct.** It never looks at the
//   registrations, so the day Register starts accepting masters, registering one would silently
//   fail to exclude it from the comparison. (When Check and Refresh were opened up to masters,
//   Register was left at kFalse for exactly this reason.)
//
//   The Source side is looked up with the official IMasterSpreadList::FindMasterByName(prefix,
//   basename). Walking every master and comparing names by hand would work too, but an official
//   "find a master by name" exists, so that is the road.
//   @warning **the SDK has no other caller of this API** (nothing in the samples, nothing in
//   source/open), and the header does not say what it returns when nothing matches. kInvalidUID
//   is assumed here, and whatever UID comes back is opened as an ISpread and nil-checked anyway,
//   so a bad value cannot crash this. What a master with no counterpart really does is worth
//   confirming on the real thing.
//========================================================================================
void KCMBuildMasterPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages)
{
	outTargetPages.clear();
	outSourcePages.clear();
	if (targetDB == nil || sourceDB == nil)
		return;

	InterfacePtr<IMasterSpreadList> tList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	InterfacePtr<IMasterSpreadList> sList(sourceDB, sourceDB->GetRootUID(), UseDefaultIID());
	if (tList == nil || sList == nil)
		return;

	// Walk the Target's master spreads in order and pair each with the Source's master of the
	// same name, when there is one.
	const int32 tn = tList->GetMasterSpreadCount();
	for (int32 i = 0; i < tn; ++i)
	{
		const UID tu = tList->GetNthMasterSpreadUID(i);
		InterfacePtr<IMasterSpread> tms(targetDB, tu, UseDefaultIID());
		if (tms == nil)
			continue;

		// The name is asked for in two parts, the prefix ("A") and the basename ("Master" or its
		//   localised equivalent). There is no need to split what GetName() returns ("A-Master"):
		//   the official lookup takes exactly these two.
		PMString prefix, basename;
		tms->GetPrefix(&prefix);
		tms->GetBasename(&basename);

		const UID su = sList->FindMasterByName(prefix, basename);
		if (su == kInvalidUID)
			continue;			// no counterpart: this master is not compared

		InterfacePtr<ISpread> tsp(targetDB, tu, UseDefaultIID());
		InterfacePtr<ISpread> ssp(sourceDB, su, UseDefaultIID());
		if (tsp == nil || ssp == nil)
			continue;
		const int32 tp = tsp->GetNumPages();
		const int32 sp = ssp->GetNumPages();
		const int32 np = (tp < sp) ? tp : sp;	// a pair with different page counts is truncated
		for (int32 p = 0; p < np; ++p)
		{
			outTargetPages.push_back(tsp->GetNthPageUID(p));
			outSourcePages.push_back(ssp->GetNthPageUID(p));
		}
	}
}

// (KCMPageMapHasOverflow was removed: nothing had called it since the drawing side moved to the
//  sOverflowT / sOverflowS caches, and its body walked every page -- worth removing before
//  someone called it from a hot path by mistake.)

//========================================================================================
// KCMMapTargetToSource / KCMMapSourceToTarget (declared in KCMPageMap.h)
//   Translate one page to its counterpart. The pairing is built by KCMBuildPairing and then
//   searched linearly. Rebuilding it every time is cheap enough: a document holds a few hundred
//   pages at most, and the callers only ask at the granularity of one spread, a handful of pages.
//
// **Master spread pages resolve too** (they used to not, which showed up as the CMYK readout
//   going blank on a master page). The master pairing (KCMBuildMasterPairing, by name) is
//   consulted only when the ordinary pairing did not find the page.
//   Mixing the two tables is safe because **page UIDs are unique within a document**, so the same
//     UID cannot appear in both. The comparison's own table (KCMCore.cpp) and the partial
//     re-comparison (KCMPeek.cpp) already do the same thing, holding ordinary pages and masters
//     in one std::map.
//   **The ordinary table is searched first**, so the common route never builds the master one.
//========================================================================================
bool16 KCMMapTargetToSource(IDataBase* targetDB, IDataBase* sourceDB,
	UID targetPageUID, UID& outSourcePageUID)
{
	outSourcePageUID = kInvalidUID;
	std::vector<UID> tPages, sPages;
	KCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	for (size_t i = 0; i < tPages.size(); ++i)
	{
		if (tPages[i] == targetPageUID)
		{
			outSourcePageUID = sPages[i];
			return kTrue;
		}
	}
	// Master spread pages (paired by name), built only because the ordinary table missed.
	std::vector<UID> mT, mS;
	KCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
	for (size_t k = 0; k < mT.size(); ++k)
	{
		if (mT[k] == targetPageUID)
		{
			outSourcePageUID = mS[k];
			return kTrue;
		}
	}
	return kFalse;
}

bool16 KCMMapSourceToTarget(IDataBase* targetDB, IDataBase* sourceDB,
	UID sourcePageUID, UID& outTargetPageUID)
{
	outTargetPageUID = kInvalidUID;
	std::vector<UID> tPages, sPages;
	KCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	for (size_t i = 0; i < sPages.size(); ++i)
	{
		if (sPages[i] == sourcePageUID)
		{
			outTargetPageUID = tPages[i];
			return kTrue;
		}
	}
	// Master spread pages (the mirror of the above).
	std::vector<UID> mT, mS;
	KCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
	for (size_t k = 0; k < mS.size(); ++k)
	{
		if (mS[k] == sourcePageUID)
		{
			outTargetPageUID = mT[k];
			return kTrue;
		}
	}
	return kFalse;
}

// End of KCMPageMap.cpp
