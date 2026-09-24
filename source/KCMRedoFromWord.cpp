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
#include "KCMWordKeep.h"
#include "KCMRedoFromWord.h"

namespace
{

/** The characters of [at, at+len) - kFalse when the range is not inside the story. (KCMRestoreAttr.cpp keeps the
	same helper; a third user moves the two into a shared header.) */
bool16 WordsAt(ITextModel* model, TextIndex at, int32 len, WideString& out)
{
	out.Clear();
	if (model == nil || at < 0 || len < 0 || at + len > model->TotalLength())
		return kFalse;
	if (len > 0)
	{
		TextIterator iter(model, at);
		iter.AppendToStringAndIncrement(&out, len);
	}
	return kTrue;
}

void Refuse(PMString& why, const char* text)
{
	why = text;
	why.SetTranslatable(kFalse);
}

/** The paragraph of KCMTextRead's reading that holds `at` - or STARTS at `at`, for a caret on a boundary - as
	(Where, para) in that place's own numbering, which is what a plan's step names. kFalse outside the story. */
bool16 WhereParaOf(const std::vector<KCMParaAttrs>& attrs, const std::vector<int32>& starts,
				   const std::vector<std::string>& paras, TextIndex at, KCMStorySync::Where& outWhere, int32& outPara)
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

	// 2. the words at the record's place are still the Source's (design 15-1-8): a record whose place has been
	//    edited since would have the redo write over the wrong words
	InterfacePtr<ITextModel> target(targetStory, UseDefaultIID());
	InterfacePtr<ITextModel> source(sourceStory, UseDefaultIID());
	if (target == nil || source == nil)
	{
		Refuse(outWhy, "the story is not open on both sides");
		return kFalse;
	}
	WideString tWords, sWords;
	if (!WordsAt(target, record.fNowStart, record.fNowEnd - record.fNowStart, tWords)
		|| !WordsAt(source, record.fLive.fSourceStart, record.fLive.fSourceEnd - record.fLive.fSourceStart, sWords))
	{
		Refuse(outWhy, "the change's place reaches past the end of the story - compare again");
		return kFalse;
	}
	if (tWords != sWords)
	{
		Refuse(outWhy, "the words here were edited since - Ctrl+Z, or import again");
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
	if (!WhereParaOf(attrs, starts, paras, record.fNowStart, where, para))
	{
		Refuse(outWhy, "the change's paragraph could not be found - compare again");
		return kFalse;
	}

	// 4. the import's comparison, narrowed to that paragraph. ⚠reshapeTables kFalse, as the import passes it
	//    (design 11-1 item 5): a table whose shape Word changed is held, here as there.
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

// End, KCMRedoFromWord.cpp.
