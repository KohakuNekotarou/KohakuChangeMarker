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
// The one case that returns at once is "neither armed nor scanning". ⚠There was a second until
// 2026-09-14: the map could be switched off from the flyout. It cannot any more - it is always on.
//
// The fingerprint also covers WHICH MASTER SPREAD IS ON SCREEN. What the map holds changes while
// a master is shown (only that master's pages go on it), and switching spreads goes through none
// of KCM's hooks either -- exactly the same reason as the hidden flags, so it rides along here.
void	KCMScrollMapNoticeDrawEvent();

#endif // __KCMScrollMap_h__
