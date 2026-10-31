//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - the Track Changes mode's list. See KCMTrackList.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#ifdef KCM_DIAG
#include "TextChar.h"			// kTextChar_Tab / _CR / _LF
#include "WideString.h"
#endif

#include <map>

#include "KCMDiag.h"			// KCM_DIAG_LOG - test builds only (nothing in a shipping .pln)
#include "KCMTrackList.h"
#include "KCMStoryDiffRun.h"	// CountAllChanges
#ifdef KCM_DIAG
#include "KCMTrackPlan.h"		// KCMTrackChangeKind - the kind words
#endif

namespace
{
/** The list and what is kept across its re-reads, as one value: Clear puts all of it back at once, and lets go of the storage
	too (memory plugin-teardown-robustness - vector::clear keeps it). The main thread only: a comparison, the facade, the
	report, the script's door and the Undo follower read it; nothing that draws does. */
struct TrackListState
{
	std::vector<KCMTrackAuthor>			fAuthors;
	std::vector<KCMTrackRow>			fRows;
	int32								fOtherRecords;
	std::map<UID, KCMCounterHistory>	fHistory;
	// The Target's tracked-change records when the list was last read (KCMTrackRead::Build): what an accept or a reject is
	// told by, since it may move no counter (2026-10-25 - NoteRecordCount). -1 = not read since Clear.
	int32								fRecordsAtRead;
	TrackListState() : fOtherRecords(0), fRecordsAtRead(-1) {}
};
TrackListState gList;

#ifdef KCM_DIAG
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
#endif // KCM_DIAG
}	// namespace

void KCMTrackList::Set(const std::vector<KCMTrackAuthor>& authors, const std::vector<KCMTrackRow>& rows, int32 otherRecords)
{
	gList.fAuthors = authors;
	gList.fRows = rows;
	gList.fOtherRecords = otherRecords;
}

void KCMTrackList::Clear()
{
	gList = TrackListState();
}

int32 KCMTrackList::GetRowCount()		{ return static_cast<int32>(gList.fRows.size()); }
int32 KCMTrackList::GetAuthorCount()	{ return static_cast<int32>(gList.fAuthors.size()); }
int32 KCMTrackList::GetOtherRecordCount()	{ return gList.fOtherRecords; }

const KCMTrackRow* KCMTrackList::GetRow(int32 nth)
{
	return (nth >= 0 && nth < static_cast<int32>(gList.fRows.size())) ? &gList.fRows[static_cast<size_t>(nth)] : nil;
}

const KCMTrackAuthor* KCMTrackList::GetAuthor(int32 a)
{
	return (a >= 0 && a < static_cast<int32>(gList.fAuthors.size())) ? &gList.fAuthors[static_cast<size_t>(a)] : nil;
}

int32 KCMTrackList::GetTotalChangeCount()
{
	int32 n = 0;
	for (size_t i = 0; i < gList.fRows.size(); ++i)
		n += static_cast<int32>(gList.fRows[i].fChanges.size());
	return n;
}

bool16 KCMTrackList::AnyNeedsReadAgain(IDataBase* targetDB)
{
	if (targetDB == nil)
		return kFalse;
	for (std::map<UID, KCMCounterHistory>::const_iterator it = gList.fHistory.begin(); it != gList.fHistory.end(); ++it)
	{
		const uint32 now = KCMStoryDiffRun::CountAllChanges(UIDRef(targetDB, it->first));
		KCM_DIAG_LOG("track counters: story %u now %u last read at %u, %d value(s) read", (unsigned)it->first.Get(), (unsigned)now,
					 (unsigned)it->second.fLast, (int)it->second.fSeen.size());
		if (it->second.CameBack(now))
			return kTrue;
	}
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
	for (size_t i = 0; i < gList.fRows.size(); ++i)
		gList.fHistory[gList.fRows[i].fRow.fStoryUID];		// (a listed story new to the history gets its place)
	for (std::map<UID, KCMCounterHistory>::iterator it = gList.fHistory.begin(); it != gList.fHistory.end(); ++it)
		it->second.Note(KCMStoryDiffRun::CountAllChanges(UIDRef(targetDB, it->first)));
}

void KCMTrackList::NoteRecordCount(int32 records)
{
	gList.fRecordsAtRead = records;
}

int32 KCMTrackList::GetRecordCountAtRead()
{
	return gList.fRecordsAtRead;
}

#ifdef KCM_DIAG
void KCMTrackList::RowsAsTsv(PMString& out)
{
	out.clear();
	out.SetTranslatable(kFalse);
	// (The heading's notRejectedInCopy went on 2026-10-27 with the copy; its pairs are read by name.)
	out.Append("#track\totherRecords\t");
	out.AppendNumber(gList.fOtherRecords);
	out.Append("\n");
	for (size_t a = 0; a < gList.fAuthors.size(); ++a)
	{
		const KCMTrackAuthor& au = gList.fAuthors[a];
		out.Append("author\t");
		AppendCell(out, au.fName);
		out.Append("\t");
		AppendColour(out, au.fHasColour, au.fR, au.fG, au.fB);
		out.Append("\t"); out.AppendNumber(au.fRowCount);
		out.Append("\t"); out.AppendNumber(au.fChangeCount);
		out.Append("\n");
	}
	// change  author  row  change  kind  uid  tstart  tend  sstart  send  srcexact  colour  hidden  new  old
	// (sstart send srcexact are always 0 - a Track listing has no Source - and stay, so that the columns the tests read
	//  do not move.)
	for (size_t r = 0; r < gList.fRows.size(); ++r)
	{
		const KCMTrackRow& row = gList.fRows[r];
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
			out.Append("\t0");		// srcexact
			out.Append("\t"); AppendColour(out, ch.fHasColour, ch.fR, ch.fG, ch.fB);
			out.Append("\t"); out.AppendNumber(ch.fHidden ? 1 : 0);
			out.Append("\t"); AppendCell(out, ch.fChange.fText);
			out.Append("\t"); AppendCell(out, ch.fChange.fOtherText);
			out.Append("\n");
		}
	}
}
#endif // KCM_DIAG

void KCMTrackList::ShutdownCleanup()
{
	KCMTrackList::Clear();
}

// End, KCMTrackList.cpp.
