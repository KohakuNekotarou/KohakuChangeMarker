//========================================================================================
//
//  KESCMScriptingDefs.h
//
//  ScriptIDs (four-character codes) published by KESCM.
//
//  WHAT IS EXPOSED: two READ-ONLY properties - app.kcmStatus and app.kcmBookResult - and the
//  toolbox tool's identity.
//  No methods and no script objects - the ones this plug-in used to have (kescmToast and the
//  rest) were removed 2026-08-05 and are not coming back; the panel is the interface.
//
//  app.kcmStatus (2026-08-06, at the user's request) reports the last line the panel put on its
//  status area, which is the plug-in's whole account of what it just did: "marks start / pages
//  compared=4 changed=2", "refreshed 1 (changed 1)", "Page: 3, Change 12%". Reading it needs one
//  line of JavaScript, so a test can check what the panel said without a person looking at the
//  screen - the same thing KBS gets from app.kfcStatus. Writing is refused (see
//  KESCMScriptProvider.cpp): a script must not be able to make the panel appear to say something
//  it never said.
//
//  The tool entry below is what ITool.h:192-223 requires every tool to have: "when adding a new
//  tool to the Tool Box, you must first define a new ScriptID and then add it to
//  kToolBoxEnumScriptElement", then return it from ITool::GetScriptID. The registration itself
//  lives in KESCM.fr (VersionedScriptElementInfo), and KESCMTool.cpp returns this value.
//
//  Why it matters (measured 2026-08-06, audit block 7): without it the tool returns en_None
//  ('none' = 1852796517), which is the value meaning "no tool at all" - so
//  app.toolBoxTools.currentTool cannot tell this tool apart from "nothing selected", AND the
//  tool cannot be picked from a script, because currentTool only accepts UITools enumerators
//  ("Error: expected UITools enumerator" when handed a raw ScriptID). Official samples do
//  exactly this: SnapTool -> en_SnapTool (Snap.fr:2085-2101) and SawWaveTool -> en_SawWaveTl
//  (WavTl.fr:319-336).
//
//  The code follows the private numbering scheme in docs/ai-notes/kes-scriptid-registry.md:
//    [1] kind  'n' = enumerator ('e' = method, 'p' = property/parameter)
//    [2] 'K'   fixed - the author's signature, shared by every Kohaku plug-in
//    [3] 'G'   the plug-in tag for KESCM (chanGe; 'C' and 'M' were already taken)
//    [4] 't'   member letter within this plug-in and kind - tool
//  Checked against ScriptingDefs.h / GenericID.h for collisions before use (none).
//
//========================================================================================
#ifndef __KESCMScriptingDefs_h__
#define __KESCMScriptingDefs_h__

/** ScriptIDs that KESCM contributes to enumerations that already exist in the object model.
	Scripts read this one as app.toolBoxTools.currentTool, and select the tool by assigning
	app.toolBoxTools.currentTool = UITools.KOHAKU_CHANGE_MARKER_TOOL.
*/
enum KESCMScriptEnums
{
	en_KESCMTool = 'nKGt'	// n = enumerator, K = Kohaku, G = KESCM, t = tool
};

/** Properties KESCM adds to the application object. */
enum KESCMScriptProperties
{
	p_KESCMStatus     = 'pKGm',	// p = property, K = Kohaku, G = KESCM, m = message (app.kcmStatus)
	p_KESCMBookResult = 'pKGb'	// b = book. app.kcmBookResult - the last book comparison, one line
								// per chapter ("name<TAB>state"). Checked against the registry in
								// docs/ai-notes/kes-scriptid-registry.md before use (2026-08-11).
};

#endif // __KESCMScriptingDefs_h__

// End, KESCMScriptingDefs.h.
