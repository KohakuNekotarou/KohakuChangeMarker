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

class UIDList;		// the Pages panel's selection, as the UI read it
class IControlView;	// the view a paw is being placed in -- compared by the drawing side, never dereferenced

class IKCMPageFlagsFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMPAGEFLAGSFACADE };

	// ---- the two toggles -----------------------------------------------------------------

	/** Toggle Register on the pages selected in the Pages panel. Re-pairs the two documents and
		re-compares, then notifies.
		`pages` IS THE SELECTION THE UI READ (KCMReadSelectedPages, ui/KCMThumbnailRefresh.h - masters
		  left out for Register): the Pages panel is a UI part, which the model does not reach for. */
	virtual void	ToggleRegisterForSelection(const UIDList& pages) = 0;

	/** Toggle Check on the pages selected in the Pages panel (`pages`, read by the UI with the masters
		in). A tick is the reader's own mark: no comparison is needed. */
	virtual void	ToggleCheckForSelection(const UIDList& pages) = 0;

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
	// `pages` = the selection the UI read for the item, as the toggles above take it.
	virtual KCMPageToggleState	GetRegisterToggleState(const UIDList& pages) = 0;
	virtual KCMPageToggleState	GetCheckToggleState(const UIDList& pages) = 0;

	// ---- the cat-paw stamps --------------------------------------------------------------
	//
	// They belong to this facade because they are the same KIND of thing as Register and Check:
	//   a mark **the reader puts there by hand**. What differs is only the grain -- a paw sits at
	//   a point on a page rather than flagging the whole page.
	// AND BECAUSE THE UI CANNOT REACH THE MODEL ANY OTHER WAY. model and UI are two DLLs, so the
	//   paw tool (ui/KCMPawTracker.cpp) calling the model's paw functions straight does not link
	//   (measured: LNK2019). **Every crossing is a facade method.**

	/** Place a paw at (x, y) on that page, in one of the two colours (a KCMPawColour: red or blue,
		whichever the tool is holding -- Shift+Alt swaps it), turned `angle` degrees clockwise from
		straight up (0 = upright, what a click places; a drag passes the way it went --
		KCMPawStamp::fAngle). baseHalf is half a paw's size on that page, the same value the lift takes.
		x and y are measured from the PAGE'S TOP-LEFT in points, never in pasteboard coordinates
		  -- KCMPawStamp.h carries the measurement that makes that a requirement.
		IT ONLY ADDS, AND IT WILL NOT STACK. Placing and lifting are two intentions and
		  therefore two gestures; and a press landing on a paw already there does nothing - both the
		  author's call.
		@param text the word to put beside it, or empty. An Alt press asks the reader for one; every
		  other gesture passes nothing.
		@return kTrue when one was placed, kFalse when a paw was already under that point - or when
		  the WRITE failed, which the model has already said on the status line ("Could not write
		  the marks into the document."). PawStampIsAt tells the two apart: after a failed write no
		  paw is under the point, so the caller says nothing over the model's sentence. */
	virtual bool16	PawStampPlaceAtAngle(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                                     int32 colour, const PMReal& baseHalf, const PMString& text,
	                                     const PMReal& angle) = 0;

	/** Lift the paw under (x, y) -- Shift + press (without Alt, which asks for a word).
		baseHalf is half a paw's size on that page (PawHalfSizeForPage); a paw is judged over a
		SQUARE of that, so what can be seen is what can be lifted.
		@return kTrue when one was lifted, kFalse when the press landed on none - or when the WRITE
		  failed (already said by the model, as above), which leaves the paw under the point:
		  PawStampIsAt tells the two apart. */
	virtual bool16	PawStampLiftAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                               const PMReal& baseHalf) = 0;

	/** How many paws this document holds. The tool says it on the status line after every press,
		which is what tells "placed" and "lifted" apart while nothing is drawn yet. */
	virtual int32	PawStampCount(IDataBase* db) = 0;

	/** Drop every paw on ONE page -- what Shift + DOUBLE click asks for. The page's
		tick is untouched.
		@return how many went; 0 for a page carrying none, which is not a failure; -1 when the
		  WRITE failed and none went (already said by the model, as above). */
	virtual int32	PawStampClearPage(IDataBase* db, UID pageUID) = 0;

	/** Half a paw's drawn size on that page, in points.
		THE ONE PLACE THE SIZE COMES FROM: the tool asks for its hit box and the drawing side
		  asks for its picture, so what can be seen is exactly what can be lifted. Answers 0 when
		  the page cannot be measured, which the caller reads as "do not stamp here". */
	virtual PMReal	PawHalfSizeForPage(IDataBase* db, UID pageUID) = 0;

	// ---- clearing one document's marks (the two flyout items) -------------------
	// ⚠**Which document is the caller's to name**, through IKCMCompareFacade::GetActiveDocDB --
	//   the one place KCM asks what is in front (memory document-activation-is-presentation). These
	//   take the answer rather than working it out again, so the greying and the command cannot
	//   end up disagreeing about which document they mean.

	/** Does this document hold any tick -- what greys "Clear Checks in This Document". */
	virtual bool16	PageCheckHasAny(IDataBase* db) = 0;

	/** "Clear Checks in This Document": drop that document's ticks, and refresh the Pages panel's
		thumbnails and the layout view with them.
		@return how many ticks went, for the status line (0 when the write failed - the model has
		  said that one itself). */
	virtual int32	ClearChecksInDoc(IDataBase* db) = 0;

	/** "Clear Cat Paws in This Document": the same for the paws.
		⚠Paws reach no thumbnail (the drawing side excludes them from the isThumb branch), so only
		  the layout view is refreshed.
		@return how many paws went (0 when the write failed, as above). */
	virtual int32	ClearPawsInDoc(IDataBase* db) = 0;

	// ---- the marks the DOCUMENT itself carries --------------------------------
	//
	// **THERE IS NO "SAVE", BECAUSE THERE IS NOTHING TO SAVE.** A tick or a paw goes into the
	//   document at the moment it is made, and Ctrl+Z takes it out again; the labels ARE the marks,
	//   so no file beside the document holds a second copy. The registrations are an input to one
	//   comparison and are not kept past it. ActionID kKCMUIPrefix + 55 (the flyout item that once
	//   wrote the marks) stays retired.
	//   Putting the marks back when a document opens happens on its own, in the after-open
	//   responder. The reasoning is in KCMPageMarksDoc.h and KCMPageMarksCmd.h.

	// ---- the paw that points where it was dragged ------------------------------

	/** Is there a paw under (x, y) on that page -- what PawStampPlaceAtAngle refuses on, asked without placing: the
		tool asks it at the press, so a press on a paw is refused before any drag is followed. */
	virtual bool16	PawStampIsAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                             const PMReal& baseHalf) = 0;

	/** Show the paw being placed, turned to `angle`, while the button is down -- drawn on screen in `view`
		alone and kept nowhere (KCMPawPreviewSet). Called again as the drag turns it. ⚠The caller repaints. */
	virtual void	PawPreviewSet(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y, int32 colour,
	                              const PMReal& angle, const IControlView* view) = 0;

	/** Take the paw being placed away again (release, abort). ⚠The caller repaints. */
	virtual void	PawPreviewClear() = 0;
};

#endif // __IKCMPageFlagsFacade_h__
