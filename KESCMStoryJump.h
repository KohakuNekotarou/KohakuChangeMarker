//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  What a click on a Story Edits row actually does.
//
//  The row's event handler decides WHICH kind of click arrived (KESCMStoryRowEH.cpp); this file
//  decides what that kind means. Both take a row index and look everything else up from it, so
//  that the handler never holds a UID the model may have replaced since the click began.
//
//========================================================================================

#ifndef __KESCMStoryJump_h__
#define __KESCMStoryJump_h__

#include "BaseType.h"	// bool16, int32

/** Single click: bring the story's first frame to the centre of the target document's window.

	The older version's window and the Pages panel follow along exactly as they do for Prev / Next
	(KESCMGotoStoryFrame). Writes the outcome to the panel's status line either way, so a click that
	cannot go anywhere still says so rather than appearing to do nothing.

	@param rowIndex which row of KESCMStoryList. Out of range - which includes the placeholder row
		shown when a comparison found no edits - is a quiet kFalse.
	@return kTrue when a view actually moved.
*/
bool16 KESCMStoryJumpToRow(int32 rowIndex);

/** Double click: put the caret at the start of that story, with the Type tool active.

	★This CHANGES THE USER'S ACTIVE TOOL - deliberately (user's call, 2026-08-10), because a caret
	the user cannot type into is not the invitation to edit that a double click is asking for. It is
	written down in How to Use for that reason. The single click above never changes the tool.

	The jump already ran on the first click of the double click, so nothing scrolls here
	(Selection::kDontScrollSelection): the frame is centred, and "somewhere on screen" would be a
	worse answer than the one already given.

	Stays silent about its refusals. Every one of them (comparison gone, story unplaced) has just
	been reported by the single click that preceded it, and saying it twice would only overwrite the
	message with itself.

	@return kTrue when the caret was placed.
*/
bool16 KESCMStoryPlaceCaret(int32 rowIndex);

#endif // __KESCMStoryJump_h__

// End, KESCMStoryJump.h.
