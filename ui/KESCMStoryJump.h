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

/** Single click on a CHANGE row: go to that edit and light it up for a moment (2026-08-20).

	★★★A MARK, NOT A SELECTION (user's call, 2026-08-20: "その文字のところに移動＋マーカーを少しの
	時間出す感じに、マーカーはグローバルテキストアドーンメントで"). It selected the words until then,
	which cost three things a pointer does not: whatever the reader had selected was thrown away, the
	Type tool was forced on, and the words stayed selected long after they had been looked at.
	⇒ The selection is still there for those who want it - it is what a DOUBLE click does now
	(KESCMStorySelectChange).

	★The mark is a global text adornment (KESCMStoryMarker), so it is drawn ON the characters by the
	text engine: it follows vertical text, rotated frames and threaded columns without this file
	working out a single coordinate, and it changes no document data. It takes itself off the screen
	after about a second.

	★IT AIMS AT THE EDIT, NOT AT THE STORY. The row above it already goes to the story; this row
	exists because the reader wants the place inside it, so the frame it centres is the one the edit
	actually falls in - which in a threaded story is often not the first frame at all.

	★THE OLDER DOCUMENT'S WINDOW COMES TOO, aimed at the same story (KESCMGotoStoryFrame does that
	part, exactly as it does for a story row). ⚠It is aimed at the STORY over there, not at the
	matching character: the source-side TextIndex is known (the diff worked it out), but turning an
	index into a frame on the source side needs the same walk again, and the value of doing it is
	that the reader sees the old wording - which they do as soon as the story is on screen.

	★THE ACTIVE TOOL IS LEFT ALONE, and so is the reader's own selection. Both used to change here,
	because both are the price of making a text selection; a mark costs neither. (The tool DOES go on
	for the double click below - there the reader has asked for the text itself.)

	@param rowIndex which story row of KESCMStoryList.
	@param changeIndex which of that row's changes.
	@return kTrue when a view actually moved.
*/
bool16 KESCMStoryJumpToChange(int32 rowIndex, int32 changeIndex);

/** Double click on a CHANGE row: select the words that edit names, with the Type tool on
	(2026-08-20, user's call: "子供のところをダブルクリックで選択に").

	★THIS IS WHAT THE SINGLE CLICK USED TO DO. Moving it onto the double click is what let the
	single click become a pointer rather than an intervention - the reader who wants the text can
	still have it in one more click, and the reader who only wants to look is no longer handed a
	selection they have to undo.

	★NOTHING SCROLLS. The first click of the double click has already centred the frame
	(Selection::kDontScrollSelection), exactly as the whole-story double click does.

	★IT TAKES THE MARK DOWN FIRST. The single click that opened this double click put one up, and an
	inversion on top of the selection's own inversion leaves the text unreadable - KBS records the
	same trap in the same words.

	★THE TOOL CHANGES, deliberately: text selected under a tool that cannot act on it is not the
	invitation to edit that a double click is asking for. Same trade as
	KESCMStorySelectWholeStory, and it is written down in How to Use for the same reason.

	@param rowIndex which story row of KESCMStoryList.
	@param changeIndex which of that row's changes.
	@return kTrue when the words were selected.
*/
bool16 KESCMStorySelectChange(int32 rowIndex, int32 changeIndex);

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
