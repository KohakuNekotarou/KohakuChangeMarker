//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Which edits the Story mode is showing on the page right now, and in which of the two
//  documents. Two things decide it, and the second REVERSES the first rather than adding to it:
//
//    * the "Always Show Marks on Target" / "Always Show Marks on Source" toggles - marks that STAY UP, the
//      Story mode's half of what those two toggles already do for the Pixel mode's frames
//      (user's request, 2026-08-22: "ツールでボタンを押さなくても常にマークが出る様に、
//      それをピクセルの方もストーリーの方にも");
//    * the KESCM tool's left button while it is held - which turns the window under the cursor
//      ROUND for as long as the button is down: marks that were off come on, marks that were on
//      go off. ★★That is the plug-in's one rule for the button, and it is why the separate
//      "Hold to Hide Marks" toggle could be retired on 2026-08-22 (user's call) - it had said
//      "keep them up, and hide them while held", of which the first half was already what
//      "Always Show Marks on Target" said. The Pixel mode's frames follow the same rule through two
//      separate flags (alwaysScreen and sMarksTempHidden, both in KESCMDrawEventHandler).
//
//  ★★IT IS THE STORY MODE'S ANSWER TO THE PIXEL MODE'S FRAMES, AND IT IS DELIBERATELY A DIFFERENT
//  SHAPE. The Pixel mode has no idea what changed - only which rectangles of the page came out
//  different - so it draws frames around them. The Story mode knows exactly which CHARACTERS
//  changed, so it lights those up instead and needs no frame around the page and no scaling with
//  the zoom (user's request: "拡大率で大きさは変わらない、ページへの外枠もいらない").
//
//  ★THE MARK ITSELF IS THE JUMP'S, UNCHANGED (user's call: "ジャンプと時につかってるのとおなじで
//  いいです").
//
//  ⚠★★★2026-08-23: WORKING OUT WHICH RANGES IS NO LONGER DONE HERE. It moved to the model plug-in
//  (source/KESCMStoryMarkBuild.cpp) along with the adornment that draws them
//  (source/KESCMStoryMarker.cpp), because **the UI's File > Export > PDF runs in the background and
//  a kUIPlugIn is never handed the drawing** - so marks living on this side could not appear on
//  paper or in an exported PDF, whatever guard they carried.
//  ⇒ The three functions below kept their names, their signatures and their meaning; each is now
//    one call through IKESCMStoryMarkFacade. Everything the comments above describe still happens,
//    just on the other side of the boundary.
//
//  ★WHICH DOCUMENT DECIDES WHAT IS MARKABLE AT ALL: a deletion only exists in the older document,
//  an insertion only in the newer one, and a story that was added or removed outright exists in
//  one of them and not the other.
//
//========================================================================================

#ifndef __KESCMStoryPressMarks_h__
#define __KESCMStoryPressMarks_h__

#include "BaseType.h"

/** Work out what should be lit up now and put exactly that on screen.

	★THE ONE ENTRY POINT, and it is idempotent: it reads the toggles, the press state and the
	comparison, and installs the result. Callers do not decide what to show - they say "something
	changed" and this decides. That is why it can be hung off a notification without either side
	knowing what the other is for.

	Does nothing (and takes down nothing it did not put up) unless a STORY comparison is armed, so
	it is safe to call from anywhere, in either mode.

	Call it whenever any of its inputs move: the toggles, the compare mode, a comparison being
	built or thrown away, a row being refreshed, the button going down or coming up.
*/
void	KESCMStoryMarksRefresh();

/** The tool's left button went down over one of the two windows. */
void	KESCMStoryPressMarksBegin(bool16 useSourceDocument);

/** ...and came up again. What the toggles asked for stays; what the press added goes.
	Safe to call on any release, including one this file never heard the start of. */
void	KESCMStoryPressMarksEnd();

#endif // __KESCMStoryPressMarks_h__

// End, KESCMStoryPressMarks.h.
