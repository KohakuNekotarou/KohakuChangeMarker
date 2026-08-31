//========================================================================================
//
//  KCMPageFlagState.h
//
//  What a per-page toggle (Register / Check) should look like right now -- the type alone.
//
//  WHY IT IS A FILE OF ITS OWN. IKCMPageFlagsFacade returns this type, so the UI has to know it;
//  it used to be reached by including KCMPageMap.h, which also declares thirteen model-side free
//  functions the UI can see and cannot link to. A header that crosses the boundary should carry
//  the type and nothing else, the way KCMBookResult.h already does -- so what travels is a
//  definition, not an invitation to call across the plug-in edge.
//
//  Nothing here allocates, and nothing here is a decision: both halves read the same three
//  fields and each decides for itself what to do with them (the model counts, the UI draws the
//  menu). @warning that division is stated where it belongs, on IKCMPageFlagsFacade -- if it
//  ever moves, it does not move HERE as well ([[one-question-one-place]]).
//
//========================================================================================
#ifndef __KCMPageFlagState_h__
#define __KCMPageFlagState_h__

#include "BaseType.h"		// bool16

//----------------------------------------------------------------------------------------
// How a per-page toggle (Register / Check) should look right now.
//
// **Writing to the menu is the UI's job**, so the model only answers "is it enabled", "all or
//   some of them", and "which role does this document play". That keeps `IActionStateList` out
//   of the model's boundary altogether, the same shape as the worked example `ICusCondTxtFacade`,
//   which has no menu-state method at all. **The label strings belong to the UI too**, being UI
//   strings.
//
// Register and Check share this type: the answer has the same shape for both, and only what is
// being counted differs.
//----------------------------------------------------------------------------------------
enum KCMPageTick
{
	kKCMPageTickNone = 0,		// none of them are ticked
	kKCMPageTickSome,			// some are (the mixed tick = kMultiSelectedAction)
	kKCMPageTickAll			// the whole selection is (a tick = kSelectedAction)
};

enum KCMPageRole
{
	kKCMPageRoleNone = 0,		// not comparing, or some third document
	kKCMPageRoleTarget,		// the comparison's Target (newer) side
	kKCMPageRoleSource		// the comparison's Source (older) side
};

struct KCMPageToggleState
{
	bool16			fEnabled;	// kFalse greys the menu out (fTick / fRole mean nothing then)
	KCMPageTick	fTick;
	KCMPageRole	fRole;		// picks Register's label. Check does not read it

	KCMPageToggleState()
		: fEnabled(kFalse), fTick(kKCMPageTickNone), fRole(kKCMPageRoleNone) {}
};

#endif // __KCMPageFlagState_h__

// End, KCMPageFlagState.h.
