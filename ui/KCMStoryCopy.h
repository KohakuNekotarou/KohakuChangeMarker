//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The right-click menu of a CHANGE row (a child row of the Story Edits list): which change the
//  menu was popped over, and the items that act on that one change - "Restore Source Text" and
//  "Undo the Restore". (⛔Each wore a second name in the fourth mode, which went on 2026-09-20.)
//
//  ★THIS SUBTREE REVERSED A DECISION OF 2026-08-21. Until 2026-09-12 a right click on a child row
//  raised no menu at all (the user's call at the time), because the only menu there acted on the
//  WHOLE STORY, and a reader pointing at one difference would have been handed an action over
//  something else. That reason does not apply to a menu of the child row's OWN: this subtree
//  (kKCMChangeRowMenuName) carries nothing but items about the change, and the story row's menu
//  (kKCMStoryRowMenuName) is not offered on a child row any more than it was.
//
//  ⛔**"Copy Source Text" WAS THE FIRST ITEM HERE AND IS GONE** (2026-09-15, the user's request:
//  "メニューにCopyがまだでているので、このきのうはなくす"). It put the older side's words on the
//  clipboard as plain text. ★The file keeps its name: renaming it would touch both vcxproj copies
//  (the one the build reads is outside the repo) and every include, for nothing. Its ActionID is a
//  dead slot - KCMUIID.h says why it is not reused.
//  ⚠**What went with it**: the clipboard recipe copied from the product itself
//  (LinksUIPanelMenuComponent::CopyStringToScrap - the session's IClipboardController, its text
//  handler's ITextScrapData, ITextModelCmds::InsertCmd into the scrap story; no Win32, so the same
//  code on both platforms). Nothing in KCM writes the clipboard any more, so that is recorded here
//  rather than left only in a deleted function.
//
//  ★WHY A FILE OF ITS OWN, beside KCMStoryRefresh: that file is the STORY row's menu (which row,
//  may it be offered, what it does) and this is the CHANGE row's. They stash different things - a
//  row there, a row AND a change here - and each item's enabling reads only its own stash.
//
//========================================================================================

#ifndef __KCMStoryCopy_h__
#define __KCMStoryCopy_h__

#include "BaseType.h"	// bool16, int32

/** Remember which change of which row the change-row menu is about.

	Called by KCMStoryRowEH::RButtonDn as the menu is popped over a CHILD row, and read back by the
	item on that menu and by its enabling test. The same construction as KCMStorySetMenuRow, kept
	apart from it because the two menus are never up at once and neither should read the other's
	row: a story-row action must not act on a change, nor this on a story.

	@param rowIndex the story row the change hangs under, or -1 for "no row".
	@param changeIndex which change under that row, or -1 for "none" (either -1 greys the item).
*/
void KCMStorySetMenuChange(int32 rowIndex, int32 changeIndex);

/** Read that stash back.

	★Exposed on 2026-09-13 for the Resources mode's "Edit..." item, which hangs on the same
	child-row menu and has to know which attribute the menu was popped over. Until then the stash
	was read only inside this file, by the copy and restore items.
	⚠**It says nothing about which MODE the list is in** - the same two numbers name a Story change
	  or a Resources attribute depending on that, and the caller must ask
	  (KCMListShowsResources) before reading them as either.

	@param outRow [out] the row, or -1.
	@param outChange [out] the change / attribute under it, or -1.
	@return kTrue when both are >= 0.
*/
bool16 KCMStoryGetMenuChange(int32& outRow, int32& outChange);

/** Whether "Restore Source Text" may be enabled for the stashed change: the Story mode, and any
    change - words, ruby or kenten (since the evening of 2026-09-13). An insertion counts -
    restoring it takes the inserted words out again. What cannot be written back is refused by
    the model with a reason, not greyed here. */
bool16 KCMChangeRowCanRestore();

/** Runs "Restore Source Text" on the stashed change through the facade (KCMStoryRestore.h on
    the model side) and puts its message on the status line. kTrue when something was written. */
bool16 KCMChangeRowRestore();

// ---- putting one change back (2026-09-16) ---------------------------------------------------
// ★**THE OPPOSITE OF THE ITEM ABOVE.**
// ⚠**NOT Edit > Undo.** Ctrl+Z reaches only the last thing done; this reaches the change the
//   reader points at, whatever they have done since, and is itself one undo step.
// (⛔Both were PAIRS until 2026-09-20: the fourth mode called them "Change to Imported Text" and
//  "Change Back to the Original". The mode is gone and one name is left for each.)

/** Whether "Undo the Restore" may be offered: the Story mode, and a change that is STANDING as
    taken in. ⚠Asks the model's fReplaced, which is the document's own answer - so a change the
    reader has already put back with Ctrl+Z greys the item rather than offering a second way to
    do what is done. */
bool16 KCMChangeRowCanUndoRestore();

/** Runs it through the facade and puts the model's message on the status line. */
bool16 KCMChangeRowUndoRestore();

#endif // __KCMStoryCopy_h__

// End, KCMStoryCopy.h.
