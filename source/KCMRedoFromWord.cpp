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
#include "KCMParaText.h"		// ModelOffsetInParagraph / CountCodePoints - where a paragraph ends in the document's count
#include "KCMStorySync.h"		// Compare / Narrow
#include "KCMStorySyncApply.h"	// KCMApplySyncPlan / KCMSyncColumnsOfRow
#include "KCMStoryTextExport.h"	// KCMStoryFromDocument - the document, read the way the import reads it
#include "KCMTextRead.h"		// ReadStory - the paragraphs and their places, for the change's paragraph
#include "KCMTextWords.h"		// WordsAt / Refuse - shared with the restore
#include "KCMWordKeep.h"
#include "KCMRedoFromWord.h"

namespace
{

using KCMTextWords::WordsAt;
using KCMTextWords::Refuse;

/** The paragraph of KCMTextRead's reading that holds `at` - or STARTS at `at`, for a caret on a boundary - as
	(Where, para) in that place's own numbering, which is what a plan's step names. kFalse outside the story. */
bool16 WhereParaOf(const std::vector<KCMParaAttrs>& attrs, const std::vector<int32>& starts,
				   const std::vector<std::string>& paras, TextIndex at, KCMStorySync::Where& outWhere, int32& outPara,
				   int32& outIndex)
{
	int32 k = -1;
	for (size_t i = 0; i < starts.size() && i < attrs.size() && i < paras.size(); ++i)
	{
		// past the paragraph's words and its return
		const int32 end = starts[i] + KCMParaText::ModelOffsetInParagraph(attrs[i], KCMParaText::CountCodePoints(paras[i])) + 1;
		if (at >= starts[i] && at < end)
		{
			k = static_cast<int32>(i);
			break;
		}
	}
	if (k < 0)
		return kFalse;
	outIndex = k;
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
	outPara = 0;
	for (int32 i = 0; i < k; ++i)
	{
		const KCMParaAttrs& b = attrs[static_cast<size_t>(i)];
		if (b.fTableOrdinal == a.fTableOrdinal && b.fCellRow == a.fCellRow && b.fCellCol == a.fCellCol
			&& b.fFootnoteOrdinal == a.fFootnoteOrdinal)
			++outPara;
	}
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
	KCMStorySync::Where where;
	int32 para = 0;
	int32 index = 0;
	if (!WhereParaOf(attrs, starts, paras, record.fNowStart, where, para, index))
	{
		Refuse(outWhy, "the change's paragraph could not be found - compare again");
		return kFalse;
	}

	// ★THE SOURCE'S WORDS HAVE TO BE IN THAT PARAGRAPH (design 15-1-7 and 15-1-8): a hand edit INSIDE it is written
	//   over with Word's - "Word is the one that counts" - while an edit that moved the change's place into another
	//   paragraph (words typed above it) is refused rather than have Word's paragraph land on the wrong one.
	//   ⚠Measured 2026-09-24: asking for the words at the exact place refused every hand edit before the place,
	//    which is not what 15-1-7 promises. The characters the reader leaves out of a paragraph's text (a break, a
	//    table's own, a note's marker) are left out here too - a table row's words are its anchor alone, and that
	//    row is judged by its paragraph.
	{
		std::string words;
		for (int32 i = 0; i < static_cast<int32>(sWords.Length()); ++i)
		{
			const int32 cp = static_cast<int32>(sWords.GetChar(i).GetValue());
			if (cp >= 0x20 && cp != 0xFEFF && cp != 0xFFFC)
				KCMParaText::AppendUtf8(words, cp);
		}
		if (!words.empty() && paras[static_cast<size_t>(index)].find(words) == std::string::npos)
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
	KCMStorySync::Narrow(outNow, whole, where, para, outPlan);
	if (outPlan.fSteps.empty())
	{
		Refuse(outWhy, "nothing to redo here - the paragraph already reads as Word's");
		return kFalse;
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
	if (wentIn == 0)
	{
		if (!result.fNotes.empty())
			outWhy = result.fNotes[0].fWhy;
		else
			outWhy = "nothing was written";
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

int32 KCMRedoTableFromWord(const UIDRef& targetStory, UID tableUID, PMString& outWhy)
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
		KCMStorySync::Plan whole;
		KCMStorySync::Compare(now, word, whole, kTrue);
		if (whole.fStoryHeld)
		{
			outWhy.SetUTF8String(whole.fWhy);
			outWhy.SetTranslatable(kFalse);
			return -1;
		}
		if (!whole.IsShapeRound())
			break;
		KCMStorySync::Plan mine;
		StepsOfTable(whole, ordinal, kTrue, mine);
		if (mine.fSteps.empty())
			break;			// the round is about OTHER tables (ones the reader matched too): this table's shape is Word's
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
		if (total > 0)
			return total;	// the shape was the whole difference
		Refuse(outWhy, "nothing to redo here - the table already reads as Word's");
		return -1;
	}
	KCMSyncResult result;
	KCMApplySyncPlan(targetStory, now, mine, result);
	total += result.fWrites + result.fAttrWrites + result.fNoteEdits + result.fTableEdits;
	if (total == 0)
	{
		outWhy = result.fNotes.empty() ? PMString("nothing was written") : result.fNotes[0].fWhy;
		outWhy.SetTranslatable(kFalse);
		return -1;
	}
	return total;
}

// End, KCMRedoFromWord.cpp.
