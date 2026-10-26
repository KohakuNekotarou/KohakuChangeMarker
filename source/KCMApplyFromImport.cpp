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

/** The steps of `plan` about table `ordinal` - its shape moves when `shape`, otherwise its cells' words, marks and
	notes (a note step stands where its reference is: the cell). ★Every step of every cell of the table is kept, so
	the numbering inside a cell is the whole plan's; only other places are left out, and a place is a thread of its
	own. */
void StepsOfTable(const KCMStorySync::Plan& plan, int32 ordinal, bool16 shape, KCMStorySync::Plan& out)
{
	out = KCMStorySync::Plan();
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const KCMStorySync::Step& s = plan.fSteps[i];
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
		StepsOfTable(whole, ordinal, kTrue, mine);
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
	StepsOfTable(whole, ordinal, kFalse, mine);
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

// End, KCMApplyFromImport.cpp.
