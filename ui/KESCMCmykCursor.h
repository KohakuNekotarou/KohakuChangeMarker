//========================================================================================
//
//  KESCMCmykCursor.h
//
//  The Alt+left CMYK colour readout: the bitmap cursor that carries the sampled values,
//  the strings shown beside the cursor and in the panel, and the drag update that refreshes
//  them while the button is held.
//
//  Split out of KESCMPeek.cpp on 2026-08-13. Behaviour unchanged. UI side: it builds cursor
//  bitmaps and draws through a gPort supplied by the tracker.
//
//  ★Everything the press owns lives in KESCMCmykCursor.cpp and nowhere else: which documents
//   it is sampling, the font it borrows, the strings it shows. Before the split those statics
//   were read from three files, which is what made the split necessary in the first place.
//
//========================================================================================

#ifndef __KESCMCmykCursor_h__
#define __KESCMCmykCursor_h__

#include "BaseType.h"
#include "CursorSpec.h"		// CreateCursorBitmapProc(Alt+左 CMYK のカスタムカーソル)

class IControlView;

// kTrue while a freshly sampled colour is waiting to be painted into the cursor bitmap.
bool16					KESCMTrackerHasPendingCmykCursor();

// The bitmap builder handed to the cursor manager. Paints the sampled CMYK values (and the
// check glyph) into the cursor.
CreateCursorBitmapProc	KESCMTrackerCmykCursorProc();

// kTrue when the tool cursor should be drawn black rather than knocked out white. While a
// comparison is armed the answer is black regardless of which document is under the mouse.
bool16					KESCMToolCursorShouldBeBlack(IControlView* viewUnderMouse);

// Re-sample under the mouse and refresh the cursor and the panel strings. Called from the
// tracker while the button is held. Returns kTrue when the readout changed.
bool16					KESCMTrackerUpdateCmykDrag();

// Every press starts by saying "this press shows no readout"; only the Alt branch turns it
// back on. Called first thing in KESCMTrackerRevealBegin, before the gesture is even known,
// which is why it is separate from KESCMCmykBeginPress below.
void					KESCMCmykClearPending();

// Begin / end the Alt+left colour pick. The gesture code calls in rather than reaching for
// the state, which is why the state can stay private to this file.
//
// Split out of KESCMTrackerRevealBegin/End on 2026-08-13. The bodies are the original lines,
// unchanged.
void					KESCMCmykBeginPress();
void					KESCMCmykEndPress();

// Shutdown: hand back the borrowed font and empty the strings, so the plug-in unloads with no
// live heap buffer in a static PMString. Called from KESCMUIStartup::Shutdown (KESCMUIStartup.cpp;
// it said KESCMPeekStartup - the MODEL half's service - until 2026-08-17, audit B-U6).
void					KESCMCmykShutdown();

#endif // __KESCMCmykCursor_h__
