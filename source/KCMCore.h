//========================================================================================
//
//  KCMCore.h
//
//  ChangeMarker (KCM)'s shared operations, callable from the script provider and from the panel
//  UI alike. The drawing engine itself and its file-local state live in KCMDrawEventHandler.cpp;
//  this is the thin entrance to it, so that a panel widget observer can drive exactly what a
//  script method drives (Start = mark changes and arm the peek, Clear = remove the marks and
//  disarm, and so on).
//
//========================================================================================

#ifndef __KCMCore_h__
#define __KCMCore_h__

#include "BaseType.h"		// ErrorCode, bool16
#include "KCMBoundaryID.h"	// KCMCompareMode -- a boundary type, kept where both sides read it
#include "PMString.h"
#include "PMReal.h"			// PMReal (the mouse point of the hit-test helper)
#include "OMTypes.h"			// UID (typedef IDType<UID_tag>)
#include <vector>
#include <set>				// the output set of KCMCollectChangedPageUIDs

class IDataBase;
class IControlView;

// Every page UID in the document, flattened in the document's page order. Shared by the
// comparison (KCMDoMarkChangesDoc) and the colour sampler.
// It is **IPageList** underneath (GetPageCount / GetNthPageUID), which the header itself names as
// the efficient route ("much more efficient" than computing the same from other sources).
// **Pages on hidden spreads are included** (measured; the reason is in the implementation).
void		KCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out);

// The master spreads' page UIDs, in master-spread order then page order, APPENDED to out.
// A different question from KCMCollectPageUIDs above, which returns the document's ordinary pages
// flattened -- excluding masters is that interface's contract (IPageList.h:81, "does not include
// master pages") -- and which the page pairing, Prev/Next, the TSV export, the view sync and Hide
// Unchanged all share. Mixing masters into it would change **what gets compared**, so masters are
// always collected separately and appended by the caller (the same shape overset uses).
// out is not cleared because the intended use is to append to the list of ordinary pages.
void		KCMCollectMasterPageUIDs(IDataBase* db, std::vector<UID>& out);

// Whether the spread this page sits on is hidden -- by Hide Unchanged Spreads or by the Pages
// panel's Hide Spread, it does not distinguish. Master pages cannot be hidden, so always kFalse.
// KCM has several hidden-spread tests, but they all ask it while walking an ISpreadList; asking
// **from a page UID** is this one's job, and it exists so that a sixth copy is not written inline
// ([[one-question-one-place]]).
// Used because a hidden page appears neither on screen nor in a PDF, so it is left out of output
// that names pages (Export Changed Pages). The test reads an IBoolData on kSpreadBoss
// (IID_IHIDESPREADBOOLDATA, kTrue = hidden) -- the same read Hide Unchanged uses.
bool16		KCMIsPageOnHiddenSpread(IDataBase* db, UID pageUID);

// If db is one of the documents currently being compared (sDB/sSrcDB), APPEND to outPages every
// page that could be carrying a mark right now (the changed ring, the overflow "/", the
// registered "/") and answer kTrue. For any other document it touches nothing and answers kFalse.
// **"What counts as marked" is defined here and nowhere else.** Add a kind of mark here and both
// the pre-comparison save (KCMDoMarkChangesDoc) and the UI's thumbnail purge follow automatically.
// The UI (KCMThumbnailRefresh.cpp) calls this; UI -> model is the allowed direction.
bool16		KCMCollectChangedPageUIDs(IDataBase* db, std::set<UID>& outPages);

// The answer to "may this page be given a tick (Check)".
//
// **A DIFFERENT QUESTION FROM "could this page be carrying a mark" above.** They agreed for a long
// time: in the Pixel mode a tick may only go on a page that has a ring or a "/", which is the same
// set word for word, so one function answered both. **The Story mode is where they parted**: it
// rasterises no page at all, so sEntries is empty, the tick's candidates shrink to the registered
// and overflow pages, and **"Check" disappears from the menu** (a disabled item is not shown in a
// context menu).
// So **in the Story mode every page of the Target and the Source may be ticked**.
// @warning do NOT widen "could be carrying a mark" to match. That one drives the thumbnail purge
// and the pre-comparison save, so letting it name every page rebuilds every thumbnail in the
// document on every comparison.
//
// The Story mode's answer is "all of them" rather than a set, so it is given **without counting a
// single page** (fAllPages): a 1000-page document does not get walked on every right click.
struct KCMCheckablePages
{
	bool16			fAllPages;	// Story mode = any page of this document (fPages stays empty)
	std::set<UID>	fPages;		// Pixel mode = the pages carrying a mark, **plus every master page**
								// (a master may be ticked even with no difference on it; the
								// reason is in the implementation)

	KCMCheckablePages() : fAllPages(kFalse) {}
	bool16 Includes(UID page) const { return (fAllPages || fPages.count(page) > 0) ? kTrue : kFalse; }
};

// Fill out and answer kTrue when db is one of the documents being compared; otherwise out is left
// empty and kFalse comes back = **not one page of that document may be ticked** (a third document,
// a closed one, or nothing started yet).
// @warning the "is it being compared" test uses **the same two pointers** (sDB/sSrcDB) as
// KCMCollectChangedPageUIDs above. If the mode decided WHICH DOCUMENTS count as well, switching
// mode would change which document can be ticked; all that may change is which of its pages.
bool16		KCMCollectCheckablePageUIDs(IDataBase* db, KCMCheckablePages& out);

// A page item's UID -> the page UID it sits on (kInvalidUID when it sits on no page). The overset
// report (KCMOversetScan) and the Story Edits list ask the same question, so they share one.
// The answer is always a real page (kPageBoss); a spread's UID never comes back. The reason is in
// the implementation.
UID			KCMFramePageUID(IDataBase* db, UID frameUID);

// Is this database still an open document's? A closed one must never be dereferenced, so the test
// is a pointer comparison through IDocumentList::FindDocByDataBase and nothing else (KCM's rule
// everywhere). Shared by the Hide Unchanged restore, the deferred thumbnail refresh and others.
bool16		KCMIsDocDBOpen(IDataBase* db);

// The active (front) document and its database, or nil. Resolving through IActiveContext is kept
// in this one place.
class IDocument;
IDocument*	KCMActiveDoc();
IDataBase*	KCMActiveDocDB();

// kTrue while the application is shutting down (IApplication::GetApplicationState() is kQuitting
// or kShuttingDown). The close-all phase of a quit, where a save prompt can still cancel it, is
// still kRunning = kFalse. While it is kTrue the teardown order of windows and panels is
// platform-dependent (the Mac's is not the Windows one), so all UI work -- touching widgets,
// forcing redraws, booking idle tasks -- must be skipped and the code reduced to discarding state.
bool16		KCMAppIsQuitting();

// The result of finding the page under the mouse (see KCMFindPageUnderMouse). globalPageBase is
// the flattened page number within that document (matching KCMCollectPageUIDs). Do NOT index the
// older document's pages by it: registered pages (which have no partner) are taken out first, so
// go through the mapping table instead (KCMMapTargetToSource / KCMMapSourceToTarget in
// KCMPageMap.h).
struct KCMPageHit
{
	int32 spreadIndex;		// index of the spread in the spread list (-1 for a master)
	UID   spreadUID;		// that spread's UID (re-query ISpread from it if needed)
	int32 numPages;			// how many pages that spread has
	int32 globalPageBase;	// flattened page number of the spread's first page (-1 for a master)
	int32 hitPageIndex;		// 0-based index, within the spread, of the page under the cursor
	UID   hitPageUID;		// that page's UID
	// Whether the hit was on a master spread's page.
	// @warning when kTrue, spreadIndex and globalPageBase are -1 and mean nothing -- a master is
	//   in neither ISpreadList nor IPageList, so it has no flattened page number.
	// Finding the partner page is unchanged: KCMMapTargetToSource / KCMMapSourceToTarget handle
	// both ordinary pages and masters (masters pair by name, KCMBuildMasterPairing).
	bool16 isMaster;
};

// Hit-test the mouse (in content / pasteboard coordinates) against every page of targetDB, in
// spread order then page order. Fills 'out' from the first page containing (mx, my) and answers
// kTrue; kFalse when nothing was hit.
//
// **onlySpreadUID restricts the walk to pages OF THE SAME KIND as the spread on screen**: if the
// view is showing a master, only that master; if it is showing an ordinary spread, all ordinary
// spreads (and no master). kInvalidUID walks everything, as before.
//
// **WHY IT IS NEEDED (measured): a master spread and the ordinary spreads OVERLAP in pasteboard
// coordinates.** With a master spread on screen the mouse's content point still lands on an
// ordinary spread's page, so an unrestricted walk grabs the ordinary page. The results were:
//   - peek ... builds the older version of an ordinary page, while the spread being drawn is the
//     master, so **nothing appears**;
//   - CMYK ... **reports an ordinary page's colour as the master's** -- a number comes out, so
//     nothing looks wrong.
// "Which spread is this window showing" can only be answered by the window, so the model cannot
// solve it alone. **Ordering cannot solve it either** (whichever kind is looked at first, the
// other one is misread).
//
// @warning the unit of restriction is the KIND, not the spread. Restricting to "the pages of that
// one spread" also throws away ordinary-to-ordinary hits, so with several spreads visible the
// CMYK reads `---` and Shift+ peek does not appear on any page but the one being "viewed".
// **Only master and ordinary overlap; two ordinary spreads never do** -- the evidence being that
// this walk covered all ordinary spreads for its whole life before the restriction existed, and
// never once picked the wrong one.
// @warning what to pass is **the spread that view is currently showing** --
// ILayoutControlData::GetSpreadRef(), whose header says "the spread this view is currently
// viewing". The UI observes it and hands it in.
bool16		KCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KCMPageHit& out,
                                    UID onlySpreadUID = kInvalidUID);

// Compare every page of targetDB against the same-numbered page of sourceDB and (re)build the mark
// overlay. outReport receives the same status string the script method returns.
//
// allowIncremental=kTrue attempts a differential re-comparison: the previous pairing
// (sPrevPairTargetToSource) is matched against this one, pages whose pair is unchanged reuse the
// previous result instead of calling MakeEntry (which rasterises two pages at high dpi), and only
// pages whose pair is new, changed or gone are recomputed. It is a speed-up for the register
// toggle (adding or clearing a page that has no partner) alone, where the document's content does
// not change and only the pairing moves, which is what makes reuse safe.
// Pass kFalse (the default) where content may differ (Start) or the exclusions may (the Ignore
// Page Number Marker toggle). Inconsistent state (a different document pair, or no previous
// pairing) falls back to a full comparison by itself.
ErrorCode	KCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport, bool16 allowIncremental = kFalse);

// Redraw every view of this document, if db is not nil (nil, or a document that cannot be
// resolved, does nothing). Shared by Clear, the print-mark toggles and the peek disarm so that
// both "the caller's db" and "the document the marks are actually on" are certainly redrawn (the
// same db twice is not redrawn twice).
void		KCMInvalidateDB(IDataBase* db);

// Discard the whole overlay (and the cached images of the older version) and redraw db.
void		KCMDoClearMarks(IDataBase* db);

// Whether marks are printed (and therefore also shown on screen at all times), and which frame
// opacity is chosen. opacity25Flag: kTrue = 25%, kFalse = 75%. The choice applies to the tool's
// left-hold display, the always-on display while printing is on, and the printed output alike.
void		KCMDoSetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db);

// Arm and disarm the peek at the older version (this also drives the panel's ON/OFF state).
void		KCMDoArmMousePeek(IDataBase* targetDB, IDataBase* sourceDB);
void		KCMDoDisarmMousePeek(IDataBase* db);

// The panel's state accessors. "Armed" means Start has run and Clear has not. While armed the
// panel shows the Target and Source names and the ON icon; otherwise it hides the names and shows
// OFF.
bool16		KCMIsArmed();
// Rebuild the Story Edits list from what these two documents hold **now**, and reflect it in the
// tree and the heading.
// Two callers: the whole-document comparison (KCMDoMarkChangesDoc) and "Refresh Page Comparison".
// The second one does not go through KCMDoMarkChangesDoc, so without sharing this the list would
// be left stale after a refresh and only after a refresh. nil is ignored silently.
void		KCMRebuildStoryEdits(IDataBase* targetDB, IDataBase* sourceDB);

IDataBase*	KCMArmedTargetDB();
IDataBase*	KCMArmedSourceDB();

// kTrue when a comparison is running AND db is one of the two documents in it.
//
// **The per-page flags both refuse to work anywhere else** -- registering a page or ticking it
// only means something inside a comparison -- and each of the four places that enforce it wrote
// the same expression out. The menu's kCustomEnabling has usually greyed the item out before any
// of them is reached; this is the belt to that pair of braces, which is why it is enforced in the
// command as well as asked by the menu.
bool16		KCMIsComparedDoc(IDataBase* db);

// The current print-mark settings, used to restore the check box and the radio to the real state
// when the panel is re-opened.
bool16		KCMGetPrintMarks();		// print marks on/off
bool16		KCMGetMarkOpacity25();	// frame opacity: kTrue = 25%, kFalse = 75%

// The mark colour. This replaced an automatic choice (cyan over reddish ground) and applies to
// the Pixel mode's rings and the Story mode's wash alike -- both go through SelectedMarkColor.
void		KCMDoSetMarkColor(bool16 cyan, IDataBase* db);
bool16		KCMGetMarkColorCyan();	// kFalse = red (the default), kTrue = cyan

// The comparison mode. The type is defined in KCMBoundaryID.h, being a boundary type both sides
// read.
// @warning this only changes the SETTING; it does not re-run the comparison. Re-comparing after a
// change is the caller's (the UI's KCMSetCompareMode). Folding it in here would mix "change the
// mode" with "compare", and the start-up restore would then run a comparison too.
KCMCompareMode	KCMGetCompareMode();
void				KCMSetCompareMode(KCMCompareMode mode);

// Pages panel thumbnails are refreshed in KCMThumbnailRefresh.*: purge the changed pages' UIDs
// through IImageCacheMgr::Purge, then ForceRedraw the Pages panel. Those two steps rebuild even
// thumbnails that are already on screen (after a comparison, a Clear, a print toggle and so on).
// What does NOT work, and was tried: IPagesSubPanelController::InvalidatePageWidget /
// InvalidateSpreadWidget, UpdatePagesPanel's bForcePurge, and the global thumbnail setting toggle.
// ForceRedraw is load-bearing in the current implementation. The reasoning is at the head of
// KCMThumbnailRefresh.cpp and in memory kescm-pages-panel-thumbnails.

// Call this just after a document is closed (the kAfterCloseDoc responder). It checks every
// tracked database (marks, images of the older version, the peek arm) against IDocumentList and
// deterministically cleans up the ones that have gone (DropAll / DropAllOrig / a silent disarm),
// then updates the panel from ON to OFF if anything was cleaned up.
// Which db closed cannot be learned from the signal (UIDRef is invalid at AfterClose), hence the
// liveness sweep. The implementation is in KCMPeek.cpp, the only place with access to the peek's
// file-local state.
void		KCMHandleDocsClosed();

//----------------------------------------------------------------------------------------
// Where the rest went
//
// Everything below was declared here once and now lives elsewhere. The list is kept because the
// **rule** it encodes still binds: **a model-side file that includes KCMUIShared.h is reverse
// flow** -- the model must not reach into the UI. (UI -> model is fine.)
//
//   asking a view          -> KCMViewLookup.h    (KCMQueryMouseContentPoint / KCMQueryViewUnderMouse /
//                                                 KCMFindDocDbForView / KCMQuerySpreadUIDForView /
//                                                 KCMQueryPanorama)
//   touching a widget      -> KCMUIShared.h      (KCMGetVisibleOwnPanel / KCMRefreshPanel /
//                                                 KCMSetStatus / KCMSetNavPosition /
//                                                 KCMSetToolButtonSelected / KCMActivateOwnTool /
//                                                 KCMIsOwnToolActive / KCMOpenAboutURL)
//   start/stop a comparison-> KCMComparisonRun.h (the six: toggle, stop, start-for, can-start,
//                                                 print marks, opacity)
//   the status string      -> KCMModelNotify.h   (held by the MODEL, displayed by the UI:
//                                                 app.kcmStatus answers with the panel closed,
//                                                 so the memory has to be model-side)
//   Find Overset apply     -> KCMOversetApply.h  (KCMApplyOversetForDoc / KCMOversetScanTargetDB)
//   Hide Unchanged Spreads -> KCMHideUnchanged.h (the toggle and its three reset/getter functions)
//   Sync Layout Views      -> KCMViewSync.h      (KCMGetLayoutSync / KCMSetLayoutSync /
//                                                 KCMAlignOtherViewsToActiveNow)
//
// Split Target on Start (KCMGetSplitOnStart / KCMDoSplitTarget) was removed rather than moved.
// To bring it back: docs/ai-notes/kescm-split-target-mechanism.md and git 69c4b07.
//----------------------------------------------------------------------------------------

#endif // __KCMCore_h__
