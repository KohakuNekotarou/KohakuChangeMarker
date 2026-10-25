//========================================================================================
//
//  KCMPageImport.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <algorithm>
#include <string>
#include <vector>

#include "IDataBase.h"
#include "IGraphicFrameData.h"		// the box's frame, as the copy holds it now
#include "IMultiColumnTextFrame.h"
#include "IStoryList.h"
#include "ITextModel.h"
#include "UIDRef.h"

#include "KCMPageImport.h"
#include "KCMPageExport.h"			// KCMReadPageStoryFacts / KCMPagePartHoldsTableOrNote - the export's own reading
#include "KCMPageSplice.h"
#include "KCMStoryDocx.h"			// SettleForThisFormat - the shape a story file reads back in
#include "KCMTextWords.h"			// PMStringOfUtf8

namespace
{

/** One box whose part was found: where in the story, as places and as TextIndexes. */
struct Matched
{
	size_t					fBox;
	KCMPageSplice::Place	fFrom;
	KCMPageSplice::Place	fTo;
	int32					fStart;
	int32					fEnd;
};

void Refuse(const KCMPageBoxEdit& box, const PMString& why, std::vector<KCMImportRefusal>& out)
{
	KCMImportRefusal r;
	r.fStory = box.fStory;
	r.fKind = "Page";
	r.fKind.SetTranslatable(kFalse);
	r.fWhereAndWhy = box.fFileName;
	r.fWhereAndWhy.Append(" box ");
	r.fWhereAndWhy.AppendNumber(box.fBoxN);
	r.fWhereAndWhy.Append(": ");
	r.fWhereAndWhy.Append(why);
	r.fWhereAndWhy.SetTranslatable(kFalse);
	r.fFileName = box.fFileName;
	r.fFileName.SetTranslatable(kFalse);
	out.push_back(r);
}

void Refuse(const KCMPageBoxEdit& box, const char* why, std::vector<KCMImportRefusal>& out)
{
	PMString s(why);
	s.SetTranslatable(kFalse);
	Refuse(box, s, out);
}

/** Whether [start, end) of the story is the box's part as exported: in the story's body, holding no table and no note
	(asked of the model, the export's own test), and of the same fingerprint. */
bool16 MatchAt(ITextModel* model, const KCMPageStoryFacts& facts, int32 start, int32 end, const KCMPageBoxEdit& box,
			   size_t which, Matched& out)
{
	if (start < 0 || end < start || end > model->TotalLength())
		return kFalse;
	if (KCMPagePartHoldsTableOrNote(model, start, end - start))
		return kFalse;
	KCMPageSplice::Place from;
	KCMPageSplice::Place to;
	if (!KCMPageSplice::PlaceOfIndex(facts.fParas, facts.fAttrs, facts.fStarts, start, from)
		|| !KCMPageSplice::PlaceOfEnd(facts.fParas, facts.fAttrs, facts.fStarts, start, end, to))
		return kFalse;
	int32 bytes = 0;
	uint32 crc = 0;
	KCMPageSplice::Fingerprint(KCMPageSplice::Cut(facts.fShape, from, to), bytes, crc);
	if (bytes != box.fPrintBytes || crc != box.fPrintCrc)
		return kFalse;
	out.fBox = which;
	out.fFrom = from;
	out.fTo = to;
	out.fStart = start;
	out.fEnd = end;
	return kTrue;
}

}	// anonymous namespace

void KCMSpliceBoxesIntoSet(IDataBase* copyDB, KCMStoryTextSet& inOutSet, std::vector<KCMImportRefusal>& outRefusals,
						   int32& outBoxesIn, int32& outStoriesIn)
{
	outRefusals.clear();
	outBoxesIn = 0;
	outStoriesIn = 0;
	const std::vector<KCMPageBoxEdit> boxes = inOutSet.fBoxes;
	inOutSet.fBoxes.clear();
	if (boxes.empty())
		return;
	if (copyDB == nil)
	{
		for (size_t b = 0; b < boxes.size(); ++b)
			Refuse(boxes[b], "the import holds no copy of the document", outRefusals);
		return;
	}
	InterfacePtr<IStoryList> stories(copyDB, copyDB->GetRootUID(), UseDefaultIID());

	// The stories the boxes name, in the order the files first name them.
	std::vector<UID> order;
	for (size_t b = 0; b < boxes.size(); ++b)
	{
		if (std::find(order.begin(), order.end(), boxes[b].fStory) == order.end())
			order.push_back(boxes[b].fStory);
	}

	for (size_t s = 0; s < order.size(); ++s)
	{
		const UID storyUID = order[s];
		std::vector<size_t> mine;
		for (size_t b = 0; b < boxes.size(); ++b)
		{
			if (boxes[b].fStory == storyUID)
				mine.push_back(b);
		}

		// ★THE STORY'S OWN FILE WAS CHOSEN TOO: it is the one imported - two answers for one story are not this plug-in's
		//   to choose between, and the whole story is the larger one.
		if (std::find(inOutSet.fUids.begin(), inOutSet.fUids.end(), storyUID) != inOutSet.fUids.end())
		{
			for (size_t i = 0; i < mine.size(); ++i)
				Refuse(boxes[mine[i]], "the story's own file was chosen too - that file is the one imported", outRefusals);
			continue;
		}

		std::vector<size_t> live;
		for (size_t i = 0; i < mine.size(); ++i)
		{
			if (!boxes[mine[i]].fWhyNot.IsEmpty())
				Refuse(boxes[mine[i]], boxes[mine[i]].fWhyNot, outRefusals);		// the file itself says so
			else
				live.push_back(mine[i]);
		}
		if (live.empty())
			continue;

		if (stories == nil || stories->GetUserAccessibleStoryIndex(storyUID) < 0)
		{
			for (size_t i = 0; i < live.size(); ++i)
				Refuse(boxes[live[i]], "no story with this ID in the document", outRefusals);
			continue;
		}
		const UIDRef storyRef(copyDB, storyUID);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		KCMPageStoryFacts facts;
		if (model == nil || !KCMReadPageStoryFacts(storyRef, facts))
		{
			for (size_t i = 0; i < live.size(); ++i)
				Refuse(boxes[live[i]], "the story cannot be read the way the export reads it", outRefusals);
			continue;
		}

		// ---- each box's part: where it was exported, else where its frame stands now --------------------------
		std::vector<Matched> matched;
		for (size_t i = 0; i < live.size(); ++i)
		{
			const KCMPageBoxEdit& box = boxes[live[i]];
			Matched m;
			bool16 found = MatchAt(model, facts, box.fStart, box.fEnd, box, live[i], m);
			if (!found && box.fFrame != kInvalidUID && copyDB->IsValidUID(box.fFrame))
			{
				InterfacePtr<IGraphicFrameData> frame(copyDB, box.fFrame, UseDefaultIID());
				InterfacePtr<IMultiColumnTextFrame> mc((frame != nil) ? frame->QueryMCTextFrame() : nil);
				if (mc != nil && mc->GetTextModelUID() == storyUID)
				{
					const int32 start = static_cast<int32>(mc->TextStart());
					const int32 end = start + mc->TextSpan();
					if (start != box.fStart || end != box.fEnd)
						found = MatchAt(model, facts, start, end, box, live[i], m);
				}
			}
			if (found)
				matched.push_back(m);
			else
				Refuse(box, "the text of this frame changed after the export - export the page again", outRefusals);
		}

		// ---- two boxes over one text (one page chosen twice, two exports of it): neither ------------------------
		std::vector<bool16> twice(matched.size(), kFalse);
		for (size_t i = 0; i < matched.size(); ++i)
		{
			for (size_t j = 0; j < i; ++j)
			{
				const int32 lo = (matched[i].fStart > matched[j].fStart) ? matched[i].fStart : matched[j].fStart;
				const int32 hi = (matched[i].fEnd < matched[j].fEnd) ? matched[i].fEnd : matched[j].fEnd;
				if (lo < hi || (matched[i].fStart == matched[j].fStart && matched[i].fEnd == matched[j].fEnd))
					twice[i] = twice[j] = kTrue;
			}
		}
		std::vector<Matched> going;
		for (size_t i = 0; i < matched.size(); ++i)
		{
			if (twice[i])
				Refuse(boxes[matched[i].fBox], "two boxes hold the same text (the same page chosen twice?)", outRefusals);
			else
				going.push_back(matched[i]);
		}

		// ---- the story with every box in, the last range first: an earlier range's places stay where they were ----
		for (size_t a = 1; a < going.size(); ++a)
		{
			for (size_t b = a; b > 0 && going[b].fStart > going[b - 1].fStart; --b)
				std::swap(going[b], going[b - 1]);
		}
		KCMStoryShape::Story whole = facts.fShape;
		int32 in = 0;
		PMString fileName;
		for (size_t i = 0; i < going.size(); ++i)
		{
			const KCMPageBoxEdit& box = boxes[going[i].fBox];
			std::string why;
			if (!KCMPageSplice::Splice(whole, going[i].fFrom, going[i].fTo, box.fContent, why))
			{
				Refuse(box, KCMTextWords::PMStringOfUtf8(why), outRefusals);
				continue;
			}
			++in;
			fileName = box.fFileName;
		}
		if (in == 0)
			continue;

		// ★THE SHAPE A STORY FILE READS BACK IN (the split form around tables): what the pour compares with the document.
		KCMStoryDocx::SettleForThisFormat(whole);
		inOutSet.fUids.push_back(storyUID);
		inOutSet.fStories.push_back(whole);
		inOutSet.fFileNames.push_back(fileName);
		outBoxesIn += in;
		++outStoriesIn;
	}
}

// End, KCMPageImport.cpp.
