//========================================================================================
//
//  KCMScriptingDefs.h
//
//  ScriptIDs (four-character codes) published by the MODEL half of KCM.
//
//  WHAT IS EXPOSED -- PROPERTIES, every one of them READ-ONLY, on three different script objects:
//    Application  app.kcmStatus, app.kcmBookResult and the others listed below
//    Story        the four change counters below
//    Document     kcmTransparencyItemCount
//  ...and, since 2026-09-14, ONE METHOD on the application: app.kcmSaveOriginXml(file).
//  ⚠**"No methods and no script objects" stood in this line for months, and it has to be read for
//    what it was about before it is quoted again.** It was about kescmToast and its neighbours --
//    a scripting API that DROVE THE PRODUCT from outside, marking changes and arming peeks -- and
//    that is still not coming back: the panel is the interface. The one method below drives
//    nothing. It writes out a thing the panel cannot reach at all any more, the flyout item that
//    used to do it having been removed the same day (the user's decision: keep the writer, reach
//    it from a script, and let the CALLER name the file rather than the plug-in choosing the
//    Desktop).
//  ⚠No script OBJECTS, still: nothing here hangs a new object off app.
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
	p_KCMOriginStatus = 'pKGi',		// i = INX (the code the throwaway probe app.kcmInxProbe had,
								// 2026-09-08 to 09-09; NEVER REGISTERED WITH ADOBE, so re-used on
								// 2026-09-12). app.kcmOriginStatus - the Task Start origin, in
								// one line: held, document, time, bytes, shape, stamps, peek.
	p_KCMResourceSnapshot = 'pKGs',	// s = snapshot. app.kcmResourceSnapshot - the Resources mode's
								// export, measured from outside: "<bytes> bytes, <ms> ms" for the
								// active document, or "FAILED: <which step>". ★The code 'pKGs' was
								// retired in 2026-06-24 (a single-page source parameter) and was
								// NEVER registered with Adobe, so re-using it cannot disagree with
								// anything registered -- the same standing as 'pKGx' in 2026-08-20.
								// ⚠Checked against the code, not the ledger: the ledger still says
								// 'pKGr' is free, and app.kcmStoryRows has been using it since
								// 2026-09-08.
	p_KCMResourceDiff = 'pKGd',	// d = diff. app.kcmResourceDiff - the Resources mode's comparison of
								// the two ARMED documents, as TSV: a summary line, a header line and
								// one line per definition that differs. ★It reads the SAME Target and
								// Source the panel does (KCMArmedTargetDB / KCMArmedSourceDB), which
								// is what makes a reading here a statement about the product.
								// ⚠Checked against the code before use, not against the ledger, and
								// the check was validated first on a code that DOES exist ('cflo' in
								// ScriptingDefs.h): a pattern that matches nothing reports every
								// candidate as free. 'pKGd' occurs nowhere in the SDK or in the four
								// Kohaku plug-ins, and the registry has no entry for it.
	p_KCMStatus     = 'pKGm',	// p = property, K = Kohaku, G = KCM, m = message (app.kcmStatus)
	p_KCMBookResult = 'pKGb',	// b = book. app.kcmBookResult - the last book comparison, one line
								// per chapter ("name<TAB>state"). Checked against the registry in
								// docs/ai-notes/kes-scriptid-registry.md before use (2026-08-11).

	p_KCMStoryRows  = 'pKGr',	// r = rows. app.kcmStoryRows - the whole Story Edits list as TSV,
								// one line per PARENT row and one per CHANGE under it.
								// ★★★WHY IT EXISTS (2026-09-08). The list's own cells are drawn by
								// hand (KCMStoryCellView), so nothing outside can read them: a
								// test that wanted to know whether the ruby reading, the Mono/Group
								// word or a footnote's number had appeared had to PHOTOGRAPH the
								// panel and read the picture. Measured that day - it was the single
								// biggest cost of checking the footnote work. This is the same
								// answer KBS reached with app.kfcResults, and for the same stated
								// reason: **a count proves nothing**; the row's own words do.
								// ⚠READ-ONLY like every other property here.
								// ★THE CODE IS A REUSE. 'pKGr' was app.kcmStoryReadCompare (the
								// direct-read migration's parallel run) from 2026-08-31 to
								// 2026-09-03 - never registered with Adobe, never shipped, and the
								// registry says the code is free (kes-scriptid-registry.md). That
								// property was also the only READ-WRITE one KCM has ever had.
};

/** THE ONE METHOD KCM publishes (2026-09-14), on the application object.

		app.kcmSaveOriginXml("C:\\work\\origin.xml")   ->  0

	It writes the held Task Start origin's XML to that file EXACTLY as Task Start took it, and
	returns a status rather than throwing: **0** written / **1** no origin is held / **2** the file
	could not be created / **3** the write failed / **4** the file argument could not be read.
	@warning **the list of numbers lives in THREE places and they are one edit** -- here,
	  KCMOrigin.h (which decides them) and the return-value string in KCM.fr (which is what a
	  reader of the DOM sees). KCMOrigin.h is the authority; these two restate it.

	WHY A METHOD, when everything else here is a property. It DOES something -- it writes a file --
	and it needs an argument to do it with. A property that wrote a file when it was read would be
	a property whose meaning depends on when you look at it, and there would be nowhere to put the
	path. This is the same shape as the official example, snippetrunner's "save snip log"
	(SnipRun.fr:1485-1497 and SnipRunScriptProvider::HandleEvent_SaveSnipLog), which likewise takes
	the file as keyAEFile and answers with "Status: 0 if ok, non-zero if error".

	★IT IS NOT IN ANY IDML. Properties on document-resident objects are (see the warning at the top
	  of this file); METHODS are not serialised at all, so the second, INX-only resource in KCM.fr
	  needs no line for this one and deliberately has none.

	Code: e = method, K = Kohaku, G = KCM, s = save -- the scheme at the top of this file.
	⚠**'eKGs' is free, and that was measured rather than assumed** (2026-09-14): 0 hits in
	  source/ (the SDK's ScriptingDefs.h and GenericID.h included), 0 in the four Kohaku plug-ins'
	  own ScriptingDefs headers, and no entry in docs/ai-notes/kes-scriptid-registry.md -- ITS
	  RETIRED ROWS READ TOO, which is where the 2026-08-18 near-miss came from ('eKGc' and 'eKGr'
	  looked free and were old KESCM methods). The search was validated first on codes that DO
	  exist ('pKGs' in this very file, 'cflo' and 'move' in the SDK), because a pattern that
	  matches nothing reports every candidate as free.
	⚠**Registered with Adobe? NOT YET** -- the seven that are were accepted on 2026-08-17. This one
	  goes in with the next submission, as a code/name pair, so the DOM name is settled now.
*/
enum KCMScriptMethods
{
	e_KCMSaveOriginXml = 'eKGs',	// s = save. app.kcmSaveOriginXml(file)

	// ★i = idml. app.kcmSaveOriginIdml(file) - the same origin, wrapped in a real IDML package
	//   (2026-09-15). The snapshot is already an IDML's designmap, so this only adds the container.
	//   ⚠'eKGi' was MEASURED free, not assumed: 0 hits in source/ and 0 in the registry ledger,
	//     with the search validated on 'eKGs', which does exist and was found (4 hits).
	//   ⚠'eKGd' looked free in source/ but IS in the ledger - the two have to agree, so the ledger
	//     is the one that decides.
	//   ⚠Not registered with Adobe yet: it goes in with 'eKGs', 'nKGp' and 'pKGx' at the next
	//     submission (the unit of registration is the code-and-name pair, so the name is settled now).
	e_KCMSaveOriginIdml = 'eKGi',

	// ★A MEASURING DOOR, not a feature (2026-09-14). app.kcmProbePdfRoute() runs the experiment
	// in KCMPdfSpike.cpp on the active document's first page and returns the whole reading as one
	// string, a line per step. It exists because the Before/After report has to stop writing
	// temporary PDFs to disk (the user's ask that day), and the two routes that could replace
	// them - a PDF made in memory, and the page's items carried across as a snippet - are both
	// things the SDK describes but neither the SDK nor this plug-in has ever run.
	// ⚠**It reads; it does not change the active document.** What it imports goes into a
	//   windowless document of its own, which is closed again before the answer comes back.
	// ⚠**'eKGv' is free, and that was measured rather than assumed** (2026-09-14), the same way
	//   'eKGs' was earlier the same day: 0 hits in source/, and the registry
	//   (docs/ai-notes/kes-scriptid-registry.md) has no row for it - ITS RETIRED ROWS READ TOO,
	//   which is what caught 'eKGp' (KESCM's old kescmSetPrintMarks) on the first try here. The
	//   search was validated on 'eKGs', which does exist and was found.
	// ⚠**Registered with Adobe? NOT YET** - it goes in with the next submission if it is still
	//   here. If the report ends up not needing it, retire the row rather than reusing the code.
	e_KCMProbePdfRoute = 'eKGv',	// v = vector: is there a route that keeps the pages vector without a file?

	// ★★THE STORY TEXT ROUND TRIP, DRIVEN WITHOUT A DIALOG (2026-09-17, the user's go-ahead: "script
	//   extensions if they are needed"). An import of "tags kept, contents emptied" crashed InDesign,
	//   and the user asked for every case that can happen - HTML mangled by hand included - to be
	//   built, imported and taken in. Each of those goes through a file dialog and a confirmation
	//   from the menu, so a run of dozens was not possible; these four are the menu items' own model
	//   calls with the dialogs left out, and each answers with the sentence the status line shows.
	//   ⚠**THE DOCUMENT IS THE ACTIVE ONE**, exactly as for the menu items.
	//   ⚠**kcmTakeInAllStories DOES NOT ASK.** The menu asks because a person pressed one item for the
	//     whole document; a script that calls this has already decided.
	//   ⚠'eKGn' / 'eKGw' / 'eKGe' / 'eKGq' were MEASURED free: 0 hits anywhere in the tree (headers,
	//     .fr and the notes), and no row in the registry ledger, whose used letters for eKG* are
	//     c r s v i m D x o h u a d t p - retired rows counted, 'm' 'x' and 'a' avoided for that
	//     reason. The same search form found 'eKGs'. ⚠Not registered with Adobe yet.
	e_KCMImportStoryText = 'eKGn',	// n = in.    app.kcmImportStoryText(file)  -> the import's sentence
	e_KCMTakeInAllStories = 'eKGw',	// w = write. app.kcmTakeInAllStories()     -> "N changes taken in..."
	e_KCMExportStoryText = 'eKGe',	// e = export. app.kcmExportStoryText(folder) -> "exported N ... to <folder>"
	e_KCMStopComparison = 'eKGq',	// q = quit.  app.kcmStopComparison()       -> the status line after it

	// ★★ONE CHANGE, AND ONE STORY, TAKEN IN FROM A SCRIPT (2026-09-17 afternoon). The user asked for the
	//   order checks - "1 became 1..5, and the user takes in 3, then 5, then 4, then 2: does the order
	//   hold?" and "one change taken in on its child row, the rest on the story's row" - which the
	//   all-stories method above cannot drive. The change is named by WORDS it holds rather than by an
	//   index, because the index space moves under a test as changes are taken in (the replaced ones stay
	//   in the list). ⚠No confirmation here either: the panel asks before a second "+" goes in alone, a
	//   script has already decided.
	//   ⚠'eKGk' / 'eKGy' were MEASURED free: 0 hits in headers, .fr, sources, notes, scripts; the same
	//     search form finds 'eKGs'. The parameters are Adobe's own p_Index and p_Contents. ⚠Not registered.
	e_KCMTakeInChange = 'eKGk',		// k = keep one. app.kcmTakeInChange(storyRow, words) -> the take-in's sentence
	e_KCMTakeInStory = 'eKGy',		// y = storY.   app.kcmTakeInStory(storyRow)          -> "N changes taken in..."

	// ★THE .DOCX ROAD OF THE EXPORT, WITHOUT A DIALOG (2026-09-19). The menu item opens the system's
	//   folder chooser, which a test cannot answer; this is the same model call with the folder
	//   handed in, exactly as 'eKGe' is for the HTML road.
	//   ⚠'eKGf' was MEASURED free: 0 hits in the tree, no row in the registry ledger (whose used
	//     letters for eKG* are a c d D e h i k m n o p q r s t u v w x y). The same search form finds
	//     'eKGe'. ⚠Not registered with Adobe.
	e_KCMExportStoryDocx = 'eKGf',	// f = file for Word. app.kcmExportStoryDocx(folder) -> "exported N ... to <folder>"

	// ★"UNDO THE RESTORE" FROM A SCRIPT (2026-09-19 night, the user: "let us implement the proposal").
	//   The take-in has had a script door since 09-17 (eKGk / eKGy); its undo had none, so checking the
	//   four orders of "take out, put back" that day took a right click, a menu choice and a read for
	//   every one of twelve presses. This is the mirror of eKGk: the change is named by WORDS it holds,
	//   among the ones already taken in (the replaced ones the panel keeps listing), and the answer is
	//   the sentence the status line shows.
	//   ⚠'eKGb' was MEASURED free: 0 files in the four Kohaku plug-ins, docs, work and skills, 0 in
	//     ScriptingDefs.h / GenericID.h, and no row in the registry ledger (whose used letters for eKG*
	//     are a c d D e f h i k m n o p q r s t u v w x y). The same search form found 'eKGk' (3 files).
	//     ⚠Not registered with Adobe.
	e_KCMUndoRestoreChange = 'eKGb',	// b = back. app.kcmUndoRestore(storyRow, words) -> the undo's sentence

	// ★A MEASURING DOOR FOR THE "Table" ROW (2026-09-19 night, KCMTableCopySpike.h): can the Task
	//   Start copy's table be copied over the live one with kCopyStoryRangeCmdBoss, across documents,
	//   styles included. ⚠'eKGz' was MEASURED free: 0 files under source/sdksamples, docs, work and
	//   .claude. ⚠Not registered with Adobe - a spike, to be retired with the file once the answer is in.
	e_KCMProbeTableCopy = 'eKGz'		// z = the last letter, for the last experiment of the night. app.kcmProbeTableCopy(storyRow, tableOrdinal) -> one line per step
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
