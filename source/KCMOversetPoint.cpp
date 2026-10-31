//========================================================================================
//
//  KCMOversetPoint.cpp
//
//  The reasoning is in the header. This is the walk, restored from the Find Overset feature
//  retired on 2026-09-08 (commit 3e98956, KCMOversetScan.cpp) - the two computations only, not
//  the scan, the thumbnails, or the Prev/Next cycle that used them.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IGeometry.h"			// the frame's inner box, for the pasteboard transform
#include "IParcelList.h"		// GetLastParcelKey / GetPreviousParcelKey / GetParcelFrameUID
#include "ITableUtils.h"		// InsideTable / TableToPrimaryTextIndex - climbing out of a pushed-out cell
#include "ITextModel.h"			// QueryTextParcelList
#include "ITextParcelList.h"	// GetParcelContaining - and its contract about overset

// General includes:
#include "ParcelKey.h"			// ParcelKey::IsValid
#include "PMMatrix.h"
#include "PMRect.h"				// the outport corner comes off GetParcelBounds
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "Utils.h"

// Project includes:
#include "KCMOversetPoint.h"

namespace
{

/* LastPlacedOutport
   The outport of the last placed parcel of this thread: walking backwards from the end, the first
   parcel with a frame. ITextParcelList is what the text model hands out, IParcelList is the one
   that can be walked (a nil one stays nil).
*/
bool16 LastPlacedOutport(ITextModel* textModel, IDataBase* db, TextIndex pos,
						 UID& outFrame, PBPMPoint& outPb)
{
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
		if (KCMParcelCornerOnPasteboard(pl, k, db, kTrue /*outport*/, outFrame, outPb))
			return kTrue;
	return kFalse;
}

}	// namespace

//========================================================================================

// ⚠The corner is taken in PARCEL-LOCAL coordinates and is right for vertical text as well -
// GetParcelToFrameMatrix carries the writing direction. The header says why a branch here breaks
// what is currently correct.
bool16 KCMParcelCornerOnPasteboard(IParcelList* pl, const ParcelKey& key, IDataBase* db, bool16 outport,
								   UID& outFrame, PBPMPoint& outPb)
{
	const UID frameUID = pl->GetParcelFrameUID(key);
	if (frameUID == kInvalidUID)
		return kFalse;	// this piece is not placed (overset)

	InterfacePtr<IGeometry> frameGeo(db, frameUID, UseDefaultIID());
	if (frameGeo == nil)
		return kFalse;

	const PMRect   parcelBounds = pl->GetParcelBounds(key);				// parcel-local
	const PMMatrix toFrame      = pl->GetParcelToFrameMatrix(key);		// parcel -> frame inner
	const PMMatrix toPasteboard = ::InnerToPasteboardMatrix(frameGeo);	// frame inner -> pasteboard

	PMPoint corner = outport ? parcelBounds.RightBottom() : parcelBounds.LeftTop();
	toFrame.Transform(&corner);
	toPasteboard.Transform(&corner);

	outFrame = frameUID;
	outPb    = corner;
	return kTrue;
}

bool16 KCMIsTextIndexOverset(ITextModel* textModel, TextIndex pos)
{
	if (textModel == nil)
		return kFalse;

	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kFalse;

	// ★THE HEADER'S OWN CONTRACT: "If the TextIndex is in overset an invalid ParcelKey will be
	//   returned" (ITextParcelList.h, above GetParcelContaining). Nothing is inferred here.
	const ParcelKey key = tpl->GetParcelContaining(pos);
	if (!key.IsValid())
		return kTrue;

	// ⚠AND THE SECOND WAY TO BE UNCOMPOSED: a parcel exists but is not in any frame. KESCL judges
	//   a search hit's visibility by exactly this test, and a place that passes the first check
	//   can still fail this one.
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	return (pl->GetParcelFrameUID(key) == kInvalidUID) ? kTrue : kFalse;
}

bool16 KCMFindOversetOutport(ITextModel* textModel, IDataBase* db, TextIndex pos,
							 UID& outFrame, PBPMPoint& outPb)
{
	if (textModel == nil || db == nil)
		return kFalse;
	if (LastPlacedOutport(textModel, db, pos, outFrame, outPb))
		return kTrue;

	// Nothing of this thread is placed. A table cell pushed out of its frame along with its row is
	// the case that happens: the "+" the reader sees belongs to the frame the TABLE is in, so the
	// anchors are climbed until an ancestor answers. The guard stops both non-progress and deep
	// nesting.
	TextIndex cur = pos;
	for (int32 guard = 0; guard < 32; ++guard)
	{
		if (!Utils<ITableUtils>()->InsideTable(textModel, cur))
			break;
		const TextIndex up = Utils<ITableUtils>()->TableToPrimaryTextIndex(textModel, cur);
		if (up == cur)
			break;	// no progress
		cur = up;
		if (LastPlacedOutport(textModel, db, cur, outFrame, outPb))
			return kTrue;
	}
	return kFalse;
}

// End, KCMOversetPoint.cpp.
