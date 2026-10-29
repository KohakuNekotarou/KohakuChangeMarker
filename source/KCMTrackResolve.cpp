//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - accepting and rejecting the Track mode's changes. See KCMTrackResolve.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IRedlineDataStrand.h"
#include "ITextModel.h"

// General includes:
#include "InCopySharedID.h"			// kRedlineStrandBoss
#include "redlineiterator.h"
#include "VOSRedline.h"

#include <algorithm>
#include <map>

// Project includes:
#include "KCMSentence.h"		// KCMSentence - a fixed sentence, never a key
#include "KCMCore.h"				// KCMIsDocDBOpen - the Target is asked, not trusted
#include "KCMDiag.h"				// KCM_DIAG_LOG - test builds only (nothing in a shipping .pln)
#include "KCMTrackResolve.h"

namespace
{

/** The story's change tracking strand - the way KCMTrackRead reaches it (codesnippets/SnpInspectTextModel.cpp:1027). */
IRedlineDataStrand* QueryRedline(ITextModel* model)
{
	return static_cast<IRedlineDataStrand*>(model->QueryStrand(kRedlineStrandBoss, IRedlineDataStrand::kDefaultIID));
}

/** Is the record the iterator stands on this one - the same kind, time and author? */
bool16 IsRecord(RedlineIterator* it, const KCMTrackResolveItem& want)
{
	const VOSRedlineChange* record = it->GetCurrentChangeRecord();
	if (record == nil)
		return kFalse;
	const bool16 isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete) ? kTrue : kFalse;
	const bool16 same = (isDelete == want.fRecord.fDelete && record->GetTimeStamp() == want.fRecord.fTime
						 && record->GetUserName() == want.fAuthor) ? kTrue : kFalse;
	delete record;			// the caller owns it (redlineiterator.h:137-138)
	return same;
}

/** An iterator left on that record at its place, or nil - KCMRejectImport's FindAt (until 2026-10-25): several records
	can stand at one place (a deletion's mark beside an insertion), and Increment walks them. ⚠The caller deletes it. */
RedlineIterator* FindRecord(IRedlineDataStrand* redline, const KCMTrackResolveItem& want)
{
	const TextIndex at = want.fRecord.fAt;
	RedlineIterator* it = redline->NewRedlineIterator(at);
	if (it == nil)
		return nil;
	for (bool16 more = kTrue; more && it->GetCurrentPosition() <= at; more = it->Increment(kFalse))
	{
		if (it->GetCurrentPosition() == at && IsRecord(it, want))
			return it;
	}
	delete it;
	return nil;
}

}	// anonymous namespace

int32 KCMTrackResolve::Run(IDataBase* targetDB, const std::vector<KCMTrackResolveItem>& items, bool16 accept,
						   int32& outGone, PMString& outMessage)
{
	outGone = 0;
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);
	if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
	{
		outMessage = KCMSentence("the Target document is not open");
		return -1;
	}

	// Story by story, each one's records from its back to its front (KCMTrackResolve.h says why).
	std::map<UID, std::vector<KCMTrackResolveItem> > byStory;
	for (size_t i = 0; i < items.size(); ++i)
		byStory[items[i].fStory].push_back(items[i]);

	int32 done = 0;
	for (std::map<UID, std::vector<KCMTrackResolveItem> >::iterator s = byStory.begin(); s != byStory.end(); ++s)
	{
		std::vector<KCMTrackResolveItem>& list = s->second;
		// Back to front; then each record once - a move's two rows both carry both of its halves (KCMTrackRead), so two
		// rows selected together can name one record twice. The author is part of the order (re-check 2026-10-25): unique
		// below removes only NEIGHBOURS, and two authors' records at one place, of one kind and one stamp (stamps are not
		// unique per edit - KCMTrackRead's StillRecorded) left a twin apart from its pair, to be counted "no longer recorded".
		std::stable_sort(list.begin(), list.end(), [](const KCMTrackResolveItem& a, const KCMTrackResolveItem& b)
		{
			if (a.fRecord.fAt != b.fRecord.fAt) return a.fRecord.fAt > b.fRecord.fAt;
			if (a.fRecord.fDelete != b.fRecord.fDelete) return a.fRecord.fDelete < b.fRecord.fDelete;
			if (a.fRecord.fTime != b.fRecord.fTime) return a.fRecord.fTime < b.fRecord.fTime;
			return a.fAuthor.Compare(kTrue, b.fAuthor) < 0;
		});
		list.erase(std::unique(list.begin(), list.end(), [](const KCMTrackResolveItem& a, const KCMTrackResolveItem& b)
		{
			return a.fRecord.fAt == b.fRecord.fAt && a.fRecord.fDelete == b.fRecord.fDelete
				&& a.fRecord.fTime == b.fRecord.fTime && a.fAuthor == b.fAuthor;
		}), list.end());
		InterfacePtr<ITextModel> model(targetDB, s->first, UseDefaultIID());
		InterfacePtr<IRedlineDataStrand> redline(model != nil ? QueryRedline(model) : nil);
		if (redline == nil)
		{
			outGone += static_cast<int32>(list.size());	// the story is gone, or tracks nothing any more
			continue;
		}
		for (size_t k = 0; k < list.size(); ++k)
		{
			RedlineIterator* it = FindRecord(redline, list[k]);
			if (it == nil)
			{
				++outGone;
				KCM_DIAG_LOG("track resolve: story %u %s at %d - not recorded any more", (unsigned)s->first.Get(),
							 list[k].fRecord.fDelete ? "deletion" : "insertion", (int)list[k].fRecord.fAt);
				continue;
			}
			const bool16 ok = accept ? it->ProcessAccept(nil, kFalse, kFalse) : it->ProcessReject(nil, kFalse, kFalse);
			delete it;
			KCM_DIAG_LOG("track resolve: story %u %s at %d %s -> %d", (unsigned)s->first.Get(),
						 list[k].fRecord.fDelete ? "deletion" : "insertion", (int)list[k].fRecord.fAt,
						 accept ? "accepted" : "rejected", (int)ok);
			if (!ok)
			{
				outMessage = KCMSentence("InDesign would not ");
				outMessage.Append(accept ? "accept" : "reject");
				outMessage.Append(" one of the records (story ");
				outMessage.AppendNumber(static_cast<int32>(s->first.Get()));
				outMessage.Append(")");
				return -1;
			}
			++done;
		}
	}
	return done;
}

// End, KCMTrackResolve.cpp.
