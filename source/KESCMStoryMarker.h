//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The coloured wash laid under the characters a Story Edits row just jumped to.
//
//  ★★★A GLOBAL TEXT ADORNMENT, NOT A DRAW EVENT (2026-08-20, user's call: "マーカーはグローバル
//  テキストアドーンメントで出して見て欲しい"). The mark is drawn ON THE CHARACTERS, by the text
//  engine, at the moment it draws them:
//    * Draw() is handed the waxRun, its render data and its glyphs - there is nothing to look up
//      and no coordinate system to convert into. Which characters a run covers comes from
//      TextOrigin() + GetCharCount(); where a sub-range of them sits comes from
//      MapCharsToGlyphs() + GetGlyphDrawPosition(), which is how the product's own spelling
//      squiggle underlines one word.
//    * ⇒ It follows vertical text, rotated frames and threaded columns for free. KBS's marker is a
//      rectangle in pasteboard coordinates worked out by hand (KBSDrawEventHandler), which is the
//      part that has to be right on every one of those.
//  ★NOTHING IS ATTACHED TO ANYTHING. A global adornment is drawn as if it were on every run;
//  the document is not touched, nothing is saved into the .indd, and no recomposition is asked for
//  (IGlobalTextAdornment.h:42-51). Its on/off state is this file's statics and nothing else.
//
//  ★★★THE LOOK IS A HIGHLIGHTER: an opaque wash in the mark colour, laid UNDER the glyphs, at the
//  strength the panel's "Marks opacity" asks for (2026-08-24). ⚠**IT WAS AN INVERSION UNTIL THEN**
//  - Difference blending with white, KBS's look (user's call: "見え方はKBSと同じように反転が嬉しい")
//  - and that is worth knowing before anyone proposes it again, because it was tried the whole way
//  down and cannot be printed:
//    * Difference is a TRANSPARENCY effect, and a text adornment has no way to declare itself to the
//      flattener (there is no IAdornmentFlattenerUsage on a service boss) - so outputs that keep
//      transparency dropped the drawing and outputs that flatten kept the paint and dropped the blend;
//    * and even after that was solved, **an exported or printed page draws the text LAST**, so the
//      ground inverted and the glyphs did not (measured: ground 255 -> 6, glyph core 0 -> 0) - black
//      on black, with the changed words as the only unreadable ones.
//  ⇒ A wash needs no transparency at all: nothing to flatten, nothing to declare, PDF 1.3 behaves
//    like 1.4, and the screen and the page show the same picture. See KESCMStoryMarker.cpp's Draw.
//
//  ★IT IS A POINTER, NOT A HIGHLIGHT, WHEN THE JUMP PUTS IT UP: it takes itself off the screen
//  after about a second (KESCMStoryMarkerExpiry).
//  ★★AND IT POINTS IN BOTH WINDOWS (2026-08-23, user's request: "ジャンプしたときソースの方でも
//  マークを一瞬出して欲しい"). The jump already moved both of them, and the diff has already
//  worked out where the same edit sits on the older side - the flash simply names the two ranges
//  it was given instead of one (KESCMStoryJump).
//
//  ★★AND IT IS THE WHOLE ANSWER WHEN THE TOOL PUTS IT UP (2026-08-22). Holding the left button
//  over a window with the KESCM tool marks EVERY edit in it at once and keeps them up until the
//  button comes back up - the Story mode's answer to the frames the Pixel mode reveals the same
//  way (KESCMStoryMarkBuild). Same adornment, same look, no countdown; and because the mark is
//  drawn on the characters by the text engine, it neither grows with the zoom nor needs a frame
//  drawn around the page (user's request: "拡大率で大きさは変わらない、ページへの外枠もいらない").
//
//========================================================================================

#ifndef __KESCMStoryMarker_h__
#define __KESCMStoryMarker_h__

#include "BaseType.h"		// bool16, int32
#include "PMReal.h"			// ⚠NOT needed BY THIS HEADER any more (2026-08-23): the opacity
							//  stopped being a parameter when the two kinds of mark were separated, and
							//  nothing here names a PMReal. It stays because KESCMStoryMarker.cpp reaches
							//  PMReal THROUGH this file (it holds the opacity in a static); moving it to
							//  the .cpp that uses it is a tidy-up, not something to do in passing.
#include "TextID.h"			// TextIndex
#include "UIDRef.h"			// UID

#include "KESCMStoryMarkDocs.h"		// KESCMStoryMarkDocs, and the rule for putting two sets together

class IDataBase;

/** The mark. Two kinds of caller drive it, and since 2026-08-23 they are exclusive PER DOCUMENT
	rather than outright:

	  ShowFlash()    - the jump's pointer: a range in each window, and a countdown that takes them
	                   off again.
	  ShowStanding() - everything that stays up: the "Show Marks on ..." toggles, and the tool's
	                   press while the button is held.

	★★WHY EXCLUSIVE AT ALL. The rule was written when the mark was an INVERSION: two marks over the
	same characters cancelled out and left a hole, so keeping one kind per document made that
	impossible rather than something the drawing had to handle (KESCMStoryMarkRanges.h).
	⚠**THE WASH OF 2026-08-24 CANNOT PUNCH A HOLE** - painting the same rectangle twice gives the
	same colour - so the rule is no longer load-bearing for correctness. It is kept because it is
	still what the reader wants: a document showing every edit does not also want one of them
	pointed at, and the flash going up in the OTHER window is the whole of what a jump adds there.
	⇒ ★In a document with a standing mark, the standing mark is all there is: the jump adds no
	  pointer there, because every character it would point at is already lit.
	⚠★★AND THE OTHER DOCUMENT IS A SEPARATE ANSWER. Until 2026-08-23 one flag stood for both, so
	  turning "Show Marks on Target" on silenced the flash in the SOURCE window as well - where
	  nothing was standing and the reader had just asked to be shown something (bug A3). The rule
	  now lives in one place and is asked per document (KESCMStoryMarkDocs.h).

	Everything goes through these calls so that the mark and its countdown cannot disagree. */
namespace KESCMStoryMarker
{
	/** Add one story's range to a set a jump is about to show.

		★IT IS HERE RATHER THAN AT THE CALLER SO THAT A DELETION LOOKS THE SAME EVERYWHERE. A range
		that comes through empty is the place something was deleted from - there is no character on
		this side to wash - and it is shown as a caret, exactly as the standing marks show one
		(KESCMStoryMarkBuild). A caller that built its own ranges would be the second place that
		decision is made ([[one-question-one-place]]).

		@param docs the set being built up. Ranges may be added in any order and may overlap.
		@param db the document the story lives in. nil and kInvalidUID are ignored, so a caller may
			hand over a window that is not open without testing first.
		@param storyUID which story.
		@param from first character to light up.
		@param to one past the last. from == to is the caret case described above.
	*/
	void AddFlashRange(KESCMStoryMarkDocs& docs, IDataBase* db, UID storyUID,
					   TextIndex from, TextIndex to);

	/** Show a jump's pointer, and start the countdown that takes it away.

		Calling it again replaces the flash and restarts the countdown, so the newest jump always
		gets the full time. An empty set is the same as ClearFlash().

		⚠It does not touch the standing marks - a document that has one keeps it, and this shows in
		whatever documents do not.
	*/
	void ShowFlash(const KESCMStoryMarkDocs& docs);

	/** Light up every range in every document of the set, and DO NOT start a countdown - it stays
		up until ClearStanding.

		★NO COUNTDOWN IS THE POINT. This is what the "Show Marks on ..." toggles put on screen, and
		what the tool's left button shows while it is held (user's request, 2026-08-22: "ボタンが
		押されていると表示されっぱなしにしたい" / "ツールでボタンを押さなくても常にマークが出る様に").
		What takes it down is a toggle going off or the button coming up, never the clock.

		Whatever was standing before is replaced. A jump's flash is left alone in any document this
		does not claim, and hidden in the ones it does claim - see the note above this namespace.

		★WHAT TO DRAW AT IS NOT A PARAMETER, AND WAS ONE UNTIL 2026-08-22. The panel's "Marks
		opacity 25% / 75%" reached the standing marks through their caller and never reached the
		jump's flash at all, which passed a hard-coded 1.0 - one setting, two answers (user's
		report: "透明度の選択が反映されるようにしてほしい。今は不透明かな？"). Both now ask the
		same private function at the moment of drawing, so the radio cannot reach one and miss the
		other ([[one-question-one-place]]).

		@param docs the ranges, per document, per story. Merged here, so the caller may hand over
			overlapping ranges in any order. An empty set takes the standing marks down.
	*/
	void ShowStanding(const KESCMStoryMarkDocs& docs);

	/** Take a jump's pointer down now, leaving the standing marks alone. Safe when there is none.

		★★IT IS NOT "Clear()" ANY MORE, AND THAT IS THE POINT (2026-08-23). One call that took
		everything down meant the double click had to put the standing marks back afterwards by
		asking for a full refresh - a repair for damage it had just done itself, and one that was
		missing until the bug recheck of 2026-08-22 found the toggles going bare (A2). Naming which
		of the two kinds is coming down cannot go wrong that way. */
	void ClearFlash();

	/** Take the standing marks down now, leaving a jump's pointer alone. Safe when there is none.

		★★ASK THE MARK, DO NOT REMEMBER IT. KESCMStoryPressMarks (the file that became
		KESCMStoryMarkBuild in 2026-08-23's migration) used to keep "did I put the current
		mark up" in a static of its own and test it before clearing - the same fact written down
		twice ([[one-question-one-place]]), and the two drifted the moment anything else cleared the
		mark. There is nothing to test now: this takes down what it owns and nothing else. */
	void ClearStanding();

	/** Take everything down for good (application shutdown). After this, nothing shows again.

		★It exists for the same reason KBS's does: taking a mark down repaints the document it was
		in, and by teardown time that document may be half gone. */
	void Shutdown();
}

#endif // __KESCMStoryMarker_h__

// End, KESCMStoryMarker.h.
