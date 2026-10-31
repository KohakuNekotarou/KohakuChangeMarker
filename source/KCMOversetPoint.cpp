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
#include "IFrameList.h"				// GetFirstDamagedFrameIndex - the text a cell's table stands in, composed first
#include "IFrameListComposer.h"		// RecomposeThruLastFrame
#include "IGeometry.h"				// the frame's inner box, for the pasteboard transform
#include "IGraphicFrameData.h"		// GetTextContentUID - only a frame with text content has the text ports
#include "IHandleShape.h"			// what GetTextFrameHandlePositions is asked of
#include "IHierarchy.h"				// QuerySplineFromTextFrame's answer
#include "IPageItemUtils.h"			// GetTextFrameHandlePositions - where InDesign draws the "+"
#include "IParcelList.h"			// GetLastParcelKey / GetPreviousParcelKey / GetParcelFrameUID
#include "ITextFrameColumn.h"		// what a parcel's frame is
#include "ITextModel.h"				// QueryTextParcelList
#include "ITextParcelList.h"		// GetParcelContaining - and its contract about overset
#include "ITextParcelListComposer.h"	// RecomposeThruTextIndex - a place's own parcel list composed through it
#include "ITextStoryThread.h"		// GetDictUID - the dictionary a thread belongs to
#include "ITextStoryThreadDict.h"	// GetAnchorTextRange - where that thread hangs from
#include "ITextUtils.h"				// QuerySplineFromTextFrame

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

/* FramePlusPoint
   Where InDesign draws the "+" of the frame this column belongs to: the question its handle code asks, the way it asks it
   (CHandleShape::DrawTextFrameHandles - the path's handle shape when the frame has one, else the frame's own). The header
   does not say in which coordinates the answer is; they are the frame's inner ones (the header of this file says what
   that was measured against). kFalse for what has no text ports - only a graphic frame with text content draws them, and
   text on a path asks elsewhere (IMainItemTOPData) - and the caller keeps the parcel's corner.
*/
bool16 FramePlusPoint(IDataBase* db, UID columnUID, PBPMPoint& outPb)
{
	InterfacePtr<ITextFrameColumn> column(db, columnUID, UseDefaultIID());
	if (column == nil)
		return kFalse;
	InterfacePtr<IHierarchy> frame(Utils<ITextUtils>()->QuerySplineFromTextFrame(column));
	InterfacePtr<IGraphicFrameData> frameData(frame, UseDefaultIID());
	InterfacePtr<IGeometry> geometry(frame, UseDefaultIID());
	if (frameData == nil || frameData->GetTextContentUID() == kInvalidUID || geometry == nil)
		return kFalse;
	InterfacePtr<IHandleShape> pathShape(frame, IID_IPATHHANDLESHAPE);
	InterfacePtr<IHandleShape> plainShape(frame, IID_IHANDLESHAPE);
	IHandleShape* const shape = (pathShape != nil) ? pathShape.get() : plainShape.get();
	if (shape == nil)
		return kFalse;

	PMPoint inPort;
	PMPoint outPort;
	Utils<IPageItemUtils>()->GetTextFrameHandlePositions(shape, &inPort, &outPort);
	::InnerToPasteboardMatrix(geometry).Transform(&outPort);
	outPb = outPort;
	return kTrue;
}

/* LastPlacedOutport
   The "+" of this thread: walking backwards from the end, the first parcel with a frame - that frame's "+" for the body,
   the parcel's outport corner for a thread with no ports of its own (a cell's red dot). ITextParcelList is what the text
   model hands out, IParcelList is the one that can be walked (a nil one stays nil).
*/
bool16 LastPlacedOutport(ITextModel* textModel, TextIndex pos, UID& outFrame, PBPMPoint& outPb)
{
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	IDataBase* const db = ::GetDataBase(textModel);
	for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
		if (KCMParcelCornerOnPasteboard(pl, k, db, kTrue /*outport*/, outFrame, outPb))
		{
			if (pos < textModel->GetPrimaryStoryThreadSpan())
				FramePlusPoint(db, outFrame, outPb);	// (a kFalse leaves the corner just taken)
			return kTrue;
		}
	return kFalse;
}

/* ComposeThrough
   The composition up to a place brought up to date before it is read: the parcels answer from the last composition, and
   text edited since - a restore this plug-in has just written, a person's typing - is composed only when something asks.
   THE OFFICIAL SHAPE (SnpTextModelHelper::QueryParcelContaining; the guide, sol-03 "Recomposing text"): the parcel list
   the place is read from, when it has damage, its composer, only as far as the place. ⛔NOT the frame list's composer with
   a cell's or a note's TextIndex - that takes the primary thread's alone (ITextParcelListComposer.h, and
   IFrameList::QueryFrameContaining with a cell's index brought InDesign down, KCMStoryList.cpp's KCMPrimaryIndexOf).
   ★THE TEXT A CELL'S TABLE STANDS IN COMES FIRST: which frame a row is in, and whether it was pushed out, is decided there,
   so a place outside the body has the frame list composed as a whole before its own thread is.
*/
void ComposeThrough(ITextModel* textModel, ITextParcelList* tpl, TextIndex pos)
{
	if (pos >= textModel->GetPrimaryStoryThreadSpan())
	{
		InterfacePtr<IFrameList> frameList(textModel->QueryFrameList());
		InterfacePtr<IFrameListComposer> frames(frameList, UseDefaultIID());
		if (frames != nil && frameList->GetFirstDamagedFrameIndex() != -1)
			frames->RecomposeThruLastFrame();
	}
	if (!tpl->GetFirstDamagedParcel().IsValid())
		return;
	InterfacePtr<ITextParcelListComposer> composer(tpl, UseDefaultIID());
	if (composer != nil)
		composer->RecomposeThruTextIndex(pos);
}

/* AskPlace (KCMPlaceColumn says what the question is)
   @return kFalse when it cannot be asked: no model, a position outside the story, no parcel list there.
*/
bool16 AskPlace(ITextModel* textModel, TextIndex pos, bool16 caret, UID& outColumn)
{
	outColumn = kInvalidUID;
	if (textModel == nil || pos < 0)
		return kFalse;
	// TotalLength is the caret's place after the story's closing return: no parcel holds it, and the character before it
	// is where it stands (asked of the composition as a character, it read as overset).
	const TextIndex total = textModel->TotalLength();
	if (pos > total || total <= 0)
		return kFalse;
	if (pos == total)
		pos = total - 1;

	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (tpl == nil || pl == nil)
		return kFalse;

	ComposeThrough(textModel, tpl, pos);

	ParcelKey key = tpl->GetParcelContaining(pos);
	if (!key.IsValid() && caret)
		key = tpl->GetParcelContainingLeanLeft(pos);
	if (key.IsValid())
		outColumn = pl->GetParcelFrameUID(key);
	return kTrue;
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

bool16 KCMThreadAnchorAt(ITextModel* textModel, TextIndex pos, TextIndex& outAnchor)
{
	InterfacePtr<ITextStoryThread> thread(textModel->QueryStoryThread(pos, nil, nil));
	if (thread == nil)
		return kFalse;
	InterfacePtr<ITextStoryThreadDict> dict(::GetDataBase(textModel), thread->GetDictUID(), UseDefaultIID());
	if (dict == nil)
		return kFalse;
	// ITextStoryThreadDict.h - the body's own dictionary answers "not anchored"; a table's or a footnote's answers the
	// range of its anchor character in the thread above it.
	bool16 anchored = kFalse;
	const Text::StoryRange anchor = dict->GetAnchorTextRange(&anchored);
	if (!anchored)
		return kFalse;
	outAnchor = anchor.Start(nil);	// RangeData::Start takes an optional Lean* out-parameter (RangeData.h)
	return kTrue;
}

UID KCMPlaceColumn(ITextModel* textModel, TextIndex pos, bool16 caret)
{
	UID column = kInvalidUID;
	AskPlace(textModel, pos, caret, column);
	return column;
}

bool16 KCMIsTextIndexOverset(ITextModel* textModel, TextIndex pos, bool16 caret)
{
	UID column = kInvalidUID;
	return (AskPlace(textModel, pos, caret, column) && column == kInvalidUID) ? kTrue : kFalse;
}

bool16 KCMFindOversetOutport(ITextModel* textModel, TextIndex pos, UID& outFrame, PBPMPoint& outPb)
{
	if (textModel == nil)
		return kFalse;
	if (LastPlacedOutport(textModel, pos, outFrame, outPb))
		return kTrue;

	// Nothing of this thread is placed: a table cell pushed out of its frame along with its row, a footnote whose
	// reference is overset (it was never placed at all). The "+" the reader sees belongs to the frame the TABLE, or the
	// reference, stands in, so the anchors are climbed until an ancestor answers.
	TextIndex cur = pos;
	for (int32 depth = 0; depth < kKCMThreadClimbLimit; ++depth)
	{
		TextIndex up = cur;
		if (!KCMThreadAnchorAt(textModel, cur, up) || up == cur)
			break;	// it hangs from nothing, or no progress
		cur = up;
		if (LastPlacedOutport(textModel, cur, outFrame, outPb))
			return kTrue;
	}
	return kFalse;
}

// End, KCMOversetPoint.cpp.
