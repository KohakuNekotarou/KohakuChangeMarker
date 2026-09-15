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
//  ★★★**AN IMPORT DOES NOT CHANGE THE DOCUMENT** (the user's decision, 2026-09-15 - taken, tried
//  the other way round on a real document, and taken again). The edited words go into the
//  task-start COPY; the Import mode shows them as ordinary comparison rows; "Restore Source Text"
//  is what puts any of them into the reader's own document, one at a time, through the door that
//  already exists and already refuses what it cannot write.
//
//  ★★**THE IMPORT MODE IS WHAT MAKES THAT WORK**, and not merely a label. The Story mode's cheap
//  sieve asks "has the document changed since the task start?" and skips the stories whose counter
//  has not moved - which is EVERY story in an import, because what changed is the copy. Measured
//  2026-09-15: a word edited in the file went into the copy and the Story comparison reported
//  nothing at all. A mode of its own is a mode that can decline that sieve (KCMStoryStamp.cpp).
//
//  ★**THE FILE NAMES ARE THE DOCUMENT'S OWN STORY UIDS**, because the export read that document.
//  The copy's UIDs are new ones, so the pairing goes through the copy's KcmOriginUid label.
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

/** The set held for the import mode, or nil when none is held. */
const KCMStoryTextSet* KCMHeldStoryText();

/** Hold a copy of `set`, dropping whatever was held before. */
void KCMHoldStoryText(const KCMStoryTextSet& set);

/** Drop it. Called by KCMReleaseOrigin - the two belong to each other - and by the model's
    shutdown, because this static holds PMStrings and std::strings (KCMStoryList.h states the rule
    and what forgetting it costs). */
void KCMReleaseStoryText();

/** Pour the held words into `copyDB` - a rehydrated task-start copy, never a real document.

    ★★★**THE COPY, AND ONLY EVER THE COPY** (the user's decision, 2026-09-15, kept after trying the
      other way): an import does not change the reader's document. The words go into the copy, the
      Import mode shows them as ordinary rows, and "Restore Source Text" is what puts any of them
      in, one at a time.
    ★**THE STORIES ARE PAIRED BY THE ORIGINAL UID**, read from the copy's own KcmOriginUid label -
      the copy's UIDs are new ones, so the file names cannot be matched against them directly.

    @return kFalse when nothing at all could be applied. */
bool16 KCMApplyStoryTextToCopy(IDataBase* copyDB, PMString& outMessage);

/** "Import Story Text..." from end to end: read the folder, take the document's state as this
    mode's origin, hold the words, and start the comparison in the Import mode.

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
