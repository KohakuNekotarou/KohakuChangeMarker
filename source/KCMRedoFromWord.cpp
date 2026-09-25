//========================================================================================
//
//  KCMRedoFromWord.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// SaveRestoreModifiedState - reading must not dirty either document
#include "ITextModel.h"

// General includes:
#include "TextIterator.h"		// the words at the record's place, both sides
#include "UIDRef.h"
#include "WideString.h"

// Project includes:
#include "KCMImportTracking.h"	// KCMImportAuthor / KCMStoryTrackingOn - the import's signature
#include "KCMParaText.h"		// AppendUtf8 - the Source's words in the reading's own UTF-8
#include "KCMStorySync.h"		// Compare / Narrow
#include "KCMStorySyncApply.h"	// KCMApplySyncPlan / KCMSyncColumnsOfRow
#include "KCMStoryTextExport.h"	// KCMStoryFromDocument - the document, read the way the import reads it
#include "KCMTextRead.h"		// ReadStory - the paragraphs and their places, for the change's paragraph
#include "KCMTextWords.h"		// WordsAt / Refuse - shared with the restore
#include "KCMWordKeep.h"
#include "KCMRedoPlace.h"		// KCMRedoPlace::Of - which paragraph the change is (2026-09-25)
#include "KCMTableShape.h"		// KCMReadTableShapes - where each table's anchor stands, for a Table − redo (2026-09-25)
#include "KCMRedoFromWord.h"

namespace
{

using KCMTextWords::WordsAt;
using KCMTextWords::Refuse;

/** The place reading paragraph `k` stands in, as a plan's step names it: the body, a note, or a cell by its index
	among its row's columns. kFalse for a paragraph out of range, or a cell whose column is not among its row's.
	★THE PARAGRAPH'S NUMBER IN THAT PLACE IS KCMRedoPlace's TO SAY (2026-09-25): this answered both until then, from
	 the paragraph holding the record's start - which for a whole paragraph cut with the break before it is the
	 paragraph BEFORE the change (KCMRedoPlace.h says what that broke). */
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

}	// namespace

//----------------------------------------------------------------------------------------
// KCMPlanRedoFromWord
//----------------------------------------------------------------------------------------

bool16 KCMPlanRedoFromWord(const UIDRef& targetStory, const UIDRef& sourceStory, const KCMRejectedRecord& record,
						   KCMStoryShape::Story& outNow, KCMStorySync::Plan& outPlan, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	outPlan = KCMStorySync::Plan();

	// 1. Word's content, kept by the import (design 15-1-5: asked here, and said out loud when it is gone)
	KCMStoryShape::Story word;
	if (!KCMWordKeepGet(targetStory.GetDataBase(), targetStory.GetUID(), word))
	{
		Refuse(outWhy, "the Word content is not in memory any more - the document was closed or InDesign restarted; import again");
		return kFalse;
	}

	// 2. the record's place is still inside the story, and the Source's words it took back are read for step 3
	InterfacePtr<ITextModel> target(targetStory, UseDefaultIID());
	InterfacePtr<ITextModel> source(sourceStory, UseDefaultIID());
	if (target == nil || source == nil)
	{
		Refuse(outWhy, "the story is not open on both sides");
		return kFalse;
	}
	WideString sWords;
	if (record.fNowStart < 0 || record.fNowEnd < record.fNowStart || record.fNowEnd > target->TotalLength()
		|| !WordsAt(source, record.fLive.fSourceStart, record.fLive.fSourceEnd - record.fLive.fSourceStart, sWords))
	{
		Refuse(outWhy, "the change's place reaches past the end of the story - compare again");
		return kFalse;
	}

	// 3. the document now, the way the import reads it, and the change's paragraph
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
	// ★★THE CHANGE'S PARAGRAPH, NOT THE ONE HOLDING THE RECORD'S START (2026-09-25, the user's report - KCMRedoPlace.h):
	//   a whole paragraph cut with the break before it ("\rNEW") starts ON the previous paragraph's return, so its
	//   own paragraph is the next one in the same place.
	const bool16 afterBreak = (record.fLive.fWholeParagraph && record.fLive.fBreakAt == kKCMBreakLeads) ? kTrue : kFalse;
	KCMRedoPlace::Para place;
	KCMStorySync::Where where;
	if (!KCMRedoPlace::Of(paras, starts, attrs, record.fNowStart, afterBreak, place) || !WhereOf(attrs, place.fHold, where))
	{
		Refuse(outWhy, "the change's paragraph could not be found - compare again");
		return kFalse;
	}
	const int32 para = place.fNumber;

	// ★THE SOURCE'S WORDS HAVE TO BE IN THAT PARAGRAPH (design 15-1-7 and 15-1-8): a hand edit INSIDE it is written
	//   over with Word's - "Word is the one that counts" - while an edit that moved the change's place into another
	//   paragraph (words typed above it) is refused rather than have Word's paragraph land on the wrong one.
	//   ⚠Measured 2026-09-24: asking for the words at the exact place refused every hand edit before the place,
	//    which is not what 15-1-7 promises. The characters the reader leaves out of a paragraph's text (a break, a
	//    table's own, a note's marker) are left out here too - a table row's words are its anchor alone, and that
	//    row is judged by its paragraph.
	//   ★IN THE CHANGE'S OWN PARAGRAPH (2026-09-25): a paragraph removed in Word and taken back stands AFTER the one
	//    holding the record's start. An insertion has no Source words to look for.
	{
		std::string words;
		for (int32 i = 0; i < static_cast<int32>(sWords.Length()); ++i)
		{
			const int32 cp = static_cast<int32>(sWords.GetChar(i).GetValue());
			if (cp >= 0x20 && cp != 0xFEFF && cp != 0xFFFC)
				KCMParaText::AppendUtf8(words, cp);
		}
		if (!words.empty()
			&& (place.fOwn < 0 || paras[static_cast<size_t>(place.fOwn)].find(words) == std::string::npos))
		{
			Refuse(outWhy, "the Source's words are not in this paragraph any more - Ctrl+Z, or import again");
			return kFalse;
		}
	}

	// 4. the import's comparison, narrowed to that paragraph. ⚠reshapeTables kFalse: a redo writes ONE paragraph
	//    and reshapes no table - a table whose shape differs from Word's is held here (the import, which passes
	//    kTrue again since the evening of 2026-09-24, would have made it Word's; import again for that).
	KCMStorySync::Plan whole;
	KCMStorySync::Compare(outNow, word, whole, kFalse);
	if (whole.fStoryHeld)
	{
		outWhy.SetUTF8String(whole.fWhy);
		outWhy.SetTranslatable(kFalse);
		return kFalse;
	}
	// ★A TABLE WORD ADDED OR TOOK AWAY STANDS TAKEN BACK IN THIS STORY (2026-09-25): the comparison then answers with
	//   those tables alone (stage 0 - the words are only compared once the tables agree), and narrowing that to a
	//   paragraph found nothing - "nothing to redo here" was said of a paragraph that plainly differed. Named instead.
	//   (A record of the table itself goes to KCMRedoTableAddedOrTaken, which the facade asks first.)
	if (whole.Count(KCMStorySync::Step::kInsertTable) + whole.Count(KCMStorySync::Step::kDeleteTable) > 0)
	{
		Refuse(outWhy, "a table Word added or took away stands taken back in this story - redo that table's row first");
		return kFalse;
	}
	// ★THE ROW'S OWN KIND OF CHANGE, AND NOTHING NEXT TO IT (2026-09-25 - KCMStorySync::NarrowScope): a paragraph added
	//   in Word is redone as that addition, one taken away as that removal, anything else as the paragraph's own edit.
	const KCMStorySync::NarrowScope scope = !record.fLive.fWholeParagraph ? KCMStorySync::kNarrowOwn
		: (record.fLive.fKind == KCMStoryChange::kInsert) ? KCMStorySync::kNarrowInserted
		: (record.fLive.fKind == KCMStoryChange::kDelete) ? KCMStorySync::kNarrowRemoved
		: KCMStorySync::kNarrowOwn;
	KCMStorySync::Narrow(outNow, whole, where, para, scope, outPlan);
	if (outPlan.fSteps.empty())
	{
		Refuse(outWhy, "nothing to redo here - the paragraph already reads as Word's");
		return kFalse;
	}
	// ★ALL OF IT OR NONE OF IT (2026-09-25 - the user's rule for every take-back, now for the redo too): a paragraph
	//   the comparison HOLDS (a table whose shape differs from Word's, a place that cannot be paired) cannot be made
	//   Word's, and writing the rest of it would leave the paragraph half redone. ⚠A tate-chu-yoko kept under a
	//   warichu is held ON PURPOSE (Word cannot carry it) and does not count.
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

//----------------------------------------------------------------------------------------
// KCMApplyRedoFromWord
//----------------------------------------------------------------------------------------

int32 KCMApplyRedoFromWord(const UIDRef& targetStory, const KCMStoryShape::Story& now, const KCMStorySync::Plan& plan,
						   PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	KCMImportAuthor author;						// signed KohakuChangeMarker, and the name put back when this returns
	KCMStoryTrackingOn tracking(targetStory);	// recorded, so that it can be rejected again
	KCMSyncResult result;
	KCMApplySyncPlan(targetStory, now, plan, result);
	const int32 wentIn = result.fWrites + result.fAttrWrites + result.fNoteEdits + result.fTableEdits;
	// ★★A WRITE REFUSED HALFWAY IS A FAILED REDO (2026-09-25): until then only "nothing went in" was one, so a
	//   paragraph half written stood as redone. -1 now, and the caller rolls the whole sequence back
	//   (KCMFacades RedoFromWord - the same rule as every take-back: all of it, or none of it). What was only HELD
	//   BACK on purpose (a note with fHeldBack) is not a failure.
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
// KCMRedoTableFromWord (2026-09-25 - "Match the Source", design section 16-1 item 7)
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

/** KCMRedoTableFromWord's body. `nothingIsDone`: a table that already reads as Word's answers 0 rather than a refusal
	- for a table the redo has just PUT IN (KCMRedoTableAddedOrTaken), where Word's cells may all be empty. */
static int32 RedoOneTable(const UIDRef& targetStory, UID tableUID, bool16 nothingIsDone, PMString& outWhy);

int32 KCMRedoTableFromWord(const UIDRef& targetStory, UID tableUID, PMString& outWhy)
{
	return RedoOneTable(targetStory, tableUID, kFalse, outWhy);
}

static int32 RedoOneTable(const UIDRef& targetStory, UID tableUID, bool16 nothingIsDone, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);

	// 1. Word's content, kept by the import (design 15-1-5)
	KCMStoryShape::Story word;
	if (!KCMWordKeepGet(targetStory.GetDataBase(), targetStory.GetUID(), word))
	{
		Refuse(outWhy, "the Word content is not in memory any more - the document was closed or InDesign restarted; import again");
		return -1;
	}

	KCMImportAuthor author;						// signed KohakuChangeMarker, and the name put back when this returns
	KCMStoryTrackingOn tracking(targetStory);	// recorded, so that it can be rejected again
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
		//   stage among ALL the tables, and another table the reader matched too could hold this table's stage back -
		//   its steps then never came, the loop left early and the redo refused with the table half Word's.
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
		// ★★A TABLE ADDED OR TAKEN AWAY SINCE THE IMPORT (stage 0) IS NOT THIS REDO'S (re-check of 2026-09-25): the
		//   story no longer has the import's tables, and a kDeleteTable names its table the way a cell step does
		//   (Where::Cell(t, -1, -1)) - it would have passed StepsOfTable and TAKEN THIS TABLE AWAY.
		if (whole.Count(KCMStorySync::Step::kInsertTable) + whole.Count(KCMStorySync::Step::kDeleteTable) > 0)
		{
			Refuse(outWhy, "a table was added to or taken from this story since the import - import again");
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

	// 3. the words, marks and notes of its cells, once the shape is Word's. ⚠reshapeTables kFalse: another table the
	//    reader matched is HELD here rather than reshaped (this redo is about one table), and this table - Word's
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
		if (total > 0 || nothingIsDone)
			return total;	// the shape was the whole difference (or the table was just put in, and Word's cells are empty)
		Refuse(outWhy, "nothing to redo here - the table already reads as Word's");
		return -1;
	}
	KCMSyncResult result;
	KCMApplySyncPlan(targetStory, now, mine, result);
	total += result.fWrites + result.fAttrWrites + result.fNoteEdits + result.fTableEdits;
	// ★A CELL REFUSED HALFWAY FAILS THE REDO TOO (2026-09-25 - the paragraph redo's rule): the caller rolls it all back.
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
// KCMRedoTableAddedOrTaken (2026-09-25)
//----------------------------------------------------------------------------------------

int32 KCMRedoTableAddedOrTaken(const UIDRef& targetStory, const KCMRejectedRecord& record, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);

	// 1. Word's content, kept by the import (design 15-1-5)
	KCMStoryShape::Story word;
	if (!KCMWordKeepGet(targetStory.GetDataBase(), targetStory.GetUID(), word))
	{
		Refuse(outWhy, "the Word content is not in memory any more - the document was closed or InDesign restarted; import again");
		return -1;
	}

	// 2. the document now against Word: stage 0 of the comparison names the tables Word added and took away, and ONLY
	//    those (Compare answers with them alone while the story's tables do not pair) - what the import's first round is
	KCMStoryShape::Story now;
	bool16 placed = kTrue;
	if (!KCMStoryFromDocument(targetStory, now, placed) || !placed)
	{
		Refuse(outWhy, "the story could not be read the way the import reads it");
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
	// Table + (the import added it; the reject took it away): Word's table goes back in. Table − (the import took it
	// away; the reject brought it back): it goes again.
	const bool16 added = (record.fLive.fKind == KCMStoryChange::kInsert) ? kTrue : kFalse;
	std::vector<int32> candidates;
	for (size_t i = 0; i < whole.fSteps.size(); ++i)
		if (whole.fSteps[i].fKind == (added ? KCMStorySync::Step::kInsertTable : KCMStorySync::Step::kDeleteTable))
			candidates.push_back(static_cast<int32>(i));
	if (candidates.empty())
	{
		Refuse(outWhy, "nothing to redo here - the story's tables already read as Word's");
		return -1;
	}

	// 3. WHICH of them is this record's
	int32 chosen = -1;
	if (added)
	{
		// ★By where it stood: the table goes in after the paragraph the record's place names (KCMRedoPlace - a table
		//   in a paragraph of its own is cut with the break before it, as a paragraph is). One table to put back is
		//   that table whatever the place says.
		if (candidates.size() == 1)
			chosen = candidates[0];
		else
		{
			std::vector<std::string> paras;
			std::vector<KCMParaAttrs> attrs;
			std::vector<int32> starts;
			{
				IDataBase::SaveRestoreModifiedState guard(targetStory.GetDataBase());
				if (!KCMTextRead::ReadStory(targetStory, paras, attrs, starts))
				{
					Refuse(outWhy, "the story could not be read");
					return -1;
				}
			}
			const bool16 afterBreak = (record.fLive.fWholeParagraph && record.fLive.fBreakAt == kKCMBreakLeads) ? kTrue : kFalse;
			KCMRedoPlace::Para place;
			if (KCMRedoPlace::Of(paras, starts, attrs, record.fNowStart, afterBreak, place))
			{
				const int32 after = afterBreak ? place.fNumber - 1 : place.fNumber;
				for (size_t c = 0; c < candidates.size() && chosen < 0; ++c)
					if (whole.fSteps[static_cast<size_t>(candidates[c])].fPara == after)
						chosen = candidates[c];
			}
		}
	}
	else
	{
		// ★By the table standing where the record's Source words - its anchor - came back, and asked twice: the reading's
		//   ordinal (what the step names) has to be the same table as the story's own list (what the write deletes), or
		//   nothing is deleted. A reject brings a table back under a NEW id (measured 2026-09-25), so no id kept from
		//   before can be what finds it.
		InterfacePtr<ITextModel> model(targetStory, UseDefaultIID());
		std::vector<KCMTableShape> shapes;
		std::vector<UIDRef> refs;
		{
			IDataBase::SaveRestoreModifiedState guard(targetStory.GetDataBase());
			if (model == nil || !KCMReadTableShapes(model, shapes) || !KCMTableRefsOfStory(targetStory, refs))
			{
				Refuse(outWhy, "the story's tables could not be read");
				return -1;
			}
		}
		const TextIndex lo = record.fNowStart;
		const TextIndex hi = (record.fNowEnd > lo) ? record.fNowEnd : lo + 1;
		int32 ordinal = -1;
		for (size_t k = 0; k < shapes.size() && ordinal < 0; ++k)
			if (shapes[k].fAnchorStart >= lo && shapes[k].fAnchorStart < hi)
				ordinal = static_cast<int32>(k);
		if (ordinal < 0 || static_cast<size_t>(ordinal) >= refs.size()
			|| refs[static_cast<size_t>(ordinal)].GetUID() != shapes[static_cast<size_t>(ordinal)].fDictUID)
		{
			Refuse(outWhy, "the table could not be found where it was taken back - compare again");
			return -1;
		}
		for (size_t c = 0; c < candidates.size() && chosen < 0; ++c)
			if (whole.fSteps[static_cast<size_t>(candidates[c])].fWhere.fTable == ordinal)
				chosen = candidates[c];
		if (chosen < 0)
		{
			Refuse(outWhy, "nothing to redo here - Word's version keeps this table");
			return -1;
		}
	}
	if (chosen < 0)
	{
		Refuse(outWhy, "which of Word's tables goes here could not be told - import again");
		return -1;
	}

	// 4. WRITTEN AS THE IMPORT WRITES IT (KCMApplyTableShape - the table in a paragraph of its own, or the table and
	//    all it holds taken away), under the import's signature, so it can be rejected again
	KCMStorySync::Plan one;
	one.fSteps.push_back(whole.fSteps[static_cast<size_t>(chosen)]);
	KCMImportAuthor author;
	KCMStoryTrackingOn tracking(targetStory);
	std::vector<UIDRef> before;
	KCMTableRefsOfStory(targetStory, before);
	KCMSyncResult shape;
	KCMApplyTableShape(targetStory, one, shape);
	if (shape.fRefused > 0 || shape.fTableEdits == 0)
	{
		outWhy = shape.fNotes.empty() ? PMString(added ? "Word's table could not be put back in" : "the table could not be taken away again")
									  : shape.fNotes[0].fWhy;
		outWhy.SetTranslatable(kFalse);
		return -1;
	}
	if (!added)
		return shape.fTableEdits;

	// 5. the table put in is made Word's - its merges and its cells' words - by the table redo (this table alone)
	std::vector<UIDRef> after;
	KCMTableRefsOfStory(targetStory, after);
	UID fresh = kInvalidUID;
	for (size_t a = 0; a < after.size() && fresh == kInvalidUID; ++a)
	{
		bool16 old = kFalse;
		for (size_t b = 0; b < before.size() && !old; ++b)
			old = (before[b].GetUID() == after[a].GetUID()) ? kTrue : kFalse;
		if (!old)
			fresh = after[a].GetUID();
	}
	if (fresh == kInvalidUID)
	{
		Refuse(outWhy, "the table put back in could not be found again");
		return -1;
	}
	const int32 filled = RedoOneTable(targetStory, fresh, kTrue, outWhy);
	if (filled < 0)
		return -1;
	return shape.fTableEdits + filled;
}

// End, KCMRedoFromWord.cpp.
