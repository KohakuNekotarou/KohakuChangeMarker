//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - every story of a document, written out as HTML to be edited
//
//  WHAT THIS IS FOR. The reader exports the document's text, edits it outside InDesign - in an
//  editor, in a browser, or by handing the file to somebody else - and imports it again. Nothing
//  here writes into their document: this half only reads. The design is
//  docs/superpowers/specs/2026-09-15-kcm-story-text-roundtrip-design.md.
//
//  THE FORMAT ITSELF IS SOMEWHERE ELSE, and deliberately: KCMStoryHtml is pure functions with no
//  SDK type in them, tested outside InDesign against its own reader (work/kcm-storyhtml-test). This
//  file is the part that cannot be tested that way - the part that asks the document questions -
//  and it is kept as thin as it can be for that reason.
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
#include "KCMStoryHtml.h"		// Story - the shape a story is read into

class IDataBase;
class IDFile;
class UIDList;

/** Which spelling the stories are written in.

    kKCMStoryTextHtml is "<uid>.html", checked against its own reader before it is written.
    kKCMStoryTextDocx is "<uid>.docx" for Word (2026-09-19, KCMStoryDocx.h), checked the same way since
    stage 2 of its plan gave it a reader (both sides of Word's revision marks read back and compared).
    It refuses what Word's format cannot hold, and says which story and why. */
enum KCMStoryTextFormat
{
	kKCMStoryTextHtml = 0,
	kKCMStoryTextDocx = 1
};

/** Write the stories of `db` into a new folder under `parent`.

    The folder is named "<document name> YYYY-MM-DD HHMMSS" and each story becomes one file called
    "<its UID in decimal>.html", carrying its own look in a <style> of its own (2026-09-16: one
    file taken out of the folder has to look right on its own, because looking at it in a browser
    is how the reader checks what they edited).

    ★**THE UID IS DECIMAL BECAUSE "Show Story IDs" PRINTS IT THAT WAY.** The reader matches a file
      against a story by reading the two side by side, so the two spellings have to agree.
    ⚠**THE TIME STAMP IS NOT DECORATION.** A folder has no "overwrite?" prompt, so keeping two
      exports apart is this code's job, exactly as it is the report's (KCMReport.cpp says so).
    ★**A VERTICAL STORY SAYS SO**, and the browser then shows it the way the page does - the
      direction is read from the story itself (IStoryOptions), so one with no frame still answers.

    @param db         the document to read. Nothing in it is changed.
    @param parent     the folder the user chose.
    @param onlyThese  the stories to write. ★**AN EMPTY LIST MEANS EVERY STORY** - the one place
                      that rule is stated, and the reason the caller decides what a selection means
                      rather than this half guessing at it. A UID in the list that is not a story
                      of this document is counted and passed over, never trusted.
    @param outMessage what happened, for the panel's status line - the count and the place when it
                      worked, the step that failed when it did not.
    @param format     the spelling. HTML when not said, which is what every caller older than the
                      .docx road means.
    @return kFalse when the folder could not be made or no story could be read. */
bool16 KCMExportStoryText(IDataBase* db, const IDFile& parent, const UIDList& onlyThese,
						  PMString& outMessage, KCMStoryTextFormat format = kKCMStoryTextHtml);

/** The story as the export reads it - body, tables, notes, the spans over them, the notes' reference
	positions: the SHAPE both the .html and the .docx are written from. ★Since stage 3 of the docx plan
	it is also the "now" the import merges Word's changes onto (KCMStoryMerge): one reader, so that
	what the export wrote and what the import compares with are the same reading of the document.
	@param outNoteRefsPlaced kFalse when a footnote's reference could not be placed - the .docx export
	  then refuses the story; the merge does not need the places and reads it all the same.
	@return kFalse when the story could not be read at all. Nothing in the document is changed. */
bool16 KCMStoryFromDocument(const UIDRef& storyRef, KCMStoryHtml::Story& out, bool16& outNoteRefsPlaced);

#endif // __KCMStoryTextExport_h__

// End, KCMStoryTextExport.h.
