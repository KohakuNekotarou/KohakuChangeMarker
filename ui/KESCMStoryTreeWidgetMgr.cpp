//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  How one row of the Story Edits list is built and filled. Three cells, left to right: the story's
//  UID, its opening words, and what kind of change moved (2026-08-10 - it was two cells before, and
//  the kind column spelled every kind out; now it names the first and says "+" for the rest).
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
#include "KCMUIID.h"
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "Utils.h"					// Utils<IKESCMStoryEditsFacade>()
#include "IKESCMStoryEditsFacade.h"	// the rows themselves (Facade since 2026-08-13, Task 14)
#include "KESCMStoryStamp.h"		// KESCMStoryChangeKind - the bits KindLabel names. A type only,
									// which is why it may be included from either side of the split
#include "KESCMStoryTree.h"

namespace
{

/** Name the kind of change that moved - the first one, with a '+' when there were others.

	★ONE WORD, NOT A LIST (user's call, 2026-08-10: "when there are two or more, something like a
	+"). The column is 62px wide and does not ellipsize, so a spelled-out "Text Attr" was being
	CLIPPED rather than shortened - the reader saw a word cut off mid-stroke and no sign that
	anything was missing. "Text+" fits, and the '+' is the sign.

	The order the kinds are tested in is fixed, so the word before the '+' is always the same one
	for the same set of changes - "Text+" never comes back as "Attr+". Added stands alone rather
	than joining the others: there is no older story to have compared anything against, so no kind
	could have been named for it, and no '+' can follow it.
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

	const uint32 bits[3] = { kKESCMStoryKindText, kKESCMStoryKindAttr, kKESCMStoryKindOther };
	const char* const keys[3] = { kKESCMStoryKindTextKey, kKESCMStoryKindAttrKey, kKESCMStoryKindOtherKey };

	// Which one to name, and whether anything else moved. Counting first rather than appending as
	// we go, because the '+' depends on what comes AFTER the word that gets printed.
	int32 firstKind = -1;
	int32 kindCount = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		if ((kinds & bits[i]) == 0)
			continue;
		if (firstKind < 0)
			firstKind = i;
		++kindCount;
	}

	if (firstKind >= 0)
	{
		PMString word(keys[firstKind]);
		word.Translate();
		out.Append(word);

		if (kindCount > 1)
			out.Append("+");
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
			RsrcSpec(LocaleSetting::GetLocale(), kKCMUIPluginID, kViewRsrcType, kKESCMStoryRowRsrcID),
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

	// ★★★THE FRAMEWORK'S INDENT IS TURNED OFF HERE, AND IT HAS TO BE.
	//
	//   CTreeViewWidgetMgr::ApplyIndentToWidget rewrites the left edge of every cell that is bound
	//   on BOTH sides (CTreeViewWidgetMgr.cpp:244-250):
	//       if (previousOffset == kMaxInt32)                  // <- only the FIRST such cell sets it
	//           previousOffset = frame.Left() - fBaseIndentOffset;
	//       frame.Left( frame.Left() + indent - previousOffset );
	//   A flat list has indent == 0, so for the first both-bound cell that reduces to
	//   frame.Left(fBaseIndentOffset). ⚠It does NOT drag every such cell there: previousOffset is
	//   computed once and reused, so a second both-bound cell keeps its distance from the first and
	//   only shifts by the same amount (2026-08-18, bug recheck B-U4 - this said "every such cell is
	//   dragged to fBaseIndentOffset", which happens to describe THIS row because only one cell is
	//   bound both ways: KCMUI.fr binds the UID cell kBindLeft, the kind cell kBindRight, and the
	//   text cell kBindLeft|kBindRight).
	//   ★And ours is ZERO: that member is only ever assigned from a REGISTERED STYLE WIDGET (:315),
	//   and this manager builds its rows in CreateWidgetForNode instead of registering styles, so it
	//   keeps the 0 its PMReal default gives it (:71-74 does not name it in the initialiser list).
	//
	//   ⚠WHAT THAT COST, measured 2026-08-10: the text cell's left edge in the .fr was being thrown
	//   away on every single apply. It went unnoticed while that cell was the leftmost thing on the
	//   row - it simply sat further left than written, which read as "the list has no padding".
	//   It stopped being invisible the moment a UID column was put in front of it: a cell bound on
	//   ONE side is NOT moved (:229-230), so the UID stayed where the .fr put it and the text
	//   landed on top of it.
	//
	//   ★The override is empty rather than clever, because a list with no hierarchy has nothing to
	//   indent. The base class asks for exactly this when its scheme does not fit
	//   (CTreeViewWidgetMgr.cpp:226: "You may want to override this method handle indent in your
	//   own way if the default way of handling indent doesn't work for you").
	//
	//   ⚠If this list is ever given levels, this override has to go and the cells have to be
	//   re-thought - not the other way round.
	virtual void ApplyIndentToWidget(const NodeID& /*node*/, IPanelControlData* /*widgetList*/, int32 /*message*/) const
	{
	}

	virtual bool16 ApplyDataToWidget(const NodeID& node, IPanelControlData* widgetList, int32 /*message*/) const
	{
		if (widgetList == nil)
			return kTrue;

		// ★A row COPIED out of the model, not a pointer into its list (Task 14). The three cells
		//   below are written from it and nothing here outlives the call, so the copy costs one
		//   PMString per row drawn.
		TreeNodePtr<ListIndexNodeID> nodeID(node);
		IKESCMStoryEditsFacade::Row row;
		const bool16 haveRow = (nodeID != nil)
			&& Utils<IKESCMStoryEditsFacade>()->GetRow(nodeID->GetIndex(), row);

		// ★All THREE cells are written on EVERY apply, including the empty case. Row widgets are
		//   recycled as the list scrolls, so a cell left alone keeps whatever the row it used to be
		//   had in it.
		//
		// ★An unreadable node writes blanks and still answers kTrue. Answering kFalse would be
		//   telling the framework to throw this widget away, build another and ask again
		//   (CTreeViewWidgetMgr.h:160-163) - which cannot help, because a row the model no longer
		//   holds will be missing from the new widget too.
		PMString uid, text, kinds;
		uid.SetTranslatable(kFalse);
		text.SetTranslatable(kFalse);
		kinds.SetTranslatable(kFalse);
		if (haveRow)
		{
			// ★UID as a plain decimal number (user's request, 2026-08-10). The cast is to the type
			//   AppendNumber takes (PMString.h:568); UID::Get() answers uint32 (OMTypes.h:78), and a
			//   document's object numbers are counted in thousands, nowhere near where the two types
			//   part company.
			uid.AppendNumber(static_cast<int32>(row.fStoryUID.Get()));
			text = row.fText;
			kinds = KindLabel(row.fKinds);
		}
		else if (Utils<IKESCMStoryEditsFacade>()->GetRowCount() == 0)
		{
			// ★The placeholder the adapter asks for while a comparison is running and found nothing
			//   (see GetNumListItems). Left cell only: there is no kind to name.
			text = PMString(kKESCMStoryNoEditsKey);
			text.Translate();
			text.SetTranslatable(kFalse);
		}

		this->SetNodeName(widgetList, uid, kKESCMStoryRowUIDWidgetID);
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
