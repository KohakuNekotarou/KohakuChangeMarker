//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  How one row of the book comparison dialog's chapter list is built and filled. Two cells, left
//  to right: the chapter's file name and the verdict on it.
//
//  Structure copied from KESCMStoryTreeWidgetMgr.cpp, which is the same thing for the panel. Two
//  differences, and both come from living in a dialog rather than in a palette:
//    1. the theme set on a new row is kIDDialogTheme, not kIDPanelTheme
//    2. the rebuild is handed the dialog's IPanelControlData, because a dialog cannot be found
//       from anywhere the way the one panel can
//
//  ★ApplyNodeIDToWidget is deliberately NOT overridden - the framework places the row content and
//  nothing here argues with it. (KBS has to override it, and to call the base FIRST, because it
//  rewrites its rows' frames itself; getting that order wrong cost it two separate bugs.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "ITreeViewMgr.h"			// ClearTree / ChangeRoot - the rebuild

// General includes:
#include "CTreeViewWidgetMgr.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "CreateObject.h"			// CreateObjectNoInit
#include "ListIndexNodeID.h"		// the node class ListTreeViewAdapter hands out
#include "LocaleSetting.h"
#include "PMString.h"
#include "RsrcSpec.h"

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KESCMStoryTreeWidgetMgr.cpp: the build
// files that would carry such a directory live outside this plug-in's repository, so a path added
// there would not survive a fresh checkout.
#include "../../open/includes/widgets/DVPublicUtilities.h"	// dv_utils::SetThemeForView

// Project includes:
#include "KESCMBookDialog.h"		// KESCMBookDialogRows - the model
#include "KESCMBookResult.h"		// KESCMChapterResult / KESCMChapterStateText
#include "KESCMBookTree.h"
#include "KESCMID.h"

/** Builds and fills the rows of the chapter list.
*/
class KESCMBookTreeWidgetMgr : public CTreeViewWidgetMgr
{
public:
	// kList, not kHierarchical: this list has no levels, and the flag is what tells the base class
	// to leave the indent machinery alone (CTreeViewWidgetMgr.h:59-63).
	KESCMBookTreeWidgetMgr(IPMUnknown* boss) : CTreeViewWidgetMgr(boss, kList) {}
	virtual ~KESCMBookTreeWidgetMgr() {}

	virtual IControlView* CreateWidgetForNode(const NodeID& /*node*/) const
	{
		// ★THREE STEPS, NOT ONE CreateObject, AND THE ORDER IS THE POINT:
		//   1. CreateObjectNoInit - make the row boss, but do not build the cells inside it yet.
		//   2. SetThemeForView - say which theme this widget draws in. The row is made here, long
		//      before the tree hands it to the dialog's window, so nothing else is ever going to
		//      say it. ★kIDDialogTheme, where the panel's list says kIDPanelTheme: this list lives
		//      in a dialog, and the two themes differ in exactly the colours a row is drawn with.
		//   3. DoPostCreate - NOW build the cells, with the theme already settled.
		// One CreateObject call would build the cells first and theme them never. This is how the
		// product's own panels do it (LayerPanelTreeViewWidgetMgr.cpp).
		//
		// A nil here would mean this plug-in's own resources failed to load, which nothing on this
		// side could improve on, so it is handed straight back: the tree asked for the widget, so
		// the tree decides what to do without one.
		IPMUnknown* newObject = ::CreateObjectNoInit(
			::GetDataBase(this),
			RsrcSpec(LocaleSetting::GetLocale(), kKESCMPluginID, kViewRsrcType, kKESCMBookRowRsrcID),
			IID_ICONTROLVIEW);
		InterfacePtr<IControlView> view(newObject, UseDefaultIID());
		if (view != nil)
		{
			dv_utils::SetThemeForView(view, dv_utils::kIDDialogTheme);
			view->DoPostCreate();
		}

		// The reference CreateObjectNoInit handed over is the one the caller gets; the InterfacePtr
		// above holds a second one and releases it here.
		return view;
	}

	virtual WidgetID GetWidgetTypeForNode(const NodeID& /*node*/) const
	{
		return kKESCMBookRowWidgetID;
	}

	// Answer both size questions rather than letting the base class build a widget and measure it.
	// Every row is one fixed height - the same constant the row resource and the tree's scroll
	// increments are written in - and every row is as wide as the list.
	virtual PMReal GetNodeWidgetHeight(const NodeID& /*node*/) const
	{
		return PMReal(kKESCMBookRowHeight);
	}

	virtual PMReal GetNodeWidgetWidth(const NodeID& /*node*/) const
	{
		return this->GetTreeViewWidth();
	}

	// ★★★THE FRAMEWORK'S INDENT IS TURNED OFF HERE, AND IT HAS TO BE.
	//
	//   CTreeViewWidgetMgr::ApplyIndentToWidget rewrites the left edge of every cell that is bound
	//   on BOTH sides (CTreeViewWidgetMgr.cpp:244-250):
	//       previousOffset = frame.Left() - fBaseIndentOffset;
	//       frame.Left( frame.Left() + indent - previousOffset );
	//   A flat list has indent == 0, so that reduces to frame.Left(fBaseIndentOffset) - every such
	//   cell is dragged to fBaseIndentOffset. ★And ours is ZERO: that member is only ever assigned
	//   from a REGISTERED STYLE WIDGET (:315), and this manager builds its rows in
	//   CreateWidgetForNode instead of registering styles, so it keeps the 0 it was constructed
	//   with.
	//
	//   ⚠WHAT THAT COSTS, measured on the panel's list 2026-08-10: the name cell's left edge in the
	//   .fr is thrown away on every apply, so the text starts at 0 instead of 8 - and if a column
	//   is ever put in front of it, the two land on top of each other (a cell bound on ONE side is
	//   not moved, :229-230). Here that would be the name cell, which is bound on both.
	//
	//   ★The override is empty rather than clever, because a list with no hierarchy has nothing to
	//   indent. The base class asks for exactly this when its scheme does not fit
	//   (CTreeViewWidgetMgr.cpp:226). Full account: memory treeview-indent-overwrites-cell-frames.
	virtual void ApplyIndentToWidget(const NodeID& /*node*/, IPanelControlData* /*widgetList*/, int32 /*message*/) const
	{
	}

	virtual bool16 ApplyDataToWidget(const NodeID& node, IPanelControlData* widgetList, int32 /*message*/) const
	{
		if (widgetList == nil)
			return kTrue;

		TreeNodePtr<ListIndexNodeID> nodeID(node);
		const std::vector<KESCMChapterResult>& rows = KESCMBookDialogRows();
		const int32 index = nodeID != nil ? nodeID->GetIndex() : -1;
		const bool16 known = index >= 0 && index < static_cast<int32>(rows.size());

		// ★BOTH cells are written on EVERY apply, including the unknown case. Row widgets are
		//   recycled as the list scrolls, so a cell left alone keeps whatever the row it used to be
		//   had in it.
		//
		// ★An unreadable node writes blanks and still answers kTrue. Answering kFalse would be
		//   telling the framework to throw this widget away, build another and ask again
		//   (CTreeViewWidgetMgr.h:160-163) - which cannot help, because a row the model no longer
		//   holds will be missing from the new widget too.
		PMString name, state;
		name.SetTranslatable(kFalse);		// a file name, and then a reason - neither is a key
		state.SetTranslatable(kFalse);		// the result's own vocabulary (KESCMBookResult.h)
		if (known)
		{
			const KESCMChapterResult& row = rows[index];
			name = row.fName;
			name.SetTranslatable(kFalse);

			// ★Why it failed belongs on the ROW, not only in the summary. Without it, "could not
			//   be opened" and "no differences" are two rows that look equally settled - and the
			//   first one means the chapter has not been checked at all.
			if (row.fState == kKESCMChapterFailed && !row.fWhy.IsEmpty())
			{
				name.Append(" - ");
				name.Append(row.fWhy);
			}

			state = PMString(KESCMChapterStateText(row.fState));
			state.SetTranslatable(kFalse);
		}

		this->SetNodeName(widgetList, name, kKESCMBookRowNameWidgetID);
		this->SetNodeName(widgetList, state, kKESCMBookRowStateWidgetID);
		return kTrue;
	}
};

CREATE_PMINTERFACE(KESCMBookTreeWidgetMgr, kKESCMBookTreeWidgetMgrImpl)

//----------------------------------------------------------------------------------------
// KESCMBookTreeRebuild - redraw the list from the rows the module holds
//----------------------------------------------------------------------------------------

void KESCMBookTreeRebuild(IPanelControlData* panelData)
{
	if (panelData == nil)
		return;

	InterfacePtr<ITreeViewMgr> treeMgr(panelData->FindWidget(kKESCMBookTreeWidgetID), UseDefaultIID());
	if (treeMgr == nil)
		return;

	// ClearTree(kTrue) drops what the tree was holding; ChangeRoot(kTrue) reloads it, the kTrue
	// promising that every row widget is the same height - which GetNodeWidgetHeight guarantees,
	// so the tree can size itself without measuring row by row.
	treeMgr->ClearTree(kTrue);
	treeMgr->ChangeRoot(kTrue);
}

// End, KESCMBookTreeWidgetMgr.cpp.
