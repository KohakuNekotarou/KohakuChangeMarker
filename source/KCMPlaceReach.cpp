//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMPlaceReach.h. The bodies are KFC's (model/KFCPageItemFacts.cpp - KFC 1.4's), copied rather than shared: the
//  two plug-ins are separate products, and each keeps its own copy of what it reads.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBoolData.h"					// a spread's hidden state (IID_IHIDESPREADBOOLDATA)
#include "IDataBase.h"					// SaveRestoreModifiedState
#include "IDocumentLayer.h"
#include "IHierarchy.h"
#include "IItemLockData.h"
#include "ILayerUtils.h"
#include "ILayoutUtils.h"				// QueryOutermostParentFor
#include "ILockPosition.h"
#include "IOwnedItem.h"					// GetFrameRef - the frame an inline's anchor is composed into
#include "IPageItemVisibilityFacade.h"
#include "IPasteboardUtils.h"			// QuerySpread - the spread an item stands on
#include "ISpread.h"
#include "ISpreadLayer.h"
#include "ITextModel.h"

// General includes:
#include "SpreadID.h"					// IID_IHIDESPREADBOOLDATA
#include "TextID.h"						// kInlineBoss
#include "Utils.h"

// Project includes:
#include "IKCMStoryEditsFacade.h"		// the kReach* bits
#include "KCMOversetPoint.h"			// KCMFindOversetOutport - the frame that carries an overset place's "+"
#include "KCMStoryList.h"				// KCMStoryFrameAt - the frame a place composes into
#include "KCMPlaceReach.h"

namespace
{

// THE LAYER OF AN ITEM PLACED IN TEXT - an inline or anchored frame, and the text frame column inside one - IS ITS ANCHOR'S
// (KFC's AnchorLayerUID and its reasoning: an inline's hierarchy answers kInvalidUID for its layer). Up to the kInlineBoss the
// item hangs from, then the frame its anchor composes into; out again while that frame is itself in text. kInvalidUID when
// the item does not hang in text, or its anchor stands in overset text.
UID AnchorLayerUID(IDataBase* db, UID item)
{
	const int32 kClimbAtMost = 16;
	UID at = item;
	for (int32 nest = 0; nest < kClimbAtMost && at != kInvalidUID; ++nest)
	{
		UID inlineUID = kInvalidUID;
		UID up = at;
		for (int32 step = 0; step < kClimbAtMost && up != kInvalidUID && db->IsValidUID(up); ++step)
		{
			if (db->GetClass(up) == kInlineBoss)
			{
				inlineUID = up;
				break;
			}
			InterfacePtr<IHierarchy> h(db, up, UseDefaultIID());
			up = (h != nil) ? h->GetParentUID() : kInvalidUID;
		}
		if (inlineUID == kInvalidUID)
			return kInvalidUID;
		InterfacePtr<IOwnedItem> owned(db, inlineUID, UseDefaultIID());
		const UIDRef anchorFrame = (owned != nil) ? owned->GetFrameRef() : UIDRef::gNull;
		if (anchorFrame.GetDataBase() != db || anchorFrame.GetUID() == kInvalidUID)
			return kInvalidUID;
		InterfacePtr<IHierarchy> anchorHier(anchorFrame, UseDefaultIID());
		const UID layer = (anchorHier != nil) ? Utils<ILayerUtils>()->GetLayerUID(anchorHier) : kInvalidUID;
		if (layer != kInvalidUID)
			return layer;
		at = anchorFrame.GetUID();
	}
	return kInvalidUID;
}

// The frame's SPREAD layer points at the DOCUMENT layer that carries the switches (the Layers panel's row). nil when any
// step cannot be resolved.
IDocumentLayer* QueryDocLayerOf(IDataBase* db, IHierarchy* frameHier)
{
	UID spreadLayerUID = Utils<ILayerUtils>()->GetLayerUID(frameHier);
	if (spreadLayerUID == kInvalidUID)
		spreadLayerUID = AnchorLayerUID(db, ::GetUID(frameHier));
	if (spreadLayerUID == kInvalidUID)
		return nil;
	InterfacePtr<ISpreadLayer> spreadLayer(db, spreadLayerUID, UseDefaultIID());
	if (spreadLayer == nil)
		return nil;
	InterfacePtr<IDocumentLayer> docLayer(db, spreadLayer->GetDocLayerUID(), UseDefaultIID());
	return docLayer.forget();
}

// Hidden: its layer hidden, or the item itself (Object > Hide) - both asked, as spellpanel's DetermineIfTextIsHidden and
// Find/Change ask them.
bool IsFrameHidden(IDataBase* db, UID frameUID)
{
	InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;
	InterfacePtr<IDocumentLayer> docLayer(QueryDocLayerOf(db, frameHier));
	if (docLayer != nil && !docLayer->IsVisible())
		return true;
	return Utils<Facade::IPageItemVisibilityFacade>()->IsHidden(UIDRef(db, frameUID)) != kFalse;
}

bool IsFrameOnLockedLayer(IDataBase* db, UID frameUID)
{
	InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;
	InterfacePtr<IDocumentLayer> docLayer(QueryDocLayerOf(db, frameHier));
	return docLayer != nil && docLayer->IsLocked();
}

// The item's own two locks (Object > Lock; InCopy's insert lock), asked of the frame the text composes into AND of its
// outermost parent - the UID a parcel names is not the page item the lock lives on (KFC measured: locking a frame left its
// text selectable until the outermost parent was asked too), and a frame inside a locked group answers by its group.
bool IsPageItemLockedForEdit(IDataBase* db, UID frameUID)
{
	InterfacePtr<ILockPosition> lockPos(db, frameUID, UseDefaultIID());
	if (lockPos != nil && lockPos->IsPageItemLocked(kFalse))
		return true;
	InterfacePtr<IItemLockData> lockData(db, frameUID, UseDefaultIID());
	if (lockData != nil && lockData->GetInsertLock())
		return true;
	InterfacePtr<IHierarchy> hier(db, frameUID, UseDefaultIID());
	if (hier == nil)
		return false;
	InterfacePtr<ILockPosition> outerLockPos(static_cast<ILockPosition*>(
		Utils<ILayoutUtils>()->QueryOutermostParentFor(hier, IID_ILOCKPOSITION)));
	if (outerLockPos != nil && outerLockPos->IsPageItemLocked(kFalse))
		return true;
	InterfacePtr<IItemLockData> outerLockData(static_cast<IItemLockData*>(
		Utils<ILayoutUtils>()->QueryOutermostParentFor(hier, IID_IITEMLOCKDATA)));
	return outerLockData != nil && outerLockData->GetInsertLock();
}

bool IsSpreadOfHidden(IDataBase* db, UID frameUID)
{
	InterfacePtr<IHierarchy> hier(db, frameUID, UseDefaultIID());
	if (hier == nil)
		return false;
	InterfacePtr<ISpread> spread(Utils<IPasteboardUtils>()->QuerySpread(hier));
	if (spread == nil)
		return false;
	InterfacePtr<IBoolData> hidden(db, ::GetUID(spread), IID_IHIDESPREADBOOLDATA);
	return hidden != nil && hidden->GetBool() != kFalse;
}

}	// anonymous namespace

int32 KCMPlaceReach(const UIDRef& story, TextIndex from, TextIndex to)
{
	IDataBase* const db = story.GetDataBase();
	if (db == nil || story.GetUID() == kInvalidUID || from < 0)
		return 0;
	// Reading only - but the frame's answer needs the story composed through the place (KCMStoryFrameAt composes).
	IDataBase::SaveRestoreModifiedState guard(db);
	int32 reach = 0;

	// THE STORY'S OWN LOCK - IItemLockData sits on the text story too (an InCopy assignment's "content cannot be edited").
	InterfacePtr<IItemLockData> storyLock(story, UseDefaultIID());
	if (storyLock != nil && storyLock->GetInsertLock())
		reach |= IKCMStoryEditsFacade::kReachLocked;

	// THE FRAME THE JUMP GOES TO: the place's own, a caret's for an empty place; else, overset, the one with the "+".
	UID frame = KCMStoryFrameAt(db, story.GetUID(), from, (to <= from) ? kTrue : kFalse);
	if (frame == kInvalidUID)
	{
		InterfacePtr<ITextModel> model(story, UseDefaultIID());
		UID plus = kInvalidUID;
		PBPMPoint pb;
		if (model != nil && KCMFindOversetOutport(model, db, from, plus, pb))
			frame = plus;
	}
	if (frame == kInvalidUID || !db->IsValidUID(frame))
		return reach;		// placed nowhere: nothing more to tell

	if (IsPageItemLockedForEdit(db, frame) || IsFrameOnLockedLayer(db, frame))
		reach |= IKCMStoryEditsFacade::kReachLocked;
	if (IsFrameHidden(db, frame))
		reach |= IKCMStoryEditsFacade::kReachHidden;
	if (IsSpreadOfHidden(db, frame))
		reach |= IKCMStoryEditsFacade::kReachHiddenSpread;
	return reach;
}

bool16 KCMIsFrameHidden(IDataBase* db, UID frame)
{
	if (db == nil || frame == kInvalidUID || !db->IsValidUID(frame))
		return kFalse;
	return IsFrameHidden(db, frame) ? kTrue : kFalse;
}

// End, KCMPlaceReach.cpp.
