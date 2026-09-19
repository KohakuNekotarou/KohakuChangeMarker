//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - the TARGET's internal IDML, taken when a Story comparison
//  against a Task Start starts (and again on Refresh).
//
//  ★WHY (2026-09-20, the user: "Task Start での Story の比較に入った時点で、ターゲットの内部 IDML を
//  作ってしまってもいいかもしれない、あとあと色々使えそう"). The first use: the table a Table row's
//  "Undo the Restore" puts back is cut out of this text (KCMTableSnippet), so no document has to
//  be exported or opened at restore time. It is the same export the Resources mode makes of the
//  document (KCMTakeResourceSnapshot: ExportINX in memory, the document left as it was).
//
//  ⚠TWO INTERNAL IDMLs ARE THEN HELD - the origin's and this one (the user: "常に2つ IDML を持つ
//   わけですね、Stop などで解放を忘れずに"). This one is dropped wherever the origin's Source cache
//   is: Stop / release of the origin, the close sweep, and the model's shutdown.
//  ⚠TAKEN ONLY AGAINST A TASK START (the two-document comparison offers no restore), and only in
//   the Story mode. A failure to take it is a line on the status bar, never a refusal to compare.
//
//========================================================================================

#pragma once
#ifndef __KCMTargetSnapshot_h__
#define __KCMTargetSnapshot_h__

#include "BaseType.h"
#include "PMString.h"

class IDataBase;
class KCMResourceBytes;

/** Export the Target (the document behind `targetDB`) as INX into the held snapshot, replacing the
    previous one. kFalse with a reason when the export failed - the previous snapshot is then gone too. */
bool16 KCMTargetSnapshotTake(IDataBase* targetDB, PMString& whyNot);

/** The held snapshot, or nil when none is held. Valid until the next Take or Drop. */
const KCMResourceBytes* KCMTargetSnapshotBytes();

/** Forget it. */
void KCMTargetSnapshotDrop();

#endif // __KCMTargetSnapshot_h__

// End, KCMTargetSnapshot.h.
