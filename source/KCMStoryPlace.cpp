//========================================================================================
//
//  KCMStoryPlace.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// SaveRestoreModifiedState - reading must not dirty either document
#include "IFrameList.h"			// the story's first text column
#include "IGeometry.h"			// the frame's path bounds, through a matrix
#include "IHierarchy.h"			// what QuerySplineFromTextFrame hands back
#include "ITextFrameColumn.h"
#include "ITextModel.h"
#include "ITextUtils.h"			// QuerySplineFromTextFrame - the frame the reader sees, the SDK's way
#include "ITOPFrameData.h"		// text on a path: the column names its path

// General includes:
#include "PMMatrix.h"
#include "PMRect.h"
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "Utils.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

// Project includes:
#include "KCMCore.h"				// KCMFramePageUID - the page a Story row names
#include "KCMDiag.h"				// KCM_DIAG_LOG / the place-pair-off switch (test builds only)
#include "KCMPageMap.h"			// KCMBuildFullPairingMap - the corresponding pages
#include "KCMStoryPlacePairRule.h"	// the rule
#include "KCMStoryPlace.h"

namespace
{

/** A story's place as the pairing compares it: its first frame's box, and whether its words run on the frame's path. */
struct Place
{
	PMRect	fBox;
	bool16	fOnPath;
	Place() : fOnPath(kFalse) {}
};

/** "Exactly the same place and size", standing there the same way: PMRect's own == (each side within kPMRealEpsilon -
	PMRect.h, PMReal.h) and the same fOnPath - a frame's own words and the words on its path share the frame's box
	(KCMStoryPlaceKey). */
struct SamePlace
{
	bool operator()(const Place& a, const Place& b) const { return (a.fOnPath == b.fOnPath && a.fBox == b.fBox) ? true : false; }
};

typedef KCMStoryAtPlace<UID, UID, Place> StoryAtPlace;

/** The frame the reader sees for a text column: the SDK's two steps (SDKLayoutHelper::GetGraphicFrameRef - the same
	pair KCMRingAdornment's story labels and KFC's FrameOwner ask): a column of text on a path names the path itself
	(outOnPath kTrue), any other column is handed to QuerySplineFromTextFrame. kInvalidUID when neither answers. */
UID HolderOf(ITextFrameColumn* column, bool16& outOnPath)
{
	outOnPath = kFalse;
	if (column == nil)
		return kInvalidUID;
	InterfacePtr<ITOPFrameData> onPath(column, UseDefaultIID());
	if (onPath != nil)
	{
		outOnPath = kTrue;
		return onPath->GetMainSplineItemUID();
	}
	Utils<ITextUtils> textUtils;
	if (!textUtils)
		return kInvalidUID;
	InterfacePtr<IHierarchy> spline(textUtils->QuerySplineFromTextFrame(column));
	return (spline != nil) ? ::GetUID(spline) : kInvalidUID;
}

/** Where a story's first frame stands: the page it is on, its box on that page, and whether the story runs ON the
	frame's path or inside it.

	@param db the document that holds the story.
	@param storyUID the story.
	@param outPage [out] the page of the story's first text column (KCMFramePageUID - the page a Story row names).
	@param outPlace [out] fBox: the box of the item that holds that column - the frame the reader sees, found by the SDK's
		two steps (ITOPFrameData::GetMainSplineItemUID for text on a path, else ITextUtils::QuerySplineFromTextFrame) - as its
		PATH bounds (no stroke - the script's geometricBounds) in the page's own coordinates, measured from the page's
		top-left corner. Through one matrix (the item's inner to the pasteboard, then the pasteboard to the page's inner),
		so a rotated frame is boxed tight, as the script boxes it.
		fOnPath: kTrue for words on a path (the column is the path's - ITOPFrameData), kFalse for words inside a
		frame. ★ONE FRAME CAN CARRY ONE STORY OF EACH (2026-10-26, the final review - tests-place.ps1 P20): both stand at
		the frame's box, so with the box alone they were two equal places on the page and neither paired. The pairing tells
		them apart by this: inside pairs with inside, on the path with on the path.
	@return kFalse when the story is not there, sits in no frame, its first frame is on no page (the pasteboard), or the
		geometry cannot be read. The outs are left alone then. */
bool16 KCMStoryPlaceKey(IDataBase* db, UID storyUID, UID& outPage, Place& outPlace)
{
	if (db == nil || storyUID == kInvalidUID)
		return kFalse;
	InterfacePtr<ITextModel> model(db, storyUID, UseDefaultIID());
	if (model == nil)
		return kFalse;
	InterfacePtr<IFrameList> frames(model->QueryFrameList());
	if (frames == nil || frames->GetFrameCount() == 0)
		return kFalse;							// a story in no frame: nowhere to stand
	InterfacePtr<ITextFrameColumn> column(frames->QueryNthFrame(0));
	if (column == nil)
		return kFalse;

	// The page as the row names it (the column's), so that the pairing and the list speak of one page.
	const UID page = KCMFramePageUID(db, ::GetUID(column));
	if (page == kInvalidUID)
		return kFalse;							// the pasteboard

	bool16 onPath = kFalse;
	const UID holder = HolderOf(column, onPath);
	if (holder == kInvalidUID)
		return kFalse;
	InterfacePtr<IGeometry> holderGeo(db, holder, UseDefaultIID());
	InterfacePtr<IGeometry> pageGeo(db, page, UseDefaultIID());
	if (holderGeo == nil || pageGeo == nil)
		return kFalse;

	// ★THE ITEM'S INNER COORDINATES TO THE PAGE'S, IN ONE MATRIX - the SDK's own recipe for "an item's box in page
	//   coordinates" (chmlfilter/CHMLFiltHelper.cpp: InnerToPasteboardMatrix of the item, then the page's, inverted -
	//   PostConcat is "this first, then that", as spellpanel's PrivateSpellingUtils.cpp composes it), handed to
	//   GetPathBoundingBox so that the path itself is boxed after the turn: a rotated frame comes out tight, the way the
	//   script's geometricBounds boxes it, not as the box of a turned box (KCMPageExport.cpp turns a rectangle twice,
	//   which is the same for the unrotated frames it writes).
	PMMatrix innerToPage = ::InnerToPasteboardMatrix(holderGeo);
	innerToPage.PostConcat(::InnerToPasteboardMatrix(pageGeo).Inverse());
	PMRect box = holderGeo->GetPathBoundingBox(innerToPage);

	// Measured from the page's own top-left corner (design 2-1 #5 - not from the ruler's zero point).
	const PMRect pageBox = pageGeo->GetPathBoundingBox();
	box.MoveRel(-pageBox.Left(), -pageBox.Top());

	outPage = page;
	outPlace.fBox = box;
	outPlace.fOnPath = onPath;
	return kTrue;
}

/** KCMStoryPlaceKey with the trace line place-pair reads (`side` = target / source). */
bool16 PlaceOf(IDataBase* db, UID story, const char* side, UID& outPage, Place& outPlace)
{
	if (!KCMStoryPlaceKey(db, story, outPage, outPlace))
		return kFalse;
	KCM_DIAG_LOG("place key %s story=%u page=%u box=%.6f,%.6f,%.6f,%.6f path=%d", side, static_cast<unsigned>(story.Get()),
				 static_cast<unsigned>(outPage.Get()), ::ToDouble(outPlace.fBox.Left()), ::ToDouble(outPlace.fBox.Top()),
				 ::ToDouble(outPlace.fBox.Right()), ::ToDouble(outPlace.fBox.Bottom()), outPlace.fOnPath ? 1 : 0);
	return kTrue;
}

}	// anonymous namespace

/* KCMStoryPairByPlace
*/
void KCMStoryPairByPlace(IDataBase* targetDB, IDataBase* sourceDB, std::vector<KCMStoryDiff>& diffs)
{
	if (targetDB == nil || sourceDB == nil || diffs.empty())
		return;
	// (Fault switch place-pair-off, a test build's only - KCMDiag.h: the rows are built as they were before the pairing.)
	if (KCM_DIAG_FAULT("place-pair-off"))
		return;

	std::vector<size_t> added;
	std::vector<size_t> removed;
	for (size_t i = 0; i < diffs.size(); ++i)
	{
		if ((diffs[i].fKinds & kKCMStoryKindAdded) != 0)
			added.push_back(i);
		else if ((diffs[i].fKinds & kKCMStoryKindRemoved) != 0)
			removed.push_back(i);
	}
	if (added.empty() || removed.empty())
		return;									// nothing on one side to pair with - and nothing read

	// Reading geometry composes nothing; guarded anyway, the way every reader of both documents here is.
	IDataBase::SaveRestoreModifiedState targetGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceGuard(sourceDB);

	// The comparison's page pairing, ONCE: a Target page -> the Source page it is compared against. Ordinary pages
	// first, then the masters by name (KCMBuildFullPairing) - the two tables KCMMapTargetToSource consults one after
	// the other, built here a single time instead of once per story. Page UIDs are unique within a document, so the
	// two cannot collide.
	std::map<UID, UID> toSource;
	KCMBuildFullPairingMap(targetDB, sourceDB, toSource);

	std::vector<StoryAtPlace> targets, sources;
	for (size_t k = 0; k < added.size(); ++k)
	{
		const UID story = diffs[added[k]].fStoryUID;
		UID page = kInvalidUID;
		Place place;
		if (!PlaceOf(targetDB, story, "target", page, place))
			continue;
		const std::map<UID, UID>::const_iterator partner = toSource.find(page);
		if (partner == toSource.end())
			continue;							// its page is compared against nothing: no place to pair on
		const StoryAtPlace here = { story, partner->second, place };
		targets.push_back(here);
	}
	for (size_t k = 0; k < removed.size(); ++k)
	{
		const UID story = diffs[removed[k]].fStoryUID;	// a Removed diff names the SOURCE's story
		UID page = kInvalidUID;
		Place place;
		if (!PlaceOf(sourceDB, story, "source", page, place))
			continue;
		const StoryAtPlace here = { story, page, place };
		sources.push_back(here);
	}

	std::vector<std::pair<UID, UID> > pairs;
	KCMPairStoriesByPlace(targets, sources, SamePlace(), pairs);
	if (pairs.empty())
		return;

	// The merge: each pair's Added diff becomes the pair's row, its Removed diff goes. ⚠Each uid is looked for among ITS
	// side's diffs only - a Target uid and a Source uid can be the same number for two unrelated stories.
	std::set<UID> joined;
	for (size_t p = 0; p < pairs.size(); ++p)
	{
		KCM_DIAG_LOG("place pair target=%u source=%u", static_cast<unsigned>(pairs[p].first.Get()),
					 static_cast<unsigned>(pairs[p].second.Get()));
		for (size_t k = 0; k < added.size(); ++k)
		{
			KCMStoryDiff& d = diffs[added[k]];
			if (d.fStoryUID != pairs[p].first)
				continue;
			d.fSourceStoryUID = pairs[p].second;
			d.fKinds = kKCMStoryKindByPosition;	// no counter bit: the two histories' counters say nothing of each other
			break;
		}
		joined.insert(pairs[p].second);
	}
	diffs.erase(std::remove_if(diffs.begin(), diffs.end(), [&joined](const KCMStoryDiff& d)
		{ return (d.fKinds & kKCMStoryKindRemoved) != 0 && joined.count(d.fStoryUID) > 0; }), diffs.end());
}

// End, KCMStoryPlace.cpp.
