//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  The peek document: the origin rehydrated and cut down to ONE spread, kept while the reader
//  peeks at that spread of the Target. Another spread throws it away and builds it again (the
//  user's rule, 2026-09-12). The origin's release, a Stop and the Target closing drop it.
//
//  WHY IT EXISTS. The peek and the CMYK sampler rasterise the SOURCE's spread, and in the
//  Task Start mode there is no Source database while armed - it was closed after the comparison.
//
//  THE PAIRING IS EXPLICIT HERE. The ordinary peek maps a Target page to a Source page through
//  KCMBuildFullPairing (every page of both documents, in order). A one-spread copy cannot be
//  paired that way, so this file answers "which page of the copy is Target page X" itself, from
//  the KcmOriginUid label the copy's spread carries and the page order within that spread.
//
//========================================================================================
#ifndef __KCMOriginPeek_h__
#define __KCMOriginPeek_h__

#include "BaseType.h"
#include "OMTypes.h"
#include "PMString.h"

class IDataBase;

/** The copy holding Target spread `targetSpreadUID` (of the origin's document) as its ONLY
    ordinary spread - built now if the held one is for another spread. nil when it could not be
    built (a word goes on the status line). outCopySpreadUID names that spread in the copy. */
IDataBase*	KCMOriginPeekDBFor(IDataBase* targetDB, UID targetSpreadUID, UID& outCopySpreadUID);

/** Target page -> the copy's page, for the spread the held copy was built for. kFalse when no
    copy is held, the page is not on that spread, or the copy is gone. */
bool16		KCMOriginPeekMapPage(IDataBase* targetDB, UID targetPageUID, UID& outCopyPageUID);

/** Close and forget the peek document, if any. Idempotent, and re-entrant: the statics are
    cleared BEFORE the close, so the kAfterCloseDoc sweep the close raises finds nothing held.
    @param deferred kTrue schedules the close instead of running it now - for the close sweep,
           which is itself inside a close responder (KCMRehydrate.h). */
void		KCMOriginPeekDrop(bool16 deferred = kFalse);

/** "-" when none is held, else the TARGET spread uid it was built for, as a number. */
void		KCMOriginPeekDescribe(PMString& out);

#endif // __KCMOriginPeek_h__

// End, KCMOriginPeek.h.
