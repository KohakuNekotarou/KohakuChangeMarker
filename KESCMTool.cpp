//========================================================================================
//
//  KESCMTool.cpp
//
//  Toolbox tool for KESCM. While this tool is the active tool, pressing and holding the LEFT
//  mouse button on the layout reveals the comparison marks; releasing hides them. Modifier keys
//  held at press time pick the variant (see KESCMTrackerRevealBegin). The mouse handling is done
//  by the tool's capturing tracker (KESCMTracker.cpp), installed for this tool boss by
//  KESCMTrackerRegister.cpp. This class is just the ITool that puts the tool in the toolbox.
//
//  ITool (via the CTool partial implementation). This tool is now the only input for the reveal /
//  peek / CMYK gestures; the earlier middle-button gestures were removed (2026-07-13).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTool.h"

#include "KESCMID.h"
#include "KESCMScriptingDefs.h"	// en_KESCMTool (this tool's ScriptID, registered in KESCM.fr)

/** The KESCM tool's ITool implementation, based on the partial implementation CTool. */
class KESCMTool : public CTool
{
public:
	/** Constructor. CTool(boss, toolType, isCreation, isSelection): a view-modification tool
		that neither creates nor selects items. */
	KESCMTool(IPMUnknown* boss) : CTool(boss, kViewModificationTool, kFalse, kFalse) {}

	/** Set the tool's name and initialise the tool's toolbox button icon. */
	virtual void Init(RsrcID iconID, const PluginID& pluginID);

	/** Returns the ScriptID that identifies this tool inside the en_ToolBoxTools enumeration.
		ITool.h:192-223 requires every toolbox tool to define one and register it in
		kToolBoxEnumScriptElement (done in KESCM.fr); the base class ASSERTs if it is not
		implemented. Scripts read it as app.toolBoxTools.currentTool and select this tool with
		app.toolBoxTools.currentTool = UITools.KOHAKU_CHANGE_MARKER_TOOL.
		This is the tool's identity, not a scripting API - KESCM still exposes no methods and no
		properties. Until 2026-08-06 this returned en_None ('none' = "no tool at all"), which left
		the tool indistinguishable from "nothing selected" and impossible to pick from a script
		(currentTool only accepts UITools enumerators). Same shape as the official samples:
		SnapTool -> en_SnapTool, SawWaveTool -> en_SawWaveTl. */
	virtual ScriptID GetScriptID() const { return en_KESCMTool; }
};

/*
	CREATE_PERSIST_PMINTERFACE creates the class factory and registers the ID; the tool's
	selected state persists across sessions (same as the snapshot sample tool).
*/
CREATE_PERSIST_PMINTERFACE(KESCMTool, kKESCMToolImpl)

void KESCMTool::Init(RsrcID iconID, const PluginID& pluginID)
{
	SetName(kKESCMToolStringKey);
	InitWidget(kKESCMToolWidgetID, iconID, pluginID);
}

// End, KESCMTool.cpp.
