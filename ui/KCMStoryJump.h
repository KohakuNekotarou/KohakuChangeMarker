//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What a click on a Story Edits row actually does.
//
//  The row's event handler decides WHICH kind of click arrived (KCMStoryRowEH.cpp); this file
//  decides what that kind means. Both take a row index and look everything else up from it, so
//  that the handler never holds a UID the model may have replaced since the click began.
//
//========================================================================================

#ifndef __KCMStoryJump_h__
#define __KCMStoryJump_h__

#include "BaseType.h"	// bool16, int32

class IDataBase;

/** Single click: bring the story's first frame to the centre of the target document's window.

	★THE OLDER VERSION'S WINDOW COMES TOO, AIMED AT THE SAME STORY - not at the same page number.
	The two versions can hold one story in different places (user's observation, 2026-08-10), so the
	source window is pointed at whatever frame that story starts in over there. Prev / Next matches
	by page, because a page is what it is stepping through; this matches by story, because a story is
	what this row is (KCMGotoStoryFrame). The Pages panel follows on both sides.

	Writes the outcome to the panel's status line either way, so a click that cannot go anywhere
	still says so rather than appearing to do nothing.

	@param rowIndex which row of KCMStoryList. Out of range - which includes the placeholder row
		shown when a comparison found no edits - is a quiet kFalse.
	@return kTrue when a view actually moved.
*/
bool16 KCMStoryJumpToRow(int32 rowIndex);

/** Single click on a CHANGE row: go to that edit and SELECT its words in the Target (since 2026-10-24 night).

	★★★A SELECTION AGAIN, THE WAY KFC MAKES ONE (docs/superpowers/specs/2026-10-24-kcm-list-like-kfc-design.md 1-1 /
	1-2; the author: "in KFC, selecting a row selects the text too, and that is pleasant to use"). The Story mode and an
	import's copy - and the Track mode since 2026-10-25 (the author: "the Track Change mode too"). KFC's landing rules:
	locked - not selected, the flash points; hidden (its layer or itself) - selected, said; on a hidden spread - neither,
	said; overset - selected at the "+", said; an empty place - the caret (in the Track mode a deletion's words and words
	under a hidden condition are kept out of the layout, so their caret is said too). The Target is made the active
	document, and the Type tool goes on (IsToolOfType). The Source window moves and flashes as before (in the Track
	mode in the author's colour, where the change's place in the copy is known).
	(⛔2026-08-20 to 2026-10-24: A MARK, NOT A SELECTION - the user's call then: "move to those characters and show a
	marker for a short while". It cost the three things a selection costs - the reader's selection thrown away, the
	Type tool forced on, the words left selected - which the KFC list showed to be the pointer worth having. The
	paragraph below on the active tool was the mark's.)

	★The mark is a global text adornment (KCMStoryMarker), so it is drawn ON the characters by the
	text engine: it follows vertical text, rotated frames and threaded columns without this file
	working out a single coordinate, and it changes no document data. It takes itself off the screen
	after about a second.

	★IT AIMS AT THE EDIT, NOT AT THE STORY. The row above it already goes to the story; this row
	exists because the reader wants the place inside it, so the frame it centres is the one the edit
	actually falls in - which in a threaded story is often not the first frame at all.

	★THE OLDER DOCUMENT'S WINDOW COMES TOO, aimed at the same story (KCMGotoStoryFrame does that
	part, exactly as it does for a story row).
	★★AND SINCE 2026-08-22 IT REACHES THE MATCHING CHARACTER, not just the story. The source-side
	TextIndex is Change::fSourceStart - a DIFFERENT number from this side's, because the same edit
	sits in a different place in each version. ⚠This note used to say the older side stopped at the
	story because "turning an index into a frame on the source side needs the same walk again" - and
	that walk is exactly what was missing when the point was added: the older window was given the
	story's FIRST frame to choose a spread by, so an edit further down a threaded story scrolled it
	to a point belonging to another spread (bug recheck 2026-08-22). Both sides now ask
	IKCMStoryEditsFacade::GetStoryFrameAt.
	★AN INSERTION AIMS THE OLDER WINDOW AT ITS PLACE (2026-08-22). It used to be left where it was,
	on the grounds that there is nothing over there to centre; the CHARACTERS are indeed missing but
	the SPOT is not, and the spot - the gap the new words went into - is what the reader is looking
	for. Change::fSourceStart carries it, with fSourceEnd equal to it (an empty range).

	(⛔Until 2026-10-24 night: THE ACTIVE TOOL WAS LEFT ALONE, and so was the reader's own selection - a mark costs
	neither. The Track mode kept that until 2026-10-25.)

	@param rowIndex which story row of KCMStoryList.
	@param changeIndex which of that row's changes.
	@return kTrue when a view actually moved.
*/
bool16 KCMStoryJumpToChange(int32 rowIndex, int32 changeIndex);

// (⛔KCMStorySelectChange and KCMStorySelectWholeStory were declared here until 2026-10-25 - what a DOUBLE click did:
//  on a change row, the change's words selected on both sides; on a story row, the whole story (the Source alone, in
//  front, for a Deleted row); and the keyboard handed back to the document. The double click went that day, on the
//  author's word ("remove the double click's behaviour" - "the way KFC does it", whose result list tells no double
//  click apart): since 2026-10-24 night one click selects the change's words (KCMStoryJumpToChange), so the second
//  click had little left to add. A double click is two clicks now (KCMStoryRowEH.cpp).)

// (⛔KCMActivateDocument was declared here from 2026-09-13 to 2026-09-21 - "make this document the
//  active one" - and it existed for one caller, the Resources mode's editor, which had to put the
//  Target in front before opening a style dialog that edits the ACTIVE document. **That editor is
//  gone**, so the door is closed again and the definition is private to this file once more, where
//  the click's selection uses it (and the Deleted row's double click did, until 2026-10-25). The reasoning - why it
//  is a presentation and not a window, and the two calls measured wrong before it - is with that definition in
//  KCMStoryJump.cpp, which is where it always was.)

/** Bring the comparison's Target to the front, when a comparison is armed.

	★**The reader's rule of 2026-09-21**: "once the comparison has started, the Target document
	must be the active one". Start OPENS the Source when the Source is a file - a Task Start copy,
	or a document named while it was closed - and InDesign leaves what it has just opened in
	front, so without this the reader is left looking at the OLDER version of their own work.
	★**Nothing happens when nothing is armed**, which is what makes this safe to call from the
	Start/Stop toggle: the Stop half finds no armed Target and raises nothing.
	⚠**A UI-SIDE JOB ON PURPOSE.** A document is made active through IDocumentPresentation, and
	the model must not drive the UI, so the model's own start cannot do this for itself - every
	UI caller that begins a comparison ends with this call instead.
*/
void KCMBringArmedTargetToFront();

/** Bring this document to the front, when it is still open (2026-10-11). The Before/After report
	puts back the document that was in front when its item was pressed: it may open a Task Start copy
	in a window to read it, as Start does, and with nothing started afterwards there is no armed
	Target for the call above to bring forward instead. nil, or a document closed since, does nothing.
	A UI-side job for the reason given above.
*/
void KCMBringDocumentToFront(IDataBase* db);

#endif // __KCMStoryJump_h__

// End, KCMStoryJump.h.
