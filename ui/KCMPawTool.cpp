//========================================================================================
//
//  KCMPawTool.cpp
//
//  The cat-paw stamp tool. ★It is a SUBTOOL of the KCM tool: its ToolDef (KCMUI.fr) names
//  kKCMToolBoss as the parent tool, which puts it inside that tool's press-and-hold flyout and
//  costs NO slot of its own in the toolbox. The SDK's only worked example of a subtool is
//  wavetool/WavTl.fr:264-276.
//
//  While it is the active tool, a left click on the layout places one pink paw at the point
//  pressed -- and clicking that paw again lifts it. The mouse handling belongs to the tool's
//  tracker (KCMPawTracker.cpp), which KCMTrackerRegister.cpp installs for this tool boss. This
//  class is only the ITool that puts the tool in the flyout.
//
//  ★WHAT IT PLACES IS NOT PART OF THE DOCUMENT. The paws are held by this plug-in and saved to
//    KCM's own JSON, so the .indd is never written to -- the same promise the comparison marks
//    make, and the reason this is a kViewModificationTool that creates nothing and selects
//    nothing.
//
//  ITool (via the CTool partial implementation), modelled on KCMTool.cpp next door.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTool.h"

#include "KCMUIID.h"
#include "KCMScriptingDefs.h"	// en_KCMPawTool (this tool's ScriptID, registered in KCMUI.fr)

/** The cat-paw stamp tool's ITool implementation, based on the partial implementation CTool. */
class KCMPawTool : public CTool
{
public:
	/** Constructor. CTool(boss, toolType, isCreation, isSelection): like the KCM tool, this one
		modifies the view rather than the document -- it creates no page item and selects
		nothing, so both flags are kFalse. */
	KCMPawTool(IPMUnknown* boss) : CTool(boss, kViewModificationTool, kFalse, kFalse) {}

	/** Set the tool's name and initialise its button icon inside the flyout. */
	virtual void Init(RsrcID iconID, const PluginID& pluginID);

	/** The ScriptID that identifies this tool inside the en_ToolBoxTools enumeration.
		★The override is not optional: CTool's own GetScriptID calls ASSERT_UNIMPLEMENTED() and
		answers en_None ('none' = "no tool at all"), which would leave this tool
		indistinguishable from "nothing selected" AND impossible to pick from a script
		(app.toolBoxTools.currentTool accepts UITools enumerators only). That was measured on
		the KCM tool itself on 2026-08-06, audit block 7.
		★Being a subtool changes nothing here -- wavetool's subtool has one too
		(en_SineWaveTl). */
	virtual ScriptID GetScriptID() const { return en_KCMPawTool; }

	// ★Select / Deselect are deliberately NOT overridden. The panel's tool button stands for the
	//   KCM tool alone (KCMTool.cpp writes its pressed state from those two methods), and picking
	//   the stamp tool takes the active tool away from the KCM tool -- so the button going dark
	//   at that moment is correct, and it already happens through KCMTool::Deselect. Overriding
	//   them here would be a second place deciding one thing ([[one-question-one-place]]).
};

/*
	CREATE_PERSIST_PMINTERFACE: the selected state persists across sessions, the same as the KCM
	tool and the snapshot sample's tool. IID_IPMPERSIST on the boss is the other half of that.
*/
CREATE_PERSIST_PMINTERFACE(KCMPawTool, kKCMPawToolImpl)

void KCMPawTool::Init(RsrcID iconID, const PluginID& pluginID)
{
	SetName(kKCMPawToolStringKey);
	InitWidget(kKCMPawToolWidgetID, iconID, pluginID);
}

// End, KCMPawTool.cpp.
