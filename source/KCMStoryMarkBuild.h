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
//  hands a kUIPlugIn any drawing, so the marks could not reach paper or PDF while they lived
//  over there. Once the adornment moved, this had to follow -- it is the half that reads
//  IKCMStoryEditsFacade, and that list has always been the model's.
//  It reads better on this side anyway: every input it consults (the two toggles, the print
//    toggle, the comparison result, which documents are armed) is model state. The only thing
//    it ever needed from the UI is whether the tool's button is down, which arrives through
//    KCMStoryMarkSetPress.
//
//  @warning **NOTHING IS CACHED BETWEEN REFRESHES.** The Story Edits list can be rebuilt by a
//   comparison, a refresh or a row's own right-click menu at any moment, and a cache of ranges
//   would go stale silently -- the marks would keep pointing at where the words used to be.
//   Rebuilding costs a few thousand integers on a document with a few thousand edits, which is
//   not worth being wrong for.
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

	@param active kTrue while the button is down.
	@param useSourceDocument which window, when active. Ignored when active is kFalse, so that
		releasing does not have to remember where the press started.
*/
void KCMStoryMarkSetPress(bool16 active, bool16 useSourceDocument);

/** May this document's marks be drawn onto paper or into an exported PDF?

	**THE RULE IS THE PIXEL MODE'S, WORD FOR WORD:**
	  * the NEWER document prints when "Print comparison marks" is on;
	  * the OLDER one prints when "Always Show Marks on Source" is on, and does not consult the
	    print toggle at all.
	That asymmetry is not this file's invention -- IKCMCompareFacade.h states it as the
	specification where GetShowSourceMarks is declared ("ON SCREEN ONLY, where the Source one also
	prints. What comes out of the Target document is decided by Print comparison marks alone").

	@warning **IT IS ASKED FROM THE DRAWING PATH, ON BACKGROUND THREADS**, so it reads the state
	 directly rather than querying a facade off kUtilsBoss -- a Query per parcel is not what this
	 should cost. The Pixel side answers the same question the same way
	 (KCMRingAdornment.cpp, KCMMarksCouldBeTranslucent).
	@warning **which document is which is asked of the armed pair** (KCMArmedTargetDB /
	 ...SourceDB), the same source the marks themselves were built from. Not
	 KCMDrawEventHandler::sDB -- that is the PIXEL marks' document and a different variable; using
	 it here would be two answers to one question ([[one-question-one-place]]).

	@param db the document being drawn - possibly a background thread's CLONE of it, which is why
		the comparison inside is KCMIsSameDoc rather than ==.
	@return kFalse for the screen-only case, and for any document that is not one of the two.
*/
bool16 KCMStoryMarkPrintAllowedFor(IDataBase* db);

/** Is ANY document allowed to print its marks right now?

	**A COARSER GATE FOR ONE CALLER THAT CANNOT ASK THE FINER QUESTION.** GetIsActive is handed an
	IParcelShape, and **IParcelShape is not an IPMUnknown** -- there is no GetDataBase to call on
	it (measured: the compiler refuses the cast outright). So the per-parcel answer cannot name a
	document, and the best it can do is refuse the whole pass when nothing is printable at all.
	Draw still asks the per-document question, from the run's own database, so nothing prints that
	  should not. This only saves the text engine from calling Draw for a document that will refuse.

	@warning **IT MUST STAY THE OR OF THE SAME TWO FLAGS the finer question reads.** If it ever
	 grew a condition of its own, a mark could be refused here and allowed there, which reads as
	 "printing
	works for some documents and not others" with nothing to point at ([[one-question-one-place]]).
*/
bool16 KCMStoryMarkPrintPossibleAtAll();

#endif // __KCMStoryMarkBuild_h__

// End, KCMStoryMarkBuild.h.
