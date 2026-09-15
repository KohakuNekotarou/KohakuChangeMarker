//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a folder of edited stories, read back into memory
//
//  WHAT THIS IS FOR. The reader exported the document's stories (KCMStoryTextExport), edited them
//  outside InDesign, and now hands the folder back. This file turns that folder into memory.
//
//  ★★★**IT DOES NOT PUT ANYTHING INTO THE DOCUMENT, AND THAT IS THE WHOLE DESIGN.** The edited
//  text is poured into the task-start COPY, the existing Story comparison shows what differs, and
//  the reader puts in the changes they want, one at a time, with "Restore Source Text" - the door
//  that already exists and already refuses what it cannot write. So an import can never surprise
//  anybody: at the moment the folder is read, their document has not changed by one character.
//  (The user weighed "apply everything on import" on 2026-09-15 and kept this; the design's
//  decision table records it, and that they may revisit it once they have used it.)
//
//  ⚠**WHAT IS HELD IS BYTES, NOT A DOCUMENT.** The set sits beside the origin and is dropped with
//   it (KCMReleaseOrigin), because it is only meaningful against that origin's copy.
//
//========================================================================================
#ifndef __KCMStoryTextImport_h__
#define __KCMStoryTextImport_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KCMStoryHtml.h"

class IDFile;

/** What one folder of edited stories holds: each story's own uid, and what was read for it.

    ⚠The two vectors are parallel and always the same length. A file that could not be read is in
      neither - it is counted and named in the message instead, never guessed at. */
struct KCMStoryTextSet
{
	std::vector<UID>					fUids;
	std::vector<KCMStoryHtml::Story>	fStories;
	PMString							fFolderName;	// for the panel's Source: line
};

/** Read every "<decimal uid>.html" in `folder`.

    ★**THE FILE NAME IS THE PAIRING.** A name that is not a decimal number is not one of ours and
      is passed over in silence - the reader may keep notes of their own in that folder.
    ⚠A file whose markup cannot be read is SKIPPED and named in whyNot, so that one bad file does
      not cost the other twenty. kFalse means nothing at all could be read.

    @param out cleared first, then filled.
    @param whyNot what went wrong - filled even when this answers kTrue, when some file was skipped. */
bool16 KCMReadStoryTextFolder(const IDFile& folder, KCMStoryTextSet& out, PMString& whyNot);

/** The set held beside the origin, or nil when none is held. */
const KCMStoryTextSet* KCMHeldStoryText();

/** Hold a copy of `set`, dropping whatever was held before. */
void KCMHoldStoryText(const KCMStoryTextSet& set);

/** Drop it. Called by KCMReleaseOrigin - the two belong to each other - and by the model's
    shutdown, because this static holds PMStrings and std::strings (KCMStoryList.h states the
    rule and what forgetting it costs). */
void KCMReleaseStoryText();

#endif // __KCMStoryTextImport_h__

// End, KCMStoryTextImport.h.
