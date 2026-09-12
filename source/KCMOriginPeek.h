//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  The peek document: the origin rehydrated WHOLE, kept while the reader peeks at the Target;
//  the spread under the press is looked up in it each time. The origin's release, a Stop and
//  the Target closing drop it.
//  ⚠It used to be cut down to the one spread pressed, and rebuilt for another (the design's
//  decision 5, 2026-09-12). That was a defect, measured the same evening: deleting the other
//  spreads does not delete the text of a threaded story, it reflows it into the frames that
//  remain, so page 2's peek showed page 1's words. The copy is whole since; the peek draws only
//  the paired pages anyway (KCMPeek.cpp, MakeOrigImage per page).
//
//  WHY IT EXISTS. The peek and the CMYK sampler rasterise the SOURCE's spread, and in the
//  Task Start mode there is no Source database while armed - it was closed after the comparison.
//
//  THE PAIRING IS EXPLICIT HERE. The ordinary peek maps a Target page to a Source page through
//  KCMBuildFullPairing over the armed pair; the copy is not armed, so this file answers "which
//  page of the copy is Target page X" itself - the spread through the comparison's own page
//  pairing (KCMBuildPairing), the page by its order within that spread.
//
//========================================================================================
#ifndef __KCMOriginPeek_h__
#define __KCMOriginPeek_h__

#include "BaseType.h"
#include "OMTypes.h"
#include "PMString.h"

class IDataBase;

/** The whole copy of the origin (built now if none is held for this Target), with
    outCopySpreadUID naming the copy's counterpart of Target spread `targetSpreadUID`. nil when the
    copy could not be built or that spread has no counterpart (a word goes on the status line;
    the copy itself stays for the other spreads). */
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
