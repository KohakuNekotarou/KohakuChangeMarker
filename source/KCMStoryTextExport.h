//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - every story of a document, written out as Word files to be edited
//
//  WHAT THIS IS FOR. The reader exports the document's text, edits it outside InDesign - in Word,
//  or by handing the file to somebody else - and imports it again. Nothing here writes into their
//  document: this half only reads. The design is
//  docs/superpowers/specs/2026-09-19-kcm-story-docx-roundtrip-design.md.
//
//  ⚠**THERE WAS A SECOND SPELLING UNTIL 2026-09-21.** The stories could be written as .html as well
//   as .docx, and this file took which one as an argument. The HTML road was retired on the user's
//   word ("Word format only"); what is left is one export with nothing to choose. The retired
//   measurements are in docs/ai-notes/kcm-html-retired-2026-09-21.md.
//
//  THE FORMAT ITSELF IS SOMEWHERE ELSE, and deliberately: KCMStoryShape and KCMStoryDocx are pure
//  functions with no SDK type in them, tested outside InDesign against each other
//  (work/kcm-storydocx-test). This file is the part that cannot be tested that way - the part that
//  asks the document questions - and it is kept as thin as it can be for that reason.
//
//  ⚠**READING MUST NOT DIRTY THE DOCUMENT.** A story walk composes text, and composition marks the
//   document modified, so the whole export runs inside IDataBase::SaveRestoreModifiedState - the
//   same guard KCMStoryDiffRun uses, and for the same reason.
//
//========================================================================================
#ifndef __KCMStoryTextExport_h__
#define __KCMStoryTextExport_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"
#include "KCMStoryShape.h"		// Story - the shape a story is read into

class IDataBase;
class IDFile;
class UIDList;

/** Write the stories of `db` into a new folder under `parent`.

    The folder is named "<document name> YYYY-MM-DD HHMMSS" and each story becomes one file called
    "<its UID in decimal>.docx", checked against its own reader - both sides of Word's revision
    marks - before it is written. A story Word's format cannot hold is refused, by story and by
    reason, rather than written and found out later.

    ★**THE UID IS DECIMAL BECAUSE "Show Story IDs" PRINTS IT THAT WAY.** The reader matches a file
      against a story by reading the two side by side, so the two spellings have to agree.
      ⚠The NAME is a courtesy: what pairs a .docx with a story is the tag inside it (KCMStoryDocx).
    ⚠**THE TIME STAMP IS NOT DECORATION.** A folder has no "overwrite?" prompt, so keeping two
      exports apart is this code's job, exactly as it is the report's (KCMReport.cpp says so).
    ★**A VERTICAL STORY SAYS SO**, and Word then shows it the way the page does - the direction is
      read from the story itself (IStoryOptions), so one with no frame still answers.

    @param db         the document to read. Nothing in it is changed.
    @param parent     the folder the user chose.
    @param onlyThese  the stories to write. ★**AN EMPTY LIST MEANS EVERY STORY** - the one place
                      that rule is stated, and the reason the caller decides what a selection means
                      rather than this half guessing at it. A UID in the list that is not a story
                      of this document is counted and passed over, never trusted.
    @param outMessage what happened, for the panel's status line - the count and the place when it
                      worked, the step that failed when it did not.
    @return kFalse when the folder could not be made or no story could be read. */
bool16 KCMExportStoryText(IDataBase* db, const IDFile& parent, const UIDList& onlyThese,
						  PMString& outMessage);

/** The story as the export reads it - body, tables, notes, the spans over them, the notes' reference
	positions: the SHAPE the .docx is written from. ★It is also the "now" the import compares with
	Word (KCMStorySync, since 2026-09-23): one reader, so that what the export wrote and what the
	import compares with are the same reading of the document.
	@param outNoteRefsPlaced kFalse when a footnote's reference could not be placed - the .docx export
	  then refuses the story, and the import leaves it as it stands (a "!" row).
	@return kFalse when the story could not be read at all. Nothing in the document is changed. */
bool16 KCMStoryFromDocument(const UIDRef& storyRef, KCMStoryShape::Story& out, bool16& outNoteRefsPlaced);

#endif // __KCMStoryTextExport_h__

// End, KCMStoryTextExport.h.
