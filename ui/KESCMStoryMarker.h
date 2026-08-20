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
//  ★IT IS A POINTER, NOT A HIGHLIGHT: it takes itself off the screen after about a second
//  (KESCMStoryMarkerExpiry).
//
//========================================================================================

#ifndef __KESCMStoryMarker_h__
#define __KESCMStoryMarker_h__

#include "BaseType.h"		// bool16, int32
#include "TextID.h"			// TextIndex
#include "UIDRef.h"			// UID

class IDataBase;

/** The jump marker. Only the jump should drive this; everything else asks through Show / Clear so
    that the mark and its countdown stay in step. */
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

	/** Take the mark down now. Safe when there is none. */
	void Clear();

	/** True while a mark is up - the adornment's own fast path, and what the expiry timer asks
	    before doing anything. */
	bool16 IsShowing();

	/** Take the mark down for good (application shutdown). After this, Show() does nothing.

		★It exists for the same reason KBS's does: Clear() repaints the document the mark was in,
		and by teardown time that document may be half gone. */
	void Shutdown();
}

#endif // __KESCMStoryMarker_h__

// End, KESCMStoryMarker.h.
