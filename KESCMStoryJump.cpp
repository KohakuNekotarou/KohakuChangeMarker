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
#include "KESCMCore.h"		// KESCMArmedTargetDB / KESCMIsDocDBOpen / KESCMSetStatus
#include "KESCMStoryJump.h"
#include "KESCMStoryList.h"

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
	const KESCMStoryRow* row = KESCMStoryList::GetRow(rowIndex);
	if (row == nil)
		return kFalse;	// out of range, or the "No edits" placeholder - nowhere to go, silently

	// ★The list belongs to the comparison that built it, so the document to move is the armed
	//   TARGET - not whatever happens to be in front. Checked for life rather than trusted: the list
	//   is dropped when a compared document closes, but a click already on its way when that
	//   happened would otherwise arrive here holding a database that is gone.
	IDataBase* db = KESCMArmedTargetDB();
	if (db == nil || !KESCMIsDocDBOpen(db))
	{
		PMString s("The comparison is no longer running.");
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return kFalse;
	}

	// A story with no frame at all is a real edit - it is in the document and it changed - but there
	// is nowhere on a page to show it. Say so rather than moving to an arbitrary place.
	if (row->fFrameUID == kInvalidUID)
	{
		PMString s("That story is not placed in a frame.");
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return kFalse;
	}

	if (!KESCMGotoStoryFrame(db, row->fFrameUID, row->fPageUID, row->fStoryUID))
	{
		PMString s("Could not scroll.");	// 文言は Prev/Next の失敗時と同じ(同じ出来事なので)
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return kFalse;
	}

	KESCMSetStatus(PageLabel(db, row->fPageUID));
	return kTrue;
}

//----------------------------------------------------------------------------------------
// KESCMStoryPlaceCaret(KESCMStoryJump.h で宣言)
//----------------------------------------------------------------------------------------
bool16 KESCMStoryPlaceCaret(int32 rowIndex)
{
	const KESCMStoryRow* row = KESCMStoryList::GetRow(rowIndex);
	if (row == nil)
		return kFalse;

	// Both of these have just been reported by the single click that preceded this one - see the
	// header for why the second one says nothing.
	IDataBase* db = KESCMArmedTargetDB();
	if (db == nil || !KESCMIsDocDBOpen(db))
		return kFalse;
	if (row->fFrameUID == kInvalidUID)
		return kFalse;

	const UIDRef storyRef(db, row->fStoryUID);
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

	// ***** THE TYPE TOOL, BECAUSE THIS IS AN INVITATION TO EDIT. ***** A caret placed while some
	// other tool is active is not somewhere the user can start typing, which is the whole of what a
	// double click here is asking for. The official recipe does exactly this and in this order -
	// tool first, selection second (gotolasttextedit's GTTxtEdtUtils.cpp:117-136).
	// ! This takes the KESCM tool off, if it was on. Deliberate (user's call, 2026-08-10), and
	//   written down in How to Use so that it is not a surprise.
	InterfacePtr<ITool> activeTool(Utils<IToolBoxUtils>()->QueryActiveTool());
	if (activeTool == nil || !activeTool->IsTextTool())
	{
		InterfacePtr<ITool> iBeamTool(Utils<IToolBoxUtils>()->QueryTool(kIBeamToolBoss));
		if (iBeamTool == nil)
			return kFalse;
		if (!Utils<IToolBoxUtils>()->SetActiveTool(iBeamTool))
			return kFalse;
	}

	// Clear whatever was selected first (the official sample does the same): a page-item selection
	// left standing is a second selection, in a different CSB.
	if (selectionManager->SelectionExists(kInvalidClass /*any CSB*/, ISelectionManager::kAnySelection))
		selectionManager->DeselectAll(nil);

	InterfacePtr<ITextSelectionSuite> textSelectionSuite(selectionManager, UseDefaultIID());
	if (textSelectionSuite == nil)
		return kFalse;

	// ! A CARET IS A ZERO-LENGTH RANGE, AND A ZERO-LENGTH RANGE MUST NAME A LEAN. RangeData.h:114-125
	//   spells it out: RangeData(34, 34) is listed as INCORRECT, and the two-argument form is
	//   (start, END) rather than (start, length). The caret form is the one the official sample uses
	//   - RangeData(index, kLeanForward) - and index 0 is the start of the story, which is where the
	//   design says to put it. Nothing is selected: this points at a place to start typing, it does
	//   not pick out text (there is no "hit range" on a story row to pick out).
	//
	//   kDontScrollSelection: the first click of this double click already centred the frame with
	//   IPanorama::ScrollContentLocationToFrameCenter. kScrollIntoView would only promise the caret
	//   is somewhere on screen, undoing the better answer just given. (The official sample asks for
	//   kScrollIntoView because nothing has scrolled on its behalf.)
	return textSelectionSuite->SetTextSelection(storyRef, RangeData(0, RangeData::kLeanForward),
		Selection::kDontScrollSelection, nil);
}

// End, KESCMStoryJump.cpp.
