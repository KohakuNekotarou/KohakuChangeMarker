//========================================================================================
//
//  KESCMViewSync.h
//
//  Layout view synchronisation: mirroring scroll position and zoom from one document's
//  layout view onto every other document's layout views, with the page Add/Remove
//  correction applied while a comparison is armed.
//
//  Split out of KESCMPeek.cpp on 2026-08-13. The functions and their behaviour are
//  unchanged; only their home moved. This is UI-side code: it works on IControlView and
//  IPanorama, which a model plug-in must not depend on.
//
//========================================================================================

#ifndef __KESCMViewSync_h__
#define __KESCMViewSync_h__

#include "BaseType.h"

// Drop the cached page geometry used by the sync engine. Call this whenever the pages of a
// tracked document may have moved (comparison start/stop, page add/remove, document close).
void	KESCMInvalidateSyncCaches();

// The "Sync Layout Views" flyout toggle. While ON, scrolling or zooming any layout view
// mirrors that viewport onto every other document's layout views (the split-window sibling
// of the same document is excluded). Independent of whether a comparison is armed.
bool16	KESCMGetLayoutSync();
void	KESCMSetLayoutSync(bool16 on);

// The "Align Other Views to Active" flyout action. Mirrors the active (frontmost) layout
// view's position and zoom onto the other documents' layout views once. Works whether or
// not the Sync toggle is ON. Returns kTrue when an active layout view was found and the
// sync ran.
bool16	KESCMAlignOtherViewsToActiveNow();

// Shutdown: drop the toggle so any notification still in flight is ignored by the observer's
// leading guard. Called from KESCMPeekStartup::Shutdown.
//
// ★This is NOT KESCMSetLayoutSync(kFalse) -- see the comment on the implementation for why
//  taking that route during teardown crashed every time.
void	KESCMViewSyncShutdown();

#endif // __KESCMViewSync_h__
