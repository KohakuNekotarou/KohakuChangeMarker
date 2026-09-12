//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The one call the rest of the plug-in makes to the Story Edits list on screen.
//
//  Everything else about the tree - the hierarchy adapter and the row widget manager - is reached
//  only by the tree framework, through the interfaces KCMUI.fr puts on kKCMStoryTreeWidgetBoss.
//  What the model side needs is a way to say "the list changed, draw it again", and that is this.
//
//========================================================================================

#ifndef __KCMStoryTree_h__
#define __KCMStoryTree_h__

#include "BaseType.h"
#include "KCMResourceKinds.h"	// KCMResourceChangeKind - KCMResourceRowHasChildren (a types-only model header, the one IKCMResourcesFacade.h includes)

/** Redraw the Story Edits list from whatever KCMStoryList holds right now.

	Safe to call at any time: with the panel closed, the section never built, or the list empty, it
	finds the step that is missing and returns without doing anything. Callers therefore do not have
	to know whether the panel is open - which is the point, since the comparison runs the same way
	either way.
*/
void KCMStoryTreeRebuild();

/** kTrue when the list is showing DEFINITIONS (the Resources mode) rather than stories.

	★★★THIS IS THE ONE PLACE THE LIST ASKS WHICH MODE IS ON, and everything that draws the list
	asks it through here: the hierarchy adapter (how many rows, and have they children), the widget
	manager (what goes in the three cells) and the section heading. Written out at each of those
	sites instead, the same judgement would sit in three files and they would drift
	([[one-question-one-place]]).

	★**The list itself is not duplicated.** The Resources mode reuses the row the Story Edits list
	already draws in the PIXEL mode - a flat row of three plain text cells - because that row is
	exactly the shape a definition needs:

	    Story       UID    | the story's text     | what kind of change
	    Resources   kind   | the key              | Added / Removed / Changed

	⚠So the second level is never grown here: a definition has no children, and the row templates,
	  heights and hit-testing of the change rows are left to the Story mode alone.
*/
bool16 KCMListShowsResources();

/** Whether a Resources row of this change kind has attribute rows under it. ★THE ONE PLACE
	(2026-09-12): the tree adapter asks it to grow the children, and the Kind column's fit asks it
	to decide whose attribute names to measure. ⚠They disagreed until then - the adapter grew
	children for Changed rows only (the user's call, KCMStoryTreeAdapter.cpp), while the fit
	measured every row's attributes, and an Added definition lists EVERY attribute it has (each
	present on one side) - so a single "+ ParagraphStyle" row fitted the column to a hidden
	"ExtendedKeyboardShortcut" plus its indent, and the user saw a Kind column twice as wide as
	its one word (work/キャプチャ.PNG). Only Changed rows have children. */
inline bool16 KCMResourceRowHasChildren(KCMResourceChangeKind what)
{
	return (what == kKCMResourceChanged) ? kTrue : kFalse;
}

class IControlView;

/** How wide the list's LEFT column is, for the mode the list is showing.

	★★THE TWO MODES PUT DIFFERENT THINGS THERE, and one width cannot serve both. The Story mode's
	left cell holds a UID - five or six digits - and 40px is right for it; the Resources mode holds
	an element name ("ColorGroupSwatch", "ParagraphStyle") and an attribute name ("PointSize"), and
	40px clips every one of them (2026-09-09, the user reading the list: "some are cut off").

	★A SHARED, WIDER COLUMN WAS THE OTHER ANSWER AND WAS REJECTED (the user's call): the middle
	column is the one that gives way, so widening it in both modes would take 80px off the story
	text for a column of digits that never needed it.

	⚠So the .fr's numbers are the STORY mode's, and the Resources mode is applied on top of them at
	  run time - which is why every place that lays these columns out has to call the function below
	  rather than trust what the resource said.
*/
int32 KCMListLeftColumnWidth();

/** Fit the Resources mode's Kind column to the widest name the list will show, and remember it.

	★★★**THE WIDTH IS MEASURED, NOT CHOSEN** (2026-09-10, the user's call). It was a fixed 120, and
	before that a drag handle that was built and taken out again - because the reason the number was
	wrong in the first place is that nobody knows the right one until the list is on screen. What is
	measured is every string the column will hold: each definition's kind, and each attribute name
	PLUS the indent its child row starts at.

	★**Call it when the list is BUILT, not per row.** KCMListLeftColumnWidth is asked once for every
	row and every heading on every lay-out; measuring in there would be O(rows^2) strings. It is
	called from KCMStoryTreeRebuild, which is the one place a list comes into being.
	⚠**Does nothing outside the Resources mode**: the Story mode's 40px is the .fr's own.
	⚠**An empty list leaves the width alone** - there is nothing to fit to, and snapping back to the
	  floor would make the headings jump between one comparison and the next.
*/
void KCMRecomputeListLeftColumnWidth();

/** The clamp the fit is put through: never below a floor, never so wide that the Definition column
	is starved. ★**The ceiling is read off the panel as it stands now**, not written down - a
	constant would be right at one panel width and wrong at every other.
*/
int32 KCMClampListLeftColumnWidth(int32 px);

/** Lay the left and middle columns out for the mode the list is showing.

	Both the heading band (KCMStorySection.cpp) and the rows themselves (KCMStoryTreeWidgetMgr.cpp)
	call this, which is what keeps a heading over the column it names: the .fr can only state one
	pair of numbers, and the agreement between the two is enforced by nothing else.

	★**ABSOLUTE POSITIONS, NEVER RELATIVE ONES.** Row widgets are recycled, so this runs many times
	on the same widget; "move it right by N" would accumulate and the column would walk across the
	panel. Every edge written here is computed from constants.

	★★**THE MIDDLE CELL'S RIGHT EDGE IS TAKEN FROM THE RIGHT CELL'S LEFT EDGE**, never computed and
	never left alone. It cannot be computed here: it moves with the panel's width. And leaving it
	alone is what went wrong the first time (measured 2026-09-09) - a recycled child row kept a right
	edge from some earlier layout and its Definition cell ran 96px past the Change column and out of
	the panel. The right cell is bound to the panel's right edge, so **its** left edge is always
	where the middle column has to stop: asking it makes the two columns meet by construction
	instead of by agreement.

	★★**ONLY THE LEFT CELL IS INDENTED ON A CHILD ROW, AND IT TOOK TWO GOES TO GET THERE**
	  (2026-09-09). The first build indented the whole row 16px and the user asked for it out
	  ("the child row's Kind and Def parts are pushed right - make them the same as the parent");
	  the second had no indent at all, and the user asked for a little back, on the Kind cell alone
	  ("shift just the Kind a bit to the right").
	  ⇒ **The values stay in one column and only the NAME steps in.** That is the reading the
	    columns are for: a reader scans the Definition column straight down, parents and children
	    alike, and the indent says which of the names is a child without moving anything else.
	  ★The left cell's RIGHT edge does not move with it - the cell narrows instead - so the two
	    columns still meet in the same place on every row.

	@param leftCell    the Kind / UID cell, or its heading. nil is ignored.
	@param middleCell  the Definition / story-text cell, or its heading. nil is ignored.
	@param rightCell   the Change cell, or its heading. Not moved - only read. nil leaves the middle
	                   cell's right edge alone.
	@param leftIndent  how far in the LEFT cell alone starts. 0 for a top-level row and for the
	                   headings.
*/
void KCMApplyListColumnWidths(IControlView* leftCell, IControlView* middleCell,
							  IControlView* rightCell, int32 leftIndent);

#endif // __KCMStoryTree_h__

// End, KCMStoryTree.h.
