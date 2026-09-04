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

	// ---- the JSON store ------------------------------------------------------------------

	/** "Save Check & Register": write both flags for the armed pair next to the Target document
		and report where they went on the status line. Armed only; says so if not. */
	virtual void	SaveChecksAndRegister() = 0;

	/** "Load Check & Register": read that file back, apply Register to both documents,
		re-compare, then restore the ticks -- but only on pages that still carry a mark. Armed
		only. */
	virtual void	LoadChecksAndRegister() = 0;

	// ---- the cat-paw stamps --------------------------------------------------------------
	//
	// ★They belong to this facade because they are the same KIND of thing as Register and Check:
	//   a mark **the reader puts there by hand**, held outside the document. What differs is only
	//   the grain -- a paw sits at a point on a page rather than flagging the whole page.
	// ★★★AND BECAUSE THE UI CANNOT REACH THE MODEL ANY OTHER WAY. model and UI are two DLLs, so
	//   ui/KCMPawTracker.cpp calling KCMPawStampToggleAt() directly does not link (measured
	//   2026-09-04: LNK2019, three unresolved symbols). **Every crossing is a facade method.**

	/** Place a paw at (x, y) on that page, or lift the one already sitting there.
		★x and y are measured from the PAGE'S TOP-LEFT in points, never in pasteboard coordinates
		  -- KCMPawStamp.h carries the measurement that makes that a requirement.
		hitRadius is half the drawn size, so "already there" means inside the paw's own square.
		@return kTrue when one was PLACED, kFalse when one was lifted (or nothing happened). */
	virtual bool16	PawStampToggleAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                                 const PMReal& hitRadius) = 0;

	/** How many paws this document holds. The tool says it on the status line after every press,
		which is what tells "placed" and "lifted" apart while nothing is drawn yet. */
	virtual int32	PawStampCount(IDataBase* db) = 0;

	/** Half a paw's drawn size on that page, in points.
		★★THE ONE PLACE THE SIZE COMES FROM: the tool asks for its hit box and the drawing side
		  asks for its picture, so what can be seen is exactly what can be lifted. Answers 0 when
		  the page cannot be measured, which the caller reads as "do not stamp here". */
	virtual PMReal	PawHalfSizeForPage(IDataBase* db, UID pageUID) = 0;
};

#endif // __IKCMPageFlagsFacade_h__
