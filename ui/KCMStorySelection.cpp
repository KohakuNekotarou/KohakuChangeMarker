//========================================================================================
//
//  KCMStorySelection.cpp -- see the header.
//
//  Two roads meet here, because InDesign has two kinds of selection and either one can be what the
//  reader means by "this story": the TEXT selection (a caret or a run of characters, which names
//  its own ITextModel) and the LAYOUT selection (frames on the page, each of which has to be asked
//  what text it shows). Both are read, the answers go into one list, and a story named twice is
//  added once.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "IGraphicFrameData.h"
#include "ILayoutTarget.h"			// GetUIDList / CreateFlattenedUIDList - and kStripStandoffs
#include "IMultiColumnTextFrame.h"	// GetTextModelUID - the story a frame shows
#include "ISelectionManager.h"
#include "ISelectionUtils.h"		// QueryActiveSelection - the front document's selection
#include "ITextTarget.h"			// GetTextModel - the story a caret stands in
#include "ITextUtils.h"				// QueryMCFOrTOPFromSpline
#include "UIDList.h"
#include "UIDRef.h"
#include "Utils.h"

#include "KCMStorySelection.h"

namespace
{

/** Add one story to the list, if it belongs to this document and is not there already. */
void AddStory(IDataBase* db, IDataBase* from, UID storyUID, UIDList& out)
{
	// ⚠**A STORY OF ANOTHER DOCUMENT IS NOT AN ERROR, IT IS A MISS.** Two documents open, a
	//  selection left behind in the other one: exporting that story into this document's folder
	//  would put a file there whose UID names nothing, and the import would pair it with whatever
	//  happened to share the number.
	if (db == nil || from != db || storyUID == kInvalidUID)
		return;

	if (!out.Contains(storyUID))
		out.Append(storyUID);
}

/** Every story shown by the page items in `items`. */
void AddStoriesOfItems(IDataBase* db, const UIDList& items, UIDList& out)
{
	IDataBase* const from = items.GetDataBase();

	for (int32 i = 0; i < items.Length(); ++i)
	{
		InterfacePtr<IGraphicFrameData> frame(from, items[i], UseDefaultIID());
		if (frame == nil)
			continue;			// a line, a group, something that is not a frame at all

		// ★**QueryMCFOrTOPFromSpline, NOT QueryTextModelFromSpline.** The header says this one also
		//   answers for TEXT ON A PATH, whose text hangs off a boss of its own
		//   ([[text-on-path-item-hierarchy]]) - and text on a path is text the reader wants to edit
		//   like any other.
		InterfacePtr<IMultiColumnTextFrame> mcf(Utils<ITextUtils>()->QueryMCFOrTOPFromSpline(frame));
		if (mcf == nil)
			continue;			// a frame with no text in it: a picture, an empty shape

		AddStory(db, from, mcf->GetTextModelUID(), out);
	}
}

}	// anonymous namespace

bool16 KCMCollectSelectedStories(IDataBase* db, UIDList& outStories)
{
	if (db == nil)
		return kFalse;

	InterfacePtr<ISelectionManager> selection(Utils<ISelectionUtils>()->QueryActiveSelection());
	if (selection == nil)
		return kFalse;

	bool16 anythingSelected = kFalse;

	// ---- a caret, or a run of characters ------------------------------------------------------
	InterfacePtr<ITextTarget> textTarget(selection, UseDefaultIID());
	if (textTarget != nil)
	{
		const UIDRef model = textTarget->GetTextModel();
		if (model.GetDataBase() != nil && model.GetUID() != kInvalidUID)
		{
			anythingSelected = kTrue;
			AddStory(db, model.GetDataBase(), model.GetUID(), outStories);
		}
	}

	// ---- frames on the page -------------------------------------------------------------------
	InterfacePtr<ILayoutTarget> layoutTarget(selection, UseDefaultIID());
	if (layoutTarget != nil)
	{
		const UIDList items = layoutTarget->GetUIDList(kStripStandoffs);
		if (!items.IsEmpty())
			anythingSelected = kTrue;
		AddStoriesOfItems(db, items, outStories);

		// ★**AND WHAT IS INSIDE THE CONTAINERS.** Selecting a group selects the group, whose own
		//   UID shows no text; the frames the reader is looking at are its children. This call is
		//   the SDK's own way to open that up - "(b) Expand all containers so their children are in
		//   the returned list", ILayoutTarget.h - and it also drops the duplicates, which is why
		//   asking for both lists costs nothing.
		// ⚠**NO SAMPLE IN THE SDK CALLS IT** (measured 2026-09-15: zero hits across sdksamples and
		//  source/open), so it is checked on a real group rather than trusted.
		const UIDList inside = layoutTarget->CreateFlattenedUIDList(kInvalidInterfaceID);
		AddStoriesOfItems(db, inside, outStories);
	}

	return anythingSelected;
}

// End, KCMStorySelection.cpp.
