//========================================================================================
//
//  KCMRejectImport.cpp
//
//  See KCMRejectImport.h.
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
#include <vector>

// Project includes:
#include "KCMImportTracking.h"		// kKCMImportAuthorName - the name the import's changes are signed with
#include "KCMRedlineRange.h"		// KCMRedlineTouches
#include "KCMRejectImport.h"

namespace
{

/** The story's change tracking strand - the official way to reach it (codesnippets/SnpInspectTextModel.cpp:1027). */
IRedlineDataStrand* QueryRedline(const UIDRef& story)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil)
		return nil;
	return static_cast<IRedlineDataStrand*>(model->QueryStrand(kRedlineStrandBoss, IRedlineDataStrand::kDefaultIID));
}

/** Whether the iterator stands on one of the import's changes touching [from, to] - and, when it does, where that
	change stands (`outAt`, when given). */
bool16 IsImportChangeHere(RedlineIterator* it, TextIndex from, TextIndex to, KCMImportChangeAt* outAt = nil)
{
	TextIndex at = 0;
	int32 len = 0;
	const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
	if (record == nil)
		return kFalse;
	const bool16 isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete) ? kTrue : kFalse;
	delete record;			// ⚠the caller owns it (redlineiterator.h:137-138; SnpInspectTextModel.cpp:1048)

	PMString who;
	it->DescribeUser(who);
	PMString ours(kKCMImportAuthorName);
	ours.SetTranslatable(kFalse);
	if (who != ours)
		return kFalse;
	if (!KCMRedlineTouches(static_cast<int32>(at), len, isDelete, static_cast<int32>(from), static_cast<int32>(to)))
		return kFalse;
	if (outAt != nil)
	{
		outAt->fAt = static_cast<int32>(at);
		outAt->fLen = len;
		outAt->fDelete = isDelete;
	}
	return kTrue;
}

/** An iterator left on one of the import's changes touching the range that stands AT `position`, or nil.
	⚠The caller deletes what it gets. */
RedlineIterator* FindAt(IRedlineDataStrand* redline, TextIndex position, TextIndex from, TextIndex to)
{
	RedlineIterator* it = redline->NewRedlineIterator(position);
	if (it == nil)
		return nil;
	for (bool16 more = kTrue; more && it->GetCurrentPosition() <= position; more = it->Increment(kFalse))
	{
		if (it->GetCurrentPosition() == position && IsImportChangeHere(it, from, to))
			return it;
	}
	delete it;
	return nil;
}

}	// anonymous namespace

int32 KCMCountImportChanges(const UIDRef& story, TextIndex from, TextIndex to)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil || !redline->StoryHasChanges())
		return 0;
	RedlineIterator* it = redline->NewRedlineIterator(0);
	if (it == nil)
		return 0;
	int32 count = 0;
	for (bool16 more = kTrue; more; more = it->Increment(kFalse))
	{
		if (IsImportChangeHere(it, from, to))
			++count;
	}
	delete it;
	return count;
}

int32 KCMImportChangesAt(const UIDRef& story, TextIndex from, TextIndex to, std::vector<KCMImportChangeAt>& out)
{
	out.clear();
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil || !redline->StoryHasChanges())
		return 0;
	RedlineIterator* it = redline->NewRedlineIterator(0);
	if (it == nil)
		return 0;
	for (bool16 more = kTrue; more; more = it->Increment(kFalse))
	{
		KCMImportChangeAt here;
		if (IsImportChangeHere(it, from, to, &here))
			out.push_back(here);
	}
	delete it;
	return static_cast<int32>(out.size());
}

int32 KCMRejectImportChanges(const UIDRef& story, TextIndex from, TextIndex to)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return -1;

	// 1. WHERE they stand, from one walk.
	std::vector<TextIndex> positions;
	{
		RedlineIterator* it = redline->NewRedlineIterator(0);
		if (it == nil)
			return 0;
		for (bool16 more = kTrue; more; more = it->Increment(kFalse))
		{
			if (IsImportChangeHere(it, from, to))
			{
				const TextIndex at = it->GetCurrentPosition();
				if (positions.empty() || positions.back() != at)
					positions.push_back(at);
			}
		}
		delete it;
	}

	// 2. ★FROM THE BACK TO THE FRONT. A reject moves what stands after it - and with [from, to] held fixed, a
	//   walk from the start after each reject pulled a change that stood just PAST the row into the range and
	//   rejected it too (found by reading, before the first run). Rejecting the last position first leaves
	//   every earlier position where it was.
	//   ★SEVERAL AT ONE POSITION: a replace row's insertion and its deletion's mark stand together, so each
	//   position is asked again until nothing of the import's is left there (a nested insertion's reject can
	//   take the deletion with it - redlineiterator.h:49-50 - and then the second ask finds nothing).
	int32 done = 0;
	std::sort(positions.begin(), positions.end());
	for (size_t k = positions.size(); k-- > 0; )
	{
		for (int32 guard = 0; guard < 100; ++guard)
		{
			RedlineIterator* it = FindAt(redline, positions[k], from, to);
			if (it == nil)
				break;
			const bool16 ok = it->ProcessReject(nil, kFalse, kFalse);	// nil = this change only (redlineiterator.h:91)
			delete it;
			if (!ok)
				break;
			++done;
		}
	}
	return done;
}

// End, KCMRejectImport.cpp.
