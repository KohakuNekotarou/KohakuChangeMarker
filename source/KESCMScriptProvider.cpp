//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Scripting: app.kcmStatus       - the last message the panel put on its status line, read-only.
//             app.kcmBookResult   - the last book comparison, one line per chapter, read-only.
//
//  WHY THIS EXISTS
//
//  The panel answers in one line: how many pages were compared and how many changed, what a
//  refresh updated, which page a jump landed on and how much of it changed, why a comparison
//  could not start. That line is the plug-in's whole account of what it just did - and until
//  2026-08-06 the only way to read it was to look at the screen. Verifying a change therefore
//  meant a person taking a screenshot, which is slow and cannot be automated.
//
//  A script property turns that into one line of JavaScript, which means it is also reachable
//  over COM from PowerShell:
//
//      $app = New-Object -ComObject "InDesign.Application.2026"
//      $app.DoScript("app.kcmStatus", 1246973031)
//
//  READ-ONLY on purpose. Setting it would let a script write something the panel never said,
//  which is exactly the property this is useful for not having.
//
//  It answers even when the panel is CLOSED: KESCMSetStatus keeps the string in the module
//  (gSessionStatus) whatever happens to the panel widget, so a comparison run from a script and
//  read back from a script needs no panel on screen at all.
//
//  This is the ONLY thing KESCM publishes besides the tool's identity - no methods, no script
//  objects. The methods this plug-in used to have (kescmToast and the rest) were removed
//  2026-08-05 and are not coming back; the panel is the interface.
//
//  Shape copied from KBS's KBSScriptProvider.cpp, which follows the SDK's scripting sample.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IScript.h"
#include "IScriptRequestData.h"

// General includes:
#include "CScriptProvider.h"
#include "ScriptData.h"
#include "WideString.h"

// Project includes:
#include "KESCMID.h"
#include "KESCMScriptingDefs.h"
#include "KESCMModelNotify.h"	// KESCMGetSessionStatus - the status line, kept on the model side (Task 9)
// (★KESCMUIShared.h は include しない ---- 2026-08-13 の Task 5 で一度足したが、この ScriptProvider は
//  widget に触る8本を1つも呼んでいなかった＝**死んだ依存**だったので外した。読んでいるのは
//  KESCMGetSessionStatus(KESCMCore.h。文字列の保持は model 側)だけで、これは逆流ではない。)
#include "KESCMBookCompare.h"	// KESCMGetBookResultText - the last book comparison, also in the module

/** Serves this plug-in's scripting additions. Two read-only properties, both on the application
    object: app.kcmStatus (the panel's status line) and app.kcmBookResult (the last book
    comparison, one line per chapter). */
class KESCMScriptProvider : public CScriptProvider
{
public:
	KESCMScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	virtual ~KESCMScriptProvider() {}

	/** Read (or refuse to write) our property. Anything not ours goes to the base class, which is
	    what keeps the rest of the application's scripting working on this object. */
	virtual ErrorCode AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script);
};

CREATE_PMINTERFACE(KESCMScriptProvider, kKESCMScriptProviderImpl)

ErrorCode KESCMScriptProvider::AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script)
{
	if (propID.Get() != p_KESCMStatus && propID.Get() != p_KESCMBookResult)
		return CScriptProvider::AccessProperty(propID, data, script);

	if (data == nil)
		return kFailure;

	// Read-only, both of them. The declarations in KESCM.fr say kReadOnly, so the engine should
	// refuse an assignment before it ever reaches here; this is the backstop, and it fails rather
	// than quietly accepting a value that would then not be there on the next read.
	if (data->IsPropertyPut())
		return kFailure;

	if (!data->IsPropertyGet())
		return CScriptProvider::AccessProperty(propID, data, script);

	PMString value;
	if (propID.Get() == p_KESCMStatus)
		KESCMGetSessionStatus(value);		// the panel's status line
	else
		KESCMGetBookResultText(value);		// the last book comparison, one line per chapter

	ScriptData outputData;
	outputData.SetWideString(WideString(value));
	data->AppendReturnData(script, propID, outputData);
	return kSuccess;
}

// End, KESCMScriptProvider.cpp.
