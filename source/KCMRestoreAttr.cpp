//========================================================================================
//
//  KCMRestoreAttr.cpp -- see the header.
//
//  What needs the SDK is here: reading the two stories, crossing a span's text offset into the document's
//  count, comparing the characters, and the writers. The window itself is KCMAttrRestorePlan.h (pure).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// SaveRestoreModifiedState - reading must not dirty either document
#include "IKentenStyle.h"		// IKentenStyle::Kenten_None / Kenten_Custom
#include "ITextModel.h"

// General includes:
#include "ErrorUtils.h"
#include "TextIterator.h"		// the characters under the window, both sides
#include "UIDRef.h"
#include "WideString.h"

// Project includes:
#include "KCMParaText.h"		// ModelOffsetInParagraph - the one crossing from the text's count into the document's
#include "KCMStoryKinds.h"		// KCMStoryAttrKind
#include "KCMStoryRestore.h"	// the writers, shared with the import's pour and the PDF report
#include "KCMTextRead.h"		// ReadStory - the same reader the comparison runs
#include "KCMRestoreAttr.h"

namespace
{

/** The list one kind lives in on a paragraph (nil for a kind that is not written back). */
const KCMAttrSpanList* SpansOfKind(const KCMParaAttrs& attrs, int32 kind)
{
	switch (kind)
	{
		case kKCMStoryAttrRuby:    return &attrs.fRuby;
		case kKCMStoryAttrKenten:  return &attrs.fKenten;
		case kKCMStoryAttrWarichu: return &attrs.fWarichu;
		case kKCMStoryAttrTcy:     return &attrs.fTcy;
		default:                   return nil;
	}
}

/** Every mark of `kind` in one story, as DOCUMENT positions.
	★THE CROSSING IS ModelOffsetInParagraph's, BOTH ENDS ASKED SEPARATELY (AddAttrChange and KCMStoryAttrPour's
	  ModelRangeOf do the same): a span reaching across a table's own character covers one fewer character of text
	  than of model, so `start + len` would be a length in the wrong count. */
void CollectPieces(const std::vector<KCMParaAttrs>& attrs, const std::vector<int32>& starts, int32 kind,
				   std::vector<KCMAttrPiece>& out)
{
	out.clear();
	for (size_t p = 0; p < attrs.size() && p < starts.size(); ++p)
	{
		const KCMAttrSpanList* const spans = SpansOfKind(attrs[p], kind);
		if (spans == nil)
			return;
		for (size_t i = 0; i < spans->size(); ++i)
		{
			const KCMAttrSpan& s = (*spans)[i];
			const int32 from = starts[p] + KCMParaText::ModelOffsetInParagraph(attrs[p], s.fStart);
			const int32 to   = starts[p] + KCMParaText::ModelOffsetInParagraph(attrs[p], s.fStart + s.fLen);
			if (to > from)
				out.push_back(KCMAttrPiece(from, to, s.fValue, s.fGroup));
		}
	}
}

/** The characters of [at, at+len) - kFalse when the range is not inside the story. */
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

/** A span's value as a PMString. ⚠THE VALUE IS UTF-8 AND PMString IS NOT: Append(c_str()) would put the bytes in
	as the platform's encoding, and a custom kenten's own character came out as mojibake that way once
	(KCMStoryAttrPour measured it). SetUTF8String also marks the string non-translatable. */
PMString Utf8(const std::string& text)
{
	PMString s;
	s.SetUTF8String(text);
	return s;
}

void Refuse(PMString& why, const char* text)
{
	why = text;
	why.SetTranslatable(kFalse);
}

}	// namespace

//----------------------------------------------------------------------------------------
// KCMPlanRestoreAttrFromSource
//----------------------------------------------------------------------------------------

bool16 KCMPlanRestoreAttrFromSource(const UIDRef& targetStory, const UIDRef& sourceStory, int32 kind,
									TextIndex tFrom, TextIndex tTo, TextIndex sFrom, TextIndex sTo,
									KCMAttrRestoreJob& outJob, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	outJob = KCMAttrRestoreJob();
	outJob.fKind = kind;
	if (kind != kKCMStoryAttrRuby && kind != kKCMStoryAttrKenten
		&& kind != kKCMStoryAttrWarichu && kind != kKCMStoryAttrTcy)
	{
		Refuse(outWhy, "this kind of change is not one that is restored (a footnote or endnote is a character: fix the words)");
		return kFalse;
	}

	InterfacePtr<ITextModel> target(targetStory, UseDefaultIID());
	InterfacePtr<ITextModel> source(sourceStory, UseDefaultIID());
	if (target == nil || source == nil)
	{
		Refuse(outWhy, "the story is not open on both sides");
		return kFalse;
	}

	std::vector<std::string> tParas, sParas;
	std::vector<KCMParaAttrs> tAttrs, sAttrs;
	std::vector<int32> tStarts, sStarts;
	{
		// READING MUST NOT DIRTY EITHER DOCUMENT (KCMTextRead.h): both guards, both reads inside them, and nothing
		// written in this function. ⚠The apply runs OUTSIDE any guard - a guard around a real write would put the
		// clean flag back over a document that has changed.
		IDataBase::SaveRestoreModifiedState targetGuard(targetStory.GetDataBase());
		IDataBase::SaveRestoreModifiedState sourceGuard(sourceStory.GetDataBase());
		if (!KCMTextRead::ReadStory(targetStory, tParas, tAttrs, tStarts)
			|| !KCMTextRead::ReadStory(sourceStory, sParas, sAttrs, sStarts))
		{
			Refuse(outWhy, "the story could not be read");
			return kFalse;
		}
	}

	std::vector<KCMAttrPiece> tPieces, sPieces;
	CollectPieces(tAttrs, tStarts, kind, tPieces);
	CollectPieces(sAttrs, sStarts, kind, sPieces);
	// ★THE PLAN'S ONE REFUSAL IS AN EMPTY SIDE, and an empty side is the diff saying THE WORDS DIFFER AS WELL
	//   (CompareParagraphAttr's textDiffered hands the other side the paragraph's start and no characters) - then no
	//   position over there names these characters. One test, one message, one place.
	if (!KCMPlanAttrRestore(tPieces, sPieces, tFrom, tTo, sFrom, sTo, outJob.fPlan))
	{
		Refuse(outWhy, "the words of this paragraph differ as well - fix the words first (after an import: Reject This Import Change), then restore this");
		return kFalse;
	}

	// ★THE CHARACTERS UNDER THE WINDOW HAVE TO BE THE SOURCE'S (design 14-1 item 4): a mark is put on whatever
	//   stands there, and putting the Source's reading over different words would be a silent wrong answer.
	const KCMAttrRestorePlan& plan = outJob.fPlan;
	WideString tWords, sWords;
	if (!WordsAt(target, plan.fTargetFrom, plan.fTargetTo - plan.fTargetFrom, tWords)
		|| !WordsAt(source, plan.fSourceFrom, plan.fSourceTo - plan.fSourceFrom, sWords))
	{
		Refuse(outWhy, "the range reaches past the end of the story - compare again");
		return kFalse;
	}
	if (tWords != sWords)
	{
		Refuse(outWhy, "the characters under this mark are not the Source's - fix the words first (after an import: Reject This Import Change), then restore this");
		return kFalse;
	}

	// A kenten of the Source has to be a kind this build can write - judged before anything is written, so a
	// refusal is whole rather than half marked (the same discipline as the import's pour).
	if (kind == kKCMStoryAttrKenten)
	{
		for (size_t i = 0; i < plan.fWrites.size(); ++i)
		{
			int16 k = 0;
			int16 c = 0;
			const PMString v = Utf8(plan.fWrites[i].fValue);
			if (!KCMKentenKindOf(v, k) && !KCMKentenCustomCharOf(v, c))
			{
				outWhy = "a kenten mark of the Source cannot be written back (\"";
				outWhy.Append(v);
				outWhy.Append("\")");
				return kFalse;
			}
		}
	}

	outJob.fCharacters = plan.fTargetTo - plan.fTargetFrom;
	return kTrue;
}

//----------------------------------------------------------------------------------------
// KCMApplyRestoreAttr
//----------------------------------------------------------------------------------------

int32 KCMApplyRestoreAttr(const UIDRef& targetStory, const KCMAttrRestoreJob& job, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	InterfacePtr<ITextModel> model(targetStory, UseDefaultIID());
	if (model == nil)
	{
		Refuse(outWhy, "the story is not open");
		return -1;
	}
	const KCMAttrRestorePlan& plan = job.fPlan;
	const TextIndex at = plan.fTargetFrom;
	const int32 len = plan.fTargetTo - plan.fTargetFrom;
	if (len <= 0)
		return 0;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	// ---- OFF over the whole window - its own marks included, they may differ in length from the Source's ----
	ErrorCode err = kFailure;
	switch (job.fKind)
	{
		case kKCMStoryAttrRuby:    err = KCMClearRuby(model, at, len); break;
		case kKCMStoryAttrKenten:  err = KCMApplyKentenKind(model, at, len, IKentenStyle::Kenten_None); break;
		case kKCMStoryAttrWarichu: err = KCMApplyWarichu(model, at, len, kFalse); break;
		case kKCMStoryAttrTcy:     err = KCMApplyTcy(model, at, len, kFalse); break;
		default:
			Refuse(outWhy, "not a kind that is restored");
			return -1;
	}
	if (err != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		Refuse(outWhy, "the mark could not be taken off (a locked story or layer?)");
		return -1;
	}
	if (job.fKind == kKCMStoryAttrRuby && !plan.fWrites.empty() && KCMCreateRubyStrandIfNeeded(model) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		Refuse(outWhy, "this story cannot be given ruby (its ruby strand could not be made)");
		return -1;
	}

	// ---- and the Source's marks ON, at Target positions ----
	int32 written = 0;
	for (size_t i = 0; i < plan.fWrites.size(); ++i)
	{
		const KCMAttrPiece& w = plan.fWrites[i];
		const int32 l = w.fEnd - w.fStart;
		if (l <= 0)
			continue;
		err = kFailure;
		switch (job.fKind)
		{
			case kKCMStoryAttrRuby:
				err = KCMApplyRuby(model, w.fStart, l, Utf8(w.fValue), w.fGroup);
				break;
			case kKCMStoryAttrKenten:
			{
				int16 k = IKentenStyle::Kenten_None;
				int16 c = 0;
				const PMString v = Utf8(w.fValue);
				if (!KCMKentenKindOf(v, k) && KCMKentenCustomCharOf(v, c))
					k = IKentenStyle::Kenten_Custom;		// the plan already found one of the two answers
				err = KCMApplyKentenKind(model, w.fStart, l, k, c);
				break;
			}
			case kKCMStoryAttrWarichu: err = KCMApplyWarichu(model, w.fStart, l, kTrue); break;
			case kKCMStoryAttrTcy:     err = KCMApplyTcy(model, w.fStart, l, kTrue); break;
			default: break;
		}
		if (err != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			Refuse(outWhy, "a mark could not be written (a locked story or layer?) - Ctrl+Z takes back what went in");
			return -1;
		}
		++written;
	}
	return written;
}

// End, KCMRestoreAttr.cpp.
