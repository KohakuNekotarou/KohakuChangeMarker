//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  THE STORY EDITS LIST'S SELECTION AND KEYBOARD, AS KFC'S RESULT LIST HAS THEM (2026-10-24 night -
//  docs/superpowers/specs/2026-10-24-kcm-list-like-kfc-design.md 1-3 / 1-4; the author: "like KFC, change with Return,
//  and change several rows selected together"). CHANGE ROWS ARE SELECTED TOGETHER - of any story; a story row is
//  selected alone, and so is every row of the Resources mode's list (KCMStoryTreeController.cpp says how). The page
//  shows the row selected last. What the rows selected together are for: one right-click item, or one Return, for all
//  of them, in ONE undo step (IKCMStoryEditsFacade's ApplyChanges / RestoreChangesFromCopy / RestoreTextsFromSource).
//  Written from KFC's KFCResultTreeController.cpp and KFCResultTree::TakeKeyboard.
//
//========================================================================================

#ifndef __KCMStoryTreeController_h__
#define __KCMStoryTreeController_h__

#include <utility>
#include <vector>

#include "NodeID.h"

class ITreeViewController;

/** One change row: (row, change), the indices IKCMStoryEditsFacade takes. */
typedef std::pair<int32, int32> KCMStoryChangeRow;

/** The CHANGE rows selected, in the order the list draws them. kFalse, `out` empty, when nothing is selected or a row
	that is not a change row is (a story row, the Resources mode's list). */
bool16 KCMStoryGetSelectedChangeRows(std::vector<KCMStoryChangeRow>& out);

/** The change rows a right click on change (row, change) acts on (KFC's RowsOfRightClick): every change row selected,
	when it is one of several selected together; else that one row. */
void KCMStoryRowsOfRightClick(int32 row, int32 change, std::vector<KCMStoryChangeRow>& out);

/** A Shift / Ctrl click's button-up (KCMStoryRowEH): the row it ADDED to the selection is landed on, as a plain click
	lands (the page shows the row selected last); a row taken away leaves the page as it is. A story row so clicked is
	selected alone (the controller's rule) and not jumped to. */
void KCMStoryFollowModifiedClick(const NodeID& clicked);

/** Shift+Up / Shift+Down (KCMStoryTreeEH): the run of change rows grown or shrunk by one row - its anchor where the run
	began, its moving end where the last click or Shift+arrow left it - and the page following its end. kFalse when the
	selection is not a run of change rows (a story row selected): the key is then the stock one's. */
bool16 KCMStoryExtendSelection(ITreeViewController* controller, bool16 down);

/** `row` selected ALONE, every row above it open, brought into view - the official re-selection (a Select alone does
	not move a selection: it adds to it in this list). Its landing is the caller's. */
void KCMStorySelectAloneInView(ITreeViewController* controller, const NodeID& row);

/** The change rows `rows` selected again - after a write the panel rebuilt the list on the model's notice, and a rebuilt
	list has nothing selected (measured 2026-10-24 night: the Down after a Return started from nowhere). Their indices
	still name them: a "=" record keeps its change's place. The last is brought into view and is the run's moving end.
	No landing - the page shows the write already. */
void KCMStoryReselectRows(ITreeViewController* controller, const std::vector<KCMStoryChangeRow>& rows);

/** THE KEYBOARD TO THE LIST, THROUGH THE PANEL'S DOORS (KFC's TakeKeyboard): the panel made the active one with the key
	focus given to it (IPanelMgr::ShowPanelByWidgetID), the list given it by its parent panel
	(IPanelControlData::SetKeyboardFocus), then IKeyBoard::AcquireKeyFocus if those did not land it there. A closed
	panel is not opened for this. kTrue when the list holds InDesign's keyboard afterwards. */
bool16 KCMStoryTakeKeyboard();

/** Does the list hold the keyboard - InDesign's holder (IKeyBoard::GetKeyFocus) AND the window the system sends the
	keys to (KCMPanelHasSystemKeyboard)? A click on another window's title bar moves the second without the first
	(memory ikeyboard-holder-vs-system-keyboard), and a Return then is that window's. */
bool16 KCMStoryListHoldsKeyboard();

/** The controlled shutdown (KCMUIStartup::Shutdown): the Return filter off the application's event dispatcher and let go,
	and never pushed again - a handler left on the dispatcher when the .pln unloads would be called into nothing.
	Defined in KCMStoryTreeEH.cpp, beside the filter. */
void KCMStoryShutdownReturnFilter();

#endif // __KCMStoryTreeController_h__

// End, KCMStoryTreeController.h.
