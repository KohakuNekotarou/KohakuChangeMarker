//========================================================================================
//
//  KCMPawWordDialog.h
//
//  Asking for the word that goes beside a cat paw, and placing the paw with it -- a moment AFTER
//  the tracker has let go.
//
//  ***** WHY A TIMER AND NOT SIMPLY A DIALOG *****
//
//  ⚠★★★**A MODAL DIALOG MUST NOT BE OPENED FROM INSIDE A TRACKER.** BeginTracking runs with the
//    tracker on the event-handler stack and the mouse captured; a modal opened there runs its loop
//    underneath both. This is not a guess and it is not new to KCM: Kohaku InDesign MCP's blue
//    pencil reached the same conclusion and answers it the same way (KIDMCPUIPencilDialog.h), and
//    **this plug-in's own tool-button flyout already waits on a one-shot ICallbackTimer for
//    exactly this reason** (ui/KCMToolButtonEH.cpp). One millisecond after BeginTracking returns is
//    after the base has released the capture.
//
//  ★ICallbackTimer is the ONE sanctioned delay ([[icallbacktimer]]); the standing rule against
//    timers and idle tasks ([[avoid-timers-and-idle-tasks]]) has this as its named exception, and
//    the reason above is what makes this an instance of it rather than a convenience.
//
//  ***** WHY MODAL *****
//
//  The word belongs to the paw just put down. A modeless box would let a second paw be placed
//  while the first is still unnamed, and then nothing could say which word belongs to which paw.
//  (The same reasoning, in the same words, as the blue pencil's.)
//
//  ***** WHAT OK, EMPTY AND CANCEL MEAN *****
//
//    OK with a word  ... the paw is placed carrying it
//    OK with nothing ... the paw is placed with no word -- an Alt press that changed its mind
//                        about the word still meant to place a paw
//    Cancel          ... nothing is placed at all
//  ⚠**Cancel has to be distinguishable from an empty OK**, and it is: CDialogController's
//    ApplyDialogFields is called for OK and never for Cancel, so the flag is set there and nowhere
//    else. Reading the box alone could not tell the two apart.
//
//========================================================================================

#ifndef __KCMPawWordDialog_h__
#define __KCMPawWordDialog_h__

#include "BaseType.h"
#include "OMTypes.h"		// UID
#include "PMReal.h"

class IDataBase;

namespace KCMPawWordDialog
{
	/** Remember where the paw is to go and arm the timer. The timer's callback opens the box and,
		unless it is cancelled, places the paw through the facade.

		@param db IN the document pressed on.
		@param pageUID IN the page pressed on.
		@param x, y IN where, measured FROM THE PAGE'S TOP-LEFT in points (KCMPawStamp.h says why
		  it can be nothing else).
		@param colour IN which colour the tool is holding.
		@param baseHalf IN half a paw's size on that page -- the same value the placing takes.

		⚠A second press arriving while one is still waiting REPLACES it. It cannot happen in
		  practice (the wait is a millisecond and the box that follows is modal), and replacing is
		  the safe answer to the impossible case: two dialogs would be worse than one. */
	void AskAndPlaceLater(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                      int32 colour, const PMReal& baseHalf);

	/** Drop the timer and forget the waiting press. Called from the UI half's shutdown.
		⚠**Not optional.** ICallbackTimer holds a raw function pointer into this plug-in; one left
		  armed while the plug-in unloads is a crash, and the timer is the reason this file has a
		  shutdown at all. */
	void Shutdown();
}

#endif // __KCMPawWordDialog_h__

// End, KCMPawWordDialog.h.
