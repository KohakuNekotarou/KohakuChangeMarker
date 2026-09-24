//========================================================================================
//
//  KCMSkippedText.cpp
//
//  See KCMSkippedText.h for what is skipped and why every reader has to ask the same object.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITableModel.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITextStoryThreadDict.h"
#include "ITextStoryThreadDictHier.h"
#include "ITextUtils.h"		// CollectOwnedItems - where each footnote's reference stands

// General includes:
#include "PersistUtils.h"	// ::GetClass, ::GetUID, ::GetUIDRef, ::GetDataBase
#include "RangeData.h"		// Text::StoryRange
#include "TextID.h"			// kFootnoteReferenceBoss
#include "Utils.h"

#include <map>

// Project includes:
#include "KCMSkippedText.h"

namespace
{

struct Thread
{
	TextIndex	fStart;
	TextIndex	fEnd;
	ClassID		fClass;
	UID			fUID;
};

struct TableBlock
{
	TextIndex	fAnchor;		// where the table stands
	TextIndex	fBlockStart;	// where its cells' threads are
	TextIndex	fBlockEnd;
};

}	// anonymous namespace

bool16 KCMSkippedText::Build(ITextModel* model)
{
	fRanges.clear();
	if (model == nil)
		return kFalse;
	const TextIndex total = model->TotalLength();

	// ---- 1. every thread of the story, the walk KCMTextRead::ReadStory makes ------------------------
	std::vector<Thread> threads;
	for (TextIndex pos = 0; pos < total; )
	{
		int32 span = 0;
		InterfacePtr<const ITextStoryThread> thread(model->QueryStoryThread(pos, &pos, &span));
		if (thread == nil || span <= 0)
			break;
		Thread t;
		t.fStart = pos;
		t.fEnd = pos + span;
		t.fClass = ::GetClass(thread);
		t.fUID = ::GetUID(thread);
		threads.push_back(t);
		pos += span;
	}

	// ---- 2. every table: where it stands, and where its cells are ------------------------------------
	//   The walk is the one every table reader here makes (SnpIterTableUseDictHier's): a dictionary is a
	//   table exactly when an ITableModel can be got from it.
	std::vector<TableBlock> tables;
	InterfacePtr<ITextStoryThreadDictHier> hier(model, UseDefaultIID());
	IDataBase* const db = (hier != nil) ? ::GetDataBase(hier) : nil;
	if (hier != nil && db != nil)
	{
		for (UID next = ::GetUIDRef(hier).GetUID(); next != kInvalidUID; next = hier->NextUID(next))
		{
			InterfacePtr<ITextStoryThreadDict> dict(db, next, UseDefaultIID());
			InterfacePtr<ITableModel> table(dict, UseDefaultIID());
			if (dict == nil || table == nil)
				continue;
			const Text::StoryRange block = dict->GetThreadBlockTextRange();
			TableBlock t;
			t.fAnchor = dict->GetAnchorTextRange().Start(nil);
			t.fBlockStart = block.Start(nil);
			t.fBlockEnd = block.End();
			tables.push_back(t);
		}
	}

	// ---- 3. where each footnote's reference stands (the thread IS the reference's boss) --------------
	std::map<UID, TextIndex> refAt;
	{
		Utils<ITextUtils> textUtils;
		OwnedItemDataList owned;
		if (textUtils != nil && total > 0)
			textUtils->CollectOwnedItems(model, 0, total - 1, &owned);
		for (int32 k = 0; k < static_cast<int32>(owned.size()); ++k)
		{
			if (owned[k].fClassID == kFootnoteReferenceBoss)
				refAt[owned[k].fUID] = owned[k].fAt;
		}
	}

	// ---- 4. the seed: a thread that is not the body, not a table's cell and not a footnote ------------
	//   The same test KCMTextRead applies to step over it, stated in terms of what it is NOT - the body
	//   is the thread that starts at zero, a cell lies in a table's block, a footnote is its own class.
	for (size_t i = 0; i < threads.size(); ++i)
	{
		const Thread& t = threads[i];
		if (t.fStart == 0 || t.fClass == kFootnoteReferenceBoss)
			continue;
		bool16 inTable = kFalse;
		for (size_t k = 0; k < tables.size() && !inTable; ++k)
			inTable = (t.fStart >= tables[k].fBlockStart && t.fStart < tables[k].fBlockEnd) ? kTrue : kFalse;
		if (!inTable)
			fRanges.push_back(std::make_pair(t.fStart, t.fEnd));
	}

	// ---- 5. and what stands inside, until nothing more is added --------------------------------------
	//   A table whose anchor is skipped takes its cells with it; a footnote whose reference is skipped
	//   takes its words. Either can stand in the other, to any depth, so the rounds go on until one
	//   adds nothing - each thing is added at most once, so they end.
	std::vector<bool16> tableDone(tables.size(), kFalse);
	std::vector<bool16> threadDone(threads.size(), kFalse);
	for (bool16 grew = kTrue; grew; )
	{
		grew = kFalse;
		for (size_t k = 0; k < tables.size(); ++k)
		{
			if (tableDone[k] || !Contains(tables[k].fAnchor))
				continue;
			fRanges.push_back(std::make_pair(tables[k].fBlockStart, tables[k].fBlockEnd));
			tableDone[k] = kTrue;
			grew = kTrue;
		}
		for (size_t i = 0; i < threads.size(); ++i)
		{
			if (threadDone[i] || threads[i].fClass != kFootnoteReferenceBoss)
				continue;
			std::map<UID, TextIndex>::const_iterator r = refAt.find(threads[i].fUID);
			if (r == refAt.end() || !Contains(r->second))
				continue;
			fRanges.push_back(std::make_pair(threads[i].fStart, threads[i].fEnd));
			threadDone[i] = kTrue;
			grew = kTrue;
		}
	}
	return kTrue;
}

bool16 KCMSkippedText::Contains(TextIndex at) const
{
	for (size_t i = 0; i < fRanges.size(); ++i)
	{
		if (at >= fRanges[i].first && at < fRanges[i].second)
			return kTrue;
	}
	return kFalse;
}
