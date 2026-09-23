//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  THE WRITING HALF OF AN IMPORT (2026-09-23): a plan KCMStorySync made, carried out on the
//  document. ★**IT JUDGES NOTHING**: what goes in and what is held was decided there, once. What
//  this half can still meet is a command InDesign refuses (a locked story, a position that went
//  stale) - and that it names, and goes on.
//  Design: docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md, section 5.
//
//========================================================================================

#ifndef __KCMStorySyncApply_h__
#define __KCMStorySyncApply_h__

#include "PMString.h"
#include "UIDRef.h"
#include "KCMStorySyncPlan.h"

#include <vector>

/** One "!" row's worth: what, why, and whether it is something held back on purpose. */
struct KCMSyncNote
{
	const char*	fKind;			// the ID column's word: "Story", "Table", "Para", "Word", "Note", "Attr", "Place"
	PMString	fWhy;
	bool16		fHeldBack;		// kept as the document has it on purpose (Word cannot carry it)
	bool16		fWholeStory;

	KCMSyncNote() : fKind("Para"), fHeldBack(kFalse), fWholeStory(kFalse) {}
};

struct KCMSyncResult
{
	int32						fWrites;		// word writes that went in (paragraphs set, added, removed)
	int32						fAttrWrites;	// ruby / kenten / tate-chu-yoko / warichu writes
	int32						fNoteEdits;		// footnotes made or taken away
	int32						fHeld;			// the plan's own Held steps
	int32						fRefused;		// writes InDesign refused
	std::vector<KCMSyncNote>	fNotes;

	KCMSyncResult() : fWrites(0), fAttrWrites(0), fNoteEdits(0), fHeld(0), fRefused(0) {}
};

/** Carry `plan` out on the story. `now` is the story KCMStorySync::Compare was given (the document as
	KCMStoryFromDocument read it). Called inside the import's one command sequence; it opens none. */
void KCMApplySyncPlan(const UIDRef& storyRef, const KCMStoryShape::Story& now,
					  const KCMStorySync::Plan& plan, KCMSyncResult& out);

#endif // __KCMStorySyncApply_h__

// End, KCMStorySyncApply.h.
