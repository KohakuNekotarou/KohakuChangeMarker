//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Working out WHAT should be lit in Story mode, as against KESCMStoryMarker.cpp which knows how
//  to draw it.
//
//  ★★★MOVED HERE FROM ui/KESCMStoryPressMarks.cpp ON 2026-08-23, with the adornment it feeds.
//  The reason is the adornment's, not this file's: the UI's File > Export > PDF runs in the
//  background and never hands a kUIPlugIn any drawing, so the marks could not reach paper or PDF
//  while they lived over there. Once the adornment moved, this had to follow - it is the half that
//  reads IKESCMStoryEditsFacade, and that list has always been the model's.
//  ⇒ ★It reads better on this side anyway: every input it consults (the two toggles, the print
//    toggle, the comparison result, which documents are armed) is model state. The only thing it
//    ever needed from the UI is whether the tool's button is down, which now arrives through
//    KESCMStoryMarkSetPress.
//
//  ⚠NOTHING IS CACHED BETWEEN REFRESHES. The Story Edits list can be rebuilt by a comparison, a
//  refresh or a row's own right-click menu at any moment, and a cache of ranges would go stale
//  silently - the marks would keep pointing at where the words used to be. Rebuilding costs a few
//  thousand integers on a document with a few thousand edits, which is not worth being wrong for.
//
//========================================================================================

#ifndef __KESCMStoryMarkBuild_h__
#define __KESCMStoryMarkBuild_h__

#include "BaseType.h"		// bool16

class IDataBase;

/** Work out what should be lit right now from the toggles, the press and the comparison result,
	and put it up as the standing mark. Idempotent.

	⚠It only ever replaces the STANDING marks. A jump's flash belongs to the jump and is left alone
	(KESCMStoryMarker.h keeps the two apart per document).

	★Callers are all events, never "draw this": a flyout toggle moved, the tool's button moved, the
	comparison was re-run, the Story Edits model was rebuilt.
*/
void KESCMStoryMarkRefresh();

/** The tool's left button went down over one of the two windows, or came back up.

	★★"PRESSED", NOT "SHOWN". While the button is held, the window it was pressed in is turned
	round - marks that were off come on, marks that were on go off - and that XOR is applied inside
	KESCMStoryMarkRefresh. This call says only which window, and the caller does not have to know
	what is currently showing there.

	@param active kTrue while the button is down.
	@param useSourceDocument which window, when active. Ignored when active is kFalse, so that
		releasing does not have to remember where the press started.
*/
void KESCMStoryMarkSetPress(bool16 active, bool16 useSourceDocument);

#endif // __KESCMStoryMarkBuild_h__

// End, KESCMStoryMarkBuild.h.
