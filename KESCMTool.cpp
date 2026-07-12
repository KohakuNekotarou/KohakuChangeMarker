//========================================================================================
//
//  KESCMTool.cpp
//
//  Toolbox tool for KESCM. While this tool is the active tool, pressing and holding the LEFT
//  mouse button on the layout reveals the comparison marks (useful on Macs that have no middle
//  button); releasing hides them. The mouse handling is done by the tool's capturing tracker
//  (KESCMTracker.cpp), installed for this tool boss by KESCMTrackerRegister.cpp. This class is
//  just the ITool that puts the tool in the toolbox.
//
//  ITool (via the CTool partial implementation). The middle-button gestures stay untouched
//  (both input methods coexist).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTool.h"

#include "KESCMID.h"

/** The KESCM tool's ITool implementation, based on the partial implementation CTool. */
class KESCMTool : public CTool
{
public:
	/** Constructor. CTool(boss, toolType, isCreation, isSelection): a view-modification tool
		that neither creates nor selects items. */
	KESCMTool(IPMUnknown* boss) : CTool(boss, kViewModificationTool, kFalse, kFalse) {}

	/** Set the tool's name and initialise the tool's toolbox button icon. */
	virtual void Init(RsrcID iconID, const PluginID& pluginID);

	/** KESCM exposes no scripting, so return en_None (avoids CTool's ASSERT_UNIMPLEMENTED). */
	virtual ScriptID GetScriptID() const { return en_None; }
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
