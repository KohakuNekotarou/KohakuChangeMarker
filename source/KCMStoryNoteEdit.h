//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - making and unmaking a footnote in the document
//
//  ★★★**A FOOTNOTE IS A CHARACTER.** InDesign hangs the note off one character standing in the
//  body (kTextChar_FootnoteMarker, U+0004), and everything here follows from that: to take a note
//  away you delete that character and InDesign takes the note with it (measured 2026-09-22 - and
//  it takes any note nested inside it as well); to make one you put the character in and ask
//  kCreateFootnoteCmdBoss to build the note around it.
//
//  ⚠**THIS IS NOT THE POUR'S ORDINARY ROAD, AND MUST NOT BECOME IT.** Writing the body's words
//  never moves one of these characters: KCMStoryTextImport refuses any change that would
//  (RangeTouchesObject), because a note would then vanish with nothing said. What Word did to the
//  notes THEMSELVES arrives as a plan (KCMStoryMerge::NoteAdd / NoteRemove) and is carried out
//  here, deliberately, one note at a time.
//
//  The SDK's own recipe: codesnippets/SnpManipulateTextFootnotes.cpp:309-369 makes one, and
//  :163-168 says in as many words that deleting the reference deletes the note.
//
//========================================================================================
#ifndef __KCMStoryNoteEdit_h__
#define __KCMStoryNoteEdit_h__

#include "BaseType.h"	// int32, bool16, TextIndex
#include "OMTypes.h"	// nil. @warning BaseType.h does NOT define it
#include "PMString.h"

class ITextModel;

/** kTrue when a footnote may be created at `at`.

	★**ASKED BY IID ALONE.** A thread's dictionary answers IID_IFOOTNOTENUMBERING when it can
	  number footnotes. The interface behind that ID is private; the ID itself is public
	  (TextID.h:711), so nothing here dereferences what it must not - the route
	  SnpManipulateTextFootnotes takes, and the only one a plug-in on Adobe Exchange may take.
	⚠Measured 2026-09-22: a table's cell takes one, and so does a footnote itself.
*/
bool16 KCMCanInsertNoteAt(ITextModel* model, TextIndex at);

/** Put a footnote at `at`, and say where its own words begin.

	Three moves, the SDK's: the marker goes into the text, kCreateFootnoteCmdBoss builds the note
	around it, and the note's own thread says where its text ends.

	@param outNoteStart where the caller may pour the note's words. ⚠**PAST WHAT THE NOTE IS BORN
	       WITH**: a new note already holds its number and a separator (measured 2026-09-22 - two
	       characters, the separator a full-width space on this install).
	@param whyNot filled when the answer is kFailure, in words a status line can show.
	@return kFailure when the place will not take a note, or a command would not run.
*/
ErrorCode KCMInsertNoteAt(ITextModel* model, TextIndex at, TextIndex& outNoteStart, PMString& whyNot);

/** Take away the footnote whose marker stands at `markerAt`, by deleting that one character.

	⚠**IT TAKES ANY NOTE NESTED INSIDE IT TOO** (measured 2026-09-22: three notes became one when
	 the marker of a note holding another was deleted). Word cannot make a nested note, so a plan
	 built from a .docx never asks for this - but a document may hold one.
*/
ErrorCode KCMDeleteNoteAt(ITextModel* model, TextIndex markerAt, PMString& whyNot);

#endif // __KCMStoryNoteEdit_h__

// End, KCMStoryNoteEdit.h.
