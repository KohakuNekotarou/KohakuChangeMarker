//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What the Story Edits list contains: under a hidden root, one node per row of KCMStoryList,
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
//  ★★★AND IT WAS REJECTED AGAIN ON 2026-09-09, when a THIRD mode arrived. The Resources mode
//  (which definitions differ) shows its rows in THIS list, using the row the PIXEL mode already
//  draws - a flat row of three plain text cells - because that is exactly the shape a definition
//  needs: kind, key, and Added/Removed/Changed.
//  ⚠A second tree was proposed and withdrawn after measuring: the argument for it was that the
//    Story rows are expensive to draw (KCMStoryCellView, ruby and kenten, two-line heights), and
//    that expense is ALL IN THE SECOND LEVEL, which the Resources mode never grows. The level this
//    list shares is the cheap one. ⇒ **Measure where the complexity is before splitting to escape
//    it.**
//  ★Which of the two the list is showing is asked in ONE function, KCMListShowsResources
//    (KCMStoryTree.h); this adapter, the widget manager and the section heading all call it.
//
//  *** THE NODE CLASS IS NOW OURS: KCMStoryNodeID. *** It used to be ListIndexNodeID, the class
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
#include "KCMStoryNodeID.h"
#include "Utils.h"					// Utils<IKCMStoryEditsFacade>()
#include "IKCMCompareFacade.h"	// IsArmed - is a comparison running at all (Facade since 2026-08-13, Task 11)
#include "IKCMStoryEditsFacade.h"	// GetRowCount / GetChangeCount (Facade since 2026-08-13, Task 14)
#include "IKCMResourcesFacade.h"	// GetChangeCount - the rows while the Resources mode is on
#include "KCMStoryTree.h"			// KCMListShowsResources - the one place the list asks the mode

/** Hierarchy adapter for the Story Edits list: hidden root -> one node per changed story ->
	one node per difference inside it (none at all in the pixel mode).
*/
class KCMStoryTreeAdapter : public CPMUnknown<ITreeViewHierarchyAdapter>
{
public:
	KCMStoryTreeAdapter(IPMUnknown* boss) : CPMUnknown<ITreeViewHierarchyAdapter>(boss) {}
	virtual ~KCMStoryTreeAdapter() {}

	virtual NodeID_rv GetRootNode() const
	{
		return KCMStoryNodeID::CreateRoot();
	}

	virtual NodeID_rv GetParentNode(const NodeID& node) const
	{
		TreeNodePtr<KCMStoryNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsRoot())
			return kInvalidNodeID;		// the root has no parent

		if (nodeID->IsChangeRow())
			return KCMStoryNodeID::CreateStory(nodeID->GetRow());

		return KCMStoryNodeID::CreateRoot();
	}

	virtual int32 GetNumChildren(const NodeID& node) const
	{
		TreeNodePtr<KCMStoryNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsChangeRow())
			return 0;					// a change is a leaf

		// ★★A DEFINITION'S CHILDREN ARE THE ATTRIBUTES THAT DIFFER (2026-09-09, the user's request:
		//   "give the result rows children ... PointSize in the Kind part and the number in the
		//   Definition part").
		//   ⚠**THIS REVERSES design section 6-1b**, which said definitions have no children and that
		//     the attributes belong in the upper pane alone. The pane is still there, and the two no
		//     longer say the same thing: the rows carry the NEWER value, the pane the older one.
		//
		//   ★★ONLY A `Changed` DEFINITION HAS CHILDREN (the user's call: "children only when it is
		//     Changed - the same feeling as Story"). An Added definition has no older side and a
		//     Removed one has no newer side, so every attribute of theirs would be listed as present
		//     on one side, which is what the parent row's own verdict already says. It is the same
		//     shape as the story list, where a story with no located differences is a leaf.
		if (KCMListShowsResources() && !nodeID->IsRoot())
		{
			PMString kind, key;
			KCMResourceChangeKind what = kKCMResourceChanged;
			Utils<IKCMResourcesFacade> resources;
			if (!resources || !resources->GetNthChange(nodeID->GetRow(), kind, key, what))
				return 0;					// out of range, or the "No differences" placeholder

			if (what != kKCMResourceChanged)
				return 0;

			return resources->GetNthAttrCount(nodeID->GetRow());
		}

		if (nodeID->IsRoot())
		{
			if (KCMListShowsResources())
			{
				// ⚠GUARDED THE SAME WAY THE BRANCH ABOVE IS. That one takes a Utils object and tests
				//  it; this one used to dereference straight through, and the two are eleven lines
				//  apart in the same function ([[utils-boss-facade-access]]).
				Utils<IKCMResourcesFacade> resources;
				const int32 defs = resources ? resources->GetChangeCount() : 0;
				if (defs > 0)
					return defs;

				// Same placeholder rule as the story list below: while a comparison is running,
				// "the definitions are identical" gets a row of its own so that it cannot be read
				// as "nothing has been compared yet".
				return Utils<IKCMCompareFacade>()->IsArmed() ? 1 : 0;
			}

			const int32 rows = Utils<IKCMStoryEditsFacade>()->GetRowCount();
			if (rows > 0)
				return rows;

			// ★One placeholder row while a comparison is running, so that "nothing changed" and
			//   "nothing has been compared yet" do not look identical - an empty list would say
			//   both. The row itself reads "No edits"; the widget manager writes it when the model
			//   has no row to answer with. Stopped, the list is genuinely empty and stays that way.
			return Utils<IKCMCompareFacade>()->IsArmed() ? 1 : 0;
		}

		// ★A story row's children are its differences - and there are none in the pixel mode,
		//   which is what keeps that mode's list one level deep.
		//   ⚠The placeholder row asks this too (row 0 with no model behind it); the Facade
		//   bounds-checks and answers 0, so the placeholder is a leaf like any story with no
		//   located differences.
		return Utils<IKCMStoryEditsFacade>()->GetChangeCount(nodeID->GetRow());
	}

	virtual NodeID_rv GetNthChild(const NodeID& node, const int32& nth) const
	{
		TreeNodePtr<KCMStoryNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsChangeRow() || nth < 0)
			return kInvalidNodeID;

		if (nodeID->IsRoot())
		{
			if (nth >= this->GetNumChildren(node))
				return kInvalidNodeID;
			return KCMStoryNodeID::CreateStory(nth);
		}

		// ★Bounds-checked against GetNumChildren rather than against the facade directly, so that
		//   "how many children has this node" is answered in ONE place. In the Resources mode that
		//   answer is 0 and this returns kInvalidNodeID, which is what makes a definition a leaf
		//   without a second test of the mode here.
		if (nth >= this->GetNumChildren(node))
			return kInvalidNodeID;
		return KCMStoryNodeID::CreateChange(nodeID->GetRow(), nth);
	}

	virtual int32 GetChildIndex(const NodeID& /*parent*/, const NodeID& child) const
	{
		TreeNodePtr<KCMStoryNodeID> childID(child);
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
		return KCMStoryNodeID::Create();
	}

	virtual bool16 ShouldAddNthChild(const NodeID& /*node*/, const int32& /*nth*/) const
	{
		return kTrue;
	}
};

CREATE_PMINTERFACE(KCMStoryTreeAdapter, kKCMStoryTreeAdapterImpl)

// End, KCMStoryTreeAdapter.cpp.
