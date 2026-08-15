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

class IActionStateList;

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
	// Both items use kCustomEnabling, and the Register item's label is dynamic (it says
	// "register" or "unregister" depending on what is selected), so the menu asks before it is
	// drawn. The list and the index are passed straight through.
	//
	// ★These take the action state list rather than returning flags for the caller to apply.
	// The plan proposed the returning form, and as a division of labour it is the better one --
	// the menu is the UI's business. It is not done here because the bodies would have to be
	// rewritten (they set the state AND the dynamic label in one pass), and Stage 1's rule is
	// that behaviour does not change. Nothing is lost by waiting: IActionStateList is a pure
	// interface, so it costs the model no library it would not otherwise link.
	// ⇒ Revisit when Stage 2 turns the model into kModelPlugIn.
	virtual void	UpdateRegisterToggleState(IActionStateList* listToUpdate, int32 index) = 0;
	virtual void	UpdateCheckToggleState(IActionStateList* listToUpdate, int32 index) = 0;

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
