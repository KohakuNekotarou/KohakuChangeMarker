//========================================================================================
//
//  KESCMScriptingDefs.h
//
//  ScriptIDs (four-character codes) published by the MODEL half of KESCM.
//
//  WHAT IS EXPOSED: SIX READ-ONLY properties - app.kcmStatus and app.kcmBookResult on the
//  application object, plus the four story change counters below (added 2026-08-15).
//  No methods and no script objects - the ones this plug-in used to have (kescmToast and the
//  rest) were removed 2026-08-05 and are not coming back; the panel is the interface.
//
//  ***** THIS FILE IS ONE HALF OF A SPLIT. ***** The toolbox tool's ScriptID (en_KESCMTool) is
//  NOT here - it went to ui/KESCMScriptingDefs.h with the tool on 2026-08-15 (split stage 2,
//  task 6B-2), because a tool is a UI boss. Read that half for it; do not restate it here.
//  Until the B-U1 audit (2026-08-16) this header still announced the tool's identity as one of
//  the things it publishes, and still counted the properties as two: THE FILE WAS SPLIT, ITS
//  COMMENT WAS NOT, and the story counters had been added the same day without the count at
//  the top being redone. The same had happened to KESCMLoc.h on both sides.
//
//  app.kcmStatus (2026-08-06, at the user's request) reports the last line the panel put on its
//  status area, which is the plug-in's whole account of what it just did: "marks start / pages
//  compared=4 changed=2", "refreshed 1 (changed 1)", "Page: 3, Change 12%". Reading it needs one
//  line of JavaScript, so a test can check what the panel said without a person looking at the
//  screen - the same thing KBS gets from app.kfcStatus. Writing is refused (see
//  KESCMScriptProvider.cpp): a script must not be able to make the panel appear to say something
//  it never said.
//
//  The registration of all six is in KESCM.fr (VersionedScriptElementInfo, one Provider block
//  per object - Application and Story), and KESCMScriptProvider.cpp serves them.
//
//  The codes follow the private numbering scheme in docs/ai-notes/kes-scriptid-registry.md:
//    [1] kind  'p' = property/parameter ('e' = method, 'n' = enumerator)
//    [2] 'K'   fixed - the author's signature, shared by every Kohaku plug-in
//    [3] 'G'   the plug-in tag for KESCM (chanGe; 'C' and 'M' were already taken)
//    [4]       member letter within this plug-in and kind (see each line below)
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
	in the panel's Story Edits list (KESCMStoryEdits::Compare - "if the two readings match, skip").
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
