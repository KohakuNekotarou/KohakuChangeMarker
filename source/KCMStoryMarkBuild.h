//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Working out WHAT should be lit in Story mode, as against KCMStoryMarker.cpp which knows how
//  to draw it.
//
//  **THE ADORNMENT AND THIS HALF BOTH LIVE ON THE MODEL SIDE**, and the reason is the
//  adornment's, not this file's: the UI's File > Export > PDF runs in the background and never
//  hands a kUIPlugIn any drawing, so the marks could not reach paper or PDF from over there. This half
//  reads IKCMStoryEditsFacade, and that list is the model's.
//  Every input it consults (the two toggles, the print toggle, the comparison result, which documents are armed)
//  is model state. The only thing it needs from the UI is whether the tool's button is down, which arrives through
//  KCMStoryMarkSetPress.
//
//  @warning **NOTHING IS CACHED BETWEEN REFRESHES.** The Story Edits list can be rebuilt by a
//   comparison, a refresh or a row's own right-click menu at any moment, and a cache of ranges
//   would go stale silently -- the marks would keep pointing at where the words used to be.
//   Rebuilding reads every change of every row (GetChange copies its strings as well: by estimate a few milliseconds
//   for a few thousand edits, not measured), which is not worth being wrong for.
//
//========================================================================================

#ifndef __KCMStoryMarkBuild_h__
#define __KCMStoryMarkBuild_h__

#include "BaseType.h"		// bool16

class IDataBase;

/** Work out what should be lit right now from the toggles, the press and the comparison result,
	and put it up as the standing mark. Idempotent.

	@warning it only ever replaces the STANDING marks. A jump's flash belongs to the jump and is
	 left alone (KCMStoryMarker.h keeps the two apart per document).

	Callers are all events, never "draw this": a flyout toggle moved, the tool's button moved, the
	comparison was re-run, the Story Edits model was rebuilt.
*/
void KCMStoryMarkRefresh();

/** The tool's left button went down over one of the two windows, or came back up.

	**"PRESSED", NOT "SHOWN".** While the button is held, the window it was pressed in is turned
	round -- marks that were off come on, marks that were on go off -- and that XOR is applied
	inside KCMStoryMarkRefresh. This call says only which window, and the caller does not have to
	know what is currently showing there.
	With "Print comparison marks" on, a press has nothing to turn round: what prints stands, as the Pixel mode's frames do.

	@param active kTrue while the button is down.
	@param useSourceDocument which window, when active. Ignored when active is kFalse, so that
		releasing does not have to remember where the press started.
*/
void KCMStoryMarkSetPress(bool16 active, bool16 useSourceDocument);

/** May this document's marks be drawn onto paper or into an exported PDF?

	**"Print comparison marks" ALONE DECIDES, FOR BOTH DOCUMENTS** - the Pixel mode's rule (spec map MK-14, SMK-18). The
	"Always Show Marks" toggles are about the screen and hold no veto over the output.

	@warning **IT IS ASKED FROM THE DRAWING PATH, ON BACKGROUND THREADS**, so it reads the state
	 directly rather than querying a facade off kUtilsBoss -- a Query per run is not what this
	 should cost. The Pixel side answers the same question the same way
	 (KCMRingAdornment.cpp, KCMMarksCouldBeTranslucent).
	@warning **which document is which is asked of the armed pair** (KCMArmedTargetDB /
	 ...SourceDB), the same source the marks themselves were built from. Not
	 KCMDrawEventHandler::sDB -- that is the PIXEL marks' document and a different variable; using
	 it here would be two answers to one question.

	@param db the document being drawn - possibly a background thread's CLONE of it, which is why
		the comparison inside is KCMIsSameDoc rather than ==.
	@return kFalse for the screen-only case, and for any document that is not one of the two.
*/
bool16 KCMStoryMarkPrintAllowedFor(IDataBase* db);

#endif // __KCMStoryMarkBuild_h__

// End, KCMStoryMarkBuild.h.
