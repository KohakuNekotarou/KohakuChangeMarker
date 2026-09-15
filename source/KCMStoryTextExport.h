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

class IDataBase;
class IDFile;

/** Write every user-accessible story of `db` into a new folder under `parent`.

    The folder is named "<document name> YYYY-MM-DD HHMMSS" and each story becomes one file called
    "<its UID in decimal>.html".

    ★**THE UID IS DECIMAL BECAUSE "Show Story IDs" PRINTS IT THAT WAY.** The reader matches a file
      against a story by reading the two side by side, so the two spellings have to agree.
    ⚠**THE TIME STAMP IS NOT DECORATION.** A folder has no "overwrite?" prompt, so keeping two
      exports apart is this code's job, exactly as it is the report's (KCMReport.cpp says so).

    @param db         the document to read. Nothing in it is changed.
    @param parent     the folder the user chose.
    @param outMessage what happened, for the panel's status line - the count and the place when it
                      worked, the step that failed when it did not.
    @return kFalse when the folder could not be made or no story could be read. */
bool16 KCMExportStoryText(IDataBase* db, const IDFile& parent, PMString& outMessage);

#endif // __KCMStoryTextExport_h__

// End, KCMStoryTextExport.h.
