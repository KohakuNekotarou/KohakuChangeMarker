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

// Project includes:
#include "KCMUIID.h"
#include "IKCMCompareFacade.h"		// IsArmed / GetCompareMode - both of the conditions on the item
#include "IKCMStoryEditsFacade.h"		// the row itself, and RefreshRow - the work
#include "KCMStoryRefresh.h"
#include "KCMUIShared.h"				// KCMSetStatus - the panel's message line

namespace
{

/* Which row the right-click menu was popped over.

   ★A FILE STATIC, for the same reason KCMStoryRowEH's double-click flag is one: it belongs to
   "the menu that is up right now" rather than to any row widget - and row widgets are recycled as
   the list scrolls, so a member would be attached to the wrong story the moment the reader moved.
   One menu is up at a time.

   ★IT IS NOT CLEARED WHEN THE MENU CLOSES, and it does not need to be: every reader of it checks
   the row against the list as it stands now (KCMStoryRowCanRefresh), so a stale index from a
   comparison ago answers kFalse rather than acting on whatever row happens to sit at that number.
*/
int32 gMenuRow = -1;

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

	// ★THE STORY MODE ONLY (user's call, 2026-08-21). The item refreshes a TEXT DIFF, and the pixel
	//   mode never runs one - a row there has no children by design, so "refreshing" it would report
	//   nothing found about a story whose words have not been looked at.
	if (Utils<IKCMCompareFacade>()->GetCompareMode() != kKCMModeStory)
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

// End, KCMStoryRefresh.cpp.
