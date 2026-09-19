//========================================================================================
//
//  KCMStoryMerge.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storyhtml-test).
#include "VCPlugInHeaders.h"

#include "KCMStoryMerge.h"
#include "KCMParaText.h"		// AppendUtf8
#include "KCMTextDiff.h"		// ToCodePoints / Diff - the same engine the comparison runs on

#include <algorithm>
#include <cstddef>
#include <utility>

namespace KCMStoryMerge
{

namespace
{

typedef KCMTextDiff::Change Change;

/** Whether the origin range [s, s+k) is clear of every change in `b`: neither overlapping it nor
	touching it. ★TOUCHING COUNTS (the design, 6-4): two insertions at one place, or a deletion
	ending where the other side's begins, cannot be ordered by anybody but a person. */
bool16 ClearOf(const std::vector<Change>& b, int32 s, int32 k)
{
	for (size_t i = 0; i < b.size(); ++i)
	{
		const int32 bs = b[i].aStart;
		const int32 be = b[i].aStart + b[i].aCount;
		if (!(be < s || s + k < bs))
			return kFalse;
	}
	return kTrue;
}

/** An origin position that is clear of b's changes, moved into the coordinates of b's other side. */
int32 ToNow(const std::vector<Change>& b, int32 s)
{
	int32 delta = 0;
	for (size_t i = 0; i < b.size(); ++i)
	{
		if (b[i].aStart + b[i].aCount <= s)
			delta += b[i].bCount - b[i].aCount;
	}
	return s + delta;
}

/** Whether the origin range [s, e) OVERLAPS a change of b (touching is fine here: a span may end
	where the document's edit begins). */
bool16 OverlapsAny(const std::vector<Change>& b, int32 s, int32 e)
{
	for (size_t i = 0; i < b.size(); ++i)
	{
		const int32 bs = b[i].aStart;
		const int32 be = b[i].aStart + ((b[i].aCount > 0) ? b[i].aCount : 1);	// an insertion sits ON a character
		if (b[i].aCount == 0)
		{
			if (bs > s && bs < e)		// an insertion strictly inside the range
				return kTrue;
			continue;
		}
		if (bs < e && s < be)
			return kTrue;
	}
	return kFalse;
}

/** One change of Word's that was taken: where it stood in origin, in after, and in now. */
struct Taken
{
	int32	fOriginStart, fOriginCount;
	int32	fAfterStart, fAfterCount;
	int32	fNowAt;			// its place in now's coordinates (the same as merged's, before the changes after it)
};

/*	SpanMerge
	The four span kinds of one paragraph, merged the same way (the header's three steps):
	  1. the document's own spans, carried to the merged coordinates - a span over words Word
	     replaced is clipped to what is left of it (a ruby, which cannot be clipped, goes);
	  2. Word's spans over the words it changed, carried with those words;
	  3. spans Word put on or took off over words it did not change: put on / taken off in the
	     document, when the document did not change those words itself - else a conflict, named.
*/
struct SpanMerge
{
	const std::vector<Change>*	fA;
	const std::vector<Change>*	fB;
	const std::vector<Taken>*	fTaken;
	bool16						fIsRuby;
	const char*					fWhat;

	/** now -> merged: the taken changes standing before p have lengthened or shortened the text. */
	int32 NowToMerged(int32 p) const
	{
		int32 delta = 0;
		for (size_t i = 0; i < fTaken->size(); ++i)
		{
			const Taken& t = (*fTaken)[i];
			if (t.fNowAt + t.fOriginCount <= p)
				delta += t.fAfterCount - t.fOriginCount;
		}
		return p + delta;
	}

	/** after -> origin, for a position outside every change of A. */
	int32 AfterToOrigin(int32 t) const
	{
		int32 delta = 0;
		for (size_t i = 0; i < fA->size(); ++i)
		{
			const Change& c = (*fA)[i];
			if (c.bStart + c.bCount <= t)
				delta += c.bCount - c.aCount;
		}
		return t - delta;
	}

	/** The change of A the after range [s, e) overlaps, or -1. */
	int32 ChangeUnder(int32 s, int32 e) const
	{
		for (size_t i = 0; i < fA->size(); ++i)
		{
			const Change& c = (*fA)[i];
			if (c.bCount == 0)
				continue;
			if (c.bStart < e && s < c.bStart + c.bCount)
				return static_cast<int32>(i);
		}
		return -1;
	}

	/** The taken change that is change `changeIndex` of A, as an index into fTaken, or -1. */
	int32 TakenFor(int32 changeIndex) const
	{
		const Change& c = (*fA)[static_cast<size_t>(changeIndex)];
		for (size_t i = 0; i < fTaken->size(); ++i)
		{
			if ((*fTaken)[i].fOriginStart == c.aStart && (*fTaken)[i].fAfterStart == c.bStart)
				return static_cast<int32>(i);
		}
		return -1;
	}

	/** Where taken change `index` begins in the merged text: its place in now, moved by the taken
		changes BEFORE it (⚠not by itself - an insertion's own length must not be counted at its own
		start, which NowToMerged would do). */
	int32 MergedStartOf(int32 index) const
	{
		int32 at = (*fTaken)[static_cast<size_t>(index)].fNowAt;
		for (int32 j = 0; j < index; ++j)
			at += (*fTaken)[static_cast<size_t>(j)].fAfterCount - (*fTaken)[static_cast<size_t>(j)].fOriginCount;
		return at;
	}

	/** An after position -> merged, or kFalse when it cannot be carried (inside a change not taken). */
	bool16 AfterToMerged(int32 t, int32& out) const
	{
		const int32 under = ChangeUnder(t, t + 1);
		if (under >= 0)
		{
			const int32 tk = TakenFor(under);
			if (tk < 0)
				return kFalse;
			out = MergedStartOf(tk) + (t - (*fTaken)[static_cast<size_t>(tk)].fAfterStart);
			return kTrue;
		}
		const int32 o = AfterToOrigin(t);
		out = NowToMerged(ToNow(*fB, o));
		return kTrue;
	}

	static bool16 StartsEarlier(const KCMAttrSpan& x, const KCMAttrSpan& y) { return x.fStart < y.fStart; }

	void Run(const KCMAttrSpanList& origin, const KCMAttrSpanList& after, const KCMAttrSpanList& now,
			 KCMAttrSpanList& out, int32& applied, std::vector<std::string>& whys) const
	{
		out.clear();

		// ---- 1. the document's own spans, carried --------------------------------------------------
		for (size_t k = 0; k < now.size(); ++k)
		{
			int32 s = now[k].fStart;
			const int32 e = now[k].fStart + now[k].fLen;
			// pieces outside the taken changes' now-ranges
			bool16 touched = kFalse;
			std::vector< std::pair<int32, int32> > pieces;
			int32 from = s;
			for (size_t i = 0; i < fTaken->size(); ++i)
			{
				const Taken& t = (*fTaken)[i];
				const int32 ts = t.fNowAt, te = t.fNowAt + t.fOriginCount;
				if (t.fOriginCount == 0)
				{
					// an insertion strictly inside the span cuts it in two (a ruby cannot be cut)
					if (ts > from && ts < e)
					{
						touched = kTrue;
						pieces.push_back(std::make_pair(from, ts));
						from = ts;
					}
					continue;
				}
				if (ts < e && from < te)
				{
					touched = kTrue;
					if (from < ts)
						pieces.push_back(std::make_pair(from, ts));
					from = (te > from) ? te : from;
				}
			}
			if (from < e)
				pieces.push_back(std::make_pair(from, e));
			if (touched && fIsRuby)
				continue;				// a reading over words Word rewrote: Word's own span says what stands there now
			for (size_t p = 0; p < pieces.size(); ++p)
			{
				KCMAttrSpan span = now[k];
				span.fStart = NowToMerged(pieces[p].first);
				span.fLen = NowToMerged(pieces[p].second - 1) + 1 - span.fStart;
				if (span.fLen > 0)
					out.push_back(span);
			}
			(void)s;
		}

		// ---- 2 and 3. Word's spans ----------------------------------------------------------------
		std::vector<char> originSeen(origin.size(), 0);		// origin spans that after still has
		for (size_t k = 0; k < after.size(); ++k)
		{
			const KCMAttrSpan& span = after[k];
			const int32 ts = span.fStart, te = span.fStart + span.fLen;
			const int32 under = ChangeUnder(ts, te);
			if (under >= 0)
			{
				// 2. over words Word changed: it travels with them when that change was taken
				int32 ms = 0, me = 0;
				if (TakenFor(under) < 0 || !AfterToMerged(ts, ms) || !AfterToMerged(te - 1, me))
					continue;			// the words were not taken (a conflict already named), so neither is this
				KCMAttrSpan carried = span;
				carried.fStart = ms;
				carried.fLen = me + 1 - ms;
				if (carried.fLen > 0)
					out.push_back(carried);
				continue;
			}

			// 3. over words Word did not change: the same as origin's, or put on in Word
			const int32 os = AfterToOrigin(ts), oe = AfterToOrigin(te - 1) + 1;
			bool16 same = kFalse;
			for (size_t i = 0; i < origin.size() && !same; ++i)
			{
				if (originSeen[i] == 0 && origin[i].fStart == os && origin[i].fStart + origin[i].fLen == oe
					&& origin[i].fValue == span.fValue)
				{
					originSeen[i] = 1;
					same = kTrue;
				}
			}
			if (same)
				continue;
			if (OverlapsAny(*fB, os, oe))
			{
				whys.push_back(std::string("the document changed the words under a ") + fWhat + " Word put on");
				continue;
			}
			KCMAttrSpan put = span;
			put.fStart = NowToMerged(ToNow(*fB, os));
			put.fLen = NowToMerged(ToNow(*fB, oe - 1)) + 1 - put.fStart;
			bool16 already = kFalse;
			for (size_t i = 0; i < out.size() && !already; ++i)
				already = (out[i].fStart == put.fStart && out[i].fLen == put.fLen && out[i].fValue == put.fValue) ? kTrue : kFalse;
			if (!already)
				out.push_back(put);
			++applied;
		}

		// 3, the other way: spans Word took off
		for (size_t i = 0; i < origin.size(); ++i)
		{
			if (originSeen[i] != 0)
				continue;
			const int32 os = origin[i].fStart, oe = origin[i].fStart + origin[i].fLen;
			// over words Word changed: gone with the words (or kept with them, when the change was not taken)
			bool16 underA = kFalse;
			for (size_t c = 0; c < fA->size() && !underA; ++c)
			{
				const Change& ch = (*fA)[c];
				const int32 as = ch.aStart, ae = ch.aStart + ch.aCount;
				underA = (ch.aCount == 0) ? ((as > os && as < oe) ? kTrue : kFalse) : ((as < oe && os < ae) ? kTrue : kFalse);
			}
			if (underA)
				continue;
			if (OverlapsAny(*fB, os, oe))
			{
				whys.push_back(std::string("the document changed the words under a ") + fWhat + " Word took off");
				continue;
			}
			const int32 ms = NowToMerged(ToNow(*fB, os));
			const int32 me = NowToMerged(ToNow(*fB, oe - 1)) + 1;
			bool16 found = kFalse;
			for (size_t k = 0; k < out.size() && !found; ++k)
			{
				if (out[k].fStart == ms && out[k].fStart + out[k].fLen == me && out[k].fValue == origin[i].fValue)
				{
					out.erase(out.begin() + static_cast<std::ptrdiff_t>(k));
					found = kTrue;
				}
			}
			if (found)
				++applied;
			else
				whys.push_back(std::string("the document already changed that ") + fWhat);
		}

		std::stable_sort(out.begin(), out.end(), StartsEarlier);
	}
};

}	// anonymous namespace

void MergePara(const KCMStoryHtml::Para& origin, const KCMStoryHtml::Para& after, const KCMStoryHtml::Para& now,
			   ParaResult& out)
{
	out = ParaResult();
	out.fMerged = now;

	std::vector<int32> o, w, n;
	KCMTextDiff::ToCodePoints(origin.fText, &o, nil);
	KCMTextDiff::ToCodePoints(after.fText, &w, nil);
	KCMTextDiff::ToCodePoints(now.fText, &n, nil);

	std::vector<Change> a;
	std::vector<Change> b;
	KCMTextDiff::Diff(o, w, a);
	KCMTextDiff::Diff(o, n, b);
	// ⚠NO EARLY RETURN WHEN a IS EMPTY: Word may have changed no words and still have put a ruby on
	//  or taken a kenten off - the spans are merged below either way (found by the harness, R-spans).

	// ---- which of Word's changes can be taken, judged before anything is written -----------------
	std::vector<Taken> taken;			// in ascending origin order
	for (size_t i = 0; i < a.size(); ++i)
	{
		const Change& c = a[i];
		if (!ClearOf(b, c.aStart, c.aCount))
		{
			out.fWhys.push_back("the document changed the same words");
			continue;
		}
		Taken t;
		t.fOriginStart = c.aStart;
		t.fOriginCount = c.aCount;
		t.fAfterStart = c.bStart;
		t.fAfterCount = c.bCount;
		t.fNowAt = ToNow(b, c.aStart);

		// ★A FOOTNOTE REFERENCE INSIDE THE WORDS WORD REPLACED cannot travel with them: the change is a
		//   conflict. A reference standing AT the change's edge is fine - it is outside the words.
		bool16 holdsReference = kFalse;
		for (size_t r = 0; r < now.fNoteRefs.size() && !holdsReference; ++r)
		{
			const int32 at = now.fNoteRefs[r].fAt;
			holdsReference = (at > t.fNowAt && at < t.fNowAt + t.fOriginCount) ? kTrue : kFalse;
		}
		if (holdsReference)
		{
			out.fWhys.push_back("a footnote reference stands in the changed words");
			continue;
		}
		taken.push_back(t);
	}

	// ---- the words, back to front ----------------------------------------------------------------
	std::vector<int32> merged = n;
	for (size_t i = taken.size(); i > 0; --i)
	{
		const Taken& t = taken[i - 1];
		merged.erase(merged.begin() + t.fNowAt, merged.begin() + t.fNowAt + t.fOriginCount);
		merged.insert(merged.begin() + t.fNowAt, w.begin() + t.fAfterStart, w.begin() + t.fAfterStart + t.fAfterCount);
	}
	out.fApplied += static_cast<int32>(taken.size());

	std::string text;
	for (size_t i = 0; i < merged.size(); ++i)
		KCMParaText::AppendUtf8(text, merged[i]);
	out.fMerged.fText = text;

	// ---- the spans over them ---------------------------------------------------------------------
	SpanMerge sm;
	sm.fA = &a;
	sm.fB = &b;
	sm.fTaken = &taken;
	sm.fIsRuby = kTrue;		sm.fWhat = "ruby";
	sm.Run(origin.fRuby, after.fRuby, now.fRuby, out.fMerged.fRuby, out.fApplied, out.fWhys);
	sm.fIsRuby = kFalse;	sm.fWhat = "kenten";
	sm.Run(origin.fKenten, after.fKenten, now.fKenten, out.fMerged.fKenten, out.fApplied, out.fWhys);
	sm.fWhat = "tate-chu-yoko";
	sm.Run(origin.fTcy, after.fTcy, now.fTcy, out.fMerged.fTcy, out.fApplied, out.fWhys);
	sm.fWhat = "warichu";
	sm.Run(origin.fWarichu, after.fWarichu, now.fWarichu, out.fMerged.fWarichu, out.fApplied, out.fWhys);
	// A tate-chu-yoko's and a warichu's value is the text it covers (KCMParaText), which may have moved.
	KCMParaText::SetSpanValuesToText(out.fMerged.fTcy, out.fMerged.fText);
	KCMParaText::SetSpanValuesToText(out.fMerged.fWarichu, out.fMerged.fText);

	// ---- the document's footnote references, moved with their words ----------------------------
	for (size_t r = 0; r < out.fMerged.fNoteRefs.size(); ++r)
	{
		int32& at = out.fMerged.fNoteRefs[r].fAt;
		int32 delta = 0;
		for (size_t i = 0; i < taken.size(); ++i)
		{
			if (taken[i].fNowAt + taken[i].fOriginCount <= at)
				delta += taken[i].fAfterCount - taken[i].fOriginCount;
		}
		at += delta;
	}
}

}	// namespace KCMStoryMerge

// End, KCMStoryMerge.cpp.
