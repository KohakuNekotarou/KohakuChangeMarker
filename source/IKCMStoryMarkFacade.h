//========================================================================================
//
//  IKCMStoryMarkFacade.h
//
//  Putting the Story mode's marks up and taking them down: the UI's window onto something that
//  lives in the model.
//
//  WHY THIS BOUNDARY EXISTS AT ALL. The mark is a global TEXT adornment, and it began life in the
//  UI plug-in because it was the flash a jump puts up - a screen thing, by definition. That made
//  it impossible to print or export, and not by an oversight that could be patched where it was:
//  **the UI's File > Export > PDF runs in the background, and a kUIPlugIn is never handed the
//  drawing at all** (measured; no warning - the plug-in simply behaves as if it were not there).
//  Opening the kPrinting guard in the old place would have got the mark onto paper and never into
//  an exported PDF, which is the half that matters for a print workflow.
//
//  MOVING IT COSTS NOTHING AT THE REGISTRATION END, which is worth knowing before anyone worries
//  about the page item adornment next door: a global text adornment is declared as a SERVICE
//  (IID_IK2SERVICEPROVIDER + kGlobalTextAdornmentServiceImpl), and **the service registry resolves
//  services separately in every execution context, background threads included**. There is no
//  equivalent for page item adornments, which is why kKCMRingAdornmentStartupBoss exists and why
//  nothing like it is needed here (KCM.fr states the contrast in full).
//
//  NO STL ACROSS THIS BOUNDARY, AND THAT IS WHY ShowJumpFlash HAS SIX PARAMETERS. The marker's own
//  vocabulary is a nested std::map (database -> story -> ranges), which must not cross a DLL edge.
//  The rest of this plug-in's boundary answers the same way - IKCMStoryEditsFacade hands out one
//  flat Row at a time rather than the list - and a jump only ever names TWO ranges (the same story
//  seen in each of the two documents), so nothing variable-length has to travel.
//
//  THE OLDER DOCUMENT IS NOT PASSED, BUT THE ROW'S OWN ONE IS, AND THE ASYMMETRY IS REAL. Which
//  document a Story Edits row is READ OUT OF depends on the row: a Removed story exists only in
//  the older version, so the jump reads it there (ui/KCMStoryJump.cpp's KCMStoryJumpToRow picks
//  the database with `removedRow ? GetArmedSourceDB() : GetArmedTargetDB()`). That is a fact about
//  the row, which is the UI's to know. Whether the OTHER window is open, and which database it is,
//  is a fact about the comparison - the model's - so it is not passed in and must not be
//  ([[one-question-one-place]]).
//  @warning an interface that took no database at all, and assumed the row was always the
//  target's, would put a Removed story's flash in the wrong window.
//
//========================================================================================

#ifndef __IKCMStoryMarkFacade_h__
#define __IKCMStoryMarkFacade_h__

#include "IPMUnknown.h"

// General includes:
#include "TextID.h"				// TextIndex
#include "UIDRef.h"				// UID

// Project includes:
#include "KCMBoundaryID.h"	// IID_IKCMSTORYMARKFACADE

class IDataBase;

/** The Story mode's marks, driven from the UI.

	EVERY CALL IS "SOMETHING HAPPENED", NOT "DRAW THIS". The model works out what should be lit
	from four inputs it can all reach by itself - the two "Show Marks on ..." toggles, the
	"Print comparison marks" toggle, whether the tool's button is held, and the comparison result -
	so the UI never builds a set of ranges. That is what keeps the rule about what is visible in
	one place instead of two.
*/
class IKCMStoryMarkFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMSTORYMARKFACADE };

	/** Work out what should be lit right now and put it up, replacing whatever was standing.

		Idempotent, and cheap enough to call on any change: it re-reads the Story Edits list rather
		than caching ranges, because that list is rebuilt by a comparison, a refresh or a row's own
		right-click menu at any moment, and a stale cache would keep pointing at where the words
		used to be.

		@warning a jump's flash is NOT touched. The two kinds of mark are kept apart on purpose;
		this one owns only what stands (KCMStoryMarker.h).

		Callers: the flyout toggles (including "Print comparison marks"), the press below, and the
		model-change observer.
	*/
	virtual void	Refresh() = 0;

	/** The tool's left button went down over one of the two windows, or came back up.

		"PRESSED", NOT "SHOWN". While the button is held, the window it was pressed in is turned
		round - marks that were off come on, and marks that were on go off - which is what lets the
		reader look at the plain page under the ones they asked to keep. This call says only which
		window, and the rule is applied inside.

		@param active kTrue while the button is down.
		@param useSourceDocument kTrue when the press was over the older version's window.
	*/
	virtual void	SetPress(bool16 active, bool16 useSourceDocument) = 0;

	/** Point at one edit in both windows for about a second (a Story Edits row was double-clicked).

		Calling it again replaces the flash and restarts the countdown, so the newest jump always
		gets the full time.

		AN EMPTY RANGE IS A CARET, AND SAYING SO IS THE MODEL'S JOB. A deletion has no width on the
		side it was deleted from and an insertion has none on the older side; both come through with
		from == to, and both are drawn as a bar standing where the caret would stand. The caller
		does not have to know which case it is holding.

		@warning THE TWO SIDES GET DIFFERENT NUMBERS. The same edit sits at a different character
		position in each version and the diff has already worked both out; handing the target's
		index to the older document would light unrelated characters over there rather than failing.

		@param db the document the ROW was read out of - the older one for a Removed story, the
			newer one otherwise. Not necessarily the target: see the note above this class.
		@param storyUID the story, which names the same story in BOTH documents (the whole Story
			mode rests on that - the diff pairs stories by uid and the double click selects by uid).
		@param from first character to light in `db`.
		@param to one past the last. from == to is the caret case.
		@param sourceFrom first character to light in the OLDER document, which is pointed at as
			well whenever it is open and is not `db` itself. The model decides that; the caller
			always passes the numbers.
		@param sourceTo one past the last there.
	*/
	virtual void	ShowJumpFlash(IDataBase* db, UID storyUID,
								  TextIndex from, TextIndex to,
								  TextIndex sourceFrom, TextIndex sourceTo) = 0;

	/** Take a jump's pointer down now, leaving the standing marks alone. Safe when there is none. */
	virtual void	ClearJumpFlash() = 0;

	// NO ShutdownMarks() HERE, AND THE ABSENCE IS DELIBERATE. Teardown happens entirely on the
	// model side (kKCMPeekStartupBoss's Shutdown calls KCMStoryMarker::Shutdown directly, which it
	// can, being in the same plug-in), so the UI has nothing to say about it.
	// **A method on a boundary that nobody calls is a promise nobody keeps** - the same rule
	// IKCMStoryEditsFacade.h states about its own missing Build().
};

#endif // __IKCMStoryMarkFacade_h__

// End, IKCMStoryMarkFacade.h.
