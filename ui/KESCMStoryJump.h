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

	★THE OLDER VERSION'S WINDOW COMES TOO, AIMED AT THE SAME STORY - not at the same page number.
	The two versions can hold one story in different places (user's observation, 2026-08-10), so the
	source window is pointed at whatever frame that story starts in over there. Prev / Next matches
	by page, because a page is what it is stepping through; this matches by story, because a story is
	what this row is (KESCMGotoStoryFrame). The Pages panel follows on both sides.

	Writes the outcome to the panel's status line either way, so a click that cannot go anywhere
	still says so rather than appearing to do nothing.

	@param rowIndex which row of KESCMStoryList. Out of range - which includes the placeholder row
		shown when a comparison found no edits - is a quiet kFalse.
	@return kTrue when a view actually moved.
*/
bool16 KESCMStoryJumpToRow(int32 rowIndex);

/** Single click on a CHANGE row: go to that edit and select the words it names (2026-08-20).

	★IT AIMS AT THE EDIT, NOT AT THE STORY. The row above it already goes to the story; this row
	exists because the reader wants the place inside it, so the frame it centres is the one the edit
	actually falls in - which in a threaded story is often not the first frame at all.

	★THE OLDER DOCUMENT'S WINDOW COMES TOO, aimed at the same story (KESCMGotoStoryFrame does that
	part, exactly as it does for a story row). ⚠It is aimed at the STORY over there, not at the
	matching character: the source-side TextIndex is known (the diff worked it out), but turning an
	index into a frame on the source side needs the same walk again, and the value of doing it is
	that the reader sees the old wording - which they do as soon as the story is on screen.

	★THE TYPE TOOL GOES ON, which a single click on a STORY row deliberately does not do. Selecting
	text under a tool that cannot act on it is not a selection the reader can use, and the whole
	point of a change row is "here is the bit that changed" - an invitation to look at it and edit
	it. The double click's note in KESCMStorySelectWholeStory reasons the same way about the same
	trade.

	@param rowIndex which story row of KESCMStoryList.
	@param changeIndex which of that row's changes.
	@return kTrue when a view actually moved.
*/
bool16 KESCMStoryJumpToChange(int32 rowIndex, int32 changeIndex);

/** Double click: select the whole of that story, with the Type tool active.

	★SELECTS EVERYTHING, rather than placing a caret at the start (user's call, 2026-08-10 - it put
	the caret there until then). The row is the report that a story changed, and what the reader
	usually wants next is the story itself: to copy it, to restyle it, to replace it. A selection is
	all of those; a caret is only the last one, and a caret is one arrow key away from a selection
	anyway (Ctrl+A), while a selection is not one keystroke away from a caret.

	★This CHANGES THE USER'S ACTIVE TOOL - deliberately (user's call, 2026-08-10), because text
	selected under a tool that cannot act on text is not the invitation to edit that a double click is
	asking for. It is written down in How to Use for that reason. The single click above never
	changes the tool.

	The jump already ran on the first click of the double click, so nothing scrolls here
	(Selection::kDontScrollSelection): the frame is centred, and "somewhere on screen" would be a
	worse answer than the one already given. ⚠A long story therefore ends up selected past the bottom
	of the window - that is what "the whole story" means, and the first click has already put the
	beginning of it in view.

	Stays silent about its refusals. Every one of them (comparison gone, story unplaced) has just
	been reported by the single click that preceded it, and saying it twice would only overwrite the
	message with itself.

	@return kTrue when the text was selected.
*/
bool16 KESCMStorySelectWholeStory(int32 rowIndex);

#endif // __KESCMStoryJump_h__

// End, KESCMStoryJump.h.
