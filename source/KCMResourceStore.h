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

class IDataBase;	// only ever passed through, so a forward declaration is the whole dependency
class KCMResourceBytes;	// likewise - the origin's bytes, handed on to the parser
#include "KCMResourceDiff.h"	// KCMResourceDiffStats. ⚠A MODEL-SIDE header, which is fine here:
								// this file is model-internal and the UI never sees it - the UI
								// sees IKCMResourcesFacade, which includes the types-only header.

namespace KCMResourceStore
{
	/** Compares two documents and KEEPS the answer.

	    ⚠**THE PAIR IS PASSED IN.** It used to ask KCMArmedTargetDB()/KCMArmedSourceDB() for itself,
	    which made it unusable from the comparison run: KCMDoMarkChangesDoc does its work before the
	    pair is armed, so a rebuild from there refused with "no comparison is armed" while holding
	    both databases in its own parameters. A caller that means the armed pair passes it.

	    ⚠It refuses when the Source is a database no session document owns - KIDMCP's task-start
	    clone is one, and exporting one kills InDesign (2026-09-09, measured twice). The refusal
	    comes back through whyNot rather than as a crash.

	    @param targetDB the newer document. nil refuses.
	    @param sourceDB the older document. nil refuses.
	    @param whyNot  on kFalse, a short English reason. The previous result is dropped either way,
	                   so a failed rebuild never leaves a stale list looking current.
	    @return kTrue when a result is being held afterwards. */
	bool16	Rebuild(IDataBase* targetDB, IDataBase* sourceDB, PMString& whyNot);

	/** Task Start (2026-09-12): the same as Rebuild, with the OLDER side supplied as the origin's
	    own XML rather than exported from a document. A rehydrated copy is born with the app's
	    defaults - fonts, TypographersQuotes, an object style - which are not changes; the bytes
	    the origin holds are the document as it stood, so they are what is compared.
	    @param sourceXml the origin's export. Empty refuses. */
	bool16	RebuildWithSourceBytes(IDataBase* targetDB, const KCMResourceBytes& sourceXml, PMString& whyNot);

	/** Rebuild for a pair whose OLDER side may be the Task Start origin: the origin's own XML when
	    an origin stands as the older side (a run in progress on its copy, or an armed origin
	    pair), otherwise an export of sourceDB. ★ONE PLACE for that choice (2026-09-13): it stood
	    in KCMCore.cpp (the comparison run) and KCMResourceDiff.cpp (app.kcmResourceDiff) as two
	    copies of the same `if`, and the PDF report would have been the third. */
	bool16	RebuildForPair(IDataBase* targetDB, IDataBase* sourceDB, PMString& whyNot);

	/** Throws the held result away. Idempotent. Called when the comparison stops. */
	void	Clear();

	/** kTrue while a result is held (a rebuild succeeded and nothing cleared it since). The PDF
	    report asks this to decide whether to rebuild for itself and clear afterwards. */
	bool16	HasResult();

	/** How many definitions differ. 0 is a real answer when a result is held - nothing differs -
	    and GetSummary is what tells "held, nothing differs" from "nothing held" in words. */
	int32	GetChangeCount();

	/** One row. @return kFalse when n is out of range, leaving the outputs alone. */
	bool16	GetNthChange(int32 n, PMString& outKind, PMString& outKey, KCMResourceChangeKind& outWhat);

	/** The two sides' text for one row.

	    ★Asked for ONE ROW rather than carried in GetNthChange, because the bodies are the large
	    part of a result and the panel needs them only for the row a person has selected. */
	bool16	GetNthValues(int32 n, PMString& outSourceBody, PMString& outTargetBody);

	/** How many ATTRIBUTES of row n are not the same on the two sides.

	    ★The answer is parsed on demand and the last row asked about is kept, because the panel
	    asks in a burst about the one row a person selected. 0 is a real answer: the element
	    changed somewhere this differ does not look (inside a child element, say). */
	int32	GetNthAttrCount(int32 n);

	/** One differing attribute of row n.

	    ⚠A side that does not HAVE the attribute comes back EMPTY - that is how an Added or a
	    Removed definition is told from one whose value merely changed.
	    ⚠Values are VERBATIM: no unit is appended and nothing is rounded (KCMResourceAttrDiff.h).

	    @return kFalse when n or i is out of range, leaving the outputs alone. */
	bool16	GetNthAttr(int32 n, int32 i, PMString& outName, PMString& outSource, PMString& outTarget);

	/** One line for the status line: how many were compared and what came of it. */
	void	GetSummary(PMString& out);

	/** The counts the last comparison produced (items on each side, paired, added, removed,
	    changed).

	    ⚠MODEL-SIDE ONLY, and deliberately NOT on the facade: the panel reads GetSummary's words,
	    and a method on a boundary that nobody calls is a promise nobody keeps. It exists for
	    app.kcmResourceDiff, whose first line is these numbers. */
	void	GetStats(KCMResourceDiffStats& out);
}

#endif // __KCMResourceStore_h__

// End, KCMResourceStore.h.
