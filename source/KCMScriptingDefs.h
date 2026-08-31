//========================================================================================
//
//  KCMScriptingDefs.h
//
//  ScriptIDs (four-character codes) published by the MODEL half of KCM.
//
//  WHAT IS EXPOSED -- all of it READ-ONLY, on three different script objects:
//    Application  app.kcmStatus, app.kcmBookResult
//    Story        the four change counters below
//    Document     kcmTransparencyItemCount
//  No methods and no script objects -- the ones this plug-in used to have (kescmToast and the
//  rest) were removed and are not coming back; the panel is the interface.
//  @warning **do not write a total here.** "SIX" stood in this line while there were seven,
//    because the document property was added later and only the implementation followed; the
//    same thing had happened once before, when the story counters arrived (the note further
//    down about the file being split and the comment not). A list that is added to is safe;
//    a number that has to be re-counted is not.
//
//  **THE PROPERTIES ON DOCUMENT-RESIDENT OBJECTS ARE KEPT OUT OF THE USER'S IDML ON PURPOSE.**
//  (The story counters and the document property. KCM.fr's second VersionedScriptElementInfo is
//  where it is done.)
//
//  A property published on an object that lives INSIDE A DOCUMENT is written into every IDML the
//  user exports, because IDML is a mapping of the scripting DOM: "scripting objects become
//  elements, and scripting properties become either attributes or child elements" (the IDML
//  cookbook). MEASURED -- every <Story> in Stories/*.xml and the <XmlStory> in
//  XML/BackingStory.xml carried all four of ours:
//
//      <Story Self="ufe" ... KcmChangeCount="6" KcmTextChangeCount="2"
//                           KcmAttrChangeCount="4" KcmOtherChangeCount="0">
//
//  - 36 attributes in a 22-file IDML of a four-page document, whether or not the plug-in had ever
//  run. Nothing broke (import ignores them, an attribute no plug-in defines is ignored just the
//  same, and app.generateIDMLSchema() declares them optional for this configuration), but volatile
//  session state does not belong in a document interchange format: the guide describes IDML
//  support as "exposing your persistent data in the scripting DOM".
//
//  => THE FIX IS A CLIENT-SPECIFIC RESOURCE, NOT A DIFFERENT SHAPE OF API. An INX-only
//  VersionedScriptElementInfo (Contexts naming kINXScriptManagerBoss) declares them under
//  Provider{kNotSupported}, which removes them from the IDML/INX DOM while leaving the ordinary
//  scripting DOM untouched. basicshape calls that "the prefered approach going forward" (sic).
//  @warning **the SDK never does it to a PROPERTY** -- its three uses of kNotSupported are
//  Parent / RepresentObject / CollectionMethod (basicshape, candlechart, customdatalink), so
//  this was measured with two builds that differed in that one resource and nothing else:
//
//      with the resource:     stories[0].kcmChangeCount -> 6      IDML: no Kcm attribute
//      without it (control):  stories[0].kcmChangeCount -> 6      IDML: KcmChangeCount="6"
//
//  The application's two strings need none of this -- the application object is not part of a
//  document's IDML (measured the same day: no KcmStatus anywhere in the package).
//
//  The reverse is worth knowing, because it is a capability rather than a trap: a property IS
//  the easy way to put your own data INTO an IDML. One way only, though -- the value comes back
//  only if the property is writable and the provider applies the put (see the guide on read-only
//  properties set by the application, and the kReadOnlyButReadWriteForINX flag in
//  ScriptInfoTypes.h). That is how candlechart and basicpersistinterface carry custom data
//  through a snippet/IDML round trip.
//
//  **THIS FILE IS ONE HALF OF A SPLIT.** The toolbox tool's ScriptID (en_KCMTool) is NOT here --
//  it went to ui/KCMScriptingDefs.h with the tool, because a tool is a UI boss. Read that half
//  for it; do not restate it here.
//  @warning this header once announced the tool as one of the things it publishes, and counted
//    its properties as two: **THE FILE WAS SPLIT, ITS COMMENT WAS NOT**, and the story counters
//    had been added the same day without the count at the top being redone. The same had
//    happened to KCMLoc.h on both sides -- **and it happened here again** when the document
//    property arrived and the count stayed at six. Hence the rule at the top: list, do not count.
//
//  app.kcmStatus reports the last line the panel put on its status area, which is the plug-in's
//  whole account of what it just did: "marks start / pages compared=4 changed=2", "refreshed 1
//  (changed 1)", "Page: 3, Change 12%". Reading it needs one line of JavaScript, so a test can
//  check what the panel said without a person looking at the screen -- the same thing KBS gets
//  from app.kfcStatus. Writing is refused (see KCMScriptProvider.cpp): a script must not be able
//  to make the panel appear to say something it never said.
//
//  The registration is in KCM.fr: one VersionedScriptElementInfo with **one Provider block per
//  script object** (Application, Story, Document), all naming this plug-in's single provider
//  boss, and KCMScriptProvider.cpp serves them all. A SECOND resource in the same file, for the
//  INX/IDML script manager only, takes the story and document ones back out of the file format
//  (see the block above).
//
//  The codes follow the private numbering scheme in docs/ai-notes/kes-scriptid-registry.md:
//    [1] kind  'p' = property/parameter ('e' = method, 'n' = enumerator)
//    [2] 'K'   fixed - the author's signature, shared by every Kohaku plug-in
//    [3] 'G'   the plug-in tag for KCM (chanGe; 'C' and 'M' were already taken)
//    [4]       member letter within this plug-in and kind (see each line below)
//  Checked against ScriptingDefs.h / GenericID.h for collisions before use (none -- and that
//  check was itself validated first, by measuring codes that DO exist, 'move' and 'cflo',
//  because a search pattern that matches nothing reports every candidate as free).
//  **All of them, plus the tool's 'nKGt', are REGISTERED WITH ADOBE** as code/name pairs.
//  @warning a registration is a PAIR: renaming one of these means re-applying
//  (docs/ai-notes/kes-scriptid-registry.md §2.1.2 holds the wording that was accepted).
//
//========================================================================================
#ifndef __KCMScriptingDefs_h__
#define __KCMScriptingDefs_h__

/** Properties KCM adds to the application object. */
enum KCMScriptProperties
{
	p_KCMStatus     = 'pKGm',	// p = property, K = Kohaku, G = KCM, m = message (app.kcmStatus)
	p_KCMBookResult = 'pKGb',	// b = book. app.kcmBookResult - the last book comparison, one line
								// per chapter ("name<TAB>state"). Checked against the registry in
								// docs/ai-notes/kes-scriptid-registry.md before use (2026-08-11).

	/** app.kcmStoryReadCompare - r = read compare.

		★**THE ONLY READ-WRITE PROPERTY KCM HAS**, and it is temporary: assigning "on" arms the
		parallel run that checks the new reader against the old one, anything else disarms it, and
		reading returns the last report. It goes when the migration lands.
		⚠'pKGr' was checked against the registry (docs/ai-notes/kes-scriptid-registry.md) and
		 against all four of my plug-ins before use - 0 hits, 2026-08-31. The check was validated
		 first by searching for 'pKGx', which does exist: a search that matches nothing would call
		 every candidate free. */
	p_KCMStoryReadCompare = 'pKGr'
};

/** Properties KCM adds to the STORY object (at the user's request).

	These are ITextModel's four change counters, read straight off the story the script names:

		app.documents[0].stories[2].kcmChangeCount   ->  8

	WHY THEY ARE WORTH PUBLISHING. The aggregate counter is what decides whether a story appears
	in the panel's Story Edits list (KCMStoryEdits::Compare -- "if the two readings match, skip").
	Until they were published that number could not be seen from outside, so when a list came back
	EMPTY there was no way to tell "the plug-in is wrong" from "the two documents genuinely read
	the same" without reading the source. Measured: two documents built by the same script, with
	different text, carry the SAME counters -- because a counter is a version number for the
	story's state, not a count of edits, and both files went through the same number of edits.
	Publishing the numbers turns that from a guess into a reading.

	READ-ONLY, all four. They are the application's own counters; a script that could set them
	would be able to make the panel report a change that never happened.

	Codes follow docs/ai-notes/kes-scriptid-registry.md (p = property, K = Kohaku, G = KCM) and
	were checked against ScriptingDefs.h / GenericID.h for collisions before use - none.
*/
enum KCMStoryScriptProperties
{
	p_KCMChangeCount      = 'pKGC',	// C = Change  - the all-changes counter (the one that decides)
	p_KCMTextChangeCount  = 'pKGT',	// T = Text    - characters inserted, removed or replaced
	p_KCMAttrChangeCount  = 'pKGA',	// A = Attr    - effective attributes, styles and overrides
	p_KCMOtherChangeCount = 'pKGO'	// O = Other   - the counter nothing measured has ever moved
};

/** The property KCM adds to the DOCUMENT object.

	`app.activeDocument.kcmTransparencyItemCount` = **how many entries are in the
	IXPManager's list of page items that carry transparency** (IXPManager::GetNumItemsWithXP).
	Read-only.

	**What it is for.** To make its marks translucent at PDF 1.3, KCM puts itself on that list
	  **for the duration of an export only** (KCMRingAdornment.cpp, section 5).
	  @warning **the list persists into the .indd and is not re-validated on reopening**
	    (measured against a control with nothing else differing). So there has to be a way to
	    ask from outside whether we **left ourselves on it when the document was saved** --
	    this is it. Save, close, reopen and read: what was written is what comes back.

	@warning **it does not count only what KCM put there** -- page items with real transparency
	  (drop shadows, opacity < 100, blend modes) are on the same list. To use it as a test,
	  compare against the same document that has not been through KCM.

	**Kept out of IDML**, like the story counters: this list is volatile session state, not the
	  "persistent data" a document interchange format is for. The mechanism is the same -- the
	  second VersionedScriptElementInfo in KCM.fr (kINXScriptManagerBoss + Provider{kNotSupported}).

	Code follows docs/ai-notes/kes-scriptid-registry.md (p = property, K = Kohaku, G = KCM) and was
	checked against ScriptingDefs.h / GenericID.h for collisions before use -- none. The check was
	itself validated first (existing codes 'move' / 'cflo' were confirmed to match) before reading
	'pKGx' as free: a search that matches nothing reports every candidate as free. */
enum KCMDocumentScriptProperties
{
	p_KCMTransparencyItemCount = 'pKGx'	// x = XP (transparency) - document.kcmTransparencyItemCount
};

#endif // __KCMScriptingDefs_h__

// End, KCMScriptingDefs.h.
