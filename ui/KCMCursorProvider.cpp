//========================================================================================
//
//  KCMCursorProvider.cpp
//
//  The "always a check mark" cursor shown while the KCM tool is active: a cursor provider (a
//  CToolCursorProvider subclass) that replaces the tool's default cursor. Choose the KCM tool in the
//  toolbox and the pointer becomes a check mark over a layout view. The vertex of the check is the
//  hotspot - the point a click is taken at - and it is given by HOTC(kKCMCheckCursorResID) in the .fr.
//
//  ★The artwork is a **PNG resource (PNGC)** rather than a CursorSpec drawing callback.
//  Why: the intermittent "rubbish" seen at the instant of a press (Alt + left) came from the base
//  CTracker::BeginTracking re-installing the check cursor (InitializeModalCursor /
//  UpdateModalCursor fetch the modal cursor again), which ran the callback and let one frame of an
//  unfinished buffer show. A resource cursor draws into no buffer when it is re-installed, so the
//  source of it is gone.
//  Note: a drawing callback DOES work on the provider route (GetCursor) - that was shown in the
//    running application - and the technique is still used for the CMYK readout cursor
//    (KCMCmykCursorBitmapProc in KCMCmykCursor.cpp), whose contents change every time and therefore
//    cannot be a resource. The check mark is fixed, so a resource is enough.
//  The images are generated with the same geometry as KCMCheckGlyph.h (vertices 5,12 - 10,18 - 20,5 /
//  halo 4.2 active or 5.0 inactive / body 2.4 / round caps): KCM_Check_10_18.png (the black check)
//  and KCM_CheckOff_10_18.png (the outlined one), each with @2x and @3to2x.
//  ⚠The halo was written here as "3.5 or 5.0" for a while, and **3.5 is the value from before it was
//    thickened to 4.2 at the user's request**. The default in KCMCheckGlyph.h and the generating
//    script work/kescm-make-check-cursor.ps1 both say 4.2, and **this one line was the only one left
//    behind** (the PNGs themselves were generated at 4.2, so nothing looked wrong).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "KCMUIID.h"

#include "CToolCursorProvider.h"	// the base (a tool cursor provider; it carries the defaults such as zoom and hand)
#include "ICursorMgr.h"			// eCursorModifierState
#include "CursorSpec.h"			// CursorSpec
#include "CursorDefs.h"			// kCrsrNone

#include "KCMCmykCursor.h"	// KCMToolCursorShouldBeBlack (black or outlined; black over any document while Started)

//----------------------------------------------------------------------------------------
//  The cursor provider itself
//----------------------------------------------------------------------------------------
/** The KCM tool's cursor provider: while the tool is active, the pointer is always a check mark.
	Modelled on sdksamples/snapshot/SnapCursorProvider.cpp.
*/
class KCMCheckCursorProvider : public CToolCursorProvider
{
	public:
		KCMCheckCursorProvider(IPMUnknown* boss) : CToolCursorProvider(boss) {}
		~KCMCheckCursorProvider() {}

		virtual CursorSpec	GetCursor(IControlView* viewUnderMouse, const SysPoint globalMouse, ICursorMgr::eCursorModifierState modifiers) const;
};

CREATE_PMINTERFACE(KCMCheckCursorProvider, kKCMCursorProviderImpl)

CursorSpec KCMCheckCursorProvider::GetCursor(IControlView* viewUnderMouse, const SysPoint globalMouse, ICursorMgr::eCursorModifierState modifiers) const
{
	// The standard cursors that modifier keys ask for (zoom, hand and the rest) are left to the base,
	// so spacebar-for-hand and its like keep working.
	CursorSpec base = CToolCursorProvider::GetCursor(viewUnderMouse, globalMouse, modifiers);
	if (base.GetID() != kCrsrNone)
		return base;

	// Everything else is the check mark. The images are PNGC resources in the .fr (one per ID; the 2x
	// and 1.5x ones are registered at the same ID plus an offset) and the hotspot comes from the HOTC
	// of each ResID. ★No callback (proc) is passed - nothing is drawn into a buffer when the cursor is
	// installed, so "one frame of an unfinished image" cannot happen on a press.
	// A black check means "Started" (a comparison is running), over ANY document under the pointer
	// (user's instruction; it used to be black only over the Target). While stopped it is the outlined
	// one (black rim, white body). The two states have CursorIDs of their own, so crossing the boundary
	// changes the spec and the cursor really does switch.
	// ★PluginCursorSpec = CursorSpec(GetPlugIn()->GetPluginID(), id), the official macro
	//   (CursorSpec.h:145-149): the header states its purpose as removing the need to bury PluginID
	//   constants in code. The product does the same in
	//   open/components/buttonui/misc/AppearancePlaceBehaviorUI.cpp:124,132.
	if (KCMToolCursorShouldBeBlack(viewUnderMouse))
		return PluginCursorSpec(kKCMCheckCursorResID);
	return PluginCursorSpec(kKCMCheckCursorInactiveResID);
}

// End, KCMCursorProvider.cpp.
