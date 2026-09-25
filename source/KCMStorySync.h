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
		stage per round (S1/S2) - ★what the import passes (2026-09-24 evening, the user's decision:
		the change history does not record those, and the user accepted that they are not taken back
		one by one). kFalse: that table is HELD, words and all - what the redo of ONE paragraph
		(KCMRedoFromWord) passes, since it reshapes nothing. (The import passed kFalse for one day,
		2026-09-24, design 11-1 item 5, since replaced.) ⚠NO DEFAULT, on purpose: every caller says
		which it means. */
void Compare(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word, Plan& out,
			 bool16 reshapeTables);

/** The same, said TABLE BY TABLE (2026-09-25): mayReshape[t] says whether N's table t (Story::fTables - the order
	KCMTableRefsOfStory reads them, which a plan's Where::fTable names) may be made Word's; a table past the end of the
	vector may not, and is held as reshapeTables kFalse holds it. ★For the "Redo from Word" of ONE table (after a
	"Match the Source"): a shape round returns the lowest stage among the tables allowed, and with every table allowed
	that could be ANOTHER table's stage - the redo then found none of its own steps and stopped short (re-check of
	2026-09-25; work/kcm-storydocx-test TestReshapeOneTable). The bool form above is this with every table the same. */
void Compare(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word, Plan& out,
			 const std::vector<bool16>& mayReshape);

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

/** The paragraphs standing at `w` in `s` - the body's, one cell's or one note's - or nil when `w` names a
	place the story does not have. ★Shared with the writing half (KCMStorySyncApply), which asks the same
	question of the finished shape. */
const std::vector<KCMStoryShape::Para>* ParasAt(const KCMStoryShape::Story& s, const Where& w);
std::vector<KCMStoryShape::Para>* ParasAt(KCMStoryShape::Story& s, const Where& w);

/** Every place of `s` a plan may name, in document order: the body, every cell (table, row, cell),
	every note. */
void AllPlaces(const KCMStoryShape::Story& s, std::vector<Where>& out);

/* (⛔RenumberNotesByReading - the notes in the order their references are READ, a table's cells where
	the table stands - stood here until 2026-09-24 with nothing calling it: the apply pairs notes by the
	THREAD order below, which is the document's own numbering.) */

/** The notes numbered in the order the STORY'S THREADS hold their references: the body first, then
	every table's cells, table by table (in ordinal order, which is thread order - KCMStoryTextExport's
	ReadTableShapes sorts them so) and row by row. ★**THIS IS KCMTextRead's NUMBERING** - what the
	document calls note n - so the apply pairs a note of the finished shape with the document's by it. */
void RenumberNotesByThread(KCMStoryShape::Story& s);

/** WHICH OF A PARAGRAPH'S STEPS THE REDO OF ONE ROW CARRIES OUT (2026-09-25). A row is one kind of change, and its
	redo is that change alone: until then every step touching the paragraph came along, so the redo of a paragraph
	added in Word also redid the NEXT paragraph's edit - which had a taken-back row of its own (measured, WN).
	kNarrowOwn - a change INSIDE the paragraph: its kSetPara / kHeld, and the notes whose references stand in it.
	kNarrowInserted - a whole paragraph added (the record's KCMStoryChange fWholeParagraph, kInsert): the kInsertParas
	  standing right before `para`, kept whole, and the notes added in those paragraphs.
	kNarrowRemoved - a whole paragraph taken away (fWholeParagraph, kDelete): the kDeleteParas covering `para`, narrowed
	  to that one paragraph, and the notes that go with it. */
enum NarrowScope { kNarrowOwn, kNarrowInserted, kNarrowRemoved };

/** The steps of `plan` that make ONE paragraph of `now` Word's - (where, para) in N's numbering - for the redo of one
	change the reader took back (2026-09-24, stage 2 C - design 15-1-7), of the kind `scope` says (2026-09-25). Kept:
	kSetPara / kHeld of that paragraph (own); kDeleteParas covering it, narrowed to that one paragraph (removed);
	kInsertParas standing right before it (fPara == para - 1, or -1 when para == 0 - inserted) - ★WHOLE: every paragraph
	Word put in at that place goes back together, and the other records there become twins of the redo; kAddNote whose
	reference lands in the kept paragraphs, its fPara renumbered for the narrowed plan; kDeleteNote whose reference
	`now` holds in that paragraph; kInsertTable at that body paragraph and kDeleteTable of a table `now` holds at it (a
	table record - ⚠no longer reached: a table added or taken away is redone by KCMRedoTableAddedOrTaken, and the
	paragraph redo refuses while one stands taken back). Shape steps are never kept - they are the import's own rounds,
	and the import holds them anyway (design 11-1 item 5). */
void Narrow(const KCMStoryShape::Story& now, const Plan& plan, const Where& where, int32 para, NarrowScope scope,
			Plan& out);

/** Which of Word's tables (its index in `word`'s Story::fTables) a kInsertTable step puts in: the step's fNote counts
	Word's BODY tables alone (the stage 0 of Compare). -1 when it names none. (2026-09-25) */
int32 WordTableOfInsert(const KCMStoryShape::Story& word, const Step& s);

/** `word` with the tables `hide` (indices into its Story::fTables) left out - what the redo of ONE table Word added is
	compared against while OTHER tables Word added stand taken back (re-check 2026-09-25: compared against all of Word,
	the story answered with the missing tables alone - stage 0 - and neither table could ever be redone;
	KCMRedoTableAddedOrTaken). ⚠Only for tables that hold no table of their own - a table Word added never does (the
	stage 0 holds the story otherwise) - so the only numbers that move are the parents the nested tables elsewhere
	name. The paragraph each stood in stays: a difference of the place around it, which no step of a table is. */
void WithoutTables(const KCMStoryShape::Story& word, const std::vector<int32>& hide, KCMStoryShape::Story& out);

}	// namespace KCMStorySync

#endif // __KCMStorySync_h__

// End, KCMStorySync.h.
