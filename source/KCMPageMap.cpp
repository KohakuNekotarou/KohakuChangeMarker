//========================================================================================
//
//  KCMPageMap.cpp
//
//  Page pairing (added / removed pages). Select pages in the Pages panel, then the context-menu
//  toggle "Register as Added/Removed Pages" registers or unregisters them as pages with no
//  counterpart. On the Target they read as "added" and on the Source as "removed", but the two
//  mean the same thing -- excluded from the pairing -- so a single UID set per document holds them.
//
//  - Reading the selection: the UI half reads it (KCMReadSelectedPages,
//    ui/KCMThumbnailRefresh.h) with Utils<ILayoutUIUtils>()->GetSelectedPages (the official API,
//    ILayoutUIUtils.h:183) and hands the list to the facade; this file decides on it
//    (KCMPageMapPagesOf). bPagesOnly=kTrue expands a selected spread into its page UIDs; the
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
//      ⚠ **that is about READING THE SELECTION.** The very same interface IS read, on the UI side,
//      to answer a different question -- "does the selection hold no real page at all" -- and there
//      an empty answer is not ambiguous, because [None] is held in it as an explicit kInvalidUID
//      (KCMPagesPanelSelectionHasNoRealPage, ui/KCMThumbnailRefresh.h; see the [None] warning
//      further down this file). **One interface, two questions, two answers.**
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
#include "ILayoutUtils.h"		// IsAMaster - a selected page that is a master's (KCMPageMapPagesOf)
#include "IMasterSpreadList.h"	// GetMasterSpreadCount / GetNthMasterSpreadUID / FindMasterByName
#include "IMasterSpread.h"		// GetPrefix / GetBasename (pairing master spreads by name)
#include "IPageList.h"			// GetPageIndex - a selected page that is in the page list (KCMPageMapPagesOf)
#include "ISpread.h"			// GetNumPages / GetNthPageUID (the pages inside a master spread)
#include "SpreadID.h"			// kPageBoss - a selected UID that is a page at all
#include "UIDList.h"
#include "PMString.h"
#include "Utils.h"

#include <algorithm>			// std::find - a selected page once in outPages
#include <set>
#include <vector>

#include "KCMCore.h"			// KCMCollectPageUIDs / KCMDoMarkChangesDoc / KCMIsComparedDoc / the armed pair
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it
#include "KCMComparisonRun.h"	// KCMStopComparison
#include "KCMPageMap.h"
#include "KCMPagePairRule.h"	// KCMPairPagesByIdentity / KCMPartnerOf - THE pairing rule and the lookup in it (pure; proved in work/kcm-pagepair-test)
#include "KCMPageExport.h"		// KCMDocumentIds - the XMP ids KCMPageMapMadeSeparately compares
#include "KCMDocUidSet.h"		// the shared "document -> page UID set" container (the tick uses it too)
#include "KCMID.h"				// kKCMPageFlagsChangedMessage (the notification's ID)
// This file deliberately does not include the UI's KCMThumbnailRefresh.h: rebuilding a thumbnail
// is the job of whoever receives the notification, which is the UI.

// The registered "no counterpart" pages: document database -> set of page UIDs, session only.
// An entry whose set became empty disappears at once (KCMDocUidSet's rule).
static KCMDocUidSet sRegistered;

//========================================================================================
// KCMPageMapPagesOf (declared in KCMPageMap.h) -- the pages of a Pages panel selection the UI
// read. outDB is the selection's database - the active
// document's, which is the one the Pages panel shows and the one the UI reads it for
// (KCMReadSelectedPages, ui/KCMThumbnailRefresh.h) - and outPages the selected page UIDs that
// really exist in that document's page list, duplicates dropped. kTrue when at least one page is
// valid.
// **All the page-directed features share this**: Register (this file), Check (KCMPageCheck.cpp),
// Refresh (KCMPeek.cpp) and Export Pages to Word (KCMFacades.cpp).
//
// WHY THE UI READS AND THIS ONLY DECIDES. Utils<ILayoutUIUtils> is a UI plug-in's Utils, which the
//   guide (vol1-07) says comes back nil on a background thread, and this is the model plug-in -
//   "the UI observes, the model decides": the UI reads the selection and the model receives a
//   UIDList as a parameter.
//   ⚠THE [None] ROW'S GUARD IS THE READER'S: selecting only [None] (the "no master" row)
//   makes GetSelectedPages fall back on the current page (measured - a tick landed on page 1 that
//   nobody chose), and this list cannot tell that page from a chosen one. The UI's reader refuses
//   such a selection itself (KCMPagesPanelSelectionHasNoRealPage, inside KCMReadSelectedPages), so
//   no route reaches here with it.
//
// includeMasters (KCMPageMap.h says what it means and which feature passes what).
//   @warning **the exclusion has two stages**: besides GetSelectedPages' bIncludeMasters (the UI's
//   call), the question asked of each page here lets a master through only when includeMasters
//   says so. Changing only one of the two still drops every master, so **both have to move
//   together with includeMasters**.
//
// EACH SELECTED PAGE IS ASKED, NOT LOOKED UP IN A LIST: flattening the document's whole page list
//   (and every master spread's pages) into a set for each call would walk a 1,000-page document to
//   check a selection of one. Is it a page at all (its class - a deleted UID answers kInvalidClass,
//   and a master SPREAD answers IsAMaster too, so the class goes first, the test KCMFramePageUID
//   uses), and then **in the page list** (IPageList::GetPageIndex >= 0 - a master page is not in
//   it, the reading KCMPageExport.cpp and KCMChangeNav.cpp also rest on) or **a master**
//   (ILayoutUtils::IsAMaster - "a page in a master spread"). The page list includes the pages of
//   hidden spreads (measured - KCMCollectPageUIDs, KCMCore.h), and GetPageIndex's default counts
//   them too.
//========================================================================================
bool16 KCMPageMapPagesOf(const UIDList& selection, bool16 includeMasters, IDataBase*& outDB, std::vector<UID>& outPages)
{
	outDB = nil;
	outPages.clear();

	IDataBase* const db = selection.GetDataBase();
	if (db == nil)
		return kFalse;

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	Utils<ILayoutUtils> layout;		// guarded before use: QueryUtilInterface has no nil check of its own
	// outPages is at most as long as the selection, so keeping it free of duplicates by a linear
	// search is fine.
	const int32 n = selection.Length();
	for (int32 i = 0; i < n; ++i)
	{
		const UID u = selection[i];
		if (db->GetClass(u) != kPageBoss)
			continue;
		const bool16 ordinary = (pageList != nil && pageList->GetPageIndex(u) >= 0) ? kTrue : kFalse;
		const bool16 master = (!ordinary && includeMasters && layout && layout->IsAMaster(u, db)) ? kTrue : kFalse;
		if ((ordinary || master) && std::find(outPages.begin(), outPages.end(), u) == outPages.end())
			outPages.push_back(u);
	}
	if (outPages.empty())
		return kFalse;

	outDB = db;
	return kTrue;
}

// Which side of the running comparison db is. Asked only past KCMIsComparedDoc, where db can only be
// the Target or the Source, so there is no third answer. The menu's label (KCMPageMapGetToggleState)
// and the status line's word (below) are both picked from this one answer.
static KCMPageRole KCMPageMapRoleOf(IDataBase* db)
{
	return (db == KCMArmedTargetDB()) ? kKCMPageRoleTarget : kKCMPageRoleSource;
}

// What to call a side's pages in the status line: "added" on the Target, "removed" on the Source.
static const char* KCMPageMapRoleWord(KCMPageRole role)
{
	return (role == kKCMPageRoleTarget) ? "added" : "removed";
}

//========================================================================================
// KCMPageMapToggleSelectedPages (declared in KCMPageMap.h)
//   Runs the context-menu toggle. Any unregistered page among the selection registers them all;
//   all registered unregisters them all -- the standard toggle behaviour that goes with the tick.
//   The outcome, and this document's registration total, go to the panel's status line (which
//   survives in the session even while the panel is hidden, and shows on the next reveal).
//========================================================================================
void KCMPageMapToggleSelectedPages(const UIDList& selection)
{
	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KCMPageMapPagesOf(selection, kFalse /*no masters - KCMPageMap.h*/, db, pages))
		return;		// kCustomEnabling should already have greyed the menu out; belt and braces

	// Registering is only possible while a comparison is running (armed) and the selected
	//   document is the Target or the Source. KCMPageMapGetToggleState's answer will have greyed
	//   the menu out already, but the rule is enforced here too.
	if (!KCMIsComparedDoc(db))
		return;

	const bool16 anyUnregistered = sRegistered.ToggleAll(db, pages);

	// The panel's status area is small in both width and lines, and it is kDontEllipsize, so
	// nothing is shortened for you: keep the message to one short line.
	// @warning **do not copy its size into this comment.** The one source of truth is the Frame
	//   of kKCMStatusTextWidgetID in ui/KCMUI.fr -- measure it there. The size was written out in
	//   several files at once and went stale in all of them, each holding a different old number
	//   (memory one-question-one-place).
	// @warning the area belongs to the UI, and the model can only reach it through a
	//   notification. **The length is still decided here**: the sender is the only one who can
	//   shorten it, since the receiver has nothing to do but cut, and a number cut in half reads
	//   as a different number (memory ellipsis-in-status-line-breaks-numbers).
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(anyUnregistered ? "+" : "-");
	msg.AppendNumber((int32)pages.size());
	msg.Append(" ");
	msg.Append(KCMPageMapRoleWord(KCMPageMapRoleOf(db)));

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
		// **Always look at the return value.** Even an incremental run can rasterise enough pages
		//   for the progress bar to offer Cancel (by position - the toggle off - a registration
		//   shifts the partner of every page after it). On cancellation KCMDoMarkChangesDoc has
		//   already thrown every mark away (kFailure); carrying on regardless would leave a running
		//   comparison with no frames at all while reporting "(recompared)", which is a lie.
		//   So a cancel goes all the way back to Stop. The registration change made here is thrown
		//   away with it, which is exactly what Stop is specified to do.
		PMString report;
		if (KCMDoMarkChangesDoc(KCMArmedTargetDB(), KCMArmedSourceDB(), report, kTrue /*allowIncremental*/) != kSuccess)
		{
			KCMStopComparison();		// marks and registrations dropped (the ticks stay), disarmed, panel updated
			KCMSayStatus("Recompare cancelled - comparison stopped", kTrue /*forceRedrawNow*/);
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
	//   Registering with a re-comparison behind it is skipped on purpose: the re-comparison has
	//   already purged and redrawn the toggled pages (they are in sRegistered by then, which
	//   KCMCollectChangedPageUIDs includes). Sending it again only rasterises the same pages twice
	//   and makes the panel flicker.
	// @warning today the unregister route IS that double: a re-comparison, this incremental one
	//   too, sends **no page set at all** (KCMNotifyDocs, KCMModelNotify.h says why), so the UI
	//   purges **every page** of the document (the branch in ui/KCMModelChangeObserver.cpp taken
	//   when fPagesA is nil). The picture is right; the cost is a second rasterisation and a flicker.
	//   **The notification stays anyway**: the day a re-comparison carries its page set, per-UID
	//   purging comes back and **this is the only route that can catch an unregistered page**.
	//   (Do not delete this block because of today's double.)
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
//   Writing to the menu (SetNthActionState / SetNthActionName) and the label strings belong to
//   ui/KCMActionComponent.cpp, because the menu is the UI's job (the reasoning is with
//   KCMPageToggleState in KCMPageFlagState.h). What is left here is the counting.
//========================================================================================
KCMPageToggleState KCMPageMapGetToggleState(const UIDList& selection)
{
	KCMPageToggleState st;	// disabled by default

	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KCMPageMapPagesOf(selection, kFalse /*no masters - KCMPageMap.h*/, db, pages))
		return st;

	// Registering is only possible while a comparison is running (armed) and the selected
	//   document is the Target or the Source; anything else is greyed out. That covers a Pages
	//   panel showing no comparison at all, and one showing some third document.
	if (!KCMIsComparedDoc(db))
		return st;

	const int32 regCount = sRegistered.CountIn(db, pages);

	st.fEnabled = kTrue;
	if (regCount == (int32)pages.size())
		st.fTick = kKCMPageTickAll;		// all registered = a tick
	else if (regCount > 0)
		st.fTick = kKCMPageTickSome;		// only some = the mixed tick

	// What the label is picked from (KCMPageMapRoleOf - past the guard above there are only the two
	//   sides). kKCMPageRoleNone is only the default while fEnabled is kFalse.
	st.fRole = KCMPageMapRoleOf(db);
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

//========================================================================================
// KCMPageMapClearAllDocs (declared in KCMPageMap.h)
//   Forget every document's registrations. Stop (KCMDoClearMarks) calls it so that clearing a
//   comparison leaves no Added/Removed registrations behind either, and so do a compared
//   document's close and shutdown (KCMPeek.cpp). Only empties the map; no pointer is touched, so
//   nothing can be dereferenced.
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
// The pairing rule switch (declared in KCMPageMap.h). Default = by UID (the author's decision,
// "UID only"); the flyout's "Pair Pages by UID" check toggle turns it off: by position. Session
// state, saved with the panel settings by the UI (ui/KCMPanelState.cpp), like the folio exclusion.
// Read and written on the main thread only: KCMBuildPairing reads it (and says why that never runs
// on a background thread), and the comparison's report and the UI read it through the getter. A
// plain bool16 is enough for that; a background reader would need std::atomic.
//========================================================================================
static bool16 sPairPagesByUid = kTrue;

bool16 KCMGetPairPagesByUid()
{
	return sPairPagesByUid;
}

void KCMSetPairPagesByUid(bool16 on)
{
	sPairPagesByUid = on;
}

//========================================================================================
// KCMPageMapMadeSeparately (declared in KCMPageMap.h, where the reasoning is)
//========================================================================================
bool16 KCMPageMapMadeSeparately(IDataBase* targetDB, IDataBase* sourceDB)
{
	std::string tId, tOriginal, sId, sOriginal;
	KCMDocumentIds(targetDB, tId, tOriginal);
	KCMDocumentIds(sourceDB, sId, sOriginal);
	return (!tOriginal.empty() && !sOriginal.empty() && tOriginal != sOriginal) ? kTrue : kFalse;
}

//========================================================================================
// KCMBuildPairing (declared in KCMPageMap.h)
//   Take each document's flat page list (KCMCollectPageUIDs), drop the registered pages -- the
//   ones the reader declared to have no counterpart -- and pair what is left BY IDENTITY: a page
//   with the page of the same UID on the other side (KCMPagePairRule.h holds the rule and the
//   reasons; work/kcm-pagepair-test holds its proof). A Target page nobody answers to is an added
//   page and a Source page nobody names a removed one; both go into outOverflowTargetPages /
//   outOverflowSourcePages when those are supplied, and the drawing puts the red "/" on them.
//   Reordered pages pair with their namesakes wherever they now stand.
//
//   With the "Pair Pages by UID" toggle OFF what is left is paired in order instead, the pages
//   that fall off the longer document's end being the overflow. It is kept for two documents that
//   share no history (made separately, or one of them through IDML, which renumbers), whose UIDs
//   mean nothing. Two made separately are found out (their OriginalDocumentID differs -
//   KCMPageMapMadeSeparately) and get the "/" on every page with the report saying why; an IDML
//   one keeps its original's id, so it goes through the rule and finds no namesakes - the same
//   "/", without the word.
//
//   MAIN THREAD ONLY. Every caller runs on it, and the drawing's one road in from a background
//   thread (the asynchronous PDF export) does not reach here: EnsureOverflowCache in
//   KCMDrawEventHandler.cpp returns before rebuilding off the main thread. The pairing switch
//   (sPairPagesByUid) leans on that.
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

	if (sPairPagesByUid)
	{
		// Two documents made separately share no page, whatever their numbers say
		//   (KCMPageMapMadeSeparately): every page is an added / removed one - the "/" - and the
		//   report says why. Pairing them in order is the toggle's job, the reader's choice.
		if (KCMPageMapMadeSeparately(targetDB, sourceDB))
		{
			if (outOverflowTargetPages) *outOverflowTargetPages = tFiltered;
			if (outOverflowSourcePages) *outOverflowSourcePages = sFiltered;
			return;
		}
		KCMPairPagesByIdentity<UID>(tFiltered, sFiltered,
			outTargetPages, outSourcePages, outOverflowTargetPages, outOverflowSourcePages);
		return;
	}

	// by position (the toggle is off)
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
//   **The rule differs from KCMBuildPairing above** (UID - or position, with the toggle off -
//   there, name here), which is why this is a separate function; the reasoning is with the
//   declaration in the header.
//   Registered pages are deliberately not excluded here: registrations are made from the Pages
//   panel selection, and Register reads it (KCMReadSelectedPages, then KCMPageMapPagesOf) with
//   includeMasters=kFalse, so no master page can be in the registered set to begin with.
//   @warning **that assumption is what makes this function correct.** It never looks at the
//   registrations, so the day Register starts accepting masters, registering one would silently
//   fail to exclude it from the comparison. (This is why Register stays at kFalse while Check and
//   Refresh take masters.)
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

//========================================================================================
// KCMBuildFullPairing (declared in KCMPageMap.h, where the reasoning is)
//========================================================================================
void KCMBuildFullPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages)
{
	KCMBuildPairing(targetDB, sourceDB, outTargetPages, outSourcePages);		// clears both
	std::vector<UID> mT, mS;
	KCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
	outTargetPages.insert(outTargetPages.end(), mT.begin(), mT.end());
	outSourcePages.insert(outSourcePages.end(), mS.begin(), mS.end());
}

//========================================================================================
// KCMMapTargetToSource / KCMMapSourceToTarget (declared in KCMPageMap.h)
//   Translate one page to its counterpart. The pairing is built by KCMBuildPairing and then
//   searched linearly (KCMPartnerOf, KCMPagePairRule.h - the two directions and the two tables
//   are one lookup, tested where Alt + hover, the one caller, cannot be). It is rebuilt on every
//   ask, which that caller's one-off sample outside a press can take (a press caches the pairing -
//   KCMColorSampler.cpp). @warning asking in a loop rebuilds it each time: take KCMBuildFullPairing
//   into a table of your own, as the comparison and the peek do.
//
// **Master spread pages resolve too** (without them the CMYK readout goes blank on a master page).
//   The master pairing (KCMBuildMasterPairing, by name) is consulted only when the ordinary pairing
//   did not find the page.
//   Mixing the two tables is safe because **page UIDs are unique within a document**, so the same
//     UID cannot appear in both. The comparison's own table (KCMCore.cpp) and the partial
//     re-comparison (KCMPeek.cpp) do the same thing, holding ordinary pages and masters
//     in one std::map.
//   **The ordinary table is searched first**, so the common route never builds the master one.
//========================================================================================
bool16 KCMMapTargetToSource(IDataBase* targetDB, IDataBase* sourceDB,
	UID targetPageUID, UID& outSourcePageUID)
{
	outSourcePageUID = kInvalidUID;
	std::vector<UID> tPages, sPages;
	KCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	if (KCMPartnerOf(tPages, sPages, targetPageUID, outSourcePageUID))
		return kTrue;
	// Master spread pages (paired by name), built only because the ordinary table missed.
	std::vector<UID> mT, mS;
	KCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
	return KCMPartnerOf(mT, mS, targetPageUID, outSourcePageUID);
}

bool16 KCMMapSourceToTarget(IDataBase* targetDB, IDataBase* sourceDB,
	UID sourcePageUID, UID& outTargetPageUID)
{
	outTargetPageUID = kInvalidUID;
	std::vector<UID> tPages, sPages;
	KCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	if (KCMPartnerOf(sPages, tPages, sourcePageUID, outTargetPageUID))
		return kTrue;
	// Master spread pages (the mirror of the above).
	std::vector<UID> mT, mS;
	KCMBuildMasterPairing(targetDB, sourceDB, mT, mS);
	return KCMPartnerOf(mS, mT, sourcePageUID, outTargetPageUID);
}

// End of KCMPageMap.cpp
