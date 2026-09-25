//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  "Redo from Word" on a change the reader took back (2026-09-24, stage 2 C - design section 15 of
//  docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md): the change's paragraph is made what Word shows
//  again, from the content the import read (KCMWordKeep), by the import's own road - KCMStoryFromDocument,
//  KCMStorySync::Compare, and KCMStorySyncApply - NARROWED to that one paragraph (KCMStorySync::Narrow).
//
//  ★★★**ONE MECHANISM FOR WORDS AND MARKS** (the user, design 14-1-5: "redo purely, the way a text change is
//   redone"): a kSetPara step carries the paragraph's words AND its ruby, kenten, tate-chu-yoko and warichu, so
//   a redo of an attribute row and a redo of a words row are the same call.
//  ★WRITTEN UNDER THE IMPORT'S SIGNATURE (KCMImportAuthor, KCMStoryTrackingOn), so that what a redo puts back
//   can be rejected again, one by one, exactly as the import's own changes can.
//  ★PLANNED FIRST, WRITTEN SECOND - two calls, so that the facade begins its command sequence only once the
//   plan has been made and a refusal lands nothing on the undo stack (the same discipline as the restore's).
//
//========================================================================================

#ifndef __KCMRedoFromWord_h__
#define __KCMRedoFromWord_h__

#include "BaseType.h"
#include "PMString.h"

#include "KCMStoryList.h"		// KCMRejectedRecord
#include "KCMStoryShape.h"
#include "KCMStorySyncPlan.h"

class UIDRef;

/** Plans the redo of one taken-back change: Word's story from KCMWordKeep, the document read now
	(KCMStoryFromDocument), the change's paragraph found from where the Source's words stand (a caret on a boundary
	names the paragraph that starts there), and the plan narrowed to it (KCMStorySync::Narrow). Refuses with a
	reason - nothing kept, the words at the record's place are not the Source's any more, the place not found,
	nothing to redo - and writes nothing. */
bool16 KCMPlanRedoFromWord(const UIDRef& targetStory, const UIDRef& sourceStory, const KCMRejectedRecord& record,
						   KCMStoryShape::Story& outNow, KCMStorySync::Plan& outPlan, PMString& outWhy);

/** Carries the narrowed plan out as the import does - KCMImportAuthor and KCMStoryTrackingOn around
	KCMApplySyncPlan - inside the CALLER's command sequence. @return the writes that went in (words + attributes +
	notes + tables); -1 when nothing could be written (outWhy). */
int32 KCMApplyRedoFromWord(const UIDRef& targetStory, const KCMStoryShape::Story& now, const KCMStorySync::Plan& plan,
						   PMString& outWhy);

/** "Redo from Word" of a TABLE record (2026-09-25 - "Match the Source", design section 16-1 item 7: "redo the same
	way the other redos work"): the table `tableUID` (its dictionary uid in the Target) is made Word's shape again and
	its cells Word's words, by the import's own road - Compare with the tables reshaped, the shape rounds of THIS
	table carried out and the story read back (five at most, as the import), then the words plan narrowed to the
	table's cells and carried out under the import's signature. Plans and writes in one call, INSIDE the caller's
	command sequence (a shape round has to be read back before the next is planned, so the two cannot be separated
	as the paragraph redo's are); the caller rolls the sequence back on -1. @return the moves and writes that went
	in; -1 when nothing could be done, and outWhy says why. */
int32 KCMRedoTableFromWord(const UIDRef& targetStory, UID tableUID, PMString& outWhy);

/** The redo of a TABLE ADDED OR TAKEN AWAY WHOLE (Table + / Table −, 2026-09-25): until then the table redo above took
	every table record and could only ever refuse these two (a table the reject took away has no id left to find, and
	one it brought back has a new one), while the paragraph redo left the table steps to a writer that skips them.
	Word's table goes back in after the paragraph the record names - the way the import puts it in, a paragraph of its
	own, then its shape and its cells' words made Word's by the table redo - or the table standing where the record's
	anchor came back is taken away again. Plans and writes in one call, INSIDE the caller's command sequence, which it
	rolls back on -1. @return the moves and writes that went in; -1 with outWhy when nothing could be done. */
int32 KCMRedoTableAddedOrTaken(const UIDRef& targetStory, const KCMRejectedRecord& record, PMString& outWhy);

#endif // __KCMRedoFromWord_h__

// End, KCMRedoFromWord.h.
