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
#include "ITextModel.h"
#include "ITextSelectionSuite.h"	// SetTextSelection - the double click's whole point
#include "ITool.h"					// IsTextTool - is a text tool already active?
#include "IToolBoxUtils.h"			// QueryActiveTool / QueryTool / SetActiveTool

// General includes:
#include "PMString.h"
#include "RangeData.h"				// the range handed to SetTextSelection
#include "TextEditorID.h"			// kIBeamToolBoss - the Type tool the double click switches to
#include "UIDRef.h"
#include "Utils.h"

// Project includes:
#include "KESCMID.h"
#include "KESCMChangeNav.h"	// KESCMGotoStoryFrame
#include "IKESCMCompareFacade.h"	// arm 状態(2026-08-13・分割 第1段 Task 11 で Facade 経由へ)
#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "KESCMStoryJump.h"
#include "IKESCMStoryEditsFacade.h"	// the row a click landed on (Facade since 2026-08-13, Task 14)

namespace
{

/** Where the status line says the jump landed: "Page: 3", or the pasteboard when there is no page.

	★The page number comes from IPageList::GetPageString with the same seven arguments the
	navigation's own label uses (KESCMChangeNav.cpp's KESCMStopLabel) - section prefixes and all, so
	that the two places in this panel that name a page never disagree about how a page is spelled.
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
		pageList->GetPageString(pageUID, &numStr, kTrue, kFalse, kDefaultPageType, kTrue, kFalse);

	label.Append("Page: ");
	if (numStr.NumUTF16TextChars() > 0)
		label.Append(numStr);
	else
		label.Append("?");	// 番号が取れないページ(通常は起きない。KESCMStopLabel と同じ受け皿)
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

	// ★The list belongs to the comparison that built it, so the document to move is the armed
	//   TARGET - not whatever happens to be in front. Checked for life rather than trusted: the list
	//   is dropped when a compared document closes, but a click already on its way when that
	//   happened would otherwise arrive here holding a database that is gone.
	IDataBase* db = Utils<IKESCMCompareFacade>()->GetArmedTargetDB();
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
bool16 KESCMStorySelectWholeStory(int32 rowIndex)
{
	IKESCMStoryEditsFacade::Row row;
	if (!Utils<IKESCMStoryEditsFacade>()->GetRow(rowIndex, row))
		return kFalse;

	// Both of these have just been reported by the single click that preceded this one - see the
	// header for why the second one says nothing.
	IDataBase* db = Utils<IKESCMCompareFacade>()->GetArmedTargetDB();
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
	// ★The nil test on the active tool is ours: the sample only ASSERTs there.
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
