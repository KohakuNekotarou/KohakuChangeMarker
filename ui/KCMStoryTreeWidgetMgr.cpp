//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  How one row of the Story Edits list is built and filled. Three cells, left to right: the story's
//  UID, its opening words, and what kind of change moved (2026-08-10 - it was two cells before, and
//  the kind column spelled every kind out; now it names the first and says "+" for the rest).
//
//  ★THREE ROW SHAPES SINCE 2026-08-22: a story row, a change row, and a change row twice as tall
//  for a RUBY, whose reading is drawn above the characters it belongs to. The three overrides that
//  build a row - which resource, which WidgetID, how tall - must agree about which shape a node is,
//  so they all ask IsTwoLineNode and nothing works it out for itself.
//
//  The list is NOT flat (it has had two levels since 2026-08-20), but this file still does none of
//  the indent arithmetic KBS's widget manager exists for: each level's layout lives in its own
//  resource. See ApplyIndentToWidget below for why that was the right way round.
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
#include "ITextControlData.h"		// the ruby row's "Mono" / "Group" cell is written directly, not through SetNodeName
#include "ITreeViewMgr.h"			// ClearTree / ChangeRoot - the rebuild

// General includes:
#include "CTreeViewWidgetMgr.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "CreateObject.h"			// CreateObjectNoInit
#include "KCMStoryNodeID.h"		// our node class: (row, change). Was ListIndexNodeID until 2026-08-20
#include "LocaleSetting.h"
#include "PMString.h"
#include "RsrcSpec.h"

// Published under source/open, reached by a relative path rather than by adding an include
// directory - the same reasoning, and the same route, as KCMStorySection.cpp's splitter headers:
// the build files that would carry such a directory live outside this plug-in's repository, so a
// path added there would not survive a fresh checkout.
#include "../../open/includes/widgets/DVPublicUtilities.h"	// dv_utils::SetThemeForView

// Project includes:
#include "IKCMStoryCellData.h"	// the change row's hand-drawn text cell takes its three pieces here
#include "IStaticTextAttributes.h"	// SetEllipsizeStyle - WHERE a cell that does not fit loses its characters
#include "KCMUIID.h"
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "Utils.h"					// Utils<IKCMStoryEditsFacade>()
#include "IKCMStoryEditsFacade.h"	// the rows themselves (Facade since 2026-08-13, Task 14)
#include "IKCMCompareFacade.h"		// GetCompareMode - asked in ONE function, KCMListShowsResources
#include "IKCMResourcesFacade.h"	// the definition rows, when that is what the list is showing
#include "KCMResourceValue.h"		// KCMShortResourceValue - what a `$ID/...` value reads as
#include "KCMXmlPretty.h"			// KCMDecodePercentEscapes - `%3a` in a group's name is a colon
#include "KCMStoryKinds.h"		// KCMStoryChangeKind - the bits KindLabel names. A header of types
									// only, which is why it may be included from either side of the
									// split (KCMStoryStamp.h, where these used to live, cannot: its
									// free functions have bodies in the model .pln alone)
#include "KCMStoryTree.h"

namespace
{

/** A string from the plug-in's own table, ready to be shown: translated, and no longer a key.

	★The second half matters as much as the first. A translated string left translatable is
	translated AGAIN by whatever it is handed to, against the built-in table - which is how
	"Source:" came out as a style-source phrase in a Japanese locale.
*/
PMString Translated(const char* key)
{
	PMString s(key);
	s.Translate();
	s.SetTranslatable(kFalse);
	return s;
}

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
	watching a refreshed row lose its children: "when nothing has changed any more, say so in the
	Change column; as it is, the children just disappear and it is hard to tell"). When the text has actually been put
	side by side and comes out the same, saying "Text+" would be repeating what the counters said
	BEFORE anybody looked at the words - which is exactly the reading the refresh just disproved.
	⚠It is not "unchanged": the counters moved, or there would be no row. It is "no difference in
	the words", and the sameKind flag is only ever set when the diff really ran (fTextCompared).

	★★AN ATTRIBUTE THE DIFF ACTUALLY IDENTIFIED IS NAMED, rather than falling back on the counters'
	"Attr" (user's request: "for Change, say Ruby"). "Attr" is what the two documents'
	CHANGE COUNTERS say - true but vague, and it is all that could be said before the text was
	compared. When the comparison has gone further and found that a RUBY moved over characters
	nobody touched, the row can say so.
	⚠It is checked before the counters and after Added/Removed/None: those three describe the row
	  itself, this describes what was found inside it.
	★IT TAKES THE KIND RATHER THAN A "hasRuby" FLAG, so that a second attribute costs a key here and
	  nothing else. Kenten (emphasis dots) is that second one, reported again since 2026-09-01.
	★AND HOW MANY KINDS, so that a story whose ruby AND kenten both moved over unchanged text reads
	  "Ruby+" (2026-09-03, user's ask) - the same '+' the counters' "Text+" below has always used, and
	  for the same reason: the word names the first thing, the sign says there was a second.

	@param sameKind kTrue when the text was compared and nothing differs - see above.
	@param attrKind which attribute the children found FIRST, as KCMStoryAttrKind; 0 for none.
	@param attrKindCount how many DIFFERENT attribute kinds they found; > 1 puts the '+' on.
*/
PMString KindLabel(uint32 kinds, bool16 sameKind, int32 attrKind, int32 attrKindCount,
				   bool16 hasTextChange)
{
	if (sameKind)
		return Translated(kKCMStoryKindNoneKey);

	if (kinds & kKCMStoryKindAdded)
		return Translated(kKCMStoryKindAddedKey);

	if (kinds & kKCMStoryKindRemoved)
		return Translated(kKCMStoryKindRemovedKey);

	// ★What the diff FOUND, ahead of what the counters merely reported. ⚠Only when the text itself
	//   did not change: a story whose words were rewritten AND whose ruby moved is a text edit
	//   first, and the "Text+" below already says there was more than one kind of change.
	// ⚠★★★**"THE TEXT DID NOT CHANGE" IS THE DIFF'S ANSWER, NOT THE COUNTERS'** (2026-09-08). This
	//   read `(kinds & kKCMStoryKindText) == 0` until footnotes arrived, and the two questions had
	//   never disagreed because ruby and kenten are not characters. **A note's marker IS one**:
	//   adding an endnote moves the document's text counter, so the row said "Text+" for an edit
	//   whose only difference was the note - the very row this feature exists to make readable.
	//   ⇒ ask what was FOUND (KCMStoryRow::fHasTextChange). The counters keep their job one branch
	//   down, where nothing was diffed and they are all there is.
	// ⚠★★EACH REPORTED ATTRIBUTE NAMES ITSELF. Ruby has since the beginning; kenten does again
	//   since 2026-09-01 (user's call), the comparison behind it having been switched back on in
	//   KCMStoryDiffRun's AddAttributeChanges. **A branch here is only reachable while that call
	//   exists** - which is the pair to watch if either is ever removed again.
	if (!hasTextChange)
	{
		const char* attrKey = nil;
		if (attrKind == kKCMStoryAttrRuby)
			attrKey = kKCMStoryKindRubyKey;
		else if (attrKind == kKCMStoryAttrKenten)
			attrKey = kKCMStoryKindKentenKey;
		else if (attrKind == kKCMStoryAttrFootnote)
			attrKey = kKCMStoryKindFootnoteKey;
		else if (attrKind == kKCMStoryAttrEndnote)
			attrKey = kKCMStoryKindEndnoteKey;

		if (attrKey != nil)
		{
			// ★"Ruby+" / "Kenten+" when the children hold more than one kind (2026-09-03). Composed
			//   the same way as the "Text+" below, and for the same reason it is no longer a key.
			PMString named = Translated(attrKey);
			if (attrKindCount > 1)
			{
				named.SetTranslatable(kFalse);
				named.Append("+");
			}
			return named;
		}
	}

	PMString out;
	out.SetTranslatable(kFalse);	// composed, so no longer a key - see the note in KCMStoryList.cpp

	const uint32 bits[3] = { kKCMStoryKindText, kKCMStoryKindAttr, kKCMStoryKindOther };
	const char* const keys[3] = { kKCMStoryKindTextKey, kKCMStoryKindAttrKey, kKCMStoryKindOtherKey };

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
class KCMStoryTreeWidgetMgr : public CTreeViewWidgetMgr
{
public:
	// ★kHierarchical since 2026-08-20: the list HAS levels now (a story row can hold the changes
	//   found inside it). It said kList until then, which told the base class to leave the indent
	//   machinery alone - see ApplyIndentToWidget below for why that machinery still must not be
	//   allowed to run on a story row, and what is done instead.
	KCMStoryTreeWidgetMgr(IPMUnknown* boss) : CTreeViewWidgetMgr(boss, kHierarchical) {}
	virtual ~KCMStoryTreeWidgetMgr() {}

	virtual IControlView* CreateWidgetForNode(const NodeID& node) const
	{
		// ★Three row templates since 2026-08-22: a story row, a change row, and a change row drawn
		//   on TWO LINES for a ruby (the reading stands above the characters it belongs to). They
		//   are the same shape (see the .fr) so that the indent arithmetic below behaves the same
		//   on all of them - what differs is what the cells hold, where they start, and how tall
		//   the row is.
		// ★★THE RESOURCES MODE USES THE STORY ROW'S RESOURCE FOR BOTH OF ITS LEVELS (2026-09-09).
		//   Its child rows - one per attribute that differs - hold three plain strings just as its
		//   parent rows do (a name, a value and a sign), so they are the SAME SHAPE and the same
		//   height, and the change row's hand-drawn cell has nothing to offer them. What tells parent
		//   from child on screen is the indent applied in ApplyDataToWidget, not a second resource.
		//   ⚠This is also why GetWidgetTypeForNode below answers one ID in this mode: two shapes that
		//     really are identical MAY be recycled onto each other, and saying otherwise would only
		//     make the tree build widgets it already had.
		TreeNodePtr<KCMStoryNodeID> nodeID(node);
		const bool16 isChange = (nodeID != nil && nodeID->IsChangeRow() && !KCMListShowsResources());
		const RsrcID rsrcID = !isChange           ? kKCMStoryRowRsrcID
							  : IsTwoLineNode(node) ? kKCMStoryRubyRowRsrcID
												    : kKCMStoryChangeRowRsrcID;

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
		// ★THE THREE KINDS MUST ANSWER DIFFERENT IDs. This is what the framework uses to decide
		//   whether a recycled widget can be reused for a node - answer the same ID for two of them
		//   and a change row would be handed a story row's widget (and vice versa) as the list
		//   scrolls. ⚠Since 2026-08-22 that includes the ruby row, and there the consequence is
		//   worse than wrong contents: the widget carries its own HEIGHT, so a recycled tall row
		//   would overlap the row below it.
		//   ★In the Resources mode BOTH levels answer kKCMStoryRowWidgetID, because both really are
		//     the same widget (see CreateWidgetForNode). The rule this comment states is about kinds
		//     that DIFFER; two that do not are meant to share.
		TreeNodePtr<KCMStoryNodeID> nodeID(node);
		if (nodeID == nil || !nodeID->IsChangeRow() || KCMListShowsResources())
			return kKCMStoryRowWidgetID;

		return IsTwoLineNode(node) ? kKCMStoryRubyRowWidgetID : kKCMStoryChangeRowWidgetID;
	}

	// Answer both size questions rather than letting the base class build a widget and measure it.
	// A row is as wide as the list, which has no columns to add up and no horizontal scroll bar.
	//
	// ★★ROWS ARE NO LONGER ALL THE SAME HEIGHT (2026-08-22). A ruby change is drawn on two lines,
	//   so it gets two lines' worth of room; everything else keeps the one height the row resources
	//   and the tree's scroll increments are written in.
	//   ⚠THE SCROLL INCREMENTS STAY AT ONE ORDINARY ROW. They say how far a click on the scroll
	//     arrow moves the list, and "one ordinary row" is the right answer whatever else is in it.
	//   ⚠AND ChangeRoot MUST NO LONGER BE PROMISED A CONSTANT HEIGHT - see KCMStoryTreeRebuild.
	virtual PMReal GetNodeWidgetHeight(const NodeID& node) const
	{
		return PMReal(IsTwoLineNode(node) ? kKCMStoryRubyRowHeight : kKCMStoryRowHeight);
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

		TreeNodePtr<KCMStoryNodeID> nodeID(node);
		const bool16 showsResources = KCMListShowsResources();
		const bool16 isChangeNode = (nodeID != nil && nodeID->IsChangeRow()) ? kTrue : kFalse;

		// ***** THE COLUMNS ARE LAID OUT HERE, ONCE, FOR EVERY ROW BUILT FROM THE STORY ROW'S
		// RESOURCE. ***** That is every row except the Story mode's change rows, which have a
		// resource of their own with no left cell at all (see ApplyChangeRow).
		// ★It is done on EVERY apply for the same reason the cells' text is: row widgets are
		//   recycled, and one that comes back from a list drawn in the other mode would otherwise
		//   keep that mode's column widths.
		// ★★**THE VALUES STAY IN ONE COLUMN AND ONLY THE NAME STEPS IN** (the user's call; the
		//   header carries the whole history). 12px: enough to see, less than the 16px of the
		//   expander column, which is what "a little" was asked for.
		if (showsResources || !isChangeNode)
		{
			const int32 kAttrNameIndent = 12;
			KCMApplyListColumnWidths(widgetList->FindWidget(kKCMStoryRowUIDWidgetID),
									 widgetList->FindWidget(kKCMStoryRowTextWidgetID),
									 widgetList->FindWidget(kKCMStoryRowKindWidgetID),
									 (showsResources && isChangeNode) ? kAttrNameIndent : 0);

			// ★★★AND WHERE THE ELLIPSIS FALLS, on the same schedule and for the same reason
			//   (2026-09-10, the user's call: "when the panel is narrowed it shortens from both
			//   ends; for Resources shorten from the back only").
			//   KCMUI.fr declares this cell **kEllipsizeMiddle**, which is right for a story's
			//   text - its opening and its closing words both help a reader place the edit - but
			//   **a definition is read from the left**: `GroupA:StyleX`, `Color/PANTONE 021 C`.
			//   The middle is the one part of it that must not go.
			//   ⚠**SET IN BOTH DIRECTIONS, EVERY TIME**, exactly like the widths above. A cell
			//     coming back from a list drawn in the other mode keeps that mode's setting, and
			//     "leave it alone unless it is ours" is the trap this list has already sprung once
			//     (2026-09-09, the child row's right edge: what is not written is the previous
			//     value, not nothing).
			InterfacePtr<IStaticTextAttributes> textAttrs(
				widgetList->FindWidget(kKCMStoryRowTextWidgetID), UseDefaultIID());
			if (textAttrs != nil)
				textAttrs->SetEllipsizeStyle(showsResources ? kEllipsizeEnd : kEllipsizeMiddle);
		}

		// ★★THE RESOURCES MODE OWNS BOTH OF ITS LEVELS AND IS ASKED FIRST (2026-09-09). A definition
		//   row and an attribute row under it hold three plain strings each, from the definitions
		//   model - the story model is not consulted anywhere in this branch. Asking the mode BEFORE
		//   the change-row test is what keeps the two models apart: the node types are shared, so a
		//   Resources attribute row IS a "change row" as far as the NodeID is concerned, and the
		//   test below would hand it to the story machinery.
		if (showsResources)
		{
			if (isChangeNode)
				return this->ApplyResourceAttrRow(nodeID->GetRow(), nodeID->GetChange(), widgetList);

			return this->ApplyResourceRow((nodeID != nil) ? nodeID->GetRow() : -1, widgetList);
		}

		// ★A CHANGE ROW IS WRITTEN BY ITS OWN BRANCH AND RETURNS. Its three cells hold different
		//   things from a story row's, and its widget came from a different resource, so nothing
		//   below applies to it.
		if (isChangeNode)
			return this->ApplyChangeRow(*nodeID, widgetList);

		// ★A row COPIED out of the model, not a pointer into its list (Task 14). The three cells
		//   below are written from it and nothing here outlives the call, so the copy costs one
		//   PMString per row drawn.
		IKCMStoryEditsFacade::Row row;
		const bool16 haveRow = (nodeID != nil)
			&& Utils<IKCMStoryEditsFacade>()->GetRow(nodeID->GetRow(), row);

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
				&& (Utils<IKCMStoryEditsFacade>()->GetChangeCount(nodeID->GetRow()) == 0);
			kinds = KindLabel(row.fKinds, sameKind, row.fAttrKind, row.fAttrKindCount,
							  row.fHasTextChange);
		}
		else if (Utils<IKCMStoryEditsFacade>()->GetRowCount() == 0)
		{
			// ★The placeholder the adapter asks for while a comparison is running and found nothing
			//   (see GetNumListItems). Left cell only: there is no kind to name.
			text = Translated(kKCMStoryNoEditsKey);
		}

		this->SetNodeName(widgetList, uid, kKCMStoryRowUIDWidgetID);
		this->SetNodeName(widgetList, text, kKCMStoryRowTextWidgetID);
		this->SetNodeName(widgetList, kinds, kKCMStoryRowKindWidgetID);
		return kTrue;
	}

private:
	/** Fill the three cells of one row from the Resources list: kind, key, and what happened.

		★THE CELLS ARE THE PIXEL MODE'S. This is why the Resources mode needed no tree, no row
		resource and no widget manager of its own - a definition fits the row a changed story
		already uses, column for column.

		⚠ALL THREE CELLS ARE WRITTEN ON EVERY APPLY, blank ones included, for the same reason the
		  story branch does it: row widgets are recycled as the list scrolls, and a cell left alone
		  keeps what the row it used to be had in it.
	*/
	bool16 ApplyResourceRow(int32 rowIndex, IPanelControlData* widgetList) const
	{
		PMString kind, key, what;
		kind.SetTranslatable(kFalse);
		key.SetTranslatable(kFalse);
		what.SetTranslatable(kFalse);

		Utils<IKCMResourcesFacade> resources;
		KCMResourceChangeKind changeKind = kKCMResourceChanged;
		const bool16 haveRow = resources && (rowIndex >= 0)
			&& resources->GetNthChange(rowIndex, kind, key, changeKind);

		if (haveRow)
		{
			switch (changeKind)
			{
				case kKCMResourceAdded:		what = "Added";		break;
				case kKCMResourceRemoved:	what = "Removed";	break;
				default:					what = "Changed";	break;
			}
			what.SetTranslatable(kFalse);
		}
		else if (resources && resources->GetChangeCount() == 0)
		{
			// ★The placeholder the adapter asks for while a comparison is running and found nothing.
			//   "The definitions are identical" and "nothing has been compared" must not look alike,
			//   which is the same distinction the story list draws with its "No edits" row.
			key = Translated(kKCMResourcesNoChangesKey);
		}

		// ***** THE KIND IS NOT SPELLED TWICE ON ONE ROW. ***** (2026-09-09, the user's call: "for the
		// parent of the style part, make the Definition just the style name".) The model's key is
		// `ParagraphStyle/Unused` - kind, separator, name - and the LEFT cell of this very row
		// already says `ParagraphStyle`, so the Definition column shows `Unused`.
		//
		// ★IT IS DONE HERE, IN THE VIEW, AND NOT IN THE MODEL. The full key is the definition's
		//   identity: it is what pairs the two documents, what the diagnostic property prints, and
		//   what tells `Color/Black` from `Ink/Black`. Shortening it at the model would shorten it
		//   for everyone. What is shortened is one cell of one list, because of what the cell NEXT
		//   to it happens to say.
		//
		// ⚠**THREE THINGS ARE CHECKED, NOT TWO.** The key has to begin with the kind, something has
		//   to be left over, **and the character between them has to be a SEPARATOR**.
		//   - `DocumentPreference` IS its kind with no name after it; stripping would leave an empty
		//     cell, a row that says nothing at all.
		//   - ⚠★★And without the third test, a key that merely STARTS with the kind's letters is
		//     silently mutilated: kind `Color` against a key `ColorABC` passes "begins with" and
		//     "something is left", and one character too many comes off - `BC`. (Found on
		//     2026-09-09 re-reading this the same day it was written. The comment that stood here
		//     claimed "the test is a comparison, not an assumption about the format" while the code
		//     went on assuming the byte after the kind was a separator.)
		//   ★The separator is still not NAMED - `/` today, `#` and `@` in other shapes
		//     (KCMResourceDiff.h). What is asserted is only that it is not a letter or a digit,
		//     which is what tells `Color/Black` from `ColorABC`.
		if (haveRow)
		{
			const CharCounter kindLen = kind.CharCount();
			if (kindLen > 0 && key.CharCount() > kindLen + 1)
			{
				PMString head(key);
				head.Remove(kindLen, kMaxInt32);		// keep only as many characters as the kind has
				// ★PlatformChar's own predicates rather than arithmetic of ours: it is multibyte
				//   aware, and IsAlpha / IsNumber are exactly the question being asked
				//   (PlatformChar.h:199-203, :177-179).
				const PlatformChar between = key.GetChar(kindLen);
				const bool16 isSeparator = (!between.IsAlpha() && !between.IsNumber()) ? kTrue : kFalse;
				if (isSeparator && head.Compare(kTrue, kind) == 0)
					key.Remove(0, kindLen + 1);			// the kind, and the separator after it
			}

			// ★★AND THE ESCAPES ARE READ, LAST. A style inside a group carries the group in its
			//   Self with the separator written `%3a`, which reads as a mojibake and is not one
			//   (KCMXmlPretty.h holds the measurement). ⚠After the strip, not before: the kind's
			//   own separator is written plain, and decoding first could produce another one.
			key.SetUTF8String(KCMDecodePercentEscapes(key.GetUTF8String()));
			key.SetTranslatable(kFalse);
		}

		this->SetNodeName(widgetList, kind, kKCMStoryRowUIDWidgetID);
		this->SetNodeName(widgetList, key, kKCMStoryRowTextWidgetID);
		this->SetNodeName(widgetList, what, kKCMStoryRowKindWidgetID);
		return kTrue;
	}

	/** Fills one ATTRIBUTE row - a child of a definition row (2026-09-09, the user's request:
		"give the result rows children, the changed part of a paragraph style as a child, PointSize
		in the Kind part and the number in the Definition part").

		★★THE THREE CELLS ARE: the attribute's name, ITS NEW VALUE, and a sign.
		  ⚠**The value shown here is the TARGET's** - the newer document's. The Source's is in the
		    panel's upper pane (KCMResourceValue.cpp), and the split is the user's: "the top of the
		    panel just shows the source side". So the row and the band are two halves of one
		    reading rather than the same reading twice.

		★★THE SIGN IS THE STORY MODE'S - `+`, `-`, `≠` (the user's call: "the Change part with the
		  symbol, the same as Story"). The vocabulary is worth sharing exactly: a reader who has
		  learnt it on one list can read the other, and the same argument that settled it there
		  applies here - a narrow column, a reader scanning down it, and nothing to translate.
		  ★An attribute the Source does not have is `+`, one the Target does not have is `-`, and one
		    both have with different values is `≠`. That is the same three-way split the story list
		    means by them.

		⚠**ALL THREE CELLS ARE WRITTEN ON EVERY APPLY**, blank ones included - row widgets are
		  recycled and a cell left alone keeps what the row it used to be had in it.
	*/
	bool16 ApplyResourceAttrRow(int32 rowIndex, int32 attrIndex, IPanelControlData* widgetList) const
	{
		PMString name, value, sign;
		name.SetTranslatable(kFalse);
		value.SetTranslatable(kFalse);
		sign.SetTranslatable(kFalse);

		PMString source, target;
		Utils<IKCMResourcesFacade> resources;
		const bool16 have = resources && (rowIndex >= 0) && (attrIndex >= 0)
			&& resources->GetNthAttr(rowIndex, attrIndex, name, source, target);

		if (have)
		{
			name.SetTranslatable(kFalse);

			// ★What the cell shows is the TAIL of the value: `$ID/[No paragraph style]` reads as
			//   "[No paragraph style]" (the user's call, 2026-09-09). The rule and its guard live
			//   in KCMResourceValue.h, because the band above this list has to shorten the other
			//   side the same way.
			value = KCMShortResourceValue(target);
			value.SetTranslatable(kFalse);

			// ⚠NOT AN ASCII CHARACTER, so `≠` is set as UTF-16 rather than written as a narrow
			//   literal - MSVC would convert it to the system code page and the cell would show
			//   whatever that came to (the change row above carries the same note and the same code).
			if (source.IsEmpty())
				sign = PMString("+");			// only the newer document has it
			else if (target.IsEmpty())
				sign = PMString("-");			// only the older one has it
			else
			{
				const char16_t notEqual[] = u"≠";
				sign.SetXString(reinterpret_cast<const UTF16TextChar*>(notEqual), 1);
			}
			sign.SetTranslatable(kFalse);
		}

		this->SetNodeName(widgetList, name, kKCMStoryRowUIDWidgetID);
		this->SetNodeName(widgetList, value, kKCMStoryRowTextWidgetID);
		this->SetNodeName(widgetList, sign, kKCMStoryRowKindWidgetID);
		return kTrue;
	}

	/** Is this node a change that has to be drawn on TWO LINES - i.e. an attribute difference,
		which today means a ruby (2026-08-22)?

		★ONE QUESTION IN ONE PLACE. Three overrides above need the answer and they must agree
		exactly: the resource decides how tall the widget is built, the WidgetID decides which
		widgets may be recycled onto it, and GetNodeWidgetHeight decides how much room the tree
		leaves for it. Two of the three agreeing is a row that overlaps its neighbour or a gap
		under it ([[one-question-one-place]]).

		★IT ASKS THE MODEL RATHER THAN REMEMBERING. Nodes hold indices, not data, and the list is
		replaced whole by the next comparison (KCMStoryNodeID.h) - anything cached here would
		outlive what it describes. The call is the cheap one for exactly this reason: it copies
		one int where GetChange copies eight strings (IKCMStoryEditsFacade.h).

		★TWO ENTRANCES, ONE ANSWER. The three overrides start from a NodeID; the apply below has
		already unpacked one. Rather than let the apply ask the model in its own words - which is
		how the drawing and the row height would drift apart - the unpacking is the only thing
		that differs, and both end here. */
	bool16 IsTwoLineChange(int32 row, int32 change) const
	{
		// ★★NAMED KINDS, NOT "any attribute": the upper line has to be worth having, and an
		//   attribute nothing can show there would leave it permanently empty. Ruby earns it with a
		//   reading; kenten earns it since 2026-09-01 by having its KIND DRAWN there as the mark
		//   itself (KCMKentenMark, user's call). A third attribute would have to earn it in turn -
		//   which is why this stays a list and does not become "attrKind != none".
		// ★A FOOTNOTE AND AN ENDNOTE EARN IT WITH THEIR NUMBER (2026-09-08, user's request: "the
		//   page shows a 1 above the character - show it in the row the way ruby is shown"). The
		//   number is written out on the upper line exactly as a reading is, which is also what the
		//   page does with it.
		const int32 attrKind = Utils<IKCMStoryEditsFacade>()->GetChangeAttrKind(row, change);
		if (attrKind != static_cast<int32>(kKCMStoryAttrRuby) &&
			attrKind != static_cast<int32>(kKCMStoryAttrKenten) &&
			attrKind != static_cast<int32>(kKCMStoryAttrFootnote) &&
			attrKind != static_cast<int32>(kKCMStoryAttrEndnote))
			return kFalse;

		// ⚠★★AND THE UPPER LINE HAS TO HAVE SOMETHING IN IT (2026-09-01, user's call: "when the
		//   ruby or the kenten is gone, make it one line"). **This reverses the decision of
		//   2026-08-22**, which kept a removed attribute on two lines so that its base text would
		//   not sit half a row higher than its neighbours. Measured against the alternative, the
		//   gap was the worse of the two: a blank upper line reads as "something should be here",
		//   and the row that most needs to be plainly readable is the one where the mark is gone.
		//   ★The height and the drawing still come from THIS ONE ANSWER, which is what stops them
		//   disagreeing - the point the older note was really making.
		return Utils<IKCMStoryEditsFacade>()->GetChangeHasAttrValue(row, change);
	}

	bool16 IsTwoLineNode(const NodeID& node) const
	{
		// ⚠**THE RESOURCES MODE MUST NOT REACH THE STORY MODEL HERE.** Its child nodes carry the
		//   same pair of indices, and IsTwoLineChange would ask IKCMStoryEditsFacade about a row that
		//   belongs to a different list. It would answer - out of range is a legal question there -
		//   and the answer would mean nothing. No row in this mode is ever two lines.
		TreeNodePtr<KCMStoryNodeID> nodeID(node);
		if (nodeID == nil || !nodeID->IsChangeRow() || KCMListShowsResources())
			return kFalse;

		return this->IsTwoLineChange(nodeID->GetRow(), nodeID->GetChange());
	}

	/** Fills one CHANGE row: what sort of edit it was, and the words it concerns.

		★TWO CELLS, WHERE A STORY ROW HAS THREE (2026-08-20): the words on the left, and the sign
		that says what sort of edit it was on the right, in the column the story row names its
		kinds in. Both are written on every apply - a recycled widget keeps whatever the row it
		used to be had in it.

		★THE CELL IDs ARE THE STORY ROW'S. A widget ID has to be unique only among the descendants
		of one parent (guide vol2-12), and these two rows are never each other's descendants. The
		same reuse is already in this plug-in: the book dialog's row shares them too
		(kKCMBookRowNameWidgetID == kKCMStoryRowTextWidgetID, and so on).

		⚠THE TWO CELLS ARE NOT WRITTEN THE SAME WAY. The text cell is drawn by hand so that the
		changed characters can keep the theme's text colour while the words around them fade, and a
		hand-drawn cell holds no ITextControlData for SetNodeName to write - it is handed its three
		pieces through IKCMStoryCellData instead. The sign's cell is a stock static text and
		still goes through SetNodeName.
	*/
	bool16 ApplyChangeRow(const KCMStoryNodeID& nodeID, IPanelControlData* widgetList) const
	{
		IKCMStoryEditsFacade::Change change;
		const bool16 have = Utils<IKCMStoryEditsFacade>()->GetChange(
								nodeID.GetRow(), nodeID.GetChange(), change);

		PMString kind;
		PMString textPre, textMid, textPost, ruby;
		bool16 twoLines = kFalse;
		int32 attrKind = 0;		// KCMStoryAttrKind: 0 = none, 1 = ruby, 2 = kenten
		kind.SetTranslatable(kFalse);
		textPre.SetTranslatable(kFalse);
		textMid.SetTranslatable(kFalse);
		textPost.SetTranslatable(kFalse);
		ruby.SetTranslatable(kFalse);

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
			//   reason KCMLoc.h keeps its Japanese in u"..." and calls SetXString).
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
			//   the colour changes - the model decided all three (KCMStoryList.h). Nothing is
			//   chosen here, and in particular the split is not made here: the boundary between the
			//   context and the change is a code point index into text that has been cut at both
			//   ends, and PMString counts UTF-16.
			textPre = change.fTextPre;
			textMid = change.fText;
			textPost = change.fTextPost;
			textPre.SetTranslatable(kFalse);
			textMid.SetTranslatable(kFalse);
			textPost.SetTranslatable(kFalse);

			// ★THE READING, AND WHETHER THERE ARE TWO LINES AT ALL, ARE TWO DIFFERENT FACTS
			//   (2026-08-22). A ruby that was REMOVED has no reading to show on the newer side -
			//   fRuby is empty - and the row still has to be laid out on two lines, or its base
			//   text would sit half a row higher than the rows around it. So the flag comes from
			//   what SORT of change this is, never from whether the string is empty.
			// ⚠It is asked of the same helper the three overrides above use, rather than read off
			//   change.fWhat here: two ways of answering it is how the drawing and the row height
			//   come to disagree.
			twoLines = this->IsTwoLineChange(nodeID.GetRow(), nodeID.GetChange());
			if (twoLines)
			{
				ruby = change.fRuby;
				ruby.SetTranslatable(kFalse);

				// ★WHICH attribute it is, carried through to the cell. The cell writes a READING
				//   out as text and paints a KIND as a mark, and the string alone cannot tell it
				//   which it has - a reading could be the word "Bullseye".
				attrKind = change.fAttrKind;
			}
		}

		// ★The sign goes in the RIGHT-HAND cell, the one the story row uses to name its kinds
		//   (2026-08-20, user's call - see the .fr). A change row has no left-hand cell at all
		//   now, so there is nothing else to write here.
		this->SetNodeName(widgetList, kind, kKCMStoryRowKindWidgetID);

		// ★The hand-drawn cell, written through its own interface. A nil here would mean the row
		//   resource and this code disagree about what the middle cell is, which nothing at runtime
		//   could repair - so it is a quiet skip, and what shows is an empty cell rather than a
		//   stale one (the row above still names the story, so the reader is not misled).
		IControlView* textCell = widgetList->FindWidget(kKCMStoryRowTextWidgetID);
		InterfacePtr<IKCMStoryCellData> cellData(textCell, UseDefaultIID());
		if (cellData != nil)
		{
			cellData->SetSegments(textPre, textMid, textPost, ruby, twoLines, attrKind);
			// ★Writing the strings does not ask for a redraw - SetNodeName does that for a stock
			//   cell, and this one has no such courtesy. Without it a recycled row can keep the
			//   picture the row it used to be left behind. (KBS's widget manager makes the same
			//   call for the same reason, right after handing its cell its segments.)
			textCell->Invalidate();
		}

		// ★HOW THE RUBY IS SET, on the upper line's right-hand column - the half of that column the
		//   sign leaves empty (2026-09-08, user's request: "when it changes from group to mono, the
		//   row does not say what changed"). Re-setting group ruby as mono leaves every reading
		//   identical, so both lines of the row read the same and the change looks like nothing.
		// ⚠**ONLY WHERE THE SETTING IS PART OF WHAT CHANGED.** Naming it in every ruby row would
		//   make it scenery: a re-typed reading over ruby that stayed mono says nothing by adding
		//   "Mono". So it appears when the two sides are set differently, and when the ruby is NEW -
		//   there is no older setting to differ from, and "this arrived as group ruby" is worth the
		//   same glance. ⚠A ruby that was REMOVED names nothing: there is no newer side to describe.
		// ⚠IT IS FOUND RATHER THAN WRITTEN THROUGH SetNodeName. Only the ruby row carries this
		//   widget, and SetNodeName asserts on a row that does not have the one it was handed.
		// ⚠THE EMPTY STRING IS WRITTEN TOO, and has to be: rows are recycled, so a row that says
		//   nothing must actively say nothing or it keeps the word the row before it left there.
		IControlView* rubyKindCell = widgetList->FindWidget(kKCMStoryRubyKindWidgetID);
		if (rubyKindCell != nil)
		{
			const bool16 isRuby = (have && change.fAttrKind == static_cast<int32>(kKCMStoryAttrRuby)) ? kTrue : kFalse;
			const bool16 settingIsNews = (isRuby && !change.fRuby.IsEmpty() &&
										  (change.fOtherRuby.IsEmpty() ||
										   (change.fRubyGroup != 0) != (change.fOtherRubyGroup != 0))) ? kTrue : kFalse;

			PMString rubyKind;
			if (settingIsNews)
				rubyKind = PMString(change.fRubyGroup ? "Group" : "Mono");
			rubyKind.SetTranslatable(kFalse);	// ★the words the Japanese typesetter uses, not keys

			InterfacePtr<ITextControlData> rubyKindData(rubyKindCell, UseDefaultIID());
			if (rubyKindData != nil)
				rubyKindData->SetString(rubyKind);
		}

		return kTrue;
	}
};

CREATE_PMINTERFACE(KCMStoryTreeWidgetMgr, kKCMStoryTreeWidgetMgrImpl)

//----------------------------------------------------------------------------------------
// KCMListShowsResources - the ONE place the list asks which mode is on (see KCMStoryTree.h)
//----------------------------------------------------------------------------------------

bool16 KCMListShowsResources()
{
	// ⚠Utils<T>() has no nil guard of its own, so the OBJECT is tested before -> is used
	//   (utils-boss-facade-access: QueryUtilInterface() dereferences before there is a pointer to
	//   test). With the model half absent there is no comparison at all, so "not Resources" is the
	//   answer that leaves the list exactly as it was before this mode existed.
	Utils<IKCMCompareFacade> compare;
	if (!compare)
		return kFalse;

	return (compare->GetCompareMode() == kKCMModeResources) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// KCMListLeftColumnWidth / KCMApplyListColumnWidths - the columns, laid out per mode
// (see KCMStoryTree.h for why the .fr cannot state both)
//----------------------------------------------------------------------------------------

int32 KCMListLeftColumnWidth()
{
	// ★40 is what the .fr writes, so the Story mode is left executing the resource unchanged: this
	//   returns the number that is already there rather than a second opinion about it.
	// ★120 was measured against the names that actually appear - "ColorGroupSwatch" is 16 characters
	//   at the palette font - with room for the longer element names the export can produce. Wider
	//   than this and the Definition column is starved at the panel's minimum width of 224.
	return KCMListShowsResources() ? 120 : 40;
}

void KCMApplyListColumnWidths(IControlView* leftCell, IControlView* middleCell,
							  IControlView* rightCell, int32 leftIndent)
{
	// ★The home positions, from the .fr's row resource (kKCMStoryRowRsrcID): the left cell starts at
	//   24 - where the expander column ends - and the middle cell starts 4px after the left one ends.
	//   ⚠These two numbers are written in the .fr as well. They are constants HERE because the
	//     recycling rule above forbids reading them off the widget: a row that has already been laid
	//     out once would answer with the answer, not with the question.
	const int32 kHomeLeft = 24;
	const int32 kGap = 4;

	// ★★THE COLUMN ENDS WHERE IT ALWAYS DOES; ONLY THE TEXT STARTS FURTHER IN. `leftEnd` is
	//   computed from the HOME position, not from the indented one, so an indented cell is
	//   narrower rather than shifted - and the Definition column below begins at the same x on a
	//   parent row and on a child row (see the header).
	const int32 leftEnd = kHomeLeft + KCMListLeftColumnWidth();

	if (leftCell != nil)
	{
		PMRect frame = leftCell->GetFrame();
		frame.Left(PMReal(kHomeLeft + leftIndent));
		frame.Right(PMReal(leftEnd));
		leftCell->SetFrame(frame);
	}

	if (middleCell != nil)
	{
		PMRect frame = middleCell->GetFrame();
		frame.Left(PMReal(leftEnd + kGap));

		// ★★THE RIGHT EDGE COMES FROM THE RIGHT CELL, not from a number and not from what this cell
		//   happens to say now. ⚠**Leaving it alone was wrong and was measured**: a child row whose
		//   widget had been laid out under some earlier width kept that right edge, and its
		//   Definition cell ran 96px past the Change column and out of the panel altogether
		//   (2026-09-09, seen in the first build of the attribute rows). The Change cell is bound to
		//   the panel's right edge, so its left edge IS where this column stops, at every width.
		if (rightCell != nil)
			frame.Right(rightCell->GetFrame().Left());

		middleCell->SetFrame(frame);
	}
}

//----------------------------------------------------------------------------------------
// KCMStoryTreeRebuild - redraw the list from the model
//----------------------------------------------------------------------------------------

void KCMStoryTreeRebuild()
{
	// Reached through the panel, which is nil while it is closed - and a comparison run with the
	// panel closed is perfectly normal, so that is a quiet return rather than a failure.
	InterfacePtr<ITreeViewMgr> treeMgr(KCMFindPanelWidget(kKCMStoryTreeWidgetID), UseDefaultIID());
	if (treeMgr == nil)
		return;

	// ClearTree(kTrue) drops the remembered expansion state; ChangeRoot reloads the tree.
	//
	// ★★★ChangeRoot's ARGUMENT IS NOT ABOUT CLEARING - IT IS A PROMISE THAT EVERY ROW WIDGET IS
	//   THE SAME HEIGHT (ITreeViewMgr.h:66, `widgetHeightIsConstant`, default kFalse). It was kTrue
	//   here from the day this list was written, and it was true: GetNodeWidgetHeight answered one
	//   constant. ⚠SINCE 2026-08-22 IT IS NOT - a ruby change is drawn on two lines and its row is
	//   twice as tall - so the promise is withdrawn and the tree measures row by row again, which
	//   is the default and what the mixed heights require.
	// ⚠The two arguments are different questions that happen to be spelled the same way: ClearTree
	//   takes `clearExpandedNodeList` (ITreeViewMgr.h:180), and that one stays kTrue - a rebuild
	//   means a new comparison, whose rows are not the ones the reader had opened or closed.
	treeMgr->ClearTree(kTrue);
	treeMgr->ChangeRoot(kFalse);

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
	treeMgr->ExpandNode(KCMStoryNodeID::CreateRoot(), kTrue /*expandAllDescendants*/);
}

// End, KCMStoryTreeWidgetMgr.cpp.
