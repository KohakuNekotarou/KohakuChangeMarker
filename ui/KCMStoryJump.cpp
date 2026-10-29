//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryJump.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"			// a layout view of the OTHER document - what a view selection
									//  manager is asked of (2026-08-21)
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "ILayoutViewUtils.h"		// GetAllLayoutViews - finding that view. The same call, with the
									//  same arguments, that KCMViewSync and KCMReadDocZoom make
#include "IPageList.h"				// GetPageString - the page number the status line names
#include "ISelectionManager.h"		// DeselectAll / SelectionExists - clearing before selecting
#include "ISelectionUtils.h"		// QueryActiveSelection (the front document's) /
									//  QueryViewSelectionManager (any document's) / ActivateView
// (IFrameList.h / IHierarchy.h / ITextFrameColumn.h stood here for the "which frame is this edit
//  in" walk. That walk moved to the model side on 2026-08-22 so that it composes before it answers
//  - see KCMStoryJumpToChange - and nothing else in this file needed them.)
#include "ITextModel.h"
#include "ITextSelectionSuite.h"	// SetTextSelection - the click's selection (SelectRangeIn)
#include "ITextUtils.h"				// GetPageUIDRef - which page that frame is on
#include "ITool.h"					// IsToolOfType(kTextSelectionTool) - is a text tool already active?
#include "IToolBoxUtils.h"			// QueryActiveTool / QueryTool / SetActiveTool
#include "DocumentPresFindCriteria.h"	// FindPresCriteria::accept_all - the SDK's own "any
									//  presentation will do" predicate
#include "IDocumentPresentation.h"	// MakeActive - what "the active document" actually is
									//  (2026-08-22; IWindow was not it)
#include "IDocumentUIUtils.h"		// FindPresentationForDocument - the route KCMBookOpen, KBS
									//  and KESCL already take to a document's presentation

// General includes:
#include "PMString.h"
#include "RangeData.h"				// the range handed to SetTextSelection
#include "TextEditorID.h"			// kIBeamToolBoss - the Type tool a selection switches to
#include "UIDRef.h"
#include "Utils.h"
#include "WritingModeID2.h"			// IID_ITEMPTEXTSELECTION_SUITE - the second way to ask for the
									//  text selection suite (see SelectRangeIn). Only the IID is
									//  published; there is no ITemptextselection_suite.h in the SDK

// Project includes:
#include "KCMUIID.h"
#include "KCMChangeNav.h"	// KCMGotoStoryFrame
#include "IKCMCompareFacade.h"	// the armed state, reached across the boundary
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMStoryJump.h"
#include "IKCMStoryMarkFacade.h"	// the flash over the characters a change row goes to (2026-08-20).
									// ★2026-08-23: the marker itself moved to the model plug-in, so
									// this side asks through the boundary rather than calling it.
#include "IKCMStoryEditsFacade.h"	// the row a click landed on (Facade since 2026-08-13, Task 14)
#include "IKCMMarkData.h"	// IsPageOnHiddenSpread - a row on a hidden page is labelled, not jumped to (2026-08-18)
#include "KCMStoryTree.h"			// KCMListShowsResources - which kind of row the click landed on
#include "KCMTrackLabels.h"		// the Track mode's heading: who, when, what (2026-10-05)
#include "KCMTrackPlan.h"			// kKCMTrackDelete - a deletion's words are not in the layout (2026-10-25)
#include "KCMResourceValue.h"		// KCMShowSelectedResource - what a Resources row does instead of jumping
#include "KCMStoryKinds.h"			// kKCMStoryAttrObject - an Object row says what changed (2026-10-27)

#include <string>

namespace
{

/** ★(2026-10-27) An Object row's value as a reader's word: the part before the tab ("QR: <its content>", "Group"...) -
	the model's KCMObjectPrintDescription, written again here because the UI plug-in cannot call into the model's code
	(the value's shape is KCMObjectPrint.h's: "<what it is>\t<fingerprint>"). */
PMString KCMObjectWord(const PMString& value)
{
	const std::string all = value.GetUTF8String();
	const std::string::size_type tab = all.find('\t');
	PMString word;
	word.SetUTF8String((tab == std::string::npos) ? all : all.substr(0, tab));
	word.SetTranslatable(kFalse);
	return word;
}

/** Where the status line says the jump landed: "Page: 3", or the pasteboard when there is no page.

	★The page number comes from IPageList::GetPageString with the same seven arguments the
	navigation's own label uses (KCMChangeNav.cpp's KCMStopLabel) - section prefixes and all, so
	that the two places in this panel that name a page never disagree about how a page is spelled.

	★★★2026-08-18 (bug recheck B10, second pass): the seventh argument,
	bIncludePagesOfHiddenSpread, went from kFalse to kTrue. InDesign carries TWO page numbers
	(measured on the machine that day): the Pages panel / page-number field / DOM page.name /
	GetPageString(...,kTrue) all COUNT the pages of hidden spreads, while the folio actually
	composed onto the page - and GetPageString(...,kFalse) - SKIP them. A label that tells a
	person which page to go and look at has to be spelled the way the Pages panel spells it,
	because that is where they will look. KCMChangeNav's label and the TSV export were changed
	the same day for the same reason: the three of them answer ONE question and must not drift.
*/
PMString PageLabel(IDataBase* db, UID pageUID)
{
	PMString label;
	label.SetTranslatable(kFalse);

	if (db == nil || pageUID == kInvalidUID)
	{
		label.Append("Pasteboard");	// a real answer: the story is there, just not on a page
		return label;
	}

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	PMString numStr;
	numStr.SetTranslatable(kFalse);
	if (pageList != nil)
		pageList->GetPageString(pageUID, &numStr, kTrue, kFalse, kDefaultPageType, kTrue, kTrue);

	label.Append("Page: ");
	if (numStr.NumUTF16TextChars() > 0)
		label.Append(numStr);
	else
		label.Append("?");	// a page whose number cannot be had (does not normally happen; the same catch-all as KCMStopLabel)

	// ★★A page on a hidden spread gets "(Hide)" after it ---- the same spelling as the Prev/Next
	//   label (KCMStopLabel). **Do not spell one state two ways in two places**
	//   ([[one-question-one-place]]). ⚠It read "three ways in three places" until 2026-09-14: the
	//   third was the TSV export's Page column, deleted together with its menu item.
	//   ⚠This mark IS the reason KCMGotoStoryFrame decided not to move the layout, so putting in one
	//     without the other makes a row that does not move and does not say why.
	if (db != nil && Utils<IKCMMarkData>()->IsPageOnHiddenSpread(db, pageUID))
		label.Append(" (Hide)");
	return label;
}

/** A layout view of this document, or nil when it has no layout window open.

	★The same call KCMViewSync (:631) and KCMReadDocZoom make, with the same arguments - "every
	layout view of THIS database". A document with no window (one this plug-in opened invisibly, or
	one being closed) answers with an empty list, which is a real answer and not an error.
	★THE FIRST ONE IS TAKEN. What it is wanted for is the document's selection, and a split layout
	view returns two entries for one window; nothing here has a reason to prefer one pane over the
	other, and picking is not the same as needing to pick.
*/
IControlView* FirstLayoutView(IDataBase* db)
{
	if (db == nil)
		return nil;

	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] != nil)
			return views[i];
	}
	return nil;
}

/** The selection manager to make a selection in THIS document with. AddRef'ed - the caller owns it.

	***** TWO ROADS, AND WHICH ONE IS NOT A DETAIL (2026-08-21). *****

	QueryActiveSelection answers "out of the active context" (ISelectionUtils.h:65-76) - the FRONT
	document's. Asking it to select in the other document is what made a Deleted row's double click
	do nothing at all while the target was in front (user's report: "double-clicking does not select
	when the source is not active").
	(⚠Since 2026-10-25 - the double click gone - the one caller is the click's selection, which makes its document the
	 active one first (KCMStoryJumpToChange), so the second road below is the fallback for when that did not take.)

	⇒ The front document keeps the road it had, because THAT is where the story editor and the
	galley live: their selection does not answer to ITextSelectionSuite's default IID and has to be
	asked for a second time with IID_ITEMPTEXTSELECTION_SUITE (the note in SelectRangeIn, measured
	in audit B-U4). Replacing GetActiveSelection with a view-specified manager everywhere would
	have taken that second ask off the road it was put there for.
	⇒ Any OTHER document is asked through one of its layout views, which is allowed to be behind
	  and is the official way to reach a selection that is not the active one (CTracker.cpp:877,
	  CLayoutTracker.cpp:107, CPathCreationTracker.cpp:592, CShape.cpp:415, and the recipe in
	  SnpManipulateStructureView.cpp:209-212).

	★"Which document is in front" is asked of GetActiveDocDB() - the one place KCM asks it through
	  (IActiveContext::GetContextDocument, KCMCore.cpp), the same one KCMChangeNav:550 uses.
	  ⚠NOT of "is the selection manager the same pointer": with a story editor in front, the active
	  manager is that editor's and the layout view's is a different object, so a pointer comparison
	  would call the front document "not active" and quietly drop the second ask.
*/
ISelectionManager* QuerySelectionManagerFor(IDataBase* db)
{
	if (db == nil)
		return nil;

	if (Utils<IKCMCompareFacade>()->GetActiveDocDB() == db)
		return Utils<ISelectionUtils>()->QueryActiveSelection();

	IControlView* view = FirstLayoutView(db);
	if (view == nil)
		return nil;		// no window: nothing to select into, and nothing to say about it

	return Utils<ISelectionUtils>()->QueryViewSelectionManager(view);
}

/** Make this document the active one, so that a selection made in it is the selection the reader
	is looking at.

	★WHO CALLS IT NOW: the click on a change row, which makes the Target the active document before selecting in it
	(since 2026-10-24 night - a selection made in a document behind another is not the one the reader holds), and
	KCMBringArmedTargetToFront / KCMBringDocumentToFront.
	(⛔"ONLY THE DELETED ROW DOES THIS" stood here from 2026-08-21 - the user's call then, a REVERSAL of the same day's
	"windows do not move": "for a deletion, selecting on the source side should make the source document active". It
	was the Deleted row's whole-story double click, gone on 2026-10-25.)

	***** "THE ACTIVE DOCUMENT" IS A PRESENTATION, NOT A WINDOW, AND TWO CALLS WERE MEASURED
		  WRONG BEFORE THIS ONE (2026-08-22). *****

	The obvious two both failed, and they failed QUIETLY - the selection worked, so nothing looked
	broken until app.activeDocument was actually read:

	  1. `ISelectionUtils::ActivateView` alone. It is the official verb for the SELECTION half ("If
	     the two do not match the Active selection will be changed to reflect the given view
	     selection", ISelectionUtils.h:176-181) and the one sample doing this uses it
	     (SnpManipulateStructureView.cpp:209). ⚠Measured: the source's story came out selected AND
	     visible in its window, and the front document did not change.
	  2. `IWindow::BringToFront()`, reached view -> IWidgetParent -> IID_IWINDOW (how the product
	     gets a document's window: LayerSelectionObserver.cpp:128-130, CTracker.cpp:260-261).
	     ⚠Measured: no change either, tab strip included. It has no caller in the SDK, and now
	     there is a reason to think that is not an accident for document windows.

	⇒ `IDocumentPresentation` is the thing that is "active": `MakeActive()` is documented as "Make
	  this the active/TARGET presentation" (IDocumentPresentation.h:111-112). A window is the
	  container; the presentation is what the application points at.

	★★AND THE ROUTE TO IT IS THE ONE THIS FAMILY OF PLUG-INS ALREADY USES, in five other places:
	  Utils<IDocumentUIUtils>()->FindPresentationForDocument + MakeActive - KCM's own
	  BringChapterToFront (ui/KCMBookOpen.cpp), KBSJump.cpp:503, KBSBookScope, and KESCL twice.
	  ⚠The first build of this function asked a layout view for its parent instead
	  (QueryParentFor(IID_IDOCUMENTPRESENTATION), as SnpManipulateStructureView.cpp:190 does). That
	  works, but it makes ONE question - "which presentation shows this document" - answered two
	  ways in one plug-in ([[one-question-one-place]]), and the established way is better: it does
	  not need a view at all.
	  ★NOT GetFrontmostPresentationForDocument: that one answers nil for a window sitting behind
	    another tab, which is exactly the case this function exists for (KCMBookOpen.cpp:145-148,
	    where both sibling plug-ins are recorded as having learned the same thing).
	★FindPresCriteria::accept_all is the SDK's own "any presentation will do" predicate
	  (DocumentPresFindCriteria.h:82). The five callers above each wrote their own one-line
	  predicate; new code should not add a sixth.
	★ActivateView still runs afterwards, for the selection context itself.
*/
void ActivateDocument(IDataBase* db)
{
	if (db == nil)
		return;

	FindPresentation_PreferCriteria noPreference;	// the first presentation found is fine
	IDocumentPresentation* presentation = Utils<IDocumentUIUtils>()->FindPresentationForDocument(
		db, &FindPresCriteria::accept_all, noPreference);
	if (presentation != nil)
		presentation->MakeActive();

	IControlView* view = FirstLayoutView(db);
	if (view != nil)
		Utils<ISelectionUtils>()->ActivateView(view);	// return value is the context; nobody needs it
}

/** Bring a range from the comparison into the story AS IT STANDS NOW.

	★The diff ran against the story as it was when the comparison did, and the reader may have
	edited it since - a stale end would be refused by the selection suite, and a stale start would
	select the wrong words silently.
	(⛔A NEGATIVE `to` meant "to the end of the story" until 2026-10-25 - the whole-story double click's, gone that day.)
*/
void ClampToStory(ITextModel* model, TextIndex& from, TextIndex& to)
{
	const TextIndex total = model->TotalLength();
	if (from < 0) from = 0;
	if (to > total) to = total;
	if (from > total) from = total;
	if (to < from) to = from;
}

/** Select from..to of one story in ONE document, with the Type tool on.

	"How KCM makes a text selection", written once - the click on a change row is its caller (KCMStoryJumpToChange;
	the two double clicks were too, until 2026-10-25).

	@param to the end of the range (clamped to the story as it stands - ClampToStory).
	@return kFalse when this document has no such story, no window, or the suite refused.
*/
bool16 SelectRangeIn(IDataBase* db, UID storyUID, TextIndex from, TextIndex to)
{
	if (db == nil || storyUID == kInvalidUID)
		return kFalse;

	const UIDRef storyRef(db, storyUID);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;	// no such story here - deleted since the comparison read it (the one caller asks of the
						//  Target; until 2026-10-25 the double click asked of the Source too, where an added
						//  story never existed: KCMStoryStamp.h, "WHY TWO VERSIONS CAN BE MATCHED AT ALL")

	ClampToStory(model, from, to);

	// ★Making a selection recomposes, and this plug-in may only have the document open in order to
	//   look at it. IDataBase.h:389-412 restores the flag the document came in with rather than
	//   forcing it clean - the same guard KBS puts round the identical operation.
	//   ★ONE GUARD, ON THE DOCUMENT BEING SELECTED INTO - each document a call selects into gets its own by being its
	//     own call (two for one double click, until 2026-10-25).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	InterfacePtr<ISelectionManager> selectionManager(QuerySelectionManagerFor(db));
	if (selectionManager == nil)
		return kFalse;

	// ***** CLEAR THE SELECTION FIRST - THAT IS THE OFFICIAL ORDER. ***** The recipe is
	// DeselectAll -> Type tool -> SetTextSelection (gotolasttextedit's GTTxtEdtUtils.cpp:113-136),
	// and it clears UNCONDITIONALLY. A page-item selection left standing is a second selection, in a
	// different CSB.
	// ! 2026-08-11, block 15 audit (A-1): this used to run the tool switch first and to ask
	//   SelectionExists() before clearing. Both are gone - the order now matches the sample, and the
	//   question is asked once instead of twice (DeselectAll on an empty selection does nothing, so
	//   the test only duplicated what the call already decides).
	selectionManager->DeselectAll(nil);

	// ***** THE TYPE TOOL, BECAUSE THIS IS AN INVITATION TO EDIT. ***** Text selected while some
	// other tool is active is not text the user can act on, which is the whole of what the click's
	// selection is for (KFC's PutTypeToolOn). The tool goes on before the selection is made, as in the sample.
	// ! This takes the KCM tool off, if it was on. Deliberate (user's call, 2026-08-10), and
	//   written down in How to Use so that it is not a surprise.
	// ★The nil test on the active tool is ours: the sample does not test it at all - it calls
	//   IsTextTool() straight off whatever QueryActiveTool answered (GTTxtEdtUtils.cpp:118-119).
	//   The ASSERT a few lines below it is on the I-beam tool queried next, not on this one.
	// ★The tool is the APPLICATION's, not a document's, so a second call finds it already on and does nothing.
	//   Asking again costs one Query and keeps this function true on its own.
	// ★★IsToolOfType(kTextSelectionTool), NOT IsTextTool() (2026-10-24 night - the 10-03 item, KFC's PutTypeToolOn):
	//   ITool.h:178-183 says IsTextTool "could be more accurately called DoesToolDeactivateTextEditor" - the Zoom,
	//   Gradient and Hand tools answer kTrue to it too - and names this call for "traditional 'text' tools that select
	//   text". KFC measured it: with IsTextTool, text came out selected under the Hand tool, in a window the user
	//   cannot type into. (The sample asks IsTextTool; its header is what it does not read.)
	InterfacePtr<ITool> activeTool(Utils<IToolBoxUtils>()->QueryActiveTool());
	if (activeTool == nil || !activeTool->IsToolOfType(ITool::kTextSelectionTool))
	{
		InterfacePtr<ITool> iBeamTool(Utils<IToolBoxUtils>()->QueryTool(kIBeamToolBoss));
		if (iBeamTool == nil)
			return kFalse;
		if (!Utils<IToolBoxUtils>()->SetActiveTool(iBeamTool))
			return kFalse;
	}

	// ***** ASK TWICE. THE DEFAULT IID IS NIL WHENEVER A TEXT EDITING WINDOW IS IN FRONT. *****
	// Story editor, galley and notes each run their own selection, and it does not answer to
	// ITextSelectionSuite's kDefaultIID - it answers to IID_ITEMPTEXTSELECTION_SUITE
	// (WritingModeID2.h:246; the IID is published but no header for the suite is). The product asks
	// both ways wherever it has to work "for whatever view": InCopyDocUtils.cpp:716-718, and three
	// more times in that same file (:799, :3780, :4014), always in this order and with this reset.
	// ! Before 2026-08-17 this asked once and returned kFalse when the answer was nil - and the
	//   refusal is deliberately silent (see the header), so a double click in the list did nothing
	//   whatsoever while the story editor was in front, with no message to say why.
	// ★MEASURED BOTH WAYS (2026-08-17, audit B-U4), because "the default IID is nil there" was
	//   inherited belief rather than something this plug-in had ever seen: a build with the second
	//   ask taken out again answered sel=0 with the story editor in front - and WORSE than doing
	//   nothing, because the DeselectAll above had already run, so the caret the user was holding
	//   was taken away and nothing was put back. With the second ask, the same double click selects
	//   the whole story (Paragraph, 15 chars), exactly as it does from a layout window.
	//   (The double click measured here went on 2026-10-25; the click's selection takes the same road.)
	// ⚠STILL NOT POSSIBLE, and this is the suite's own rule rather than something to work around
	//   here: a row whose story the front story editor is NOT showing. That window's selection
	//   belongs to its own story, so SetTextSelection for any other one is refused - measured on
	//   the two rows either side of it. The jump still reports the page, so the click is not silent;
	//   only its selecting half declines (and the flash points instead - KCMStoryJumpToChange).
	InterfacePtr<ITextSelectionSuite> textSelectionSuite(selectionManager, UseDefaultIID());
	if (textSelectionSuite == nil)
		textSelectionSuite.reset(InterfacePtr<ITextSelectionSuite>(selectionManager, IID_ITEMPTEXTSELECTION_SUITE).forget());
	if (textSelectionSuite == nil)
		return kFalse;

	// ⚠AN EMPTY RANGE HAS NO WIDTH - a deletion's target side, an insertion's source side.
	//   RangeData(n, n) is the caret case, and RangeData.h:114-125 says a caret needs a lean to
	//   settle which side new text joins. kLeanForward puts it before what followed the deleted
	//   text, which is where the text used to begin.
	//
	//   ★For the whole story the range is (0, TotalLength()), which has length, so no lean applies.
	//     TotalLength() counts every character "including data for embedded tables"
	//     (ITextModel.h:137-140), so a story that is nothing but a table selects that table's text
	//     as well - which is what "the whole story" has to mean for a row that was listed BECAUSE a
	//     table cell was edited. The trailing carriage return is inside the range on purpose: it is
	//     a character of the story, and InDesign's own Select All takes it too.
	//
	//   kDontScrollSelection: the click's jump (KCMGotoStoryFrame) has just centred the frame with
	//   IPanorama::ScrollContentLocationToFrameCenter. kScrollIntoView would only promise the
	//   selection is somewhere on screen, undoing the better answer already given. (The official
	//   sample asks for kScrollIntoView because nothing has scrolled on its behalf.)
	const RangeData range = (to > from)
							? RangeData(from, to)
							: RangeData(from, from, RangeData::kLeanForward);

	return textSelectionSuite->SetTextSelection(storyRef, range, Selection::kDontScrollSelection, nil);
}

}	// anonymous namespace

// (⛔KCMActivateDocument stood here from 2026-09-13 to 2026-09-21: a one-line door out of this
//  file for ActivateDocument, opened for the Resources mode's editor and closed when that editor
//  went. ActivateDocument itself is unchanged, in the anonymous namespace above, with its callers
//  inside this file.)

//----------------------------------------------------------------------------------------
// KCMBringArmedTargetToFront (declared in KCMStoryJump.h)
//----------------------------------------------------------------------------------------
void KCMBringArmedTargetToFront()
{
	// ★ONE PLACE ANSWERS "which document is the reader looking at now" (2026-09-21): every UI
	//   caller that begins a comparison ends here, rather than each one deciding for itself.
	IDataBase* const targetDB = Utils<IKCMCompareFacade>()->GetArmedTargetDB();
	if (targetDB == nil)
		return;	// nothing armed: the Stop half of the toggle, or a start that did not take

	// ⚠**ASKED OF THE FACADE, NOT REMEMBERED BY THE CALLER.** The Target a start settles on is
	//   not always the one the caller had in mind - a chosen Target, the document that happened
	//   to be in front, or the one a Task Start copied - and the armed state is the only place
	//   that knows which it became.
	ActivateDocument(targetDB);
}

//----------------------------------------------------------------------------------------
// KCMBringDocumentToFront (declared in KCMStoryJump.h)
//----------------------------------------------------------------------------------------
void KCMBringDocumentToFront(IDataBase* db)
{
	// ⚠**ASKED BEFORE IT IS USED.** The caller noted this pointer before a long run (the report), and a
	//   document closed meanwhile leaves its address behind for another to reuse ([[uidref-reuse-after-close]]);
	//   the facade answers by comparing it against the open documents, never by dereferencing it.
	if (db == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(db))
		return;
	ActivateDocument(db);
}

//----------------------------------------------------------------------------------------
// KCMStoryJumpToRow (declared in KCMStoryJump.h)
//----------------------------------------------------------------------------------------
bool16 KCMStoryJumpToRow(int32 rowIndex)
{
	// ***** A RESOURCES ROW HAS NOWHERE TO JUMP TO, AND SHOWS ITS VALUES INSTEAD. *****
	//
	// ★THE XML HAS NO PAGES. A definition is not on a page - the export writes page items under
	//   <Spread>, not under <Page>, and a paragraph style is not under either (design section 6).
	//   Which page changed is the Pixel comparison's answer, and the user's call is that these rows
	//   do not pretend to have one. What the reader gets instead is the pair of values, in the
	//   upper pane (KCMResourceValue.cpp).
	//
	// ★★THIS IS THE ONE PLACE THE TWO GESTURES MEET. A click on a row (KCMStoryRowEH::LButtonUp)
	//   and the up / down walk (KCMStoryTreeEH::HandleUpDownKey) both arrive here for a row with no
	//   children, and a Resources row never has children (design 6-1b). Branching here therefore
	//   covers both without asking the mode in either of them.
	//   ⚠**It has to be the FIRST thing in the function.** Everything below reads the row out of
	//     IKCMStoryEditsFacade, whose list is a different list: in the Resources mode it is empty,
	//     so the old code path answered kFalse and did nothing at all - which is why a Resources row
	//     was silently inert rather than wrong, and why this is an addition and not a repair.
	if (KCMListShowsResources())
		return KCMShowSelectedResource(rowIndex, -1);	// -1 = the definition row itself, not one attribute

	IKCMStoryEditsFacade::Row row;
	if (!Utils<IKCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;	// out of range, or the "No edits" placeholder - nowhere to go, silently

	// ***** THE WALK'S POSITION MOVES HERE TOO (user's request: "selecting a Story Edits row should
	// carry Prev/Next along with it, or it feels wrong"). *****
	//
	// ★A ROW WITH CHILDREN IS NOT A STOP, so what this leaves the walk standing on is the ENTRANCE to
	//   its first child: the readout shows that child's number and the next press of Next goes TO it
	//   rather than past it (KCMChangeNav.h explains the rule, which is the one already used for
	//   "1/N" straight after a comparison starts).
	// ★IT IS DONE HERE, BEFORE THE REFUSALS BELOW, on purpose: a story that is not placed in a frame
	//   still exists as a row, and the walk has to be able to step off it. Putting this after the
	//   early returns would leave such a row stuck - Next would keep landing on it.
	// ⚠It does nothing in the Pixel mode - that is decided inside, where the mode is asked once.
	KCMNoteStoryStop(rowIndex, -1);

	// ★The list belongs to the comparison that built it, so the document to move is one of the two
	//   ARMED ones - not whatever happens to be in front. Checked for life rather than trusted: the
	//   list is dropped when a compared document closes, but a click already on its way when that
	//   happened would otherwise arrive here holding a database that is gone.
	//
	// ★★WHICH OF THE TWO IS THE ROW'S OWN (2026-08-21). A REMOVED row's story is not in the target
	//   at all - it is in the older document - and the user's call is that clicking it moves the
	//   SOURCE window alone ("selecting one of those should jump the source alone"). Every other row is a
	//   target row and behaves exactly as before.
	//   ★★★AND "ALONE" NEEDS NO EXTRA FLAG: KCMGotoStoryFrame only brings the companion window
	//     along when the source it finds is NOT the database it was asked to move
	//     (`sourceDB != db`). Handing it the source therefore moves the source and stops. ⚠That is
	//     a property of that function, not an accident of this call - if the companion logic there
	//     is ever rewritten, this promise has to be re-checked.
	//   ⚠row.fStoryUID / fFrameUID / fPageUID all belong to whichever document this picks. Reading
	//     the row against the other one would not fail loudly - a uid can name a DIFFERENT object
	//     over there rather than nothing.
	const bool16 removedRow = ((row.fKinds & kKCMStoryKindRemoved) != 0) ? kTrue : kFalse;
	IDataBase* db = removedRow ? Utils<IKCMCompareFacade>()->GetArmedSourceDB()
							   : Utils<IKCMCompareFacade>()->GetArmedTargetDB();
	if (db == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(db))
	{
		// ⛔**THE ORIGIN'S WORDING WENT ON 2026-09-21.** A Removed row's story existed only in the
		//   origin, which had no window of its own, so this said so rather than sending the reader
		//   off to press Start. The Source is an open document now - Start opens it - so a database
		//   that is not there means exactly what the one remaining sentence says.
		KCMSetStatus("The comparison is no longer running.");
		return kFalse;
	}

	// ★A SOURCE KEPT IN MEMORY HAS NO WINDOW (2026-10-13): a Removed row's story is in the Source alone, and a Task
	//   Start kept in memory is a database clone no window shows - said, rather than "Could not scroll.".
	if (removedRow)
	{
		PMString label;
		if (Utils<IKCMCompareFacade>()->GetExternalSourceLabel(db, label))
		{
			KCMSetStatus("The Source is a Task Start kept in memory - it has no window to show this story in.");
			return kFalse;
		}
	}

	// ★A ROW STANDING FOR A FILE (2026-09-19): an import was handed a file named after a story the
	//   document does not hold. There is no story, so there is nowhere to go; the row's text says
	//   which file, and this says why the page did not move.
	if (row.fStoryUID == kInvalidUID)
	{
		KCMSetStatus("That file names no story in this document - nothing to go to.");
		return kFalse;
	}

	// A story with no frame at all is a real edit - it is in the document and it changed - but there
	// is nowhere on a page to show it. Say so rather than moving to an arbitrary place.
	if (row.fFrameUID == kInvalidUID)
	{
		KCMSetStatus("That story is not placed in a frame.");
		return kFalse;
	}

	// ***** NO SaveRestoreModifiedState HERE, AND THAT WAS MEASURED RATHER THAN ASSUMED. *****
	//
	// ⚠This looks like an omission, and it is worth saying why it is not, because the shape of the
	// mistake it resembles has actually been made in this plug-in: bug recheck B5 (2026-08-17) found
	// that the dirty guard stood in FOUR places on the model side and was missing from a fifth that
	// rasterises a page, and KBS wraps its own jump - the same kSetSpreadCmdBoss, the same scroll -
	// in one (KBSJump.cpp:691, :981). So a reader arriving here has every reason to suspect this line.
	//
	// ★MEASURED 2026-08-18 (bug recheck B-U4), with a pair built so the answer could come out the
	//   other way: both documents saved and clean, both layout windows left on page 1, and the row
	//   clicked was a story on page 3 - a DIFFERENT SPREAD, so KCMEnsureSpreadInView really did
	//   run kSetSpreadCmdBoss through CmdUtils::ProcessCommand. Both windows moved to page 3, the
	//   status line said "Page: 3", and app.documents[i].modified was FALSE for target AND source
	//   afterwards. Start itself, the "Next >" button, Stop and the double click (gone 2026-10-25) were all measured the
	//   same way and are all clean too.
	// ⇒ Scrolling and switching the spread do not dirty a document; the guard below is for the
	//   SELECTION, which is a different operation (see its own note).
	// ⚠WHAT WOULD CHANGE THE ANSWER: anything added to this path that touches the model rather than
	//   the view. Then measure again rather than reasoning from here.
	// ★The Source window is lined up on the row's SOURCE story (2026-10-26 - Row::fSourceStoryUID; another uid for a row
	//   paired by the place of its first frame, the same one for every other).
	if (!KCMGotoStoryFrame(db, row.fFrameUID, row.fPageUID, row.fStoryUID, row.fSourceStoryUID))
	{
		KCMSetStatus("Could not scroll.");	// the same wording as a failed Prev/Next (it is the same event)
		return kFalse;
	}

	// ★★A ROW PAIRED BY PLACE SAYS SO (2026-10-26, design 2-2 #13): the list shows the Target's story number, and nothing
	//   else on the panel tells the reader that the Source side is ANOTHER story - the one whose frame stood where this
	//   story's frame stands now.
	PMString status = PageLabel(db, row.fPageUID);
	if ((row.fKinds & kKCMStoryKindByPosition) != 0 && row.fSourceStoryUID != kInvalidUID)
	{
		status.Append(" - paired by place with the Source's story ");
		status.AppendNumber(static_cast<int32>(row.fSourceStoryUID.Get()));
	}
	status.SetTranslatable(kFalse);
	KCMSetStatus(status);
	return kTrue;
}

//----------------------------------------------------------------------------------------
// KCMStoryJumpToChange (declared in KCMStoryJump.h)
//----------------------------------------------------------------------------------------
bool16 KCMStoryJumpToChange(int32 rowIndex, int32 changeIndex)
{
	// ***** AN ATTRIBUTE ROW IS A "CHANGE ROW" TOO, AND BELONGS TO THE OTHER MODEL. ***** Since
	// 2026-09-09 a definition has children - one per attribute that differs - and they carry the
	// same pair of indices a story's changes do. Everything below reads the STORY model with them,
	// which would be a row of a different list; and there is nowhere to jump to in any case (the
	// XML has no pages). What a click on one does is show THAT ATTRIBUTE's older value in the band -
	// which is the same thing a click on a Story change row does with the older wording, and the
	// reason these rows exist at all: one click, one value, two lines.
	// ⚠It is the FIRST thing here for the same reason as in KCMStoryJumpToRow: both the click and
	//   the arrow walk arrive through this function, so branching here covers both.
	if (KCMListShowsResources())
		return KCMShowSelectedResource(rowIndex, changeIndex);

	IKCMStoryEditsFacade::Row row;
	IKCMStoryEditsFacade::Change change;
	if (!Utils<IKCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;
	if (!Utils<IKCMStoryEditsFacade>()->GetChange(rowIndex, changeIndex, change))
		return kFalse;

	// ★★**A "!" CHILD HAS NO PLACE TO GO TO** (2026-09-19): it names something an import could not
	//   put in, and its positions are zero. The page goes to the STORY (what a click on the row does -
	//   a story the document lacks says so on the status line), and the message area shows the
	//   reason in full, which is the one thing the row's narrow cell may have cut short.
	if (change.fWhat == IKCMStoryEditsFacade::Change::kWhatRefused)
	{
		const bool16 moved = KCMStoryJumpToRow(rowIndex);	// notes the stop as (row, -1) ...
		KCMNoteStoryStop(rowIndex, changeIndex);			// ... and the walk stands on this child
		PMString label("Could not go in (");
		label.SetTranslatable(kFalse);
		label.Append(change.fTextPre);						// the kind word: Word / Table / Place / ...
		label.Append("):");
		PMString none;
		none.SetTranslatable(kFalse);
		KCMSetStatusSegments(label, none, change.fText, none, none, kKCMStoryAttrNone);
		return moved;
	}

	// The walk stands on this change from now on - see the note in KCMStoryJumpToRow for why this
	// is done before the refusals below, and KCMChangeNav.h for what it means on a parent row.
	KCMNoteStoryStop(rowIndex, changeIndex);

	// ★★THE TRACK MODE ASKS THE DOCUMENT FIRST (2026-10-05, design 5-1): a change accepted, rejected or undone since
	//   the read is no longer recorded, and a jump to where it stood would point at words that are not the change.
	IKCMStoryEditsFacade::TrackChange track;
	const bool16 isTrack = Utils<IKCMStoryEditsFacade>()->GetTrackChange(rowIndex, changeIndex, track);
	if (isTrack && !Utils<IKCMStoryEditsFacade>()->TrackChangeStillRecorded(rowIndex, changeIndex))
	{
		KCMSetStatus("This change is no longer recorded - accepted, rejected or undone. Refresh to read again.");
		return kFalse;
	}

	IDataBase* db = Utils<IKCMCompareFacade>()->GetArmedTargetDB();
	if (db == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(db))
		return kFalse;

	const UIDRef storyRef(db, row.fStoryUID);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	TextIndex from = change.fTargetStart;
	TextIndex to = change.fTargetEnd;
	ClampToStory(model, from, to);

	// Asking where the edit is composes, and so does the selection below - the same guard, and the same reason, as
	// SelectRangeIn's.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	// ***** THE FRAME THE EDIT IS IN, not the story's first one. ***** A story threaded across
	// several frames has its first frame nowhere near an edit late in it.
	//
	// ★★★2026-08-22 (bug recheck): THE WALK MOVED TO THE MODEL SIDE, WHICH COMPOSES BEFORE IT
	//   ANSWERS. It was written out here, and it ran before anything had recomposed - so for a story
	//   whose composition was out of date it named a frame from the OLD composition, while
	//   GetStoryPointAt (further down, inside KCMGotoStoryFrame) composed and answered from the
	//   NEW one. The two then disagreed about which SPREAD, and a pasteboard point is
	//   spread-relative: the window does not land slightly off, it lands on another page.
	//   ⚠The sharper form of the same fault: text that was overset before the recompose made this
	//     walk fall back to the story's first frame, while the point succeeded on a spread far away.
	//   ⇒ Both readings now come from one composition, and THE SOURCE SIDE ASKS THE VERY SAME WAY
	//     (KCMChangeNav.cpp) - where it used to be handed the story's first frame and nothing else.
	// ⚠The fallback is the story's first frame (what a story row uses), for a story whose frame list
	//   cannot answer - an unplaced story has none at all.
	// ★A ZERO-WIDTH CHANGE IS A GAP, NOT A CHARACTER (2026-09-24): a deletion seen from here, an
	//   insertion seen from the Source. In front of a table the two have different answers - the
	//   character there is the table's own, composed as the table frame's line, so asking for it
	//   centred the window on the table's corner and the mark drew nothing (the user's 「あ[表]い」→
	//   「あえ[表]い」) - so each side says which it is asking (KCMChangeNav.h on focusIsCaret).
	const bool16 targetCaret = (to <= from);
	const bool16 sourceCaret = (change.fSourceEnd <= change.fSourceStart);
	UID frameUID = targetCaret
		? Utils<IKCMStoryEditsFacade>()->GetCaretFrameAt(db, row.fStoryUID, from)
		: Utils<IKCMStoryEditsFacade>()->GetStoryFrameAt(db, row.fStoryUID, from);

	// ***** AN OVERSET CHANGE GOES TO THE "+" INSTEAD (2026-09-15, the user's request). *****
	//
	// ★**WHAT IT USED TO DO, MEASURED ON THE APPLICATION.** Text that is not composed belongs to
	//   no frame, so the line above answered kInvalidUID and the jump fell through to the story's
	//   FIRST frame: the window moved to page 1, a caret was placed correctly at a position
	//   nothing draws (parentTextFrames = 0), and nothing was said. The reader sees a press that
	//   did nothing - which is how the user found it.
	// ⇒ The overflow indicator is the one thing on the page that IS about this text. The model
	//   works out where it is (KCMOversetPoint.cpp, restored from the retired Find Overset), and
	//   the frame that shows it becomes the frame this jump goes to.
	// ⚠**THE "+" OF A TABLE CELL PUSHED OUT WITH ITS ROW IS THE TABLE'S, NOT THE CELL'S** - the
	//   walk climbs the anchors, which is also what InDesign's own preflight reports.
	// ⚠The caret is still placed at the change itself further down: its position is correct even
	//   when it is invisible, and it is what makes the selection right if the frame is later
	//   made big enough. What changes here is only where the WINDOW goes.
	PBPMPoint oversetPb;
	bool16 wentToOverset = kFalse;
	if (frameUID == kInvalidUID && change.fOverset)
	{
		UID plusFrame = kInvalidUID;
		if (Utils<IKCMStoryEditsFacade>()->GetOversetPoint(db, row.fStoryUID, from, plusFrame, oversetPb)
			&& plusFrame != kInvalidUID)
		{
			frameUID = plusFrame;
			wentToOverset = kTrue;
		}
	}

	if (frameUID == kInvalidUID)
		frameUID = row.fFrameUID;
	if (frameUID == kInvalidUID)
		return kFalse;		// nothing placed - nowhere to go, and the story row says so already

	// ★The page is asked of the frame we are actually going to, not of the story's start: an edit
	//   late in a threaded story is on a different page from the story's beginning, and this is what
	//   the Pages panel follows.
	const UIDRef pageRef = Utils<ITextUtils>()->GetPageUIDRef(UIDRef(db, frameUID));
	const UID pageUID = (pageRef.GetDataBase() != nil) ? pageRef.GetUID() : row.fPageUID;

	// Both windows move here - the target to this frame, the source to the row's Source story (the same story for every
	// row paired by UID; another for one paired by place - 2026-10-26).
	//
	// ★★★AND BOTH LAND ON THE EDIT ITSELF, NOT ON THE TOP OF THE STORY (user's request, 2026-08-22:
	//   "the very start of the changed part should move to the centre of the layout view" - what was arriving
	//   in the middle of the window was the story's first character, which for a long story is
	//   nowhere near what the row is pointing at). The point centred is where the CARET stands in
	//   front of the first changed character (user's words: "where that blinking vertical line appears").
	// ⚠★★THE TWO SIDES GET DIFFERENT NUMBERS. The same edit sits at a different character position
	//   in each version, and the diff has already worked both out - so the older window is told
	//   fSourceStart rather than being handed the target's index (which would name some unrelated
	//   character over there). ⇒ This is also what finally settles the "stage one" caveat in KCMID.h
	//   ⑬: the source window now reaches the corresponding CHARACTER, not just the same story.
	//   ★★AN INSERTION AIMS AT ITS PLACE (user's report: "when it shows +, the jump on the source
	//   side looks wrong"). It used to be excluded here, on the grounds that there is nothing
	//   in the older version to centre - true of the CHARACTERS and false of the SPOT, which is
	//   exactly where the reader wants to look: the gap the new words went into. The range is empty
	//   for one (fSourceEnd == fSourceStart) and centring works off the start, so nothing else has
	//   to change.
	const TextIndex sourceFocus = change.fSourceStart;

	// ★★A TABLE ROW GOES TO THE TOP-LEFT OF THE CELLS THAT CHANGED (2026-09-19 night, the user: "the
	//   jump lands on the top-most, left-most coordinate of the changed cells" - not the table's own
	//   corner). Each changed cell's first character is asked for its point (the same composition the
	//   caret's point comes from - the dirty guard above covers it), and the smallest x and the smallest
	//   y are put together into one point, which is handed to the goto the way the overset "+" is: a
	//   pasteboard point to centre instead of a character.
	PBPMPoint tableCorner;
	bool16 haveCorner = kFalse;
	if (change.fWhat == IKCMStoryEditsFacade::Change::kWhatTable && !wentToOverset)
	{
		for (int32 s = 0; s < change.fMarkSpanCount; ++s)
		{
			TextIndex a = 0;
			TextIndex b = 0;
			PBPMPoint p;
			if (!Utils<IKCMStoryEditsFacade>()->GetChangeMarkSpan(rowIndex, changeIndex, s, a, b))
				continue;
			if (!Utils<IKCMStoryEditsFacade>()->GetStoryPointAt(db, row.fStoryUID, a, p))
				continue;
			if (!haveCorner)
			{
				tableCorner = p;
				haveCorner = kTrue;
			}
			else
			{
				if (p.X() < tableCorner.X()) tableCorner.X(p.X());
				if (p.Y() < tableCorner.Y()) tableCorner.Y(p.Y());
			}
		}
	}

	const bool16 moved = KCMGotoStoryFrame(db, frameUID, pageUID, row.fStoryUID, row.fSourceStoryUID, from, sourceFocus,
										   haveCorner ? &tableCorner : (wentToOverset ? &oversetPb : nil),
										   targetCaret, sourceCaret);

	// ***** AND LIGHT THE CHARACTERS UP FOR A MOMENT. *****
	//
	//   ★★★A MARK, NOT A SELECTION (user's call: "move to those characters and show a marker for a
	//   short while"). Until then this made a text selection, which had three costs a pointer does
	//   not: it threw away whatever the reader had selected, it forced the Type tool on, and it left
	//   the words sitting selected long after the reader had looked at them. The mark says "here" and
	//   then gets out of the way. ⇒ The selection stayed available on the DOUBLE click (KCMStorySelectChange) until
	//   2026-10-24 night, when the click took it back (below); the double click went on 2026-10-25.
	// ★It is drawn ON the characters by the text engine (KCMStoryMarker.cpp), so it needs no
	//   coordinates from here: the story and the character range are the whole of the request.
	//
	//   ★★★AND IT POINTS IN BOTH WINDOWS (user's request: "when it jumps, flash the mark on the
	//   source side too"). Both windows have just been moved to this edit, so a
	//   pointer in only one of them leaves the reader to find it by eye in the other - which is the
	//   very window where "what it used to say" lives.
	// ⚠★★THE TWO SIDES GET DIFFERENT NUMBERS, for the same reason the centring above does: the
	//   same edit sits at a different character position in each version, and the diff has already
	//   worked both out. Handing the target's index to the older document would light some unrelated
	//   characters over there rather than failing.
	// ★AN INSERTION NEEDS NO SPECIAL CASE HERE: its source range is empty, and an empty range is
	//   drawn as the caret standing where the new words went in (KCMStoryMarker::AddFlashRange) -
	//   the same thing the standing marks show for it.
	// ⚠THE OLDER SIDE'S RANGE IS NOT CLAMPED to the story as it stands now, and the target's is
	//   (above). The difference is real: the target's numbers also make a SELECTION (below), which
	//   the suite would refuse if they were stale, while a mark is only ever asked "is
	//   any of this run marked" and a stale range simply never meets a run (KCMStoryPressMarks
	//   records the same reasoning for the standing marks).
	// ★★ONE CALL SINCE 2026-08-23, WHERE THIS USED TO BUILD THE SET ITSELF. The marker moved to the
	//   model plug-in (so that the marks can reach paper and PDF - the UI's export runs in the
	//   background and never draws a kUIPlugIn), and its vocabulary is a nested std::map that must
	//   not cross a DLL edge. ⇒ The two ranges travel as six plain numbers and the model builds the
	//   set. ★What went with it: deciding whether the older window is open, and treating an empty
	//   range as a caret. Both are facts about the comparison rather than about this row.
	// ⚠NO nil TEST HERE, AND ITS ABSENCE IS DELIBERATE - unlike KCMStoryPressMarks.cpp, which
	//   tests every one of its three calls. The difference is who is calling: **this runs from a
	//   click on a row**, so the panel is up, the comparison is armed and kUtilsBoss is certainly
	//   there; that file's Refresh hangs off a model NOTIFICATION, which can arrive while the
	//   application is tearing down. Every other facade call in this file (IKCMCompareFacade,
	//   IKCMStoryEditsFacade) is written the same way for the same reason.
	//
	// ★★★AND SINCE 2026-10-24 NIGHT THE TARGET'S WORDS ARE SELECTED WHERE THEY CAN BE (docs/superpowers/specs/
	//   2026-10-24-kcm-list-like-kfc-design.md 1-1 / 1-2; the author: "in KFC, selecting a row selects the text too,
	//   and that is pleasant to use - KCM too, the Story mode and Word / PDF", "the hidden and locked cases too - select
	//   the way KFC does"). The 2026-08-20 call above is changed the way design section 4 says: its first two costs (the
	//   reader's selection thrown away, the Type tool) are taken as KFC takes them - the selection IS the pointer. KFC's
	//   rules, which are Find/Change's own (KFCJump.cpp, NotSelected): a LOCKED place is not selected and the flash
	//   points at it instead; a HIDDEN one (its layer or itself) is selected with nothing of it showing, and said; one
	//   on a HIDDEN SPREAD is neither shown nor selected, and said; an overset one is selected where its "+" is, and
	//   said; an empty place (a deletion's) gets the caret. The Target is made the active document first - a selection
	//   made in a document behind another is not the one the reader holds (ActivateDocument says how that was measured).
	//   ★The Source window moves and flashes as before (design section 3, Q1).
	//   ★★THE TRACK MODE SELECTS TOO SINCE 2026-10-25 (the author: "in the Track Change mode too, select the words when a
	//     row is selected - remember that"; design Q2's "the Track mode as it is" reversed). The same rules on the same
	//     place, and one thing more: what a record keeps out of the layout - a deletion's words, which InDesign shows in
	//     the Story Editor only, and words under a hidden condition, whose place is where that text comes back
	//     (KCMTrackRead) - cannot be selected as words, so the caret stands at the place and the message area says why
	//     nothing shows there.
	//   ★No flash on a selected place: an inversion over a selection cannot be read (KFC's JMP-29).
	const char* landing = nil;		// what the message area says first - nil for a plain landing
	const int32 reach = Utils<IKCMStoryEditsFacade>()->GetChangePlaceReach(rowIndex, changeIndex);
	bool16 selected = kFalse;
	if ((reach & IKCMStoryEditsFacade::kReachHiddenSpread) != 0)
		landing = "That change is on a hidden spread - it cannot be shown or selected.";
	else if ((reach & IKCMStoryEditsFacade::kReachLocked) != 0)
		landing = "That change is locked - it cannot be selected.";
	else
	{
		if (Utils<IKCMCompareFacade>()->GetActiveDocDB() != db)
			ActivateDocument(db);
		// A refusal is quiet, and the flash points instead: a story editor in front showing another story, a window
		// that could not be had (SelectRangeIn says which).
		selected = SelectRangeIn(db, row.fStoryUID, from, to);
		if (selected && wentToOverset)
			landing = "(Overset Text) The change is selected, past the end of its frames - nothing of it shows on the page.";
		else if (selected && (reach & IKCMStoryEditsFacade::kReachHidden) != 0)
			landing = "The change is selected, on a hidden layer - nothing of it shows.";
		else if (selected && isTrack && track.fHidden)
			landing = "That change is under a hidden condition - the caret stands where its text is hidden; nothing of it shows.";
		else if (selected && isTrack && track.fKind == kKCMTrackDelete)
			landing = "The deleted words show in the Story Editor only - the caret stands where they were.";
	}
	// The Target's flash only where nothing was selected and something can be shown; the Source's always (a nil
	// document is skipped by the marker - KCMStoryMarker's AddFlashRange).
	const bool16 targetFlash = (!selected && (reach & IKCMStoryEditsFacade::kReachHiddenSpread) == 0) ? kTrue : kFalse;
	if (isTrack)
	{
		// ★In the author's colour (2026-10-05); in the Source window only where the change's place in the copy is
		//   known exactly - otherwise its Source range is the story's start, and a flash there would point at nothing.
		Utils<IKCMStoryMarkFacade>()->ShowJumpFlashColoured(targetFlash ? db : nil, row.fStoryUID,
			targetFlash ? from : 0, targetFlash ? to : 0,
			track.fSourceExact ? change.fSourceStart : -1, track.fSourceExact ? change.fSourceEnd : -1,
			track.fHasColour, track.fR, track.fG, track.fB);
	}
	else
		// ★The Source's range under the row's SOURCE story (2026-10-26 - another uid for a row paired by place).
		Utils<IKCMStoryMarkFacade>()->ShowJumpFlash(targetFlash ? db : nil, row.fStoryUID,
													  targetFlash ? from : 0, targetFlash ? to : 0,
													  row.fSourceStoryUID, change.fSourceStart, change.fSourceEnd);

	// ***** AND THE OTHER SIDE OF THE EDIT GOES TO THE PANEL'S MESSAGE AREA. *****
	//
	// ★The row shows the side that CHANGED; this shows the other one, so that "what it used to say"
	//   is readable without leaving the panel (user's request: "make use of the panel's message
	//   area"). For a deletion the row is showing what was removed, so what lands here is the
	//   text that closed up over it - see KCMStoryList.h for why the field is called "other" and
	//   not "old".
	//
	// ★A LABEL ON THE FIRST LINE, THE TEXT FROM THE SECOND (user's call, 2026-08-20, after seeing
	//   the plain version: "first line Old, the old text from the second line"). The message area holds four lines
	//   in a Japanese UI and six in an English one, so the label costs one line and buys the reader
	//   the one thing a bare sentence in this box does not say - which version they are looking at.
	//
	// ★★"Source" / "Target", NOT "Old" / "New" - BECAUSE THE PANEL ALREADY SPEAKS THAT WAY. Two
	//   lines at the top of it name the documents being compared, "Target:" and "Source:", so the
	//   reader has been told which is which before ever reaching this box. A second pair of names
	//   for one pair of documents would be the panel disagreeing with itself
	//   ([[one-question-one-place]] applied to words rather than to code).
	//   ★★AND "TEXT" AFTER IT (user's call: "where it says Source, let us make it Source Text").
	//     Those two lines at the top name FILES; this names the WORDS inside one of
	//     them. Borrowing their word without saying which of the two things is meant made one label
	//     answer for both - "Source Text:" says it is the same document and a different thing.
	//   ⚠"Target:" FOR A DELETION, and that is not a special case bolted on: the row shows the side
	//     that CHANGED, so for a deletion the row holds the words that were REMOVED and what lands
	//     here is the text that closed up over them. Calling that the source would be false, and a
	//     deletion is the row where the reader most needs to know what stands there now.
	//     (fKind: 0 = replace, 1 = insert, 2 = delete - IKCMStoryEditsFacade.h.)
	//
	// ★★AND IT GOES OVER IN THREE PIECES, NOT AS ONE SENTENCE (2026-08-20). The box is drawn by
	//   hand now (KCMStatusTextView.cpp), so it can do here what the ROW already does: draw the
	//   characters that differ at the theme's text colour and fade the words around them. The split
	//   is not made here and could not be - the boundary between context and change is a code point
	//   index into text that has been cut at both ends, and the joined text carries no mark of it.
	//   The model made it (KCMStoryDiffRun's Slice) and it travels on the Change.
	//   ★The label is its own argument rather than the head of the first piece: when the message
	//     does not fit, the CONTEXT gives way from its outer ends, and a label living in the context
	//     would be the first thing cut. It is the one piece that has to survive.
	// ⚠★★WHICH DOCUMENT fOtherText CAME FROM USED TO DEPEND ON THE KIND OF CHANGE (2026-08-22), and
	//   getting that wrong once labelled a removed ruby "Target Text:" over text that had come from
	//   the SOURCE. **That whole question is gone as of 2026-09-01**: every row shows the newer
	//   version, so fOtherText is the older one for every kind of change there is.
	//   ★The lesson outlived the branch, and is why the note stays: a value that can come from
	//   either document must be labelled from what the change IS, never from what it looks like.
	// ★★★ALWAYS THE SOURCE, BECAUSE THE ROW IS ALWAYS THE TARGET (2026-09-01, user's decision).
	//   The row shows the newer version for every kind of change now - including a deletion, which
	//   used to be the exception - so the other side is the older one every time and this label
	//   never has to work out which document it is looking at.
	//   ⚠**THE BRANCH THAT STOOD HERE WAS NOT A STYLE CHOICE**: while deletions showed the older
	//   text in the row, this had to say "Target Text:" for them or it would have labelled the
	//   newer text as the source. It went out with the rule that made it necessary - and the two
	//   MUST go together. Restoring one without the other mislabels every deletion in the list.
	//   (KCMStoryDiffRun's Slice is the other half.)
	// ★★★**A REPLACED ROW ANSWERS A DIFFERENT QUESTION** (the Import mode, 2026-09-15). Its words
	//   ARE the source's now - they are in the row - so showing the source here would show the
	//   same string twice and tell the reader nothing. What is nowhere else on the panel is what
	//   stood there BEFORE, which is precisely what they replaced. ⇒ The label says which of the
	//   two this is, for the same reason it says "Source Text" the rest of the time: a box of
	//   words that does not say which version it is showing is a box that can be read backwards.
	// ⚠**THE WIDTH IS NOT SETTLED.** This is a narrow box and the phrase is long; it may have to
	//   come down to "Before:". That is a thing to look at rather than to argue about, and it is
	//   one line here.
	// ★★★**AND THE HEADING NAMES THE KIND OF CHANGE** (2026-09-20, the user: "put the ID part after
	//   the colon - Source: Text, Source: Ruby"). The word is the ID column's own (KCMChangeIdLabel),
	//   so the heading and the row cannot disagree about what a change is called.
	//   ⚠**"Source Text:" BECAME "Source:" FOR THIS** - the reader's own point: with the kind
	//    appended, the old wording read "Source Text: Text". "Before the replacement:" came down to
	//    "Before:" with it, which is also what the note above this block had been asking for on
	//    width grounds.
	//   ⚠No "OV " prefix here: this label appends its own " (overset)" below, and saying it twice
	//    in one line is worse than not saying it at all.
	PMString label;
	label.SetTranslatable(kFalse);
	label.Append("Source:");	// (⛔"Before:" for a change taken in, until the restore went 2026-09-21)
	const PMString kindWord = KCMChangeIdLabel(change.fAttrKind, change.fPlace,
											   change.fWholeParagraph, change.fWhat, kFalse);
	if (!kindWord.IsEmpty())
	{
		label.Append(" ");
		label.Append(kindWord);
	}

	// ★AN OBJECT PLACED IN THE TEXT (2026-10-27 - KCMStoryKinds.h, kKCMStoryAttrObject): what it was and what it is now.
	//   Its anchor is the same character on both sides, so the text below says nothing; the value's part before the tab
	//   is the reader's word for it (KCMObjectPrint.h - "QR: <its content>", "SVG / MathML", "Group"...).
	if (change.fAttrKind == kKCMStoryAttrObject)
	{
		const PMString before = KCMObjectWord(change.fOtherRuby);
		const PMString after = KCMObjectWord(change.fRuby);
		label.Append(": ");
		if (before == after)
		{
			label.Append(after);
			label.Append(" - its content changed");
		}
		else
		{
			const char16_t kArrow[] = u" → ";	// " → " as UTF-16 (a narrow literal goes through the code page)
			PMString arrow;
			arrow.SetXString(reinterpret_cast<const UTF16TextChar*>(kArrow), 3);
			arrow.SetTranslatable(kFalse);
			label.Append(before);
			label.Append(arrow);
			label.Append(after);
		}
		label.SetTranslatable(kFalse);
	}

	// ★AND A WORD WHEN THE WINDOW WENT TO THE "+" RATHER THAN TO THE TEXT. The row's ID column
	//   already says OV, but the label is what the reader is looking at the instant after they
	//   press, and "the page did not move to my words" deserves an answer in that moment.
	if (wentToOverset)
		label.Append(" (overset)");

	// ★THE TRACK MODE'S HEADING (2026-10-05, design 4-3): who, when, and what - then the words it took away.
	if (isTrack)
	{
		// ★" · " as UTF-16 (the "≠" road of this list): a narrow literal with a non-ASCII byte goes through the
		//   code page and draws as something else (memory cpp-japanese-needs-bom).
		const char16_t kDot[] = u" · ";
		PMString dot;
		dot.SetXString(reinterpret_cast<const UTF16TextChar*>(kDot), 3);
		dot.SetTranslatable(kFalse);
		IKCMStoryEditsFacade::TrackAuthor au;
		label = Utils<IKCMStoryEditsFacade>()->GetTrackAuthor(track.fAuthor, au) ? KCMTrackAuthorName(au.fName) : PMString();
		label.Append(dot);
		label.Append(KCMTrackTimeLabel(track.fTime));
		label.Append(dot);
		label.Append(KCMTrackKindWord(track.fKind));
		label.Append(":");
		if (track.fHidden)
			label.Append(" (hidden condition)");
		if (wentToOverset)
			label.Append(" (overset)");
		label.SetTranslatable(kFalse);
	}

	// ★★THE OTHER SIDE'S READING GOES WITH IT (2026-08-22). The list shows the NEWER version, so a
	//   reading that was REMOVED can be seen nowhere else - and the row's own upper line is left
	//   empty for exactly those (user's call). This box is where it is answered.
	// ⚠ASKED OF THE STRING'S MEANING, NOT OF THE STRING. fOtherRuby is only filled for an attribute
	//   change (IKCMStoryEditsFacade.h says so at the field); a text change leaves it
	//   default-constructed and reading it anyway would be relying on that rather than on the
	//   contract.
	// ⚠★★★AND THE QUESTION IS fAttrKind, NEVER fWhat (corrected 2026-08-23, bug recheck). fWhat says
	//   "not the words"; it does NOT say the value is something a reader reads. Kenten fills these
	//   very fields with a KIND ("BlackCircle"), so asking fWhat drew that name over the older text
	//   as though it were a reading - the fault the feature was withdrawn for.
	// ★★AND SINCE 2026-09-01 THE TWO QUESTIONS GIVE DIFFERENT ANSWERS AGAIN: kenten is reported
	//   once more, so a change reaching here really can be one whose value is a name rather than a
	//   reading. **That is why the KIND travels beside it** (the last argument below): the box
	//   draws a name as the MARK it names, exactly as the row does - it never writes it out.
	// ★★KENTEN COMES THROUGH HERE TOO (2026-09-01, user: "can the kenten be shown above the source
	//   text, the way ruby is?"). This box shows the OTHER version, so **it is the only place a
	//   mark that was REMOVED can be seen at all** - the row shows the newer version, where there
	//   is nothing left to draw. Leaving kenten out of this condition made exactly that case
	//   invisible, and it was invisible in a way that looked deliberate.
	// ★★AND A NOTE'S NUMBER TOO, SINCE 2026-09-16 - the question is now the one the row asks
	//   (KCMAttrKindHasMarkLine): every kind drawn on two lines there is drawn on two lines here, with
	//   a bar on the upper line on the side that has none. Asking a list of two kinds here while the
	//   row asked four is how a removed footnote came to show its number nowhere.
	PMString otherRuby;
	if (KCMAttrKindHasMarkLine(change.fAttrKind))
	{
		otherRuby = change.fOtherRuby;
		otherRuby.SetTranslatable(kFalse);
	}

	// The other side of this edit, in the three pieces the box draws in two colours.
	// (⛔A row the reader had taken in showed its BEFORE side here instead - the side that was no
	//  longer anywhere else - until the restore went on 2026-09-21.)
	const PMString& otherPre  = change.fOtherTextPre;
	const PMString& otherMid  = change.fOtherText;
	const PMString& otherPost = change.fOtherTextPost;

	// ★A WARICHU / TATE-CHU-YOKO carries its other side's LINES too (2026-09-16), cut by the model;
	//   the box draws them in layers. fCount is 0 for every other kind, and then nothing changes.
	// ★A CHANGE THE READER TOOK BACK SAYS SO FIRST (2026-09-24, stage 2 C): the "=" row's message area opens with
	//   what stands there now and what the row's menu offers, then shows the Source side as any row does.
	PMString shownLabel(label);
	if (change.fReplaced)
	{
		PMString taken;
		taken.SetTranslatable(kFalse);
		// ★APPLY'S "=" READS THE OTHER WAY ROUND (2026-10-24 evening, design 11-4): against an import's copy the files'
		//   words went IN here, and the way back is the row's Restore from Copy or Ctrl+Z - the take-back's sentence
		//   below ("Redo puts Word's back") said the opposite of what had happened.
		PMString whyNot;
		const bool16 copyKept = Utils<IKCMStoryEditsFacade>()->GetChangeCopyState(rowIndex, changeIndex, whyNot);
		if (Utils<IKCMStoryEditsFacade>()->IsImportSource())
		{
			taken.Append("Applied - the imported words stand here now; ");
			if (copyKept)
				taken.Append("right-click: Restore from Copy, or Ctrl+Z. ");
			else
			{
				taken.Append("Ctrl+Z takes it back (no copy was kept: ");
				taken.Append(whyNot);
				taken.Append("). ");
			}
		}
		// ★THE STORY MODE'S "=" (2026-10-24 night, design 1-8): Restore from Source put the Source's words here, and its
		//   way back is the same as Apply's. (The old sentence - "Redo puts Word's back" - was the 2026-09 import's.)
		else if (copyKept)
			taken.Append("Restored - the Source's words stand here now; right-click: Restore from Copy, or Ctrl+Z. ");
		else
			taken.Append("Restored - the Source's words stand here now; Ctrl+Z takes it back. ");
		taken.SetTranslatable(kFalse);
		taken.Append(label);
		shownLabel = taken;
	}
	// ★HOW THE LANDING LEFT THE TARGET COMES FIRST (2026-10-24 night - KFC's sentences, word for word but "change" for
	//   "match"): the reader is looking here the instant after the press, and a press that selected nothing, or selected
	//   what cannot be seen, deserves its answer then.
	if (landing != nil)
	{
		PMString first(landing);
		first.SetTranslatable(kFalse);
		first.Append(" ");
		first.Append(shownLabel);
		shownLabel = first;
	}
	KCMSetStatusSegments(shownLabel, otherPre, otherMid, otherPost,
						   otherRuby, change.fAttrKind, change.fOtherLayers);

	return moved;
}

// (⛔KCMStorySelectChange and KCMStorySelectWholeStory stood here until 2026-10-25 - the double click's selection:
//  a change's words on both sides, or the whole story, Deleted rows in the Source with it brought to front. They went
//  with the double click (KCMStoryRowEH.cpp; KCMStoryJump.h says why); SelectRangeIn above is what selects now.)

// End, KCMStoryJump.cpp.
