//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - reading the tracked changes. See KCMTrackRead.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IOwnedItem.h"				// where hidden conditional text comes back to
#include "IRedlineDataStrand.h"
#include "IStoryList.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITrackChangeUtils.h"		// the deleted text, and the colour a record is drawn in

// General includes:
#include "ConditionalTextID.h"		// kHiddenTextBoss
#include "InCopySharedID.h"			// kRedlineStrandBoss, kNoteDataBoss
#include "K2SmartPtr.h"				// the iterators and the records are the caller's to delete (redlineiterator.h)
#include "PersistUtils.h"			// ::GetClass
#include "TablesID.h"				// kTextCellContentBoss
#include "TextChar.h"
#include "Utils.h"
#include "VOSRedline.h"
#include "WideString.h"
#include "redlineiterator.h"

#include <algorithm>

// Project includes:
#include "KCMCore.h"				// KCMIsDocDBOpen - neither document is trusted to be open
#include "KCMDiag.h"				// KCM_DIAG_LOG - test builds only (nothing in a shipping .pln)
#include "KCMProgressBar.h"			// KCMProgressStepper - the bar after kKCMProgressBarDelayMs, as the Story diff has
#include "KCMSentence.h"			// the bar's fixed words
#include "KCMOversetPoint.h"		// KCMChangeIsOverset
#include "KCMParaText.h"			// ShownCodePoint - one table of what a row shows
#include "KCMStoryList.h"			// KCMStoryList::ReadRowForStory
#include "KCMStoryKinds.h"
#include "KCMTextWords.h"			// WordsAt
#include "KCMTrackList.h"
#include "KCMTrackPlan.h"
#include "KCMTrackRead.h"

namespace
{
const int32 kContextCodePoints = 14;	// KCMStoryDiffRun's, so that a Track row reads like a Story row
const int32 kChangeCodePoints = 300;	// a longer change is shown cut, with "…" (KFC's Source Text rule)

IRedlineDataStrand* QueryRedline(ITextModel* model)
{
	return static_cast<IRedlineDataStrand*>(model->QueryStrand(kRedlineStrandBoss, IRedlineDataStrand::kDefaultIID));
}

ClassID ThreadClassAt(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	return (thread != nil) ? ::GetClass(thread) : kInvalidClass;
}

// Where hidden conditional text comes back to - the climb KFC's track code made (KFCTrackChange::HiddenTextAnchor,
// out of KFC since its 809fd58).
TextIndex HiddenTextAnchor(ITextModel* model, TextIndex at)
{
	TextIndex anchor = kInvalidTextIndex;
	TextIndex pos = at;
	for (int32 depth = 0; depth < 8; ++depth)
	{
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(pos, nil, nil));
		if (thread == nil || ::GetClass(thread) != kHiddenTextBoss)
			break;
		InterfacePtr<IOwnedItem> owned(thread, UseDefaultIID());
		const TextIndex next = (owned != nil) ? owned->GetTextIndex() : kInvalidTextIndex;
		if (next == kInvalidTextIndex || next == pos)
			break;
		anchor = pos = next;
	}
	return anchor;
}

// One character as the list shows it - KCMParaText::ShownCodePoint, a table as one ▦.
void AppendShown(WideString& out, uint32 v, bool& inTable)
{
	if (v == kTextChar_Table || v == kTextChar_TableContinued)
	{
		if (!inTable)
			out.Append(0x25A6);			// U+25A6 - KCM's table sign (KCMStoryList.cpp, kKCMTableSign)
		inTable = true;
		return;
	}
	inTable = false;
	const int32 shown = KCMParaText::ShownCodePoint(static_cast<int32>(v));
	if (shown >= 0)
		out.Append(static_cast<uint32>(shown));
}

WideString Shown(const WideString& raw, int32 maxCodePoints, bool16 ellipsisAtEnd, bool16 ellipsisAtStart)
{
	WideString w;
	bool inTable = false;
	if (ellipsisAtStart)
		w.Append(0x2026);
	int32 n = 0;
	for (WideString::const_iterator it = raw.begin(); it != raw.end(); ++it)
	{
		if (n >= maxCodePoints)
		{
			w.Append(0x2026);
			return w;
		}
		AppendShown(w, *it, inTable);		// the iterator hands out a UniCodePoint (uint32), WideString.h
		++n;
	}
	if (ellipsisAtEnd)
		w.Append(0x2026);
	return w;
}

WideString ReadRaw(ITextModel* model, TextIndex from, TextIndex to)
{
	WideString w;
	KCMTextWords::WordsAt(model, from, to - from, w);		// a range outside the story reads as nothing
	return w;
}

PMString ToPM(const WideString& w)
{
	PMString s(w);
	s.SetTranslatable(kFalse);
	return s;
}

// The deleted text - ITrackChangeUtils::GetDeletedText first, the iterator's description when it reads
// nothing (as KFCTrackChange's ReadDeletedText did, out of KFC since its 809fd58).
WideString DeletedText(ITextModel* model, Utils<ITrackChangeUtils>& utils, RedlineIterator* it, TextIndex at)
{
	WideString text;
	if (utils)
		utils->GetDeletedText(model, at, text);
	if (text.CharCount() == 0)
	{
		PMString described;
		it->DescribeChangeContent(described, 0x7fffffff);
		text = WideString(described);
	}
	return text;
}

/** The colour InDesign paints one record on. Kept apart from KCMTrackRecord, which holds no SDK answer of this kind for KCMTrackPlan.h's
	pure tests. */
struct RecordColour
{
	bool16	fHas;		// kFalse = the galley's white: the Mark colour stands in
	uint8	fR, fG, fB;
	RecordColour() : fHas(kFalse), fR(0), fG(0), fB(0) {}
};

/** ASKED OF EACH RECORD, because it is the record's: the Track Changes preferences choose per kind between the author's colour and one of
	their own - a deletion given its own colour read as the author's, on the author's other changes as well, when the first was asked for
	all of them (measured). With the three choices at their default the answers are the author's colour. */
RecordColour ColourOfRecord(IDataBase* db, Utils<ITrackChangeUtils>& utils, RedlineIterator* it)
{
	RecordColour out;
	if (!utils)
		return out;
	const RealAGMColor c = utils->GetTrackedChangeBGColor(db, it);
	const double r = ::ToDouble(c.red), g = ::ToDouble(c.green), b = ::ToDouble(c.blue);
	if (!KCMTrackColourIsWhite(r, g, b))
	{
		out.fHas = kTrue;
		out.fR = (uint8)(r * 255.0 + 0.5);
		out.fG = (uint8)(g * 255.0 + 0.5);
		out.fB = (uint8)(b * 255.0 + 0.5);
	}
	return out;
}

int32 AuthorIndex(std::vector<KCMTrackAuthor>& authors, const PMString& name)
{
	for (size_t i = 0; i < authors.size(); ++i)
		if (authors[i].fName == name)
			return static_cast<int32>(i);
	KCMTrackAuthor a;
	a.fName = name;
	a.fName.SetTranslatable(kFalse);
	authors.push_back(a);
	return static_cast<int32>(authors.size() - 1);
}

// Every tracked-change record of the stories Build reads, a note's too - the follower's count (RecordsMovedSinceRead).
int32 CountRecords(IDataBase* db)
{
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	const int32 count = (stories != nil) ? stories->GetUserAccessibleStoryCount() : 0;
	int32 n = 0;
	for (int32 i = 0; i < count; ++i)
	{
		InterfacePtr<ITextModel> model(stories->GetNthUserAccessibleStoryUID(i), UseDefaultIID());
		InterfacePtr<IRedlineDataStrand> redline(model != nil ? QueryRedline(model) : nil);
		K2::scoped_ptr<RedlineIterator> it((redline != nil) ? redline->NewRedlineIterator(0) : nil);
		if (it.get() == nil)
			continue;
		for (bool16 more = kTrue; more; more = it->Increment(kFalse))
		{
			K2::scoped_ptr<const VOSRedlineChange> record(it->GetCurrentChangeRecord());
			if (record.get() != nil)
				++n;
		}
	}
	return n;
}
}	// namespace

int32 KCMTrackRead::Build(IDataBase* targetDB, bool16* outCancelled)
{
	if (outCancelled != nil)
		*outCancelled = kFalse;
	std::vector<KCMTrackAuthor> authors;
	std::vector<KCMTrackRow> rows;
	// THE TARGET ASKED FOR AGAIN, NOT TRUSTED: a caller hands over the armed pointer, and the
	//   Target may have been closed since - a closed database is never dereferenced (KCMIsDocDBOpen compares pointers
	//   only). A Target that is gone leaves an empty list.
	if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
	{
		KCMTrackList::Set(authors, rows, 0);
		return 0;
	}

	IDataBase::SaveRestoreModifiedState dirtyGuard(targetDB);	// KCMChangeIsOverset composes
	Utils<ITrackChangeUtils> utils;
	InterfacePtr<IStoryList> stories(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	const int32 count = (stories != nil) ? stories->GetUserAccessibleStoryCount() : 0;

	const PMString barTitle = KCMSentence("Reading tracked changes...");
	KCMProgressStepper progress(barTitle, count);
	int32 otherRecords = 0;
	int32 targetRecords = 0;			// every record read, a note's too - CountRecords' count (RecordsMovedSinceRead)
	std::vector<bool16> authorColoured;	// per author: the author's row has its colour

	for (int32 s = 0; s < count; ++s)
	{
		PMString item = KCMSentence("Story ");	// not a key: it carries numbers (Append keeps the mark)
		item.AppendNumber(s + 1);
		item.Append(" / ");
		item.AppendNumber(count);
		progress.Step(s, item);
		KCM_DIAG_SLOW("track-read-slow", 200);	// test builds only: the bar and its Cancel within reach (KCMDiag.h)

		const UIDRef storyRef = stories->GetNthUserAccessibleStoryUID(s);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		InterfacePtr<IRedlineDataStrand> redline(model != nil ? QueryRedline(model) : nil);
		K2::scoped_ptr<RedlineIterator> it((redline != nil) ? redline->NewRedlineIterator(0) : nil);
		if (it.get() == nil)
			continue;

		std::vector<KCMTrackRecord> recs;
		std::vector<WideString> deleted;
		std::vector<RecordColour> colours;
		for (bool16 more = kTrue; more; more = it->Increment(kFalse))
		{
			TextIndex at = 0;
			int32 len = 0;
			K2::scoped_ptr<const VOSRedlineChange> record(it->GetCurrentChangeRecord(&at, &len));
			if (record.get() == nil)
				continue;
			++targetRecords;
			const ClassID thread = ThreadClassAt(model, at);
			if (thread == kNoteDataBoss)
				continue;				// a note's text is not the document's (design 3-2)
			KCMTrackRecord r;
			const VOSRedlineChange::RedlineChangeType type = record->GetChangeType();
			r.fKind = (type == VOSRedlineChange::kInsert) ? kKCMTrackRecInsert
					: (type == VOSRedlineChange::kDelete) ? kKCMTrackRecDelete : kKCMTrackRecOther;
			r.fMoved = record->GetIsMovedText();
			r.fAt = at;
			r.fTime = record->GetTimeStamp();
			r.fHidden = (thread == kHiddenTextBoss) ? kTrue : kFalse;
			const int32 author = AuthorIndex(authors, record->GetUserName());
			r.fAuthor = author;
			const RecordColour colour = ColourOfRecord(targetDB, utils, it.get());
			if (authorColoured.size() < authors.size())
				authorColoured.resize(authors.size(), kFalse);
			// The author's own row: the colour of their first record (memory track-change-author-colour).
			if (!authorColoured[static_cast<size_t>(author)])
			{
				authorColoured[static_cast<size_t>(author)] = kTrue;
				KCMTrackAuthor& au = authors[static_cast<size_t>(author)];
				au.fHasColour = colour.fHas;
				au.fR = colour.fR; au.fG = colour.fG; au.fB = colour.fB;
				KCM_DIAG_LOG("track read: author %d colour %d %d %d -> %s", author, (int)colour.fR, (int)colour.fG, (int)colour.fB,
							 colour.fHas ? "own" : "white = the Mark colour");
			}
			WideString del;
			if (r.fKind == kKCMTrackRecDelete)
				del = DeletedText(model, utils, it.get(), at);
			else
				r.fLen = len;
			recs.push_back(r);
			deleted.push_back(del);
			colours.push_back(colour);
		}
		it.reset();		// not held over what follows: WasCancelled at the end of this loop pumps events
		if (recs.empty())
			continue;

		std::vector<KCMTrackPlanned> planned;
		int32 other = 0;
		KCMPlanTrackChanges(recs, planned, other);

		// A move's two rows: each is given the other's records as well (KCMTrackChange::fRecords below), so that accepting or
		// rejecting either does the whole move.
		std::vector<int32> partner;
		KCMPairMoves(recs, planned, [&](int32 insRec, int32 delRec)
		{
			const KCMTrackRecord& ins = recs[static_cast<size_t>(insRec)];
			return deleted[static_cast<size_t>(delRec)] == ReadRaw(model, ins.fAt, ins.fAt + ins.fLen);
		}, partner);
		KCM_DIAG_LOG("track read: story %u records=%d planned=%d other=%d", (unsigned)storyRef.GetUID().Get(),
					 (int)recs.size(), (int)planned.size(), (int)other);
		otherRecords += other;

		// One row per author who has changes in this story.
		for (size_t a = 0; a < authors.size(); ++a)
		{
			KCMTrackRow row;
			row.fAuthor = static_cast<int32>(a);
			for (size_t k = 0; k < planned.size(); ++k)
			{
				const KCMTrackPlanned& p = planned[k];
				if (p.fAuthor != static_cast<int32>(a))
					continue;
				KCMTrackChange ch;
				ch.fAuthor = p.fAuthor;
				ch.fKind = p.fKind;
				ch.fTime = p.fTime;
				ch.fHidden = p.fHidden;
				// A replacement is shown by what it put in: the insertion's colour.
				const RecordColour& colour = colours[static_cast<size_t>((p.fInsRec >= 0) ? p.fInsRec : p.fDelRec)];
				ch.fHasColour = colour.fHas;
				ch.fR = colour.fR; ch.fG = colour.fG; ch.fB = colour.fB;
				// THE RECORDS IT WAS MADE OF: Accept and Reject find them again by these (KCMTrackResolve) -
				//   a replacement is two, the insertion and the deletion beside it; a move's row carries its other half's too.
				const KCMTrackPlanned* const halves[2] = { &p, (partner[k] >= 0) ? &planned[static_cast<size_t>(partner[k])] : nil };
				for (int32 h = 0; h < 2; ++h)
				{
					if (halves[h] == nil)
						continue;
					const int32 own[2] = { halves[h]->fInsRec, halves[h]->fDelRec };
					for (int32 o = 0; o < 2; ++o)
					{
						if (own[o] < 0)
							continue;
						const KCMTrackRecord& rec = recs[static_cast<size_t>(own[o])];
						KCMTrackRecordRef ref;
						ref.fDelete = (rec.fKind == kKCMTrackRecDelete) ? kTrue : kFalse;
						ref.fAt = rec.fAt;
						ref.fTime = rec.fTime;
						ch.fRecords.push_back(ref);
					}
				}
				KCMStoryChange& sc = ch.fChange;
				sc.fWhat = KCMStoryChange::kText;
				sc.fKind = (p.fKind == kKCMTrackDelete) ? KCMStoryChange::kDelete
						 : (p.fKind == kKCMTrackReplace) ? KCMStoryChange::kReplace : KCMStoryChange::kInsert;
				const ClassID thread = ThreadClassAt(model, p.fFrom);
				sc.fPlace = (thread == kTextCellContentBoss) ? kKCMPlaceCell : kKCMPlaceBody;
				// Where a click goes: the change itself, or - in hidden conditional text - where it comes back to.
				TextIndex from = p.fFrom, to = p.fTo;
				if (p.fHidden)
				{
					const TextIndex anchor = HiddenTextAnchor(model, p.fFrom);
					from = to = (anchor != kInvalidTextIndex) ? anchor : 0;
				}
				sc.fTargetStart = from;
				sc.fTargetEnd = to;
				// The words: 14 code points either side, the change cut at 300 - and INSIDE THE CHANGE'S OWN STORY THREAD
				//   (the body, one table cell, the text it comes back to), its terminator left out. Measured: read
				//   across threads, a cell's row showed the words outside the table and the cells' terminators, and a
				//   hidden change's row ran on into the hidden text stored after the story's end.
				TextIndex threadStart = 0;
				int32 threadSpan = 0;
				model->GetStoryThreadSpan(from, &threadStart, &threadSpan);
				TextIndex total = (threadSpan > 0) ? (threadStart + threadSpan - 1) : (model->TotalLength() - 1);
				if (threadSpan <= 0)
					threadStart = 0;
				if (total < to)
					total = to;		// a deletion anchored on the terminator itself
				const TextIndex preFrom = (from - threadStart > kContextCodePoints) ? (from - kContextCodePoints) : threadStart;
				const TextIndex postTo = (to + kContextCodePoints < total) ? (to + kContextCodePoints) : total;
				sc.fTextPre  = ToPM(Shown(ReadRaw(model, preFrom, from), kContextCodePoints, kFalse, preFrom > threadStart));
				sc.fText     = p.fHidden ? ToPM(Shown(ReadRaw(model, p.fFrom, p.fTo), kChangeCodePoints, kFalse, kFalse))
										 : ToPM(Shown(ReadRaw(model, from, to), kChangeCodePoints, kFalse, kFalse));
				sc.fTextPost = ToPM(Shown(ReadRaw(model, to, postTo), kContextCodePoints, postTo < total, kFalse));
				sc.fOtherTextPre  = sc.fTextPre;
				sc.fOtherText     = (p.fDelRec >= 0) ? ToPM(Shown(deleted[static_cast<size_t>(p.fDelRec)], kChangeCodePoints, kFalse, kFalse)) : PMString();
				sc.fOtherText.SetTranslatable(kFalse);
				sc.fOtherTextPost = sc.fTextPost;
				// Overset: a change that stands or runs past the frames (the jump goes to the "+" when it begins there).
				sc.fOverset = (!p.fHidden && KCMChangeIsOverset(model, from, to)) ? kTrue : kFalse;
				// No place in an original: a Track listing has no Source.
				sc.fSourceStart = sc.fSourceEnd = 0;
				row.fChanges.push_back(ch);
			}
			if (row.fChanges.empty())
				continue;
			if (!KCMStoryList::ReadRowForStory(targetDB, storyRef.GetUID(), row.fRow))
				continue;
			row.fRow.fKinds = kKCMStoryKindText;
			row.fRow.fTextCompared = kTrue;
			row.fRow.fHasTextChange = kTrue;
			rows.push_back(row);
		}

		if (s + 1 < count && progress.WasCancelled())
		{
			if (outCancelled != nil)
				*outCancelled = kTrue;
			KCM_DIAG_LOG("track read: cancelled after story %d of %d", (int)(s + 1), (int)count);
			return -1;
		}
	}

	// The authors with a row (one whose only records were left out has none), by name; their rows consecutive, by page
	// then story.
	std::vector<bool16> hasRow(authors.size(), kFalse);
	for (size_t r = 0; r < rows.size(); ++r)
		hasRow[static_cast<size_t>(rows[r].fAuthor)] = kTrue;
	std::vector<int32> order;
	for (size_t i = 0; i < authors.size(); ++i)
		if (hasRow[i])
			order.push_back(static_cast<int32>(i));
	std::sort(order.begin(), order.end(), [&](int32 x, int32 y) { return authors[x].fName < authors[y].fName; });
	std::vector<int32> newIndex(authors.size(), -1);
	std::vector<KCMTrackAuthor> kept;
	for (size_t i = 0; i < order.size(); ++i)
	{
		newIndex[static_cast<size_t>(order[i])] = static_cast<int32>(i);
		kept.push_back(authors[static_cast<size_t>(order[i])]);
	}
	for (size_t r = 0; r < rows.size(); ++r)
	{
		rows[r].fAuthor = newIndex[static_cast<size_t>(rows[r].fAuthor)];
		for (size_t c = 0; c < rows[r].fChanges.size(); ++c)
			rows[r].fChanges[c].fAuthor = rows[r].fAuthor;
	}
	std::stable_sort(rows.begin(), rows.end(), [](const KCMTrackRow& x, const KCMTrackRow& y)
	{
		if (x.fAuthor != y.fAuthor) return x.fAuthor < y.fAuthor;
		if (x.fRow.fPageIndex != y.fRow.fPageIndex) return x.fRow.fPageIndex < y.fRow.fPageIndex;
		return x.fRow.fStoryUID.Get() < y.fRow.fStoryUID.Get();
	});
	int32 total = 0;
	for (size_t r = 0; r < rows.size(); ++r)
	{
		KCMTrackAuthor& au = kept[static_cast<size_t>(rows[r].fAuthor)];
		if (au.fRowCount == 0)
			au.fFirstRow = static_cast<int32>(r);
		++au.fRowCount;
		au.fChangeCount += static_cast<int32>(rows[r].fChanges.size());
		total += static_cast<int32>(rows[r].fChanges.size());
	}

	KCMTrackList::Set(kept, rows, otherRecords);
	KCMTrackList::NoteStoryCounters(targetDB);
	KCMTrackList::NoteRecordCount(targetRecords);
	KCM_DIAG_LOG("track read: done changes=%d authors=%d rows=%d other=%d records=%d", (int)total,
				 (int)kept.size(), (int)rows.size(), (int)otherRecords, (int)targetRecords);
	return total;
}

bool16 KCMTrackRead::RecordsMovedSinceRead(IDataBase* targetDB)
{
	const int32 atRead = KCMTrackList::GetRecordCountAtRead();
	if (atRead < 0 || targetDB == nil || !KCMIsDocDBOpen(targetDB))
		return kFalse;
	const int32 now = CountRecords(targetDB);
	KCM_DIAG_LOG("track records: now %d, %d when read", (int)now, (int)atRead);
	return (now != atRead) ? kTrue : kFalse;
}

bool16 KCMTrackRead::StillRecorded(IDataBase* targetDB, UID storyUID, const KCMTrackChange& change)
{
	InterfacePtr<ITextModel> model(targetDB, storyUID, UseDefaultIID());
	InterfacePtr<IRedlineDataStrand> redline(model != nil ? QueryRedline(model) : nil);
	K2::scoped_ptr<RedlineIterator> it((redline != nil) ? redline->NewRedlineIterator(0) : nil);
	if (it.get() == nil)
		return kFalse;
	const int32 wantKind = (change.fKind == kKCMTrackDelete) ? VOSRedlineChange::kDelete : VOSRedlineChange::kInsert;
	// AND THE AUTHOR: stamps are not unique per operation - two separate edits by two authors carried
	//   one stamp in the probe material (docs/ai-notes/kcm-track-probe-2026-10-05.md).
	const KCMTrackAuthor* const author = KCMTrackList::GetAuthor(change.fAuthor);
	bool16 found = kFalse;
	for (bool16 more = kTrue; more && !found; more = it->Increment(kFalse))
	{
		K2::scoped_ptr<const VOSRedlineChange> record(it->GetCurrentChangeRecord());
		if (record.get() == nil)
			continue;
		found = (record->GetTimeStamp() == change.fTime && record->GetChangeType() == wantKind
				 && (author == nil || record->GetUserName() == author->fName)) ? kTrue : kFalse;
	}
	KCM_DIAG_LOG("track still recorded: story %u time %llu kind %d -> %d", (unsigned)storyUID.Get(),
				 (unsigned long long)change.fTime, (int)change.fKind, (int)found);
	return found;
}

// End, KCMTrackRead.cpp.
