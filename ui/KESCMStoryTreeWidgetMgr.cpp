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
#include "KESCMStoryNodeID.h"		// our node class: (row, change). Was ListIndexNodeID until 2026-08-20
#include "LocaleSetting.h"
#include "PMString.h"
#include "RsrcSpec.h"

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KESCMStorySection.cpp's splitter headers:
// the build files that would carry such a directory live outside this plug-in's repository, so a
// path added there would not survive a fresh checkout.
#include "../../open/includes/widgets/DVPublicUtilities.h"	// dv_utils::SetThemeForView

// Project includes:
#include "IKESCMStoryCellData.h"	// the change row's hand-drawn text cell takes its three pieces here
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
	for the same set of changes - "Text+" never comes back as "Attr+". Added and Removed stand alone
	rather than joining the others: there is no story on the other side to have compared anything
	against, so no kind could have been named for either, and no '+' can follow them.
	(Removed arrived 2026-08-21 - the story is in the older version and gone from the newer one.)

	★"None" STANDS ALONE TOO, AND IT OUTRANKS THE COUNTERS (2026-08-21, user's request after
	watching a refreshed row lose its children: "何も変更が亡くなった場合 Change のところに表示して
	欲しい、いまのままだと子供がなくなっただけでわかりずらい"). When the text has actually been put
	side by side and comes out the same, saying "Text+" would be repeating what the counters said
	BEFORE anybody looked at the words - which is exactly the reading the refresh just disproved.
	⚠It is not "unchanged": the counters moved, or there would be no row. It is "no difference in
	the words", and the sameKind flag is only ever set when the diff really ran (fTextCompared).

	@param sameKind kTrue when the text was compared and nothing differs - see above.
*/
PMString KindLabel(uint32 kinds, bool16 sameKind)
{
	if (sameKind)
	{
		PMString none(kKESCMStoryKindNoneKey);
		none.Translate();
		none.SetTranslatable(kFalse);
		return none;
	}

	if (kinds & kKESCMStoryKindAdded)
	{
		PMString added(kKESCMStoryKindAddedKey);
		added.Translate();
		added.SetTranslatable(kFalse);
		return added;
	}

	if (kinds & kKESCMStoryKindRemoved)
	{
		PMString removed(kKESCMStoryKindRemovedKey);
		removed.Translate();
		removed.SetTranslatable(kFalse);
		return removed;
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
	// ★kHierarchical since 2026-08-20: the list HAS levels now (a story row can hold the changes
	//   found inside it). It said kList until then, which told the base class to leave the indent
	//   machinery alone - see ApplyIndentToWidget below for why that machinery still must not be
	//   allowed to run on a story row, and what is done instead.
	KESCMStoryTreeWidgetMgr(IPMUnknown* boss) : CTreeViewWidgetMgr(boss, kHierarchical) {}
	virtual ~KESCMStoryTreeWidgetMgr() {}

	virtual IControlView* CreateWidgetForNode(const NodeID& node) const
	{
		// ★Two row templates since 2026-08-20: a story row and a change row. They are the same
		//   three-cell shape (see the .fr) so that the indent arithmetic below behaves the same on
		//   both - what differs is what the cells hold and where they start.
		TreeNodePtr<KESCMStoryNodeID> nodeID(node);
		const RsrcID rsrcID = (nodeID != nil && nodeID->IsChangeRow())
							  ? kKESCMStoryChangeRowRsrcID
							  : kKESCMStoryRowRsrcID;

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
			RsrcSpec(LocaleSetting::GetLocale(), kKCMUIPluginID, kViewRsrcType, rsrcID),
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

	virtual WidgetID GetWidgetTypeForNode(const NodeID& node) const
	{
		// ★THE TWO KINDS MUST ANSWER DIFFERENT IDs. This is what the framework uses to decide
		//   whether a recycled widget can be reused for a node - answer the same ID for both and a
		//   change row would be handed a story row's widget (and vice versa) as the list scrolls.
		TreeNodePtr<KESCMStoryNodeID> nodeID(node);
		return (nodeID != nil && nodeID->IsChangeRow())
			   ? kKESCMStoryChangeRowWidgetID
			   : kKESCMStoryRowWidgetID;
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
	//   ★The override is empty rather than clever. The base class asks for exactly this when its
	//   scheme does not fit (CTreeViewWidgetMgr.cpp:226: "You may want to override this method
	//   handle indent in your own way if the default way of handling indent doesn't work for you").
	//
	//   ★★★2026-08-20 - THE LIST WAS GIVEN LEVELS, AND THIS OVERRIDE STAYED EMPTY.
	//
	//   The note here used to say "if this list is ever given levels, this override has to go".
	//   It did not have to go, and writing an indent here would have been a mistake:
	//
	//     ⚠ROW WIDGETS ARE RECYCLED. This is called every time one is applied to a node, so an
	//       indent expressed as "move the cells right by N" ACCUMULATES - the same widget drifts
	//       further right each time it is scrolled back into view. Expressing it as an absolute
	//       position is what the base class does, and it needs fBaseIndentOffset to do it, which
	//       is 0 here for the reason given above.
	//
	//   ⇒ Each level's layout lives in ITS OWN RESOURCE instead - one for the story row, one for
	//     the change row - written once, statically. It cannot accumulate, it cannot depend on a
	//     member that was never assigned, and - the point that decided it - ★THE STORY ROW'S PATH
	//     THROUGH THIS FUNCTION IS UNCHANGED, so the pixel mode's list is not merely expected to
	//     look the same, it executes the same instructions.
	//
	//   ★★AND THE SECOND LEVEL NO LONGER INDENTS ITS TEXT AT ALL (2026-08-20, user's call): a
	//     change row's text starts at exactly the story row's text, 68. What says the row hangs
	//     under the one above is the expander in front of a story row and the blank where a change
	//     row's would be. So the two resources now differ in what they HOLD, not in where they put
	//     it. (Which is another reason not to hand this to the framework: the answer here was to
	//     indent by nothing, and an indent machine has no way to express that.)
	//
	//   ⚠A THIRD level would break this: two levels can be two resources, ten cannot. Anyone adding
	//     one has to come back here and do the arithmetic properly - starting by giving
	//     fBaseIndentOffset a value (RegisterStyleWidget), not by adding to frame.Left().
	virtual void ApplyIndentToWidget(const NodeID& node, IPanelControlData* widgetList, int32 message) const
	{
		// ★★THE ONE THING THAT *IS* DONE HERE: hide the expand arrow on a row that has nothing to
		//   expand (2026-08-20). The base class's own helper does it, finding the arrow by its stock
		//   WidgetID, so this is the framework's answer rather than ours - and it is the reason the
		//   pixel mode shows no arrows at all: nothing there has children.
		//   ⚠It hides the arrow; it does not reclaim the 16px the arrow occupies. That space is part
		//     of the row's layout in both modes (see the .fr).
		this->HideExpanderIfNotExpandable(node, widgetList, message);
	}

	virtual bool16 ApplyDataToWidget(const NodeID& node, IPanelControlData* widgetList, int32 /*message*/) const
	{
		if (widgetList == nil)
			return kTrue;

		TreeNodePtr<KESCMStoryNodeID> nodeID(node);

		// ★A CHANGE ROW IS WRITTEN BY ITS OWN BRANCH AND RETURNS. Its three cells hold different
		//   things from a story row's, and its widget came from a different resource, so nothing
		//   below applies to it.
		if (nodeID != nil && nodeID->IsChangeRow())
			return this->ApplyChangeRow(*nodeID, widgetList);

		// ★A row COPIED out of the model, not a pointer into its list (Task 14). The three cells
		//   below are written from it and nothing here outlives the call, so the copy costs one
		//   PMString per row drawn.
		IKESCMStoryEditsFacade::Row row;
		const bool16 haveRow = (nodeID != nil)
			&& Utils<IKESCMStoryEditsFacade>()->GetRow(nodeID->GetRow(), row);

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
			// ★"the text was diffed" AND "nothing came of it" - both halves are needed, and they
			//   live in different places on purpose: the fact that somebody looked is the row's
			//   (fTextCompared), and what they found is the children's. Asking the row how many
			//   children it has is the same question the tree itself asks to decide whether to
			//   draw a triangle, so the two can never disagree.
			const bool16 sameKind = row.fTextCompared
				&& (Utils<IKESCMStoryEditsFacade>()->GetChangeCount(nodeID->GetRow()) == 0);
			kinds = KindLabel(row.fKinds, sameKind);
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

private:
	/** Fills one CHANGE row: what sort of edit it was, and the words it concerns.

		★TWO CELLS, WHERE A STORY ROW HAS THREE (2026-08-20): the words on the left, and the sign
		that says what sort of edit it was on the right, in the column the story row names its
		kinds in. Both are written on every apply - a recycled widget keeps whatever the row it
		used to be had in it.

		★THE CELL IDs ARE THE STORY ROW'S. A widget ID has to be unique only among the descendants
		of one parent (guide vol2-12), and these two rows are never each other's descendants. The
		same reuse is already in this plug-in: the book dialog's row shares them too
		(kKESCMBookRowNameWidgetID == kKESCMStoryRowTextWidgetID, and so on).

		⚠THE TWO CELLS ARE NOT WRITTEN THE SAME WAY. The text cell is drawn by hand so that the
		changed characters can keep the theme's text colour while the words around them fade, and a
		hand-drawn cell holds no ITextControlData for SetNodeName to write - it is handed its three
		pieces through IKESCMStoryCellData instead. The sign's cell is a stock static text and
		still goes through SetNodeName.
	*/
	bool16 ApplyChangeRow(const KESCMStoryNodeID& nodeID, IPanelControlData* widgetList) const
	{
		IKESCMStoryEditsFacade::Change change;
		const bool16 have = Utils<IKESCMStoryEditsFacade>()->GetChange(
								nodeID.GetRow(), nodeID.GetChange(), change);

		PMString kind;
		PMString textPre, textMid, textPost;
		kind.SetTranslatable(kFalse);
		textPre.SetTranslatable(kFalse);
		textMid.SetTranslatable(kFalse);
		textPost.SetTranslatable(kFalse);

		if (have)
		{
			// ★A SIGN, NOT A WORD. The column is narrow and the reader is scanning down it; a sign
			//   tells the kinds apart at a glance and needs no translation. (The story row names
			//   its kinds in the same column in WORDS - Text, Attr, Other - because there the
			//   words are the answer, and there is one per story rather than one per edit.)
			//
			// ★★A REPLACEMENT IS "≠" (U+2260), THE THIRD SIGN - "it is not equal any more"
			//   (user's call, 2026-08-20). It took three tries to land there, and the middle one is
			//   worth keeping: "~" was tried first and rejected as saying nothing, then the column
			//   was left EMPTY for a replacement - and empty turned out to be worse than a poor
			//   sign, because "+" and "-" were then the only marks and a replacement read as an
			//   unmarked row rather than as a kind of its own. ⇒ ★An absence is not a symbol.
			//
			// ⚠NOT AN ASCII CHARACTER, so it is set as UTF-16 rather than written as a narrow
			//   literal - MSVC would convert a narrow "≠" to the system code page and the cell
			//   would show whatever that came to (memory cpp-japanese-needs-bom, and the same
			//   reason KESCMLoc.h keeps its Japanese in u"..." and calls SetXString).
			switch (change.fKind)
			{
				case 1:  kind = PMString("+"); break;	// insert
				case 2:  kind = PMString("-"); break;	// delete
				default:								// replace
				{
					const char16_t notEqual[] = u"≠";
					kind.SetXString(reinterpret_cast<const UTF16TextChar*>(notEqual), 1);
					break;
				}
			}
			kind.SetTranslatable(kFalse);

			// ★Already the right side for its kind, already cut to length, and already SPLIT where
			//   the colour changes - the model decided all three (KESCMStoryList.h). Nothing is
			//   chosen here, and in particular the split is not made here: the boundary between the
			//   context and the change is a code point index into text that has been cut at both
			//   ends, and PMString counts UTF-16.
			textPre = change.fTextPre;
			textMid = change.fText;
			textPost = change.fTextPost;
			textPre.SetTranslatable(kFalse);
			textMid.SetTranslatable(kFalse);
			textPost.SetTranslatable(kFalse);
		}

		// ★The sign goes in the RIGHT-HAND cell, the one the story row uses to name its kinds
		//   (2026-08-20, user's call - see the .fr). A change row has no left-hand cell at all
		//   now, so there is nothing else to write here.
		this->SetNodeName(widgetList, kind, kKESCMStoryRowKindWidgetID);

		// ★The hand-drawn cell, written through its own interface. A nil here would mean the row
		//   resource and this code disagree about what the middle cell is, which nothing at runtime
		//   could repair - so it is a quiet skip, and what shows is an empty cell rather than a
		//   stale one (the row above still names the story, so the reader is not misled).
		IControlView* textCell = widgetList->FindWidget(kKESCMStoryRowTextWidgetID);
		InterfacePtr<IKESCMStoryCellData> cellData(textCell, UseDefaultIID());
		if (cellData != nil)
		{
			cellData->SetSegments(textPre, textMid, textPost);
			// ★Writing the strings does not ask for a redraw - SetNodeName does that for a stock
			//   cell, and this one has no such courtesy. Without it a recycled row can keep the
			//   picture the row it used to be left behind. (KBS's widget manager makes the same
			//   call for the same reason, right after handing its cell its segments.)
			textCell->Invalidate();
		}

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

	// ★★THE SECOND LEVEL IS OPEN FROM THE START (2026-08-20). A tree node is collapsed by default,
	//   and a collapsed tree here would hide the very thing the Story Changes mode exists to show -
	//   the reader would have to open each story to find out what changed in it, one at a time,
	//   which is the question they already asked by choosing the mode.
	//
	//   ⚠kTrue = expand the descendants too, from the hidden root, so this opens every story in one
	//   call. The pixel mode reaches this line as well and nothing happens there: no row has
	//   children, so there is nothing to expand (which is why this is not guarded by the mode - a
	//   guard would be describing the same emptiness twice).
	//
	//   ⚠The expansion is redone on every rebuild because ClearTree(kTrue) drops the remembered
	//   list. That is wanted: a rebuild means a new comparison, and a new comparison's rows are not
	//   the ones the reader had opened or closed.
	treeMgr->ExpandNode(KESCMStoryNodeID::CreateRoot(), kTrue /*expandAllDescendants*/);
}

// End, KESCMStoryTreeWidgetMgr.cpp.
