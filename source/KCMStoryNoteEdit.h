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
//  never moves one of these characters: KCMStorySyncApply refuses any change that would
//  (RangeTouchesObject), because a note would then vanish with nothing said. What Word did to the
//  notes THEMSELVES arrives as steps of the plan (KCMStorySync's kAddNote / kDeleteNote) and is
//  carried out here, deliberately, one note at a time.
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

/** Put a footnote at `at`, and say where the note's own words go.

	Three moves, the SDK's: the marker goes into the text, kCreateFootnoteCmdBoss builds the note
	around it, and the note's own thread says how far its text runs.

	★★★**THE RANGE IS WHAT THE CALLER REPLACES, NOT WHAT IT APPENDS TO.** A new note is born
	  holding its number AND a separator (measured 2026-09-22: two characters, the separator a
	  full-width space on this install), while a note read out of a .docx carries its own separator
	  in its text - Word's or the one the export wrote. Pouring the file's words after the ones the
	  note was born with would print both. So [outWordsFrom, outWordsTo) is the note's text apart
	  from its number: take it out, put the file's in.

	@param outWordsFrom just past the note's own number - the first character the caller may replace.
	@param outWordsTo   just past the note's last character, before its closing return.
	@param whyNot filled when the answer is kFailure, in words a status line can show.
	@return kFailure when the place will not take a note, or a command would not run.
*/
ErrorCode KCMInsertNoteAt(ITextModel* model, TextIndex at, TextIndex& outWordsFrom, TextIndex& outWordsTo,
						  PMString& whyNot);

/** Take away the footnote whose marker stands at `markerAt`, by deleting that one character.

	⚠**IT TAKES ANY NOTE NESTED INSIDE IT TOO** (measured 2026-09-22: three notes became one when
	 the marker of a note holding another was deleted). Word cannot make a nested note, so a plan
	 built from a .docx never asks for this - but a document may hold one.
*/
ErrorCode KCMDeleteNoteAt(ITextModel* model, TextIndex markerAt, PMString& whyNot);

#endif // __KCMStoryNoteEdit_h__

// End, KCMStoryNoteEdit.h.
