//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  What the Story Edits list contains: under a hidden root, one node per row of KESCMStoryList,
//  and under each of those, one node per difference the text diff found inside that story.
//
//  ★★★THE SECOND LEVEL EXISTS IN BOTH MODES AND IS EMPTY IN ONE OF THEM (2026-08-20).
//  This was a flat ListTreeViewAdapter until the Story Changes mode arrived. It is now a real
//  hierarchy, and the pixel mode simply never grows a branch: nothing fills a row's fChanges
//  there, so GetChangeCount answers 0 and every story row is a leaf. The list looks and behaves
//  exactly as it always has.
//
//  ⚠THE ALTERNATIVE - two trees, switched by mode - WAS REJECTED (user's call). It would have
//  meant two row templates, two widget managers and two click handlers, i.e. the same judgements
//  made in two places, which is how they come to disagree ([[one-question-one-place]]).
//
//  *** THE NODE CLASS IS NOW OURS: KESCMStoryNodeID. *** It used to be ListIndexNodeID, the class
//  ListTreeViewAdapter creates and compares against internally - which is exactly why a flat list
//  could get away with overriding one method. A hierarchy has to answer "what is this node's
//  parent" and "what is its nth child", and a single index cannot say. KBS's tree is built the
//  same way, one level deeper (chapter, font, hit).
//
//  ⚠WHAT WAS LOST BY LEAVING ListTreeViewAdapter: it implemented GetRootNode / GetParentNode /
//  GetNthChild / GetChildIndex for us (ListTreeViewAdapter.cpp:70-129). All five are written out
//  below. There is no partial route - the base class's methods are all phrased in terms of its own
//  node class, so keeping any of them while changing the node class would leave the set
//  inconsistent.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITreeViewHierarchyAdapter.h"

// General includes:
#include "CPMUnknown.h"

// Project includes:
#include "KCMUIID.h"
#include "KESCMStoryNodeID.h"
#include "Utils.h"					// Utils<IKESCMStoryEditsFacade>()
#include "IKESCMCompareFacade.h"	// IsArmed - is a comparison running at all (Facade since 2026-08-13, Task 11)
#include "IKESCMStoryEditsFacade.h"	// GetRowCount / GetChangeCount (Facade since 2026-08-13, Task 14)

/** Hierarchy adapter for the Story Edits list: hidden root -> one node per changed story ->
	one node per difference inside it (none at all in the pixel mode).
*/
class KESCMStoryTreeAdapter : public CPMUnknown<ITreeViewHierarchyAdapter>
{
public:
	KESCMStoryTreeAdapter(IPMUnknown* boss) : CPMUnknown<ITreeViewHierarchyAdapter>(boss) {}
	virtual ~KESCMStoryTreeAdapter() {}

	virtual NodeID_rv GetRootNode() const
	{
		return KESCMStoryNodeID::CreateRoot();
	}

	virtual NodeID_rv GetParentNode(const NodeID& node) const
	{
		TreeNodePtr<KESCMStoryNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsRoot())
			return kInvalidNodeID;		// the root has no parent

		if (nodeID->IsChangeRow())
			return KESCMStoryNodeID::CreateStory(nodeID->GetRow());

		return KESCMStoryNodeID::CreateRoot();
	}

	virtual int32 GetNumChildren(const NodeID& node) const
	{
		TreeNodePtr<KESCMStoryNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsChangeRow())
			return 0;					// a change is a leaf

		if (nodeID->IsRoot())
		{
			const int32 rows = Utils<IKESCMStoryEditsFacade>()->GetRowCount();
			if (rows > 0)
				return rows;

			// ★One placeholder row while a comparison is running, so that "nothing changed" and
			//   "nothing has been compared yet" do not look identical - an empty list would say
			//   both. The row itself reads "No edits"; the widget manager writes it when the model
			//   has no row to answer with. Stopped, the list is genuinely empty and stays that way.
			return Utils<IKESCMCompareFacade>()->IsArmed() ? 1 : 0;
		}

		// ★A story row's children are its differences - and there are none in the pixel mode,
		//   which is what keeps that mode's list one level deep.
		//   ⚠The placeholder row asks this too (row 0 with no model behind it); the Facade
		//   bounds-checks and answers 0, so the placeholder is a leaf like any story with no
		//   located differences.
		return Utils<IKESCMStoryEditsFacade>()->GetChangeCount(nodeID->GetRow());
	}

	virtual NodeID_rv GetNthChild(const NodeID& node, const int32& nth) const
	{
		TreeNodePtr<KESCMStoryNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsChangeRow() || nth < 0)
			return kInvalidNodeID;

		if (nodeID->IsRoot())
		{
			if (nth >= this->GetNumChildren(node))
				return kInvalidNodeID;
			return KESCMStoryNodeID::CreateStory(nth);
		}

		if (nth >= Utils<IKESCMStoryEditsFacade>()->GetChangeCount(nodeID->GetRow()))
			return kInvalidNodeID;
		return KESCMStoryNodeID::CreateChange(nodeID->GetRow(), nth);
	}

	virtual int32 GetChildIndex(const NodeID& /*parent*/, const NodeID& child) const
	{
		TreeNodePtr<KESCMStoryNodeID> childID(child);
		if (childID == nil || childID->IsRoot())
			return -1;

		// ★The index a node carries IS its place among its siblings, in both cases - which is the
		//   whole reason the node is a pair of indices rather than anything richer. The parent is
		//   not consulted: a change row's place under its story is its change index, and a story
		//   row's place under the root is its row index, and neither can be anything else.
		return childID->IsChangeRow() ? childID->GetChange() : childID->GetRow();
	}

	virtual NodeID_rv GetGenericNodeID() const
	{
		return KESCMStoryNodeID::Create();
	}

	virtual bool16 ShouldAddNthChild(const NodeID& /*node*/, const int32& /*nth*/) const
	{
		return kTrue;
	}
};

CREATE_PMINTERFACE(KESCMStoryTreeAdapter, kKESCMStoryTreeAdapterImpl)

// End, KESCMStoryTreeAdapter.cpp.
