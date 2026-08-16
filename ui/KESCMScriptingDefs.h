//========================================================================================
//
//  KESCMScriptingDefs.h
//
//  The ScriptID this half publishes: the toolbox tool's identity, and nothing else.
//
//  ***** THIS FILE IS ONE HALF OF A SPLIT. ***** Its name-mate, source/KESCMScriptingDefs.h,
//  holds the SIX read-only properties the model half serves (app.kcmStatus, app.kcmBookResult
//  and the four story change counters). The two are not copies of each other: they were divided
//  on 2026-08-15 (split stage 2, task 6B-2) along the same line the plug-ins were - the tool is
//  a UI boss (kGenericToolBoss cannot live in a kModelPlugIn), the properties are the model's
//  answers. Read the other half for the properties; do not restate them here.
//
//  Until the B-U1 audit (2026-08-16) this header still carried the model half's account of
//  those properties word for word, including a claim about where the tool is registered that
//  had been false since the day of the split: THE FILE WAS SPLIT, ITS COMMENT WAS NOT. The
//  same had happened to KESCMLoc.h on both sides.
//
//  The tool entry below is what ITool.h:192-223 requires every tool to have: "when adding a new
//  tool to the Tool Box, you must first define a new ScriptID and then add it to
//  kToolBoxEnumScriptElement", then return it from ITool::GetScriptID. The registration itself
//  lives in KCMUI.fr (the VersionedScriptElementInfo at the end of that file), and
//  KESCMTool.cpp returns this value.
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

#endif // __KESCMScriptingDefs_h__

// End, KESCMScriptingDefs.h.
