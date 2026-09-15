//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a folder of edited stories, read back and put into the document
//
//  WHAT THIS IS FOR. The reader exported the document's stories (KCMStoryTextExport), edited them
//  outside InDesign - in an editor, in a browser, or by handing the file to somebody else - and
//  now hands the folder back. This file reads it and writes those words into the document.
//
//  ★★★**IMPORTING CHANGES THE DOCUMENT. THAT IS THE DECISION** (the user's, 2026-09-15, after
//  trying it the other way round): "read it in and everything is changed at that point". The
//  design's first shape poured the words into the task-start COPY and let the reader put them in
//  one at a time; that is recorded in the spec along with why it was dropped.
//  ⚠**SO THE WHOLE IMPORT IS ONE UNDO STEP.** It is their document, and Ctrl+Z has to take back
//    the import rather than the last paragraph of it.
//
//  ★**THE FILE NAMES ARE THE DOCUMENT'S OWN STORY UIDS**, because the export read that document:
//  no labels, no copy, no pairing table. A file whose uid is not in the document is counted and
//  reported, never guessed at.
//
//========================================================================================
#ifndef __KCMStoryTextImport_h__
#define __KCMStoryTextImport_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KCMStoryHtml.h"

class IDataBase;
class IDFile;

/** What one folder of edited stories holds: each story's own uid, and what was read for it.

    ⚠The two vectors are parallel and always the same length. A file that could not be read is in
      neither - it is counted and named in the message instead, never guessed at. */
struct KCMStoryTextSet
{
	std::vector<UID>					fUids;
	std::vector<KCMStoryHtml::Story>	fStories;
	PMString							fFolderName;	// for the panel's status line
};

/** Read every "<decimal uid>.html" in `folder`.

    ★**THE FILE NAME IS THE PAIRING.** A name that is not a decimal number is not one of ours and
      is passed over in silence - the reader may keep notes of their own in that folder.
    ⚠A file whose markup cannot be read is SKIPPED and named in whyNot, so that one bad file does
      not cost the other twenty. kFalse means nothing at all could be read.

    @param out cleared first, then filled.
    @param whyNot what went wrong - filled even when this answers kTrue, when some file was skipped. */
bool16 KCMReadStoryTextFolder(const IDFile& folder, KCMStoryTextSet& out, PMString& whyNot);

/** "Import Story Text..." from end to end: read the folder, then write it into the active document
    as ONE undo step.

    ⚠**ONLY CHANGES INSIDE A PARAGRAPH ARE APPLIED, so far.** A place whose paragraph COUNT differs
      is refused with a reason rather than guessed at: adding and removing paragraphs needs the end
      of a thread to be known exactly, and that is measured work not yet done. Everything else -
      the words inside each paragraph - goes in minimally, so that the ruby and the kenten on the
      parts nobody edited are still there afterwards.
    ⚠**A CHANGE TOUCHING AN INVISIBLE CHARACTER IS REFUSED.** An anchored object's character, a
      page number, an index marker: these can be moved or deleted from outside only by accident,
      and the file format carries them precisely so that this check can be made.

    @param folder the folder the reader chose.
    @param outMessage what happened, for the panel's status line.
    @return kFalse when nothing could be read or nothing could be applied. */
bool16 KCMImportStoryText(const IDFile& folder, PMString& outMessage);

/** Whether the fourth mode is up.

    ★★★**IT IS MODAL, AND THAT IS THE POINT** (the user's rule): while an import is showing, the
      other three modes and Task Start are greyed, and Stop Comparison is the way out. Because no
      other comparison can run inside it, the reader's own Task Start can simply be parked for its
      duration - which is what lets one origin slot serve both. */
bool16 KCMInImportMode();

/** Leave it: the import's own origin goes, the reader's parked Task Start comes back, and the mode
    that was showing before returns. Called by Stop Comparison; doing nothing when no import is up. */
void KCMEndImportMode();

#endif // __KCMStoryTextImport_h__

// End, KCMStoryTextImport.h.
