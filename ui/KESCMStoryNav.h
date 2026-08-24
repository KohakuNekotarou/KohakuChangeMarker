//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  What the panel's Prev / Next buttons walk in the STORY CHANGES mode: the LEAVES of the Story
//  Edits list. Added 2026-08-24 at the user's request ("StoryEdit の行を選択したのと同じ挙動").
//
//  ***** THE RULE, IN THE USER'S OWN WORDS. ***** "子供の有るのは親は除外する / 親だけのは含める":
//  a story row that has changes under it is NOT a stop - its children are, one each. A story row
//  with no children IS a stop, because there is nothing more precise to point at.
//  ⇒ every stop is a place a reader can actually be taken to, and no place is offered twice. That
//  is also what makes the count in the readout ("1/4") mean something: it counts edits, not rows.
//
//  ⚠NOT THE SET THE ARROW KEYS WALK, and the difference is deliberate. Up / down in the list steps
//  through EVERY row, parents included (KESCMStoryTreeEH), because there the reader is moving
//  through a list they can see and a heading is a thing on screen. The buttons are moving through
//  the EDITS, where a parent that has children is only the heading of them.
//
//  ★WHAT A STOP DOES IS NOT DECIDED HERE. It is the row click's own behaviour, called through
//  KESCMStoryJump - the jump, the flash mark, and the panel's message area all come from there.
//  That is the only way "同じ挙動" can stay true as either side changes: one implementation,
//  reached by two gestures ([[one-question-one-place]]).
//
//  ⚠THE PIXEL MODE NEVER COMES HERE. Its stops are the pages a comparison ringed, and they are
//  built where they always were (KESCMChangeNav.cpp). This file is asked only when the mode is
//  kKESCMModeStory.
//
//========================================================================================

#ifndef __KESCMStoryNav_h__
#define __KESCMStoryNav_h__

#include "BaseType.h"	// int32
#include "OMTypes.h"	// UID / kInvalidUID

#include <vector>

/** One stop of the Story Changes walk.

	★IDENTIFIED BY ALL THREE TOGETHER - the story, the row and the change (KESCMChangeNav.cpp's
	KESCMFindCurrentStop). The navigation remembers where it is by CONTENT rather than by index
	alone, because the list it is walking is rebuilt underneath it: "Refresh Story Comparison"
	replaces one row's children, and a fresh comparison replaces the lot.
	⚠THE ROW NUMBER WAS ADDED ON A REASON THAT TURNED OUT TO BE WRONG (2026-08-25, and withdrawn the
	  same day): "two documents that are not versions of one another can have the same uid on a
	  target row and on a source's deleted one". They cannot - KESCMStoryStamp.h defines Added as
	  "no story with this UID on the source side" and Removed as "none on the TARGET side", so a uid
	  present in both documents always pairs and never becomes either kind. ⇒ ONE UID, ONE ROW.
	★It is kept all the same, for three reasons that do hold: the uniqueness above is a property of
	  the pairing code rather than of this file; fRow is what everything downstream is asked in terms
	  of anyway (the facade, the node ids, the jump); and if any of the three disagrees the stop is
	  simply "not found", which starts the walk from the beginning - the safe way to be wrong.
*/
struct KESCMStoryNavStop
{
	int32	fRow;		// which row of the Story Edits list
	int32	fChange;	// which change under it, or -1 for a row that has none
	UID		fStoryUID;	// the row's story: what this stop is remembered by

	KESCMStoryNavStop() : fRow(-1), fChange(-1), fStoryUID(kInvalidUID) {}
};

/** Fill out with every leaf of the Story Edits list, in the order the list is in.

	★THE LIST'S OWN ORDER IS KEPT rather than being re-sorted by page. The rows are already in page
	order with the removed ones grouped after them (KESCMStoryList::Build), and a reader pressing
	Next is reading the list they can see - a walk that visited them in some other order would make
	"next" unpredictable in the one place it is on display.

	Answers an empty list when nothing has been compared, or when the comparison found no edited
	story: both leave KESCMStoryList empty, and the caller then has no stops of this kind to add.
*/
void KESCMBuildStoryNavStops(std::vector<KESCMStoryNavStop>& out);

/** Go to one stop: exactly what clicking that row in the list does, plus moving the list's own
	selection onto it.

	★THE JUMP IS THE CLICK'S (KESCMStoryJump). A change stop calls KESCMStoryJumpToChange - both
	windows move to the edit, a mark flashes over the characters, and the other side's wording goes
	to the message area. A parent stop calls KESCMStoryJumpToRow - the story's beginning, and the
	page in the message area.
	⚠A PARENT STOP FLASHES NO MARK, and that is not an omission: a row with no children is a story
	  whose edits could not be located (added, refused by the diff, or the words agree), so there is
	  no range to light up. Clicking it does the same nothing, for the same reason.

	★THE LIST'S SELECTION FOLLOWS (user's call, 2026-08-24), so the reader can see which of the
	edits they are standing on and carry on with the arrow keys from there. It is done quietly:
	with the panel closed, the section collapsed, or the tree not built, the jump still happens and
	only the highlight is skipped.

	Refusals are reported by the jump itself and are not repeated here.

	★AND THE WALK'S POSITION IS MOVED BY THE JUMP, not by the caller (KESCMNoteStoryStop, called
	from inside those two functions as soon as the row is known to exist). That is deliberate twice
	over: a CLICK on a row moves the readout by the same route, and a row that cannot be shown -
	an unplaced story, a hidden page - still counts as visited, so the walk can step off it instead
	of landing on it again.
*/
void KESCMGotoStoryNavStop(const KESCMStoryNavStop& stop);

#endif // __KESCMStoryNav_h__

// End, KESCMStoryNav.h.
