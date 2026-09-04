//========================================================================================
//
//  KCMScriptingDefs.h
//
//  The ScriptID this half publishes: the toolbox tool's identity, and nothing else.
//
//  ***** THIS FILE IS ONE HALF OF A SPLIT. ***** Its name-mate, source/KCMScriptingDefs.h,
//  holds the read-only properties the model half serves. **Read the other half for that list.**
//  ⚠This line used to say "the SIX properties" while there were seven -- the document one was
//  added later and only the model side was corrected. **Do not write a count of them here
//  again**: a total in two files rots in one of them.
//  The two files are not copies of each other: they were divided along the same line the
//  plug-ins were - the tool is a UI boss (kGenericToolBoss cannot live in a kModelPlugIn), and
//  the properties are the model's answers.
//
//  Until the B-U1 audit (2026-08-16) this header still carried the model half's account of
//  those properties word for word, including a claim about where the tool is registered that
//  had been false since the day of the split: THE FILE WAS SPLIT, ITS COMMENT WAS NOT. The
//  same had happened to KCMLoc.h on both sides.
//
//  The tool entry below is what ITool.h:192-223 requires every tool to have: "when adding a new
//  tool to the Tool Box, you must first define a new ScriptID and then add it to
//  kToolBoxEnumScriptElement", then return it from ITool::GetScriptID. The registration itself
//  lives in KCMUI.fr (the VersionedScriptElementInfo at the end of that file), and
//  KCMTool.cpp returns this value.
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
//    [3] 'G'   the plug-in tag for KCM (chanGe; 'C' and 'M' were already taken)
//    [4] 't'   member letter within this plug-in and kind - tool
//  Checked against ScriptingDefs.h / GenericID.h for collisions before use (none).
//
//  ★THE INCLUDE GUARD IS DELIBERATELY NOT THE ONE ITS NAME-MATE USES (__KCMUIScriptingDefs_h__
//  here, __KCMScriptingDefs_h__ over there - split apart 2026-08-18, bug recheck B11). The UI
//  project carries the model half's folder on its include path, so BOTH files are reachable from
//  one translation unit, and two DIFFERENT files sharing one guard means whichever is included
//  first silently deletes the other - a missing identifier with no mention of the file that went
//  missing. ⚠KCMBoundaryID.h is the opposite case and shares its guard on purpose: those two
//  ARE the same file, byte for byte (measured the same day - not one line differs), so it does
//  not matter which copy a translation unit gets.
//
//========================================================================================
#ifndef __KCMUIScriptingDefs_h__
#define __KCMUIScriptingDefs_h__

/** ScriptIDs that KCM contributes to enumerations that already exist in the object model.
	Scripts read this one as app.toolBoxTools.currentTool, and select the tool by assigning
	app.toolBoxTools.currentTool = UITools.KOHAKU_CHANGE_MARKER_TOOL.
*/
enum KCMScriptEnums
{
	en_KCMTool = 'nKGt',	// n = enumerator, K = Kohaku, G = KCM, t = tool
	// ★The cat-paw stamp tool (2026-09-04). It is a SUBTOOL of the one above -- it lives in that
	//   tool's press-and-hold flyout -- but a subtool is a tool, so ITool.h:192-223 asks the same
	//   of it: its own ScriptID, registered in kToolBoxEnumScriptElement (KCMUI.fr) and returned
	//   from ITool::GetScriptID (KCMPawTool.cpp). wavetool does exactly this for its own subtool
	//   (en_SineWaveTl, WavTl.fr:319-336).
	// ★'p' = paw. Checked against ScriptingDefs.h, GenericID.h, the whole of source/ and the
	//   registry in docs/ai-notes/kes-scriptid-registry.md before use: 0 hits.
	en_KCMPawTool = 'nKGp'	// n = enumerator, K = Kohaku, G = KCM, p = paw stamp
};

#endif // __KCMUIScriptingDefs_h__

// End, KCMScriptingDefs.h.
