//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  How one row of the Story Edits list is built and filled: the story's opening words on the left,
//  the kinds of change that moved on the right.
//
//  Five methods, and four of them are one line. The list is flat, so this file does none of the
//  indent arithmetic that KBS's widget manager exists for.
//
//  ★ApplyNodeIDToWidget is deliberately NOT overridden. KBS has to override it - and to call the
//  base FIRST - because it rewrites its rows' frames itself and has to land on top of the
//  framework's indent; getting that order wrong cost it two separate bugs. Here the framework
//  places the row content and nothing argues with it, so overriding ApplyDataToWidget alone means
//  the question of "before or after the base" never arises. paneltreeview and loggerpreferences
//  are this same shape.
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
// directory - the same reasoning, and the same route, as KESCMStorySection.cpp's splitter headers:
// the build files that would carry such a directory live outside this plug-in's repository, so a
// path added there would not survive a fresh checkout.
#include "../../open/includes/widgets/DVPublicUtilities.h"	// dv_utils::SetThemeForView

// Project includes:
#include "KESCMID.h"
#include "KESCMCore.h"				// KESCMGetVisibleOwnPanel
#include "KESCMStoryList.h"
#include "KESCMStoryTree.h"

namespace
{

/** Name the kinds of change that moved, in a fixed order.

	Fixed so that two rows whose changes are the same read the same way - "Text Attr" never comes
	back as "Attr Text". Added stands alone rather than joining the others: there is no older story
	to have compared anything against, so no kind could have been named for it.
*/
PMString KindLabel(uint32 kinds)
{
	if (kinds & kKESCMStoryKindAdded)
	{
		PMString added(kKESCMStoryKindAddedKey);
		added.Translate();
		added.SetTranslatable(kFalse);
		return added;
	}

	PMString out;
	out.SetTranslatable(kFalse);	// composed, so no longer a key - see the note in KESCMStoryList.cpp

	// A flag rather than a test for emptiness: it says what it means ("is this the first word?")
	// and keeps this out of the deprecated corner of PMString.
	bool16 first = kTrue;

	const uint32 bits[3] = { kKESCMStoryKindText, kKESCMStoryKindAttr, kKESCMStoryKindOther };
	const char* const keys[3] = { kKESCMStoryKindTextKey, kKESCMStoryKindAttrKey, kKESCMStoryKindOtherKey };

	for (int32 i = 0; i < 3; ++i)
	{
		if ((kinds & bits[i]) == 0)
			continue;

		PMString word(keys[i]);
		word.Translate();

		if (!first)
			out.Append(" ");
		out.Append(word);
		first = kFalse;
	}

	return out;
}

}	// anonymous namespace

/** Builds and fills the rows of the Story Edits list.
*/
class KESCMStoryTreeWidgetMgr : public CTreeViewWidgetMgr
{
public:
	// kList, not kHierarchical: this list has no levels, and the flag is what tells the base class
	// to leave the indent machinery alone (CTreeViewWidgetMgr.h:59-63).
	KESCMStoryTreeWidgetMgr(IPMUnknown* boss) : CTreeViewWidgetMgr(boss, kList) {}
	virtual ~KESCMStoryTreeWidgetMgr() {}

	virtual IControlView* CreateWidgetForNode(const NodeID& /*node*/) const
	{
		// ★THREE STEPS, NOT ONE CreateObject, AND THE ORDER IS THE POINT:
		//   1. CreateObjectNoInit - make the row boss, but do not build the cells inside it yet.
		//   2. SetThemeForView(kIDPanelTheme) - say that this widget is going to live in a palette.
		//      The row is made here, long before the tree hands it to the panel's window, so
		//      nothing else is ever going to say which theme it draws in.
		//   3. DoPostCreate - NOW build the cells, with the theme already settled.
		// One CreateObject call would build the cells first and theme them never. This is how the
		// product's own panels do it (LayerPanelTreeViewWidgetMgr.cpp), and KBS after them.
		//
		// A nil here would mean this plug-in's own resources failed to load, which nothing on this
		// side could improve on, so it is handed straight back: the tree asked for the widget, so
		// the tree decides what to do without one.
		IPMUnknown* newObject = ::CreateObjectNoInit(
			::GetDataBase(this),
			RsrcSpec(LocaleSetting::GetLocale(), kKESCMPluginID, kViewRsrcType, kKESCMStoryRowRsrcID),
			IID_ICONTROLVIEW);
		InterfacePtr<IControlView> view(newObject, UseDefaultIID());
		if (view != nil)
		{
			dv_utils::SetThemeForView(view, dv_utils::kIDPanelTheme);
			view->DoPostCreate();
		}

		// The reference CreateObjectNoInit handed over is the one the caller gets; the InterfacePtr
		// above holds a second one and releases it here.
		return view;
	}

	virtual WidgetID GetWidgetTypeForNode(const NodeID& /*node*/) const
	{
		return kKESCMStoryRowWidgetID;
	}

	// Answer both size questions rather than letting the base class build a widget and measure it.
	// Every row is one fixed height - the same constant the row resource and the tree's scroll
	// increments are written in - and every row is as wide as the list, which has no columns to add
	// up and no horizontal scroll bar.
	virtual PMReal GetNodeWidgetHeight(const NodeID& /*node*/) const
	{
		return PMReal(kKESCMStoryRowHeight);
	}

	virtual PMReal GetNodeWidgetWidth(const NodeID& /*node*/) const
	{
		return this->GetTreeViewWidth();
	}

	virtual bool16 ApplyDataToWidget(const NodeID& node, IPanelControlData* widgetList, int32 /*message*/) const
	{
		if (widgetList == nil)
			return kTrue;

		TreeNodePtr<ListIndexNodeID> nodeID(node);
		const KESCMStoryRow* row = nodeID != nil ? KESCMStoryList::GetRow(nodeID->GetIndex()) : nil;

		// ★Both cells are written on EVERY apply, including the empty case. Row widgets are recycled
		//   as the list scrolls, so a cell left alone keeps whatever the row it used to be had in it.
		//
		// ★An unreadable node writes blanks and still answers kTrue. Answering kFalse would be
		//   telling the framework to throw this widget away, build another and ask again
		//   (CTreeViewWidgetMgr.h:160-163) - which cannot help, because a row the model no longer
		//   holds will be missing from the new widget too.
		PMString text, kinds;
		text.SetTranslatable(kFalse);
		kinds.SetTranslatable(kFalse);
		if (row != nil)
		{
			text = row->fText;
			kinds = KindLabel(row->fKinds);
		}
		else if (KESCMStoryList::GetRowCount() == 0)
		{
			// ★The placeholder the adapter asks for while a comparison is running and found nothing
			//   (see GetNumListItems). Left cell only: there is no kind to name.
			text = PMString(kKESCMStoryNoEditsKey);
			text.Translate();
			text.SetTranslatable(kFalse);
		}

		this->SetNodeName(widgetList, text, kKESCMStoryRowTextWidgetID);
		this->SetNodeName(widgetList, kinds, kKESCMStoryRowKindWidgetID);
		return kTrue;
	}
};

CREATE_PMINTERFACE(KESCMStoryTreeWidgetMgr, kKESCMStoryTreeWidgetMgrImpl)

//----------------------------------------------------------------------------------------
// KESCMStoryTreeRebuild - redraw the list from the model
//----------------------------------------------------------------------------------------

void KESCMStoryTreeRebuild()
{
	// Reached through the panel, which is nil while it is closed - and a comparison run with the
	// panel closed is perfectly normal, so that is a quiet return rather than a failure.
	IControlView* panel = KESCMGetVisibleOwnPanel();
	if (panel == nil)
		return;

	InterfacePtr<const IPanelControlData> panelData(panel, UseDefaultIID());
	if (panelData == nil)
		return;

	InterfacePtr<ITreeViewMgr> treeMgr(panelData->FindWidget(kKESCMStoryTreeWidgetID), UseDefaultIID());
	if (treeMgr == nil)
		return;

	// ClearTree(kTrue) drops what the tree was holding; ChangeRoot(kTrue) reloads it, the kTrue
	// promising that every row widget is the same height - which GetNodeWidgetHeight guarantees,
	// so the tree can size itself without measuring row by row.
	treeMgr->ClearTree(kTrue);
	treeMgr->ChangeRoot(kTrue);
}

// End, KESCMStoryTreeWidgetMgr.cpp.
