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
#include "ITool.h"				// what QueryTool hands back
#include "IToolBoxUtils.h"		// QueryTool / SetActiveTool - the official way to change tool
#include "Utils.h"				// Utils<> - how every SDK utility is reached
#include "PersistUtils.h"		// ::GetClass - is the active tool this one?

#include "KCMUIShared.h"		// KCMSyncToolButton - the panel's one tool button
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

	/** Called as this tool becomes / stops being the active one.
		⚠★They ARE overridden now (2026-09-04). They were not while the panel's tool button stood
		  for the comparison tool alone -- then KCMTool::Deselect said everything there was to say.
		  Since the button carries BOTH tools, the panel has to hear about this one as well: pick
		  the stamp from the toolbox and the button must come to wear the paw.
		★Neither method is told what to display. Both call the one function that READS the toolbox,
		  so there is still exactly one place deciding what the button shows
		  ([[one-question-one-place]]). */
	virtual void Select();
	virtual void Deselect();
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

void KCMPawTool::Select()
{
	CTool::Select();					// base first - it tells the selection the tool is changing
	KCMSyncToolButton();
}

void KCMPawTool::Deselect()
{
	CTool::Deselect();					// base first, same reason
	KCMSyncToolButton();
}

//========================================================================================
// The pair the panel calls: make this tool active, and ask whether it is.
//
//  ★Written out rather than shared with KCMTool.cpp's pair through a boss argument. Two functions
//    of four lines, each naming its own boss, read better at the call site than one that has to be
//    handed kKCMToolBoss or kKCMPawToolBoss -- and the call site is a switch that already knows
//    which tool it means.
//  *SetActiveTool's second parameter (the exclusion group) is left at its default for the reason
//   written out in KCMTool.cpp: the toolbox has one active tool at a time.
//========================================================================================

bool16 KCMActivatePawTool()
{
	// Nothing to do where there is no toolbox (InDesign Server and the like).
	if (!Utils<IToolBoxUtils>().Exists())
		return kFalse;

	InterfacePtr<ITool> tool(Utils<IToolBoxUtils>()->QueryTool(kKCMPawToolBoss));
	if (tool == nil)
		return kFalse;	// not registered - cannot happen, but a button press must not take the app down

	return Utils<IToolBoxUtils>()->SetActiveTool(tool);
}

bool16 KCMIsPawToolActive()
{
	if (!Utils<IToolBoxUtils>().Exists())
		return kFalse;

	InterfacePtr<ITool> active(Utils<IToolBoxUtils>()->QueryActiveTool());
	if (active == nil)
		return kFalse;

	return (::GetClass(active) == kKCMPawToolBoss) ? kTrue : kFalse;
}

// End, KCMPawTool.cpp.
