//========================================================================================
//
//  KCMStorySelection.cpp -- see the header.
//
//  ONE ROAD, AND THAT IS THE POINT. What counts as "this story" is the LAYOUT selection: the frames
//  and the paths the reader has selected, each asked what text it shows. A story named twice is
//  added once.
//
//  ★★**A CARET IS NOT READ** (the user's decision, 2026-09-16). This is a rule about meaning - the
//  reader points at frames - and it also removes a defect rather than guarding against one. Reading
//  the text selection meant asking ITextTarget, and ITextTarget::GetTextModel() answers with a model
//  even when no caret stands anywhere: the header promises nothing about the no-selection case, and
//  the official form (sdksamples/basicpersistinterface/BPISuiteTextCSB.cpp) guards it with
//  QueryTextFocus() for exactly that reason - a guard this file did not have.
//  ⚠**MEASURED 2026-09-16**: one frame selected, one story in it, and TWO stories were written. The
//  second was the story a caret had been in EARLIER, still named by GetTextModel(). Not asking the
//  question is the whole fix; there is no state left to go stale.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "IGraphicFrameData.h"
#include "ILayoutTarget.h"			// GetUIDList / CreateFlattenedUIDList - and kStripStandoffs
#include "IConcreteSelection.h"	// the CSB itself - the step between the suite and the target
#include "ILayoutHitTestSuite.h"	// the LAYOUT CSB's suite, used here only as a way in to that CSB
#include "IMultiColumnTextFrame.h"	// GetTextModelUID - the story a frame shows
#include "ISelectionManager.h"
#include "ISelectionUtils.h"		// QueryActiveSelection - the front document's selection
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

	// ---- frames and paths on the page ---------------------------------------------------------
	// ★★★**THREE STEPS, AND THE FIRST TWO ARE NOT OPTIONAL** - copied from the official sample that
	//   does this from inside an ActionComponent, which is exactly where we are:
	//   csxsdemo/VDActionComponent.cpp, GetSelectedPageItemsUIDs. The road is
	//       suite  ->  IConcreteSelection (the CSB itself)  ->  ILayoutTarget  ->  GetUIDList.
	//   ⚠**WHY NOT STRAIGHT OFF THE SELECTION MANAGER.** QueryActiveSelection returns the ABSTRACT
	//   selection boss. The guide is explicit about what lives where - "kIntegratorSuiteBoss
	//   Abstract (IIntegratorTarget) / kLayoutSuiteBoss Layout (ILayoutTarget)" - and the IID/boss
	//   dictionary agrees: IID_ILAYOUTTARGET is on seven bosses, not one of them abstract. So
	//   InterfacePtr<ILayoutTarget>(selection, UseDefaultIID()) returns nil FOR EVERY SELECTION
	//   THERE HAS EVER BEEN. That was this file's defect from the day it was written: it answered
	//   "nothing is selected" always, and Export Story Text wrote the whole document every time.
	//   ⚠**AND IT FAILED SILENTLY**: nil is also the honest answer for "nothing selected", so the
	//   broken road and the working one are spelt the same. What gave it away was a COUNT - four
	//   stories out of a four-story document with one frame selected.
	//   ⚠**QuerySuite(IID_ILAYOUTTARGET, ...) IS NOT THE WAY EITHER** (measured 2026-09-16, still
	//   four files): that call dispatches SUITES on the abstract boss, and a target is not a suite.
	//   The suite below is asked for only as a HANDLE ON THE CSB - which suite it is does not
	//   matter, and the sample picks this one.
	InterfacePtr<ILayoutHitTestSuite> layoutSuite(
		Utils<ISelectionUtils>()->QueryLayoutHitTestSuite(selection));
	InterfacePtr<IConcreteSelection> layoutCSB(layoutSuite, UseDefaultIID());
	InterfacePtr<ILayoutTarget> layoutTarget(layoutCSB, UseDefaultIID());
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
