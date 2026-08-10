//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Which click on a Story Edits row means what. What each one DOES is KESCMStoryJump.cpp.
//
//  The shape is the layer panel's row handler (LayerTreeRowPanelEH::LButtonUp), by way of KBS's
//  result rows: act on the button coming UP, only when the base handler did not claim the event,
//  only without Shift / Cmd, and only for the row that ended up selected. What each buys:
//    * UP, not DOWN  - a press that turns into a drag, or that the user rolls off before letting
//                      go, is not a request to go anywhere.
//    * !result       - the base returns kTrue for what it handled itself. Jumping on top of that
//                      would be a second action out of one click.
//    * no Shift/Cmd  - those are selection modifiers, not "take me there".
//    * IsSelected    - the row the click actually landed on (the press already set the selection).
//
//  ★THE KEYBOARD IS LEFT ALONE. KBS takes the key focus onto its tree so the arrows walk the
//  results; KESCM deliberately does not (user's call, 2026-08-10). This panel's job is to send the
//  user INTO the document, and a panel holding the keyboard is a panel where the tool shortcuts
//  have quietly stopped working.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IEvent.h"					// ShiftKeyDown / CmdKeyDown
#include "ITreeNodeIDData.h"		// this node's NodeID
#include "ITreeViewController.h"	// IsSelected - is this the row the click landed on?
#include "IWidgetParent.h"			// QueryParentFor - the row -> the tree that owns the selection

// General includes:
#include "ListIndexNodeID.h"		// the node class ListTreeViewAdapter hands out (KESCMStoryTreeAdapter.cpp)

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KESCMStoryTreeWidgetMgr.cpp's
// DVPublicUtilities.h: the build files that would carry such a directory live outside this
// plug-in's repository, so a path added there would not survive a fresh checkout.
// ⚠KBS includes this one by name alone; its project carries the directory. KESCM's does not, and
//   the difference is deliberate rather than an oversight (measured 2026-08-10: C1083).
#include "../../open/includes/widgets/TreeNodeEventHandler.h"	// stock base for a tree row

// Project includes:
#include "KESCMID.h"
#include "KESCMStoryJump.h"

class KESCMStoryRowEH : public TreeNodeEventHandler
{
public:
	KESCMStoryRowEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KESCMStoryRowEH() {}

	virtual bool16 LButtonUp(IEvent* e);

private:
	/** Which row this click is for, or -1 when it should be left alone.

		Answers -1 for a click the stock handler already used, for a modified click, and for a row
		that is not the selected one - so a caller gets one test instead of four.
	*/
	int32 RowForClick(IEvent* e, bool16 baseHandled) const;
};

CREATE_PMINTERFACE(KESCMStoryRowEH, kKESCMStoryRowEHImpl)

int32 KESCMStoryRowEH::RowForClick(IEvent* e, bool16 baseHandled) const
{
	if (baseHandled || e == nil || e->ShiftKeyDown() || e->CmdKeyDown())
		return -1;

	// The node's NodeID lives on this boss's ITreeNodeIDData (every tree node widget carries it).
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return -1;
	const NodeID& node = nodeData->Get();
	TreeNodePtr<ListIndexNodeID> nodeID(node);
	if (nodeID == nil)
		return -1;

	// The selection lives on the tree, not on the row, so ask upwards for it.
	InterfacePtr<const IWidgetParent> widgetParent(this, UseDefaultIID());
	if (widgetParent == nil)
		return -1;
	InterfacePtr<ITreeViewController> treeController(
		static_cast<ITreeViewController*>(widgetParent->QueryParentFor(ITreeViewController::kDefaultIID)));
	if (treeController == nil || !treeController->IsSelected(node))
		return -1;

	return nodeID->GetIndex();
}

bool16 KESCMStoryRowEH::LButtonUp(IEvent* e)
{
	// Let the stock handler finish the click first (selection, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);

	const int32 rowIndex = this->RowForClick(e, result);
	if (rowIndex >= 0)
		KESCMStoryJumpToRow(rowIndex);

	return result;
}

// End, KESCMStoryRowEH.cpp.
