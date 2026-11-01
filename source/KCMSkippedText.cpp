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
#include "ITextUtils.h"		// CollectStoryRanges - what stands inside a stretch of text

// General includes:
#include "PersistUtils.h"	// ::GetClass, ::GetUID, ::GetUIDRef, ::GetDataBase
#include "RangeData.h"		// Text::StoryRange
#include "TextID.h"			// kFootnoteReferenceBoss
#include "Utils.h"

// Project includes:
#include "KCMSkippedText.h"

namespace
{

struct Thread
{
	TextIndex	fStart;
	TextIndex	fEnd;
	ClassID		fClass;
};

struct TableBlock
{
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
		threads.push_back(t);
		pos += span;
	}

	// ---- 2. every table: where its cells are ------------------------------------
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
			t.fBlockStart = block.Start(nil);
			t.fBlockEnd = block.End();
			tables.push_back(t);
		}
	}

	// ---- 3. the seed: a thread that is not the body, not a table's cell and not a footnote ------------
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
	if (fRanges.empty())
		return kTrue;		// nothing skipped, so nothing stands in it: almost every story stops here

	// ---- 4. and what stands inside: a table whose anchor is skipped takes its cells with it, a footnote -----
	//   whose reference is skipped takes its words, to any depth. ITextUtils::CollectStoryRanges answers exactly that of a stretch of text
	//   (the nested text in between its pieces). Measured against the rounds this replaced, thread by thread: tracked and conditional, footnotes
	//   in cells, a table in a cell with a footnote of its own.
	Utils<ITextUtils> textUtils;
	if (textUtils != nil)
	{
		const std::vector<std::pair<TextIndex, TextIndex> > seeds(fRanges);
		for (size_t i = 0; i < seeds.size(); ++i)
		{
			Text::StoryRangeList ranges;
			textUtils->CollectStoryRanges(model, seeds[i].first, seeds[i].second - seeds[i].first, &ranges);
			for (int32 r = 0; r < static_cast<int32>(ranges.size()); ++r)
				fRanges.push_back(std::make_pair(ranges[r].Start(nil), ranges[r].End()));
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
