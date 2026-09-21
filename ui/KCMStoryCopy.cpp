//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryCopy.h for what this is for. One answer lives here now: which change the menu was
//  popped over. (It held three until 2026-09-21 - the other two belonged to the restore.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// (The clipboard's seven includes went with "Copy Source Text" on 2026-09-15, and the restore's
//  six more went on 2026-09-21 - the header says what each recipe was, so that removing the code
//  did not take the finding with it.)

// Project includes:
#include "KCMStoryCopy.h"

namespace
{

/* Which change the change-row menu was popped over.

   ★FILE STATICS, for the reason KCMStoryRefresh gives for its row: they belong to "the menu that
   is up right now", not to any row widget, and row widgets are recycled as the list scrolls. One
   menu is up at a time.

   ★NOT CLEARED WHEN THE MENU CLOSES, and not needing to be: the reader of the stash asks the model
   for that row afresh (the Resources mode's "Edit..." looks the attribute up), so indexes from a
   comparison ago answer "no such row" rather than naming whatever sits at those numbers now.
*/
int32 gMenuRow = -1;
int32 gMenuChange = -1;

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMStorySetMenuChange
//----------------------------------------------------------------------------------------

void KCMStorySetMenuChange(int32 rowIndex, int32 changeIndex)
{
	gMenuRow = rowIndex;
	gMenuChange = changeIndex;
}

//----------------------------------------------------------------------------------------
// KCMStoryGetMenuChange
//----------------------------------------------------------------------------------------

bool16 KCMStoryGetMenuChange(int32& outRow, int32& outChange)
{
	outRow = gMenuRow;
	outChange = gMenuChange;
	return (gMenuRow >= 0 && gMenuChange >= 0) ? kTrue : kFalse;
}

// (⛔Six functions stood here and went on 2026-09-21 with the restore itself: the change row's
//  "Restore Source Text" and "Undo the Restore", the story row's "Restore All in This Story", and
//  the three tests that decided when each was offered. They asked the model's CanWriteToTarget and
//  then called the facade; **nothing in KCM writes the reader's text from a menu any more.** What
//  is left in this file is the stash alone, read by the Resources mode's "Edit...".)

// End, KCMStoryCopy.cpp.
