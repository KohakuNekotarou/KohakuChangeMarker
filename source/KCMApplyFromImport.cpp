//========================================================================================
//
//  KCMApplyFromImport.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// SaveRestoreModifiedState - reading must not dirty either document
#include "ITextModel.h"

// General includes:
#include "UIDRef.h"

// Project includes:
#include "KCMParaText.h"		// ModelOffsetInParagraph / CountCodePoints - a paragraph's span in the text model
#include "KCMStorySync.h"		// Compare / Narrow
#include "KCMStorySyncApply.h"	// KCMApplySyncPlan / KCMSyncColumnsOfRow
#include "KCMStoryTextExport.h"	// KCMStoryFromDocument / KCMTableRefsOfStory - the document, read the way the import reads it
#include "KCMTextRead.h"		// ReadStory - the paragraphs and their places, for the change's paragraph
#include "KCMTextWords.h"		// Refuse - shared with the restore
#include "KCMStoryTextImport.h"	// KCMImportSourceStory - what is put in, read from the import's copy (2026-10-26)
#include "KCMApplyPlace.h"		// KCMApplyPlace::Of - which paragraph the change is (2026-09-25)
#include "KCMCore.h"				// KCMArmedSourceDB - the import's copy, whose tables a Table - row names (2026-10-27)

#include <string>
#include "KCMApplyFromImport.h"

namespace
{

using KCMTextWords::Refuse;

/** The place reading paragraph `k` stands in, as a plan's step names it: the body, a note, or a cell by its index
	among its row's columns. kFalse for a paragraph out of range, or a cell whose column is not among its row's.
	★THE PARAGRAPH'S NUMBER IN THAT PLACE IS KCMApplyPlace's TO SAY (2026-09-25): this answered both until then, from
	 the paragraph holding the record's start - which for a whole paragraph cut with the break before it is the
	 paragraph BEFORE the change (KCMApplyPlace.h says what that broke). */
bool16 WhereOf(const std::vector<KCMParaAttrs>& attrs, int32 k, KCMStorySync::Where& outWhere)
{
	if (k < 0 || static_cast<size_t>(k) >= attrs.size())
		return kFalse;
	const KCMParaAttrs& a = attrs[static_cast<size_t>(k)];
	if (a.IsCell())
	{
		std::vector<int32> cols;
		KCMSyncColumnsOfRow(attrs, a.fTableOrdinal, a.fCellRow, cols);
		int32 cell = -1;
		for (size_t c = 0; c < cols.size(); ++c)
			if (cols[c] == a.fCellCol)
				cell = static_cast<int32>(c);
		if (cell < 0)
			return kFalse;
		outWhere = KCMStorySync::Where::Cell(a.fTableOrdinal, a.fCellRow, cell);
	}
	else if (a.fFootnoteOrdinal != KCMParaAttrs::kNotAFootnote)
		outWhere = KCMStorySync::Where::Note(a.fFootnoteOrdinal);
	else
		outWhere = KCMStorySync::Where::Body();
	return kTrue;
}

/** ★APPLY'S PLAN (2026-10-24): the document now, the way the import reads it; the paragraph the change at document
	position `at` is (KCMApplyPlace); the import's comparison with Word's story, narrowed to that paragraph as `scope`
	says; and the rule that a paragraph the comparison HOLDS is not half written. (The retired "Redo from Word" planned
	the same steps for a taken-back change; it went on 2026-10-25, and this is their one copy now.)
	outParaStart / outParaEnd: the change's paragraph in the document, its return included (the paragraph holding `at`
	when the change is an insertion past its place's last paragraph). */
bool16 PlanApplyAt(const UIDRef& targetStory, const KCMStoryShape::Story& word, int32 at, bool16 afterBreak,
				   KCMStorySync::NarrowScope scope, KCMStoryShape::Story& outNow, KCMStorySync::Plan& outPlan,
				   int32& outParaStart, int32& outParaEnd, PMString& outWhy)
{
	const char* const again = "import again, or use Apply All Changes in This Story";
	outParaStart = 0;
	outParaEnd = 0;
	bool16 placed = kTrue;
	if (!KCMStoryFromDocument(targetStory, outNow, placed) || !placed)
	{
		Refuse(outWhy, "the story could not be read the way the import reads it");
		return kFalse;
	}
	std::vector<std::string> paras;
	std::vector<KCMParaAttrs> attrs;
	std::vector<int32> starts;
	{
		IDataBase::SaveRestoreModifiedState guard(targetStory.GetDataBase());
		if (!KCMTextRead::ReadStory(targetStory, paras, attrs, starts))
		{
			Refuse(outWhy, "the story could not be read");
			return kFalse;
		}
	}
	KCMApplyPlace::Para place;
	KCMStorySync::Where where;
	if (!KCMApplyPlace::Of(paras, starts, attrs, at, afterBreak, place) || !WhereOf(attrs, place.fHold, where))
	{
		Refuse(outWhy, "the change's paragraph could not be found - compare again");
		return kFalse;
	}
	const int32 para = place.fNumber;
	{
		const int32 k = (place.fOwn >= 0) ? place.fOwn : place.fHold;
		outParaStart = starts[static_cast<size_t>(k)];
		outParaEnd = outParaStart
					 + KCMParaText::ModelOffsetInParagraph(attrs[static_cast<size_t>(k)],
														   KCMParaText::CountCodePoints(paras[static_cast<size_t>(k)])) + 1;
	}
	// the import's comparison, narrowed to that paragraph. ⚠reshapeTables kFalse: ONE paragraph is written and no table
	// reshaped - a table whose shape differs from Word's is held (its own row puts it in, or the import).
	KCMStorySync::Plan whole;
	KCMStorySync::Compare(outNow, word, whole, kFalse);
	if (whole.fStoryHeld)
	{
		outWhy.SetUTF8String(whole.fWhy);
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}
	// ★A TABLE WORD ADDED OR TOOK AWAY that the document does not reflect: the comparison answers with those tables alone
	//   (stage 0), and narrowing that to a paragraph found nothing (2026-09-25).
	if (whole.Count(KCMStorySync::Step::kInsertTable) + whole.Count(KCMStorySync::Step::kDeleteTable) > 0)
	{
		outWhy = "a table Word added or took away is not in this story yet - ";
		outWhy.Append(again);
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}
	KCMStorySync::Narrow(outNow, whole, where, para, scope, outPlan);
	if (outPlan.fSteps.empty())
	{
		// ★A CELL OF A TABLE THE COMPARISON HOLDS WHOLE (re-check 2026-09-25): the table's own reason is given.
		if (where.fKind == KCMStorySync::Where::kCell)
		{
			for (size_t i = 0; i < whole.fSteps.size(); ++i)
			{
				const KCMStorySync::Step& s = whole.fSteps[i];
				if (s.fKind == KCMStorySync::Step::kHeld && s.fWhere.fKind == KCMStorySync::Where::kCell
					&& s.fWhere.fTable == where.fTable && s.fWhere.fRow < 0)
				{
					outWhy.SetUTF8String(s.fWhy + " - " + again);
					outWhy.SetTranslatable(kFalse);
					return kFalse;
				}
			}
		}
		Refuse(outWhy, "nothing to write here - the paragraph already reads as Word's");
		return kFalse;
	}
	// ★ALL OF IT OR NONE OF IT (2026-09-25): a paragraph the comparison HOLDS is not half written. ⚠A tate-chu-yoko kept
	//   under a warichu is held ON PURPOSE (Word cannot carry it) and does not count.
	for (size_t i = 0; i < outPlan.fSteps.size(); ++i)
	{
		const KCMStorySync::Step& s = outPlan.fSteps[i];
		if (s.fKind == KCMStorySync::Step::kHeld && s.fWhat != "Tcy")
		{
			outWhy.SetUTF8String("this paragraph cannot be made Word's: " + s.fWhy);
			outWhy.SetTranslatable(kFalse);
			return kFalse;
		}
	}
	return kTrue;
}

/** Where the reference of N's footnote `note` stands: a body paragraph, or a paragraph of a cell (the cell by its index
	among its row's cells, as a plan's Where names it). kFalse when no paragraph of `now` holds it. */
bool16 ReferenceOf(const KCMStoryShape::Story& now, int32 note, KCMStorySync::Where& outWhere, int32& outPara)
{
	for (size_t p = 0; p < now.fBody.size(); ++p)
		for (size_t k = 0; k < now.fBody[p].fNoteRefs.size(); ++k)
			if (now.fBody[p].fNoteRefs[k].fNote == note)
			{
				outWhere = KCMStorySync::Where::Body();
				outPara = static_cast<int32>(p);
				return kTrue;
			}
	for (size_t t = 0; t < now.fTables.size(); ++t)
		for (size_t r = 0; r < now.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < now.fTables[t].fRows[r].fCells.size(); ++c)
			{
				const std::vector<KCMStoryShape::Para>& ps = now.fTables[t].fRows[r].fCells[c].fParas;
				for (size_t p = 0; p < ps.size(); ++p)
					for (size_t k = 0; k < ps[p].fNoteRefs.size(); ++k)
						if (ps[p].fNoteRefs[k].fNote == note)
						{
							outWhere = KCMStorySync::Where::Cell(static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c));
							outPara = static_cast<int32>(p);
							return kTrue;
						}
			}
	return kFalse;
}

/** ★A FOOTNOTE'S OWN PARAGRAPH, WHOLE (2026-10-27 night - design 2026-10-27-kcm-apply-tables-notes-per-row-design.md
	§5-4): the note is put in or taken out, its reference with it, by the plan narrowed to the paragraph that holds -
	or will hold - the reference: that is where the import's comparison puts a kAddNote or a kDeleteNote, and the only
	paragraph KCMStorySync::Narrow keeps one for. Narrowed to the note's own paragraph, the plan was empty, and the row
	said "nothing to write here - the paragraph already reads as Word's" of a note Word had taken away (measured on the
	re-inspection's IN1 - kcm-blockM19-inspections-2026-10-27.md §2); a note Word added was refused for having no place.
	The reference paragraph's own changes go in with it (the paragraph road's rule) - outParaStart / outParaEnd name that
	paragraph in the document when it is in the body, so the rows standing in it are told they went in too. */
bool16 PlanApplyNote(const UIDRef& targetStory, const KCMStoryShape::Story& word, const KCMStoryChange& live,
					 KCMStoryShape::Story& outNow, KCMStorySync::Plan& outPlan, int32& outParaStart, int32& outParaEnd,
					 PMString& outWhy)
{
	const char* const again = "import again, or use Apply All Changes in This Story";
	outParaStart = 0;
	outParaEnd = 0;
	bool16 placed = kTrue;
	if (!KCMStoryFromDocument(targetStory, outNow, placed) || !placed)
	{
		Refuse(outWhy, "the story could not be read the way the import reads it");
		return kFalse;
	}
	std::vector<std::string> paras;
	std::vector<KCMParaAttrs> attrs;
	std::vector<int32> starts;
	{
		IDataBase::SaveRestoreModifiedState guard(targetStory.GetDataBase());
		if (!KCMTextRead::ReadStory(targetStory, paras, attrs, starts))
		{
			Refuse(outWhy, "the story could not be read");
			return kFalse;
		}
	}
	KCMStorySync::Plan whole;
	KCMStorySync::Compare(outNow, word, whole, kFalse);
	if (whole.fStoryHeld)
	{
		outWhy.SetUTF8String(whole.fWhy);
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}
	if (whole.Count(KCMStorySync::Step::kInsertTable) + whole.Count(KCMStorySync::Step::kDeleteTable) > 0)
	{
		outWhy = "a table Word added or took away is not in this story yet - ";
		outWhy.Append(again);
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}

	KCMStorySync::Where where = KCMStorySync::Where::Body();
	int32 para = -1;
	int32 goes = KCMParaAttrs::kNotAFootnote;		// kInsert: the document's note the files took away
	if (live.fKind == KCMStoryChange::kInsert)		// only the document has this paragraph
	{
		KCMApplyPlace::Para place;
		if (!KCMApplyPlace::Of(paras, starts, attrs, live.fTargetStart, kFalse, place) || place.fHold < 0
			|| !attrs[static_cast<size_t>(place.fHold)].IsFootnote())
		{
			Refuse(outWhy, "the footnote's paragraph could not be found - compare again");
			return kFalse;
		}
		const int32 note = attrs[static_cast<size_t>(place.fHold)].fFootnoteOrdinal;
		bool16 planned = kFalse;
		for (size_t i = 0; i < whole.fSteps.size() && !planned; ++i)
			if (whole.fSteps[i].fKind == KCMStorySync::Step::kDeleteNote && whole.fSteps[i].fNote == note)
				planned = kTrue;
		if (!planned)
		{
			// the files keep the note: this paragraph is the note's own edit, and the note's own place is the road
			Refuse(outWhy, "the imported text keeps this footnote - compare again");
			return kFalse;
		}
		if (!ReferenceOf(outNow, note, where, para))
		{
			outWhy = "the footnote's reference could not be found - ";
			outWhy.Append(again);
			outWhy.SetTranslatable(kFalse);
			return kFalse;
		}
		goes = note;
	}
	else if (live.fKind == KCMStoryChange::kDelete)	// only the files have it: a note they add
	{
		const std::string rowWords = live.fOtherText.GetUTF8String();
		const KCMStorySync::Step* add = nil;
		const KCMStorySync::Step* only = nil;
		int32 adds = 0;
		for (size_t i = 0; i < whole.fSteps.size(); ++i)
		{
			const KCMStorySync::Step& s = whole.fSteps[i];
			if (s.fKind != KCMStorySync::Step::kAddNote)
				continue;
			++adds;
			only = &s;
			std::string words;
			for (size_t p = 0; p < s.fParas.size(); ++p)
				words += s.fParas[p].fText;
			if (!rowWords.empty() && words.find(rowWords) != std::string::npos)
				add = &s;
		}
		if (add == nil && adds == 1)
			add = only;
		if (add == nil)
		{
			outWhy = "the footnote the imported text adds could not be told apart from its others - ";
			outWhy.Append(again);
			outWhy.SetTranslatable(kFalse);
			return kFalse;
		}
		// ⚠A kAddNote numbers its paragraph in the FINISHED story: a paragraph put in or taken out before it in the same
		//  place would number it otherwise than the document does now - then it goes in with the story.
		for (size_t i = 0; i < whole.fSteps.size(); ++i)
		{
			const KCMStorySync::Step& s = whole.fSteps[i];
			if ((s.fKind == KCMStorySync::Step::kInsertParas || s.fKind == KCMStorySync::Step::kDeleteParas)
				&& s.fWhere == add->fWhere && s.fPara < add->fPara)
			{
				outWhy = "the paragraph the footnote's reference stands in is put in or moved too - ";
				outWhy.Append(again);
				outWhy.SetTranslatable(kFalse);
				return kFalse;
			}
		}
		where = add->fWhere;
		para = add->fPara;
	}
	else
	{
		Refuse(outWhy, "this footnote's paragraph is neither added nor taken away - compare again");
		return kFalse;
	}

	KCMStorySync::Narrow(outNow, whole, where, para, KCMStorySync::kNarrowOwn, outPlan);
	bool16 noteIn = kFalse;
	for (size_t i = 0; i < outPlan.fSteps.size() && !noteIn; ++i)
	{
		const KCMStorySync::Step& s = outPlan.fSteps[i];
		if ((goes >= 0) ? (s.fKind == KCMStorySync::Step::kDeleteNote && s.fNote == goes) : (s.fKind == KCMStorySync::Step::kAddNote))
			noteIn = kTrue;
	}
	if (!noteIn)
	{
		outWhy = "the footnote could not be planned with its reference's paragraph - ";
		outWhy.Append(again);
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}
	for (size_t i = 0; i < outPlan.fSteps.size(); ++i)
	{
		const KCMStorySync::Step& s = outPlan.fSteps[i];
		if (s.fKind == KCMStorySync::Step::kHeld && s.fWhat != "Tcy")
		{
			outWhy.SetUTF8String("this paragraph cannot be made Word's: " + s.fWhy);
			outWhy.SetTranslatable(kFalse);
			return kFalse;
		}
	}
	// the reference's paragraph in the document - a body one (a cell's is left at 0, 0: only the note's row is told)
	if (where.fKind == KCMStorySync::Where::kBody)
	{
		int32 body = -1;
		for (size_t k = 0; k < attrs.size(); ++k)
		{
			if (attrs[k].IsCell() || attrs[k].IsFootnote())
				continue;
			if (++body == para)
			{
				outParaStart = starts[k];
				outParaEnd = outParaStart + KCMParaText::ModelOffsetInParagraph(attrs[k], KCMParaText::CountCodePoints(paras[k])) + 1;
				break;
			}
		}
	}
	return kTrue;
}

}	// namespace

//----------------------------------------------------------------------------------------
// KCMApplyParagraphPlan
//----------------------------------------------------------------------------------------

int32 KCMApplyParagraphPlan(const UIDRef& targetStory, const KCMStoryShape::Story& now, const KCMStorySync::Plan& plan,
						   PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	KCMSyncResult result;
	KCMApplySyncPlan(targetStory, now, plan, result);
	const int32 wentIn = result.fWrites + result.fAttrWrites + result.fNoteEdits + result.fTableEdits;
	// ★★A WRITE REFUSED HALFWAY IS A FAILED WRITE (2026-09-25): until then only "nothing went in" was one, so a
	//   paragraph half written stood as done. -1 now, and the caller rolls the whole step back (KCMFacades ApplyWords -
	//   all of it, or none of it). What was only HELD BACK on purpose (a note with fHeldBack) is not a failure.
	if (wentIn == 0 || result.fRefused > 0)
	{
		outWhy = "nothing was written";
		for (size_t i = 0; i < result.fNotes.size(); ++i)
			if (!result.fNotes[i].fHeldBack)
			{
				outWhy = result.fNotes[i].fWhy;
				break;
			}
		outWhy.SetTranslatable(kFalse);
		return -1;
	}
	return wentIn;
}

//----------------------------------------------------------------------------------------
// KCMPlanApplyParagraph (2026-10-24)
//----------------------------------------------------------------------------------------

bool16 KCMPlanApplyParagraph(const UIDRef& targetStory, UID wordStory, const KCMStoryChange& live,
							KCMStoryShape::Story& outNow, KCMStorySync::Plan& outPlan,
							int32& outParaStart, int32& outParaEnd, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	outPlan = KCMStorySync::Plan();
	outParaStart = 0;
	outParaEnd = 0;

	KCMStoryShape::Story word;
	PMString whyNot;
	if (!KCMImportSourceStory(wordStory, word, whyNot))	// the import's copy, by the story the files named (the header says why)
	{
		outWhy = whyNot;
		return kFalse;
	}
	// ★A FOOTNOTE THE FILES ADD (measured 2026-10-24, matrix A57): its paragraphs are a note the document does not have
	//   yet, so the row's place in the document is a caret past the story's end - and a note goes in with the reference
	//   in its body paragraph, which no row of its own stands for. Said for what it is, rather than as a bad place.
	const char* const noteWhole = "this footnote is not in the document yet - it goes in with the paragraph that refers "
								  "to it: use Apply All Changes in This Story";
	// ★A FOOTNOTE'S OWN PARAGRAPH, WHOLE, GOES IN OR OUT WITH ITS NOTE AND REFERENCE (2026-10-27 night - PlanApplyNote).
	if (live.fPlace == kKCMPlaceNote && live.fWholeParagraph)
		return PlanApplyNote(targetStory, word, live, outNow, outPlan, outParaStart, outParaEnd, outWhy);
	InterfacePtr<ITextModel> target(targetStory, UseDefaultIID());
	if (target == nil || live.fTargetStart < 0 || live.fTargetEnd < live.fTargetStart || live.fTargetEnd > target->TotalLength())
	{
		Refuse(outWhy, (live.fPlace == kKCMPlaceNote) ? noteWhole : "the change's place reaches past the end of the story - compare again");
		return kFalse;
	}

	// ★★THE DIFF RAN FROM THE SOURCE (the files) TO THE TARGET (the document), SO ITS KINDS ARE THE OTHER WAY ROUND FROM
	//   WHAT APPLY DOES (design 5-2): a whole paragraph only the document holds ("+" in the diff) is one the files took
	//   away - Apply takes it out; one only the files hold ("-") is one they put in - Apply puts it in. A whole paragraph
	//   cut with the break before it ("\rNEW") starts ON the paragraph before's return (KCMApplyPlace.h).
	const bool16 afterBreak = (live.fWholeParagraph && live.fBreakAt == kKCMBreakLeads) ? kTrue : kFalse;
	const KCMStorySync::NarrowScope scope = !live.fWholeParagraph ? KCMStorySync::kNarrowOwn
		: (live.fKind == KCMStoryChange::kInsert) ? KCMStorySync::kNarrowRemoved
		: (live.fKind == KCMStoryChange::kDelete) ? KCMStorySync::kNarrowInserted
		: KCMStorySync::kNarrowOwn;
	const bool16 planned = PlanApplyAt(targetStory, word, static_cast<int32>(live.fTargetStart), afterBreak, scope,
									   outNow, outPlan, outParaStart, outParaEnd, outWhy);
	if (!planned && live.fPlace == kKCMPlaceNote && live.fWholeParagraph && live.fKind == KCMStoryChange::kDelete)
		Refuse(outWhy, noteWhole);
	return planned;
}

bool16 KCMImportStoryNeedsTableRound(const UIDRef& targetStory, UID wordStory)
{
	KCMStoryShape::Story word;
	PMString whyNot;
	if (!KCMImportSourceStory(wordStory, word, whyNot))	// the import's copy, by the story the files named (the header says why)
		return kFalse;
	KCMStoryShape::Story now;
	bool16 placed = kTrue;
	if (!KCMStoryFromDocument(targetStory, now, placed) || !placed)
		return kFalse;
	KCMStorySync::Plan plan;
	KCMStorySync::Compare(now, word, plan, kTrue);
	return (plan.Count(KCMStorySync::Step::kInsertTable) + plan.Count(KCMStorySync::Step::kDeleteTable) > 0) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// KCMApplyTableFromImport (2026-09-25 for the retired table redo; Apply's table road since 2026-10-24)
//----------------------------------------------------------------------------------------

namespace
{

/** The index of table `tableUID` among the story's tables in the import's reading (KCMTableRefsOfStory - the ordinal
	a plan's Where::fTable names), or -1 when the story no longer holds it. */
int32 OrdinalOfTable(const UIDRef& targetStory, UID tableUID)
{
	std::vector<UIDRef> tables;
	if (!KCMTableRefsOfStory(targetStory, tables))
		return -1;
	for (size_t k = 0; k < tables.size(); ++k)
		if (tables[k].GetUID() == tableUID)
			return static_cast<int32>(k);
	return -1;
}

/** Whether a paragraph of a cell of table `ordinal` in `now` holds the reference of note `note` (N's numbering - the
	plan's, and the document's: KCMStorySyncPlan.h). */
bool16 CellsReferTo(const KCMStoryShape::Story& now, int32 ordinal, int32 note)
{
	if (ordinal < 0 || static_cast<size_t>(ordinal) >= now.fTables.size())
		return kFalse;
	const KCMStoryShape::Table& t = now.fTables[static_cast<size_t>(ordinal)];
	for (size_t r = 0; r < t.fRows.size(); ++r)
		for (size_t c = 0; c < t.fRows[r].fCells.size(); ++c)
			for (size_t p = 0; p < t.fRows[r].fCells[c].fParas.size(); ++p)
			{
				const std::vector<KCMStoryShape::NoteRef>& refs = t.fRows[r].fCells[c].fParas[p].fNoteRefs;
				for (size_t k = 0; k < refs.size(); ++k)
					if (refs[k].fNote == note)
						return kTrue;
			}
	return kFalse;
}

/** The steps of `plan` about table `ordinal` - its shape moves when `shape`, otherwise its cells' words, marks and
	notes (a note step stands where its reference is: the cell). ★Every step of every cell of the table is kept, so
	the numbering inside a cell is the whole plan's; only other places are left out, and a place is a thread of its
	own.
	★★A NOTE WORD TOOK AWAY NAMES NO PLACE (KCMStorySync's kDeleteNote carries the note alone), and is kept when `now`
	  holds its reference in one of the table's cells (2026-10-27, the re-inspection's M19 - measured: until then a
	  footnote taken out of a cell in Word stayed, while one Word ADDED in a cell - a step naming its cell - went in, and
	  a Table row whose only difference was such a note answered "nothing was written" every time). Narrow keeps the one
	  a paragraph holds the same way. */
void StepsOfTable(const KCMStoryShape::Story& now, const KCMStorySync::Plan& plan, int32 ordinal, bool16 shape,
				  KCMStorySync::Plan& out)
{
	out = KCMStorySync::Plan();
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const KCMStorySync::Step& s = plan.fSteps[i];
		if (s.fKind == KCMStorySync::Step::kDeleteNote)
		{
			if (!shape && CellsReferTo(now, ordinal, s.fNote))
				out.fSteps.push_back(s);
			continue;
		}
		if (s.fWhere.fKind != KCMStorySync::Where::kCell || s.fWhere.fTable != ordinal)
			continue;
		if ((s.IsShape() ? kTrue : kFalse) != shape)
			continue;
		out.fSteps.push_back(s);
	}
}

}	// namespace

int32 KCMApplyTableFromImport(const UIDRef& targetStory, UID wordStory, UID tableUID, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);

	// 1. the files' content, as the import's copy holds it (2026-10-26 - KCMImportSourceStory)
	KCMStoryShape::Story word;
	PMString whyNot;
	if (!KCMImportSourceStory(wordStory, word, whyNot))	// by the story the files named (the header says why)
	{
		outWhy = whyNot;
		return -1;
	}

	int32 total = 0;

	// 2. the shape rounds of THIS table, as the import runs them (KCMStoryTextImport): planned with the tables
	//    reshaped, carried out, the story read back, planned again - five at most.
	for (int32 round = 0; ; ++round)
	{
		if (round >= 5)
		{
			Refuse(outWhy, "the table's shape is still not Word's after five rounds of changes");
			return -1;
		}
		KCMStoryShape::Story now;
		bool16 placed = kTrue;
		if (!KCMStoryFromDocument(targetStory, now, placed) || !placed)
		{
			Refuse(outWhy, "the story could not be read the way the import reads it");
			return -1;
		}
		const int32 ordinal = OrdinalOfTable(targetStory, tableUID);
		if (ordinal < 0)
		{
			Refuse(outWhy, "the table is not in the story any more - compare again");
			return -1;
		}
		// ★★THIS TABLE ALONE MAY BE RESHAPED (re-check of 2026-09-25): with every table allowed, a round is the lowest
		//   stage among ALL the tables, and another table that also differs could hold this table's stage back - its
		//   steps then never came, the loop left early and the write refused with the table half the files'.
		std::vector<bool16> onlyThis(now.fTables.size(), kFalse);
		if (static_cast<size_t>(ordinal) < onlyThis.size())
			onlyThis[static_cast<size_t>(ordinal)] = kTrue;
		KCMStorySync::Plan whole;
		KCMStorySync::Compare(now, word, whole, onlyThis);
		if (whole.fStoryHeld)
		{
			outWhy.SetUTF8String(whole.fWhy);
			outWhy.SetTranslatable(kFalse);
			return -1;
		}
		// ★★A TABLE ADDED OR TAKEN AWAY (stage 0) IS NOT THIS ROAD'S (re-check of 2026-09-25): the story's tables and
		//   the files' do not pair one for one then, and a kDeleteTable names its table the way a cell step does
		//   (Where::Cell(t, -1, -1)) - it would have passed StepsOfTable and TAKEN THIS TABLE AWAY. (Apply refuses such
		//   a story before it gets here - KCMImportStoryNeedsTableRound; this is the second line.)
		if (whole.Count(KCMStorySync::Step::kInsertTable) + whole.Count(KCMStorySync::Step::kDeleteTable) > 0)
		{
			Refuse(outWhy, "the imported text adds or takes away a table in this story - put the story in with Apply All"
						   " Changes in This Story");
			return -1;
		}
		if (!whole.IsShapeRound())
			break;
		KCMStorySync::Plan mine;
		StepsOfTable(now, whole, ordinal, kTrue, mine);
		if (mine.fSteps.empty())
			break;			// this table's shape is Word's
		KCMSyncResult shape;
		KCMApplyTableShape(targetStory, mine, shape);
		if (shape.fRefused > 0)
		{
			outWhy = shape.fNotes.empty() ? PMString("a change of the table's shape was refused") : shape.fNotes[0].fWhy;
			outWhy.SetTranslatable(kFalse);
			return -1;
		}
		total += shape.fTableEdits;
	}

	// 3. the words, marks and notes of its cells, once the shape is the files'. ⚠reshapeTables kFalse: another table
	//    that differs is HELD here rather than reshaped (this write is about one table), and this table - the files'
	//    shape by now - is paired cell for cell.
	KCMStoryShape::Story now;
	bool16 placed = kTrue;
	if (!KCMStoryFromDocument(targetStory, now, placed) || !placed)
	{
		Refuse(outWhy, "the story could not be read back the way the import reads it");
		return -1;
	}
	const int32 ordinal = OrdinalOfTable(targetStory, tableUID);
	if (ordinal < 0)
	{
		Refuse(outWhy, "the table is not in the story any more - compare again");
		return -1;
	}
	KCMStorySync::Plan whole;
	KCMStorySync::Compare(now, word, whole, kFalse);
	if (whole.fStoryHeld)
	{
		outWhy.SetUTF8String(whole.fWhy);
		outWhy.SetTranslatable(kFalse);
		return -1;
	}
	KCMStorySync::Plan mine;
	StepsOfTable(now, whole, ordinal, kFalse, mine);
	for (size_t i = 0; i < mine.fSteps.size(); ++i)
	{
		const KCMStorySync::Step& s = mine.fSteps[i];
		if (s.fKind == KCMStorySync::Step::kHeld && s.fWhere.fRow < 0)
		{
			// the whole table is held: its shape could not be made Word's after all
			outWhy.SetUTF8String(s.fWhy);
			outWhy.SetTranslatable(kFalse);
			return -1;
		}
	}
	if (mine.fSteps.empty())
	{
		if (total > 0)
			return total;	// the shape was the whole difference
		Refuse(outWhy, "nothing to write here - the table already reads as the imported text's");
		return -1;
	}
	KCMSyncResult result;
	KCMApplySyncPlan(targetStory, now, mine, result);
	total += result.fWrites + result.fAttrWrites + result.fNoteEdits + result.fTableEdits;
	// ★A CELL REFUSED HALFWAY FAILS THE WRITE TOO (2026-09-25 - the paragraph road's rule): the caller rolls it all back.
	if (total == 0 || result.fRefused > 0)
	{
		outWhy = "nothing was written";
		for (size_t i = 0; i < result.fNotes.size(); ++i)
			if (!result.fNotes[i].fHeldBack)
			{
				outWhy = result.fNotes[i].fWhy;
				break;
			}
		outWhy.SetTranslatable(kFalse);
		return -1;
	}
	return total;
}

//----------------------------------------------------------------------------------------
// KCMApplyTablesAddedOrTakenAway (2026-10-27 night - the per-row Apply of tables)
//----------------------------------------------------------------------------------------

namespace
{

/** The index of table `tableUID` among the BODY tables of the import's copy of `wordStory` - what a kInsertTable step
	names Word's table by (its fNote, KCMStorySync's PlanTables) - or -1 when the copy holds no such table, or holds it
	inside another table. `word` is the copy's story as the import reads it: its fTables walk the story in the same
	order KCMTableRefsOfStory does (the ordinal a plan's Where::fTable names). */
int32 BodyOrdinalOfCopyTable(UID wordStory, UID tableUID, const KCMStoryShape::Story& word)
{
	IDataBase* const copyDB = KCMArmedSourceDB();
	if (copyDB == nil || tableUID == kInvalidUID)
		return -1;
	std::vector<UIDRef> tables;
	if (!KCMTableRefsOfStory(UIDRef(copyDB, wordStory), tables) || tables.size() != word.fTables.size())
		return -1;
	int32 body = 0;
	for (size_t k = 0; k < tables.size(); ++k)
	{
		const bool16 inBody = (word.fTables[k].fInTable < 0) ? kTrue : kFalse;
		if (tables[k].GetUID() == tableUID)
			return inBody ? body : -1;
		if (inBody)
			++body;
	}
	return -1;
}

}	// namespace

int32 KCMApplyTablesAddedOrTakenAway(const UIDRef& targetStory, UID wordStory, const KCMStoryChange& live,
								   int32& outTogether, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	outTogether = 0;

	// 1. the files' content (the import's copy), and the document as the import reads it
	KCMStoryShape::Story word;
	PMString whyNot;
	if (!KCMImportSourceStory(wordStory, word, whyNot))	// by the story the files named (the header says why)
	{
		outWhy = whyNot;
		return -1;
	}
	KCMStoryShape::Story now;
	bool16 placed = kTrue;
	if (!KCMStoryFromDocument(targetStory, now, placed) || !placed)
	{
		Refuse(outWhy, "the story could not be read the way the import reads it");
		return -1;
	}

	// 2. the story's round of tables added and taken away - stage 0, the import's first round (design section 10-2)
	KCMStorySync::Plan whole;
	KCMStorySync::Compare(now, word, whole, kTrue);
	if (whole.fStoryHeld)
	{
		outWhy.SetUTF8String(whole.fWhy);
		outWhy.SetTranslatable(kFalse);
		return -1;
	}
	KCMStorySync::Plan round;
	for (size_t i = 0; i < whole.fSteps.size(); ++i)
		if (whole.fSteps[i].fKind == KCMStorySync::Step::kInsertTable || whole.fSteps[i].fKind == KCMStorySync::Step::kDeleteTable)
			round.fSteps.push_back(whole.fSteps[i]);
	if (round.fSteps.empty())
	{
		Refuse(outWhy, "the imported text adds or takes away no table in this story any more - compare again");
		return -1;
	}

	// 3. ★THE ROW'S OWN TABLE HAS TO BE ONE OF THEM: the list pairs tables by their uids, the import's round by their
	//    words (PlanTables) - when the two disagree about which table went or came, the row is not put in by this road.
	bool16 mine = kFalse;
	if (live.fKind == KCMStoryChange::kInsert)			// Table +: only the document has it - the files took it away
	{
		std::vector<UIDRef> tables;
		int32 ordinal = -1;
		if (KCMTableRefsOfStory(targetStory, tables))
			for (size_t k = 0; k < tables.size(); ++k)
				if (tables[k].GetUID() == live.fTargetTableUID)
					ordinal = static_cast<int32>(k);
		for (size_t i = 0; i < round.fSteps.size() && !mine; ++i)
			if (round.fSteps[i].fKind == KCMStorySync::Step::kDeleteTable && round.fSteps[i].fWhere.fTable == ordinal
				&& ordinal >= 0)
				mine = kTrue;
	}
	else if (live.fKind == KCMStoryChange::kDelete)		// Table −: only the files have it - they put it in
	{
		const int32 bodyOrdinal = BodyOrdinalOfCopyTable(wordStory, live.fSourceTableUID, word);
		for (size_t i = 0; i < round.fSteps.size() && !mine; ++i)
			if (round.fSteps[i].fKind == KCMStorySync::Step::kInsertTable && round.fSteps[i].fNote == bodyOrdinal
				&& bodyOrdinal >= 0)
				mine = kTrue;
	}
	if (!mine)
	{
		Refuse(outWhy, "the import pairs this story's tables another way - put the story in with Apply All Changes in "
					   "This Story");
		return -1;
	}
	// ★★THE WHOLE ROUND, AND SAID (design section 3, the anti-goal "said when they go in together"): with two tables or
	//   more added or taken away, the import pairs what is left by POSITION only once the numbers agree (PlanTables) -
	//   one table put in alone leaves an empty table the next round cannot pair, and two rows would block each other.
	//   So the round goes in whole; the caller's sentence says how many went in together.
	outTogether = static_cast<int32>(round.fSteps.size());

	// 4. carried out as the import carries a shape round out; the tables it put in are told apart by their uids
	std::vector<UIDRef> before;
	KCMTableRefsOfStory(targetStory, before);
	KCMSyncResult shape;
	KCMApplyTableShape(targetStory, round, shape);
	if (shape.fRefused > 0)
	{
		outWhy = shape.fNotes.empty() ? PMString("a table could not be added or taken away") : shape.fNotes[0].fWhy;
		outWhy.SetTranslatable(kFalse);
		return -1;
	}
	int32 total = shape.fTableEdits;
	if (total <= 0)
	{
		Refuse(outWhy, "no table was added or taken away");
		return -1;
	}

	// 5. each table the round put in - each cell one empty paragraph - made the files': its shape and its cells' words,
	//    by Apply's table road (the round is done, so no table is added or taken away any more).
	std::vector<UIDRef> after;
	KCMTableRefsOfStory(targetStory, after);
	for (size_t k = 0; k < after.size(); ++k)
	{
		bool16 wasThere = kFalse;
		for (size_t b = 0; b < before.size() && !wasThere; ++b)
			if (before[b].GetUID() == after[k].GetUID())
				wasThere = kTrue;
		if (wasThere)
			continue;
		PMString why;
		const int32 done = KCMApplyTableFromImport(targetStory, wordStory, after[k].GetUID(), why);
		if (done > 0)
			total += done;
		else if (why.GetUTF8String().find("already reads") == std::string::npos)	// an empty table Word left empty is no failure
		{
			outWhy = why;
			outWhy.SetTranslatable(kFalse);
			return -1;
		}
	}
	return total;
}

// End, KCMApplyFromImport.cpp.
