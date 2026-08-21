//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryJump.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "IPageList.h"				// GetPageString - the page number the status line names
#include "ISelectionManager.h"		// DeselectAll / SelectionExists - clearing before selecting
#include "ISelectionUtils.h"		// GetActiveSelection - the front document's selection manager
#include "IFrameList.h"				// QueryFrameContaining - which frame an edit falls in (2026-08-20)
#include "IHierarchy.h"				// GetParentUID - a text column's frame is its parent
#include "ITextFrameColumn.h"
#include "ITextModel.h"
#include "ITextSelectionSuite.h"	// SetTextSelection - the double click's whole point
#include "ITextUtils.h"				// GetPageUIDRef - which page that frame is on
#include "ITool.h"					// IsTextTool - is a text tool already active?
#include "IToolBoxUtils.h"			// QueryActiveTool / QueryTool / SetActiveTool

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
#include "KESCMChangeNav.h"	// KESCMGotoStoryFrame
#include "IKESCMCompareFacade.h"	// arm 状態(2026-08-13・分割 第1段 Task 11 で Facade 経由へ)
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "KESCMStoryJump.h"
#include "KESCMStoryMarker.h"	// the flash over the characters a change row goes to (2026-08-20)
#include "IKESCMStoryEditsFacade.h"	// the row a click landed on (Facade since 2026-08-13, Task 14)
#include "IKESCMMarkData.h"	// IsPageOnHiddenSpread - a row on a hidden page is labelled, not jumped to (2026-08-18)

namespace
{

/** Where the status line says the jump landed: "Page: 3", or the pasteboard when there is no page.

	★The page number comes from IPageList::GetPageString with the same seven arguments the
	navigation's own label uses (KESCMChangeNav.cpp's KESCMStopLabel) - section prefixes and all, so
	that the two places in this panel that name a page never disagree about how a page is spelled.

	★★★2026-08-18 (bug recheck B10, second pass): the seventh argument,
	bIncludePagesOfHiddenSpread, went from kFalse to kTrue. InDesign carries TWO page numbers
	(measured on the machine that day): the Pages panel / page-number field / DOM page.name /
	GetPageString(...,kTrue) all COUNT the pages of hidden spreads, while the folio actually
	composed onto the page - and GetPageString(...,kFalse) - SKIP them. A label that tells a
	person which page to go and look at has to be spelled the way the Pages panel spells it,
	because that is where they will look. KESCMChangeNav's label and the TSV export were changed
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
		label.Append("?");	// 番号が取れないページ(通常は起きない。KESCMStopLabel と同じ受け皿)

	// ★★2026-08-18(不具合再検査 B10 の2周目): 隠れているスプレッドのページなら "(Hide)" を添える
	//   ---- Prev/Next のラベル(KESCMStopLabel)・書き出しの Page 列(KESCMChangedPagesTSV の PageDisplay)と
	//   同じ綴り。**同じ状態を3か所で3通りに綴らない**([[one-question-one-place]])。
	//   ⚠この印は KESCMGotoStoryFrame が「レイアウトを動かさない」と決めた理由そのものなので、
	//     片方だけ入れると「動かないのに理由が出ない」行ができる。
	if (db != nil && Utils<IKESCMMarkData>()->IsPageOnHiddenSpread(db, pageUID))
		label.Append(" (Hide)");
	return label;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KESCMStoryJumpToRow(KESCMStoryJump.h で宣言)
//----------------------------------------------------------------------------------------
bool16 KESCMStoryJumpToRow(int32 rowIndex)
{
	IKESCMStoryEditsFacade::Row row;
	if (!Utils<IKESCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;	// out of range, or the "No edits" placeholder - nowhere to go, silently

	// ★The list belongs to the comparison that built it, so the document to move is one of the two
	//   ARMED ones - not whatever happens to be in front. Checked for life rather than trusted: the
	//   list is dropped when a compared document closes, but a click already on its way when that
	//   happened would otherwise arrive here holding a database that is gone.
	//
	// ★★WHICH OF THE TWO IS THE ROW'S OWN (2026-08-21). A REMOVED row's story is not in the target
	//   at all - it is in the older document - and the user's call is that clicking it moves the
	//   SOURCE window alone ("それを、選択したらソースの方だけジャンプ"). Every other row is a
	//   target row and behaves exactly as before.
	//   ★★★AND "ALONE" NEEDS NO EXTRA FLAG: KESCMGotoStoryFrame only brings the companion window
	//     along when the source it finds is NOT the database it was asked to move
	//     (`sourceDB != db`). Handing it the source therefore moves the source and stops. ⚠That is
	//     a property of that function, not an accident of this call - if the companion logic there
	//     is ever rewritten, this promise has to be re-checked.
	//   ⚠row.fStoryUID / fFrameUID / fPageUID all belong to whichever document this picks. Reading
	//     the row against the other one would not fail loudly - a uid can name a DIFFERENT object
	//     over there rather than nothing.
	const bool16 removedRow = ((row.fKinds & kKESCMStoryKindRemoved) != 0) ? kTrue : kFalse;
	IDataBase* db = removedRow ? Utils<IKESCMCompareFacade>()->GetArmedSourceDB()
							   : Utils<IKESCMCompareFacade>()->GetArmedTargetDB();
	if (db == nil || !Utils<IKESCMCompareFacade>()->IsDocDBOpen(db))
	{
		PMString s("The comparison is no longer running.");
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return kFalse;
	}

	// A story with no frame at all is a real edit - it is in the document and it changed - but there
	// is nowhere on a page to show it. Say so rather than moving to an arbitrary place.
	if (row.fFrameUID == kInvalidUID)
	{
		PMString s("That story is not placed in a frame.");
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
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
	//   clicked was a story on page 3 - a DIFFERENT SPREAD, so KESCMEnsureSpreadInView really did
	//   run kSetSpreadCmdBoss through CmdUtils::ProcessCommand. Both windows moved to page 3, the
	//   status line said "Page: 3", and app.documents[i].modified was FALSE for target AND source
	//   afterwards. Start itself, Next Change, Stop and the double click below were all measured the
	//   same way and are all clean too.
	// ⇒ Scrolling and switching the spread do not dirty a document; the guard below is for the
	//   SELECTION, which is a different operation (see its own note).
	// ⚠WHAT WOULD CHANGE THE ANSWER: anything added to this path that touches the model rather than
	//   the view. Then measure again rather than reasoning from here.
	if (!KESCMGotoStoryFrame(db, row.fFrameUID, row.fPageUID, row.fStoryUID))
	{
		PMString s("Could not scroll.");	// 文言は Prev/Next の失敗時と同じ(同じ出来事なので)
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return kFalse;
	}

	KESCMSetStatus(PageLabel(db, row.fPageUID));
	return kTrue;
}

//----------------------------------------------------------------------------------------
// KESCMStorySelectWholeStory(KESCMStoryJump.h で宣言)
//----------------------------------------------------------------------------------------
/* KESCMStoryJumpToChange
   See the header for what this aims at and why it switches the tool.
*/
bool16 KESCMStoryJumpToChange(int32 rowIndex, int32 changeIndex)
{
	IKESCMStoryEditsFacade::Row row;
	IKESCMStoryEditsFacade::Change change;
	if (!Utils<IKESCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;
	if (!Utils<IKESCMStoryEditsFacade>()->GetChange(rowIndex, changeIndex, change))
		return kFalse;

	IDataBase* db = Utils<IKESCMCompareFacade>()->GetArmedTargetDB();
	if (db == nil || !Utils<IKESCMCompareFacade>()->IsDocDBOpen(db))
		return kFalse;

	const UIDRef storyRef(db, row.fStoryUID);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	// ★The range is clamped to the story as it stands NOW. The diff ran against the story as it was
	//   when the comparison did, and the reader may have edited it since - a stale end would be
	//   refused by the suite, and a stale start would select the wrong words silently.
	const TextIndex total = model->TotalLength();
	TextIndex from = change.fTargetStart;
	TextIndex to = change.fTargetEnd;
	if (from < 0) from = 0;
	if (to > total) to = total;
	if (from > total) from = total;
	if (to < from) to = from;

	// Making a selection recomposes - the same guard, and the same reason, as the double click's.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	// ***** THE FRAME THE EDIT IS IN, not the story's first one. ***** A story threaded across
	// several frames has its first frame nowhere near an edit late in it.
	// ⚠The fallback is the story's first frame (what a story row uses), for a story whose frame list
	//   cannot answer - an unplaced story has none at all.
	UID frameUID = row.fFrameUID;
	InterfacePtr<IFrameList> frameList(model->QueryFrameList());
	if (frameList != nil)
	{
		int32 frameIndex = 0;
		InterfacePtr<ITextFrameColumn> column(frameList->QueryFrameContaining(from, &frameIndex));
		if (column != nil)
		{
			// ★The frame the reader sees is the column's PARENT: a text frame's geometry lives on
			//   the item, and the column is what holds the text inside it.
			InterfacePtr<IHierarchy> columnHierarchy(column, UseDefaultIID());
			if (columnHierarchy != nil)
			{
				const UID parentUID = columnHierarchy->GetParentUID();
				if (parentUID != kInvalidUID)
					frameUID = parentUID;
			}
		}
	}
	if (frameUID == kInvalidUID)
		return kFalse;		// nothing placed - nowhere to go, and the story row says so already

	// ★The page is asked of the frame we are actually going to, not of the story's start: an edit
	//   late in a threaded story is on a different page from the story's beginning, and this is what
	//   the Pages panel follows.
	const UIDRef pageRef = Utils<ITextUtils>()->GetPageUIDRef(UIDRef(db, frameUID));
	const UID pageUID = (pageRef.GetDataBase() != nil) ? pageRef.GetUID() : row.fPageUID;

	// Both windows move here - the target to this frame, the source to the same story.
	const bool16 moved = KESCMGotoStoryFrame(db, frameUID, pageUID, row.fStoryUID);

	// ***** AND LIGHT THE CHARACTERS UP FOR A MOMENT. *****
	//
	// ★★★A MARK, NOT A SELECTION (2026-08-20, user's call: "その文字のところに移動＋マーカーを少しの
	//   時間出す感じに"). Until then this made a text selection, which had three costs a pointer does
	//   not: it threw away whatever the reader had selected, it forced the Type tool on, and it left
	//   the words sitting selected long after the reader had looked at them. The mark says "here" and
	//   then gets out of the way. ⇒ The selection is still available - it is what a DOUBLE click does
	//   now (KESCMStorySelectChange below).
	// ★It is drawn ON the characters by the text engine (KESCMStoryMarker.cpp), so it needs no
	//   coordinates from here: the story and the character range are the whole of the request.
	KESCMStoryMarker::Show(db, row.fStoryUID, from, to);

	// ***** AND THE OTHER SIDE OF THE EDIT GOES TO THE PANEL'S MESSAGE AREA. *****
	//
	// ★The row shows the side that CHANGED; this shows the other one, so that "what it used to say"
	//   is readable without leaving the panel (user's request, 2026-08-20: "パネルのメッセージ部分の
	//   有効活用"). For a deletion the row is showing what was removed, so what lands here is the
	//   text that closed up over it - see KESCMStoryList.h for why the field is called "other" and
	//   not "old".
	//
	// ★A LABEL ON THE FIRST LINE, THE TEXT FROM THE SECOND (user's call, 2026-08-20, after seeing
	//   the plain version: "一行目をOld 2行目から旧テキストかな"). The message area holds four lines
	//   in a Japanese UI and six in an English one, so the label costs one line and buys the reader
	//   the one thing a bare sentence in this box does not say - which version they are looking at.
	//
	// ★★"Source" / "Target", NOT "Old" / "New" - BECAUSE THE PANEL ALREADY SPEAKS THAT WAY. Two
	//   lines at the top of it name the documents being compared, "Target:" and "Source:", so the
	//   reader has been told which is which before ever reaching this box. A second pair of names
	//   for one pair of documents would be the panel disagreeing with itself
	//   ([[one-question-one-place]] applied to words rather than to code).
	//   ★★AND "TEXT" AFTER IT (user's call, 2026-08-21: "ソースとなっているところを、ソーステキスト
	//     にしましょうか"). Those two lines at the top name FILES; this names the WORDS inside one of
	//     them. Borrowing their word without saying which of the two things is meant made one label
	//     answer for both - "Source Text:" says it is the same document and a different thing.
	//   ⚠"Target:" FOR A DELETION, and that is not a special case bolted on: the row shows the side
	//     that CHANGED, so for a deletion the row holds the words that were REMOVED and what lands
	//     here is the text that closed up over them. Calling that the source would be false, and a
	//     deletion is the row where the reader most needs to know what stands there now.
	//     (fKind: 0 = replace, 1 = insert, 2 = delete - IKESCMStoryEditsFacade.h.)
	//
	// ★★AND IT GOES OVER IN THREE PIECES, NOT AS ONE SENTENCE (2026-08-20). The box is drawn by
	//   hand now (KESCMStatusTextView.cpp), so it can do here what the ROW already does: draw the
	//   characters that differ at the theme's text colour and fade the words around them. The split
	//   is not made here and could not be - the boundary between context and change is a code point
	//   index into text that has been cut at both ends, and PMString counts UTF-16. The model made
	//   it (KESCMStoryDiffRun's Slice) and it travels on the Change.
	//   ★The label is its own argument rather than the head of the first piece: when the message
	//     does not fit, the CONTEXT gives way from its outer ends, and a label living in the context
	//     would be the first thing cut. It is the one piece that has to survive.
	PMString label;
	label.SetTranslatable(kFalse);
	label.Append((change.fKind == 2) ? "Target Text:" : "Source Text:");
	KESCMSetStatusSegments(label, change.fOtherTextPre, change.fOtherText, change.fOtherTextPost);

	return moved;
}

bool16 KESCMStorySelectChange(int32 rowIndex, int32 changeIndex)
{
	IKESCMStoryEditsFacade::Row row;
	IKESCMStoryEditsFacade::Change change;
	if (!Utils<IKESCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;
	if (!Utils<IKESCMStoryEditsFacade>()->GetChange(rowIndex, changeIndex, change))
		return kFalse;

	IDataBase* db = Utils<IKESCMCompareFacade>()->GetArmedTargetDB();
	if (db == nil || !Utils<IKESCMCompareFacade>()->IsDocDBOpen(db))
		return kFalse;

	const UIDRef storyRef(db, row.fStoryUID);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	// Clamped to the story as it stands NOW, for the same reason the jump clamps: the reader may
	// have edited it since the comparison ran, and a stale end is refused while a stale start
	// silently selects the wrong words.
	const TextIndex total = model->TotalLength();
	TextIndex from = change.fTargetStart;
	TextIndex to = change.fTargetEnd;
	if (from < 0) from = 0;
	if (to > total) to = total;
	if (from > total) from = total;
	if (to < from) to = from;

	// Making a selection recomposes - the same guard, and the same reason, as everywhere else here.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	// ★THE MARK COMES DOWN. The single click that opened this double click put one up; leaving it
	//   there would put an inversion on top of the selection's own inversion, and the text under
	//   both is unreadable (KBS records exactly this in KBSJump.cpp).
	KESCMStoryMarker::Clear();

	// Everything below mirrors the whole-story double click's selection path; see
	// KESCMStorySelectWholeStory for why the tool goes on first and why the suite is asked twice.
	ISelectionManager* selectionManager = Utils<ISelectionUtils>()->GetActiveSelection();
	if (selectionManager == nil)
		return kFalse;

	selectionManager->DeselectAll(nil);

	InterfacePtr<ITool> activeTool(Utils<IToolBoxUtils>()->QueryActiveTool());
	if (activeTool == nil || !activeTool->IsTextTool())
	{
		InterfacePtr<ITool> iBeamTool(Utils<IToolBoxUtils>()->QueryTool(kIBeamToolBoss));
		if (iBeamTool == nil)
			return kFalse;
		if (!Utils<IToolBoxUtils>()->SetActiveTool(iBeamTool))
			return kFalse;
	}

	InterfacePtr<ITextSelectionSuite> textSelectionSuite(selectionManager, UseDefaultIID());
	if (textSelectionSuite == nil)
		textSelectionSuite.reset(InterfacePtr<ITextSelectionSuite>(selectionManager, IID_ITEMPTEXTSELECTION_SUITE).forget());
	if (textSelectionSuite == nil)
		return kFalse;

	// ⚠A DELETION HAS NO WIDTH on this side - the words are gone. RangeData(n, n) is the caret case,
	//   and RangeData.h:114-125 says a caret needs a lean to settle which side new text joins.
	//   kLeanForward puts it before what followed the deleted text, which is where the text used to
	//   begin.
	const RangeData range = (to > from)
							? RangeData(from, to)
							: RangeData(from, from, RangeData::kLeanForward);

	// kDontScrollSelection: the first click of this double click already centred the frame, and
	// kScrollIntoView would only promise the selection is somewhere on screen - undoing the better
	// answer already given.
	return textSelectionSuite->SetTextSelection(storyRef, range, Selection::kDontScrollSelection, nil);
}

bool16 KESCMStorySelectWholeStory(int32 rowIndex)
{
	IKESCMStoryEditsFacade::Row row;
	if (!Utils<IKESCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;

	// Both of these have just been reported by the single click that preceded this one - see the
	// header for why the second one says nothing.
	// ★The database is picked the same way the single click picks it: a REMOVED row's story lives
	//   in the source, so that is where the selection goes (2026-08-21). The reasoning is written
	//   out at KESCMStoryJumpToRow; it is not repeated here, but the two must not drift apart -
	//   selecting in one document after scrolling the other would be two windows disagreeing.
	const bool16 removedRow = ((row.fKinds & kKESCMStoryKindRemoved) != 0) ? kTrue : kFalse;
	IDataBase* db = removedRow ? Utils<IKESCMCompareFacade>()->GetArmedSourceDB()
							   : Utils<IKESCMCompareFacade>()->GetArmedTargetDB();
	if (db == nil || !Utils<IKESCMCompareFacade>()->IsDocDBOpen(db))
		return kFalse;
	if (row.fFrameUID == kInvalidUID)
		return kFalse;

	const UIDRef storyRef(db, row.fStoryUID);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	// ★Making a selection recomposes, and this plug-in may only have the document open in order to
	//   look at it. IDataBase.h:389-412 restores the flag the document came in with rather than
	//   forcing it clean - the same guard KBS puts round the identical operation.
	//   ★It guards the SELECTION, not the jump: the single click above was measured to leave both
	//     documents clean without one (see the note there). ★ONE GUARD, ON THE DOCUMENT BEING
	//     SELECTED INTO - which is why this is one guard where the rest of KESCM takes two
	//     (KESCMCore.cpp:480-481 and the three like it): only ever one document is selected into
	//     by this function, and `db` above already names it.
	//     ⚠2026-08-21 訂正: この理由は「Source は選択されない」と書いてあったが、**Removed 行では
	//       選択されるのは Source** になった。守る対象が「Target」でなく「db」だったので中身は
	//       正しいままで、外れていたのは理由の書き方だけ。
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	ISelectionManager* selectionManager = Utils<ISelectionUtils>()->GetActiveSelection();
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
	// ! This takes the KESCM tool off, if it was on. Deliberate (user's call, 2026-08-10), and
	//   written down in How to Use so that it is not a surprise.
	// ★The nil test on the active tool is ours: the sample does not test it at all - it calls
	//   IsTextTool() straight off whatever QueryActiveTool answered (GTTxtEdtUtils.cpp:118-119).
	//   The ASSERT a few lines below it is on the I-beam tool queried next, not on this one.
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

	// ! THE WHOLE STORY, WHICH IS (0, TotalLength()). The two-argument RangeData is (start, END), not
	//   (start, length) - RangeData.h:114-125 is explicit about it, and lists RangeData(34, 34) as
	//   INCORRECT for the caret case. Here the range has length, so no lean is needed: leans only
	//   settle which side of an insertion point new text joins, and there is no insertion point.
	//
	//   ★TotalLength() counts every character in the story "including data for embedded tables"
	//     (ITextModel.h:137-140), so a story that is nothing but a table selects that table's text
	//     as well - which is what "the whole story" has to mean for a row that was listed BECAUSE a
	//     table cell was edited.
	//   ★The trailing carriage return is inside the range on purpose: it is a character of the story,
	//     and InDesign's own Select All takes it too.
	//
	//   kDontScrollSelection: the first click of this double click already centred the frame with
	//   IPanorama::ScrollContentLocationToFrameCenter. kScrollIntoView would only promise the
	//   selection is somewhere on screen, undoing the better answer just given. (The official sample
	//   asks for kScrollIntoView because nothing has scrolled on its behalf.)
	return textSelectionSuite->SetTextSelection(storyRef, RangeData(0, model->TotalLength()),
		Selection::kDontScrollSelection, nil);
}

// End, KESCMStoryJump.cpp.
