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
	@param word the story as Word left it - KCMStoryDocx::Read's fAfter (the split shape).
	@param reshapeTables kTrue: a table whose rows, columns or merges differ is made Word's, one
		stage per round (S1/S2). kFalse: that table is HELD, words and all - ★what the import passes
		since 2026-09-24 (design 11-1 item 5, the user's decision): the import writes under Track
		Changes, and InDesign's change history does not record rows, columns or merges, so a change
		of that kind could not be taken back one by one. ⚠NO DEFAULT, on purpose: every caller says
		which it means. */
void Compare(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word, Plan& out,
			 bool16 reshapeTables);

/** The plan's kResizeRows steps carried out on `now` (the document's own shape), the way InDesign
	does it (measured: docs/ai-notes/kcm-table-reshape-spike-2026-09-23.md): a row added at the end
	runs like the last row, each of its cells one empty paragraph; a row taken away takes its words and
	its footnotes with it, and the notes after close up. What the document should read back as once the
	first round is done - the side that writes checks it with SameTableLayout. */
KCMStoryShape::Story ReshapeOnPaper(const KCMStoryShape::Story& now, const Plan& plan);

/** kTrue when the two stories' tables have the same rows and the same cells (count and span) - the
	check after a first round. `why` names the first difference. */
bool16 SameTableLayout(const KCMStoryShape::Story& a, const KCMStoryShape::Story& b, std::string& why);

/** kTrue when every table of the two stories stands in the same place - the same paragraph of the same
	body or cell, at the same offset - and the body and every cell hold as many paragraphs (2026-09-24,
	S3b design 12-3-4). What the import checks after writing: the plan on paper (ApplyToShape) against
	the document read again, because only the live write can leave a table behind (the matrix's X03
	did, in the middle of a word, and said nothing). Words are not its business. `why` names the first
	difference. */
bool16 SameTablePlaces(const KCMStoryShape::Story& a, const KCMStoryShape::Story& b, std::string& why);

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

/** The steps of `plan` that make ONE paragraph of `now` Word's - (where, para) in N's numbering - for the redo of one
	change the reader took back (2026-09-24, stage 2 C - design 15-1-7). Kept: kSetPara / kHeld of that paragraph;
	kDeleteParas covering it, narrowed to that one paragraph; kInsertParas standing right before it (fPara == para - 1,
	or -1 when para == 0) - ★WHOLE: every paragraph Word put in at that place goes back together, and the other records
	there become twins of the redo; kAddNote whose reference lands in the kept paragraphs, its fPara renumbered for the
	narrowed plan; kDeleteNote whose reference `now` holds in that paragraph; kInsertTable at that body paragraph and
	kDeleteTable of a table `now` holds at it (a table record). Shape steps are never kept - they are the import's own
	rounds, and the import holds them anyway (design 11-1 item 5). */
void Narrow(const KCMStoryShape::Story& now, const Plan& plan, const Where& where, int32 para, Plan& out);

}	// namespace KCMStorySync

#endif // __KCMStorySync_h__

// End, KCMStorySync.h.
