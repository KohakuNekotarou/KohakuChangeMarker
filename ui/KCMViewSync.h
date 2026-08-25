//========================================================================================
//
//  KCMViewSync.h
//
//  Layout view synchronisation: mirroring scroll position and zoom from one document's
//  layout view onto every other document's layout views, with the page Add/Remove
//  correction applied while a comparison is armed.
//
//  UI-side code: it works on IControlView and IPanorama, which a model plug-in must not
//  depend on.
//
//========================================================================================

#ifndef __KCMViewSync_h__
#define __KCMViewSync_h__

#include "BaseType.h"

// Drop the cached page geometry used by the sync engine. Called on comparison start/stop, document
// close, sync toggle on/off, "Align Other Views" and shutdown -- i.e. wherever the set of tracked
// documents changes.
//
// ⚠**Adding or removing a page does NOT call this, and that is deliberate.** No notification this
// plug-in listens to is raised when a page is added, so page geometry that moves while the sync is
// running is caught by the cache's 250ms TTL instead (the second half of the invalidation comment
// in the .cpp). Do not list "page add/remove" here: it makes the absence of such a call read like
// an oversight.
void	KCMInvalidateSyncCaches();

// The "Sync Layout Views" flyout toggle. While ON, scrolling or zooming any layout view
// mirrors that viewport onto every other document's layout views (the split-window sibling
// of the same document is excluded). Independent of whether a comparison is armed.
bool16	KCMGetLayoutSync();
void	KCMSetLayoutSync(bool16 on);

// The "Align Other Views to Active" flyout action. Mirrors the active (frontmost) layout
// view's position and zoom onto the other documents' layout views once. Works whether or
// not the Sync toggle is ON.
//
// ★Returns kTrue only where views were actually aligned. kFalse has three causes and the caller
// must not report success for any of them (the third one used to be reported as success):
//   (a) no frontmost layout view / it has no panorama or document
//   (b) armed and the front document is a third document (the engine syncs Target<->Source only)
//   (c) ★there was nothing to align TO -- only one document open, the partner was closed, or
//       Target and Source are the same document
bool16	KCMAlignOtherViewsToActiveNow();

// Shutdown: drop the toggle so any notification still in flight is ignored by the observer's
// leading guard. Called from KCMUIStartup::Shutdown.
//
// ★This is NOT KCMSetLayoutSync(kFalse) -- see the comment on the implementation for why
//  taking that route during teardown crashed every time.
void	KCMViewSyncShutdown();

#endif // __KCMViewSync_h__
