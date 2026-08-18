//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  What the book comparison dialog's chapter list contains: one node per chapter the last
//  comparison reported, and nothing else.
//
//  ONE METHOD IS THE WHOLE ADAPTER. ListTreeViewAdapter already answers every other question a
//  flat list raises - the root, each row's parent, how a row maps back to an index - in terms of
//  its own node class, ListIndexNodeID (ListTreeViewAdapter.cpp:70-129). Saying how many rows
//  there are is the only thing it cannot know: its own GetNumListItems is a placeholder that
//  answers 10 (:62-65), so a list that does not override it shows ten empty rows. Same reasoning,
//  same base, as the panel's Story Edits list (KESCMStoryTreeAdapter.cpp).
//  ⚠That range read :62-129 until 2026-08-18 (bug recheck B-U5), which swept in the one method
//  this file DOES override - the opposite of what "every other question" means. B-U4 had corrected
//  the identical sentence in the Story Edits adapter the same day; this copy was not looked for.
//
//  ★NO PLACEHOLDER ROW HERE. The Story Edits list invents a "No edits" row when a comparison found
//  nothing, because an empty list there would be indistinguishable from "nothing has been compared
//  yet". This dialog has a status line saying which of the two it is in words, so an empty list
//  needs no explaining and an invented row would only be one more thing to read.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "ListTreeViewAdapter.h"

// Project includes:
#include "KESCMBookDialog.h"		// KESCMBookDialogRows - the model
#include "KCMUIID.h"

/** Hierarchy adapter for the chapter list: a flat list, as long as the last result.
*/
class KESCMBookTreeAdapter : public ListTreeViewAdapter
{
public:
	KESCMBookTreeAdapter(IPMUnknown* boss) : ListTreeViewAdapter(boss) {}
	virtual ~KESCMBookTreeAdapter() {}

protected:
	virtual int32 GetNumListItems() const
	{
		return static_cast<int32>(KESCMBookDialogRows().size());
	}
};

CREATE_PMINTERFACE(KESCMBookTreeAdapter, kKESCMBookTreeAdapterImpl)

// End, KESCMBookTreeAdapter.cpp.
