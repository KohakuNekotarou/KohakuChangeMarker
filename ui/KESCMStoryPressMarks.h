//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  What the KESCM tool shows in the Story mode while the left button is held: EVERY edit in the
//  window under the cursor, inverted, until the button comes back up.
//
//  ★★IT IS THE STORY MODE'S ANSWER TO THE PIXEL MODE'S REVEAL, AND IT IS DELIBERATELY A
//  DIFFERENT SHAPE. The Pixel mode has no idea what changed - only which rectangles of the page
//  came out different - so it draws frames around them. The Story mode knows exactly which
//  CHARACTERS changed, so it lights those up instead and needs no frame around the page and no
//  scaling with the zoom (user's request, 2026-08-22: "変化や追加などが有った部分を反転状態で
//  マークを出して欲しい ... 拡大率で大きさは変わらない、ページへの外枠もいらない").
//
//  ★THE MARK ITSELF IS THE JUMP'S, UNCHANGED (user's call: "ジャンプと時につかってるのとおなじで
//  いいです"). This file works out WHICH ranges; KESCMStoryMarker draws them, as the global text
//  adornment it already was.
//
//  ★THE WINDOW UNDER THE CURSOR DECIDES WHICH DOCUMENT (user's call, 2026-08-22), which is the
//  same rule the Pixel mode's reveal follows. It also decides what is markable at all: a deletion
//  only exists in the older document, an insertion only in the newer one.
//
//========================================================================================

#ifndef __KESCMStoryPressMarks_h__
#define __KESCMStoryPressMarks_h__

#include "BaseType.h"

/** Light up every edit in one of the two compared documents, and leave it up.

	Does nothing at all unless a Story comparison is armed - so the caller may call it on any
	press without asking first.

	@param useSourceDocument kTrue for the older document (the one the source window shows),
		kFalse for the newer one.
	@return kTrue if anything was put on screen. kFalse leaves whatever was showing alone, which
		is what lets a press in the Pixel mode leave a jump's marker where it was.
*/
bool16	KESCMStoryPressMarksBegin(bool16 useSourceDocument);

/** Take them down again, if this file is what put them up. Safe to call on any release. */
void	KESCMStoryPressMarksEnd();

#endif // __KESCMStoryPressMarks_h__

// End, KESCMStoryPressMarks.h.
