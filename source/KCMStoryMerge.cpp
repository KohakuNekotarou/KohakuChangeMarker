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
#include <cstdio>
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

void MergePara(const KCMStoryShape::Para& origin, const KCMStoryShape::Para& after, const KCMStoryShape::Para& now,
			   ParaResult& out, const std::vector<int32>* keep)
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
		// ★A TABLE'S PLACE, THE SAME WAY: a change may begin or end at it, not run across it.
		bool16 straddles = kFalse;
		for (size_t p = 0; keep != nil && p < keep->size() && !straddles; ++p)
		{
			const int32 at = (*keep)[p];
			straddles = (at > t.fNowAt && at < t.fNowAt + t.fOriginCount) ? kTrue : kFalse;
		}
		if (straddles)
		{
			out.fWhys.push_back("a table stands in the changed words");
			continue;
		}
		taken.push_back(t);
	}
	for (size_t i = 0; i < taken.size(); ++i)
	{
		Edit e;
		e.fNowAt = taken[i].fNowAt;
		e.fNowCount = taken[i].fOriginCount;
		e.fNewCount = taken[i].fAfterCount;
		out.fEdits.push_back(e);
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

//========================================================================================
//  The whole story: place by place
//========================================================================================

namespace
{

typedef std::vector<KCMStoryShape::Para> Paras;

std::string Num(int32 n)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(n));
	return std::string(buf);
}

/** kTrue when the three stories hold the same NUMBER of tables.

	★★**THIS IS THE ONE QUESTION THAT IS STILL THE WHOLE STORY'S** (2026-09-22). A table added or
	  taken away in Word moves the paragraphs around it - in the split shape a table IS a paragraph
	  of its own - so the three sides' paragraph lists no longer name the same places and nothing
	  can be lined up. Every other disagreement is one table's business; see TableAgreesThreeWays. */
bool16 TablesLineUp(const KCMStoryShape::Story& o, const KCMStoryShape::Story& w, const KCMStoryShape::Story& n,
					std::string& why)
{
	if (o.fTables.size() != w.fTables.size() || o.fTables.size() != n.fTables.size())
	{
		why = "the number of tables changed";
		return kFalse;
	}
	return kTrue;
}

/** kTrue when the three stories agree about table `t`: standing in the same place, with the same
	rows and the same cells in each row.

	★**ONE TABLE AT A TIME** (2026-09-22, the user's call: "or refusing is fine too" - but refusing
	  the story for one table's sake is not). A table the three sides disagree about is left exactly
	  as the document has it; the body, the notes and the other tables are merged as usual.
	⚠**WHY THIS IS STILL JUDGED BEFORE ANY CELL IS MERGED**: the cells are paired BY POSITION, so a
	  merge in Word does not read as a missing cell - it reads as a DIFFERENT cell, and the file's
	  words would go into it without anything looking wrong. Only comparing the shapes catches that,
	  and it has to be done before the pairing, not during it. */
bool16 TableAgreesThreeWays(const KCMStoryShape::Story& o, const KCMStoryShape::Story& w, const KCMStoryShape::Story& n,
							size_t t, std::string& why)
{
	const KCMStoryShape::Table& a = o.fTables[t];
	const KCMStoryShape::Table& b = w.fTables[t];
	const KCMStoryShape::Table& c = n.fTables[t];
	if (a.fInTable != b.fInTable || a.fInTable != c.fInTable || a.fInRow != b.fInRow || a.fInRow != c.fInRow
		|| a.fInCell != b.fInCell || a.fInCell != c.fInCell)
	{
		why = "it stands somewhere else";
		return kFalse;
	}
	if (a.fRows.size() != b.fRows.size() || a.fRows.size() != c.fRows.size())
	{
		why = "the number of rows changed";
		return kFalse;
	}
	for (size_t r = 0; r < a.fRows.size(); ++r)
	{
		if (a.fRows[r].fCells.size() != b.fRows[r].fCells.size() || a.fRows[r].fCells.size() != c.fRows[r].fCells.size())
		{
			why = "row " + Num(static_cast<int32>(r)) + ": the number of cells changed (a merge, a split, a row or a column)";
			return kFalse;
		}
	}
	return kTrue;
}

/** The tables of `s` standing in one place, as indices into s.fTables. */
void TablesIn(const KCMStoryShape::Story& s, int32 inTable, int32 inRow, int32 inCell, std::vector<size_t>& out)
{
	out.clear();
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		const KCMStoryShape::Table& table = s.fTables[t];
		if (table.fInTable != inTable)
			continue;
		if (inTable >= 0 && (table.fInRow != inRow || table.fInCell != inCell))
			continue;
		out.push_back(t);
	}
}

/** Whether any table of `tables` (indices into s.fTables) stands in paragraphs [from, to) of its place. */
bool16 TableInParagraphs(const KCMStoryShape::Story& s, const std::vector<size_t>& tables, int32 from, int32 to)
{
	for (size_t k = 0; k < tables.size(); ++k)
	{
		const int32 p = s.fTables[tables[k]].fParaIndex;
		if (p >= from && p < to)
			return kTrue;
	}
	return kFalse;
}

/** Whether origin paragraph `i` is inside a change of `b` that takes paragraphs away or rewrites them. */
bool16 InsideChange(const std::vector<Change>& b, int32 i)
{
	for (size_t k = 0; k < b.size(); ++k)
	{
		if (b[k].aCount > 0 && i >= b[k].aStart && i < b[k].aStart + b[k].aCount)
			return kTrue;
	}
	return kFalse;
}

/** The change of `b` origin paragraph `i` is inside, or -1. */
int32 ChangeHolding(const std::vector<Change>& b, int32 i)
{
	for (size_t k = 0; k < b.size(); ++k)
	{
		if (b[k].aCount > 0 && i >= b[k].aStart && i < b[k].aStart + b[k].aCount)
			return static_cast<int32>(k);
	}
	return -1;
}

/** Whether `b` inserts paragraphs exactly at origin position `i` (before paragraph i). */
bool16 InsertsAt(const std::vector<Change>& b, int32 i)
{
	for (size_t k = 0; k < b.size(); ++k)
	{
		if (b[k].aCount == 0 && b[k].aStart == i)
			return kTrue;
	}
	return kFalse;
}

/*	MergePlace
	One place's paragraphs, three ways. `nowTables` / `originTables` / `afterTables` are the tables
	standing in this place (indices into the three stories' fTables; the three lists run parallel
	because TablesAgreeThreeWays has passed). `merged` starts as a copy of now's paragraphs, and the
	tables' fParaIndex / fOffset in `out.fMerged` are moved as the paragraphs move.
*/
void MergePlace(const KCMStoryShape::Story& o, const KCMStoryShape::Story& w, const KCMStoryShape::Story& n,
				const Paras& oParas, const Paras& wParas, const Paras& nParas,
				const std::vector<size_t>& tables, const std::string& where,
				Paras& merged, Result& out)
{
	merged = nParas;

	std::vector<std::string> table;
	std::vector<std::string> oText, wText, nText;
	for (size_t i = 0; i < oParas.size(); ++i) oText.push_back(oParas[i].fText);
	for (size_t i = 0; i < wParas.size(); ++i) wText.push_back(wParas[i].fText);
	for (size_t i = 0; i < nParas.size(); ++i) nText.push_back(nParas[i].fText);
	std::vector<int32> oTok, wTok, nTok;
	KCMTextDiff::Tokenize(oText, table, oTok);
	KCMTextDiff::Tokenize(wText, table, wTok);
	KCMTextDiff::Tokenize(nText, table, nTok);

	std::vector<Change> a, b;
	KCMTextDiff::Diff(oTok, wTok, a);
	KCMTextDiff::Diff(oTok, nTok, b);

	// now index -> merged index, kept up to date as paragraphs go in and out (-1 = gone)
	std::vector<int32> mergedIndexOfNow(nParas.size());
	for (size_t i = 0; i < nParas.size(); ++i)
		mergedIndexOfNow[i] = static_cast<int32>(i);
	// the tables of this place: their paragraph, as a now index, and their offset
	std::vector<int32> tableNowPara(tables.size());
	std::vector<int32> tableOffset(tables.size());
	for (size_t k = 0; k < tables.size(); ++k)
	{
		tableNowPara[k] = n.fTables[tables[k]].fParaIndex;
		tableOffset[k] = n.fTables[tables[k]].fOffset;
	}

	// ---- Word's paragraph-level changes, back to front ------------------------------------------
	for (size_t ci = a.size(); ci > 0; --ci)
	{
		const Change& c = a[ci - 1];
		const int32 s = c.aStart, k = c.aCount, t = c.bStart, m = c.bCount;
		const std::string here = where + " paragraph " + Num(s + 1);

		if (k == 1 && m == 1)
		{
			// ---- the words of one paragraph -------------------------------------------------------
			int32 nowIdx = -1;
			const int32 holding = ChangeHolding(b, s);
			if (holding < 0)
			{
				nowIdx = ToNow(b, s);
			}
			else if (b[static_cast<size_t>(holding)].aCount == 1 && b[static_cast<size_t>(holding)].bCount == 1)
			{
				nowIdx = b[static_cast<size_t>(holding)].bStart;		// the document rewrote it 1:1: three ways inside
			}
			else
			{
				Refusal r; r.fWhere = here; r.fWhy = "the document changed the paragraph count here";
				out.fConflicts.push_back(r);
				continue;
			}
			if (nowIdx < 0 || static_cast<size_t>(nowIdx) >= nParas.size() || mergedIndexOfNow[static_cast<size_t>(nowIdx)] < 0)
				continue;
			// the tables standing in this paragraph: positions no change may straddle
			std::vector<int32> keep;
			for (size_t tk = 0; tk < tables.size(); ++tk)
			{
				if (tableNowPara[tk] == nowIdx)
					keep.push_back(tableOffset[tk]);
			}
			ParaResult pr;
			MergePara(oParas[static_cast<size_t>(s)], wParas[static_cast<size_t>(t)], nParas[static_cast<size_t>(nowIdx)], pr,
					  keep.empty() ? nil : &keep);
			merged[static_cast<size_t>(mergedIndexOfNow[static_cast<size_t>(nowIdx)])] = pr.fMerged;
			out.fApplied += pr.fApplied;
			for (size_t y = 0; y < pr.fWhys.size(); ++y)
			{
				Refusal r; r.fWhere = here; r.fWhy = pr.fWhys[y];
				out.fConflicts.push_back(r);
			}
			// the tables after the edited words move with them
			for (size_t tk = 0; tk < tables.size(); ++tk)
			{
				if (tableNowPara[tk] != nowIdx)
					continue;
				int32 delta = 0;
				for (size_t e = 0; e < pr.fEdits.size(); ++e)
				{
					if (pr.fEdits[e].fNowAt + pr.fEdits[e].fNowCount <= tableOffset[tk])
						delta += pr.fEdits[e].fNewCount - pr.fEdits[e].fNowCount;
				}
				tableOffset[tk] += delta;
			}
			continue;
		}

		// ---- paragraphs added, taken out, split or joined ---------------------------------------
		// The origin paragraphs concerned, and the ones on either side, have to be the document's still.
		bool16 clean = kTrue;
		for (int32 i = s; i < s + k && clean; ++i)
			clean = InsideChange(b, i) ? kFalse : kTrue;
		if (clean && k == 0)
		{
			// an insertion before origin paragraph s: neither neighbour rewritten, no insertion of the
			// document's at the same place
			if ((s > 0 && InsideChange(b, s - 1)) || (s < static_cast<int32>(oParas.size()) && InsideChange(b, s)) || InsertsAt(b, s))
				clean = kFalse;
		}
		if (!clean)
		{
			Refusal r; r.fWhere = here;
			r.fWhy = (k == 0) ? "the document changed the paragraphs around the ones Word added"
							  : "the document changed a paragraph Word took out or split";
			out.fConflicts.push_back(r);
			continue;
		}
		if (TableInParagraphs(o, tables.empty() ? std::vector<size_t>() : tables, s, s + k) && k > 0)
		{
			// (the tables' indices are the same in o and n: the shapes agree)
			Refusal r; r.fWhere = here; r.fWhy = "a table stands in the changed paragraphs";
			out.fConflicts.push_back(r);
			continue;
		}
		{
			std::vector<size_t> afterTables;
			TablesIn(w, tables.empty() ? -2 : w.fTables[tables[0]].fInTable, tables.empty() ? 0 : w.fTables[tables[0]].fInRow,
					 tables.empty() ? 0 : w.fTables[tables[0]].fInCell, afterTables);
			if (!tables.empty() && TableInParagraphs(w, afterTables, t, t + m))
			{
				Refusal r; r.fWhere = here; r.fWhy = "a table stands in the changed paragraphs";
				out.fConflicts.push_back(r);
				continue;
			}
		}

		const int32 nowAt = ToNow(b, s);			// where the k paragraphs stand in now (k may be 0)
		if (nowAt < 0 || static_cast<size_t>(nowAt) > nParas.size())
			continue;
		// their merged position: the first of them that is still there, else the position after the last
		int32 at = -1;
		for (int32 i = nowAt; i < nowAt + k && at < 0; ++i)
			at = mergedIndexOfNow[static_cast<size_t>(i)];
		if (at < 0)
		{
			at = static_cast<int32>(merged.size());
			for (int32 i = nowAt + k; i < static_cast<int32>(nParas.size()); ++i)
			{
				if (mergedIndexOfNow[static_cast<size_t>(i)] >= 0)
				{
					at = mergedIndexOfNow[static_cast<size_t>(i)];
					break;
				}
			}
		}
		// take the k out (they are the document's still, so they are origin's), put the m in
		merged.erase(merged.begin() + at, merged.begin() + at + k);
		merged.insert(merged.begin() + at, wParas.begin() + t, wParas.begin() + t + m);
		for (size_t i = 0; i < mergedIndexOfNow.size(); ++i)
		{
			if (static_cast<int32>(i) >= nowAt && static_cast<int32>(i) < nowAt + k)
				mergedIndexOfNow[i] = -1;
			else if (mergedIndexOfNow[i] >= at + k)
				mergedIndexOfNow[i] += m - k;
		}
		// ★**COUNTED THE WAY THE ROWS ARE** (2026-09-22, the user's call: the number the status line
		//   gives has to agree with what the reader can count in the panel). This change takes k
		//   paragraphs out and puts m in, and the panel makes A ROW PER PARAGRAPH it touches - so a
		//   single "++" here said 1 where three rows stood. Measured on the application: three
		//   paragraphs replaced by three others, three rows, and "1 change(s) from Word" beside them.
		//   ⚠**THE LARGER OF THE TWO**, because one side is empty for an addition (k = 0) or a
		//    removal (m = 0), and the rows follow the side that HAS the paragraphs.
		//   ⚠The 1:1 branch above does not come through here: it counts what MergePara took, which
		//    is already one per change the reader sees.
		out.fApplied += (k > m) ? k : m;
	}

	// ---- the paragraphs whose WORDS Word left alone ----------------------------------------------
	//
	// ★★★**WORD CAN CHANGE NO WORDS AND STILL HAVE PUT A RUBY ON OR TAKEN A KENTEN OFF** (2026-09-22).
	//   Such a paragraph stands in no change of `a`, so the loop above never reaches it - and
	//   MergePara, which merges the spans and was written for exactly this case, was never called
	//   for it. ⚠**THE SYMPTOM WAS SILENT**: an import of a .docx whose marks accounted for every
	//   change said "0 change(s)" with no conflict beside it, because nothing had been CONSIDERED -
	//   and the same story, with its <w:rPrChange> stripped so that the whole text was compared
	//   instead, put every mark in. Measured with work/kcm-storydocx-test/kenten_probe.cpp.
	//   ⚠MergePara's own "NO EARLY RETURN WHEN a IS EMPTY" guards the CHARACTER-level diff; this is
	//    the same rule one level up, at the PARAGRAPH-level diff that decides who is merged at all.
	// ★A paragraph the DOCUMENT rewrote is passed over: its words are not the same three ways, so a
	//   span has no shared position to be carried to - and the loop above has already had its say.
	for (int32 oi = 0; oi < static_cast<int32>(oParas.size()); ++oi)
	{
		if (InsideChange(a, oi) || InsideChange(b, oi))
			continue;
		const int32 afterIdx = ToNow(a, oi);
		const int32 nowIdx = ToNow(b, oi);
		if (afterIdx < 0 || static_cast<size_t>(afterIdx) >= wParas.size())
			continue;
		if (nowIdx < 0 || static_cast<size_t>(nowIdx) >= nParas.size())
			continue;
		const int32 mi = mergedIndexOfNow[static_cast<size_t>(nowIdx)];
		if (mi < 0 || static_cast<size_t>(mi) >= merged.size())
			continue;
		std::vector<int32> keep;
		for (size_t tk = 0; tk < tables.size(); ++tk)
		{
			if (tableNowPara[tk] == nowIdx)
				keep.push_back(tableOffset[tk]);
		}
		ParaResult pr;
		MergePara(oParas[static_cast<size_t>(oi)], wParas[static_cast<size_t>(afterIdx)],
				  nParas[static_cast<size_t>(nowIdx)], pr, keep.empty() ? nil : &keep);
		// ★**NOTHING OF WORD'S HERE MEANS THE DOCUMENT'S PARAGRAPH IS LEFT EXACTLY AS IT IS.** Writing
		//   the merged copy back unconditionally would put this pass's own idea of the paragraph over
		//   every untouched one in the story, which is a great deal of nothing to risk for no gain.
		if (pr.fApplied == 0 && pr.fWhys.empty())
			continue;
		merged[static_cast<size_t>(mi)] = pr.fMerged;
		out.fApplied += pr.fApplied;
		const std::string here = where + " paragraph " + Num(oi + 1);
		for (size_t y = 0; y < pr.fWhys.size(); ++y)
		{
			Refusal r; r.fWhere = here; r.fWhy = pr.fWhys[y];
			out.fConflicts.push_back(r);
		}
	}

	// ---- the tables of this place, in their new paragraphs --------------------------------------
	for (size_t k = 0; k < tables.size(); ++k)
	{
		KCMStoryShape::Table& moved = out.fMerged.fTables[tables[k]];
		const int32 idx = mergedIndexOfNow[static_cast<size_t>(tableNowPara[k])];
		if (idx >= 0)
			moved.fParaIndex = idx;
		moved.fOffset = tableOffset[k];
	}
}

/*	RefsOfPara
	One paragraph's footnote references, as two lists in the order they stand: where each one is,
	and which note it points at.
*/
void RefsOfPara(const KCMStoryShape::Para& p, std::vector<int32>& outAt, std::vector<int32>& outNote)
{
	outAt.clear();
	outNote.clear();
	for (size_t r = 0; r < p.fNoteRefs.size(); ++r)
	{
		outAt.push_back(p.fNoteRefs[r].fAt);
		outNote.push_back(p.fNoteRefs[r].fNote);
	}
}

/*	PlanNotesForPlace
	What Word did to the FOOTNOTES whose references stand in this place: which notes it added, which
	it took away, and - for the ones that stayed - which of `after`'s notes is which of origin's
	(noteMap, indexed by origin's note, holding after's or -1 for one Word took away).

	★★★**THE REFERENCES ARE PAIRED BY THEIR OFFSETS, WITH A DIFF.** Rank alone cannot do it:
	  measured on Word's own file 2026-09-22, an added reference landed at the SAME offset as the
	  one already standing there, so pairing by rank would have called the LAST note the new one.
	  Myers over the offsets pairs them right, and it is the diff this file uses for everything else.

	⚠**AN OFFSET MEANS NOTHING ACROSS AN EDIT.** A paragraph whose text any side changed is left
	 alone and named: putting a note at a guessed offset is worse than saying it cannot be placed.
	⚠**AND SO IS A PLACE WHOSE PARAGRAPHS DO NOT LINE UP THREE WAYS** - kFalse, and the caller says
	 so once for the whole story.

	@return kFalse when this place's paragraphs cannot be lined up at all.
*/
bool16 PlanNotesForPlace(const Paras& oParas, const Paras& wParas, const Paras& nParas,
						 const PlaceRef& place, const KCMStoryShape::Story& after,
						 std::vector<int32>& noteMap, const std::string& where, Result& out)
{
	// ★★**NOTHING TO PAIR IS NOT A FAILURE TO PAIR** - and most places hold no reference at all.
	//   Without this, a place whose PARAGRAPHS merely moved (which is the ordinary case: Word adds
	//   one, the document has one fewer) would refuse notes that nobody touched.
	bool16 anyRefs = kFalse;
	for (size_t i = 0; i < oParas.size() && !anyRefs; ++i)
		if (!oParas[i].fNoteRefs.empty()) anyRefs = kTrue;
	for (size_t i = 0; i < wParas.size() && !anyRefs; ++i)
		if (!wParas[i].fNoteRefs.empty()) anyRefs = kTrue;
	for (size_t i = 0; i < nParas.size() && !anyRefs; ++i)
		if (!nParas[i].fNoteRefs.empty()) anyRefs = kTrue;
	if (!anyRefs)
		return kTrue;

	if (oParas.size() != wParas.size() || oParas.size() != nParas.size())
		return kFalse;

	for (size_t p = 0; p < oParas.size(); ++p)
	{
		std::vector<int32> oAt, oNote, wAt, wNote, nAt, nNote;
		RefsOfPara(oParas[p], oAt, oNote);
		RefsOfPara(wParas[p], wAt, wNote);
		RefsOfPara(nParas[p], nAt, nNote);

		// ★THE PAIRING IS KEPT EVEN WHERE NOTHING HAPPENED: a note whose words Word edited is
		//   merged through noteMap, so every reference has to put its note in it.
		if (oAt == wAt)
		{
			for (size_t k = 0; k < oNote.size() && k < wNote.size(); ++k)
			{
				if (oNote[k] >= 0 && static_cast<size_t>(oNote[k]) < noteMap.size())
					noteMap[static_cast<size_t>(oNote[k])] = wNote[k];
			}
			continue;
		}

		const std::string here = where + " paragraph " + Num(static_cast<int32>(p) + 1);
		if (oAt != nAt)
		{
			Refusal r; r.fWhere = here; r.fWhy = "the document moved a footnote's reference as well";
			out.fConflicts.push_back(r);
			continue;
		}
		if (!(oParas[p].fText == wParas[p].fText && oParas[p].fText == nParas[p].fText))
		{
			Refusal r; r.fWhere = here;
			r.fWhy = "a footnote was added or taken away among words that also changed";
			out.fConflicts.push_back(r);
			continue;
		}

		std::vector<Change> d;
		if (!KCMTextDiff::Diff(oAt, wAt, d))
		{
			Refusal r; r.fWhere = here; r.fWhy = "the footnote references differ too much to pair";
			out.fConflicts.push_back(r);
			continue;
		}

		size_t oi = 0, wi = 0;
		for (size_t c = 0; c <= d.size(); ++c)
		{
			const size_t oEnd = (c < d.size()) ? static_cast<size_t>(d[c].aStart) : oAt.size();
			const size_t wEnd = (c < d.size()) ? static_cast<size_t>(d[c].bStart) : wAt.size();
			while (oi < oEnd && wi < wEnd)
			{
				if (oNote[oi] >= 0 && static_cast<size_t>(oNote[oi]) < noteMap.size())
					noteMap[static_cast<size_t>(oNote[oi])] = wNote[wi];
				++oi;
				++wi;
			}
			if (c >= d.size())
				break;

			for (int32 k = 0; k < d[c].bCount; ++k)
			{
				const size_t at = static_cast<size_t>(d[c].bStart + k);
				NoteAdd add;
				add.fPlace = place;
				add.fPara = static_cast<int32>(p);
				add.fAt = wAt[at];
				if (wNote[at] >= 0 && static_cast<size_t>(wNote[at]) < after.fNotes.size())
					add.fParas = after.fNotes[static_cast<size_t>(wNote[at])];
				out.fNoteAdds.push_back(add);
			}
			for (int32 k = 0; k < d[c].aCount; ++k)
			{
				const size_t at = static_cast<size_t>(d[c].aStart + k);
				// ★origin's notes and the document's are the same notes here: the caller has
				//   already refused the story when their number differs.
				NoteRemove rem;
				rem.fNowNote = oNote[at];
				out.fNoteRemoves.push_back(rem);
				if (oNote[at] >= 0 && static_cast<size_t>(oNote[at]) < noteMap.size())
					noteMap[static_cast<size_t>(oNote[at])] = -1;
			}
			oi = static_cast<size_t>(d[c].aStart + d[c].aCount);
			wi = static_cast<size_t>(d[c].bStart + d[c].bCount);
		}
	}
	return kTrue;
}

}	// anonymous namespace

void Merge(const KCMStoryShape::Story& origin, const KCMStoryShape::Story& after, const KCMStoryShape::Story& now,
		   Result& out)
{
	out = Result();
	out.fMerged = now;

	if (!TablesLineUp(origin, after, now, out.fWhy))
	{
		out.fStoryRefused = kTrue;
		return;
	}

	// ★**WHICH TABLES THE THREE SIDES DISAGREE ABOUT** (2026-09-22). Judged here, before anything is
	//   merged, and remembered: such a table keeps the document's own cells, and the rest of the
	//   story goes on being merged. ⚠A nested table is an ordinal of its own, so a table inside a
	//   refused one is judged on its own account - its place is part of what is compared, so a
	//   parent that changed shape shows up as "it stands somewhere else" here as well.
	std::vector<bool16> tableRefused(now.fTables.size(), kFalse);
	for (size_t t = 0; t < now.fTables.size(); ++t)
	{
		std::string tableWhy;
		if (TableAgreesThreeWays(origin, after, now, t, tableWhy))
			continue;
		tableRefused[t] = kTrue;
		Refusal r;
		r.fWhere = "table " + Num(static_cast<int32>(t));
		r.fWhy = tableWhy + " - that table was left as the document has it";
		out.fTableRefusals.push_back(r);
	}

	// ---- the body ------------------------------------------------------------------------------
	{
		std::vector<size_t> tables;
		TablesIn(now, -1, 0, 0, tables);
		Paras merged;
		MergePlace(origin, after, now, origin.fBody, after.fBody, now.fBody, tables, "body", merged, out);
		out.fMerged.fBody = merged;
	}

	// ---- the cells -----------------------------------------------------------------------------
	for (size_t t = 0; t < now.fTables.size(); ++t)
	{
		if (tableRefused[t])
			continue;			// ★left exactly as the document has it, and already named above
		for (size_t r = 0; r < now.fTables[t].fRows.size(); ++r)
		{
			for (size_t c = 0; c < now.fTables[t].fRows[r].fCells.size(); ++c)
			{
				std::vector<size_t> tables;
				TablesIn(now, static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c), tables);
				Paras merged;
				const std::string where = "table " + Num(static_cast<int32>(t)) + " row " + Num(static_cast<int32>(r))
										  + " cell " + Num(static_cast<int32>(c));
				MergePlace(origin, after, now,
						   origin.fTables[t].fRows[r].fCells[c].fParas, after.fTables[t].fRows[r].fCells[c].fParas,
						   now.fTables[t].fRows[r].fCells[c].fParas, tables, where, merged, out);
				out.fMerged.fTables[t].fRows[r].fCells[c].fParas = merged;
			}
		}
	}

	// ---- the notes -----------------------------------------------------------------------------
	// ★★★**WHAT WORD DID TO THE NOTES THEMSELVES** (2026-09-22, the user's call: take it in).
	//   Until that day any change in their NUMBER left every note alone, with one conflict to say
	//   so. Now the references are paired with a diff, so a note Word added becomes a plan for the
	//   pour (NoteAdd), one it took away becomes another (NoteRemove), and the notes that stayed
	//   are merged exactly as they always were.
	// ⚠**THE DOCUMENT'S OWN NOTES STILL HAVE TO MATCH origin's.** A footnote added or deleted in
	//   InDesign since the export moves every note's rank, and nothing here can say which is which.
	if (origin.fNotes.size() != now.fNotes.size())
	{
		Refusal r; r.fWhere = "the notes";
		r.fWhy = "the document added or removed a footnote of its own since the export";
		out.fConflicts.push_back(r);
		return;					// out.fMerged.fNotes is now's already
	}

	std::vector<int32> noteMap(now.fNotes.size(), -1);
	bool16 placesLineUp = PlanNotesForPlace(origin.fBody, after.fBody, now.fBody, PlaceRef(), after,
											noteMap, "body", out);
	for (size_t t = 0; t < now.fTables.size() && placesLineUp; ++t)
	{
		if (tableRefused[t])
			continue;			// its cells keep the document's own contents - notes included
		for (size_t r = 0; r < now.fTables[t].fRows.size() && placesLineUp; ++r)
		{
			for (size_t c = 0; c < now.fTables[t].fRows[r].fCells.size() && placesLineUp; ++c)
			{
				PlaceRef place;
				place.fTable = static_cast<int32>(t);
				place.fRow = static_cast<int32>(r);
				place.fCell = static_cast<int32>(c);
				const std::string where = "table " + Num(place.fTable) + " row " + Num(place.fRow)
										  + " cell " + Num(place.fCell);
				placesLineUp = PlanNotesForPlace(origin.fTables[t].fRows[r].fCells[c].fParas,
												 after.fTables[t].fRows[r].fCells[c].fParas,
												 now.fTables[t].fRows[r].fCells[c].fParas,
												 place, after, noteMap, where, out);
			}
		}
	}
	if (!placesLineUp)
	{
		// ⛔**NOTHING IS PLANNED OUT OF A PLACE THAT DOES NOT LINE UP.** Half a plan would put a
		//   note somewhere nobody asked for one, which is the one outcome worse than saying no.
		out.fNoteAdds.clear();
		out.fNoteRemoves.clear();
		Refusal r; r.fWhere = "the notes";
		r.fWhy = "the paragraphs holding their references do not line up";
		out.fConflicts.push_back(r);
		return;
	}

	for (size_t nn = 0; nn < now.fNotes.size(); ++nn)
	{
		const int32 w = noteMap[nn];
		if (w < 0 || static_cast<size_t>(w) >= after.fNotes.size())
			continue;			// Word took this one away, or its reference could not be paired
		std::vector<size_t> noTables;
		Paras merged;
		MergePlace(origin, after, now, origin.fNotes[nn], after.fNotes[static_cast<size_t>(w)],
				   now.fNotes[nn], noTables, "note " + Num(static_cast<int32>(nn) + 1), merged, out);
		out.fMerged.fNotes[nn] = merged;
	}
}

}	// namespace KCMStoryMerge

// End, KCMStoryMerge.cpp.
