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
#include "KCMPageMap.h"	// KCMPageToggleState. Borrowed for the type -- but this header also
						// declares 13 model-side free functions, and the UI can SEE all of them.
						// Calling one fails at link time rather than silently, so the risk is a
						// wasted build, not a wrong build.

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
};

#endif // __IKCMPageFlagsFacade_h__
