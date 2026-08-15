//========================================================================================
//
//  KESCMPathDisplay.h
//
//  How a file path is SHOWN. One function, one decision, one place.
//
//  Created 2026-08-15 at the user's request: the panel's Target:/Source: lines and the book
//  comparison's two lines showed Windows paths with backslashes, and on a Japanese system the
//  backslash is drawn as a yen sign - so "…\new\ch01.indd" reads as "…¥new¥ch01.indd". Forward
//  slashes read the same everywhere.
//
//  ★WHY A FILE OF ITS OWN. Three places show a path (the panel, the book dialog, the book
//  confirmation alert) and they reach it through two different functions. Putting the rule here
//  means the answer to "how do we show a path?" exists once - the same reason KESCMElidePathFront
//  was moved out of KESCMBookPair.h in Stage 1 Task 15.
//
//  ⚠NOT for paths the user is meant to USE. The status line reports where a file was saved
//  (KESCMPanelState.json, KESCMPageChecks.json, the changed-pages TSV) and those stay as the
//  platform writes them, because they get pasted into Explorer. This function is for the two
//  documents being compared, which are named to be READ, not copied.
//
//  ★This is a view decision, not a model one - the same boundary IKESCMBookFacade.h draws when
//  it says shortening a path to fit is the caller's business.
//
//========================================================================================

#ifndef __KESCMPathDisplay_h__
#define __KESCMPathDisplay_h__

#include "PMString.h"

/** The same path with every backslash turned into a forward slash.

	@param path any path, or any string - a string without separators comes back unchanged.
	@return the path as it should be SHOWN. Never translatable (a path is data, not UI text).
*/
PMString KESCMPathForDisplay(const PMString& path);

#endif // __KESCMPathDisplay_h__

// End, KESCMPathDisplay.h.
