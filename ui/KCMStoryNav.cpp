//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryNav.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "ITreeViewController.h"	// the selection
#include "ITreeViewMgr.h"			// expanding and scrolling to a node

// General includes:
#include "Utils.h"

// Project includes:
#include "KCMUIID.h"				// kKCMStoryTreeWidgetID
#include "KCMStoryNodeID.h"		// the tree's node class: (row, change)
#include "KCMStoryJump.h"			// what a stop DOES - the row click's own behaviour
#include "KCMUIShared.h"			// KCMGetVisibleOwnPanel
#include "IKCMStoryEditsFacade.h"	// the list itself, read across the model/UI boundary
#include "KCMStoryNav.h"

namespace
{

/** Put the list's own selection on this stop and bring it into view.

	★WHY THE WALK TOUCHES THE LIST AT ALL (user's call, 2026-08-24). The readout between the
	buttons says "3/12", which is where the walk is - not WHICH edit that is. The list is the only
	place that can say that, and it is already on screen; leaving it standing on some earlier row
	while the windows are showing another one would make the panel disagree with itself.
	★And it leaves the reader somewhere to continue from: the arrow keys walk on from the selected
	row (KCMStoryTreeEH) once they click into the list.

	★EVERY REFUSAL IS SILENT AND NORMAL. The panel can be closed, the Story Edits section collapsed,
	or the tree not built yet - a comparison runs the same way in all of those, and the jump itself
	does not depend on any of them (KCMStoryTreeRebuild is written the same way and says so).

	⚠THE KEY FOCUS IS DELIBERATELY LEFT ALONE, where a row click takes it (KCMStoryRowEH's
	LButtonUp). The gesture here is a BUTTON, and a button that stole the keyboard on every press
	would break its own repetition: the next Prev / Next would go somewhere else. The reader who
	wants to walk with the arrows clicks the list, exactly as before.
*/
void SelectInTree(const KCMStoryNavStop& stop)
{
	IControlView* treeView = KCMFindPanelWidget(kKCMStoryTreeWidgetID);
	if (treeView == nil)
		return;

	InterfacePtr<ITreeViewMgr> treeMgr(treeView, UseDefaultIID());
	InterfacePtr<ITreeViewController> controller(treeView, UseDefaultIID());
	if (treeMgr == nil || controller == nil)
		return;

	// A change row can only be selected with its story open. ScrollToNode expands ancestors by
	// itself (ITreeViewMgr.h:114), but the expansion is asked for outright here - the same order
	// KESCL uses for the same list-walking job (KESCLReportPanelObserver.cpp), and it keeps what
	// is being relied on visible in this file rather than in a contract two headers away.
	if (stop.fChange >= 0)
		treeMgr->ExpandNode(KCMStoryNodeID::CreateStory(stop.fRow), kFalse /*expandAllDescendants*/);

	const NodeID node = (stop.fChange >= 0)
						? KCMStoryNodeID::CreateChange(stop.fRow, stop.fChange)
						: KCMStoryNodeID::CreateStory(stop.fRow);
	treeMgr->ScrollToNode(node, ITreeViewMgr::eScrollIntoView);

	// ★★notifyOfChange IS kFalse HERE, WHERE KESCL'S IS kTrue - and the difference is about who
	//   does the jumping, not about taste. In KESCL the selection observer is what goes to the
	//   match, so notifying IS the jump. In this panel nothing listens to the tree's selection: the
	//   jump is called outright by the gesture (the row's event handler for a click, this file for
	//   a button). Notifying would therefore do nothing today, and would jump a SECOND time on the
	//   day something did listen.
	// ★DeselectAll first, for the reason the SDK's own sample gives: "Make sure nothing else is
	//   selected or we won't be able to select this one" (BscSlDlgTreeViewDlgSwitcher.cpp:124-126).
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	controller->Select(node, kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMBuildStoryNavStops (KCMStoryNav.h)
//----------------------------------------------------------------------------------------
void KCMBuildStoryNavStops(std::vector<KCMStoryNavStop>& out)
{
	out.clear();

	// ★ASKED THROUGH AN InterfacePtr, not one Utils<> call per question: this loop asks two things
	//   of the facade for every row (Utils.h:74-80 - three uses upwards, take the interface once).
	// ⚠AND IT IS TESTED FOR nil, unlike the calls in KCMStoryJump.cpp. The difference is the
	//   caller: those run from a click, so the panel is up and kUtilsBoss is certainly there, while
	//   this is also reached from a model NOTIFICATION (KCMRefreshNavPosition, called when the
	//   list is rebuilt), which can arrive while the application is tearing down.
	InterfacePtr<IKCMStoryEditsFacade> edits(Utils<IKCMStoryEditsFacade>().QueryUtilInterface());
	if (edits == nil)
		return;

	const int32 rowCount = edits->GetRowCount();
	for (int32 r = 0; r < rowCount; ++r)
	{
		IKCMStoryEditsFacade::Row row;
		if (!edits->GetRow(r, row))
			continue;	// out of range: the list was replaced while this walk was building. The rows
						//  that still answer are still good ones, so this drops one rather than all.

		KCMStoryNavStop stop;
		stop.fRow = r;
		stop.fStoryUID = row.fStoryUID;

		// ***** THE RULE: THE LEAVES, AND ONLY THE LEAVES. *****
		// "where there are children, leave the parent out; where a parent stands alone, include it". A row with children is
		// a heading for them - stopping on it first would show the reader the top of the story and
		// then, one press later, the first edit inside it: the same place twice, and the count in
		// the readout inflated by the number of stories.
		const int32 changeCount = edits->GetChangeCount(r);
		if (changeCount <= 0)
		{
			// No children at all - and that is a row worth visiting rather than one to skip. It
			// means the story changed but the edits could not be located (an added story, a diff
			// that refused it) or the words came out the same and only the ruby or the formatting
			// moved. The story's beginning is then the most precise place there is.
			stop.fChange = -1;
			out.push_back(stop);
			continue;
		}

		for (int32 c = 0; c < changeCount; ++c)
		{
			stop.fChange = c;
			out.push_back(stop);
		}
	}
}

//----------------------------------------------------------------------------------------
// KCMGotoStoryNavStop (KCMStoryNav.h)
//----------------------------------------------------------------------------------------
void KCMGotoStoryNavStop(const KCMStoryNavStop& stop)
{
	if (stop.fRow < 0)
		return;

	// The list first, so the row is already highlighted while the windows are moving to it - and so
	// that a jump which reports a refusal to the status line does not have the highlight arriving
	// after the message that explains it.
	SelectInTree(stop);

	// ***** AND THEN EXACTLY WHAT CLICKING THAT ROW DOES. *****
	// ★These two calls are the whole of "the same behaviour as selecting a Story Edits row".
	//   Neither the jump, nor the flash mark, nor the wording that goes to the message area is
	//   decided here - all three are inside them, which is what keeps the button and the click from
	//   drifting apart as either is changed ([[one-question-one-place]]).
	// ⚠The return values are dropped ON PURPOSE. A stop whose story is unplaced, or whose page is
	//   hidden, reports that itself and is still a stop: the walk has to be able to step past it.
	// ★★AND THE WALK'S POSITION IS SET INSIDE THEM, not out here (2026-08-24) - by
	//   KCMNoteStoryStop, which they call as soon as the row is known to exist. That is what lets
	//   a CLICK on a row, and the arrow keys, move the readout as well: all three gestures end up in
	//   the same two functions, so "where am I standing" is answered in one place
	//   ([[one-question-one-place]]). ⇒ Nothing here writes it.
	if (stop.fChange >= 0)
		KCMStoryJumpToChange(stop.fRow, stop.fChange);
	else
		KCMStoryJumpToRow(stop.fRow);
}

// End, KCMStoryNav.cpp.
