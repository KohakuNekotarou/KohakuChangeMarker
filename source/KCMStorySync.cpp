//========================================================================================
//
//  KCMStorySync.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storydocx-test).
#include "VCPlugInHeaders.h"

#include "KCMStorySync.h"
#include "KCMStoryDocx.h"		// WriteParts / Read / RejoinTables - the one format both sides go through
#include "KCMParaPairing.h"		// which paragraph goes with which
#include "KCMParaText.h"		// IsObjectCharacter / KeepTcyInsideWarichu's rule
#include "KCMTextDiff.h"		// ToCodePoints / Diff
#include "KCMZipStore.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <map>
#include <utility>

namespace KCMStorySync
{

std::string Where::Say() const
{
	char buf[64];
	if (fKind == kCell)
		std::snprintf(buf, sizeof(buf), "table %d row %d cell %d", static_cast<int>(fTable),
					  static_cast<int>(fRow), static_cast<int>(fCell));
	else if (fKind == kNote)
		std::snprintf(buf, sizeof(buf), "note %d", static_cast<int>(fNote) + 1);
	else
		std::snprintf(buf, sizeof(buf), "the body");
	return std::string(buf);
}

namespace
{

typedef std::vector<KCMStoryShape::Para> Paras;
typedef KCMTextDiff::Change Change;

std::string Num(int32 n)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(n));
	return std::string(buf);
}

bool16 SpansSame(const KCMAttrSpanList& a, const KCMAttrSpanList& b)
{
	if (a.size() != b.size())
		return kFalse;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].fStart != b[i].fStart || a[i].fLen != b[i].fLen || a[i].fValue != b[i].fValue
			|| a[i].fGroup != b[i].fGroup)
			return kFalse;
	}
	return kTrue;
}

std::vector<int32> RefPlaces(const KCMStoryShape::Para& p)
{
	std::vector<int32> out;
	for (size_t i = 0; i < p.fNoteRefs.size(); ++i)
		out.push_back(p.fNoteRefs[i].fAt);
	return out;
}

/** Everything a paragraph carries, the note references by PLACE only (their numbers are each side's own). */
bool16 ParaSame(const KCMStoryShape::Para& a, const KCMStoryShape::Para& b)
{
	return (a.fText == b.fText && SpansSame(a.fRuby, b.fRuby) && SpansSame(a.fKenten, b.fKenten)
			&& SpansSame(a.fTcy, b.fTcy) && SpansSame(a.fWarichu, b.fWarichu)
			&& RefPlaces(a) == RefPlaces(b) && a.fEndnoteAt == b.fEndnoteAt) ? kTrue : kFalse;
}

/** Where a table stands, how many rows, how many cells in each, and how far each reaches.
	★**THE HEADER FLAG IS NOT PART OF IT** (2026-09-23, the user's rule: header and footer rows are
	  rows like any other - only what happened to the cells is looked at). */
bool16 TableShapeSame(const KCMStoryShape::Table& a, const KCMStoryShape::Table& b, std::string& why)
{
	if (a.fInTable != b.fInTable || a.fInRow != b.fInRow || a.fInCell != b.fInCell)
	{
		why = "it stands somewhere else";
		return kFalse;
	}
	if (a.fRows.size() != b.fRows.size())
	{
		why = Num(static_cast<int32>(a.fRows.size())) + " row(s) became " + Num(static_cast<int32>(b.fRows.size()));
		return kFalse;
	}
	for (size_t r = 0; r < a.fRows.size(); ++r)
	{
		if (a.fRows[r].fCells.size() != b.fRows[r].fCells.size())
		{
			why = "row " + Num(static_cast<int32>(r)) + ": " + Num(static_cast<int32>(a.fRows[r].fCells.size()))
				  + " cell(s) became " + Num(static_cast<int32>(b.fRows[r].fCells.size()));
			return kFalse;
		}
		for (size_t c = 0; c < a.fRows[r].fCells.size(); ++c)
		{
			if (a.fRows[r].fCells[c].fColSpan != b.fRows[r].fCells[c].fColSpan
				|| a.fRows[r].fCells[c].fRowSpan != b.fRows[r].fCells[c].fRowSpan)
			{
				why = "row " + Num(static_cast<int32>(r)) + " cell " + Num(static_cast<int32>(c))
					  + ": cells were merged or split";
				return kFalse;
			}
		}
	}
	return kTrue;
}

/** The tables of `s` standing in one place, in document order. */
void TablesIn(const KCMStoryShape::Story& s, int32 inTable, int32 inRow, int32 inCell, std::vector<size_t>& out)
{
	out.clear();
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		const KCMStoryShape::Table& x = s.fTables[t];
		if (x.fInTable != inTable)
			continue;
		if (inTable >= 0 && (x.fInRow != inRow || x.fInCell != inCell))
			continue;
		out.push_back(t);
	}
}

const Paras* ParasAt(const KCMStoryShape::Story& s, const Where& w)
{
	if (w.fKind == Where::kBody)
		return &s.fBody;
	if (w.fKind == Where::kNote)
		return (w.fNote >= 0 && static_cast<size_t>(w.fNote) < s.fNotes.size()) ? &s.fNotes[static_cast<size_t>(w.fNote)] : nil;
	if (w.fTable < 0 || static_cast<size_t>(w.fTable) >= s.fTables.size())
		return nil;
	const KCMStoryShape::Table& t = s.fTables[static_cast<size_t>(w.fTable)];
	if (w.fRow < 0 || static_cast<size_t>(w.fRow) >= t.fRows.size())
		return nil;
	if (w.fCell < 0 || static_cast<size_t>(w.fCell) >= t.fRows[static_cast<size_t>(w.fRow)].fCells.size())
		return nil;
	return &t.fRows[static_cast<size_t>(w.fRow)].fCells[static_cast<size_t>(w.fCell)].fParas;
}

Paras* ParasAt(KCMStoryShape::Story& s, const Where& w)
{
	return const_cast<Paras*>(ParasAt(static_cast<const KCMStoryShape::Story&>(s), w));
}

/** A position of N's paragraph carried through the character changes to W's, or -1 when a change
	takes away the character it stands before. A position AT the start of a change stays before it. */
int32 MapThrough(const std::vector<Change>& ch, int32 pos)
{
	int32 delta = 0;
	for (size_t i = 0; i < ch.size(); ++i)
	{
		if (pos <= ch[i].aStart)
			return pos + delta;
		if (pos < ch[i].aStart + ch[i].aCount)
			return -1;
		delta += ch[i].bCount - ch[i].aCount;
	}
	return pos + delta;
}

/** Where W puts the text that follows a change starting exactly at `pos` - the other place a
	reference standing at `pos` may have gone. ★**A REFERENCE AT THE START OF A CHANGE CAN LAND ON
	EITHER SIDE OF IT** (measured by the harness 2026-09-23: "X" typed where a reference stood came
	out before the reference in Word, after it in MapThrough), and the text alone cannot say which.
	-1 when no change starts at `pos`. */
int32 MapPastChangeAt(const std::vector<Change>& ch, int32 pos)
{
	int32 delta = 0;
	for (size_t i = 0; i < ch.size(); ++i)
	{
		if (pos < ch[i].aStart)
			return -1;
		if (pos == ch[i].aStart)
			return pos + delta + ch[i].bCount;
		delta += ch[i].bCount - ch[i].aCount;
	}
	return -1;
}

/** One comparison's state: the three stories and the plan being written. */
struct Run
{
	const KCMStoryShape::Story*	fNow;		// the document as read - for the tate-chu-yoko guard
	const KCMStoryShape::Story*	fN;			// the document through this format (Normalize)
	const KCMStoryShape::Story*	fW;			// Word's, in the document's table shape (Normalize)
	Plan*						fPlan;
	std::vector<int32>			fWordOfNote;	// N's note -> the W note it pairs with, or -1
	std::vector<bool16>			fTableHeld;

	Run() : fNow(nil), fN(nil), fW(nil), fPlan(nil) {}
};

void Hold(Run& run, const Where& where, int32 para, const char* what, const std::string& why)
{
	Step s;
	s.fKind = Step::kHeld;
	s.fWhere = where;
	s.fPara = para;
	s.fWhat = what;
	s.fWhy = why;
	run.fPlan->fSteps.push_back(s);
}

void AddNote(Run& run, const Where& where, int32 resultPara, int32 at, int32 wordNote)
{
	Step s;
	s.fKind = Step::kAddNote;
	s.fWhere = where;
	s.fPara = resultPara;
	s.fAt = at;
	s.fNote = wordNote;
	if (wordNote >= 0 && static_cast<size_t>(wordNote) < run.fW->fNotes.size())
		s.fParas = run.fW->fNotes[static_cast<size_t>(wordNote)];
	run.fPlan->fSteps.push_back(s);
}

void DeleteNote(Run& run, int32 nowNote)
{
	Step s;
	s.fKind = Step::kDeleteNote;
	s.fNote = nowNote;
	run.fPlan->fSteps.push_back(s);
}

/** The references of a paired paragraph: which of N's stay (returned, at W's offsets, numbered as N's
	notes), which go (kDeleteNote) and which W adds (kAddNote). Paired by their offsets with a diff -
	★rank alone pairs wrong when a reference is added at the offset one already stands at (measured
	on Word's own file, 2026-09-22 - KCMStoryMerge's PlanNotesForPlace said so first). A reference the
	changed words took away goes, and W's one there comes as a new note with W's words. */
std::vector<KCMStoryShape::NoteRef> PlanRefs(Run& run, const Where& where, int32 result,
											 const KCMStoryShape::Para& n, const KCMStoryShape::Para& w,
											 const std::vector<Change>& ch)
{
	std::vector<int32> wAt = RefPlaces(w);
	std::vector<int32> nAt;
	std::vector<int32> nNote;
	for (size_t i = 0; i < n.fNoteRefs.size(); ++i)
	{
		int32 m = MapThrough(ch, n.fNoteRefs[i].fAt);
		if (m < 0)
		{
			DeleteNote(run, n.fNoteRefs[i].fNote);
			continue;
		}
		// at the start of a change: W decides which side it stands on, when W has one there
		const int32 past = MapPastChangeAt(ch, n.fNoteRefs[i].fAt);
		if (past >= 0 && std::find(wAt.begin(), wAt.end(), m) == wAt.end()
			&& std::find(wAt.begin(), wAt.end(), past) != wAt.end())
			m = past;
		nAt.push_back(m);
		nNote.push_back(n.fNoteRefs[i].fNote);
	}

	std::vector<Change> d;
	if (!KCMTextDiff::Diff(nAt, wAt, d))
	{
		d.clear();
		Change all;
		all.aStart = 0; all.aCount = static_cast<int32>(nAt.size());
		all.bStart = 0; all.bCount = static_cast<int32>(wAt.size());
		d.push_back(all);
	}

	std::vector<KCMStoryShape::NoteRef> kept;
	size_t ni = 0;
	size_t wi = 0;
	for (size_t c = 0; c <= d.size(); ++c)
	{
		const size_t nEnd = (c < d.size()) ? static_cast<size_t>(d[c].aStart) : nAt.size();
		const size_t wEnd = (c < d.size()) ? static_cast<size_t>(d[c].bStart) : wAt.size();
		for (; ni < nEnd && wi < wEnd; ++ni, ++wi)
		{
			KCMStoryShape::NoteRef r;
			r.fAt = wAt[wi];
			r.fNote = nNote[ni];
			kept.push_back(r);
			const int32 wn = w.fNoteRefs[wi].fNote;
			if (nNote[ni] >= 0 && static_cast<size_t>(nNote[ni]) < run.fWordOfNote.size())
				run.fWordOfNote[static_cast<size_t>(nNote[ni])] = wn;
		}
		if (c >= d.size())
			break;
		for (int32 k = 0; k < d[c].aCount; ++k)
			DeleteNote(run, nNote[static_cast<size_t>(d[c].aStart + k)]);
		for (int32 k = 0; k < d[c].bCount; ++k)
		{
			const size_t j = static_cast<size_t>(d[c].bStart + k);
			AddNote(run, where, result, wAt[j], w.fNoteRefs[j].fNote);
		}
		ni = static_cast<size_t>(d[c].aStart + d[c].aCount);
		wi = static_cast<size_t>(d[c].bStart + d[c].bCount);
	}
	return kept;
}

/** ★WORD CANNOT HOLD A TATE-CHU-YOKO INSIDE A WARICHU (KCMParaText::KeepTcyInsideWarichu says why and
	how it was measured). One the DOCUMENT has there, whose words W still has unchanged but not the
	tate-chu-yoko, is kept - carried to W's offsets - and named. One whose words W changed is W's to
	decide, and nothing is said. */
void KeepTcy(Run& run, const Where& where, int32 nIndex, const KCMStoryShape::Para& raw,
			 const std::vector<Change>& ch, KCMStoryShape::Para& target)
{
	for (size_t i = 0; i < raw.fTcy.size(); ++i)
	{
		const KCMAttrSpan& t = raw.fTcy[i];
		bool16 underWarichu = kFalse;
		for (size_t k = 0; k < raw.fWarichu.size() && !underWarichu; ++k)
		{
			const int32 ws = raw.fWarichu[k].fStart;
			const int32 we = ws + raw.fWarichu[k].fLen;
			underWarichu = (ws < t.fStart + t.fLen && t.fStart < we) ? kTrue : kFalse;
		}
		if (!underWarichu)
			continue;
		const int32 s = MapThrough(ch, t.fStart);
		const int32 e = MapThrough(ch, t.fStart + t.fLen);
		bool16 there = kFalse;
		for (size_t k = 0; k < target.fTcy.size() && !there; ++k)
			there = (target.fTcy[k].fStart == s && target.fTcy[k].fLen == t.fLen) ? kTrue : kFalse;
		if (there)
			continue;
		// ★THE WORDS UNDER IT CHANGED IN WORD: then it is Word's edit, not something Word could not
		//   carry, and W decides (measured on the matrix 2026-09-23: A39 emptied a warichu, and a
		//   "could not be kept" named a tate-chu-yoko whose words the reader had taken out).
		if (s < 0 || e < 0 || e - s != t.fLen)
			continue;
		KCMAttrSpan kept = t;
		kept.fStart = s;
		target.fTcy.push_back(kept);
		std::sort(target.fTcy.begin(), target.fTcy.end(),
				  [](const KCMAttrSpan& x, const KCMAttrSpan& y) { return x.fStart < y.fStart; });
		Hold(run, where, nIndex, "Tcy", "a tate-chu-yoko inside a warichu was kept: Word cannot carry one there");
	}
}

/** One of N's paragraphs against the W paragraph it pairs with. `result` is its index among the
	place's paragraphs once the plan is carried out. `tablesHere`: (N's table ordinal, its offset in
	N's paragraph) for every table standing in this paragraph. */
void ComparePara(Run& run, const Where& where, int32 nIndex, int32 result,
				 const KCMStoryShape::Para& n, const KCMStoryShape::Para& raw, const KCMStoryShape::Para& w,
				 const std::vector< std::pair<int32, int32> >& tablesHere)
{
	if (ParaSame(n, w))
	{
		// the same references, one for one: their notes pair
		for (size_t k = 0; k < n.fNoteRefs.size() && k < w.fNoteRefs.size(); ++k)
		{
			const int32 nn = n.fNoteRefs[k].fNote;
			if (nn >= 0 && static_cast<size_t>(nn) < run.fWordOfNote.size())
				run.fWordOfNote[static_cast<size_t>(nn)] = w.fNoteRefs[k].fNote;
		}
		return;
	}

	std::vector<int32> a;
	std::vector<int32> b;
	KCMTextDiff::ToCodePoints(n.fText, &a, nil);
	KCMTextDiff::ToCodePoints(w.fText, &b, nil);
	std::vector<Change> ch;
	if (!KCMTextDiff::Diff(a, b, ch))
	{
		Hold(run, where, nIndex, "Para", "the paragraph differs too much to place the changes");
		return;
	}

	for (size_t c = 0; c < ch.size(); ++c)
	{
		for (int32 i = ch[c].aStart; i < ch[c].aStart + ch[c].aCount; ++i)
		{
			if (KCMParaText::IsObjectCharacter(a[static_cast<size_t>(i)]))
			{
				Hold(run, where, nIndex, "Para",
					 "a change would take away an object's character (an anchored frame, a variable, an index marker...)");
				return;
			}
		}
		for (int32 j = ch[c].bStart; j < ch[c].bStart + ch[c].bCount; ++j)
		{
			if (KCMParaText::IsObjectCharacter(b[static_cast<size_t>(j)]))
			{
				Hold(run, where, nIndex, "Para", "Word added an object's character, which an import cannot make");
				return;
			}
		}
		for (size_t t = 0; t < tablesHere.size(); ++t)
		{
			const int32 off = tablesHere[t].second;
			if (ch[c].aStart < off && off < ch[c].aStart + ch[c].aCount)
			{
				Hold(run, where, nIndex, "Para", "a change crosses the place a table stands");
				return;
			}
		}
	}

	std::vector<int32> ends;
	for (size_t e = 0; e < n.fEndnoteAt.size(); ++e)
		ends.push_back(MapThrough(ch, n.fEndnoteAt[e]));
	if (ends != w.fEndnoteAt)
	{
		Hold(run, where, nIndex, "Para", "an endnote's mark would move, go or come - endnotes are not carried");
		return;
	}

	KCMStoryShape::Para target = w;
	target.fNoteRefs = PlanRefs(run, where, result, n, w, ch);
	if (raw.fText == n.fText)
		KeepTcy(run, where, nIndex, raw, ch, target);

	Step s;
	s.fKind = Step::kSetPara;
	s.fWhere = where;
	s.fPara = nIndex;
	s.fParas.push_back(target);
	for (size_t t = 0; t < tablesHere.size(); ++t)
	{
		const int32 ordinal = tablesHere[t].first;
		s.fTables.push_back(std::make_pair(ordinal, run.fW->fTables[static_cast<size_t>(ordinal)].fOffset));
	}
	run.fPlan->fSteps.push_back(s);
}

/** A paragraph taken out or put in whole must not carry what an import cannot take out or put in. */
bool16 HoldsObjectOrEndnote(const KCMStoryShape::Para& p)
{
	if (!p.fEndnoteAt.empty())
		return kTrue;
	std::vector<int32> cps;
	KCMTextDiff::ToCodePoints(p.fText, &cps, nil);
	for (size_t i = 0; i < cps.size(); ++i)
		if (KCMParaText::IsObjectCharacter(cps[i]))
			return kTrue;
	return kFalse;
}

/** The paragraphs of N in [nFrom, nTo) against W's in [wFrom, wTo) - a run with no table in it. */
void PairRun(Run& run, const Where& nWhere, const Where& wWhere, int32 nFrom, int32 nTo, int32 wFrom, int32 wTo,
			 int32& result)
{
	const Paras& np = *ParasAt(*run.fN, nWhere);
	const Paras& rp = *ParasAt(*run.fNow, nWhere);
	const Paras& wp = *ParasAt(*run.fW, wWhere);

	std::vector<std::string> nt;
	std::vector<std::string> wt;
	for (int32 i = nFrom; i < nTo; ++i) nt.push_back(np[static_cast<size_t>(i)].fText);
	for (int32 i = wFrom; i < wTo; ++i) wt.push_back(wp[static_cast<size_t>(i)].fText);
	std::vector<KCMParaPairing::Step> steps;
	KCMParaPairing::Pair(nt, wt, steps);

	const std::vector< std::pair<int32, int32> > noTables;
	for (size_t k = 0; k < steps.size(); ++k)
	{
		const KCMParaPairing::Step& st = steps[k];
		if (st.fKind == KCMParaPairing::Step::kPair)
		{
			const size_t ni = static_cast<size_t>(nFrom + st.fDoc);
			ComparePara(run, nWhere, nFrom + st.fDoc, result, np[ni], rp[ni], wp[static_cast<size_t>(wFrom + st.fFile)], noTables);
			++result;
		}
		else if (st.fKind == KCMParaPairing::Step::kInsert)
		{
			Step s;
			s.fKind = Step::kInsertParas;
			s.fWhere = nWhere;
			s.fPara = (st.fDoc < 0) ? nFrom - 1 : nFrom + st.fDoc;
			bool16 refused = kFalse;
			for (int32 i = 0; i < st.fCount; ++i)
			{
				KCMStoryShape::Para p = wp[static_cast<size_t>(wFrom + st.fFile + i)];
				if (HoldsObjectOrEndnote(p))
					refused = kTrue;
				for (size_t r = 0; r < p.fNoteRefs.size(); ++r)
					AddNote(run, nWhere, result + i, p.fNoteRefs[r].fAt, p.fNoteRefs[r].fNote);
				p.fNoteRefs.clear();
				s.fParas.push_back(p);
			}
			if (refused)
				Hold(run, nWhere, s.fPara, "Para", "Word added a paragraph holding an object's character or an endnote's mark");
			else
			{
				run.fPlan->fSteps.push_back(s);
				result += st.fCount;
			}
		}
		else	// kDelete
		{
			bool16 refused = kFalse;
			for (int32 i = 0; i < st.fCount; ++i)
				if (HoldsObjectOrEndnote(np[static_cast<size_t>(nFrom + st.fDoc + i)]))
					refused = kTrue;
			if (refused)
			{
				Hold(run, nWhere, nFrom + st.fDoc, "Para",
					 "Word took out a paragraph holding an object's character or an endnote's mark");
				result += st.fCount;		// they stay
				continue;
			}
			for (int32 i = 0; i < st.fCount; ++i)
			{
				const KCMStoryShape::Para& gone = np[static_cast<size_t>(nFrom + st.fDoc + i)];
				for (size_t r = 0; r < gone.fNoteRefs.size(); ++r)
					DeleteNote(run, gone.fNoteRefs[r].fNote);
			}
			Step s;
			s.fKind = Step::kDeleteParas;
			s.fWhere = nWhere;
			s.fPara = nFrom + st.fDoc;
			s.fCount = st.fCount;
			run.fPlan->fSteps.push_back(s);
		}
	}
}

/** One place: the body, a cell or a note. ★THE TABLES STANDING HERE ARE PEGS - the paragraphs that
	hold them pair with each other, k-th with k-th, and the runs between them are paired on their
	own, so no paragraph pairing ever reaches across a table. */
void ComparePlace(Run& run, const Where& nWhere, const Where& wWhere)
{
	const Paras* np = ParasAt(*run.fN, nWhere);
	const Paras* rp = ParasAt(*run.fNow, nWhere);
	const Paras* wp = ParasAt(*run.fW, wWhere);
	if (np == nil || rp == nil || wp == nil || np->size() != rp->size())
	{
		Hold(run, nWhere, -1, "Place", nWhere.Say() + " could not be lined up");
		return;
	}

	std::vector<size_t> nt;
	std::vector<size_t> wt;
	if (nWhere.fKind == Where::kBody)
	{
		TablesIn(*run.fN, -1, 0, 0, nt);
		TablesIn(*run.fW, -1, 0, 0, wt);
	}
	else if (nWhere.fKind == Where::kCell)
	{
		TablesIn(*run.fN, nWhere.fTable, nWhere.fRow, nWhere.fCell, nt);
		TablesIn(*run.fW, wWhere.fTable, wWhere.fRow, wWhere.fCell, wt);
	}
	if (nt.size() != wt.size())
	{
		Hold(run, nWhere, -1, "Table", "the number of tables in " + nWhere.Say() + " changed");
		return;
	}

	// the pegs: the paragraphs holding tables, each once, paired k-th with k-th
	std::vector<int32> nPeg;
	std::vector<int32> wPeg;
	for (size_t k = 0; k < nt.size(); ++k)
	{
		const int32 a = run.fN->fTables[nt[k]].fParaIndex;
		const int32 b = run.fW->fTables[wt[k]].fParaIndex;
		const bool16 newN = (nPeg.empty() || nPeg.back() != a) ? kTrue : kFalse;
		const bool16 newW = (wPeg.empty() || wPeg.back() != b) ? kTrue : kFalse;
		if (newN != newW)
		{
			Hold(run, nWhere, -1, "Table", "the tables in " + nWhere.Say() + " stand in different paragraphs");
			return;
		}
		if (newN)
		{
			nPeg.push_back(a);
			wPeg.push_back(b);
		}
	}

	int32 nFrom = 0;
	int32 wFrom = 0;
	int32 result = 0;
	for (size_t g = 0; g <= nPeg.size(); ++g)
	{
		const int32 nTo = (g < nPeg.size()) ? nPeg[g] : static_cast<int32>(np->size());
		const int32 wTo = (g < wPeg.size()) ? wPeg[g] : static_cast<int32>(wp->size());
		PairRun(run, nWhere, wWhere, nFrom, nTo, wFrom, wTo, result);
		if (g == nPeg.size())
			break;
		std::vector< std::pair<int32, int32> > here;
		for (size_t k = 0; k < nt.size(); ++k)
		{
			if (run.fN->fTables[nt[k]].fParaIndex == nPeg[g])
				here.push_back(std::make_pair(static_cast<int32>(nt[k]), run.fN->fTables[nt[k]].fOffset));
		}
		ComparePara(run, nWhere, nPeg[g], result, (*np)[static_cast<size_t>(nPeg[g])],
					(*rp)[static_cast<size_t>(nPeg[g])], (*wp)[static_cast<size_t>(wPeg[g])], here);
		++result;
		nFrom = nPeg[g] + 1;
		wFrom = wPeg[g] + 1;
	}
}

/** Every place's paragraph count, table by table and note by note - what Normalize checks N' against. */
bool16 SameLayout(const KCMStoryShape::Story& a, const KCMStoryShape::Story& b)
{
	if (a.fBody.size() != b.fBody.size() || a.fTables.size() != b.fTables.size() || a.fNotes.size() != b.fNotes.size())
		return kFalse;
	for (size_t t = 0; t < a.fTables.size(); ++t)
	{
		if (a.fTables[t].fRows.size() != b.fTables[t].fRows.size())
			return kFalse;
		for (size_t r = 0; r < a.fTables[t].fRows.size(); ++r)
		{
			if (a.fTables[t].fRows[r].fCells.size() != b.fTables[t].fRows[r].fCells.size())
				return kFalse;
			for (size_t c = 0; c < a.fTables[t].fRows[r].fCells.size(); ++c)
				if (a.fTables[t].fRows[r].fCells[c].fParas.size() != b.fTables[t].fRows[r].fCells[c].fParas.size())
					return kFalse;
		}
	}
	for (size_t n = 0; n < a.fNotes.size(); ++n)
		if (a.fNotes[n].size() != b.fNotes[n].size())
			return kFalse;
	return kTrue;
}

// ---- ApplyToShape's pieces ----------------------------------------------------------------------

void ApplyPlace(KCMStoryShape::Story& out, const Where& where, const Plan& plan)
{
	Paras* ps = ParasAt(out, where);
	if (ps == nil)
		return;
	const Paras before = *ps;

	std::map<int32, const KCMStoryShape::Para*> set;
	std::map<int32, const Step*> setStep;
	std::map<int32, const Paras*> insertAfter;
	std::vector<bool16> gone(before.size(), kFalse);
	bool16 any = kFalse;
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const Step& s = plan.fSteps[i];
		if (!(s.fWhere == where))
			continue;
		if (s.fKind == Step::kSetPara && !s.fParas.empty())
		{
			set[s.fPara] = &s.fParas[0];
			setStep[s.fPara] = &s;
			any = kTrue;
		}
		else if (s.fKind == Step::kInsertParas)
		{
			insertAfter[s.fPara] = &s.fParas;
			any = kTrue;
		}
		else if (s.fKind == Step::kDeleteParas)
		{
			for (int32 k = s.fPara; k < s.fPara + s.fCount && k < static_cast<int32>(gone.size()); ++k)
				gone[static_cast<size_t>(k)] = kTrue;
			any = kTrue;
		}
	}
	if (!any)
		return;

	// the tables standing in this place, found before the paragraphs move
	std::vector<size_t> tables;
	if (where.fKind == Where::kBody)
		TablesIn(out, -1, 0, 0, tables);
	else if (where.fKind == Where::kCell)
		TablesIn(out, where.fTable, where.fRow, where.fCell, tables);

	Paras result;
	std::vector<int32> newIndexOf(before.size(), -1);
	std::map<int32, const Paras*>::const_iterator head = insertAfter.find(-1);
	if (head != insertAfter.end())
		result.insert(result.end(), head->second->begin(), head->second->end());
	for (size_t i = 0; i < before.size(); ++i)
	{
		if (!gone[i])
		{
			newIndexOf[i] = static_cast<int32>(result.size());
			std::map<int32, const KCMStoryShape::Para*>::const_iterator s = set.find(static_cast<int32>(i));
			result.push_back((s != set.end()) ? *s->second : before[i]);
		}
		std::map<int32, const Paras*>::const_iterator ins = insertAfter.find(static_cast<int32>(i));
		if (ins != insertAfter.end())
			result.insert(result.end(), ins->second->begin(), ins->second->end());
	}
	*ps = result;

	for (size_t k = 0; k < tables.size(); ++k)
	{
		KCMStoryShape::Table& t = out.fTables[tables[k]];
		const int32 old = t.fParaIndex;
		if (old >= 0 && static_cast<size_t>(old) < newIndexOf.size() && newIndexOf[static_cast<size_t>(old)] >= 0)
			t.fParaIndex = newIndexOf[static_cast<size_t>(old)];
		std::map<int32, const Step*>::const_iterator s = setStep.find(old);
		if (s == setStep.end())
			continue;
		for (size_t j = 0; j < s->second->fTables.size(); ++j)
			if (s->second->fTables[j].first == static_cast<int32>(tables[k]))
				t.fOffset = s->second->fTables[j].second;
	}
}

void AllPlaces(const KCMStoryShape::Story& s, std::vector<Where>& out)
{
	out.clear();
	out.push_back(Where::Body());
	for (size_t t = 0; t < s.fTables.size(); ++t)
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				out.push_back(Where::Cell(static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c)));
	for (size_t n = 0; n < s.fNotes.size(); ++n)
		out.push_back(Where::Note(static_cast<int32>(n)));
}

/** Reading order: a place's paragraphs, and inside each its references and the tables standing in
	it, by position (a reference before a table at the same offset). */
void ReadPlace(const KCMStoryShape::Story& s, const Paras& ps, int32 inT, int32 inR, int32 inC, std::vector<int32>& order)
{
	std::vector<size_t> tables;
	TablesIn(s, inT, inR, inC, tables);
	for (size_t p = 0; p < ps.size(); ++p)
	{
		std::vector< std::pair< std::pair<int32, int32>, int32 > > events;	// ((position, 0 ref / 1 table), what)
		for (size_t r = 0; r < ps[p].fNoteRefs.size(); ++r)
			events.push_back(std::make_pair(std::make_pair(ps[p].fNoteRefs[r].fAt, 0), ps[p].fNoteRefs[r].fNote));
		for (size_t k = 0; k < tables.size(); ++k)
			if (s.fTables[tables[k]].fParaIndex == static_cast<int32>(p))
				events.push_back(std::make_pair(std::make_pair(s.fTables[tables[k]].fOffset, 1), static_cast<int32>(tables[k])));
		std::stable_sort(events.begin(), events.end(),
						 [](const std::pair< std::pair<int32, int32>, int32 >& x, const std::pair< std::pair<int32, int32>, int32 >& y)
						 { return x.first < y.first; });
		for (size_t e = 0; e < events.size(); ++e)
		{
			if (events[e].first.second == 0)
			{
				order.push_back(events[e].second);
				continue;
			}
			const int32 t = events[e].second;
			const KCMStoryShape::Table& tb = s.fTables[static_cast<size_t>(t)];
			for (size_t r = 0; r < tb.fRows.size(); ++r)
				for (size_t c = 0; c < tb.fRows[r].fCells.size(); ++c)
					ReadPlace(s, tb.fRows[r].fCells[c].fParas, t, static_cast<int32>(r), static_cast<int32>(c), order);
		}
	}
}

void RemapRefs(Paras& ps, const std::vector<int32>& newOf)
{
	for (size_t p = 0; p < ps.size(); ++p)
	{
		std::vector<KCMStoryShape::NoteRef> keep;
		for (size_t r = 0; r < ps[p].fNoteRefs.size(); ++r)
		{
			const int32 old = ps[p].fNoteRefs[r].fNote;
			if (old < 0 || static_cast<size_t>(old) >= newOf.size() || newOf[static_cast<size_t>(old)] < 0)
				continue;
			KCMStoryShape::NoteRef x = ps[p].fNoteRefs[r];
			x.fNote = newOf[static_cast<size_t>(old)];
			keep.push_back(x);
		}
		ps[p].fNoteRefs = keep;
	}
}

void RemapAllRefs(KCMStoryShape::Story& s, const std::vector<int32>& newOf)
{
	RemapRefs(s.fBody, newOf);
	for (size_t t = 0; t < s.fTables.size(); ++t)
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				RemapRefs(s.fTables[t].fRows[r].fCells[c].fParas, newOf);
}

}	// anonymous namespace

bool16 Normalize(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word,
				 KCMStoryShape::Story& outNow, KCMStoryShape::Story& outWord, std::string& whyNot)
{
	whyNot.clear();
	if (now.fTables.size() != word.fTables.size())
	{
		whyNot = "the number of tables changed (" + Num(static_cast<int32>(now.fTables.size())) + " became "
				 + Num(static_cast<int32>(word.fTables.size())) + ")";
		return kFalse;
	}
	std::vector<KCMZipStore::Entry> parts;
	std::string why;
	if (!KCMStoryDocx::WriteParts(now, 1, parts, why))
	{
		whyNot = "the document's story cannot be written in Word's format: " + why;
		return kFalse;
	}
	KCMStoryDocx::ReadResult back;
	if (!KCMStoryDocx::Read(parts, back, why))
	{
		whyNot = "the document's story does not read back from Word's format: " + why;
		return kFalse;
	}
	outNow = back.fAfter;
	KCMStoryDocx::RejoinTables(outNow, now);
	outWord = word;
	KCMStoryDocx::RejoinTables(outWord, now);
	// ★**WHETHER A ROW IS A HEADER ROW DOES NOT TRAVEL** (2026-09-23, the user's rule: header and footer
	//   rows are rows like any other - only what happened to the cells is looked at). Word's flag is
	//   made the document's here, once, so that nothing downstream ever sees it differ (measured on
	//   the matrix: H39 took a header row's flag off in Word, and the plan's check failed on it).
	for (size_t t = 0; t < outWord.fTables.size() && t < outNow.fTables.size(); ++t)
		for (size_t r = 0; r < outWord.fTables[t].fRows.size() && r < outNow.fTables[t].fRows.size(); ++r)
			outWord.fTables[t].fRows[r].fHeader = outNow.fTables[t].fRows[r].fHeader;
	if (!SameLayout(outNow, now))
	{
		whyNot = "the document's story does not come back through Word's format paragraph for paragraph";
		return kFalse;
	}
	return kTrue;
}

void Compare(const KCMStoryShape::Story& now, const KCMStoryShape::Story& word, Plan& out)
{
	out = Plan();
	KCMStoryShape::Story n;
	KCMStoryShape::Story w;
	std::string why;
	if (!Normalize(now, word, n, w, why))
	{
		out.fStoryHeld = kTrue;
		out.fWhy = why;
		return;
	}

	Run run;
	run.fNow = &now;
	run.fN = &n;
	run.fW = &w;
	run.fPlan = &out;
	run.fWordOfNote.assign(n.fNotes.size(), -1);
	run.fTableHeld.assign(n.fTables.size(), kFalse);

	// ---- the tables first: a table whose shape changed is left as it is (S1/S2 reshape it) --------
	// ★In document order, so a nested table's parent is judged before it: one inside a held table is
	//   held with it, and named once, with its parent.
	for (size_t t = 0; t < n.fTables.size(); ++t)
	{
		const int32 parent = n.fTables[t].fInTable;
		if (parent >= 0 && static_cast<size_t>(parent) < run.fTableHeld.size() && run.fTableHeld[static_cast<size_t>(parent)])
		{
			run.fTableHeld[t] = kTrue;
			continue;
		}
		std::string tw;
		if (!TableShapeSame(n.fTables[t], w.fTables[t], tw))
		{
			run.fTableHeld[t] = kTrue;
			Hold(run, Where::Cell(static_cast<int32>(t), -1, -1), -1, "Table",
				 "table " + Num(static_cast<int32>(t)) + ": " + tw + " - that table was left as it is");
		}
	}

	// ---- the places: the body, every cell of a table not held, then the notes that pair ---------
	ComparePlace(run, Where::Body(), Where::Body());
	for (size_t t = 0; t < n.fTables.size(); ++t)
	{
		if (run.fTableHeld[t])
			continue;
		for (size_t r = 0; r < n.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < n.fTables[t].fRows[r].fCells.size(); ++c)
			{
				const Where cell = Where::Cell(static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c));
				ComparePlace(run, cell, cell);
			}
	}
	for (size_t k = 0; k < n.fNotes.size(); ++k)
	{
		const int32 wn = run.fWordOfNote[k];
		if (wn >= 0)
			ComparePlace(run, Where::Note(static_cast<int32>(k)), Where::Note(wn));
	}
}

KCMStoryShape::Story ApplyToShape(const KCMStoryShape::Story& normalizedNow, const Plan& plan)
{
	KCMStoryShape::Story out = normalizedNow;
	if (plan.fStoryHeld)
		return out;

	// the paragraph steps, place by place (the notes' own places included, by N's numbers)
	std::vector<Where> places;
	AllPlaces(normalizedNow, places);
	for (size_t i = 0; i < places.size(); ++i)
		ApplyPlace(out, places[i], plan);

	// the notes added: a reference in the finished paragraph, the words at the end of the list
	std::vector<bool16> noteGone(out.fNotes.size(), kFalse);
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const Step& s = plan.fSteps[i];
		if (s.fKind == Step::kDeleteNote && s.fNote >= 0 && static_cast<size_t>(s.fNote) < noteGone.size())
			noteGone[static_cast<size_t>(s.fNote)] = kTrue;
		if (s.fKind != Step::kAddNote)
			continue;
		Paras* ps = ParasAt(out, s.fWhere);
		if (ps == nil || s.fPara < 0 || static_cast<size_t>(s.fPara) >= ps->size())
			continue;
		KCMStoryShape::Para& p = (*ps)[static_cast<size_t>(s.fPara)];
		KCMStoryShape::NoteRef r;
		r.fAt = s.fAt;
		r.fNote = static_cast<int32>(out.fNotes.size());
		size_t at = 0;
		while (at < p.fNoteRefs.size() && p.fNoteRefs[at].fAt <= r.fAt)
			++at;
		p.fNoteRefs.insert(p.fNoteRefs.begin() + static_cast<std::ptrdiff_t>(at), r);
		out.fNotes.push_back(s.fParas);
		noteGone.push_back(kFalse);
	}

	// the notes taken away: their references and their words go, and the rest close up
	std::vector<int32> newOf(out.fNotes.size(), -1);
	std::vector<Paras> notes;
	for (size_t k = 0; k < out.fNotes.size(); ++k)
	{
		if (noteGone[k])
			continue;
		newOf[k] = static_cast<int32>(notes.size());
		notes.push_back(out.fNotes[k]);
	}
	out.fNotes = notes;
	RemapAllRefs(out, newOf);
	return out;
}

void RenumberNotesByReading(KCMStoryShape::Story& s)
{
	std::vector<int32> order;
	ReadPlace(s, s.fBody, -1, 0, 0, order);
	std::vector<int32> newOf(s.fNotes.size(), -1);
	std::vector<Paras> notes;
	for (size_t i = 0; i < order.size(); ++i)
	{
		const int32 o = order[i];
		if (o < 0 || static_cast<size_t>(o) >= s.fNotes.size() || newOf[static_cast<size_t>(o)] >= 0)
			continue;
		newOf[static_cast<size_t>(o)] = static_cast<int32>(notes.size());
		notes.push_back(s.fNotes[static_cast<size_t>(o)]);
	}
	for (size_t k = 0; k < s.fNotes.size(); ++k)		// a note nobody refers to keeps a place at the end
	{
		if (newOf[k] >= 0)
			continue;
		newOf[k] = static_cast<int32>(notes.size());
		notes.push_back(s.fNotes[k]);
	}
	s.fNotes = notes;
	RemapAllRefs(s, newOf);
}

}	// namespace KCMStorySync

// End, KCMStorySync.cpp.
