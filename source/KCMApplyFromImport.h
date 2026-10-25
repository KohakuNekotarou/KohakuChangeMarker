//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  APPLY THIS CHANGE'S TWO ROADS THAT WRITE MORE THAN ONE CHANGE'S WORDS (2026-10-24 - docs/superpowers/specs/
//  2026-10-24-kcm-import-as-source-design.md): a change that cannot be written on its own is put in with its
//  PARAGRAPH, and a "Table" row with its TABLE - from the files' content the import read (KCMWordKeep), by the
//  import's own road - KCMStoryFromDocument, KCMStorySync::Compare and KCMStorySyncApply - NARROWED to that one
//  paragraph (KCMStorySync::Narrow) or that one table.
//
//  ★★★**ONE MECHANISM FOR WORDS AND MARKS** (the user, design 14-1-5 of 2026-09-23-kcm-import-sync-design.md): a
//   kSetPara step carries the paragraph's words AND its ruby, kenten, tate-chu-yoko and warichu, so a paragraph's
//   words and its marks go in by the same call.
//  ★UNSIGNED AND UNTRACKED (2026-10-24, design section 11-2 - the author: "if it is tracked, change it without
//   tracking"): nothing here touches the user name or a story's change tracking.
//  ★PLANNED FIRST, WRITTEN SECOND - two calls, so that the facade begins its command sequence only once the
//   plan has been made and a refusal lands nothing on the undo stack (the same discipline as the restore's).
//  ⛔"Redo from Word" (2026-09-24 to 2026-10-25) made a taken-back change's paragraph Word's again by the same road;
//   it went with the import that wrote into the document (the tidy, 2026-10-25), and this file kept its roads.
//
//========================================================================================

#ifndef __KCMApplyFromImport_h__
#define __KCMApplyFromImport_h__

#include "BaseType.h"
#include "PMString.h"

#include "KCMStoryList.h"		// KCMStoryChange
#include "KCMStoryShape.h"
#include "KCMStorySyncPlan.h"

class UIDRef;

/** Carries a narrowed plan out as the import does - KCMApplySyncPlan - inside the CALLER's command sequence, unsigned
	and untracked (a story the reader tracks still records it). @return the writes that went in (words + attributes +
	notes + tables); -1 when nothing could be written (outWhy). */
int32 KCMApplyParagraphPlan(const UIDRef& targetStory, const KCMStoryShape::Story& now, const KCMStorySync::Plan& plan,
						   PMString& outWhy);

/** ★"Apply This Change" (2026-10-24 - docs/superpowers/specs/2026-10-24-kcm-import-as-source-design.md): plans putting
	a LIVE change of the import's comparison into the document - the files' content kept by the import (KCMWordKeep,
	under the document), the document read now, the change's paragraph found from where the change stands in the
	document, and the import's comparison narrowed to it (a whole paragraph only the document has is taken out, one
	only the files have is put in; anything else is the paragraph's own edit). The writing is KCMApplyParagraphPlan's,
	and only for a change that cannot be written on its own (design section 11-1: an object's character or a return in
	its range, or a whole paragraph). Refuses with a reason and writes nothing.
	outParaStart / outParaEnd: the paragraph the plan writes, in the document, its return included - where the
	changes that go in together stand.
	★`wordStory` (2026-10-26) - THE STORY THE FILES NAMED: the files' content is kept under it, and it is the row's Source
	  story (KCMStoryRow::fSourceStoryUID, the copy's) - `targetStory`'s own uid for every row but one paired by the place
	  of its first frame, whose Target story is a new one. The same parameter on the two below. */
bool16 KCMPlanApplyParagraph(const UIDRef& targetStory, UID wordStory, const KCMStoryChange& live,
							KCMStoryShape::Story& outNow, KCMStorySync::Plan& outPlan,
							int32& outParaStart, int32& outParaEnd, PMString& outWhy);

/** ★Whether the import's comparison of this story begins with tables added or taken away (stage 0) - measured
	2026-10-24 on matrix A55: then nothing of the story can be put in one change at a time (every paragraph and table is
	paired only once the tables agree), and "Apply All Changes in This Story" is the way in. Reads the document and the
	files' content kept for it; kFalse when either cannot be read (the Apply then says its own reason). */
bool16 KCMImportStoryNeedsTableRound(const UIDRef& targetStory, UID wordStory);

/** Apply This Change on a "Table" row (Table ≠, 2026-10-24 - first written 2026-09-25 for the retired "Redo from Word"
	of a table): the table `tableUID` (its dictionary uid in the Target) is made the files' shape and its cells the
	files' words, by the import's own road - Compare with THIS table allowed to be reshaped, its shape rounds carried
	out and the story read back (five at most, as the import), then the words plan narrowed to the table's cells and
	carried out, unsigned and untracked. Plans and writes in one call, INSIDE the caller's command sequence (a shape
	round has to be read back before the next is planned, so the two cannot be separated as the paragraph road's
	are); the caller rolls the sequence back on -1. @return the moves and writes that went in; -1 when nothing could
	be done, and outWhy says why. */
int32 KCMApplyTableFromImport(const UIDRef& targetStory, UID wordStory, UID tableUID, PMString& outWhy);

#endif // __KCMApplyFromImport_h__

// End, KCMApplyFromImport.h.
