//========================================================================================
//
//  IKCMPageFlagsFacade.h
//
//  The two per-page flags the user sets by hand: Register (this page has no partner -- it was
//  added or removed) and Check (a tick the user puts on a page they have dealt with).
//
//  Both flags live in the model because both change what the comparison means: Register
//  changes the page pairing, and Check is drawn into the marks and into the Pages panel
//  thumbnails. The menu items that set them stay in the UI.
//
//  READING the flags is not here -- it is on IKCMMarkData, with the rest of the read-only
//  questions. This interface is the writing half plus the two menu-state calls.
//
//========================================================================================

#ifndef __IKCMPageFlagsFacade_h__
#define __IKCMPageFlagsFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// Project includes:
#include "KCMBoundaryID.h"	// IID_IKCMPAGEFLAGSFACADE. The boundary header rather than KCMID.h,
							// for the reason given at the same spot in IKCMCompareFacade.h.
#include "OMTypes.h"			// UID -- which page a paw sits on
#include "PMReal.h"			// the paw's coordinates and its size
#include "KCMPageFlagState.h"	// KCMPageToggleState. A header of TYPES ONLY, which is what a
							// header the UI includes has to be: this used to reach the type
							// through KCMPageMap.h, whose 13 model-side free functions the UI
							// could then see and could not link to.

class IKCMPageFlagsFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMPAGEFLAGSFACADE };

	// ---- the two toggles -----------------------------------------------------------------

	/** Toggle Register on the pages currently selected in the Pages panel. Re-pairs the two
		documents and re-compares, then notifies. Which pages are selected is read inside --
		the menu handler does not gather them. */
	virtual void	ToggleRegisterForSelection() = 0;

	/** Toggle Check on the pages currently selected in the Pages panel. Only meaningful while a
		comparison is armed; Stop clears every tick. */
	virtual void	ToggleCheckForSelection() = 0;

	// ---- what the context menu should look like ------------------------------------------
	//
	// Both items use kCustomEnabling, so the menu asks before it is drawn: whether the item is
	// enabled, whether it shows a tick or the intermediate dash, and -- for Register -- which of
	// the two documents the selection is in, because that decides the wording.
	//
	// THESE RETURN A STATE AND TOUCH NO MENU. Naming IActionStateList here would put a UI type on
	// a model plug-in's boundary; the SDK's own model/UI pair does not do it either
	// (ICusCondTxtFacade has no menu-state method at all). SetNthActionState, SetNthActionName and
	// the label strings are UI text and live in ui/KCMActionComponent.cpp. The counting is here.
	virtual KCMPageToggleState	GetRegisterToggleState() = 0;
	virtual KCMPageToggleState	GetCheckToggleState() = 0;

	// ---- (the JSON store went on 2026-09-07) ---------------------------------------------
	//
	// SaveChecksAndRegister() and LoadChecksAndRegister() stood here. They wrote the ticks, the
	// paws and the registrations into a private file beside the document. The ticks and the paws
	// are written INTO the document now, as they are made, so the file held a second copy of the
	// same thing; the registrations are an input to one comparison and are not kept past it.
	// ⚠**This is a vtable, so removing them moved every slot below.** Both halves of the plug-in
	//   are rebuilt together, and Kohaku InDesign MCP -- the one other product that compiles a KCM
	//   facade header -- uses IKCMCompareFacade and not this one (checked before removing them).

	// ---- the cat-paw stamps --------------------------------------------------------------
	//
	// ★They belong to this facade because they are the same KIND of thing as Register and Check:
	//   a mark **the reader puts there by hand**, held outside the document. What differs is only
	//   the grain -- a paw sits at a point on a page rather than flagging the whole page.
	// ★★★AND BECAUSE THE UI CANNOT REACH THE MODEL ANY OTHER WAY. model and UI are two DLLs, so
	//   ui/KCMPawTracker.cpp calling KCMPawStampToggleAt() directly does not link (measured
	//   2026-09-04: LNK2019, three unresolved symbols). **Every crossing is a facade method.**

	/** Place a paw at (x, y) on that page, in one of the two colours (a KCMPawColour: red or blue,
		whichever the tool is holding -- Shift+Alt swaps it). baseHalf is half a paw's size on that
		page, the same value the lift takes.
		★x and y are measured from the PAGE'S TOP-LEFT in points, never in pasteboard coordinates
		  -- KCMPawStamp.h carries the measurement that makes that a requirement.
		★★IT ONLY ADDS, AND IT WILL NOT STACK. Placing and lifting are two intentions and
		  therefore two gestures; and a press landing on a paw already there does nothing, both at
		  the user's request on 2026-09-04.
		@param text the word to put beside it, or empty. An Alt press asks the reader for one
		  (2026-09-07); every other gesture passes nothing.
		@return kTrue when one was placed, kFalse when a paw was already under that point. */
	virtual bool16	PawStampPlaceAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                                int32 colour, const PMReal& baseHalf, const PMString& text) = 0;

	/** Lift the paw under (x, y) -- Shift + press (without Alt, which asks for a word).
		baseHalf is half a paw's size on that page (PawHalfSizeForPage); a paw is judged over a
		SQUARE of that, so what can be seen is what can be lifted.
		@return kTrue when one was lifted, kFalse when the press landed on none. */
	virtual bool16	PawStampLiftAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                               const PMReal& baseHalf) = 0;

	/** How many paws this document holds. The tool says it on the status line after every press,
		which is what tells "placed" and "lifted" apart while nothing is drawn yet. */
	virtual int32	PawStampCount(IDataBase* db) = 0;

	/** Drop every paw on ONE page -- what Shift + DOUBLE click asks for (2026-09-07). The page's
		tick is untouched.
		@return how many went; 0 for a page carrying none, which is not a failure. */
	virtual int32	PawStampClearPage(IDataBase* db, UID pageUID) = 0;

	/** Half a paw's drawn size on that page, in points.
		★★THE ONE PLACE THE SIZE COMES FROM: the tool asks for its hit box and the drawing side
		  asks for its picture, so what can be seen is exactly what can be lifted. Answers 0 when
		  the page cannot be measured, which the caller reads as "do not stamp here". */
	virtual PMReal	PawHalfSizeForPage(IDataBase* db, UID pageUID) = 0;

	// ---- clearing one document's marks (the two flyout items, 2026-09-04) -------------------
	// ⚠**Which document is the caller's to name**, through IKCMCompareFacade::GetActiveDocDB --
	//   the one place KCM asks what is in front ([[document-activation-is-presentation]]). These
	//   take the answer rather than working it out again, so the greying and the command cannot
	//   end up disagreeing about which document they mean.

	/** Does this document hold any tick -- what greys "Clear Checks in This Document". */
	virtual bool16	PageCheckHasAny(IDataBase* db) = 0;

	/** "Clear Checks in This Document": drop that document's ticks, and refresh the Pages panel's
		thumbnails and the layout view with them.
		@return how many ticks went, for the status line. */
	virtual int32	ClearChecksInDoc(IDataBase* db) = 0;

	/** "Clear Cat Paws in This Document": the same for the paws.
		⚠Paws reach no thumbnail (the drawing side excludes them from the isThumb branch), so only
		  the layout view is refreshed.
		@return how many paws went. */
	virtual int32	ClearPawsInDoc(IDataBase* db) = 0;

	// ---- the marks the DOCUMENT itself carries (2026-09-07) --------------------------------
	//
	// ★★**THERE IS NO "SAVE" ANY MORE, BECAUSE THERE IS NOTHING TO SAVE.** A tick or a paw goes
	//   into the document at the moment it is made, and Ctrl+Z takes it out again; the labels ARE
	//   the marks. `SaveMarksToDocument` was the door while writing was a separate act, and it went
	//   with the act (the flyout item too -- ActionID kKCMUIPrefix + 55 stays retired).
	//   Putting the marks back when a document opens still happens on its own, in the after-open
	//   responder. The reasoning is in KCMPageMarksDoc.h and KCMPageMarksCmd.h.

	/** "Clear Marks from Document": take every mark of OURS off every page in one undoable step,
		leaving all other labels exactly as they were.
		@return how many pages were changed, or -1 when the document could not be used. */
	virtual int32	ClearMarksFromDocument(IDataBase* db) = 0;
};

#endif // __IKCMPageFlagsFacade_h__
