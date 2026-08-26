//========================================================================================
//
//  KCMChangeNav.h
//
//  Walking "the places worth looking at" in order -- what the panel's Prev and Next buttons do.
//  What is walked (KCMBuildStops in KCMChangeNav.cpp is the statement of record):
//    1. pages with a change on them (a red/blue ring) = those with a key in sEntries. Only while
//       a comparison is running IN THE PIXEL MODE.
//    2. overset "+" places = Find Overset's sOversetLocs. These can be walked on their own,
//       without a comparison; one place is one stop.
//    3. THE LEAVES OF THE STORY EDITS LIST -- one edit, or a row with no children at all. Rows
//       that DO have children are not stops (the rule and the reason are in KCMStoryNav.h). Only
//       while a comparison is running IN THE STORY MODE.
//  Added/Removed (registered, a green "/") and Overflow (a red "/") are NOT walked, at the user's
//  request.
//  1 and 3 are mutually exclusive, decided by the mode (the Story mode rasterises no page at all,
//  so 1 is empty to begin with). 2 FOLLOWS EITHER OF THEM whenever Find Overset is on.
//  A changed page is scrolled to its centre without touching the zoom; an overset stop scrolls to
//  its "+" point. Neither selects anything.
//  Case 3 travels differently: it calls THE SAME IMPLEMENTATION A CLICK ON THE LIST ROW DOES
//  (jump, a brief mark, the message line -- KCMStoryNav.cpp into KCMStoryJump.cpp).
//
//========================================================================================

#ifndef __KCMChangeNav_h__
#define __KCMChangeNav_h__

#include "BaseType.h"	// bool16
#include "OMTypes.h"	// UID

class IDataBase;
class IControlView;

// Unless view is already showing spreadUID, switch it with the official kSetSpreadCmdBoss.
// A view that is already showing it costs nothing, so this is cheap to call repeatedly.
// SCROLLING ALONE WILL NOT REACH ANOTHER SPREAD, a master spread least of all: it lands on empty
// pasteboard instead. "Switch when it is a different spread" is the official form -- SnapTracker
// makes no exception for masters either.
// Both Prev/Next (in this .cpp) and the layout view synchroniser (KCMViewSync.cpp) call it, so
// that one judgement is not written down twice.
// Returns kTrue when it actually switched (kFalse when it was already showing it, or on
// failure).
bool16 KCMEnsureViewShowsSpread(IControlView* view, IDataBase* db, UID spreadUID);

// Scroll the layout view to the next or previous place worth looking at. With no comparison
// running, or nothing to walk, nothing is scrolled and the panel's status line says so. Safe to
// call any number of times.
void KCMGotoNextChange();
void KCMGotoPrevChange();

// Forget where the walk is (the page last visited). Called on Start -- a full recomparison, so
// the documents are swapped -- and on Stop.
// A UID means nothing outside its own database, so this is what stops a page UID from the old
// document happening to match one in the new and the walk resuming from the wrong place.
void KCMResetNav();

// Rebuild the position readout between Prev and Next out of the current set of stops and where
// the walk is, and send it to the panel. As with KESCL's UpdateNavWidgets, this is called from
// every path that can change the set, so the readout follows without Prev or Next being pressed.
// The callers are:
//   - KCMModelChangeObserver ... a comparison rebuilt or cleared, an overset scan, and the Story
//     Edits list being rebuilt (a Refresh Story Comparison changes a row's child count, which
//     changes N)
//   - KCMActionComponent     ... Find Overset going off (its places leave the walk)
//   - KCMPanelObserver       ... the panel's contents being rebuilt (KCMApplyPanelInfo)
//   - KCMChangeNav.cpp itself ... every exit of the walk, and KCMNoteStoryStop
// What is shown:
//   - nothing to walk in any document -> empty (no comparison AND Find Overset off)
//   - a document but no stops         -> "/"
//   - N stops, none visited yet       -> "1/N" (shown as soon as a comparison starts)
//   - standing on the k'th            -> "k/N"
// Note that WITHOUT A COMPARISON BUT WITH FIND OVERSET ON, "1/N" does appear -- what is walked is
// then the overset places.
void KCMRefreshNavPosition();

// Record that the walk now stands on a Story Edits row, so that k/N reflects it. (The user asked
// for this: selecting a StoryEdit row and having Prev/Next not follow felt wrong.)
//
// THE ONLY CALLERS ARE THE TWO JUMP FUNCTIONS, KCMStoryJumpToRow and KCMStoryJumpToChange
// (KCMStoryJump.cpp). A click on a row, an arrow key walking the list, and Prev/Next all end up
// in one of those two once they know where they are going, so where the walk stands is decided
// in a single place. Recording it on the Prev/Next side as well would give one row two answers.
//
// The argument means three different things, and THE THIRD IS THE POINT OF THE FUNCTION:
//   - changeIndex >= 0             ... stand on that change (it is a stop in its own right)
//   - changeIndex < 0, row without children ... stand on the row (the row itself is the stop)
//   - changeIndex < 0, row WITH children    ... STAND AT THE ENTRANCE TO ITS FIRST CHILD. The
//     readout shows that child's number, but THE WALK HAS NOT GONE THERE YET. The next Next goes
//     to it (it is not skipped); Prev goes to the stop before it. That is exactly the rule that
//     makes a comparison show "1/N" the moment it starts -- what is shown is where Next will go
//     (the user's decision). A parent row with children is not a stop (KCMStoryNav.h), so this
//     entrance is the only place to stand.
//
// IN THE PIXEL MODE THIS DOES NOTHING: what is walked there is pages, and a list row is not among
// them -- touching it would make clicking a row move the page walk.
// A rowIndex outside the current list does nothing either (a click arriving just after the list
// was rebuilt).
void KCMNoteStoryStop(int32 rowIndex, int32 changeIndex);

// The jump a Story Edits row makes: put the first frame of that story in the centre of the view.
//   - the spread of frameUID is shown first, so this reaches another spread, a master, or the
//     pasteboard
//   - THE SOURCE WINDOW COMES ALONG TOO, but what it is lined up on is THE SAME STORY (storyUID),
//     not the same page: when that story sits somewhere else in the two versions, both windows
//     still show the story. That is deliberately unlike Prev/Next, which looks a page up in the
//     pairing table. For a story with no counterpart in the Source (an Added one) only the Target
//     moves. While "Sync Layout Views" is on, the Source is NOT moved by hand -- Sync already
//     carries the Target's scroll over, and doing both would double it.
//   - the Pages panel follows on both sides (pageUID is what resolves that; kInvalidUID, a frame
//     that is on no page, leaves the Pages panel where it is)
//   - the Prev/Next position k/N is NOT affected: this is a different route, so it does not move
//     the walk
// Returns kTrue when at least one view could be scrolled. Defined in KCMChangeNav.cpp.
//
// focusIndex / sourceFocusIndex: pass them to centre THAT CHARACTER -- where the caret would
// stand -- rather than the start of the story. THE TWO SIDES ARE TAKEN SEPARATELY because the
// same edit is at different character positions in the two versions: pass a Change's fTargetStart
// and fSourceStart straight through.
// kInvalidTextIndex (the default) means the start of the story as before, and that is what A
// PARENT STORY ROW passes, the row pointing at the story itself.
// @warning THE CALLER MUST HOLD AN IDataBase::SaveRestoreModifiedState ON db (the new side):
//   producing the point needs composition, and composition dirties the document
//   (IKCMStoryEditsFacade::GetStoryPointAt). The guard for the OLD side is held by this function
//   itself, since this is the only place that touches the old document.
bool16 KCMGotoStoryFrame(IDataBase* db, UID frameUID, UID pageUID, UID storyUID,
	TextIndex focusIndex = kInvalidTextIndex, TextIndex sourceFocusIndex = kInvalidTextIndex);

#endif // __KCMChangeNav_h__
