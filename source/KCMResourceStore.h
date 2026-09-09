//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  Where the result of a Resources comparison is KEPT.
//
//  ★★WHY A STORE AT ALL. Taking the two snapshots costs 200-2400ms (two whole-document exports
//  plus two parses). A panel redraws far more often than that, and a panel that recompared on
//  every draw would be unusable. So the comparison runs once and the answer is held, exactly the
//  way the Story Edits list is held (KCMStoryList) - the panel then reads rows, and reading a row
//  is free.
//
//  ★WHAT IT IS NOT: a cache that refreshes itself. Nothing here notices that a document changed;
//  the list goes stale the moment someone edits, and only Rebuild() makes it current again. That
//  is the same contract Story Edits has, and the panel drives both from the same places (Start /
//  Stop / Refresh Comparison).
//
//  ⚠MAIN THREAD ONLY, like KCMStoryList, and for the same reason: the drawing path is the only
//  thing that runs on a background thread, and the drawing path never asks about definitions. It
//  reads the page map and the mark state, neither of which is here.
//  @warning what would break it: drawing something on the page from this list, or rebuilding it
//    from a background export. Either one puts a reader on the BG thread, and then this needs the
//    lock the mark maps use.
//
//========================================================================================
#ifndef __KCMResourceStore_h__
#define __KCMResourceStore_h__

#include "PMString.h"

#include "KCMResourceKinds.h"	// KCMResourceChangeKind
#include "KCMResourceDiff.h"	// KCMResourceDiffStats. ⚠A MODEL-SIDE header, which is fine here:
								// this file is model-internal and the UI never sees it - the UI
								// sees IKCMResourcesFacade, which includes the types-only header.

namespace KCMResourceStore
{
	/** Compares the two documents the comparison is armed on and KEEPS the answer.

	    ⚠It refuses when the Source is a database no session document owns - KIDMCP's task-start
	    clone is one, and exporting one kills InDesign (2026-09-09, measured twice). The refusal
	    comes back through whyNot rather than as a crash.

	    @param whyNot  on kFalse, a short English reason. The previous result is dropped either way,
	                   so a failed rebuild never leaves a stale list looking current.
	    @return kTrue when a result is being held afterwards. */
	bool16	Rebuild(PMString& whyNot);

	/** Throws the held result away. Idempotent. Called when the comparison stops. */
	void	Clear();

	/** kTrue when a result is being held. ⚠Says nothing about whether it is still TRUE of the
	    documents - see the header's note on staleness. */
	bool16	HasResult();

	/** How many definitions differ. 0 with HasResult() kTrue is a real answer: nothing differs. */
	int32	GetChangeCount();

	/** One row. @return kFalse when n is out of range, leaving the outputs alone. */
	bool16	GetNthChange(int32 n, PMString& outKind, PMString& outKey, KCMResourceChangeKind& outWhat);

	/** The two sides' text for one row.

	    ★Asked for ONE ROW rather than carried in GetNthChange, because the bodies are the large
	    part of a result and the panel needs them only for the row a person has selected. */
	bool16	GetNthValues(int32 n, PMString& outSourceBody, PMString& outTargetBody);

	/** One line for the status line: how many were compared and what came of it. */
	void	GetSummary(PMString& out);

	/** The counts the last comparison produced, including the StyleUniqueId sieve's four cells.

	    ⚠MODEL-SIDE ONLY, and deliberately NOT on the facade: the panel has no use for it, and a
	    method on a boundary that nobody calls is a promise nobody keeps. It exists so that
	    app.kcmResourceDiff can report the sieve, which is how "is StyleUniqueId usable" was
	    answered at all (three of its four cells cannot tell "never lied" from "never spoke"). */
	void	GetStats(KCMResourceDiffStats& out);
}

#endif // __KCMResourceStore_h__

// End, KCMResourceStore.h.
