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

/** Properties KESCM adds to the application object. */
enum KESCMScriptProperties
{
	p_KESCMStatus     = 'pKGm',	// p = property, K = Kohaku, G = KESCM, m = message (app.kcmStatus)
	p_KESCMBookResult = 'pKGb'	// b = book. app.kcmBookResult - the last book comparison, one line
								// per chapter ("name<TAB>state"). Checked against the registry in
								// docs/ai-notes/kes-scriptid-registry.md before use (2026-08-11).
};

/** Properties KESCM adds to the STORY object (2026-08-15, at the user's request).

	These are ITextModel's four change counters, read straight off the story the script names:

		app.documents[0].stories[2].kcmChangeCount   ->  8

	WHY THEY ARE WORTH PUBLISHING. The aggregate counter is what decides whether a story appears
	in the panel's Story Edits list (KESCMStoryStamp.cpp:84 - "if the two readings match, skip").
	Until now that number could not be seen from outside, so when a list came back EMPTY there was
	no way to tell "the plug-in is wrong" from "the two documents genuinely read the same" without
	reading the source. Measured 2026-08-15: two documents built by the same script, with different
	text, carry the SAME counters - because a counter is a version number for the story's state,
	not a count of edits, and both files went through the same number of edits. Publishing the
	numbers turns that from a guess into a reading.

	READ-ONLY, all four. They are the application's own counters; a script that could set them
	would be able to make the panel report a change that never happened.

	Codes follow docs/ai-notes/kes-scriptid-registry.md (p = property, K = Kohaku, G = KESCM) and
	were checked against ScriptingDefs.h / GenericID.h for collisions before use - none.
*/
enum KESCMStoryScriptProperties
{
	p_KESCMChangeCount      = 'pKGC',	// C = Change  - the all-changes counter (the one that decides)
	p_KESCMTextChangeCount  = 'pKGT',	// T = Text    - characters inserted, removed or replaced
	p_KESCMAttrChangeCount  = 'pKGA',	// A = Attr    - effective attributes, styles and overrides
	p_KESCMOtherChangeCount = 'pKGO'	// O = Other   - the counter nothing measured has ever moved
};

#endif // __KESCMScriptingDefs_h__

// End, KESCMScriptingDefs.h.
