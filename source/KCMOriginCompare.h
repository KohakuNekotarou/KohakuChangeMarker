//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  Start and Refresh when the chosen Source is the origin.
//
//  THE PROCEDURE IS THE ORDINARY ONE. A rehydrated copy is made (KCMRehydrate), the very same
//  KCMStartComparisonOn runs on (the origin's document, the copy), and then the copy is DETACHED
//  from the armed state (KCMDetachArmedSource) and closed. The results stay in the stores; the
//  panel goes on drawing them; KCMArmedSourceDB answers nil until the next Refresh.
//
//  THE STORY MODE PAIRS BY UID and the copy's uids are new, so while the copy is open a table
//  original <-> copy is built from the KcmOriginUid labels, and the places that open a Source
//  story by uid go through KCMOriginToSourceUID (KCMStoryList.cpp, KCMStoryDiffRun.cpp). The
//  older side's change counters come from the origin's stamps, not from the copy
//  (KCMRebuildStoryEdits). Outside a run the two translators are the identity.
//
//  THE RESOURCES MODE DOES NOT READ THE COPY: it compares the origin's own bytes against a fresh
//  export of the document (KCMResourceStore::RebuildWithSourceBytes), so the app defaults a new
//  document is born with (fonts, TypographersQuotes, an object style) never show up as changes.
//
//========================================================================================
#ifndef __KCMOriginCompare_h__
#define __KCMOriginCompare_h__

#include "BaseType.h"
#include "OMTypes.h"	// UID

class IDataBase;

/** Start: rehydrate, compare, detach, close. kFalse (and a word on the status line) when the
    origin is gone, the rehydration failed, or the comparison was cancelled. */
bool16	KCMOriginStart();

/** Refresh: the same, on the armed origin pair. kFalse as above; a cancel stops the comparison,
    as the ordinary Refresh does. */
bool16	KCMOriginRefresh();

/** kTrue while a comparison is armed whose Source was the origin. KCMArmedSourceDB is nil then. */
bool16	KCMOriginArmed();

/** Stop's hook: forget the armed-origin flag and drop the peek document. */
void	KCMOriginOnStop();

/** kTrue only between the rehydration and the close inside a run - when the Source database the
    procedure holds is the copy. KCMDoMarkChangesDoc and KCMRebuildStoryEdits ask it. */
bool16	KCMOriginRunInProgress();

/** original uid -> the copy's uid, when sourceDB is the copy of the run in progress (identity
    otherwise, and for a uid the table does not know). */
UID		KCMOriginToSourceUID(IDataBase* sourceDB, UID originalUID);

/** the copy's uid -> original uid (identity otherwise / unknown). */
UID		KCMSourceToOriginUID(IDataBase* sourceDB, UID sourceUID);

#endif // __KCMOriginCompare_h__

// End, KCMOriginCompare.h.
