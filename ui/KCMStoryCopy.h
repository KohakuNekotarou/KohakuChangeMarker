//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The right-click menu of a CHANGE row (a child row of the Story Edits list): WHICH CHANGE the
//  menu was popped over.
//
//  ⛔**THE ITEMS THAT ACTED ON THAT CHANGE ARE GONE** (2026-09-21). "Restore Source Text" and "Undo
//  the Restore" went with the whole restore, on the user's word - "the Source document is in front
//  of you, so if you want it back, take it from there" - as "Copy Source Text" had gone before them.
//  ⇒ **In the Story mode this subtree is empty and raises no menu at all**, which is what a child
//  row did before 2026-09-12. What is left here is the stash, read by the Resources mode's "Edit...".
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

// (⛔Six declarations stood here: KCMChangeRowCanRestore / KCMChangeRowRestore, the "Undo the
//  Restore" pair (2026-09-16), and the one-story pair (2026-09-15, gone 2026-09-20, back and gone
//  again 2026-09-21). All six went with the restore on 2026-09-21. ⚠**The row's own menu file,
//  KCMStoryRefresh, kept its items** - Refresh Story Comparison reads, it does not write.)

#endif // __KCMStoryCopy_h__

// End, KCMStoryCopy.h.
