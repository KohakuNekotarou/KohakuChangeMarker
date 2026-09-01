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
#include "ITextSelectionSuite.h"	// SetTextSelection - the double click's whole point
#include "ITextUtils.h"				// GetPageUIDRef - which page that frame is on
#include "ITool.h"					// IsTextTool - is a text tool already active?
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
#include "TextEditorID.h"			// kIBeamToolBoss - the Type tool the double click switches to
#include "UIDRef.h"
#include "Utils.h"
#include "WritingModeID2.h"			// IID_ITEMPTEXTSELECTION_SUITE - the second way to ask for the
									//  text selection suite (see SelectWholeStory). Only the IID is
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

namespace
{

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
	//   label (KCMStopLabel) and the Page column of the export (PageDisplay in KCMChangedPagesTSV).
	//   **Do not spell one state three ways in three places** ([[one-question-one-place]]).
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

	★ONLY THE DELETED ROW DOES THIS (user's call, 2026-08-21 - and it is a REVERSAL of the same
	day's earlier "windows do not move": "for a deletion, selecting on the source side should make
	the source document active"). Its story exists in the source alone, so leaving the target in front
	would put the reader's attention on the one document that has nothing to show.
	⚠Every other row leaves the front document alone: it has something to show on both sides, and
	moving the front document under a reader who did not ask for it is a bigger intervention than
	the selection itself.

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
	@param to a NEGATIVE value means "to the end of the story", which is what the whole-story
		double click asks for. ⚠Nothing the diff hands over is ever negative, so that rule only
		fires for the caller that asks for it on purpose.
*/
void ClampToStory(ITextModel* model, TextIndex& from, TextIndex& to)
{
	const TextIndex total = model->TotalLength();
	if (to < 0) to = total;
	if (from < 0) from = 0;
	if (to > total) to = total;
	if (from > total) from = total;
	if (to < from) to = from;
}

/** Select from..to of one story in ONE document, with the Type tool on.

	Everything the two double clicks have in common lives here, so that "how KCM makes a text
	selection" is written once and the callers are left saying only WHERE.

	@param to the end of the range, or a NEGATIVE value for "to the end of the story" - what the
		whole-story double click wants. The story has to be opened here to clamp the range anyway,
		so its length is known here and the caller is not made to open it a second time to say so.
	@return kFalse when this document has no such story, no window, or the suite refused.
*/
bool16 SelectRangeIn(IDataBase* db, UID storyUID, TextIndex from, TextIndex to)
{
	if (db == nil || storyUID == kInvalidUID)
		return kFalse;

	const UIDRef storyRef(db, storyUID);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;	// no such story here. Normal on the source side: an added story never
						//  existed over there, and the two versions have to be versions of each
						//  other for a uid to mean the same story at all (KCMStoryStamp.h, "WHY TWO VERSIONS CAN BE MATCHED AT ALL")

	ClampToStory(model, from, to);		// "to" may be -1 here: the story row asks for the whole story

	// ★Making a selection recomposes, and this plug-in may only have the document open in order to
	//   look at it. IDataBase.h:389-412 restores the flag the document came in with rather than
	//   forcing it clean - the same guard KBS puts round the identical operation.
	//   ★ONE GUARD, ON THE DOCUMENT BEING SELECTED INTO - and now that two documents can be
	//     selected into for one click, each of them gets its own by being its own call.
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
	// other tool is active is not text the user can act on, which is the whole of what a double click
	// here is asking for. The tool goes on before the selection is made, as in the sample.
	// ! This takes the KCM tool off, if it was on. Deliberate (user's call, 2026-08-10), and
	//   written down in How to Use so that it is not a surprise.
	// ★The nil test on the active tool is ours: the sample does not test it at all - it calls
	//   IsTextTool() straight off whatever QueryActiveTool answered (GTTxtEdtUtils.cpp:118-119).
	//   The ASSERT a few lines below it is on the I-beam tool queried next, not on this one.
	// ★The tool is the APPLICATION's, not a document's, so the second call of a two-document click
	//   finds it already on and does nothing. Asking again costs one Query and keeps this function
	//   true on its own.
	InterfacePtr<ITool> activeTool(Utils<IToolBoxUtils>()->QueryActiveTool());
	if (activeTool == nil || !activeTool->IsTextTool())
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
	// ⚠STILL NOT POSSIBLE, and this is the suite's own rule rather than something to work around
	//   here: a row whose story the front story editor is NOT showing. That window's selection
	//   belongs to its own story, so SetTextSelection for any other one is refused - measured on
	//   the two rows either side of it. The jump on the first click still reports the page, so the
	//   click is not silent; only the selecting half of it declines.
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
	//   kDontScrollSelection: the first click of the double click already centred the frame with
	//   IPanorama::ScrollContentLocationToFrameCenter. kScrollIntoView would only promise the
	//   selection is somewhere on screen, undoing the better answer already given. (The official
	//   sample asks for kScrollIntoView because nothing has scrolled on its behalf.)
	const RangeData range = (to > from)
							? RangeData(from, to)
							: RangeData(from, from, RangeData::kLeanForward);

	return textSelectionSuite->SetTextSelection(storyRef, range, Selection::kDontScrollSelection, nil);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMStoryJumpToRow (declared in KCMStoryJump.h)
//----------------------------------------------------------------------------------------
bool16 KCMStoryJumpToRow(int32 rowIndex)
{
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
		KCMSetStatus("The comparison is no longer running.");
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
	//   afterwards. Start itself, Next Change, Stop and the double click below were all measured the
	//   same way and are all clean too.
	// ⇒ Scrolling and switching the spread do not dirty a document; the guard below is for the
	//   SELECTION, which is a different operation (see its own note).
	// ⚠WHAT WOULD CHANGE THE ANSWER: anything added to this path that touches the model rather than
	//   the view. Then measure again rather than reasoning from here.
	if (!KCMGotoStoryFrame(db, row.fFrameUID, row.fPageUID, row.fStoryUID))
	{
		KCMSetStatus("Could not scroll.");	// the same wording as a failed Prev/Next (it is the same event)
		return kFalse;
	}

	KCMSetStatus(PageLabel(db, row.fPageUID));
	return kTrue;
}

//----------------------------------------------------------------------------------------
// KCMStoryJumpToChange (declared in KCMStoryJump.h)
//----------------------------------------------------------------------------------------
bool16 KCMStoryJumpToChange(int32 rowIndex, int32 changeIndex)
{
	IKCMStoryEditsFacade::Row row;
	IKCMStoryEditsFacade::Change change;
	if (!Utils<IKCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;
	if (!Utils<IKCMStoryEditsFacade>()->GetChange(rowIndex, changeIndex, change))
		return kFalse;

	// The walk stands on this change from now on - see the note in KCMStoryJumpToRow for why this
	// is done before the refusals below, and KCMChangeNav.h for what it means on a parent row.
	KCMNoteStoryStop(rowIndex, changeIndex);

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

	// Making a selection recomposes - the same guard, and the same reason, as the double click's.
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
	UID frameUID = Utils<IKCMStoryEditsFacade>()->GetStoryFrameAt(db, row.fStoryUID, from);
	if (frameUID == kInvalidUID)
		frameUID = row.fFrameUID;
	if (frameUID == kInvalidUID)
		return kFalse;		// nothing placed - nowhere to go, and the story row says so already

	// ★The page is asked of the frame we are actually going to, not of the story's start: an edit
	//   late in a threaded story is on a different page from the story's beginning, and this is what
	//   the Pages panel follows.
	const UIDRef pageRef = Utils<ITextUtils>()->GetPageUIDRef(UIDRef(db, frameUID));
	const UID pageUID = (pageRef.GetDataBase() != nil) ? pageRef.GetUID() : row.fPageUID;

	// Both windows move here - the target to this frame, the source to the same story.
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
	const bool16 moved = KCMGotoStoryFrame(db, frameUID, pageUID, row.fStoryUID, from, sourceFocus);

	// ***** AND LIGHT THE CHARACTERS UP FOR A MOMENT. *****
	//
	//   ★★★A MARK, NOT A SELECTION (user's call: "move to those characters and show a marker for a
	//   short while"). Until then this made a text selection, which had three costs a pointer does
	//   not: it threw away whatever the reader had selected, it forced the Type tool on, and it left
	//   the words sitting selected long after the reader had looked at them. The mark says "here" and
	//   then gets out of the way. ⇒ The selection is still available - it is what a DOUBLE click does
	//   now (KCMStorySelectChange below).
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
	//   (above). The difference is real: the target's numbers also make a SELECTION on the double
	//   click, which the suite would refuse if they were stale, while a mark is only ever asked "is
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
	Utils<IKCMStoryMarkFacade>()->ShowJumpFlash(db, row.fStoryUID, from, to,
												  change.fSourceStart, change.fSourceEnd);

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
	//   index into text that has been cut at both ends, and PMString counts UTF-16. The model made
	//   it (KCMStoryDiffRun's Slice) and it travels on the Change.
	//   ★The label is its own argument rather than the head of the first piece: when the message
	//     does not fit, the CONTEXT gives way from its outer ends, and a label living in the context
	//     would be the first thing cut. It is the one piece that has to survive.
	// ⚠★★★WHICH DOCUMENT fOtherText CAME FROM IS NOT ALWAYS INFERABLE FROM fKind (2026-08-22).
	//   For a TEXT change it is: the row shows the side that changed, so a deletion (kind 2) shows
	//   the older side and fOtherText is therefore the newer one.
	//   For a RUBY change it is NOT: the characters exist in both versions, so KCMStoryDiffRun's
	//   AddRubyChange always puts the target in fText and the source in fOtherText, with no
	//   rowShowsOldSide branch to make. Reading fKind alone labelled a removed ruby "Target Text:"
	//   over text that had come from the SOURCE.
	//   ⇒ Ask what sort of change it is FIRST. (Found by an independent review of this range, after
	//     I had read the same diff and called it clean - the fault was reading the new code without
	//     counting who already reads the values it sets.)
	const bool16 otherIsTarget = (change.fWhat == IKCMStoryEditsFacade::Change::kWhatText
								  && change.fKind == 2) ? kTrue : kFalse;
	PMString label;
	label.SetTranslatable(kFalse);
	label.Append(otherIsTarget ? "Target Text:" : "Source Text:");

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
	//   once more, so a change reaching here really can be one whose value is a name. **The upper
	//   line is left empty for it deliberately** - the kind is in the label above, drawn as a mark
	//   in the row itself, and writing it here as well would put it back exactly where it did not
	//   belong. The stand-in this comment warned about is gone: the branch below is now load-bearing
	//   rather than merely correct.
	PMString otherRuby;
	if (change.fAttrKind == static_cast<int32>(kKCMStoryAttrRuby))
	{
		otherRuby = change.fOtherRuby;
		otherRuby.SetTranslatable(kFalse);
	}

	KCMSetStatusSegments(label, change.fOtherTextPre, change.fOtherText, change.fOtherTextPost,
						   otherRuby, change.fAttrKind);

	return moved;
}

bool16 KCMStorySelectChange(int32 rowIndex, int32 changeIndex)
{
	IKCMStoryEditsFacade::Row row;
	IKCMStoryEditsFacade::Change change;
	if (!Utils<IKCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;
	if (!Utils<IKCMStoryEditsFacade>()->GetChange(rowIndex, changeIndex, change))
		return kFalse;

	// ★A CHILD ROW IS ALWAYS A TARGET ROW. Only a story that exists in both versions is diffed, so
	//   an added story and a removed one have no children at all (KCMStoryStamp.h's Unpaired kinds,
	//   and IKCMStoryEditsFacade::GetChangeCount says the same). ⇒ No Removed test here; if that
	//   ever changes, this is one of the places that has to be told.
	IDataBase* targetDB = Utils<IKCMCompareFacade>()->GetArmedTargetDB();
	if (targetDB == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(targetDB))
		return kFalse;

	// ★THE MARK COMES DOWN. The single click that opened this double click put one up; leaving it
	//   there would put an inversion on top of the selection's own inversion, and the text under
	//   both is unreadable (KBS records exactly this in KBSJump.cpp).
	// ⚠★★AND THEN WHATEVER STANDS ON ITS OWN GOES BACK UP (2026-08-22 bug recheck A2). Clear() takes
	//   down the ONE adornment both kinds of mark share, so before this it also wiped the marks the
	//   "Always Show Marks on Target / Source" toggles were holding there - and nothing put them back:
	//   the toggle stayed on, the screen stayed bare, and only a fresh comparison or a press of the
	//   tool would bring them round again. A reader who turned a toggle on precisely so as not to
	//   have to hold the tool would never see them return.
	//   ★The refresh is idempotent and decides for itself, so this says "something changed", not
	//     "put the marks back" - which is why it is right even when no toggle is on (it then leaves
	//     the screen clear) and in the Pixel mode (it does nothing at all).
	//   ★THE INVERSION-ON-INVERSION PROBLEM ABOVE IS NOT REINTRODUCED: a standing mark being hard to
	//     read under a selection is the reader's own choice of two things at once, and it is a
	//     choice they can undo from the flyout. The jump's flash is not - it appears unasked, one
	//     click before this one.
	// ★★★AND THE REFRESH THAT PUT THEM BACK IS GONE (2026-08-23), because there is nothing left to
	//   put back: this now takes down the FLASH and says so, where it used to take down the one
	//   adornment both kinds shared and then ask for a full refresh to repair the standing marks it
	//   had just wiped. ⚠The repair was itself a fix, added after the bug recheck of 2026-08-22
	//   found the toggles going bare (A2) - a fix for damage this line was doing to itself. Naming
	//   which of the two is coming down removes both (KCMStoryMarker.h).
	Utils<IKCMStoryMarkFacade>()->ClearJumpFlash();

	// ***** AND THE SAME EDIT IS SELECTED ON THE OLDER SIDE TOO (user's call, 2026-08-21). *****
	//
	// ★The row names ONE edit, and that edit has two ends - what it says now and what it said
	//   before. Selecting only the new one leaves the reader to find the old words by eye in a
	//   window that is already pointed at the right story.
	// ★The older side's range is carried on the Change (fSourceStart / fSourceEnd) - the diff
	//   worked it out and there is nothing to recompute here.
	// ★★AN INSERTION SELECTS ITS PLACE - AS A CARET (2026-08-22). This used to be skipped for one,
	//   because fSourceStart/fSourceEnd were said to be meaningless there. They are not: the range
	//   is EMPTY, which puts the insertion point exactly where the new words went in. That is the
	//   same thing the target side already does for a DELETION, on the very next line - the two are
	//   mirror images, and one of them was written years before the other was noticed.
	// ★THE OLDER SIDE GOES FIRST, so that the target's selection is the last one made. Nothing
	//   here moves the active context, so the order does not decide which window the reader is
	//   in - but if one of the two ever fails, the one left standing should be the target's.
	// ★A refusal on that side is SILENT. It is normal: the source may have no window open, and the
	//   row has just been reported on by the single click. The return value is the target's.
	IDataBase* sourceDB = Utils<IKCMCompareFacade>()->GetArmedSourceDB();
	if (sourceDB != nil && Utils<IKCMCompareFacade>()->IsDocDBOpen(sourceDB))
		SelectRangeIn(sourceDB, row.fStoryUID, change.fSourceStart, change.fSourceEnd);

	return SelectRangeIn(targetDB, row.fStoryUID, change.fTargetStart, change.fTargetEnd);
}

bool16 KCMStorySelectWholeStory(int32 rowIndex)
{
	IKCMStoryEditsFacade::Row row;
	if (!Utils<IKCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;

	// Both of these have just been reported by the single click that preceded this one - see the
	// header for why the second one says nothing.
	//
	// ***** WHICH DOCUMENTS GET SELECTED IS DECIDED BY THE ROW (user's calls, 2026-08-21). *****
	//
	//   | the row                        | selected                                    |
	//   |--------------------------------|---------------------------------------------|
	//   | a normal changed row (in both) | the target AND the source                   |
	//   | Added   (target only)          | the target                                  |
	//   | Deleted (source only)          | the source, and the source is brought to front |
	//
	// ★The first line is the new one ("select both"). The row is a report about ONE story that exists
	//   in two versions, and the reader who double clicks it is asking for that story - not for the
	//   half of it that happens to be in the newer file. Both windows are already pointed at it by
	//   the single click; this makes both of them usable.
	// ★The database each half is read out of is picked exactly as the single click picks it - the
	//   reasoning is written out at KCMStoryJumpToRow and must not drift apart from this, since
	//   selecting in one document after scrolling the other would be two windows disagreeing.
	const bool16 removedRow = ((row.fKinds & kKCMStoryKindRemoved) != 0) ? kTrue : kFalse;
	const bool16 addedRow = ((row.fKinds & kKCMStoryKindAdded) != 0) ? kTrue : kFalse;

	IDataBase* targetDB = Utils<IKCMCompareFacade>()->GetArmedTargetDB();
	IDataBase* sourceDB = Utils<IKCMCompareFacade>()->GetArmedSourceDB();

	// ⚠fFrameUID belongs to the row's OWN document (IKCMStoryEditsFacade.h), so this one test
	//   covers whichever side the row is about: a story in no frame at all cannot be shown, and the
	//   single click has already said so.
	if (row.fFrameUID == kInvalidUID)
		return kFalse;

	// ***** A DELETED ROW: THE SOURCE ALONE, AND THE SOURCE IN FRONT. *****
	// The story is not in the target at all. Selecting it needs the source's selection manager, and
	// - this is the part measured the hard way - a selection made in a document that is not the
	// active one is not the selection the reader is holding, so the document is activated first
	// (see ActivateDocument for why this row and no other).
	if (removedRow)
	{
		if (sourceDB == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(sourceDB))
			return kFalse;

		ActivateDocument(sourceDB);
		return SelectRangeIn(sourceDB, row.fStoryUID, 0, -1);
	}

	if (targetDB == nil || !Utils<IKCMCompareFacade>()->IsDocDBOpen(targetDB))
		return kFalse;

	// ***** A ROW THAT EXISTS IN BOTH: THE OLDER SIDE TOO. *****
	// ★Added is excluded because there is nothing over there to select - the story is new. It is
	//   asked as its own question rather than left to SelectRangeIn's "no such story" refusal: that
	//   refusal is a fallback for a pair of documents that are not versions of each other, and a
	//   fallback should not be a plug-in's way of expressing a decision it has already made.
	// ★THE OLDER SIDE GOES FIRST so the target's selection is the last one made, and its refusals
	//   are SILENT: the source may have no window open, and it is not what the row is about. The
	//   return value is the target's.
	// ⚠The source's story can be a different LENGTH - it is the older wording. SelectRangeIn asks
	//   that document for its own TotalLength, which is the whole point of "-1" being resolved
	//   there rather than here.
	if (!addedRow && sourceDB != nil && Utils<IKCMCompareFacade>()->IsDocDBOpen(sourceDB))
		SelectRangeIn(sourceDB, row.fStoryUID, 0, -1);

	return SelectRangeIn(targetDB, row.fStoryUID, 0, -1);
}

// End, KCMStoryJump.cpp.
