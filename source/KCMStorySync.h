//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  THE DECIDING HALF OF AN IMPORT (2026-09-23): the story the document holds now (N) and the one
//  Word left (W) in, what has to be done to make N into W out (KCMStorySyncPlan.h).
//  ★**PURE**: no SDK type, so work/kcm-storydocx-test runs it with no application in the room, and
//  checks the one property it exists for - ApplyToShape(N', Compare(N, W)) is W.
//  ★**COMPARED AFTER BOTH HAVE BEEN THROUGH THE SAME FORMAT** (Normalize): N is written as a .docx
//  and read back, so whatever Word's format cannot say, or says one way only (a reading over one
//  character is always mono), is the same on both sides and is never taken for an edit.
//  Design: docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md, section 4.
//
//========================================================================================

#ifndef __KCMStorySync_h__
#define __KCMStorySync_h__

#include "KCMStorySyncPlan.h"

namespace KCMStorySync
{

/** N and W, made comparable: N written as a .docx and read back, and both put back into N's own
	table shape (KCMStoryDocx::RejoinTables), so that paragraph i of either IS paragraph i of the
	document.
	@return kFalse, with the reason, when there is nothing to compare: the number of tables differs
	  (stage S3), N cannot be written in Word's format at all (the export refuses the same story), or
	  N does not come back paragraph for paragraph. */
bool16 Normalize(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word,
				 KCMStoryShape::Story& outNow, KCMStoryShape::Story& outWord, std::string& whyNot);

/** What to do to N to make it W. Everything that cannot be done is a Held step (or fStoryHeld).
	@param now  the document's story as KCMStoryFromDocument reads it (its own table shape).
	@param word the story as Word left it - KCMStoryDocx::Read's fAfter (the split shape). */
void Compare(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word, Plan& out);

/** The plan carried out on the shape - what the document would hold. `normalizedNow` is Normalize's
	outNow. For the harness: it is the only way to check a plan without InDesign. */
KCMStoryShape::Story ApplyToShape(const KCMStoryShape::Story& normalizedNow, const Plan& plan);

/** The notes numbered in the order their references are READ (the body, a table's cells where the
	table stands, row by row). InDesign numbers notes that way; Word's file numbers them by its own
	ids, which is creation order. Two stories are compared after both have been through this. */
void RenumberNotesByReading(KCMStoryShape::Story& s);

/** The notes numbered in the order the STORY'S THREADS hold their references: the body first, then
	every table's cells, table by table (in ordinal order, which is thread order - KCMStoryTextExport's
	ReadTableShapes sorts them so) and row by row. ★**THIS IS KCMTextRead's NUMBERING** - what the
	document calls note n - so the apply pairs a note of the finished shape with the document's by it. */
void RenumberNotesByThread(KCMStoryShape::Story& s);

}	// namespace KCMStorySync

#endif // __KCMStorySync_h__

// End, KCMStorySync.h.
