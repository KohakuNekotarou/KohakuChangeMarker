//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  "COMPARE WITH TRACKED CHANGES" (2026-10-05; 2026-10-27 the author's: Track out of the Compare mode menu, "Compare with
//  Tracked Changes alone", "list what changed from the tracked changes alone - do not make a Source document", "Stop
//  goes back to the mode before" - docs/superpowers/specs/2026-10-27-kcm-track-records-only-design.md). The Target - the
//  chosen one, else the active document - has its tracked changes listed from its own records: the Track listing is
//  raised (KCMCore.h) and the ordinary start runs on the Target alone (KCMStartComparisonOn with no Source). Nothing is
//  made - no file, no window, no Source - and the Target is never written to. A Stop ends the listing, and the mode is
//  the one the reader had chosen.
//  (Until 2026-10-27 this was KCMTrackOriginal.*: a save dialog, a copy of the document saved there and opened in a
//   window with every tracked change rejected, chosen as the Source for the peek and the Source window - the Word
//   import's road of the day.)
//
//========================================================================================

#ifndef __KCMTrackStart_h__
#define __KCMTrackStart_h__

#include "BaseType.h"

class PMString;

/** A document to list: the chosen Target, else the active one (KCMTaskDocumentDB) - the flyout item's grey. */
bool16 KCMCanStartTrackChanges();

/** Stop a running comparison - a Track listing too (pressed again = read again) - and list the Target's tracked changes.
	@return kTrue when the listing started; outMessage is what the status line shows either way. */
bool16 KCMStartTrackChanges(PMString& outMessage);

#endif // __KCMTrackStart_h__

// End, KCMTrackStart.h.
