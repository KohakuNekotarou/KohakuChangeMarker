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
	bool16					fWhole;		// the box is its whole story (2026-10-26): it IS the story, nothing is spliced

	Matched() : fBox(0), fStart(0), fEnd(0), fWhole(kFalse) {}
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

/** The story's whole body as a box's place (2026-10-26 - a box that is its whole story, the header's B): from its start to
	its last paragraph's return - and no fingerprint: what InDesign changed since the export is what the comparison is to
	show. ★Its tables and footnotes go with it (2026-10-26 evening); an endnote's marker does not (asked of the model, as
	MatchAt asks). kFalse when the body cannot be placed or holds an endnote's marker. */
bool16 WholeStoryAt(ITextModel* model, const KCMPageStoryFacts& facts, size_t which, Matched& out)
{
	if (facts.fParas.empty())
		return kFalse;
	const int32 start = 0;
	const int32 end = KCMParaText::ParagraphReturn(facts.fParas, facts.fStarts, facts.fAttrs,
													static_cast<int32>(facts.fParas.size()) - 1);
	if (end < start || end > model->TotalLength())
		return kFalse;
	if (KCMPagePartHoldsEndnote(model, start, end - start))
		return kFalse;
	KCMPageSplice::Place from;
	KCMPageSplice::Place to;
	if (!KCMPageSplice::PlaceOfIndex(facts.fParas, facts.fAttrs, facts.fStarts, start, from)
		|| !KCMPageSplice::PlaceOfEnd(facts.fParas, facts.fAttrs, facts.fStarts, start, end, to))
		return kFalse;
	out.fBox = which;
	out.fFrom = from;
	out.fTo = to;
	out.fStart = start;
	out.fEnd = end;
	out.fWhole = kTrue;
	return kTrue;
}

}	// anonymous namespace

void KCMSpliceBoxesIntoSet(IDataBase* copyDB, IDataBase* documentDB, KCMStoryTextSet& inOutSet,
						   std::vector<KCMImportRefusal>& outRefusals, int32& outBoxesIn, int32& outStoriesIn)
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
			// ★ANOTHER DOCUMENT'S FILE (2026-10-26 - the header): its uids can be this document's, and an edited box is
			//   compared now (below), so the ids decide. A file of 2026-10-25 names no document and is let through as before.
			else if (!KCMDocumentIdsMatch(documentDB, boxes[mine[i]].fDocumentId, boxes[mine[i]].fOriginalDocumentId))
				Refuse(boxes[mine[i]], "this page file was exported from another document", outRefusals);
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
			InterfacePtr<IMultiColumnTextFrame> mc;
			if (box.fFrame != kInvalidUID && copyDB->IsValidUID(box.fFrame))
			{
				InterfacePtr<IGraphicFrameData> frame(copyDB, box.fFrame, UseDefaultIID());
				InterfacePtr<IMultiColumnTextFrame> mine((frame != nil) ? frame->QueryMCTextFrame() : nil);
				if (mine != nil && mine->GetTextModelUID() == storyUID)
					mc = mine;
			}
			bool16 found = kFalse;
			const char* why = "the text of this frame changed after the export - export the page again";
			if (box.fWhole)
			{
				// ★★A BOX THAT IS ITS WHOLE STORY IS PUT BACK OVER IT, EDITED OR NOT (2026-10-26, the author's B - the
				//   header): over the story's whole body, while its frame is still the story's only one. Threaded since the
				//   export, the box is no longer the whole story, and putting it back over the story would wipe the other
				//   frames' words. ★Its tables and footnotes go with it (2026-10-26 evening) - so the part's fingerprint,
				//   which a part of a story needs, is not asked: the whole story replaces the story.
				if (mc == nil)
					why = "the box's frame is not this story's any more - export the page again";
				else if (!mc->GetIsOnlyMCF())
					why = "the frame was threaded after the export - export its story with Export Story Text...";
				else if (!(found = WholeStoryAt(model, facts, live[i], m)))
					why = "the story holds an endnote now - export it with Export Story Text...";
			}
			else
			{
				// A box of 2026-10-25 - a part of its story: where its part still is what was exported, first at the
				// range it came from, then at the range its frame holds now.
				found = MatchAt(model, facts, box.fStart, box.fEnd, box, live[i], m);
				if (!found && mc != nil)
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
				Refuse(box, why, outRefusals);
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
		// ★A WHOLE BOX IS THE STORY (2026-10-26 evening): its body, its tables and its footnotes as Word holds them - the
		//   direction the document's (a box carries none). Alone by now: it overlaps any other box of its story. ⚠Every
		//   other box is a PART and they ALL go in (a threaded story's frames): the first version stopped that loop after
		//   one part (tests-tr.ps1 PG11 - a file of 2026-10-25 with two parts of one story put back six boxes of seven).
		const bool16 asWhole = (going.size() == 1 && going[0].fWhole) ? kTrue : kFalse;
		if (asWhole)
		{
			const KCMPageBoxEdit& box = boxes[going[0].fBox];
			whole = box.fContent;
			whole.fVertical = facts.fShape.fVertical;
			in = 1;
			fileName = box.fFileName;
		}
		for (size_t i = 0; !asWhole && i < going.size(); ++i)
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
