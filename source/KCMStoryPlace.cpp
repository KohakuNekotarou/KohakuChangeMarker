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
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "Utils.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

// Project includes:
#include "KCMCore.h"				// KCMFramePageUID - the page a Story row names
#include "KCMDiag.h"				// KCM_DIAG_LOG / the place-pair-off switch (test builds only)
#include "KCMPageMap.h"			// KCMBuildPairing / KCMBuildMasterPairing - the corresponding pages
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
	(KCMStoryPlaceKey, KCMStoryPlace.h). */
struct SamePlace
{
	bool operator()(const Place& a, const Place& b) const { return (a.fOnPath == b.fOnPath && a.fBox == b.fBox) ? true : false; }
};

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

}	// anonymous namespace

/* KCMStoryPlaceKey
*/
bool16 KCMStoryPlaceKey(IDataBase* db, UID storyUID, UID& outPage, PMRect& outBox, bool16& outOnPath)
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
	outBox = box;
	outOnPath = onPath;
	return kTrue;
}

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
	// first (KCMBuildPairing), then the masters by name (KCMBuildMasterPairing) - the two tables KCMMapTargetToSource
	// consults one after the other, built here a single time instead of once per story. Page UIDs are unique within a
	// document, so the two cannot collide.
	std::map<UID, UID> toSource;
	{
		std::vector<UID> tPages;
		std::vector<UID> sPages;
		KCMBuildPairing(targetDB, sourceDB, tPages, sPages);
		for (size_t i = 0; i < tPages.size() && i < sPages.size(); ++i)
			toSource[tPages[i]] = sPages[i];
		std::vector<UID> tMasters;
		std::vector<UID> sMasters;
		KCMBuildMasterPairing(targetDB, sourceDB, tMasters, sMasters);
		for (size_t i = 0; i < tMasters.size() && i < sMasters.size(); ++i)
			toSource[tMasters[i]] = sMasters[i];
	}

	std::vector<UID> tIds, tPages, sIds, sPages;
	std::vector<Place> tPlaces, sPlaces;
	for (size_t k = 0; k < added.size(); ++k)
	{
		const UID story = diffs[added[k]].fStoryUID;
		UID page = kInvalidUID;
		Place place;
		if (!KCMStoryPlaceKey(targetDB, story, page, place.fBox, place.fOnPath))
			continue;
		KCM_DIAG_LOG("place key target story=%u page=%u box=%.6f,%.6f,%.6f,%.6f path=%d", static_cast<unsigned>(story.Get()),
					 static_cast<unsigned>(page.Get()), ::ToDouble(place.fBox.Left()), ::ToDouble(place.fBox.Top()),
					 ::ToDouble(place.fBox.Right()), ::ToDouble(place.fBox.Bottom()), place.fOnPath ? 1 : 0);
		const std::map<UID, UID>::const_iterator partner = toSource.find(page);
		if (partner == toSource.end())
			continue;							// its page is compared against nothing: no place to pair on
		tIds.push_back(story);
		tPages.push_back(partner->second);
		tPlaces.push_back(place);
	}
	for (size_t k = 0; k < removed.size(); ++k)
	{
		const UID story = diffs[removed[k]].fStoryUID;	// a Removed diff names the SOURCE's story
		UID page = kInvalidUID;
		Place place;
		if (!KCMStoryPlaceKey(sourceDB, story, page, place.fBox, place.fOnPath))
			continue;
		KCM_DIAG_LOG("place key source story=%u page=%u box=%.6f,%.6f,%.6f,%.6f path=%d", static_cast<unsigned>(story.Get()),
					 static_cast<unsigned>(page.Get()), ::ToDouble(place.fBox.Left()), ::ToDouble(place.fBox.Top()),
					 ::ToDouble(place.fBox.Right()), ::ToDouble(place.fBox.Bottom()), place.fOnPath ? 1 : 0);
		sIds.push_back(story);
		sPages.push_back(page);
		sPlaces.push_back(place);
	}

	std::vector<std::pair<UID, UID> > pairs;
	KCMPairStoriesByPlace<UID, UID, Place, SamePlace>(tIds, tPages, tPlaces, sIds, sPages, sPlaces, SamePlace(), pairs);
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
	std::vector<KCMStoryDiff> kept;
	kept.reserve(diffs.size());
	for (size_t i = 0; i < diffs.size(); ++i)
	{
		const bool16 goes = ((diffs[i].fKinds & kKCMStoryKindRemoved) != 0 && joined.count(diffs[i].fStoryUID) > 0)
							? kTrue : kFalse;
		if (!goes)
			kept.push_back(diffs[i]);
	}
	diffs.swap(kept);
}

// End, KCMStoryPlace.cpp.
