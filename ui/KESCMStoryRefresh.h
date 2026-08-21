//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  "Refresh Story Comparison" - the Story Edits rows' right-click menu.
//
//  A comparison is a photograph: it says what differed at the moment it ran. The reader then goes
//  into the newer document and starts repairing what it found, with the panel open beside them -
//  and every row goes on showing the state of things before they began. This is how one row is
//  brought up to date without re-running the comparison over the whole document (user's request,
//  2026-08-21: "親の部分の右クリックに比較を更新を作る、それを使うとそのストーリーだけ比較を更新
//  したい").
//
//  ★THE WORK ITSELF IS THE MODEL'S (IKESCMStoryEditsFacade::RefreshRow). What lives here is
//  everything the MENU needs and the model has no business knowing: which row the cursor was over
//  when the menu was popped, whether the item may be offered at all, and what the panel says
//  afterwards.
//
//  ★WHY A FILE OF ITS OWN rather than a few functions added to KESCMStoryJump. That file answers
//  "what does a CLICK on a row mean", and every function in it moves a window or a selection. This
//  is not a click and moves neither: it re-reads the documents and rebuilds part of the list. The
//  two would sit oddly together, and the split matches how the chapter rows are laid out (the row
//  handler in KESCMBookRowEH.cpp, what the item does in KESCMBookOpen.cpp).
//
//========================================================================================

#ifndef __KESCMStoryRefresh_h__
#define __KESCMStoryRefresh_h__

#include "BaseType.h"	// bool16, int32

/** Remember which Story Edits row the right-click menu is about.

	Called by KESCMStoryRowEH::RButtonDn as the menu is popped, and read back by the action and by
	its enabling test. ★STORY ROWS ONLY - a right click on a change row raises no menu at all and
	never reaches here (user's call, 2026-08-21; the reasoning is at that function).

	@param rowIndex the story row, or -1 for "no row" (which greys the item).
*/
void KESCMStorySetMenuRow(int32 rowIndex);

/** Which row the last right-click menu was popped over, or -1. */
int32 KESCMStoryMenuRow();

/** Whether "Refresh Story Comparison" may be offered for the stashed row.

	Answers kFalse in every case where the item would do nothing or would lie:
	  - no row was stashed, or the list has been rebuilt shorter since;
	  - no comparison is armed (there is nothing to compare against);
	  - the panel is in the PIXEL mode - the whole point of the item is the text diff, which that
	    mode does not run, so refreshing there would report "no differences" about a story that has
	    never been looked at that way (user's call, 2026-08-21: "ストーリーモードでのみで");
	  - the row is an ADDED story, which has no partner in the older document at all.

	⚠Being the only item in its menu, greyed means the MENU DOES NOT APPEAR - which is what makes
	  this the whole of "the Story mode only". The chapter rows' menu behaves the same way and was
	  measured doing so (KCMUI.fr, kKESCMBookRowStartActionID).
*/
bool16 KESCMStoryRowCanRefresh();

/** Compare the stashed row's story again and replace what hangs under it.

	Reports the outcome on the panel's message line either way - including "nothing differs now",
	which is the answer a reader who has just finished repairing a story is hoping for and would
	otherwise see as an empty row and no explanation.

	@return kTrue when the row was refreshed (0 differences included), kFalse when it could not be.
*/
bool16 KESCMStoryRefreshMenuRow();

#endif // __KESCMStoryRefresh_h__

// End, KESCMStoryRefresh.h.
