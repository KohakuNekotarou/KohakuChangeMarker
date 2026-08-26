//========================================================================================
//
//  KCMScrollMap.h
//
//  The scrollbar map: a narrow strip widget injected at run time just left of a document
//  window's (kLayoutPresentationBoss) vertical scrollbar, showing where the pages carrying a KCM
//  frame are. The same idea as Visual Studio's search marks in its scrollbar. The record of how
//  this was established against the SDK is docs/ai-notes/scrollbar-minimap.md, relative to the SDK
//  root.
//
//  The strip draws real data: changed pages in red, pages registered as Add/Remove in green
//  (the colours are the user's choice).
//
//========================================================================================

#ifndef __KCMScrollMap_h__
#define __KCMScrollMap_h__

#include "BaseType.h"		// bool16

class IDataBase;

// Collect the document windows (presentations) behind every layout view of targetDB and, where
// there is not one already, inject the map strip left of the vertical scrollbar. A window that
// already has one is skipped, so this is safe to call any number of times; a window with no
// vertical scrollbar is skipped silently.
void	KCMScrollMapAttach(IDataBase* targetDB);

// Find and remove every injected map strip, in every window of every document. No pointer to a
// strip is ever held (each one is looked up with FindWidget), so this is safe even for a window
// that has already been closed.
void	KCMScrollMapDetachAll();

// Invalidate every injected strip, to bring the marks up to date after a comparison, a
// recomparison or a registration toggle. Does nothing when there is no strip.
void	KCMScrollMapInvalidateAll();

// Detects a manual Hide/Show Spread by riding along on the spread draw event. The caller is the
// UI-side draw service, KCMUIDrawEventHandler::HandleDrawEvent (KCMUIDrawEvent.cpp).
//
// Hiding or showing a spread from the Pages panel goes through none of KCM's hooks, but it always
// causes a redraw. So on every draw (throttled to 250 ms) a fingerprint of the hidden-flag layout
// is taken, and the map is invalidated when it has changed. A hidden state changed by Undo/Redo
// is picked up the same way.
//
// THE FINGERPRINT COVERS THREE DOCUMENTS, not two: the Target, the Source, and the document
// scanned by Find Overset (a window with Find Overset alone gets a strip too). For the same
// reason, an unarmed state does NOT return early -- it continues whenever Find Overset is on.
// The two cases that do return at once are "Show Scrollbar Map is off" and "neither armed nor
// scanning".
//
// The fingerprint also covers WHICH MASTER SPREAD IS ON SCREEN. What the map holds changes while
// a master is shown (only that master's pages go on it), and switching spreads goes through none
// of KCM's hooks either -- exactly the same reason as the hidden flags, so it rides along here.
void	KCMScrollMapNoticeDrawEvent();

// Whether the scrollbar map is on: the "Show Scrollbar Map" toggle in the flyout, on by default.
// While it is off, KCMScrollMapAttach and KCMScrollMapNoticeDrawEvent return at once, so a Start
// injects no strip. Strips that already exist are removed by whoever flips the toggle.
//
// THE SETTER ONLY WRITES THE FLAG. Attaching and detaching strips belongs to its callers, and
// there are two of them:
//   - KCMActionComponent (the flyout toggle) ... Attach + Invalidate when on, DetachAll when off
//   - KCMPanelState (restoring the saved panel settings) ... the flag ALONE.
// The second one doing nothing else is correct, not an oversight: KCMLoadPanelStateIfPresent runs
// once per session (guarded by sLoaded) from the UI-side Startup, and at that point no comparison
// has begun and there is not a single strip to attach or detach.
// If that restore is ever made callable while a comparison is running, it will need the same
// cleanup the flyout toggle does.
bool16	KCMGetScrollMapEnabled();
void	KCMSetScrollMapEnabled(bool16 on);

#endif // __KCMScrollMap_h__
