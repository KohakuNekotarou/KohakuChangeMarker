//========================================================================================
//
//  KCMPawCursorProvider.cpp
//
//  The cursor shown while the cat-paw stamp tool is active: a pink paw whose hot spot is its own
//  centre, so the pointer shows where the paw will land. A CToolCursorProvider subclass, exactly
//  as the KCM tool's own cursor is (KCMCursorProvider.cpp), and modelled on
//  sdksamples/snapshot/SnapCursorProvider.cpp.
//
//  ★The artwork is a **PNGC resource**, not a drawing callback. The callback route works on this
//    path, but the check cursor next door was moved off it after intermittent rubbish appeared at
//    the instant of a press (the base re-installs the modal cursor, which re-ran the callback and
//    let an unfinished buffer show). A PNG cannot be half-drawn, so the whole class of problem is
//    absent -- the reasoning is written out in full in KCMCursorProvider.cpp.
//
//  ★IT IS SIMPLER THAN THE KCM TOOL'S, and deliberately. That one has two states -- black while a
//    comparison is armed, outlined while stopped -- because what it does depends on the
//    comparison. A paw can be placed at any time on any document, so there is no state to report
//    and one picture is the honest answer.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "KCMUIID.h"

#include "CToolCursorProvider.h"	// the base: it carries the standard cursors (zoom, hand, ...)
#include "ICursorMgr.h"				// eCursorModifierState
#include "CursorSpec.h"				// CursorSpec / PluginCursorSpec
#include "CursorDefs.h"				// kCrsrNone

/** The stamp tool's cursor provider: while the tool is active, the pointer is a pink paw. */
class KCMPawCursorProvider : public CToolCursorProvider
{
	public:
		KCMPawCursorProvider(IPMUnknown* boss) : CToolCursorProvider(boss) {}
		~KCMPawCursorProvider() {}

		virtual CursorSpec	GetCursor(IControlView* viewUnderMouse, const SysPoint globalMouse,
									  ICursorMgr::eCursorModifierState modifiers) const;
};

CREATE_PMINTERFACE(KCMPawCursorProvider, kKCMPawCursorProviderImpl)

CursorSpec KCMPawCursorProvider::GetCursor(IControlView* viewUnderMouse, const SysPoint globalMouse,
										   ICursorMgr::eCursorModifierState modifiers) const
{
	// The cursors a modifier key asks for (zoom, hand and the rest) are left to the base, so
	// spacebar-for-hand and its like keep working while this tool is chosen.
	CursorSpec base = CToolCursorProvider::GetCursor(viewUnderMouse, globalMouse, modifiers);
	if (base.GetID() != kCrsrNone)
		return base;

	// Everything else is the paw. The images are PNGC resources in the .fr (one per ID; the 2x and
	// 1.5x ones are registered at the same ID plus an offset) and the hot spot comes from that ID's
	// HOTC. ★No callback is passed, so nothing is drawn into a buffer as the cursor is installed.
	// ★PluginCursorSpec = CursorSpec(GetPlugIn()->GetPluginID(), id), the official macro
	//   (CursorSpec.h:145-149), which exists so a PluginID need not be buried in code.
	return PluginCursorSpec(kKCMPawCursorResID);
}

// End, KCMPawCursorProvider.cpp.
