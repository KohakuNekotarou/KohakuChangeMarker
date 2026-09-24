//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The import's tracked changes in one range of a story: counted, and rejected (2026-09-24, stage 2 A -
//  "Reject This Import Change" on a Story Edits change row; design section 13 of
//  docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md).
//
//  ★ONLY THE IMPORT'S: a change counts when its author is "KohakuChangeMarker" (kKCMImportAuthorName -
//   the name the import writes under, KCMImportTracking.h) and it touches the range (KCMRedlineTouches).
//   The reader's own changes are left - except where InDesign itself takes a deletion along with a nested
//   insertion (redlineiterator.h:49-50), which the user allowed (design 13-1 item 4).
//
//========================================================================================

#ifndef __KCMRejectImport_h__
#define __KCMRejectImport_h__

class UIDRef;

/** How many of the import's tracked changes touch [from, to] of `story`. 0 when the story has none, or
	no change tracking strand at all. */
int32 KCMCountImportChanges(const UIDRef& story, TextIndex from, TextIndex to);

/** Rejects each of them, one at a time, FROM THE BACK OF THE STORY TO THE FRONT: a reject moves what stands
	after it, so the positions are found in one walk and the last is rejected first - every earlier one then
	stays where it was found, and the fixed range cannot pull in a change that stood just past it. A position
	holding several (a replace row's insertion and its deletion's mark) is asked again until none is left.
	⚠The CALLER wraps this in a command sequence - one undo step for the whole row.
	@return how many were rejected; -1 when the story has no redline strand. */
int32 KCMRejectImportChanges(const UIDRef& story, TextIndex from, TextIndex to);

#endif // __KCMRejectImport_h__

// End, KCMRejectImport.h.
