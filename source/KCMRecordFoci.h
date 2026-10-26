//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  ONE TEXT FOCUS PER "=" RECORD, OF KCM'S OWN CLASS (KCMRecordFocus.h - 2026-10-26, docs/superpowers/specs/2026-10-26-
//  kcm-equal-follow-focus-design.md): InDesign moves it when text is typed or deleted around it, and the user's Undo and
//  Redo put it back - so a record's place follows a hand edit before it, and the edit's Undo, where until now it went
//  stale until the row was compared again. KFC's KFCRowFoci, cut down to what KCM asks of it: kept by a NUMBER of this
//  session (KCMEqualRecord::fFocusId), not by a row's index - records are inserted, pruned and carried from one list to
//  the next, and a number stays with its record through all of it.
//  Made inside a write's step by the facade's writes (KCMStoryList::FocusBeforeWrite / FocusAfterWrite), or after a
//  comparison placed a record by its readings (KCMStoryList::FollowRecords). Let go wherever a record goes, before its
//  document closes (kBeforeCloseDoc - KCMDocResponder.cpp), and at the shutdown - its target database cleared first.
//  The main thread only: every call off it does nothing and answers "none".
//
//========================================================================================

#ifndef __KCMRecordFoci_h__
#define __KCMRecordFoci_h__

#include "BaseType.h"	// TextIndex
#include "UIDRef.h"		// UID

#include <set>

class IDataBase;

namespace KCMRecordFoci
{
	/** What a record's focus can say (Where). */
	enum State { kNone, kLost, kFollowed };

	/** A focus of KCM's own class on [start, end) of `story` in `db`, its target database set (without one its snapshots
		go nowhere, and an Undo leaves it behind as InDesign's own focus is left - KCMRecordFocus.h). A focus made outside a
		step follows every edit from now on and their Undos and Redos, not an Undo of what came before it (it is then lost).
		@return its number (> 0), 0 when none was made: not the main thread, the story not there, a range past its end, the
		  test build's switch focus-off (KCMDiag.h). */
	int32 Make(IDataBase* db, UID story, TextIndex start, TextIndex end);

	/** Focus `id`, if it is `db`'s story `story`'s, and its range now: kFollowed = it has followed every edit and every
		Undo since it was made - the record's words are there, or were edited there; kLost = it met an Undo it could not
		follow (the range is no answer); kNone = none (an unknown number, another document or story, or no longer usable -
		its document, its story's manager, its model - then it is let go, and the caller drops the number). */
	State Where(int32 id, IDataBase* db, UID story, TextIndex& outStart, TextIndex& outEnd);

	/** Focus `id` put on [start, end). inStep = inside a write's step: marked first (PreDirtyFocus) - ALWAYS, even when it
		stands there already, so that the step holds a snapshot of it and its Undo puts it back rather than finding it with
		none (KCMRecordFocus.h's one thing it cannot follow). Outside a step an Undo after the move reads back the range of
		before it (the KT spike note's S7), so nothing here moves a focus outside one. */
	void MoveTo(int32 id, TextIndex start, TextIndex end, bool16 inStep);

	/** Focus `id` let go - its target database cleared, out of its story's manager, our reference released (the last two
		only while the manager lists it: a leak beats a call into what may be gone). An unknown number does nothing. */
	void Let(int32 id);

	/** Every focus on `db` let go - before its document closes (kBeforeCloseDoc), while it certainly stands. */
	void LetDocument(IDataBase* db);

	/** Every focus let go (the list emptied - Stop, a mode without story rows, the shutdown). */
	void LetAll();

	/** Every focus whose number is not in `keep` let go - the list's own sweep, after a comparison (a record that went by
		a road that did not let its focus go). */
	void LetAllBut(const std::set<int32>& keep);
}

#endif // __KCMRecordFoci_h__

// End, KCMRecordFoci.h.
