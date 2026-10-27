//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - the Track Changes mode's list. See KCMTrackList.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ITextModel.h"
#include "TextChar.h"			// kTextChar_Tab / _CR / _LF
#include "WideString.h"
#include "UIDRef.h"

#include <algorithm>
#include <map>

#include "KCMDiag.h"			// KCM_DIAG_LOG - test builds only (nothing in a shipping .pln)
#include "KCMTrackList.h"
#include "KCMTrackPlan.h"		// KCMTrackChangeKind - the kind words

namespace
{
std::vector<KCMTrackAuthor>	gAuthors;
std::vector<KCMTrackRow>	gRows;
int32						gOtherRecords = 0;

struct CounterHistory
{
	uint32				fLast;
	std::vector<uint32>	fSeen;
	CounterHistory() : fLast(0) {}
};
std::map<UID, CounterHistory>	gHistory;
// The Target's tracked-change records when the list was last read (KCMTrackRead::Build): what an accept or a reject is
// told by, since it may move no counter (2026-10-25 - NoteRecordCount). -1 = not read since Clear.
int32							gRecordsAtRead = -1;

uint32 CounterOf(IDataBase* db, UID story)
{
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	return (model != nil) ? model->GetChangeCount() : 0;
}

const char* KindWord(int32 kind)
{
	switch (kind)
	{
		case kKCMTrackInsert:	return "insert";
		case kKCMTrackDelete:	return "delete";
		case kKCMTrackMove:		return "move";
		default:				return "replace";
	}
}

void AppendColour(PMString& out, bool16 has, uint8 r, uint8 g, uint8 b)
{
	if (!has)
	{
		out.Append("-");
		return;
	}
	char buf[16];
	::sprintf_s(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
	out.Append(buf);
}

// A TSV cell must not carry a tab or a line break of its own.
// (Walked as a WideString: PMString's operator[] hands out a PlatformChar, not a code point.)
void AppendCell(PMString& out, const PMString& s)
{
	const WideString w(s);
	WideString kept;
	for (WideString::const_iterator it = w.begin(); it != w.end(); ++it)
	{
		const UniCodePoint ch = *it;
		if (ch != kTextChar_Tab && ch != kTextChar_CR && ch != kTextChar_LF)
			kept.Append(ch);
	}
	PMString c(kept);
	c.SetTranslatable(kFalse);
	out.Append(c);
}
}	// namespace

void KCMTrackList::Set(const std::vector<KCMTrackAuthor>& authors, const std::vector<KCMTrackRow>& rows, int32 otherRecords)
{
	gAuthors = authors;
	gRows = rows;
	gOtherRecords = otherRecords;
}

void KCMTrackList::Clear()
{
	gAuthors.clear();
	gRows.clear();
	gOtherRecords = 0;
	gHistory.clear();
	gRecordsAtRead = -1;
}

int32 KCMTrackList::GetRowCount()		{ return static_cast<int32>(gRows.size()); }
int32 KCMTrackList::GetAuthorCount()	{ return static_cast<int32>(gAuthors.size()); }
int32 KCMTrackList::GetOtherRecordCount()	{ return gOtherRecords; }

const KCMTrackRow* KCMTrackList::GetRow(int32 nth)
{
	return (nth >= 0 && nth < static_cast<int32>(gRows.size())) ? &gRows[static_cast<size_t>(nth)] : nil;
}

const KCMTrackAuthor* KCMTrackList::GetAuthor(int32 a)
{
	return (a >= 0 && a < static_cast<int32>(gAuthors.size())) ? &gAuthors[static_cast<size_t>(a)] : nil;
}

int32 KCMTrackList::GetTotalChangeCount()
{
	int32 n = 0;
	for (size_t i = 0; i < gRows.size(); ++i)
		n += static_cast<int32>(gRows[i].fChanges.size());
	return n;
}

void KCMTrackList::GetStoryUIDs(std::vector<UID>& out)
{
	out.clear();
	for (size_t i = 0; i < gRows.size(); ++i)
		if (std::find(out.begin(), out.end(), gRows[i].fRow.fStoryUID) == out.end())
			out.push_back(gRows[i].fRow.fStoryUID);
}

bool16 KCMTrackList::NeedsReadAgain(UID storyUID, IDataBase* targetDB)
{
	std::map<UID, CounterHistory>::const_iterator it = gHistory.find(storyUID);
	if (it == gHistory.end() || targetDB == nil)
		return kFalse;
	const uint32 now = CounterOf(targetDB, storyUID);
	KCM_DIAG_LOG("track counters: story %u now %u last read at %u, %d value(s) read", (unsigned)storyUID.Get(), (unsigned)now,
				 (unsigned)it->second.fLast, (int)it->second.fSeen.size());
	if (now == it->second.fLast)
		return kFalse;
	if (now < it->second.fLast)
		return kTrue;										// went back: an undo
	return (std::find(it->second.fSeen.begin(), it->second.fSeen.end(), now) != it->second.fSeen.end())
		? kTrue : kFalse;									// came back to a value read before: a redo
}

bool16 KCMTrackList::AnyNeedsReadAgain(IDataBase* targetDB)
{
	for (std::map<UID, CounterHistory>::const_iterator it = gHistory.begin(); it != gHistory.end(); ++it)
		if (NeedsReadAgain(it->first, targetDB))
			return kTrue;
	return kFalse;
}

void KCMTrackList::NoteStoryCounters(IDataBase* targetDB)
{
	if (targetDB == nil)
		return;
	// The listed stories AND every story noted before (2026-10-25): a story that has lost all its rows - every change
	// in it rejected - keeps its place in the history, and its counter is read now too. Read only for the listed ones
	// (until 2026-10-25), it stayed at the value from before the reject, and the reject's Undo, which takes the counter
	// back to exactly that value, read as "nothing moved" (tests-track-ar.ps1 AR-8: Reject All Changes in Document, then
	// Ctrl+Z - the list stayed empty).
	std::vector<UID> stories;
	GetStoryUIDs(stories);
	for (std::map<UID, CounterHistory>::const_iterator it = gHistory.begin(); it != gHistory.end(); ++it)
		if (std::find(stories.begin(), stories.end(), it->first) == stories.end())
			stories.push_back(it->first);
	for (size_t i = 0; i < stories.size(); ++i)
	{
		CounterHistory& h = gHistory[stories[i]];
		h.fLast = CounterOf(targetDB, stories[i]);
		if (std::find(h.fSeen.begin(), h.fSeen.end(), h.fLast) == h.fSeen.end())
			h.fSeen.push_back(h.fLast);
	}
}

void KCMTrackList::NoteRecordCount(int32 records)
{
	gRecordsAtRead = records;
}

int32 KCMTrackList::GetRecordCountAtRead()
{
	return gRecordsAtRead;
}

void KCMTrackList::RowsAsTsv(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	// (The heading's notRejectedInCopy went on 2026-10-27 with the copy; its pairs are read by name.)
	out.Append("#track\totherRecords\t");
	out.AppendNumber(gOtherRecords);
	out.Append("\n");
	for (size_t a = 0; a < gAuthors.size(); ++a)
	{
		const KCMTrackAuthor& au = gAuthors[a];
		out.Append("author\t");
		AppendCell(out, au.fName);
		out.Append("\t");
		AppendColour(out, au.fHasColour, au.fR, au.fG, au.fB);
		out.Append("\t"); out.AppendNumber(au.fRowCount);
		out.Append("\t"); out.AppendNumber(au.fChangeCount);
		out.Append("\n");
	}
	// change  author  row  change  kind  uid  tstart  tend  sstart  send  srcexact  colour  hidden  new  old
	// (sstart send srcexact are 0 since 2026-10-27 - no copy - and stay, so that the columns the tests read do not move.)
	for (size_t r = 0; r < gRows.size(); ++r)
	{
		const KCMTrackRow& row = gRows[r];
		for (size_t c = 0; c < row.fChanges.size(); ++c)
		{
			const KCMTrackChange& ch = row.fChanges[c];
			out.Append("change\t"); out.AppendNumber(row.fAuthor);
			out.Append("\t"); out.AppendNumber(static_cast<int32>(r));
			out.Append("\t"); out.AppendNumber(static_cast<int32>(c));
			out.Append("\t"); out.Append(KindWord(ch.fKind));
			out.Append("\t"); out.AppendNumber(static_cast<int32>(row.fRow.fStoryUID.Get()));
			out.Append("\t"); out.AppendNumber(ch.fChange.fTargetStart);
			out.Append("\t"); out.AppendNumber(ch.fChange.fTargetEnd);
			out.Append("\t"); out.AppendNumber(ch.fChange.fSourceStart);
			out.Append("\t"); out.AppendNumber(ch.fChange.fSourceEnd);
			out.Append("\t"); out.AppendNumber(ch.fSourceExact ? 1 : 0);
			out.Append("\t"); AppendColour(out, ch.fHasColour, ch.fR, ch.fG, ch.fB);
			out.Append("\t"); out.AppendNumber(ch.fHidden ? 1 : 0);
			out.Append("\t"); AppendCell(out, ch.fChange.fText);
			out.Append("\t"); AppendCell(out, ch.fChange.fOtherText);
			out.Append("\n");
		}
	}
}

void KCMTrackList::ShutdownCleanup()
{
	KCMTrackList::Clear();
}

// End, KCMTrackList.cpp.
