//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  What the Story Edits list contains: one node per row of KESCMStoryList, and nothing else.
//
//  ONE METHOD IS THE WHOLE ADAPTER. ListTreeViewAdapter already answers every other question a
//  flat list raises - the root, each row's parent, how a row maps back to an index - in terms of
//  its own node class, ListIndexNodeID (ListTreeViewAdapter.cpp:62-129). Saying how many rows
//  there are is the only thing it cannot know.
//
//  *** THE NODE CLASS IS ListIndexNodeID, NOT ONE OF OURS. *** The base creates nodes with
//  ListIndexNodeID::Create(nth) and compares them against ListIndexNodeID::RootNodeID(), so
//  answering in any other class would mean overriding GetNthListItem, GetRootNode, GetParentNode
//  and GetChildIndex to keep the set consistent. The samples that use this base - linkwatcher and
//  basicselectabledialog - all stay with ListIndexNodeID, and the row index it carries is exactly
//  what KESCMStoryList::GetRow() wants. (KBS writes a node class of its own because its nodes are
//  a triple of chapter, font and hit; a flat list has no such need.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "ListTreeViewAdapter.h"

// Project includes:
#include "KESCMID.h"
#include "KESCMStoryList.h"

/** Hierarchy adapter for the Story Edits list: a flat list, as long as the model is.
*/
class KESCMStoryTreeAdapter : public ListTreeViewAdapter
{
public:
	KESCMStoryTreeAdapter(IPMUnknown* boss) : ListTreeViewAdapter(boss) {}
	virtual ~KESCMStoryTreeAdapter() {}

protected:
	virtual int32 GetNumListItems() const
	{
		return KESCMStoryList::GetRowCount();
	}
};

CREATE_PMINTERFACE(KESCMStoryTreeAdapter, kKESCMStoryTreeAdapterImpl)

// End, KESCMStoryTreeAdapter.cpp.
