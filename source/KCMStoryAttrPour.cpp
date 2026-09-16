//========================================================================================
//
//  KCMStoryAttrPour.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <string>
#include <vector>

#include "IKentenStyle.h"		// IKentenStyle::Kenten_None - "no mark"
#include "ITextModel.h"
#include "ErrorUtils.h"

#include "KCMStoryAttrPour.h"
#include "KCMParaText.h"		// PlanSpanChanges / ModelOffsetInParagraph - the pure half
#include "KCMStoryRestore.h"	// the writers, shared with the restore and the PDF report

namespace
{

/** A span's place in THE DOCUMENT'S count: where it starts, and how long it is there.

	★**THE CROSSING IS ModelOffsetInParagraph's, never an addition of our own** - the same rule
	  ApplyParagraph keeps. A table standing inside this paragraph makes the text's count and the
	  document's part company from there on (KCMParaText.h).
	⚠**A RANGE ENDING EXACTLY WHERE A TABLE STANDS COMES BACK ONE POSITION WIDE**, because that
	 function answers for a START (its own header says so, and says the widening was chosen
	 deliberately). What that costs HERE is an attribute also landing on the table's own character
	 - which draws nothing, is taken back out by KCMTextRead when the story is read again, and
	 happens in the COPY, never in the reader's document. Measured cost: none visible. It is
	 written down rather than guarded because a guard would refuse a paragraph nobody can see a
	 fault in. */
void ModelRangeOf(const KCMParaAttrs& attrs, const KCMAttrSpan& span, TextIndex paraStart,
				  TextIndex& outAt, int32& outLen)
{
	const int32 from = KCMParaText::ModelOffsetInParagraph(attrs, span.fStart);
	const int32 to = KCMParaText::ModelOffsetInParagraph(attrs, span.fStart + span.fLen);
	outAt = paraStart + from;
	outLen = to - from;
}

/** kTrue when `applies` holds a span standing on exactly the same characters.

	★★★**THIS IS WHAT KEEPS A READING'S LOOK WHEN ONLY ITS TEXT CHANGED.** A reading replaced in
	  place needs no clearing: writing the three attributes that ARE a reading over it replaces
	  them and leaves the twenty-seven that are its look exactly as the reader set them. Clearing
	  first would take the size, the colour and the position off a reading the reader never
	  touched the look of - and they would never come back, because the file does not carry a
	  look at all.
	⚠A span whose base GREW or SHRANK has no such counterpart and is cleared: there is no honest
	 way to keep a look that stood over different characters. */
bool16 StandsOnTheSameCharacters(const KCMAttrSpanList& applies, const KCMAttrSpan& span)
{
	for (size_t i = 0; i < applies.size(); ++i)
	{
		if (applies[i].fStart == span.fStart && applies[i].fLen == span.fLen)
			return kTrue;
	}
	return kFalse;
}

PMString Utf8(const std::string& text)
{
	PMString s;
	s.SetUTF8String(text);		// marks it not translatable, which is what we want
	return s;
}

}	// anonymous namespace

int32 KCMPourParagraphAttributes(ITextModel* model, TextIndex paraStart,
								 const KCMParaAttrs& docAttrs, const std::string& docText,
								 const KCMStoryHtml::Para& file,
								 PMString& whyNot, bool16& outRefused)
{
	outRefused = kFalse;
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	if (model == nil)
		return 0;

	KCMAttrSpanList clearRuby;
	KCMAttrSpanList applyRuby;
	KCMAttrSpanList clearKenten;
	KCMAttrSpanList applyKenten;
	KCMAttrSpanList clearTcy;
	KCMAttrSpanList applyTcy;
	KCMParaText::PlanSpanChanges(docAttrs.fRuby, file.fRuby, clearRuby, applyRuby);
	KCMParaText::PlanSpanChanges(docAttrs.fKenten, file.fKenten, clearKenten, applyKenten);
	// ★TATE-CHU-YOKO TOO (2026-09-17). Its value is its characters on both sides - KCMTextRead and
	//   KCMStoryHtml's reader settle it the same way - so a stretch that did not change pairs off and
	//   nothing is written, exactly as for a reading.
	KCMParaText::PlanSpanChanges(docAttrs.fTcy, file.fTcy, clearTcy, applyTcy);
	// ★AND WARICHU (2026-09-17), on exactly the same terms: its value is its characters on both sides.
	KCMAttrSpanList clearWarichu;
	KCMAttrSpanList applyWarichu;
	KCMParaText::PlanSpanChanges(docAttrs.fWarichu, file.fWarichu, clearWarichu, applyWarichu);

	// ★THE ORDINARY ANSWER, and the one worth being fast and silent about: the reader edited a
	//   word somewhere else and every reading in this paragraph is where it was.
	if (clearRuby.empty() && applyRuby.empty() && clearKenten.empty() && applyKenten.empty()
		&& clearTcy.empty() && applyTcy.empty() && clearWarichu.empty() && applyWarichu.empty())
		return 0;

	// ★★★**THE WORDS FIRST.** Both sides count in the paragraph's own text, so the offsets mean
	//   the same characters exactly when the two texts are the same text (the file header says why
	//   this is the enabling condition rather than a precaution).
	if (docText != file.fText)
	{
		whyNot = "the words of this paragraph did not go in, so its ruby, kenten, tate-chu-yoko and "
				 "warichu were left alone (the words go first)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	// ⚠**NOTHING IS WRITTEN UNTIL EVERY KIND HAS BEEN JUDGED**, the same discipline ApplyParagraph
	//   keeps: a paragraph turned away is turned away whole, rather than half marked.
	std::vector<int16> kentenKinds(applyKenten.size(), IKentenStyle::Kenten_None);
	for (size_t i = 0; i < applyKenten.size(); ++i)
	{
		int16 kind = IKentenStyle::Kenten_None;
		if (!KCMKentenKindOf(Utf8(applyKenten[i].fValue), kind))
		{
			// The format carries a custom mark's own character and this build's writer does not
			// take one (KCMStoryRestore's table, "Custom" among the names it refuses). Saying so is
			// better than writing nine of the ten marks and leaving the tenth silently missing.
			whyNot = "a kenten mark in the file cannot be written back (\"";
			whyNot.SetTranslatable(kFalse);
			whyNot.Append(applyKenten[i].fValue.c_str());
			whyNot.Append("\" - a custom mark carries a character this version does not write)");
			outRefused = kTrue;
			return 0;
		}
		kentenKinds[i] = kind;
	}

	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	int32 written = 0;
	TextIndex at = 0;
	int32 len = 0;

	// ---- what comes off -------------------------------------------------------------------------
	// ★**CLEARS BEFORE APPLIES**, because two spans can overlap - a reading that grew covers where
	//   the old one stood - and a clear running after the apply beside it would wipe it.
	for (size_t i = 0; i < clearRuby.size(); ++i)
	{
		if (StandsOnTheSameCharacters(applyRuby, clearRuby[i]))
			continue;				// replaced in place: the apply does it, and the look survives
		ModelRangeOf(docAttrs, clearRuby[i], paraStart, at, len);
		if (len <= 0)
			continue;
		if (KCMClearRuby(model, at, len) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "a ruby could not be taken off (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}

	for (size_t i = 0; i < clearKenten.size(); ++i)
	{
		if (StandsOnTheSameCharacters(applyKenten, clearKenten[i]))
			continue;				// the apply below writes the new kind over exactly these
		ModelRangeOf(docAttrs, clearKenten[i], paraStart, at, len);
		if (len <= 0)
			continue;
		if (KCMApplyKentenKind(model, at, len, IKentenStyle::Kenten_None) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "a kenten could not be taken off (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}

	// ⚠A tate-chu-yoko has no look to lose by being turned off and on again (its offsets are not
	//  touched either way), so it needs no StandsOnTheSameCharacters: a stretch that grew is simply
	//  off over the old one and on over the new.
	for (size_t i = 0; i < clearTcy.size(); ++i)
	{
		ModelRangeOf(docAttrs, clearTcy[i], paraStart, at, len);
		if (len <= 0)
			continue;
		if (KCMApplyTcy(model, at, len, kFalse) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "a tate-chu-yoko could not be taken off (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}

	// ⚠A WARICHU's settings - its line count, size, alignment - are separate attributes and are never
	//  written (KCMApplyWarichu), so turning it off and on again keeps them on every character that had
	//  them. That is why it too needs no StandsOnTheSameCharacters.
	for (size_t i = 0; i < clearWarichu.size(); ++i)
	{
		ModelRangeOf(docAttrs, clearWarichu[i], paraStart, at, len);
		if (len <= 0)
			continue;
		if (KCMApplyWarichu(model, at, len, kFalse) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "a warichu could not be taken off (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}

	// ---- and what goes on -----------------------------------------------------------------------
	if (!applyRuby.empty())
	{
		// ⚠A story that never had ruby has no strand for one to live in, and the apply would go
		//   nowhere. Asked once per paragraph rather than once per span: it answers kSuccess
		//   straight away when the strand is already there.
		if (KCMCreateRubyStrandIfNeeded(model) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "this story cannot be given ruby (its ruby strand could not be made)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
	}

	for (size_t i = 0; i < applyRuby.size(); ++i)
	{
		ModelRangeOf(docAttrs, applyRuby[i], paraStart, at, len);
		if (len <= 0)
			continue;
		// ★fGroup COMES FROM THE FILE. The comparison does not judge by it (2026-09-12, the user's
		//   decision), but a reading being written has to say which it is - and the file's answer
		//   is the reader's own.
		if (KCMApplyRuby(model, at, len, Utf8(applyRuby[i].fValue), applyRuby[i].fGroup) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "a ruby could not be written (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}

	for (size_t i = 0; i < applyKenten.size(); ++i)
	{
		ModelRangeOf(docAttrs, applyKenten[i], paraStart, at, len);
		if (len <= 0)
			continue;
		if (KCMApplyKentenKind(model, at, len, kentenKinds[i]) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "a kenten could not be written (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}

	for (size_t i = 0; i < applyTcy.size(); ++i)
	{
		ModelRangeOf(docAttrs, applyTcy[i], paraStart, at, len);
		if (len <= 0)
			continue;
		if (KCMApplyTcy(model, at, len, kTrue) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "a tate-chu-yoko could not be written (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}

	for (size_t i = 0; i < applyWarichu.size(); ++i)
	{
		ModelRangeOf(docAttrs, applyWarichu[i], paraStart, at, len);
		if (len <= 0)
			continue;
		if (KCMApplyWarichu(model, at, len, kTrue) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = "a warichu could not be written (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}

	return written;
}

// End, KCMStoryAttrPour.cpp.
