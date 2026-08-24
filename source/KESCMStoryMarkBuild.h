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

/** May this document's marks be drawn onto paper or into an exported PDF?

	★★THE RULE IS THE PIXEL MODE'S, WORD FOR WORD (user's choice, 2026-08-23):
	  * the NEWER document prints when "Print comparison marks" is on;
	  * the OLDER one prints when "Always Show Marks on Source" is on, and does not consult the print
	    toggle at all.
	That asymmetry is not this file's invention - IKESCMCompareFacade.h:146-147 already states it as
	the specification ("ON SCREEN ONLY, where the Source one also prints. What comes out of the
	Target document is decided by Print comparison marks alone").

	⚠★★★IT IS ASKED FROM THE DRAWING PATH, ON BACKGROUND THREADS, so it reads the state directly
	rather than querying a facade off kUtilsBoss - a Query per parcel is not what this should cost.
	The Pixel side answers the same question the same way (KESCMRingAdornment.cpp:253).
	⚠**Which document is which is asked of the armed pair** (KESCMArmedTargetDB / ...SourceDB), the
	same source the marks themselves were built from. ⚠Not KESCMDrawEventHandler::sDB - that is the
	PIXEL marks' document and a different variable; using it here would be two answers to one
	question ([[one-question-one-place]]).

	@param db the document being drawn - possibly a background thread's CLONE of it, which is why
		the comparison inside is KESCMIsSameDoc rather than ==.
	@return kFalse for the screen-only case, and for any document that is not one of the two.
*/
bool16 KESCMStoryMarkPrintAllowedFor(IDataBase* db);

/** Is ANY document allowed to print its marks right now?

	★★A COARSER GATE FOR ONE CALLER THAT CANNOT ASK THE FINER QUESTION. GetIsActive is handed an
	IParcelShape, and **IParcelShape is not an IPMUnknown** - there is no GetDataBase to call on it
	(measured 2026-08-23: "指示された型は関連がありません"). So the per-parcel answer cannot name a
	document, and the best it can do is refuse the whole pass when nothing is printable at all.
	⇒ Draw still asks the per-document question, from the run's own database, so nothing prints that
	  should not. This only saves the text engine from calling Draw for a document that will refuse.

	⚠**IT MUST STAY THE OR OF THE SAME TWO FLAGS the finer question reads.** If it ever grew a
	condition of its own, a mark could be refused here and allowed there, which reads as "printing
	works for some documents and not others" with nothing to point at ([[one-question-one-place]]).
*/
bool16 KESCMStoryMarkPrintPossibleAtAll();

#endif // __KESCMStoryMarkBuild_h__

// End, KESCMStoryMarkBuild.h.
