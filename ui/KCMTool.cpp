//========================================================================================
//
//  KCMTool.cpp
//
//  Toolbox tool for KCM. While this tool is the active tool, pressing and holding the LEFT
//  mouse button on the layout reveals the comparison marks; releasing hides them. Modifier keys
//  held at press time pick the variant (see KCMTrackerRevealBegin). The mouse handling is done
//  by the tool's capturing tracker (KCMTracker.cpp), installed for this tool boss by
//  KCMTrackerRegister.cpp. This class is just the ITool that puts the tool in the toolbox.
//
//  ITool (via the CTool partial implementation). This tool is now the only input for the reveal /
//  peek / CMYK gestures; the earlier middle-button gestures were removed (2026-07-13).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTool.h"
#include "ITool.h"				// what QueryTool hands back
#include "IToolBoxUtils.h"		// QueryTool / SetActiveTool - the official way to change tool
#include "Utils.h"				// Utils<> - how every SDK utility is reached
#include "PersistUtils.h"		// ::GetClass - is the active tool ours?

#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMUIID.h"
#include "KCMScriptingDefs.h"	// en_KCMTool (this tool's ScriptID, registered in KCMUI.fr)

/** The KCM tool's ITool implementation, based on the partial implementation CTool. */
class KCMTool : public CTool
{
public:
	/** Constructor. CTool(boss, toolType, isCreation, isSelection): a view-modification tool
		that neither creates nor selects items. */
	KCMTool(IPMUnknown* boss) : CTool(boss, kViewModificationTool, kFalse, kFalse) {}

	/** Set the tool's name and initialise the tool's toolbox button icon. */
	virtual void Init(RsrcID iconID, const PluginID& pluginID);

	/** Returns the ScriptID that identifies this tool inside the en_ToolBoxTools enumeration.
		The doc comment on ITool::GetScriptID requires every toolbox tool to define one and register
		it in kToolBoxEnumScriptElement (done in KCMUI.fr); CTool's default GetScriptID calls
		ASSERT_UNIMPLEMENTED() and returns en_None, so not overriding it asserts.
		Scripts read it as app.toolBoxTools.currentTool and select this tool with
		app.toolBoxTools.currentTool = UITools.KOHAKU_CHANGE_MARKER_TOOL.
		This is the tool's identity, not a scripting API - KCM still exposes no methods and no
		properties. Until 2026-08-06 this returned en_None ('none' = "no tool at all"), which left
		the tool indistinguishable from "nothing selected" and impossible to pick from a script
		(currentTool only accepts UITools enumerators). Same shape as the official samples:
		SnapTool -> en_SnapTool, SawWaveTool -> en_SawWaveTl. */
	virtual ScriptID GetScriptID() const { return en_KCMTool; }

	/** Called when this tool becomes the active one - whichever way it was picked: the toolbox, the
		panel's tool button, a keyboard shortcut or a script. Overridden so the panel's button can
		show the same pressed state the toolbox does.
		!CTool.h is explicit at Select/Deselect that an override must call the base version FIRST
		 ("it notifies the selection that the tool is changing"), so that is what happens below. */
	virtual void Select();

	/** The other half - called when some other tool takes over. */
	virtual void Deselect();
};

/*
	CREATE_PERSIST_PMINTERFACE creates the class factory and registers the ID; the tool's
	selected state persists across sessions (same as the snapshot sample tool).
*/
CREATE_PERSIST_PMINTERFACE(KCMTool, kKCMToolImpl)

void KCMTool::Init(RsrcID iconID, const PluginID& pluginID)
{
	SetName(kKCMToolStringKey);
	InitWidget(kKCMToolWidgetID, iconID, pluginID);
}

//========================================================================================
// Making this tool the active one, from the panel's tool button (2026-08-07, user request)
//
//  *The official way, and the SDK has four worked examples of it - all the same shape
//    (codesnippets): SnpCreateFrame.cpp:357-368, SnpManipulateTextModel.cpp:537-545,
//    SnpManipulateTextFrame.cpp:1416-1427, SnpCopyPasteTable.cpp:262-266.
//      Utils<IToolBoxUtils>()->QueryTool(<tool boss>)  ->  SetActiveTool(that tool)
//    Those four look at the CURRENT tool first (QueryActiveTool) because they only switch when
//    the text tool is not already active. Here the button always means "give me this tool", so
//    that half is left out.
//
//  *QueryTool ADDREFS what it returns ("Callers should Release() this pointer when done",
//    IToolBoxUtils.h:77) -> InterfacePtr, exactly as the snippets do.
//
//  *SetActiveTool's second parameter (toolType, default kPointerToolBoss) is the EXCLUSION GROUP,
//    not the tool's own kind: "Tools of the same type are mutually exclusive" (IToolBoxUtils.h:45-46).
//    The toolbox has one active tool at a time, and every snippet leaves this at its default - so
//    it is left at the default here too. (Not to be confused with kViewModificationTool, which is
//    what this class passes to CTool above; that one describes what the tool DOES.)
//
//  !Every source file of this plug-in is UTF-8 with a BOM, this one included, which is what lets
//   non-ASCII text compile at all (without the BOM, MSVC reads the bytes in the machine's code page
//   and C4819 follows). What is true of THIS file is only that its text happens to be ASCII
//   throughout. An older note here claimed it had no BOM and that nothing non-ASCII could go in;
//   the file has carried one for as long as the history goes back.
//========================================================================================

bool16 KCMActivateOwnTool()
{
	// Nothing to do where there is no toolbox (InDesign Server and the like).
	if (!Utils<IToolBoxUtils>().Exists())
		return kFalse;

	InterfacePtr<ITool> tool(Utils<IToolBoxUtils>()->QueryTool(kKCMToolBoss));
	if (tool == nil)
		return kFalse;	// not registered - cannot happen, but a button press must not take the app down

	// *SetActiveTool answers whether it took (IToolBoxUtils.h:47). The panel says so on its status
	//  line, so the press is never silent - a tool can refuse to become active (it is disabled for
	//  the current view, say), and "nothing happened" would otherwise look like a broken button.
	return Utils<IToolBoxUtils>()->SetActiveTool(tool);
}

//========================================================================================
// Keeping the panel's tool button in step with the toolbox (2026-08-07, user request:
// "make it change colour while selected, the way the toolbox does")
//
//  *ITool::Select / Deselect (ITool.h:90-96) are called whenever this tool becomes, or stops
//   being, the active tool - NO MATTER HOW it was picked: the toolbox, the panel's own button, a
//   keyboard shortcut, or a script setting app.toolBoxTools.currentTool. That is why the button's
//   state is written from HERE and nowhere else: there is exactly one place that knows, so the
//   panel can never disagree with the toolbox.
//     !The alternative - subscribing to kToolChangedToolBoxMessage (ToolboxID.h:130) - was not
//      needed. That message is DECLARED in the public header but has no use site anywhere in the
//      SDK, so which subject carries it would have had to be measured on the real application.
//
//  *The base class runs first, as CTool.h instructs at Select / Deselect.
//========================================================================================

void KCMTool::Select()
{
	CTool::Select();					// base first - it tells the selection the tool is changing
	KCMSetToolButtonSelected(kTrue);
}

void KCMTool::Deselect()
{
	CTool::Deselect();					// base first, same reason
	KCMSetToolButtonSelected(kFalse);
}

// Is this plug-in's tool the active one right now? Asked when the panel is (re)built, so a panel
// opened while the tool is already active still shows the button pressed.
// *QueryActiveTool addrefs (IToolBoxUtils.h:54) -> InterfacePtr.
bool16 KCMIsOwnToolActive()
{
	if (!Utils<IToolBoxUtils>().Exists())
		return kFalse;

	InterfacePtr<ITool> active(Utils<IToolBoxUtils>()->QueryActiveTool());
	if (active == nil)
		return kFalse;

	return (::GetClass(active) == kKCMToolBoss) ? kTrue : kFalse;
}

// End, KCMTool.cpp.
