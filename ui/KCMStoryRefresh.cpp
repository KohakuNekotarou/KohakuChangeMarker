//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryRefresh.h for what this is for. Three small answers live here: which row the menu
//  is about, whether the item may be offered, and what the panel says once it has run.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "PMString.h"
#include "Utils.h"

#include <vector>

// Project includes:
#include "KCMUIID.h"
#include "IKCMCompareFacade.h"		// IsArmed / GetCompareMode - both of the conditions on the item
#include "IKCMStoryEditsFacade.h"		// the row itself, and RefreshRow - the work
#include "KCMStoryRefresh.h"
#include "KCMStoryTreeController.h"	// KCMStoryRowsOfRightClick - the rows selected together (2026-10-24 night)
#include "KCMUIShared.h"				// KCMSetStatus - the panel's message line

namespace
{

/* Which row the right-click menu was popped over.

   ★A FILE STATIC, because it belongs to
   "the menu that is up right now" rather than to any row widget - and row widgets are recycled as
   the list scrolls, so a member would be attached to the wrong story the moment the reader moved.
   One menu is up at a time.

   ★IT IS NOT CLEARED WHEN THE MENU CLOSES, and it does not need to be: every reader of it checks
   the row against the list as it stands now (KCMStoryRowCanRefresh), so a stale index from a
   comparison ago answers kFalse rather than acting on whatever row happens to sit at that number.
*/
int32 gMenuRow = -1;

/* Which CHANGE of that row, when the menu was popped over a change row (2026-09-24, stage 2 A); -1 when it
   was popped over a story row. ★Written by both kinds of click (KCMStorySetMenuRow clears it), so a story
   row's menu can never find a change left over from an earlier right click. */
int32 gMenuChange = -1;

/* Which AUTHOR, when the menu was popped over a Track author row (2026-10-25 - accept and reject at the author's level); -1
   otherwise. ★Written by every kind of right click (the two setters above clear it), so a row's menu can never find an
   author left over from an earlier right click. */
int32 gMenuAuthor = -1;

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMStorySetMenuRow / KCMStoryMenuRow
//   ⚠The note that stood here - "NO GETTER, where the chapter rows have KCMBookMenuRow, because
//   both readers of this one take no argument" - stopped being true on 2026-09-09, when the
//   Resources mode put a second item on this menu. That item's row belongs to the OTHER list, so
//   it cannot go through KCMStoryRowCanRefresh and needs the number itself.
//   ⇒ A getter, rather than a second stash: two records of "which row was right-clicked" would be
//   the same fact written twice, and one of them would go stale.
//----------------------------------------------------------------------------------------

void KCMStorySetMenuRow(int32 rowIndex)
{
	gMenuRow = rowIndex;
	gMenuChange = -1;		// a story row's menu: no change (2026-09-24)
	gMenuAuthor = -1;		// ...and no author row (2026-10-25)
}

//----------------------------------------------------------------------------------------
// KCMStorySetMenuChange - a CHANGE row's own menu (2026-09-24; its first item, "Reject This Import Change", went on
//   2026-10-25 with the import that wrote into the document, and "Redo from Word" with it).
//----------------------------------------------------------------------------------------

void KCMStorySetMenuChange(int32 rowIndex, int32 changeIndex)
{
	gMenuRow = rowIndex;
	gMenuChange = changeIndex;
	gMenuAuthor = -1;
}

//----------------------------------------------------------------------------------------
// KCMStorySetMenuAuthor - a Track AUTHOR row's own menu (2026-10-25)
//----------------------------------------------------------------------------------------

void KCMStorySetMenuAuthor(int32 author)
{
	gMenuRow = -1;
	gMenuChange = -1;
	gMenuAuthor = author;
}

namespace
{
/** What the Story mode's change row items ask first: a CHANGE row was right-clicked, a comparison is armed, and the
	mode has story rows. (Asked in this order for the reason KCMStoryRowCanRefresh gives: the mode is a setting that
	outlives Stop.) What each item then asks of ITS row is its own. */
bool16 ChangeRowMenuLive()
{
	if (gMenuRow < 0 || gMenuChange < 0)
		return kFalse;
	if (!Utils<IKCMCompareFacade>()->IsArmed())
		return kFalse;
	// ★NEITHER RESTORE NOR MATCH AGAINST AN IMPORT'S COPY (2026-10-24): there the rows are what the document WOULD
	//   become, and "Apply This Change" (below) is the item - it uses the restore and the table's road itself where a
	//   row needs them.
	if (Utils<IKCMStoryEditsFacade>()->IsImportSource())
		return kFalse;
	// The change row's menu offers what only a DIFFED row can do - none of it in the Track mode (design D3, 2026-10-05).
	return KCMModeDiffsStoryText(Utils<IKCMCompareFacade>()->GetCompareMode());
}

/** The status line's sentence for an Apply: the model's words, or the reason it gave. */
void SayApplied(const char* what, int32 done, const PMString& why)
{
	PMString msg;
	msg.SetTranslatable(kFalse);
	if (done < 0)
	{
		msg.Append(what);
		msg.Append(": could not - ");
	}
	msg.Append(why);
	KCMSetStatus(msg);
}
}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMChangeRowCanApply / KCMChangeRowApply / KCMStoryRowCanApply / KCMStoryRowApply / KCMHeadingCanApplyAll /
// KCMHeadingApplyAll - the three Apply items (2026-10-24 -
// docs/superpowers/specs/2026-10-24-kcm-import-as-source-design.md). Each asks the model whether it may act and the
// model does the work; the same test is asked again at the moment of acting (KCMStoryRefreshMenuRow's reason).
//----------------------------------------------------------------------------------------

namespace
{
/** ★THE ROWS A CHANGE ROW'S ITEM ACTS ON (2026-10-24 night - design 1-5, KFC's RowsOfRightClick): every change row
	selected, when the row right-clicked is one of several selected together; else that row. Of those, the ones `can`
	says yes for - into two plain arrays, the way the facade's batches take them. */
template <typename Can>
void RowsOfMenuThatCan(Can can, std::vector<int32>& outRows, std::vector<int32>& outChanges)
{
	outRows.clear();
	outChanges.clear();
	std::vector<KCMStoryChangeRow> rows;
	KCMStoryRowsOfRightClick(gMenuRow, gMenuChange, rows);
	for (size_t k = 0; k < rows.size(); ++k)
		if (can(rows[k].first, rows[k].second))
		{
			outRows.push_back(rows[k].first);
			outChanges.push_back(rows[k].second);
		}
}
}	// anonymous namespace

bool16 KCMChangeRowCanApply()
{
	if (gMenuRow < 0 || gMenuChange < 0)
		return kFalse;
	if (!Utils<IKCMCompareFacade>()->IsArmed())
		return kFalse;
	// ★Live when any of the rows it acts on can go in (KFC's CanReplaceAny) - several selected, or the one right-clicked.
	std::vector<int32> rows, changes;
	RowsOfMenuThatCan([](int32 r, int32 c) { return Utils<IKCMStoryEditsFacade>()->CanApplyChange(r, c) != kFalse; },
					  rows, changes);
	return rows.empty() ? kFalse : kTrue;
}

bool16 KCMChangeRowApply()
{
	std::vector<int32> rows, changes;
	RowsOfMenuThatCan([](int32 r, int32 c) { return Utils<IKCMStoryEditsFacade>()->CanApplyChange(r, c) != kFalse; },
					  rows, changes);
	if (rows.empty() || !Utils<IKCMCompareFacade>()->IsArmed())
	{
		KCMSetStatus("apply: not a change of the imported text.");
		return kFalse;
	}
	// ★ONE UNDO STEP FOR THE ROWS SELECTED TOGETHER (design 1-5) - one row alone is the single command (the facade says).
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->ApplyChanges(&rows[0], &changes[0], static_cast<int32>(rows.size()), why);
	SayApplied("apply", done, why);
	return (done > 0) ? kTrue : kFalse;
}

bool16 KCMStoryRowCanApply()
{
	if (gMenuRow < 0 || gMenuChange >= 0)
		return kFalse;
	if (!Utils<IKCMCompareFacade>()->IsArmed())
		return kFalse;
	return Utils<IKCMStoryEditsFacade>()->CanApplyStory(gMenuRow);
}

bool16 KCMStoryRowApply()
{
	if (!KCMStoryRowCanApply())
	{
		KCMSetStatus("apply: not a story of the imported text with anything left to put in.");
		return kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->ApplyStory(gMenuRow, why);
	SayApplied("apply", done, why);
	return (done > 0) ? kTrue : kFalse;
}

bool16 KCMHeadingCanApplyAll()
{
	// ★NO ROW ASKED (2026-10-24 evening): the item is on the section heading's menu now (KCMTopPaneEH) and acts
	//   on every story the import filled, so whichever row the last menu was popped over has nothing to say about it.
	return Utils<IKCMStoryEditsFacade>()->IsImportSource();
}

bool16 KCMHeadingApplyAll()
{
	if (!KCMHeadingCanApplyAll())
	{
		KCMSetStatus("apply: no imported text is being compared.");
		return kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->ApplyAll(why);
	SayApplied("apply", done, why);
	return (done > 0) ? kTrue : kFalse;
}

bool16 KCMChangeRowCanRestoreCopy()
{
	if (gMenuRow < 0 || gMenuChange < 0)
		return kFalse;
	if (!Utils<IKCMCompareFacade>()->IsArmed())
		return kFalse;
	// ★Live when any of the rows it acts on holds its copy (several selected, or the one right-clicked) - Apply's "=" or,
	//   since 2026-10-24 night, the Story mode's Restore from Source's.
	std::vector<int32> rows, changes;
	RowsOfMenuThatCan([](int32 r, int32 c) { return Utils<IKCMStoryEditsFacade>()->CanRestoreChangeFromCopy(r, c) != kFalse; },
					  rows, changes);
	return rows.empty() ? kFalse : kTrue;
}

bool16 KCMChangeRowRestoreCopy()
{
	std::vector<int32> rows, changes;
	RowsOfMenuThatCan([](int32 r, int32 c) { return Utils<IKCMStoryEditsFacade>()->CanRestoreChangeFromCopy(r, c) != kFalse; },
					  rows, changes);
	if (rows.empty() || !Utils<IKCMCompareFacade>()->IsArmed())
	{
		KCMSetStatus("restore from copy: not a change put in with a copy kept.");
		return kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->RestoreChangesFromCopy(&rows[0], &changes[0],
																			   static_cast<int32>(rows.size()), why);
	SayApplied("restore from copy", done, why);
	return (done > 0) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// KCMStoryRowCanRestoreAll / KCMStoryRowRestoreAll - "Restore All Changes in This Story" on a STORY row of the Story mode
// (2026-10-24 night, design 1-8 - the author: "on the Story row too").
//----------------------------------------------------------------------------------------

bool16 KCMStoryRowCanRestoreAll()
{
	if (gMenuRow < 0 || gMenuChange >= 0)
		return kFalse;
	if (!Utils<IKCMCompareFacade>()->IsArmed())
		return kFalse;
	return Utils<IKCMStoryEditsFacade>()->CanRestoreStoryFromSource(gMenuRow);
}

bool16 KCMStoryRowRestoreAll()
{
	if (!KCMStoryRowCanRestoreAll())
	{
		KCMSetStatus("restore: not a story with a text change of two open documents.");
		return kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->RestoreStoryFromSource(gMenuRow, why);
	SayApplied("restore", done, why);
	return (done > 0) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// KCMChangeRowCanRestoreAttr / KCMChangeRowRestoreAttr
//   "Restore from Source" on an ATTRIBUTE change row (2026-09-24, stage 2 B - design section 14).
//----------------------------------------------------------------------------------------

bool16 KCMChangeRowCanRestoreAttr()
{
	if (!ChangeRowMenuLive())
		return kFalse;
	// ★ASKED OF THE ROW'S KIND AND THE TWO DOCUMENTS, not of their contents (design 14-2): whether the characters
	//   under the mark are the Source's is the action's to find out, and it says why when they are not.
	// ★★AND A TEXT ROW'S TOO SINCE 2026-10-24 NIGHT (design 1-8 - the author: "in Story mode, put the Source's text back
	//   with a copy of the Source"): live when the row right-clicked - or any of the rows selected with it - is a text
	//   change the facade can copy over (CanRestoreTextFromSource, which reads no document either).
	if (Utils<IKCMStoryEditsFacade>()->CanRestoreAttr(gMenuRow, gMenuChange))
		return kTrue;
	std::vector<int32> rows, changes;
	RowsOfMenuThatCan([](int32 r, int32 c) { return Utils<IKCMStoryEditsFacade>()->CanRestoreTextFromSource(r, c) != kFalse; },
					  rows, changes);
	return rows.empty() ? kFalse : kTrue;
}

bool16 KCMChangeRowRestoreAttr()
{
	// The same test the menu was greyed by, asked again at the moment of acting - the Source document may have
	// been closed while the menu was up (KCMStoryRefreshMenuRow's reason).
	if (!KCMChangeRowCanRestoreAttr())
	{
		KCMSetStatus("restore: not a change of two open documents that can be put back from the Source.");
		return kFalse;
	}
	// ★A TEXT ROW (2026-10-24 night, design 1-8): the Source's words by copy - every text row selected with it, as ONE undo
	//   step (RestoreTextsFromSource; one row alone is the single command). A mark row goes on below, as it always has.
	if (!Utils<IKCMStoryEditsFacade>()->CanRestoreAttr(gMenuRow, gMenuChange))
	{
		std::vector<int32> rows, changes;
		RowsOfMenuThatCan([](int32 r, int32 c) { return Utils<IKCMStoryEditsFacade>()->CanRestoreTextFromSource(r, c) != kFalse; },
						  rows, changes);
		PMString textWhy;
		const int32 restored = rows.empty() ? -1
			: Utils<IKCMStoryEditsFacade>()->RestoreTextsFromSource(&rows[0], &changes[0], static_cast<int32>(rows.size()),
																	textWhy);
		SayApplied("restore", restored, textWhy);
		return (restored > 0) ? kTrue : kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->RestoreAttr(gMenuRow, gMenuChange, why);
	PMString msg;
	msg.SetTranslatable(kFalse);
	if (done < 0)
	{
		msg.Append("restore: could not - ");
		msg.Append(why);
	}
	else if (done == 0)
	{
		msg.Append("restored from the Source - the mark is off there too; Ctrl+Z brings it back");
	}
	else
	{
		msg.Append("restored from the Source (");
		msg.AppendNumber(done);
		msg.Append(done == 1 ? " mark put on)" : " marks put on)");
		msg.Append(" - Ctrl+Z brings the change back");
	}
	if (done >= 0 && !why.IsEmpty())		// the "=" did not come: the model says what it found (2026-09-24 night)
	{
		msg.Append(" - ");
		msg.Append(why);
	}
	KCMSetStatus(msg);
	return (done >= 0) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// KCMChangeRowCanMatchTable / KCMChangeRowMatchTable
//   "Match the Source" on a TABLE row (2026-09-25 - design section 16).
//----------------------------------------------------------------------------------------

bool16 KCMChangeRowCanMatchTable()
{
	if (!ChangeRowMenuLive())
		return kFalse;
	return Utils<IKCMStoryEditsFacade>()->CanMatchTable(gMenuRow, gMenuChange);
}

bool16 KCMChangeRowMatchTable()
{
	// The same test the menu was greyed by, asked again at the moment of acting (KCMStoryRefreshMenuRow's reason).
	if (!KCMChangeRowCanMatchTable())
	{
		KCMSetStatus("match: not a table that differs between two open documents.");
		return kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->MatchTable(gMenuRow, gMenuChange, why);
	PMString msg;
	msg.SetTranslatable(kFalse);
	if (done < 0)
	{
		msg.Append("match: could not - ");
		msg.Append(why);
	}
	else
	{
		msg.Append("matched the Source's table (");
		msg.AppendNumber(done);
		msg.Append(done == 1 ? " move: " : " moves: ");
		msg.Append("rows, columns, merges and the cells they changed; the other cells were left alone) - Ctrl+Z brings the difference back");
	}
	KCMSetStatus(msg);
	return (done >= 0) ? kTrue : kFalse;
}

int32 KCMStoryMenuRow()
{
	return gMenuRow;
}

//----------------------------------------------------------------------------------------
// KCMStoryRowCanRefresh
//----------------------------------------------------------------------------------------

bool16 KCMStoryRowCanRefresh()
{
	if (gMenuRow < 0)
		return kFalse;

	// Nothing to compare against. ⚠Asked before the mode, because the mode is remembered across a
	// Stop - it is a setting, not part of the comparison (IKCMCompareFacade::SetCompareMode).
	if (!Utils<IKCMCompareFacade>()->IsArmed())
		return kFalse;

	// ★THE MODES THAT RUN A TEXT DIFF (user's call, 2026-08-21). The item refreshes one, and the
	//   pixel mode never runs it - a row there had no children by design, so "refreshing" it would
	//   have reported nothing found about a story whose words had not been looked at. (Since
	//   2026-10-11 the pixel mode has no story rows at all - its list is one note.)
	// ⚠★★**AND THE IMPORT MODE IS ONE OF THEM** (2026-09-15, measured on the application). This
	//   read `!= kKCMModeStory`, which greyed the item there - and the Import mode is exactly where
	//   it is needed most: a story edited after the import is REFUSED by the write (KCMStoryRestore
	//   compares the story's change counter), and this is the door out of that refusal. Greyed, the
	//   refusal became a dead end. **The same shape as the bug in StashedChange found an hour
	//   earlier**: a fourth mode added to a question that had only ever had two answers.
	if (!KCMModeUsesStoryRows(Utils<IKCMCompareFacade>()->GetCompareMode()))
		return kFalse;

	// The row has to still be there. The list is rebuilt whole by every comparison, and a right
	// click is followed by a menu the reader may leave open - so the index is only as good as the
	// list it was taken from.
	IKCMStoryEditsFacade::Row row;
	if (!Utils<IKCMStoryEditsFacade>()->GetRow(gMenuRow, row))
		return kFalse;

	// A story with no partner in the other document has nothing to be compared with - the same
	// judgement the model makes, read here so that the item is greyed rather than offered and then
	// refused (KCMStoryDiffRun::RunOne answers -1 for these).
	// ★kKCMStoryKindUnpaired covers ADDED and REMOVED alike (2026-08-21): a removed story is not
	//   in the target at all, so "compare it again" has nothing to point at either.
	if ((row.fKinds & kKCMStoryKindUnpaired) != 0)
		return kFalse;

	return kTrue;
}

//----------------------------------------------------------------------------------------
// KCMStoryRefreshMenuRow
//----------------------------------------------------------------------------------------

bool16 KCMStoryRefreshMenuRow()
{
	// The same test the menu was greyed by, asked again at the moment of acting. Between the two
	// the reader may have closed a document or stopped the comparison - a menu that is already up
	// is not re-tested by anybody else.
	if (!KCMStoryRowCanRefresh())
	{
		KCMSetStatus("story refresh: nothing to compare.");
		return kFalse;
	}

	const int32 count = Utils<IKCMStoryEditsFacade>()->RefreshRow(gMenuRow);

	// ★A TRACK LISTING'S RE-READ CANCELLED AT ITS BAR STOPPED THE LISTING (2026-10-27, the author's "Cancel -> Stop"), and
	//   the model has said so on the status line (KCMStopForCancelledReread) - "could not compare this story" would put a
	//   wrong reason over it. Nothing is armed after such a refresh, and nothing else ends one that way.
	if (count < 0 && !Utils<IKCMCompareFacade>()->IsArmed())
		return kFalse;

	// ★IT SAYS SO EVEN WHEN IT FOUND NOTHING, and that case is the one worth spelling out: a reader
	//   who has just finished repairing a story sees its children disappear, and "no differences
	//   now" is the difference between "it worked" and "did anything happen at all?".
	PMString msg;
	msg.SetTranslatable(kFalse);
	if (count < 0)
	{
		msg.Append("story refresh: could not compare this story.");
	}
	else if (count == 0)
	{
		msg.Append("story refreshed - no differences now");
	}
	else
	{
		msg.Append("story refreshed (");
		msg.AppendNumber(count);
		msg.Append(count == 1 ? " change)" : " changes)");
	}
	KCMSetStatus(msg);

	return (count >= 0);
}

//----------------------------------------------------------------------------------------
// ★ACCEPT AND REJECT IN THE TRACK MODE (2026-10-25 - the author: "a right click that takes the change back", "and confirms
// it", "on the Story too", "on the Story Edits heading, for the document"; design
// docs/superpowers/specs/2026-10-25-kcm-track-accept-reject-design.md). Each Can is the item's grey; each Do asks it again at
// the moment of acting (KCMStoryRefreshMenuRow's reason) and puts the model's sentence on the message line.
//----------------------------------------------------------------------------------------

namespace
{
/** A Track comparison is running - what every item of the four scopes asks first. */
bool16 TrackMenuLive()
{
	return (Utils<IKCMCompareFacade>()->IsArmed() && Utils<IKCMCompareFacade>()->GetCompareMode() == kKCMModeTrack)
		? kTrue : kFalse;
}

/** The status line's sentence for an accept or a reject: the model's words - the reason, when nothing was done. */
void SayResolved(bool16 accept, int32 done, const PMString& why)
{
	PMString msg;
	msg.SetTranslatable(kFalse);
	if (done < 0)
	{
		msg.Append(accept ? "accept" : "reject");
		msg.Append(": could not - ");
	}
	msg.Append(why);
	KCMSetStatus(msg);
}
}	// anonymous namespace

bool16 KCMChangeRowCanResolveTrack()
{
	if (gMenuRow < 0 || gMenuChange < 0 || !TrackMenuLive())
		return kFalse;
	std::vector<int32> rows, changes;
	RowsOfMenuThatCan([](int32 r, int32 c) { return Utils<IKCMStoryEditsFacade>()->CanResolveTrackChange(r, c) != kFalse; },
					  rows, changes);
	return rows.empty() ? kFalse : kTrue;
}

bool16 KCMChangeRowResolveTrack(bool16 accept)
{
	std::vector<int32> rows, changes;
	if (gMenuRow >= 0 && gMenuChange >= 0 && TrackMenuLive())
		RowsOfMenuThatCan([](int32 r, int32 c) { return Utils<IKCMStoryEditsFacade>()->CanResolveTrackChange(r, c) != kFalse; },
						  rows, changes);
	if (rows.empty())
	{
		KCMSetStatus(accept ? "accept: not a change of a running Track comparison." : "reject: not a change of a running Track comparison.");
		return kFalse;
	}
	// ★ONE UNDO STEP FOR THE ROWS SELECTED TOGETHER - as Apply These Changes (design 1-5 of 2026-10-24).
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->ResolveTrackChanges(&rows[0], &changes[0], static_cast<int32>(rows.size()),
																		 accept, why);
	SayResolved(accept, done, why);
	return (done > 0) ? kTrue : kFalse;
}

bool16 KCMStoryRowCanResolveTrack()
{
	if (gMenuRow < 0 || gMenuChange >= 0 || !TrackMenuLive())
		return kFalse;
	return (Utils<IKCMStoryEditsFacade>()->GetChangeCount(gMenuRow) > 0) ? kTrue : kFalse;
}

bool16 KCMStoryRowResolveTrack(bool16 accept)
{
	if (!KCMStoryRowCanResolveTrack())
	{
		KCMSetStatus(accept ? "accept: not a story row of a running Track comparison." : "reject: not a story row of a running Track comparison.");
		return kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->ResolveTrackRow(gMenuRow, accept, why);
	SayResolved(accept, done, why);
	return (done > 0) ? kTrue : kFalse;
}

bool16 KCMAuthorRowCanResolveTrack()
{
	if (gMenuAuthor < 0 || !TrackMenuLive())
		return kFalse;
	IKCMStoryEditsFacade::TrackAuthor author;
	return (Utils<IKCMStoryEditsFacade>()->GetTrackAuthor(gMenuAuthor, author) && author.fChangeCount > 0) ? kTrue : kFalse;
}

bool16 KCMAuthorRowResolveTrack(bool16 accept)
{
	if (!KCMAuthorRowCanResolveTrack())
	{
		KCMSetStatus(accept ? "accept: not an author of a running Track comparison." : "reject: not an author of a running Track comparison.");
		return kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->ResolveTrackAuthor(gMenuAuthor, accept, why);
	SayResolved(accept, done, why);
	return (done > 0) ? kTrue : kFalse;
}

bool16 KCMHeadingCanResolveTrack()
{
	// ★NO ROW ASKED: the heading's items act on every change the list holds (KCMHeadingCanApplyAll's reason).
	return (TrackMenuLive() && Utils<IKCMStoryEditsFacade>()->GetTrackChangeTotal() > 0) ? kTrue : kFalse;
}

bool16 KCMHeadingResolveTrack(bool16 accept)
{
	if (!KCMHeadingCanResolveTrack())
	{
		KCMSetStatus(accept ? "accept: no tracked change is listed." : "reject: no tracked change is listed.");
		return kFalse;
	}
	PMString why;
	const int32 done = Utils<IKCMStoryEditsFacade>()->ResolveTrackAll(accept, why);
	SayResolved(accept, done, why);
	return (done > 0) ? kTrue : kFalse;
}

// End, KCMStoryRefresh.cpp.
