//========================================================================================
//
//  IKESCMPageFlagsFacade.h
//
//  The two per-page flags the user sets by hand: Register (this page has no partner -- it was
//  added or removed) and Check (a tick the user puts on a page they have dealt with).
//
//  Created 2026-08-13 for the model/UI split (Stage 1), Task 13.
//
//  Both flags live in the model because both change what the comparison means: Register
//  changes the page pairing, and Check is drawn into the marks and into the Pages panel
//  thumbnails. The menu items that set them stay in the UI.
//
//  ★READING the flags is not here -- it is on IKESCMMarkData, with the rest of the read-only
//  questions. This interface is the writing half plus the two menu-state calls.
//
//========================================================================================

#ifndef __IKESCMPageFlagsFacade_h__
#define __IKESCMPageFlagsFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// Project includes:
#include "KESCMID.h"
#include "KESCMPageMap.h"	// KESCMPageToggleState (a type only -- types cross the boundary fine,
							// the same way IKESCMMarkData borrows KESCMOversetLoc)

class IKESCMPageFlagsFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKESCMPAGEFLAGSFACADE };

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
	// ★★2026-08-15 (API audit B2): THESE USED TO TAKE AN IActionStateList AND WRITE INTO IT.
	// The header said so itself and left a note: "as a division of labour [returning flags] is the
	// better one -- the menu is the UI's business ... ⇒ Revisit when Stage 2 turns the model into
	// kModelPlugIn." Stage 2 did, so this is that revisit.
	//
	// What moved with it: SetNthActionState, SetNthActionName, and the label STRINGS -- those are
	// UI text and now live in ui/KESCMActionComponent.cpp. What stayed: the counting.
	// ⇒ The model no longer names a UI type on its boundary, which is what the SDK's own model/UI
	//   pair does (ICusCondTxtFacade has no menu-state method at all).
	virtual KESCMPageToggleState	GetRegisterToggleState() = 0;
	virtual KESCMPageToggleState	GetCheckToggleState() = 0;

	// ---- the JSON store ------------------------------------------------------------------

	/** "Save Check & Register": write both flags for the armed pair next to the Target document
		and report where they went on the status line. Armed only; says so if not. */
	virtual void	SaveChecksAndRegister() = 0;

	/** "Load Check & Register": read that file back, apply Register to both documents,
		re-compare, then restore the ticks -- but only on pages that still carry a mark. Armed
		only. */
	virtual void	LoadChecksAndRegister() = 0;
};

#endif // __IKESCMPageFlagsFacade_h__
