//========================================================================================
//
//  KCMPageMap.h
//
//  The way in to page pairing: which page of the Target is compared against which page of the
//  Source. The pairing is BY IDENTITY - a page pairs with the page of the same
//  UID on the other side, and a UID found on one side only is an added (Target) or a removed
//  (Source) page, drawn with the red "/" without anyone having to say so. The rule itself, and
//  why there is no positional fall-back, is KCMPagePairRule.h. The flyout's "Pair Pages by UID"
//  toggle (default ON) pairs by position instead, for two documents that share no history.
//
//  The registration ("Register as Added/Removed Pages" on the Pages panel's context menu) is
//  the reader's override: a registered page is taken out before the pairing, whichever rule is
//  in force, and so has no counterpart. It matters mostly under the positional rule; under
//  the identity rule it can still unpair a page whose namesake the reader does not want compared.
//  A registration lives for the session only and is never written to the document file.
//
//  Everything downstream -- the comparison, peek's picture of the older version, a single
//  spread's re-comparison, the CMYK sampler, the view sync -- goes through KCMBuildFullPairing /
//  KCMBuildPairing / KCMMapTargetToSource / KCMMapSourceToTarget; nothing zips the raw page lists together
//  (background: docs/ai-notes/kcm-page-pairing-by-uid-2026-09-13.md).
//
//========================================================================================
#ifndef __KCMPageMap_h__
#define __KCMPageMap_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <vector>
#include <set>

// The toggle-state type the two answers below are given in. IT LIVES IN ITS OWN HEADER because
// IKCMPageFlagsFacade returns it, and a header the UI includes must not also put the
// free functions BELOW within its reach -- they are model-side and cannot be linked from there.
#include "KCMPageFlagState.h"	// KCMPageTick / KCMPageRole / KCMPageToggleState

class IDataBase;
class UIDList;			// the Pages panel's selection, as the UI read it

// The pages of a Pages panel selection THE UI READ (KCMReadSelectedPages in
// ui/KCMThumbnailRefresh.h reads the panel and hands the list over; KCMPageMap.cpp says why it is
// the UI). outDB is the selection's database (the active document's) and
// outPages its page UIDs that really exist in that document's page list - and master list, with
// includeMasters - with duplicates removed. kTrue when at least one page is valid.
// **This is the one place all the page-directed features read "the selected pages"** (Register /
//   Check / Refresh / Export Pages to Word). Change
//   what they mean here and nowhere else. The body is in KCMPageMap.cpp.
//
// includeMasters = are master pages read as part of the selection. The features disagree
//   about this, which is why it is a parameter:
//   - kTrue ... Check and Refresh. Master spreads are compared (paired by name,
//               KCMBuildMasterPairing), so master pages get frames, and both a tick and a
//               partial re-comparison mean something on them.
//   - kFalse .. Register and Export Pages to Word (a master's items are every page's -
//               KCMFacades.cpp). **A master must never be registerable**: masters pair by
//               name and never look at the registrations at all (that is KCMBuildMasterPairing's
//               contract), so allowing it would build a menu item that registers the page and
//               then changes nothing about the comparison.
bool16 KCMPageMapPagesOf(const UIDList& selection, bool16 includeMasters, IDataBase*& outDB, std::vector<UID>& outPages);

// Runs the Pages panel context-menu toggle "Register as Added/Removed Pages": registers or
// unregisters the selected pages as "no counterpart" (any unregistered one registers them all,
// all registered unregisters them all). The outcome goes to the panel's status line.
// `selection` is the Pages panel's, as the UI read it (masters left out). The body is in KCMPageMap.cpp.
void KCMPageMapToggleSelectedPages(const UIDList& selection);

// How that toggle (kCustomEnabling) should look right now.
//   - fEnabled . grey when the Pages panel selection holds no document page, when no comparison
//                is running, or when this is some third document
//   - fTick .... All when every selected page is registered, Some when only part of it is
//   - fRole .... whether the active document is the Target (= Added) or the Source (= Removed)
// **The menu itself is not touched here.** SetNthActionState / SetNthActionName are called by
//   the caller (UpdateActionStates in ui/KCMActionComponent.cpp), which also reads `selection`.
KCMPageToggleState KCMPageMapGetToggleState(const UIDList& selection);

// The liveness sweep run after documents close (called from KCMHandleDocsClosed). Drops the
// registrations of closed documents, state only. **A closed database is never dereferenced**
// (pointer comparison against IDocumentList::FindDocByDataBase, nothing more).
void KCMPageMapSweepClosedDocs();

// Forget every document's registrations. Stop uses it so that clearing a comparison leaves no
// Added/Removed registrations behind either, and so do a compared document's close and shutdown.
// Only empties the map; no pointer is touched.
void KCMPageMapClearAllDocs();

// Is pageUID (in db) registered as "no counterpart"? kFalse when db is nil or that document has
// no registrations.
bool16 KCMPageMapIsRegistered(IDataBase* db, UID pageUID);

// Does db hold any registered ("no counterpart") page at all -- existence only. The drawing
// side's early out uses it (wantMarks / wantSrcMarks in KCMDrawEventHandler::DrawSpreadMarks).
bool16 KCMPageMapHasAnyRegistered(IDataBase* db);

// Add every registered (Added/Removed = green "/") page UID of db into out. This is what puts
// the registered pages into the set of UIDs the Pages panel thumbnails are purged for: they are
// held apart from sEntries and the overflow sets, so leaving them out means the green "/" does
// not reach the thumbnail until something else redraws it. Does nothing when db is nil or has
// no registrations.
void KCMPageMapCollectRegistered(IDataBase* db, std::set<UID>& out);

// The pairing rule switch behind the flyout's "Pair Pages by UID" check toggle. kTrue (the
// default) = by identity (KCMPagePairRule.h); kFalse = by position. The UI flips it and saves it
// with the panel settings; a running comparison is re-run by the caller, as the folio toggle
// does. The body is in KCMPageMap.cpp.
bool16 KCMGetPairPagesByUid();
void KCMSetPairPagesByUid(bool16 on);

// Were the two documents MADE SEPARATELY - is their XMP OriginalDocumentID different? Then the page
// UIDs they share are a coincidence: each
// document numbers its pages in the same order from the same start (two new 3-page documents both
// numbered theirs 243, 263, 264, measured), so pairing them by UID would compare unrelated pages.
// KCMBuildPairing pairs no ordinary page of such a pair by UID, and the comparison's report says why.
// A Save As, a Save a Copy and a copy of the file keep the original's OriginalDocumentID (measured),
//   an IDML round trip too and a Task Start in memory, so they still pair
//   by UID - the IDML one finds no namesakes (its UIDs are renumbered).
//   ⚠Two documents made from the SAME TEMPLATE keep the template's original id too: this cannot
//   tell them apart (memory indesign-xmp-document-ids-on-save-as).
// kFalse when either id cannot be read: nothing is decided on a missing id.
bool16 KCMPageMapMadeSeparately(IDataBase* targetDB, IDataBase* sourceDB);

// The exclusion pairing: take each document's flat page list (KCMCollectPageUIDs), drop the
// registered ("no counterpart") pages, and pair what is left - by identity (the default: a page
// with the page of the same UID), or in order when the toggle above is off. outTargetPages[i] and
// outSourcePages[i] are one pair; both arrays end up the same length. A nil targetDB or sourceDB
// empties both and returns.
// outOverflowTargetPages / outOverflowSourcePages (optional, nil allowed) collect the pages that
// were NOT registered and still found no partner: by identity, the Target pages nobody answers to
// (added) and the Source pages nobody names (removed), either side, anywhere in the document; by
// position, the pages that fell off the longer document's end (the shorter side's stays empty).
// The body is in KCMPageMap.cpp.
void KCMBuildPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages,
	std::vector<UID>* outOverflowTargetPages = nil, std::vector<UID>* outOverflowSourcePages = nil);

// Pairs master spreads **by name** and then pairs the pages of the spreads that matched, in
// order. outTargetPages / outSourcePages come out the same length, the i-th of each being one
// pair (both are cleared on entry).
// **The rule itself differs from KCMBuildPairing above**, which is why this is a separate
//   function: ordinary pages pair by UID (or by position with the toggle off), masters by name
//   ("A-Master" and so on). A master keeps its name through insertions and reordering, whereas
//   pairing them by position means one extra master on one side makes every following pair
//   compare two unrelated masters. (Master UIDs survive Save As too - measured - so
//   pairing them by UID with the name as the fall-back would let a renamed master be followed;
//   not done, the name rule having been measured to hold.)
// A name that exists on one side only is not paired (no counterpart = not compared). A pair
// whose spreads hold different numbers of pages is truncated to the shorter one.
// The body is in KCMPageMap.cpp.
void KCMBuildMasterPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages);

// The full pairing: the ordinary pages' (KCMBuildPairing) and then the master spreads' pages
// (KCMBuildMasterPairing) appended - **the one place the two are put together**. Both vectors
// come out the same length, the i-th of each one pair (both cleared on entry). Page UIDs are
// unique within a document, so the two tables cannot collide.
// Use this wherever a page of either kind needs its partner. Leaving the masters out was a bug
//   three times, each looking like something else: the peek showed nothing on a master, Refresh
//   on a master found "no pages", the CMYK readout went blank on a master while dragging.
void KCMBuildFullPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages);

// Find the single sourceDB page that targetPageUID (in targetDB) is compared against, using
// KCMBuildPairing internally. kFalse, with outSourcePageUID kInvalidUID, when targetPageUID is
// registered (and therefore excluded) or falls outside the pairing (no counterpart).
// **Master spread pages resolve too**: when the ordinary pairing does not find the page, the
//   master pairing (KCMBuildMasterPairing, by name) is consulted as well. Page UIDs are unique
//   within a document, so the two tables cannot collide.
bool16 KCMMapTargetToSource(IDataBase* targetDB, IDataBase* sourceDB,
	UID targetPageUID, UID& outSourcePageUID);

// The other direction: the targetDB page that sourcePageUID (in sourceDB) is compared against.
bool16 KCMMapSourceToTarget(IDataBase* targetDB, IDataBase* sourceDB,
	UID sourcePageUID, UID& outTargetPageUID);

#endif // __KCMPageMap_h__
