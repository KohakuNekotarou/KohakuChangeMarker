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
	reaches here. ⚠Since 2026-09-12 a child row HAS a menu of its own ("Copy Source Text"), with a
	stash of its own (KCMStorySetMenuChange in KCMStoryCopy.h); until then it raised none at all
	(user's call, 2026-08-21; the reasoning is at that function).

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

	⚠Being the only item in its menu, greyed means the MENU DOES NOT APPEAR - which is what makes
	  this the whole of "the Story mode only". The chapter rows' menu behaves the same way and was
	  measured doing so (KCMUI.fr, kKCMBookRowStartActionID).
*/
bool16 KCMStoryRowCanRefresh();

/** Compare the stashed row's story again and replace what hangs under it.

	Reports the outcome on the panel's message line either way - including "nothing differs now",
	which is the answer a reader who has just finished repairing a story is hoping for and would
	otherwise see as an empty row and no explanation.

	@return kTrue when the row was refreshed (0 differences included), kFalse when it could not be.
*/
bool16 KCMStoryRefreshMenuRow();

#endif // __KCMStoryRefresh_h__

// End, KCMStoryRefresh.h.
