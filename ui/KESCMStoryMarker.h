//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The flash of inverted pixels over the characters a Story Edits row just jumped to.
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
//  ★THE LOOK IS KBS's: Difference blending with white, which inverts whatever is underneath, so
//  the mark shows up on any background and the glyphs stay readable (user's call: "見え方はKBSと
//  同じように反転が嬉しい"). KBSDrawEventHandler.cpp:533-546 has the reasoning, including why a
//  plain red rectangle and an XOR raster port were both rejected.
//
//  ★IT IS A POINTER, NOT A HIGHLIGHT, WHEN THE JUMP PUTS IT UP: it takes itself off the screen
//  after about a second (KESCMStoryMarkerExpiry).
//
//  ★★AND IT IS THE WHOLE ANSWER WHEN THE TOOL PUTS IT UP (2026-08-22). Holding the left button
//  over a window with the KESCM tool marks EVERY edit in it at once and keeps them up until the
//  button comes back up - the Story mode's answer to the frames the Pixel mode reveals the same
//  way (KESCMStoryPressMarks). Same adornment, same look, no countdown; and because the mark is
//  drawn on the characters by the text engine, it neither grows with the zoom nor needs a frame
//  drawn around the page (user's request: "拡大率で大きさは変わらない、ページへの外枠もいらない").
//
//========================================================================================

#ifndef __KESCMStoryMarker_h__
#define __KESCMStoryMarker_h__

#include "BaseType.h"		// bool16, int32
#include "PMReal.h"			// the opacity a press is drawn at
#include "TextID.h"			// TextIndex
#include "UIDRef.h"			// UID

#include <map>

#include "KESCMStoryMarkRanges.h"	// KESCMMarkRangeList - what a press hands over

class IDataBase;

/** Which characters of which stories are lit up in ONE document: story UID -> its ranges. */
typedef std::map<UID, KESCMMarkRangeList> KESCMStoryMarkMap;

/** The same thing for both compared documents at once: database -> what is lit up in it.

	★★BOTH AT ONCE IS NOT A LUXURY - "Show Marks on Target" and "Show Marks on Source" can be on
	together, and then the newer document's edits and the older one's have to be up at the same
	time (user's request, 2026-08-22). ⚠A press, by contrast, only ever marks the window it was
	made in; it is the same structure holding one entry. */
typedef std::map<IDataBase*, KESCMStoryMarkMap> KESCMStoryMarkDocs;

/** The mark. Two kinds of caller drive it and they are EXCLUSIVE, not additive:

	  Show()     - the jump's pointer: one range, and a countdown that takes it off again.
	  ShowDocs() - everything that stays up: the "Show Marks on ..." toggles, and the tool's press
	               while the button is held.

	★★WHY EXCLUSIVE. Difference blending inverts what is underneath, so two marks over the same
	characters would cancel each other out and leave a hole (KESCMStoryMarkRanges.h). Keeping one
	set at a time makes that impossible rather than making it something the drawing has to handle.
	⇒ ★A standing mark WINS: while one is up the jump does not add its own pointer, because every
	  character it would have pointed at is already lit.

	Everything goes through these calls so that the mark and its countdown cannot disagree. */
namespace KESCMStoryMarker
{
	/** Light up a range of one story, and start its countdown.

		Calling it again replaces the mark and restarts the countdown, so the newest jump always
		gets the full time. A range that is empty (a deletion has no width on the newer side) is
		widened by one character so that there is something to see - the mark then covers the
		character the deleted text used to stand in front of.

		@param db the document the story lives in. nil clears the mark.
		@param storyUID which story.
		@param from first character to light up.
		@param to one past the last. from == to is the caret case described above.
	*/
	void Show(IDataBase* db, UID storyUID, TextIndex from, TextIndex to);

	/** Light up every range in every document of the set, and DO NOT start a countdown - it stays
		up until Clear.

		★NO COUNTDOWN IS THE POINT. This is what the "Show Marks on ..." toggles put on screen, and
		what the tool's left button shows while it is held (user's request, 2026-08-22: "ボタンが
		押されていると表示されっぱなしにしたい" / "ツールでボタンを押さなくても常にマークが出る様に").
		What takes it down is a toggle going off or the button coming up, never the clock.

		Whatever was showing before is replaced, the jump's mark included - see the note above this
		namespace for why the two cannot both be up.

		★WHAT TO DRAW AT IS NOT A PARAMETER, AND WAS ONE UNTIL 2026-08-22. The panel's "Marks
		opacity 25% / 75%" reached the standing marks through their caller and never reached the
		jump's flash at all, which passed a hard-coded 1.0 - one setting, two answers (user's
		report: "透明度の選択が反映されるようにしてほしい。今は不透明かな？"). Both now ask the
		same private function at the moment of drawing, so the radio cannot reach one and miss the
		other ([[one-question-one-place]]).

		@param docs the ranges, per document, per story. Merged here, so the caller may hand over
			overlapping ranges in any order. An empty set clears the mark.
	*/
	void ShowDocs(const KESCMStoryMarkDocs& docs);

	/** Take the mark down now. Safe when there is none. */
	void Clear();

	/** True while a mark is up - the adornment's own fast path, and what the expiry timer asks
	    before doing anything. */
	bool16 IsShowing();

	/** True while what is up is a STANDING mark - one ShowDocs put there, rather than a jump's
		pointer.

		★★IT EXISTS SO THAT NOBODY KEEPS A SECOND COPY OF THIS ANSWER. KESCMStoryPressMarks used to
		remember "did I put the current mark up" in a static of its own, which is the same fact
		written down twice ([[one-question-one-place]]) - and the two drifted the moment anything
		else called Clear(): the double click's does (KESCMStorySelectChange), and after it the
		other copy still said a standing mark was on screen when there was none.
		⇒ Ask the mark, do not remember it. */
	bool16 IsShowingPersistent();

	/** Take the mark down for good (application shutdown). After this, Show() does nothing.

		★It exists for the same reason KBS's does: Clear() repaints the document the mark was in,
		and by teardown time that document may be half gone. */
	void Shutdown();
}

#endif // __KESCMStoryMarker_h__

// End, KESCMStoryMarker.h.
