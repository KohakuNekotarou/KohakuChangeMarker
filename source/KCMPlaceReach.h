//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  WHETHER A CLICK CAN SELECT A PLACE IN THE TARGET (2026-10-24 night - docs/superpowers/specs/2026-10-24-kcm-list-like-
//  kfc-design.md section 1-2, the author: "the hidden and locked cases too - select the way KFC does"). Find/Change's own
//  Find Next is the model, as it is KFC's (KFCJump.cpp, NotSelected): a LOCKED place is not selected - the jump's flash
//  points at it instead; a HIDDEN one is selected with nothing of it showing; one on a HIDDEN SPREAD is neither shown nor
//  selected. The facts are KFC's (model/KFCPageItemFacts.cpp, and KFCSearchEngine.cpp's story lock), read the same way:
//    - locked: the story's own insert lock (IItemLockData on the story - InCopy's), the lock flags of the frame the place
//      composes into and of its outermost parent (ILockPosition::IsPageItemLocked(kFalse), IItemLockData::GetInsertLock -
//      a frame in a locked group too), and the frame's layer (IDocumentLayer::IsLocked);
//    - hidden: the frame's layer not visible, or the frame itself hidden (Facade::IPageItemVisibilityFacade::IsHidden);
//    - hidden spread: the frame's spread hidden in the Pages panel (IID_IHIDESPREADBOOLDATA).
//  The frame is the one the jump finds (KCMStoryJump.cpp): the place's own frame - an empty place asked as a caret - or,
//  for an overset place, the frame that carries the "+" (KCMFindOversetOutport).
//  ! A question that cannot be answered reads as "can be reached" - KFC's rule: a "cannot tell" must not start refusing
//    ordinary clicks.
//
//========================================================================================

#ifndef __KCMPlaceReach_h__
#define __KCMPlaceReach_h__

#include "UIDRef.h"

/** The reach of [from, to) of `story` in its document, as IKCMStoryEditsFacade's kReach* bits - 0 when it can be selected
	and shown. An empty place (a deletion's) is asked as a caret. Reads only (the composition it needs is asked under the
	document's SaveRestoreModifiedState). */
int32 KCMPlaceReach(const UIDRef& story, TextIndex from, TextIndex to);

#endif // __KCMPlaceReach_h__

// End, KCMPlaceReach.h.
