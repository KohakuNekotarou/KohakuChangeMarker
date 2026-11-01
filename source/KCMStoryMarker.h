//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The coloured wash laid under the characters a Story Edits row just jumped to.
//
//  **A GLOBAL TEXT ADORNMENT, NOT A DRAW EVENT.** The mark is drawn ON THE CHARACTERS, by the
//  text engine, at the moment it draws them:
//    * Draw() is handed the waxRun, its render data and its glyphs -- there is nothing to look
//      up and no coordinate system to convert into. Which characters a run covers comes from
//      TextOrigin() + GetCharCount(); where a sub-range of them sits comes from
//      MapCharsToGlyphs() + GetEscapementAt(), which is how the product's own spelling squiggle
//      finds one word (it adds the widths up itself).
//    * So it follows vertical text, rotated frames and threaded columns for free. A marker worked
//      out by hand as a rectangle in pasteboard coordinates has to be right on every one of those
//      (KFC's was one, until it moved to this same mechanism).
//    * Text on a path is the one case that is not free: its glyphs do not stand where its run does,
//      and the run's shape is asked instead (KCMStoryMarkerBoxOnPath).
//  **NOTHING IS ATTACHED TO ANYTHING.** A global adornment is drawn as if it were on every run;
//  the document is not touched, nothing is saved into the .indd, and no recomposition is asked
//  for (IGlobalTextAdornment.h says so where the interface itself is described). Its on/off
//  state is this file
//
//  **THE LOOK IS A HIGHLIGHTER:** an opaque wash in the mark colour, laid UNDER the glyphs, at
//  the strength the panel's "Marks opacity" asks for.
//  @warning **IT WAS AN INVERSION BEFORE THAT** -- Difference blending with white, KFC's look --
//   and that is worth knowing before anyone proposes it again, because it was tried the whole
//   way down and cannot be printed:
//    * Difference is a TRANSPARENCY effect, and a text adornment has no way to declare itself to
//      the flattener (there is no IAdornmentFlattenerUsage on a service boss) -- so outputs that
//      keep transparency dropped the drawing and outputs that flatten kept the paint and dropped
//      the blend;
//    * and even after that was solved, **an exported or printed page draws the text LAST**, so
//      the ground inverted and the glyphs did not (measured: ground 255 -> 6, glyph core 0 -> 0)
//      -- black on black, with the changed words as the only unreadable ones.
//  A wash needs no transparency at all: nothing to flatten, nothing to declare, PDF 1.3 behaves
//    like 1.4, and the screen and the page show the same picture
//
//  **IT IS A POINTER, NOT A HIGHLIGHT, WHEN THE JUMP PUTS IT UP:** it takes itself off the
//  screen after about a second (KCMStoryMarkerExpiry).
//  **AND IT POINTS IN BOTH WINDOWS.** The jump already moved both of them, and the diff has
//  already worked out where the same edit sits on the older side -- the flash simply names the
//  two ranges it was given instead of one (KCMStoryJump).
//
//  **AND IT IS THE WHOLE ANSWER WHEN THE TOOL PUTS IT UP.** Holding the left button over a
//  window with the KCM tool marks EVERY edit in it at once and keeps them up until the button
//  comes back up -- the Story mode's answer to the frames the Pixel mode reveals the same way
//  (KCMStoryMarkBuild). Same adornment, same look, no countdown; and because the mark is drawn
//  on the characters by the text engine, it neither grows with the zoom nor needs a frame drawn
//  around the page.
//
//========================================================================================

#ifndef __KCMStoryMarker_h__
#define __KCMStoryMarker_h__

#include "KCMStoryMarkDocs.h"		// KCMStoryMarkDocs, and the rule for putting two sets together (TextIndex, UID, uint8 come with it)

class IDataBase;
class IDocumentList;

/** The mark. Two kinds of caller drive it, and they are exclusive PER DOCUMENT rather than
	outright:

	  ShowFlash()    - the jump's pointer: a range in each window, and a countdown that takes them
	                   off again.
	  ShowStanding() - everything that stays up: the "Show Marks on ..." toggles, and the tool's
	                   press while the button is held.

	**WHY EXCLUSIVE AT ALL.** The rule was written when the mark was an INVERSION: two marks over
	the same characters cancelled out and left a hole, so keeping one kind per document made that
	impossible rather than something the drawing had to handle (KCMStoryMarkRanges.h).
	@warning **the wash cannot punch a hole** -- painting the same rectangle twice gives the same
	 colour -- so the rule is no longer load-bearing for correctness. It is kept because it is
	 still what the reader wants: a document showing every edit does not also want one of them
	 pointed at, and the flash going up in the OTHER window is the whole of what a jump adds there.
	In a document with a standing mark, the standing mark is all there is: the jump adds no
	  pointer there, because every character it would point at is already lit.
	@warning **and the other document is a separate answer.** One flag used to stand for both, so
	 turning "Always Show Marks on Target" on silenced the flash in the SOURCE window as well --
	 where nothing was standing and the reader had just asked to be shown something (bug A3). The
	 rule now lives in one place and is asked per document (KCMStoryMarkDocs.h).

	Everything goes through these calls so that the mark and its countdown cannot disagree. */
namespace KCMStoryMarker
{
	/** The range that marks the gap at `at` in this story: a caret. A deletion has no width on the side
		it was deleted from, and an insertion has none on the older side, so there is no character to
		wash -- a thin bar stands where the words were, or went in. The jump's flash and the standing
		marks both take it from here, so the two say the same thing about the same gap.

		**IN FRONT OF THE NEXT CHARACTER, WHERE THERE IS ONE THAT DRAWS.** Two places have none:
		  * the end of the story: the caret would stand on the final carriage return, which reaches no
		    wax run (it draws nothing, and the last run may not even carry it), so deleting the last
		    characters drew no bar at all. It stands after the last visible character instead, which is
		    where a caret clicked there would stand. TotalLength counts that final return, so the last
		    visible character is one before it.
		  * in front of a table: a table's own characters are composed as the table frame's line, whose
		    run has no glyphs, so a word typed in front of a table showed no bar on the Source. The
		    drawing's own step back (a caret whose character draws nothing) cannot help, because the
		    character before is in another run. It stands after the last character before the table
		    (KCMCaretOnTableChars says what was measured).

		@param db the document. Not nil.
		@param storyUID which story. Not kInvalidUID.
		@param at where the gap is.
	*/
	KCMMarkRange CaretAt(IDataBase* db, UID storyUID, TextIndex at);

	/** Add one story's range to a set a jump is about to show.

		A range that comes through empty is a gap, and what marks it is CaretAt.

		@param docs the set being built up. Ranges may be added in any order and may overlap.
		@param db the document the story lives in. nil and kInvalidUID are ignored, so a caller may
			hand over a window that is not open without testing first.
		@param storyUID which story.
		@param from first character to light up.
		@param to one past the last. from == to is the caret case described above.
		@param colour the Track mode's author colour; none of its own = the Mark colour,
			which is what every other caller wants.
	*/
	void AddFlashRange(KCMStoryMarkDocs& docs, IDataBase* db, UID storyUID,
					   TextIndex from, TextIndex to,
					   const KCMMarkColour& colour = KCMMarkColour());

	/** Show a jump's pointer, and start the countdown that takes it away.

		Calling it again replaces the flash and restarts the countdown, so the newest jump always
		gets the full time. An empty set is the same as ClearFlash().

		@warning it does not touch the standing marks -- a document that has one keeps it, and this
		 shows in whatever documents do not.

		@param docs the ranges, taken over rather than copied: hand a local set in with std::move.
	*/
	void ShowFlash(KCMStoryMarkDocs docs);

	/** Light up every range in every document of the set, and DO NOT start a countdown - it stays
		up until the next call replaces it.

		**NO COUNTDOWN IS THE POINT.** This is what the "Show Marks on ..." toggles put on screen, and
		what the tool's left button shows while it is held. What takes it down is a toggle going off
		or the button coming up, never the clock.

		Whatever was standing before is replaced. A jump's flash is left alone in any document this
		does not claim, and hidden in the ones it does claim -- see the note above this namespace.

		**WHAT TO DRAW AT IS NOT A PARAMETER.** The drawing reads the panel's "Marks opacity" for itself,
		for both kinds of mark, so the radio cannot reach one and miss the other.

		@param docs the ranges, per document, per story. Merged here, so the caller may hand over
			overlapping ranges in any order. An empty set takes the standing marks down, and leaves a
			jump's pointer alone. Taken over rather than copied: hand a local set in with std::move.
	*/
	void ShowStanding(KCMStoryMarkDocs docs);

	/** Take a jump's pointer down now, leaving the standing marks alone. Safe when there is none.

		**IT IS NOT "Clear()", AND THAT IS THE POINT.** One call that took everything down meant the
		double click (gone 2026-10-25) had to put the standing marks back afterwards by asking for a full refresh -- a
		repair for damage it had just done itself, and one that was missing until a bug recheck found
		the toggles going bare (A2). Naming which of the two kinds is coming down cannot go wrong that
		way. */
	void ClearFlash();

	/** Forget every mark of a document that is no longer open - standing and flash alike. Nothing is
		repainted: what an open document shows depends on its own entries alone.

		⚠**INDESIGN CRASHED WITHOUT THIS** (the third crash of the night): the
		marks' maps are keyed by IDataBase*, and nothing took a closed document's entry out of them
		until the UI's deferred rebuild came round. In between, composing ANY other document - a
		new one being created, say - reached the adornment, which walked the map and asked
		KCMIsSameDoc about the dead pointer: a virtual call on a destroyed object, purecall,
		straight from KCMStoryMarkerRangesFor. The sweep on kAfterCloseDoc (KCMHandleDocsClosed)
		cleared every other piece of the comparison's state and never this one.

		**POINTERS ARE COMPARED, NEVER DEREFERENCED**: a closed IDataBase* is dead memory
		([[uidref-reuse-after-close]] - and its address can already belong to a NEW document, which
		is the other reason the entry has to go: left in, the marks would come up on that document).
		Liveness is IDocumentList::FindDocByDataBase plus the lent Source (KCMIsDbAlive).

		@param docList the application's document list, on the MAIN thread only - on a background
			thread every database is a clone and nothing would look alive (KCMHandleDocsClosed says
			the same at its entry). Nil does nothing. */
	void ForgetClosedDocs(IDocumentList* docList);

	/** Take everything down for good (application shutdown). After this, nothing shows again.

		It exists for the same reason KFC's does: taking a mark down repaints the document it was
		in, and by teardown time that document may be half gone. */
	void Shutdown();
}

#endif // __KCMStoryMarker_h__

// End, KCMStoryMarker.h.
