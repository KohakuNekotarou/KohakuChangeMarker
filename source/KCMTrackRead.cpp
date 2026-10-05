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
#include "PersistUtils.h"			// ::GetClass
#include "TablesID.h"				// kTextCellContentBoss
#include "TextChar.h"
#include "TextIterator.h"
#include "Utils.h"
#include "VOSRedline.h"
#include "WideString.h"
#include "redlineiterator.h"

#include <algorithm>
#include <string>

// Project includes:
#include "KCMDiag.h"				// KCM_DIAG_LOG - test builds only (nothing in a shipping .pln)
#include "KCMProgressBar.h"			// KCMProgressStepper - the bar after kKCMProgressBarDelayMs, as the Story diff has
#include "KCMStoryList.h"			// KCMStoryList::ReadRowForStory, KCMStoryFrameAt
#include "KCMStoryKinds.h"
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

// Where hidden conditional text comes back to (KFCTrackChange::HiddenTextAnchor, the same climb).
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

// One character as the list shows it - KCMStoryDiffRun's MarkUpBreaks, on UTF-32: a paragraph end as ¶, a
// forced line break as ↵, an anchored object as ⚓, a table as one ▦; other control characters are left out.
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
	if (v == kTextChar_CR)
		out.Append(0x00B6);
	else if (v == kTextChar_LF)
		out.Append(0x21B5);
	else if (v == kTextChar_ObjectReplacementCharacter)
		out.Append(0x2693);
	else if (v >= 0x20 || v == kTextChar_Tab)
		out.Append(v);
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
	if (to <= from)
		return w;
	TextIterator it(model, from);
	for (TextIndex i = from; i < to && !it.IsNull(); ++i, ++it)
		w.Append((*it).GetValue());
	return w;
}

PMString ToPM(const WideString& w)
{
	PMString s(w);
	s.SetTranslatable(kFalse);
	return s;
}

// The deleted text - ITrackChangeUtils::GetDeletedText first, the iterator's description when it reads
// nothing (KFCTrackChange's ReadDeletedText).
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

std::u16string SortKey(const PMString& s)
{
	WideString w(s);
	int32 n = 0;
	const UTF16TextChar* b = w.GrabUTF16Buffer(&n);
	return (b != nil) ? std::u16string(reinterpret_cast<const char16_t*>(b), static_cast<size_t>(n)) : std::u16string();
}

// Is copyDB's story at [from, to) these characters? (The check behind fSourceExact.)
bool16 CopyReads(IDataBase* copyDB, UID story, TextIndex from, const WideString& want)
{
	InterfacePtr<ITextModel> model(copyDB, story, UseDefaultIID());
	if (model == nil || from < 0 || from + want.CharCount() > model->TotalLength())
		return kFalse;
	return (ReadRaw(model, from, from + want.CharCount()) == want) ? kTrue : kFalse;
}

// The records the copy still holds - what "every change rejected" could not take back.
int32 CountRecords(IDataBase* db)
{
	if (db == nil)
		return 0;
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	const int32 count = (stories != nil) ? stories->GetAllTextModelCount() : 0;
	int32 n = 0;
	for (int32 i = 0; i < count; ++i)
	{
		InterfacePtr<ITextModel> model(stories->GetNthTextModelUID(i), UseDefaultIID());
		InterfacePtr<IRedlineDataStrand> redline(model != nil ? QueryRedline(model) : nil);
		RedlineIterator* it = (redline != nil) ? redline->NewRedlineIterator(0) : nil;
		if (it == nil)
			continue;
		for (bool16 more = kTrue; more; more = it->Increment(kFalse))
		{
			const VOSRedlineChange* record = it->GetCurrentChangeRecord();
			if (record == nil)
				continue;
			++n;
			delete record;
		}
		delete it;
	}
	return n;
}
}	// namespace

int32 KCMTrackRead::Build(IDataBase* targetDB, IDataBase* sourceDB, bool16* outCancelled)
{
	if (outCancelled != nil)
		*outCancelled = kFalse;
	std::vector<KCMTrackAuthor> authors;
	std::vector<KCMTrackRow> rows;
	if (targetDB == nil)
	{
		KCMTrackList::Set(authors, rows, 0, 0);
		return 0;
	}

	// Is the Source the copy "Compare with Tracked Changes..." made from targetDB? Not knowable until that copy
	// exists (KCMTrackOriginal.cpp, Task 6 of the plan): until then no Source is one, and every Source range is
	// (0,0) - the story's start.
	const bool16 sourceIsCopy = kFalse;
	IDataBase::SaveRestoreModifiedState dirtyGuard(targetDB);	// KCMStoryFrameAt composes (the overset test)
	Utils<ITrackChangeUtils> utils;
	InterfacePtr<IStoryList> stories(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	const int32 count = (stories != nil) ? stories->GetAllTextModelCount() : 0;

	PMString barTitle("Reading tracked changes...");
	barTitle.SetTranslatable(kFalse);
	KCMProgressStepper progress(barTitle, count);
	int32 otherRecords = 0;
	std::vector<bool16> colourAsked;	// per author: its colour has been asked (once - it is the author's)

	for (int32 s = 0; s < count; ++s)
	{
		PMString item("Story ");
		item.AppendNumber(s + 1);
		item.Append(" / ");
		item.AppendNumber(count);
		item.SetTranslatable(kFalse);
		progress.Step(s, item);

		const UIDRef storyRef = stories->GetNthTextModelUID(s);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		InterfacePtr<IRedlineDataStrand> redline(model != nil ? QueryRedline(model) : nil);
		RedlineIterator* it = (redline != nil) ? redline->NewRedlineIterator(0) : nil;
		if (it == nil)
			continue;

		std::vector<KCMTrackRecord> recs;
		std::vector<WideString> deleted;
		for (bool16 more = kTrue; more; more = it->Increment(kFalse))
		{
			TextIndex at = 0;
			int32 len = 0;
			const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
			if (record == nil)
				continue;
			const ClassID thread = ThreadClassAt(model, at);
			if (thread == kNoteDataBoss)
			{
				delete record;			// a note's text is not the document's (design 3-2)
				continue;
			}
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
			if (colourAsked.size() < authors.size())
				colourAsked.resize(authors.size(), kFalse);
			// The colour is the AUTHOR's (memory track-change-author-colour): asked of the first record of each author.
			if (!colourAsked[static_cast<size_t>(author)] && utils)
			{
				colourAsked[static_cast<size_t>(author)] = kTrue;
				KCMTrackAuthor& au = authors[static_cast<size_t>(author)];
				const RealAGMColor c = utils->GetTrackedChangeBGColor(targetDB, it);
				const double cr = ::ToDouble(c.red), cg = ::ToDouble(c.green), cb = ::ToDouble(c.blue);
				if (!KCMTrackColourIsWhite(cr, cg, cb))
				{
					au.fHasColour = kTrue;
					au.fR = (uint8)(cr * 255.0 + 0.5);
					au.fG = (uint8)(cg * 255.0 + 0.5);
					au.fB = (uint8)(cb * 255.0 + 0.5);
				}
				KCM_DIAG_LOG("track read: author %d colour %.3f %.3f %.3f -> %s", author, cr, cg, cb,
							 au.fHasColour ? "own" : "white = the Mark colour");
			}
			WideString del;
			if (r.fKind == kKCMTrackRecDelete)
			{
				del = DeletedText(model, utils, it, at);
				r.fLen = del.CharCount();
			}
			else
				r.fLen = len;
			recs.push_back(r);
			deleted.push_back(del);
			delete record;				// the caller owns it (redlineiterator.h:137-138)
		}
		delete it;
		if (recs.empty())
			continue;

		std::vector<KCMTrackPlanned> planned;
		int32 other = 0;
		KCMPlanTrackChanges(recs, planned, other);
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
				// The words: 14 code points either side, the change cut at 300.
				const TextIndex preFrom = (from > kContextCodePoints) ? (from - kContextCodePoints) : 0;
				const TextIndex total = model->TotalLength() - 1;		// not the story's final return
				const TextIndex postTo = (to + kContextCodePoints < total) ? (to + kContextCodePoints) : total;
				sc.fTextPre  = ToPM(Shown(ReadRaw(model, preFrom, from), kContextCodePoints, kFalse, preFrom > 0));
				sc.fText     = p.fHidden ? ToPM(Shown(ReadRaw(model, p.fFrom, p.fTo), kChangeCodePoints, kFalse, kFalse))
										 : ToPM(Shown(ReadRaw(model, from, to), kChangeCodePoints, kFalse, kFalse));
				sc.fTextPost = ToPM(Shown(ReadRaw(model, to, postTo), kContextCodePoints, postTo < total, kFalse));
				sc.fOtherTextPre  = sc.fTextPre;
				sc.fOtherText     = (p.fDelRec >= 0) ? ToPM(Shown(deleted[static_cast<size_t>(p.fDelRec)], kChangeCodePoints, kFalse, kFalse)) : PMString();
				sc.fOtherText.SetTranslatable(kFalse);
				sc.fOtherTextPost = sc.fTextPost;
				// Overset: a change composed into no frame (the jump then goes to the "+").
				sc.fOverset = (!p.fHidden && KCMStoryFrameAt(targetDB, storyRef.GetUID(), from, to <= from) == kInvalidUID) ? kTrue : kFalse;
				// The original: exact only against the copy, and only when the copy reads as expected there.
				sc.fSourceStart = sc.fSourceEnd = 0;
				if (sourceIsCopy && !p.fHidden)
				{
					bool16 ok = kFalse;
					if (p.fDelRec >= 0)
						ok = CopyReads(sourceDB, storyRef.GetUID(), p.fOrigFrom, deleted[static_cast<size_t>(p.fDelRec)]);
					else
					{
						const int32 k = (p.fFrom < 4) ? p.fFrom : 4;
						ok = (k > 0 && p.fOrigFrom >= k)
							? CopyReads(sourceDB, storyRef.GetUID(), p.fOrigFrom - k, ReadRaw(model, p.fFrom - k, p.fFrom))
							: (p.fOrigFrom == 0 && p.fFrom == 0);
					}
					if (!ok)
						KCM_DIAG_LOG("track read: story %u change at %d: the copy does not read as expected at %d - Source range left at the start",
									 (unsigned)storyRef.GetUID().Get(), (int)p.fFrom, (int)p.fOrigFrom);
					if (ok)
					{
						sc.fSourceStart = p.fOrigFrom;
						sc.fSourceEnd = p.fOrigTo;
						ch.fSourceExact = kTrue;
					}
				}
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

	// Authors by name; their rows consecutive, by page then story; colours onto each change.
	std::vector<int32> order(authors.size());
	for (size_t i = 0; i < order.size(); ++i)
		order[i] = static_cast<int32>(i);
	std::sort(order.begin(), order.end(), [&](int32 x, int32 y) { return SortKey(authors[x].fName) < SortKey(authors[y].fName); });
	std::vector<int32> newIndex(authors.size());
	std::vector<KCMTrackAuthor> sorted;
	for (size_t i = 0; i < order.size(); ++i)
	{
		newIndex[static_cast<size_t>(order[i])] = static_cast<int32>(i);
		sorted.push_back(authors[static_cast<size_t>(order[i])]);
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
	for (size_t a = 0; a < sorted.size(); ++a)
	{
		sorted[a].fFirstRow = 0;
		sorted[a].fRowCount = 0;
		sorted[a].fChangeCount = 0;
	}
	int32 total = 0;
	for (size_t r = 0; r < rows.size(); ++r)
	{
		KCMTrackAuthor& au = sorted[static_cast<size_t>(rows[r].fAuthor)];
		if (au.fRowCount == 0)
			au.fFirstRow = static_cast<int32>(r);
		++au.fRowCount;
		au.fChangeCount += static_cast<int32>(rows[r].fChanges.size());
		total += static_cast<int32>(rows[r].fChanges.size());
		for (size_t c = 0; c < rows[r].fChanges.size(); ++c)
		{
			KCMTrackChange& ch = rows[r].fChanges[c];
			ch.fHasColour = au.fHasColour;
			ch.fR = au.fR; ch.fG = au.fG; ch.fB = au.fB;
		}
	}
	// Drop authors with no row (their only records were left out).
	std::vector<KCMTrackAuthor> kept;
	std::vector<int32> remap(sorted.size(), -1);
	for (size_t a = 0; a < sorted.size(); ++a)
		if (sorted[a].fRowCount > 0)
		{
			remap[a] = static_cast<int32>(kept.size());
			kept.push_back(sorted[a]);
		}
	for (size_t r = 0; r < rows.size(); ++r)
	{
		rows[r].fAuthor = remap[static_cast<size_t>(rows[r].fAuthor)];
		for (size_t c = 0; c < rows[r].fChanges.size(); ++c)
			rows[r].fChanges[c].fAuthor = rows[r].fAuthor;
	}

	KCMTrackList::Set(kept, rows, sourceIsCopy ? CountRecords(sourceDB) : 0, otherRecords);
	KCMTrackList::NoteStoryCounters(targetDB);
	KCM_DIAG_LOG("track read: done changes=%d authors=%d rows=%d sourceIsCopy=%d other=%d", (int)total, (int)kept.size(),
				 (int)rows.size(), (int)sourceIsCopy, (int)otherRecords);
	return total;
}

bool16 KCMTrackRead::StillRecorded(IDataBase* targetDB, UID storyUID, const KCMTrackChange& change)
{
	InterfacePtr<ITextModel> model(targetDB, storyUID, UseDefaultIID());
	InterfacePtr<IRedlineDataStrand> redline(model != nil ? QueryRedline(model) : nil);
	RedlineIterator* it = (redline != nil) ? redline->NewRedlineIterator(0) : nil;
	if (it == nil)
		return kFalse;
	const int32 wantKind = (change.fKind == kKCMTrackDelete) ? VOSRedlineChange::kDelete : VOSRedlineChange::kInsert;
	bool16 found = kFalse;
	for (bool16 more = kTrue; more && !found; more = it->Increment(kFalse))
	{
		const VOSRedlineChange* record = it->GetCurrentChangeRecord();
		if (record == nil)
			continue;
		found = (record->GetTimeStamp() == change.fTime && record->GetChangeType() == wantKind) ? kTrue : kFalse;
		delete record;
	}
	delete it;
	KCM_DIAG_LOG("track still recorded: story %u time %llu kind %d -> %d", (unsigned)storyUID.Get(),
				 (unsigned long long)change.fTime, (int)change.fKind, (int)found);
	return found;
}

// End, KCMTrackRead.cpp.
