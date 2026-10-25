//========================================================================================
//
//  KCMPageSplice.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cstdio>
#include <string>
#include <vector>

#include "KCMPageSplice.h"
#include "KCMZipStore.h"		// Crc32 - the fingerprint

namespace KCMPageSplice
{

namespace
{

/** The model index of paragraph p's break: its start, its characters, and the positions it counts that the text does
	not (KCMStoryTextExport's paraEnds, the same sum). */
int32 BreakIndex(const std::vector<std::string>& paras, const std::vector<KCMParaAttrs>& attrs,
				 const std::vector<int32>& starts, size_t p)
{
	return starts[p] + KCMParaText::CountCodePoints(paras[p]) + static_cast<int32>(attrs[p].fUncountedAt.size());
}

/** The text offset of gap g of a paragraph - g model positions after its start: g less the uncounted positions in
	front of the gap. (The k-th uncounted position stands at model offset fUncountedAt[k] + k - KCMTextRead, ClosePara.) */
int32 TextOffsetOfGap(const KCMParaAttrs& attrs, int32 g)
{
	int32 before = 0;
	for (size_t k = 0; k < attrs.fUncountedAt.size(); ++k)
	{
		if (attrs.fUncountedAt[k] + static_cast<int32>(k) < g)
			++before;
	}
	return g - before;
}

bool16 VectorsAgree(const std::vector<std::string>& paras, const std::vector<KCMParaAttrs>& attrs,
					const std::vector<int32>& starts)
{
	return (!paras.empty() && attrs.size() >= paras.size() && starts.size() >= paras.size()) ? kTrue : kFalse;
}

/** The spans over [a, b), counted from a. */
KCMAttrSpanList ClipSpans(const KCMAttrSpanList& spans, int32 a, int32 b)
{
	KCMAttrSpanList out;
	for (size_t i = 0; i < spans.size(); ++i)
	{
		const int32 lo = (spans[i].fStart > a) ? spans[i].fStart : a;
		const int32 end = spans[i].fStart + spans[i].fLen;
		const int32 hi = (end < b) ? end : b;
		if (hi > lo)
			out.push_back(KCMAttrSpan(lo - a, hi - lo, spans[i].fValue, spans[i].fGroup));
	}
	return out;
}

/** [a, b) of one paragraph: its words and the four kinds of mark over them. No note reference, no endnote mark. */
KCMStoryShape::Para Slice(const KCMStoryShape::Para& src, int32 a, int32 b)
{
	KCMStoryShape::Para out;
	out.fText = KCMParaText::SliceCodePoints(src.fText, a, b - a);
	out.fRuby = ClipSpans(src.fRuby, a, b);
	out.fKenten = ClipSpans(src.fKenten, a, b);
	out.fTcy = ClipSpans(src.fTcy, a, b);
	out.fWarichu = ClipSpans(src.fWarichu, a, b);
	KCMParaText::SetSpanValuesToText(out.fTcy, out.fText);
	KCMParaText::SetSpanValuesToText(out.fWarichu, out.fText);
	return out;
}

void AppendShifted(KCMAttrSpanList& into, const KCMAttrSpanList& more, int32 shift)
{
	for (size_t i = 0; i < more.size(); ++i)
	{
		KCMAttrSpan span(more[i]);
		span.fStart += shift;
		into.push_back(span);
	}
}

/** Touching spans become one - of the same value (kenten), or of any (tate-chu-yoko, warichu: an ON/OFF the document
	holds per character). */
void JoinTouching(KCMAttrSpanList& spans, bool16 sameValueOnly)
{
	KCMAttrSpanList out;
	for (size_t i = 0; i < spans.size(); ++i)
	{
		if (!out.empty() && out.back().fStart + out.back().fLen == spans[i].fStart
			&& (!sameValueOnly || out.back().fValue == spans[i].fValue))
		{
			out.back().fLen += spans[i].fLen;
			continue;
		}
		out.push_back(spans[i]);
	}
	spans.swap(out);
}

/** a followed by b, as one paragraph. */
KCMStoryShape::Para Join(const KCMStoryShape::Para& a, const KCMStoryShape::Para& b)
{
	KCMStoryShape::Para out = a;
	const int32 shift = KCMParaText::CountCodePoints(a.fText);
	out.fText += b.fText;
	AppendShifted(out.fRuby, b.fRuby, shift);
	AppendShifted(out.fKenten, b.fKenten, shift);
	AppendShifted(out.fTcy, b.fTcy, shift);
	AppendShifted(out.fWarichu, b.fWarichu, shift);
	for (size_t i = 0; i < b.fNoteRefs.size(); ++i)
	{
		KCMStoryShape::NoteRef ref = b.fNoteRefs[i];
		ref.fAt += shift;
		out.fNoteRefs.push_back(ref);
	}
	for (size_t i = 0; i < b.fEndnoteAt.size(); ++i)
		out.fEndnoteAt.push_back(b.fEndnoteAt[i] + shift);
	JoinTouching(out.fKenten, kTrue);
	JoinTouching(out.fTcy, kFalse);
	JoinTouching(out.fWarichu, kFalse);
	KCMParaText::SetSpanValuesToText(out.fTcy, out.fText);
	KCMParaText::SetSpanValuesToText(out.fWarichu, out.fText);
	return out;
}

/** Whether (p, at) stands strictly inside [from, to): at `from` it is in front of the range, at `to` behind it. */
bool16 Inside(const Place& from, const Place& to, int32 p, int32 at)
{
	if (p < from.fPara || p > to.fPara)
		return kFalse;
	if (from.fPara == to.fPara)
		return (at > from.fOffset && at < to.fOffset) ? kTrue : kFalse;
	if (p == from.fPara)
		return (at > from.fOffset) ? kTrue : kFalse;
	if (p == to.fPara)
		return (at < to.fOffset) ? kTrue : kFalse;
	return kTrue;
}

bool16 RubyCrosses(const KCMAttrSpanList& ruby, int32 at)
{
	for (size_t i = 0; i < ruby.size(); ++i)
	{
		if (ruby[i].fStart < at && ruby[i].fStart + ruby[i].fLen > at)
			return kTrue;
	}
	return kFalse;
}

void AppendNumber(long long n, std::string& out)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%lld", n);
	out += buf;
}

void SpellSpans(char kind, const KCMAttrSpanList& spans, std::string& out)
{
	for (size_t i = 0; i < spans.size(); ++i)
	{
		out += kind;
		AppendNumber(spans[i].fStart, out);
		out += ",";
		AppendNumber(spans[i].fLen, out);
		out += spans[i].fGroup ? ",1," : ",0,";
		AppendNumber(static_cast<long long>(spans[i].fValue.size()), out);
		out += ":";
		out += spans[i].fValue;
		out += "\n";
	}
}

}	// anonymous namespace

bool16 PlaceOfIndex(const std::vector<std::string>& paras, const std::vector<KCMParaAttrs>& attrs,
					const std::vector<int32>& starts, int32 at, Place& out)
{
	if (!VectorsAgree(paras, attrs, starts) || at < 0)
		return kFalse;
	for (size_t p = 0; p < paras.size(); ++p)
	{
		if (at > BreakIndex(paras, attrs, starts, p))
			continue;
		out.fPara = static_cast<int32>(p);
		// ★IN FRONT OF THE PARAGRAPH'S FIRST CHARACTER - its own start, or a table or a note reference it begins with
		//   (KCMParaAttrs::fLeadingUncounted: the reader starts the paragraph past them).
		out.fOffset = (at <= starts[p]) ? 0 : TextOffsetOfGap(attrs[p], at - starts[p]);
		return kTrue;
	}
	return kFalse;
}

bool16 PlaceOfEnd(const std::vector<std::string>& paras, const std::vector<KCMParaAttrs>& attrs,
				  const std::vector<int32>& starts, int32 start, int32 end, Place& out)
{
	if (end <= start)
		return PlaceOfIndex(paras, attrs, starts, start, out);
	if (!VectorsAgree(paras, attrs, starts))
		return kFalse;
	const int32 last = end - 1;
	for (size_t p = 0; p < paras.size(); ++p)
	{
		const int32 brk = BreakIndex(paras, attrs, starts, p);
		if (last > brk)
			continue;
		out.fPara = static_cast<int32>(p);
		if (last == brk)
			out.fOffset = KCMParaText::CountCodePoints(paras[p]);		// the break: the paragraph's end, the break left out
		else if (last < starts[p])
			out.fOffset = 0;											// a position the paragraph begins with
		else
			out.fOffset = TextOffsetOfGap(attrs[p], last - starts[p] + 1);
		return kTrue;
	}
	return kFalse;
}

KCMStoryShape::Story Cut(const KCMStoryShape::Story& s, const Place& from, const Place& to)
{
	// ★NO DIRECTION: a piece is a box's words, and the direction is the box's (KCMPageDocx::Box::fVertical) - a piece
	//   carrying the story's read back unequal to its own box and the export refused every vertical frame (2026-10-25,
	//   found live).
	KCMStoryShape::Story piece;
	const int32 n = static_cast<int32>(s.fBody.size());
	if (from.fPara < 0 || to.fPara >= n || from.fPara > to.fPara)
		return piece;
	for (int32 p = from.fPara; p <= to.fPara; ++p)
	{
		const KCMStoryShape::Para& src = s.fBody[static_cast<size_t>(p)];
		const int32 len = KCMParaText::CountCodePoints(src.fText);
		int32 a = (p == from.fPara) ? from.fOffset : 0;
		int32 b = (p == to.fPara) ? to.fOffset : len;
		a = (a < 0) ? 0 : ((a > len) ? len : a);
		b = (b < a) ? a : ((b > len) ? len : b);
		piece.fBody.push_back(Slice(src, a, b));
	}
	return piece;
}

void Fingerprint(const KCMStoryShape::Story& piece, int32& outBytes, uint32& outCrc)
{
	std::string spelt;
	for (size_t p = 0; p < piece.fBody.size(); ++p)
	{
		const KCMStoryShape::Para& para = piece.fBody[p];
		spelt += "P";
		AppendNumber(static_cast<long long>(para.fText.size()), spelt);
		spelt += ":";
		spelt += para.fText;
		spelt += "\n";
		SpellSpans('R', para.fRuby, spelt);
		SpellSpans('K', para.fKenten, spelt);
		SpellSpans('T', para.fTcy, spelt);
		SpellSpans('W', para.fWarichu, spelt);
		for (size_t i = 0; i < para.fNoteRefs.size(); ++i)
		{
			spelt += "N";
			AppendNumber(para.fNoteRefs[i].fAt, spelt);
			spelt += ",";
			AppendNumber(para.fNoteRefs[i].fNote, spelt);
			spelt += "\n";
		}
		for (size_t i = 0; i < para.fEndnoteAt.size(); ++i)
		{
			spelt += "E";
			AppendNumber(para.fEndnoteAt[i], spelt);
			spelt += "\n";
		}
	}
	outBytes = static_cast<int32>(spelt.size());
	outCrc = KCMZipStore::Crc32(spelt.data(), spelt.size());
}

bool16 Splice(KCMStoryShape::Story& s, const Place& from, const Place& to, const KCMStoryShape::Story& piece,
			  std::string& whyNot)
{
	whyNot.clear();

	// ---- the box: text only, in this version ------------------------------------------------------------
	if (!piece.fTables.empty())
	{
		whyNot = "the box holds a table - this version puts text only back";
		return kFalse;
	}
	if (!piece.fNotes.empty())
	{
		whyNot = "the box holds a footnote - this version puts text only back";
		return kFalse;
	}
	for (size_t p = 0; p < piece.fBody.size(); ++p)
	{
		if (!piece.fBody[p].fNoteRefs.empty())
		{
			whyNot = "the box holds a footnote reference - this version puts text only back";
			return kFalse;
		}
		if (!piece.fBody[p].fEndnoteAt.empty())
		{
			whyNot = "the box holds an endnote mark - this version puts text only back";
			return kFalse;
		}
	}

	// ---- the range ---------------------------------------------------------------------------------------
	const int32 n = static_cast<int32>(s.fBody.size());
	if (from.fPara < 0 || to.fPara >= n || from.fPara > to.fPara
		|| (from.fPara == to.fPara && from.fOffset > to.fOffset))
	{
		whyNot = "the frame's range is not in the story";
		return kFalse;
	}
	const size_t pS = static_cast<size_t>(from.fPara);
	const size_t pE = static_cast<size_t>(to.fPara);
	const int32 oS = from.fOffset;
	const int32 oE = to.fOffset;
	const int32 lenE = KCMParaText::CountCodePoints(s.fBody[pE].fText);
	if (oS < 0 || oS > KCMParaText::CountCodePoints(s.fBody[pS].fText) || oE < 0 || oE > lenE)
	{
		whyNot = "the frame's range is not in the story";
		return kFalse;
	}
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		if (s.fTables[t].fInTable == -1 && Inside(from, to, s.fTables[t].fParaIndex, s.fTables[t].fOffset))
		{
			whyNot = "a table stands in the frame's range";
			return kFalse;
		}
	}
	for (size_t p = pS; p <= pE; ++p)
	{
		const KCMStoryShape::Para& para = s.fBody[p];
		for (size_t i = 0; i < para.fNoteRefs.size(); ++i)
		{
			if (Inside(from, to, static_cast<int32>(p), para.fNoteRefs[i].fAt))
			{
				whyNot = "a footnote reference stands in the frame's range";
				return kFalse;
			}
		}
		for (size_t i = 0; i < para.fEndnoteAt.size(); ++i)
		{
			if (Inside(from, to, static_cast<int32>(p), para.fEndnoteAt[i]))
			{
				whyNot = "an endnote mark stands in the frame's range";
				return kFalse;
			}
		}
	}
	if (RubyCrosses(s.fBody[pS].fRuby, oS) || RubyCrosses(s.fBody[pE].fRuby, oE))
	{
		whyNot = "a ruby crosses the edge of the frame's range";
		return kFalse;
	}

	// ---- the new paragraphs: the head of the first, the box's, the tail of the last ------------------------
	std::vector<KCMStoryShape::Para> box = piece.fBody;
	if (box.empty())
		box.push_back(KCMStoryShape::Para());
	const size_t k = box.size();

	KCMStoryShape::Para head = Slice(s.fBody[pS], 0, oS);
	for (size_t i = 0; i < s.fBody[pS].fNoteRefs.size(); ++i)
	{
		if (s.fBody[pS].fNoteRefs[i].fAt <= oS)
			head.fNoteRefs.push_back(s.fBody[pS].fNoteRefs[i]);
	}
	for (size_t i = 0; i < s.fBody[pS].fEndnoteAt.size(); ++i)
	{
		if (s.fBody[pS].fEndnoteAt[i] <= oS)
			head.fEndnoteAt.push_back(s.fBody[pS].fEndnoteAt[i]);
	}
	KCMStoryShape::Para tail = Slice(s.fBody[pE], oE, lenE);
	for (size_t i = 0; i < s.fBody[pE].fNoteRefs.size(); ++i)
	{
		if (s.fBody[pE].fNoteRefs[i].fAt >= oE)
		{
			KCMStoryShape::NoteRef ref = s.fBody[pE].fNoteRefs[i];
			ref.fAt -= oE;
			tail.fNoteRefs.push_back(ref);
		}
	}
	for (size_t i = 0; i < s.fBody[pE].fEndnoteAt.size(); ++i)
	{
		if (s.fBody[pE].fEndnoteAt[i] >= oE)
			tail.fEndnoteAt.push_back(s.fBody[pE].fEndnoteAt[i] - oE);
	}

	std::vector<KCMStoryShape::Para> made;
	int32 tailAt = 0;						// where the tail begins in the last new paragraph
	if (k == 1)
	{
		const KCMStoryShape::Para front = Join(head, box[0]);
		tailAt = KCMParaText::CountCodePoints(front.fText);
		made.push_back(Join(front, tail));
	}
	else
	{
		made.push_back(Join(head, box[0]));
		for (size_t i = 1; i + 1 < k; ++i)
			made.push_back(box[i]);
		tailAt = KCMParaText::CountCodePoints(box[k - 1].fText);
		made.push_back(Join(box[k - 1], tail));
	}

	// ---- the body's tables move with the paragraphs ------------------------------------------------------
	const int32 lastNew = static_cast<int32>(pS + k) - 1;
	const int32 grows = static_cast<int32>(k) - static_cast<int32>(pE - pS + 1);
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		KCMStoryShape::Table& table = s.fTables[t];
		if (table.fInTable != -1 || table.fParaIndex < from.fPara)
			continue;									// in a cell (counted in its cell), or before the range
		if (table.fParaIndex == from.fPara && table.fOffset <= oS)
			continue;									// in the head: the first new paragraph begins with it
		if (table.fParaIndex == to.fPara && table.fOffset >= oE)
		{
			table.fParaIndex = lastNew;					// in the tail
			table.fOffset = table.fOffset - oE + tailAt;
			continue;
		}
		if (table.fParaIndex > to.fPara)
			table.fParaIndex += grows;
	}

	s.fBody.erase(s.fBody.begin() + static_cast<std::ptrdiff_t>(pS), s.fBody.begin() + static_cast<std::ptrdiff_t>(pE) + 1);
	s.fBody.insert(s.fBody.begin() + static_cast<std::ptrdiff_t>(pS), made.begin(), made.end());
	return kTrue;
}

}	// namespace KCMPageSplice

// End, KCMPageSplice.cpp.
