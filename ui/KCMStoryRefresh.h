//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  "Refresh Story Comparison" - the Story Edits rows' right-click menu.
//
//  A comparison is a photograph: it says what differed at the moment it ran. The reader then goes
//  into the newer document and starts repairing what it found, with the panel open beside them -
//  and every row goes on showing the state of things before they began. This is how one row is
//  brought up to date without re-running the comparison over the whole document (user's request,
//  "put a refresh-the-comparison item on the parent row's context menu, and let it update the
//  comparison for that story alone").
//
//  ★THE WORK ITSELF IS THE MODEL'S (IKCMStoryEditsFacade::RefreshRow). What lives here is
//  everything the MENU needs and the model has no business knowing: which row the cursor was over
//  when the menu was popped, whether the item may be offered at all, and what the panel says
//  afterwards.
//
//  ★WHY A FILE OF ITS OWN rather than a few functions added to KCMStoryJump. That file answers
//  "what does a CLICK on a row mean", and every function in it moves a window or a selection. This
//  is not a click and moves neither: it re-reads the documents and rebuilds part of the list. The
//  two would sit oddly together, and the split matches how the chapter rows are laid out (the row
//  handler in KCMBookRowEH.cpp, what the item does in KCMBookOpen.cpp).
//
//========================================================================================

#ifndef __KCMStoryRefresh_h__
#define __KCMStoryRefresh_h__

#include "BaseType.h"	// bool16, int32

/** Remember which row of the list the right-click menu is about.

	Called by KCMStoryRowEH::RButtonDn as the menu is popped, and read back by the actions on that
	menu and by their enabling tests. ★TOP-LEVEL ROWS ONLY - a right click on a child row never
	reaches here. ⚠A child row had a menu of its own from 2026-09-12, with a stash of its own
	(KCMStoryCopy), and ⛔**both went on 2026-09-21** when the last item on it did. A right click on
	a child row raises nothing again, as it did before 2026-09-12 (user's call, 2026-08-21; the
	reasoning is at KCMStoryRowEH::RButtonDn).

	⚠★★**WHICH LIST THE INDEX BELONGS TO DEPENDS ON THE MODE** (2026-09-09). The list is shared:
	  in the Story mode this is a story of IKCMStoryEditsFacade, and in the Resources mode it is a
	  definition of IKCMResourcesFacade. **The number alone does not say which**, so every reader
	  has to ask the mode first - which each of them does, because each is enabled in one mode only.

	@param rowIndex the row, or -1 for "no row" (which greys every item on the menu).
*/
void KCMStorySetMenuRow(int32 rowIndex);

/** The row the right-click menu was popped over, or -1.

	★Added 2026-09-09 so that the Resources mode's own menu item can find its definition. The
	stash was private to the Story refresh until then, and the alternative - a second stash for the
	second mode - would be the same fact recorded twice, which is how the two would drift apart.
	⚠Read it with the mode in hand; see the warning above.
*/
int32 KCMStoryMenuRow();

/** Whether "Refresh Story Comparison" may be offered for the stashed row.

	Answers kFalse in every case where the item would do nothing or would lie:
	  - no row was stashed, or the list has been rebuilt shorter since;
	  - no comparison is armed (there is nothing to compare against);
	  - the panel is in the PIXEL mode - the whole point of the item is the text diff, which that
	    mode does not run, so refreshing there would report "no differences" about a story that has
	    never been looked at that way (user's call: "only in the story mode");
	  - the row is an ADDED story, which has no partner in the older document at all.

	⚠**IT WAS THE ONLY ITEM ON THIS MENU UNTIL 2026-09-15**, when "Restore All in This Story" (and
	  its Import-mode twin) joined it. Greyed still means the menu does not appear, but only when
	  EVERY item on it is greyed - which is what the Pixel and Resources modes do, since the bulk
	  pair is confined to the same two modes as this one. So "the Story mode only" still holds; it
	  is no longer this item alone that delivers it. The chapter rows' menu is the surviving
	  one-item example, measured (KCMUI.fr, kKCMBookRowStartActionID).
*/
bool16 KCMStoryRowCanRefresh();

/** Compare the stashed row's story again and replace what hangs under it.

	Reports the outcome on the panel's message line either way - including "nothing differs now",
	which is the answer a reader who has just finished repairing a story is hoping for and would
	otherwise see as an empty row and no explanation.

	@return kTrue when the row was refreshed (0 differences included), kFalse when it could not be.
*/
bool16 KCMStoryRefreshMenuRow();

// ---- "Reject This Import Change" on a CHANGE row (2026-09-24, stage 2 A) ------------------------------
// ★The change row's menu is back (it went on 2026-09-21 with its last item), carrying ONE item about the
//  change under the cursor - the rule of 2026-08-21 (a reader pointing at one difference is not handed an
//  action over the whole story) holds.

/** The row AND the change the menu was popped over (KCMStoryRowEH::RButtonDn on a child row). */
void KCMStorySetMenuChange(int32 rowIndex, int32 changeIndex);

/** kTrue when the menu was popped over a change row, with its row and change. */
bool16 KCMStoryGetMenuChange(int32& outRow, int32& outChange);

/** Whether "Reject This Import Change" may be offered: a comparison armed, a mode with story rows, and at
	least one of the import's tracked changes touching the change's range (asked of the story, design 13-1
	item 3). */
bool16 KCMChangeRowCanReject();

/** Rejects them (one undo step) and says how many on the panel's message line. */
bool16 KCMChangeRowReject();

#endif // __KCMStoryRefresh_h__

// End, KCMStoryRefresh.h.
