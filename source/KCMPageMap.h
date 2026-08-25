//========================================================================================
//
//  KCMPageMap.h
//
//  The way in to page pairing (added / removed pages). Pages that fall outside the flat pairing
//  of the two documents -- added in the newer version or removed from the older one, both of
//  which mean "no counterpart to compare against" -- are registered and unregistered by the user
//  through a toggle on the Pages panel's context menu. A registration lives for the session only
//  and is never written to the document file.
//
//  What the registration is for: the pairing built here (KCMBuildPairing) leaves the registered
//  pages out and pairs what is left in order, so everything downstream of it -- the comparison,
//  peek's picture of the older version, a single spread's re-comparison, the CMYK sampler --
//  lines up the right pages once the page counts differ. Nothing zips the raw page lists
//  together any more; they all go through KCMBuildPairing / KCMMapTargetToSource /
//  KCMMapSourceToTarget (background: memory kescm-page-offset-idea).
//
//========================================================================================
#ifndef __KCMPageMap_h__
#define __KCMPageMap_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <vector>
#include <set>

class IDataBase;

//----------------------------------------------------------------------------------------
// How a per-page toggle (Register / Check) should look right now.
//
// **Writing to the menu is the UI's job**, so the model only answers "is it enabled", "all or
//   some of them", and "which role does this document play". That keeps `IActionStateList` out
//   of the model's boundary altogether, the same shape as the worked example `ICusCondTxtFacade`,
//   which has no menu-state method at all. **The label strings belong to the UI too**, being UI
//   strings.
//
// Register and Check share this type: the answer has the same shape for both, and only what is
// being counted differs.
//----------------------------------------------------------------------------------------
enum KCMPageTick
{
	kKCMPageTickNone = 0,		// none of them are ticked
	kKCMPageTickSome,			// some are (the mixed tick = kMultiSelectedAction)
	kKCMPageTickAll			// the whole selection is (a tick = kSelectedAction)
};

enum KCMPageRole
{
	kKCMPageRoleNone = 0,		// not comparing, or some third document
	kKCMPageRoleTarget,		// the comparison's Target (newer) side
	kKCMPageRoleSource		// the comparison's Source (older) side
};

struct KCMPageToggleState
{
	bool16			fEnabled;	// kFalse greys the menu out (fTick / fRole mean nothing then)
	KCMPageTick	fTick;
	KCMPageRole	fRole;		// picks Register's label. Check does not read it

	KCMPageToggleState()
		: fEnabled(kFalse), fTick(kKCMPageTickNone), fRole(kKCMPageRoleNone) {}
};

// The shared reader of the Pages panel's selected pages. outDB is the document the selection
// belongs to (the active / frontmost one) and outPages the selected page UIDs that really exist
// in that document's page list, with duplicates removed. kTrue when at least one page is valid.
// The selection comes from the official Utils<ILayoutUIUtils>()->GetSelectedPages, which expands
// a selected spread into its page UIDs.
// **This is the one reader all three context-menu features share** (Register / Check / Refresh),
//   after three copies of it were merged into one. Change what "the selected pages" means here
//   and nowhere else. The body is in KCMPageMap.cpp.
//
// includeMasters = are master pages read as part of the selection. The three features disagree
//   about this, which is why it is a parameter:
//   - kTrue ... Check and Refresh. Master spreads are compared (paired by name,
//               KCMBuildMasterPairing), so master pages get frames, and both a tick and a
//               partial re-comparison mean something on them.
//   - kFalse .. Register (the default). **A master must never be registerable**: masters pair by
//               name and never look at the registrations at all (that is KCMBuildMasterPairing's
//               contract), so allowing it would build a menu item that registers the page and
//               then changes nothing about the comparison.
bool16 KCMPageMapReadSelection(IDataBase*& outDB, std::vector<UID>& outPages, bool16 includeMasters = kFalse);

// Runs the Pages panel context-menu toggle "Register as Added/Removed Pages": registers or
// unregisters the selected pages as "no counterpart" (any unregistered one registers them all,
// all registered unregisters them all). The outcome goes to the panel's status line.
// The body is in KCMPageMap.cpp.
void KCMPageMapToggleSelectedPages();

// How that toggle (kCustomEnabling) should look right now.
//   - fEnabled . grey when the Pages panel selection holds no document page, when no comparison
//                is running, or when this is some third document
//   - fTick .... All when every selected page is registered, Some when only part of it is
//   - fRole .... whether the active document is the Target (= Added) or the Source (= Removed)
// **The menu itself is not touched here.** SetNthActionState / SetNthActionName are called by
//   the caller (UpdateActionStates in ui/KCMActionComponent.cpp).
KCMPageToggleState KCMPageMapGetToggleState();

// The liveness sweep run after documents close (called from KCMHandleDocsClosed). Drops the
// registrations of closed documents, state only. **A closed database is never dereferenced**
// (pointer comparison against IDocumentList::FindDocByDataBase, nothing more).
void KCMPageMapSweepClosedDocs();

// (KCMPageMapClearAll(db), which dropped just one document's registrations, was removed. It had
//  no caller anywhere in the repository, yet this header declared one -- "called from Stop
//  (KCMDoClearMarks)" -- when what Stop actually calls is KCMPageMapClearAllDocs() below.
//  **It declared a caller that did not exist.** If a "Clear Registered Pages" command is ever
//  built, bring the entry point back together with that caller.)

// Forget every document's registrations. Stop uses it so that clearing a comparison leaves no
// Added/Removed registrations behind either. Only empties the map; no pointer is touched.
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

// Replace db's registrations wholesale with pages (the setter "Load Check & Register" uses).
// It only rewrites sRegistered: no re-comparison and no thumbnail refresh happen here, because
// the caller sets both documents first and then re-compares once. An empty pages drops the
// document's registrations. The body is in KCMPageMap.cpp.
void KCMPageMapReplaceRegistered(IDataBase* db, const std::vector<UID>& pages);

// The exclusion pairing: take each document's flat page list (KCMCollectPageUIDs), drop the
// registered ("no counterpart") pages, and pair what is left in order. outTargetPages[i] and
// outSourcePages[i] are one pair; both arrays end up the same length, truncated to the shorter
// side. A nil targetDB or sourceDB empties both and returns.
// outOverflowTargetPages / outOverflowSourcePages (optional, nil allowed) collect the pages that
// were NOT registered but still fell off the end because the documents hold different numbers of
// pages. Only the longer document's set is filled; the shorter one's stays empty.
// The body is in KCMPageMap.cpp.
void KCMBuildPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages,
	std::vector<UID>* outOverflowTargetPages = nil, std::vector<UID>* outOverflowSourcePages = nil);

// Pairs master spreads **by name** and then pairs the pages of the spreads that matched, in
// order. outTargetPages / outSourcePages come out the same length, the i-th of each being one
// pair (both are cleared on entry).
// **The rule itself differs from KCMBuildPairing above**, which is why this is a separate
//   function: ordinary pages pair by position, masters by name ("A-Master" and so on). A master
//   keeps its name through insertions and reordering, whereas pairing them by position means one
//   extra master on one side makes every following pair compare two unrelated masters.
// A name that exists on one side only is not paired (no counterpart = not compared). A pair
// whose spreads hold different numbers of pages is truncated to the shorter one.
// The body is in KCMPageMap.cpp.
void KCMBuildMasterPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages);

// Find the single sourceDB page that targetPageUID (in targetDB) is compared against, using
// KCMBuildPairing internally. kFalse, with outSourcePageUID left undefined, when targetPageUID is
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
