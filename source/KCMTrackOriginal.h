//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  "COMPARE WITH TRACKED CHANGES..." (2026-10-05, design section 2-2) - the same road as Import Story Text:
//  a save dialog, a copy of the document saved there, that copy opened IN A WINDOW (KCMRealisePairEnd - the
//  way Start opens a Task Start copy) with every tracked change REJECTED and saved again, chosen as the
//  Source, and the Track Changes mode started against it. The Target is never written to.
//  ★WHY A FILE: the peek and the Source window need a document to draw (the user's choice over a clone,
//   an internal INX, or rejecting in place and rolling back - design D9).
//
//========================================================================================

#ifndef __KCMTrackOriginal_h__
#define __KCMTrackOriginal_h__

#include "BaseType.h"
#include "IDFile.h"
#include "PMString.h"

class IDataBase;

/** A document to copy: the chosen Target, else the active one (KCMTaskDocumentDB). */
bool16 KCMCanTakeTrackOriginalCopy();

/** Make the copy and start the Track comparison. dest nil = ask with the save dialog.
	@return kTrue when the comparison was started; outMessage is what the status line shows.
	⚠kFalse with an EMPTY outMessage = the reader cancelled the dialog - say nothing (RUN-67). */
bool16 KCMTakeTrackOriginalCopy(const IDFile* dest, PMString& outMessage);

/** Is sourceDB the copy this session made from targetDB? Asked of the FILES (the copy's, and the Target's -
	or its document ID when it was never saved), never of the pointers ([[uidref-reuse-after-close]]). */
bool16 KCMIsTrackOriginalOf(IDataBase* sourceDB, IDataBase* targetDB);

#endif // __KCMTrackOriginal_h__

// End, KCMTrackOriginal.h.
