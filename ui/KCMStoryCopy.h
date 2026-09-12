//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  "Copy Source Text" - the right-click menu of a CHANGE row (a child row of the Story Edits
//  list). What arrives is words only - no formatting, none of InDesign's marker characters.
//  (The name read "Copy Source Text as Plain Text" for an hour on 2026-09-12, at the user's
//  request, and the user took the suffix out again the same night.)
//
//  The user's ask (2026-09-12): "I want the SOURCE side's text of a change on the clipboard, as
//  plain text - the words a replacement replaced, the words a deletion removed - and for a ruby
//  change or a removed ruby, the READING itself, not the characters it sits on." One item, one
//  side. The newer side is the document in front of the reader; the older side is what the panel
//  alone can still show, which is why that is the one worth a menu.
//
//  ★THIS REVERSES A DECISION OF 2026-08-21. Until today a right click on a child row raised no
//  menu at all (user's call at the time), because the only menu there was acted on the WHOLE
//  STORY, and a reader pointing at one difference would have been handed an action over
//  something else. That reason does not apply to a menu of the child row's OWN: this subtree
//  (kKCMChangeRowMenuName) carries nothing but this item, and the story row's menu
//  (kKCMStoryRowMenuName) is not offered on a child row any more than it was.
//
//  ★THE CLIPBOARD IS WRITTEN THE WAY THE PRODUCT WRITES IT (LinksUIPanelMenuComponent::
//  CopyStringToScrap, source/open/components/linksui/LinksUIPanelMenuComponent.cpp:1707): the
//  session's IClipboardController, its text handler's ITextScrapData, and ITextModelCmds::InsertCmd
//  into the scrap story. No Win32, so it is the same code on both platforms, and what other
//  applications receive is the plain text InDesign itself externalises for a copied text
//  selection.
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

/** Whether "Copy Source Text" may be offered for the stashed change.

	Answers kFalse in every case where there is nothing on the older side to copy:
	  - no change was stashed, or the list has been rebuilt since and the indexes no longer name one;
	  - the panel is not in the STORY mode (the Resources list's child rows are not text changes);
	  - an INSERTION - nothing stood on the older side;
	  - a ruby ADDED - no older reading;
	  - a kenten, footnote or endnote row - what those carry is a kind, not text a reader would paste.

	⚠Being the only item in its menu, greyed means the MENU DOES NOT APPEAR (InDesign's behaviour,
	  measured on the chapter rows' menu), which is the intended answer for the rows above.
*/
bool16 KCMChangeRowCanCopySource();

/** Put the stashed change's older-side text on the clipboard as plain text.

	A replacement: the words that were replaced. A deletion: the words that were removed. A ruby
	change or removal: the older READING, not the base characters. Context on either side is left
	out - the row's fTextPre / fTextPost exist to place the words for a reader, and are not part of
	the change. InDesign's own marker characters (anchors, table and footnote references, the
	page-number and variable stand-ins) are dropped, so what arrives is text and nothing else.

	Reports the outcome on the panel's message line.

	@return kTrue when something was copied, kFalse when the item should not have been live.
*/
bool16 KCMChangeRowCopySource();

#endif // __KCMStoryCopy_h__

// End, KCMStoryCopy.h.
