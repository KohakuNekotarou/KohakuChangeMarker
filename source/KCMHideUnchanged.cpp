//========================================================================================
//
//  KCMHideUnchanged.cpp
//
//  The implementation of "Hide Unchanged Spreads". Spreads carrying no change mark are hidden
//  with kHideSpreadCmdBoss, ONLY what this feature hid is remembered, and only that is shown
//  again. Target and Source are classified the same way.
//
//  **Everything that decides "carries no change mark" is read from the same "as compared"
//  record the drawing looks at**: the overflow sets are sOverflowT (the cache) and the Source
//  side's exclusion table is sPrevPairTargetToSource (the previous comparison's pairing).
//  KCMBuildPairing used to be called here instead, which worked the answer out from the
//  CURRENT document structure -- so unless the user re-compared after adding or removing pages,
//  **the "/" on screen and the hide/keep decision disagreed**.
//
//  MODEL side: issuing kHideSpreadCmdBoss changes the document (both documents go dirty).
//  **The Target and Source commands are issued inside one CmdUtils::SequenceContext**, so one
//  Ctrl+Z puts both back.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "CAlert.h"
#include "PMString.h"

// For Hide Unchanged Spreads (kHideSpreadCmdBoss):
#include "CmdUtils.h"				// CreateCommand / ProcessCommand (model changes go through a Command) / SequenceContext
#include "ICommand.h"
#include "ErrorUtils.h"				// GlobalErrorStatePreserver -- hiding and restoring may fail, and what they raise must not leave
#include "IBoolData.h"				// kHideSpreadCmdBoss has no CmdData of its own; the direction rides on IBoolData (as kLockLayerCmdBoss does)
#include "UIDList.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "SpreadID.h"				// kHideSpreadCmdBoss / IID_IHIDESPREADBOOLDATA
#include "IDataBase.h"				// GetRootUID() (walking the spreads)
#include <vector>
#include <map>
#include <set>

// Project includes:
#include "KCMID.h"
#include "KCMLoc.h"		// the confirmation shown before the document is changed follows the UI language
#include "KCMHideUnchanged.h"
#include "KCMCore.h"		// KCMIsDocDBOpen / KCMArmedSourceDB
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it (Task 9)
#include "KCMDrawEventHandler.h"	// sDB / sEntries / sOverflowT / sPrevPairTargetToSource -- what
							//   "changed" is decided from. **All of it the "as compared" record,
							//   i.e. the same thing the screen, the thumbnails and the map read**,
							//   so that hiding agrees with what is drawn.
#include "KCMPageMap.h"	// KCMPageMapIsRegistered / KCMPageMapHasAnyRegistered
#include "KCMID.h"		// kKCMPageFlagsChangedMessage (the notification ID)
// The UI-side header KCMScrollMap.h is deliberately NOT included: redrawing the map after a
// hide or a restore is the job of the UI that receives the notification.

// The toggle's state. While it is ON, sHiddenSpreads is the record of the spread UIDs THIS
// feature hid -- OFF shows back exactly those, so spreads the user hid by hand in the Pages
// panel are never swept up. sHiddenDB is the document they were hidden in. Like the other
// statics it lives for the session only; the hidden state itself is persistent, since
// kHideSpreadCmdBoss writes it into the .indd (going dirty is part of the agreed feature).
// The Source document is hidden by the same classification, with its own record.
static bool16 sHideUnchangedOn = kFalse;
static std::vector<UID> sHiddenSpreads;
static IDataBase* sHiddenDB = nil;
static std::vector<UID> sHiddenSrcSpreads;
static IDataBase* sHiddenSrcDB = nil;

// The body of the restore. The public KCMResetHideUnchanged is void, and so is the boundary
// Facade, so **how many restore commands failed** is something only this internal version
// returns. The ON->OFF toggle calls it directly -- otherwise the status line would report
// "restored" for spreads that are still hidden.
static int32 KCMResetHideUnchangedCore(bool16 restoreSpreads);

//========================================================================================
// Hide Unchanged Spreads (the flyout's checked toggle)
//========================================================================================

// Hide or show `uids` with a single kHideSpreadCmdBoss. hide = kTrue hides.
// The direction rides on IBoolData, kTrue = hide (measured; the same shape as kLockLayerCmdBoss).
static ErrorCode KCMProcessHideSpreadCmd(IDataBase* db, const std::vector<UID>& uids, bool16 hide)
{
	if (db == nil || uids.empty())
		return kFailure;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kHideSpreadCmdBoss));
	if (cmd == nil)
		return kFailure;

	UIDList list(db);
	for (size_t i = 0; i < uids.size(); ++i)
		list.Append(uids[i]);
	cmd->SetItemList(list);

	InterfacePtr<IBoolData> data(cmd, IID_IBOOLDATA);
	if (data == nil)
		return kFailure;
	data->Set(hide);

	return CmdUtils::ProcessCommand(cmd);
}

/* KCMHideUnchangedToggle (declared in KCMHideUnchanged.h) -- the flyout's "Hide Unchanged
   Spreads".
   OFF->ON: confirm (Yes/No, in the UI language), then collect every spread that holds no
   comparison mark (sEntries), hide them all with one kHideSpreadCmdBoss, remember the UIDs and
   tick the item. The Source document follows by the same classification, through the flat page
   pairing (both documents go dirty).
   ON->OFF: show back what was recorded, on both sides, without asking.
   Guard: with no comparison marks (before Start, or nothing changed) it does nothing. In
   particular, if NO spread carries a change every spread would be hidden, and InDesign does not
   allow hiding them all, so it stops. (If only the Source side would be emptied, only the
   Source side is skipped.) */
void KCMHideUnchangedToggle()
{
	// ON->OFF: show back what this feature hid, then drop the state.
	if (sHideUnchangedOn)
	{
		const int32 failed = KCMResetHideUnchangedCore(kTrue);
		// The scrollbar map is redrawn for the new arrangement (hidden spreads are left out of it).
		// The map belongs to the UI, so this goes out as a notification: showing or hiding a spread
		// is "the pages look different now", which rides on kKCMPageFlagsChangedMessage.
		// **No document is passed** -- no thumbnail purge is needed, only the strip's layout changes.
		KCMNotify(kKCMPageFlagsChangedMessage);
		// @warning the result is checked. This used to print "restored." unconditionally, i.e. it
		//   reported success for spreads it had failed to show. The record is dropped either way (the
		//   next Start rebuilds it), so a failure here leaves the user to re-show them by hand from
		//   the Pages panel -- not a failure to swallow silently.
		if (failed > 0)
			KCMSayStatus("Hide Unchanged: could not show all hidden spreads back.");
		else
			KCMSayStatus("Hide Unchanged: hidden spreads restored.");
		return;
	}

	// OFF -> ON.
	IDataBase* db = KCMDrawEventHandler::sDB;
	if (db == nil)
	{
		// Nothing started yet.
		KCMSayStatus("Hide Unchanged: Start first.");
		return;
	}

	// A spread holding an overflow page (one that carries "/" -- not registered, but with no
	// counterpart because the two documents differ in page count, i.e. never compared) is kept
	// visible, exactly like a spread holding a changed page or a registered "Added" page, so that
	// nothing uncompared is hidden away. (On the Source side the classification below already
	// treats a page outside the pairing as "changed", so those are not hidden either.)
	//
	// **That "/" has to be the same set as the "/" on screen.** This used to call KCMBuildPairing
	// and compute the overflow **from the current document structure**, while the screen, the
	// thumbnails, the scrollbar map and the boundary (IKCMMarkData::IsOverflowPage) all read the
	// overflow cache (sOverflowT/sOverflowS) -- and that one is **frozen at the moment of the
	// comparison** ("does not follow bare page insertions or deletions; fixed until the next
	// Start or re-comparison", as KCMDrawEventHandler.h puts it). So unless the user re-compared
	// after adding or removing pages, **a page carrying no "/" on screen was counted as changed**
	// (and the other way round): the promise above pointed at a different set from the one the
	// screen was showing. It reads the drawing's cache now.
	// @warning EnsureOverflowCache does **nothing** when the recorded (sDB, sSrcDB) still match
	//   the current ones, which is the ordinary case. When it does rebuild, it takes the lock and
	//   swaps, so it cannot collide with a background draw.
	// @warning Target comes from the drawing engine's sDB and Source from the armed state
	//   (KCMArmedSourceDB) -- **two different places**. What keeps them in step is that
	//   KCMDoDisarmMousePeek has exactly one caller, KCMStopComparison, which calls
	//   KCMDoClearMarks (dropping sDB) immediately before it: "armed dropped while sDB survives"
	//   cannot be reached. **The day a path disarms on its own, this premise is gone**
	//   ([[one-question-one-place]]).
	IDataBase* const srcDB = KCMArmedSourceDB();
	const bool16 hasSource = (srcDB != nil && srcDB != db);
	KCMDrawEventHandler::EnsureOverflowCache();
	const std::set<UID>& tOverflowSet = KCMDrawEventHandler::sOverflowT;

	// An empty sEntries is not the end: registered ("Added") pages and overflow pages count as
	// "changed, so keep it" in their own right. Only when there is none of any kind would every
	// spread be a candidate -- and InDesign does not allow hiding them all, so it stops.
	if (KCMDrawEventHandler::sEntries.empty() && !KCMPageMapHasAnyRegistered(db) && tOverflowSet.empty())
	{
		KCMSayStatus("Hide Unchanged: no changes to hide.");
		return;
	}

	// An unchanged spread = one whose pages appear in sEntries not even once.
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
	{
		KCMSayStatus("Hide Unchanged: spread list not available.");
		return;
	}
	std::vector<UID> unchanged;
	int32 visibleCount = 0;		// spreads currently visible -- the denominator of the "not all of them" guard
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		// Spreads the user has already hidden (from the Pages panel, say) are left alone: taking
		// them into the record would show them again on ON->OFF, which is not ours to do.
		// The hidden state is read from the IBoolData on kSpreadBoss (IID_IHIDESPREADBOOLDATA,
		// kTrue = hidden).
		InterfacePtr<IBoolData> hideFlag(db, spreadUID, IID_IHIDESPREADBOOLDATA);
		if (hideFlag != nil && hideFlag->GetBool())
			continue;
		++visibleCount;
		bool16 changed = kFalse;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
		{
			const UID pageUID = spread->GetNthPageUID(p);
			// A registered ("Added") page has no counterpart, so it is never compared and never
			// reaches sEntries -- but it carries a green frame and counts as changed. An overflow
			// page ("/", uncompared because the page counts differ) counts the same way, so that
			// nothing uncompared is hidden by mistake.
			if (KCMDrawEventHandler::sEntries.count(pageUID) > 0 ||
			    KCMPageMapIsRegistered(db, pageUID) ||
			    tOverflowSet.count(pageUID) > 0)
			{
				changed = kTrue;
				break;
			}
		}
		if (!changed)
			unchanged.push_back(spreadUID);
	}

	if (unchanged.empty())
	{
		KCMSayStatus("Hide Unchanged: all changed; none to hide.");
		return;
	}
	if ((int32)unchanged.size() >= visibleCount)
	{
		// Belt and braces (with sEntries non-empty this is not normally reached): stop if every
		// visible spread would be hidden. InDesign does not allow it, and the denominator is the
		// spreads visible NOW, so manually hidden ones are already out of it.
		KCMSayStatus("Hide Unchanged: can't hide all spreads.");
		return;
	}

	// The confirmation. kHideSpreadCmdBoss is a persistent edit: the document goes dirty and the
	// hidden state reaches the saved file. The wording follows the UI language (enUS / jaJP);
	// Windows only offers the standard Yes/No buttons.
	// **This is the one alert the model half raises.** Why it stays:
	//   - `CAlert` is a static class in Public.lib, **not a boss provided by a UI plug-in**, so it
	//     is not the "returns nil on a background thread" case the guide describes (which is why
	//     neither the linker nor a grep flags it).
	//   - this function is only ever entered from a flyout action through the Facade, and **the
	//     drawing paths that run on background threads never reach it** (checked by grepping every
	//     call site).
	// @warning it is still the model asking a human. **If a path is ever added that calls this
	//   from a background thread, move the question to the UI and take the answer as an argument.**
	//   A modal raised on a background thread has nobody to answer it and **stops that thread**.
	const int16 clicked = CAlert::ModalAlert
	(
		// "This feature modifies the document file. Continue?" -- in Japanese on a Japanese UI
		// (KCMLoc). This one is asked **before the document is changed**, so it is shown in the
		// user's own language to remove any chance of misreading it.
		KCMLoc::Text(kKCMHideConfirmKey, KCMJa::kHideConfirm),
		kYesString,
		kNoString,
		kNullString,
		1,							// Yes is the default button
		CAlert::eWarningIcon
	);
	if (clicked != 1)
		return;						// No: nothing happens, and the item is not ticked

	// ---- Target and Source are issued as ONE sequence from here ----
	// This used to be a separate ProcessCommand per document, which split the undo into two
	// steps. Measured: kHideSpreadCmdBoss pushes **one step per call** (hide two spreads
	// separately, press Ctrl+Z once, and only the second comes back). Left split, one Ctrl+Z would
	// restore the Source while the Target stayed hidden -- with KCM's record still saying "both
	// are hidden".
	// @warning splitting an operation that spans documents into one sequence per document gives
	//   the shape where undoing one leaves the other **gone from the history but not restored**
	//   (measured). The official vessel is CmdUtils::SequenceContext -- the variant that JOINS an
	//   existing sequence rather than starting a second one. Adobe's example:
	//   conditionaltextui/ConditionSetDropDownObserver.cpp (UpdateAllConditionSets).
	// **No sequence name is passed**, so the Edit menu keeps InDesign's own wording for the
	//   command; KCM's UI strings are English-only and have no business appearing in a Japanese
	//   Edit menu.
	// **The order of the two declarations matters**: GlobalErrorStatePreserver is constructed
	//   first, so the sequence declared after it is destroyed first. Whether the sequence commits
	//   or rolls back is decided by the global error code at the moment it closes (see
	//   CmdUtils::SequenceContext's own comment), so every failure is turned into a status line
	//   first and the error state is then cleared. Leaving it raised and issuing the next command
	//   is what CmdUtils::ProcessCommand documents as protective shutdown.
	// The same shape appears twice more in KCM: the heading "This open is allowed to fail" in
	//   KCMBookCompare.cpp, and "THE WINDOW IS ALLOWED NOT TO APPEAR" in ui/KCMBookOpen.cpp.
	//   @warning **quoting a heading only works while the heading is spelled the same way** --
	//     these two were line numbers once, and both had drifted by ten lines; the first quotation
	//     then broke again when that heading was reworded. Grep for a fragment, not the casing.
	ErrorCode err = kFailure;
	int32 srcHiddenCount = 0;
	bool16 srcSkippedAll = kFalse;
	bool16 srcFailed = kFalse;		// the Source hide command failed (this used to be entirely silent)
	{
		GlobalErrorStatePreserver hideErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		CmdUtils::SequenceContext hideSeq;

		err = KCMProcessHideSpreadCmd(db, unchanged, kTrue /*hide*/);
		if (err == kSuccess)
		{
			sHideUnchangedOn = kTrue;
			sHiddenDB = db;
			sHiddenSpreads = unchanged;

			// ---- The Source side is hidden by the same classification ----
			// "Changed" is carried over to Source pages through the exclusion pairing (the ordinal
			// correspondence with registered pages -- those with no counterpart -- left out), then the
			// Source spreads are walked and any that holds no page corresponding to a changed one is
			// hidden. A Source page missing from the pairing (i.e. registered as removed) is treated as
			// changed, which is the safe side.
			// A failure or a skip on the Source side leaves the Target hide standing (it is not fatal).
			// @warning **that is exactly why the failure must not be left in the error state**: leaving
			//   it raised would roll the Target hide back too when the sequence closes.
			if (hasSource)
			{
				// **The pairing table is the one from the comparison, too.** sPrevPairTargetToSource is
				// the Target->Source pairing the last comparison used, and it is built from **the same
				// single comparison** as the sEntries read below (KCMCore.cpp fills both from the same
				// tPages/sPages indices and swaps them in at the end). KCMBuildPairing used to be called
				// here instead, so once the page structure changed after Start, the Source was classified
				// by "**today's** table x **the comparison's** marks" -- a combination that never existed
				// at any single moment. (Same reason as lining the Target overflow up with the screen;
				// see the comment at the top of this function.)
				// @warning registered (Added/Removed) pages and overflow pages were never in this table:
				//   the find misses and they fall to "changed", i.e. not hidden. That safe-side property
				//   is unchanged.
				// @warning the table also holds the master-page pairs (KCMCore.cpp appends them after the
				//   ordinary pages), but the walk below is over ISpreadList, which visits ordinary
				//   spreads only, so the extra pairs are never looked up.
				std::map<UID, bool16> srcChangedMap;	// Source page in the table -> is its Target page changed
				const std::map<UID, UID>& pairing = KCMDrawEventHandler::sPrevPairTargetToSource;
				for (std::map<UID, UID>::const_iterator pit = pairing.begin(); pit != pairing.end(); ++pit)
					srcChangedMap[pit->second] = (KCMDrawEventHandler::sEntries.count(pit->first) > 0) ? kTrue : kFalse;

				InterfacePtr<ISpreadList> srcSpreadList(srcDB, srcDB->GetRootUID(), UseDefaultIID());
				if (srcSpreadList != nil)
				{
					std::vector<UID> srcUnchanged;
					int32 srcVisibleCount = 0;	// denominator of the Source side's "not all of them" guard (visible only)
					const int32 nss = srcSpreadList->GetSpreadCount();
					for (int32 s = 0; s < nss; ++s)
					{
						const UID srcSpreadUID = srcSpreadList->GetNthSpreadUID(s);
						InterfacePtr<ISpread> srcSpread(srcDB, srcSpreadUID, UseDefaultIID());
						if (srcSpread == nil)
							continue;
						const int32 np = srcSpread->GetNumPages();
						// Source spreads the user hid by hand are left alone, as on the Target side.
						InterfacePtr<IBoolData> srcHideFlag(srcDB, srcSpreadUID, IID_IHIDESPREADBOOLDATA);
						if (srcHideFlag != nil && srcHideFlag->GetBool())
							continue;
						++srcVisibleCount;
						bool16 srcChanged = kFalse;
						for (int32 p = 0; p < np; ++p)
						{
							const UID srcPageUID = srcSpread->GetNthPageUID(p);
							std::map<UID, bool16>::const_iterator mit = srcChangedMap.find(srcPageUID);
							if (mit == srcChangedMap.end() || mit->second)
							{
								srcChanged = kTrue;
								break;
							}
						}
						if (!srcChanged)
							srcUnchanged.push_back(srcSpreadUID);
					}

					if (!srcUnchanged.empty())
					{
						if ((int32)srcUnchanged.size() >= srcVisibleCount)
						{
							// Hiding every spread is not allowed. It can be reached when the changes sit on
							// pages added to the Target, so that no Source page corresponds to any of them and
							// every Source spread classifies as unchanged. Then the Source side is skipped and
							// the Target hide stands.
							srcSkippedAll = kTrue;
						}
						else if (KCMProcessHideSpreadCmd(srcDB, srcUnchanged, kTrue /*hide*/) == kSuccess)
						{
							sHiddenSrcDB = srcDB;
							sHiddenSrcSpreads = srcUnchanged;
							srcHiddenCount = (int32)srcUnchanged.size();
						}
						else
							srcFailed = kTrue;
					}
				}
			}
		}

		// Every failure has been turned into the status line below, so the error state is cleared
		// before the sequence closes. **The condition for clearing is that the failure survives in
		// another form** -- this is not swallowing it.
		if (err != kSuccess || srcFailed)
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}

	if (err != kSuccess)
	{
		KCMSayStatus("Hide Unchanged: hide command failed.");
		return;
	}

	PMString msg("Hide Unchanged: hid ");
	msg.SetTranslatable(kFalse);
	msg.AppendNumber((int32)unchanged.size());
	msg.Append(" target spread(s)");
	if (srcHiddenCount > 0)
	{
		msg.Append(" + ");
		msg.AppendNumber(srcHiddenCount);
		msg.Append(" source spread(s)");
	}
	msg.Append(".");
	if (srcSkippedAll)
		msg.Append(" Source not hidden (would hide all its spreads).");
	if (srcFailed)
		msg.Append(" Source not hidden (hide command failed).");
	KCMNotifyStatus(msg);

	// Redraw the scrollbar map for the new arrangement, in both windows (hidden spreads are left
	// out of it). A notification, for the same reason as the ON->OFF branch above.
	KCMNotify(kKCMPageFlagsChangedMessage);
}

// Document liveness is asked of the shared helper KCMIsDocDBOpen (KCMCore.h).

// Show one side back (db + its record) and drop the state. The restore command is only issued
// when restore = kTrue and the document is still open; spreads deleted in the meantime are
// skipped, since the ISpread query comes back nil for them. If the document has closed, the
// state is dropped silently with no dereference. db is taken by reference and reset to nil.
// **Returns kTrue when a restore command was issued and failed** -- the return value used to be
// discarded, so neither the caller nor the user learned that spreads had stayed hidden. The
// record is dropped either way, as before: the next Start rebuilds it, so keeping it buys
// nothing.
static bool16 KCMRestoreHiddenList(IDataBase*& db, std::vector<UID>& list, bool16 restore)
{
	bool16 failed = kFalse;
	if (restore && db != nil && !list.empty() && KCMIsDocDBOpen(db))
	{
		std::vector<UID> alive;
		for (size_t i = 0; i < list.size(); ++i)
		{
			InterfacePtr<ISpread> spread(db, list[i], UseDefaultIID());
			if (spread != nil)
				alive.push_back(list[i]);
		}
		if (!alive.empty() && KCMProcessHideSpreadCmd(db, alive, kFalse /*unhide*/) != kSuccess)
			failed = kTrue;
	}
	list.clear();
	db = nil;
	return failed;
}

// KCMResetHideUnchangedCore (forward-declared at the top of this file) -- the body of the
// reset. Returns the number of restore commands that failed (0..2).
//   restoreSpreads = kTrue: show the recorded spreads again, then drop the state
//     (re-comparison / Stop / the toggle / the close sweep). Document liveness is checked
//     internally, so it is safe when one side has closed -- only the surviving side is shown.
//   restoreSpreads = kFalse: touch no database, drop the state only (no command, so no
//     sequence either).
// **The two restores go into one sequence** for the same reason the two hides do. They used to
// be issued back to back, so a failure in the first left the error state raised while the
// second was issued -- the exact shape CmdUtils::ProcessCommand documents as protective
// shutdown.
static int32 KCMResetHideUnchangedCore(bool16 restoreSpreads)
{
	int32 failed = 0;
	if (restoreSpreads)
	{
		GlobalErrorStatePreserver restoreErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		CmdUtils::SequenceContext restoreSeq;

		if (KCMRestoreHiddenList(sHiddenDB, sHiddenSpreads, kTrue))
			++failed;
		if (KCMRestoreHiddenList(sHiddenSrcDB, sHiddenSrcSpreads, kTrue))
			++failed;

		// The failure is already in the return value (and in the toggle's status line), so the error
		// state is cleared before the sequence closes. Left raised, the sequence would roll back even
		// the side that succeeded, leaving spreads hidden after a "restored" report.
		if (failed > 0)
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
	else
	{
		KCMRestoreHiddenList(sHiddenDB, sHiddenSpreads, kFalse);
		KCMRestoreHiddenList(sHiddenSrcDB, sHiddenSrcSpreads, kFalse);
	}
	sHideUnchangedOn = kFalse;
	return failed;
}

// KCMResetHideUnchanged (declared in KCMHideUnchanged.h) -- the public form of the body above.
// Its callers do not change what they do according to whether the restore worked, so it stays
// void and the boundary Facade is left alone.
void KCMResetHideUnchanged(bool16 restoreSpreads)
{
	(void)KCMResetHideUnchangedCore(restoreSpreads);
}

// KCMGetHideUnchangedOn (declared in KCMHideUnchanged.h) -- for the menu item's check mark.
// The toggle flag stays inside this file; the UI's UpdateActionStates asks here.
bool16 KCMGetHideUnchangedOn()
{
	return sHideUnchangedOn;
}

IDataBase* KCMGetHideUnchangedDB()
{
	return sHiddenDB;
}

IDataBase* KCMGetHideUnchangedSrcDB()
{
	return sHiddenSrcDB;
}

// End, KCMHideUnchanged.cpp.
