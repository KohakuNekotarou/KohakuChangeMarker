//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Whether a tracked change touches a Story Edits change row's range (2026-09-24, stage 2 A -
//  "Reject This Import Change", design docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md
//  section 13). Pure: no SDK type beyond int32 / bool16, so the harness in
//  work/kcm-storydocx-test tests it without InDesign.
//
//========================================================================================

#ifndef __KCMRedlineRange_h__
#define __KCMRedlineRange_h__

/** Whether a tracked change touches the range [from, to] - BOTH ends included.
	An insertion covers [at, at + len). A deletion stands as ONE mark at the character after the deleted
	words (redlineiterator.h:42), so it touches the range when from <= at <= to.
	★Both ends, because of what a row is (measured 2026-09-24 on the matrix's A30): a deletion row has
	 from == to ("delete" at 4..4), and a REPLACE row [4, 7] is two changes - its insertion at 4..7 and its
	 deletion's mark at 4 - both of which have to go for the words to be the Source's again.
	@param at the change's position in the story (RedlineIterator::GetCurrentChangeRecord)
	@param len the change's length (1 for a deletion's mark)
	@param isDelete kTrue for a deletion
	@param from / to the row's range in the Target (IKCMStoryEditsFacade::Change fTargetStart / fTargetEnd)
*/
inline bool16 KCMRedlineTouches(int32 at, int32 len, bool16 isDelete, int32 from, int32 to)
{
	if (isDelete)
		return (at >= from && at <= to) ? kTrue : kFalse;
	return (at <= to && at + len >= from) ? kTrue : kFalse;
}

#endif // __KCMRedlineRange_h__

// End, KCMRedlineRange.h.
