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
#include "IDataBase.h"
#include "IPageList.h"		// GetPageString - the page number the status line names

// General includes:
#include "PMString.h"

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

	if (!KESCMGotoStoryFrame(db, row->fFrameUID, row->fPageUID))
	{
		PMString s("Could not scroll.");	// 文言は Prev/Next の失敗時と同じ(同じ出来事なので)
		s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return kFalse;
	}

	KESCMSetStatus(PageLabel(db, row->fPageUID));
	return kTrue;
}

// End, KESCMStoryJump.cpp.
