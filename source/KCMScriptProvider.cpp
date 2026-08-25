//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  Scripting: everything this plug-in publishes, served from ONE provider boss.
//
//      app.kcmStatus                   - the last message the panel put on its status line
//      app.kcmBookResult               - the last book comparison, one line per chapter
//      stories[n].kcmChangeCount       - ITextModel's aggregate change counter
//      stories[n].kcmTextChangeCount   - characters inserted, removed or replaced
//      stories[n].kcmAttrChangeCount   - formatting, applied styles and overrides
//      stories[n].kcmOtherChangeCount  - everything else
//
//  All six are READ-ONLY. No methods and no script objects - the ones this plug-in used to have
//  (kescmToast and the rest) were removed 2026-08-05 and are not coming back; the panel is the
//  interface. The toolbox tool's identity (en_KCMTool) lives on the UI side with the tool.
//
//  ★★★THE FOUR STORY COUNTERS ARE HIDDEN FROM IDML/INX (2026-08-18, bug recheck B11). A property
//  on a document-resident object is written into the user's IDML as an attribute - measured, every
//  <Story> carried four of ours - so KCM.fr declares them a second time, in an INX-only resource,
//  under Provider{kNotSupported}. Scripts see them exactly as before; the file format does not.
//  The two application properties need nothing: the application object is not in a document's IDML.
//  Full record: docs/ai-notes/kescm-bug-recheck-b11-2026-08-18.md.
//
//  ★★★ ONE BOSS, TWO PROVIDER BLOCKS (2026-08-15)
//
//  These properties sit on two different script objects - Application and Story - and until
//  2026-08-15 that was done with TWO provider bosses, on the belief that one boss could not keep
//  them apart. That belief was wrong, and the file that has now been folded in here stated it in
//  its own header:
//
//      "a Provider block in the .fr puts EVERY property it lists onto EVERY object it lists"
//
//  That sentence is ALMOST right, and what it is about is a BLOCK - never a boss. The guide's own
//  field table for the Provider element says it exactly (vol1-11:1300 and :1302):
//
//      Object    "properties and methods referenced by SUBSEQUENT fields are added" to it
//      Property  "added to the script object referenced by the PRECEDING Object ... field"
//
//  So a property lands on whatever Object stands BEFORE it - not on every object in the block.
//  BPI.fr:603-612 lists its two Objects back to back precisely because it wants the one property
//  on both. And the guide states outright that a provider element "can be defined (used) in
//  multiple places" (vol1-11:1237): the .fr may give the SAME boss more than one Provider block.
//  basicshape does it with Adobe's own kPageItemScriptProviderBoss - three blocks inside one
//  VersionedScriptElementInfo whose Contexts list has a single entry (BscShp.fr:370-404, :317).
//
//  KCM.fr therefore names this one boss in two blocks - Object{Application} carrying the two
//  strings, Object{Story} carrying the four counters - and this class serves both, splitting on
//  the ScriptID.
//
//  ★MEASURED on the running application right after the change (2026-08-15):
//
//      'kcmStatus' in app        -> true      'kcmChangeCount' in story -> true (a number)
//      'kcmChangeCount' in app   -> FALSE     'kcmStatus' in story      -> FALSE
//
//  - the two FALSEs are the whole point: neither object grew the other's members. Editing the
//  text then moved the story's text counter (2 -> 3) while the attribute counter stayed put, so
//  the numbers coming back are live readings rather than a stale snapshot.
//
//  WHY THESE ARE PUBLISHED AT ALL
//
//  The panel answers in one line: how many pages were compared and how many changed, what a
//  refresh updated, which page a jump landed on and how much of it changed, why a comparison could
//  not start. That line is the plug-in's whole account of what it just did - and until 2026-08-06
//  the only way to read it was to look at the screen. A script property turns that into one line
//  of JavaScript, which means it is also reachable over COM from PowerShell:
//
//      $app = New-Object -ComObject "InDesign.Application.2026"
//      $app.DoScript("app.kcmStatus", 1246973031)
//
//  It answers even when the panel is CLOSED: KCMSetStatus keeps the string in the module
//  (gSessionStatus) whatever happens to the panel widget, so a comparison run from a script and
//  read back from a script needs no panel on screen at all.
//
//  The four story counters were added 2026-08-15 at the user's request. The aggregate one is what
//  decides whether a story appears in the panel's Story Edits list (KCMStoryStamp.cpp compares
//  the target's reading with the source's and skips the story when they match), and not being able
//  to see it from outside had just cost a whole investigation: a list came back EMPTY for two
//  documents whose text plainly differed, with no way to tell "the plug-in is broken" from "the
//  two files genuinely read the same" without reading the source. They read the same - a counter
//  is a VERSION NUMBER for the story's state, not a count of edits, so two documents built by the
//  same script with the same number of edits carry the same counters however different the words
//  are (measured 2026-08-08, docs/ai-notes/text-change-counters-2026-08-08.md).
//
//  READ-ONLY matters for both kinds. A script that could write the status line could make the
//  panel appear to say something it never said; one that could write a counter could make the
//  panel report a change that never happened.
//
//  Shape follows the SDK's basicpersistinterface sample, the official example of adding a property
//  to an object the application already has.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IScript.h"
#include "IScriptErrorUtils.h"	// SetReadOnlyPropertyErrorData - how the base class refuses a put
#include "IScriptRequestData.h"

// General includes:
#include "CScriptProvider.h"
#include "ScriptData.h"
#include "ScriptingID.h"	// kInvalidScriptTargetError (ScriptingID.h:286)
#include "UIDList.h"
#include "WideString.h"

// Project includes:
#include "KCMID.h"
#include "KCMScriptingDefs.h"
#include "KCMModelNotify.h"	// KCMGetSessionStatus - the status line, kept on the model side (Task 9)
// (★KCMUIShared.h は include しない ---- 2026-08-13 の Task 5 で一度足したが、この ScriptProvider は
//  widget に触る8本を1つも呼んでいなかった＝**死んだ依存**だったので外した。読んでいるのは
//  KCMGetSessionStatus(★宣言は KCMModelNotify.h。2026-08-13 の Task 9 に KCMCore.h から移った
//  ---- この注記だけが旧ヘッダー名のまま残っていた＝2026-08-18 の再検査 B11 で訂正。すぐ上の include が
//  最初から正しく書いている＝**同じ問いに2つの答えがあった**)だけで、これは逆流ではない。)
#include "KCMBookCompare.h"	// KCMGetBookResultText - the last book comparison, also in the module
#include "KCMStoryStamp.h"	// KCMStoryEdits::ReadStamp - the SAME reading the panel uses
#include "KCMRingAdornment.h"	// KCMGetNumItemsWithXP - document.kcmTransparencyItemCount (2026-08-20)

/** Serves every scripting addition this plug-in makes: two read-only strings on the application
    object and four read-only counters on the story object. One boss, because the .fr splits them
    by Provider block rather than by boss (see the file header). */
class KCMScriptProvider : public CScriptProvider
{
public:
	KCMScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	virtual ~KCMScriptProvider() {}

	/** Read (or refuse to write) one of our seven properties. Anything not ours goes to the base
	    class, which is what keeps the rest of the scripting working on whichever object we were
	    asked about. */
	virtual ErrorCode AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script);

private:
	/** app.kcmStatus / app.kcmBookResult - both come from the module, not from a widget. */
	ErrorCode ReadAppString(int32 id, ScriptID propID, IScriptRequestData* data, IScript* script);

	/** stories[n].kcm*ChangeCount - read off the story the script object names. */
	ErrorCode ReadStoryCounter(int32 id, ScriptID propID, IScriptRequestData* data, IScript* script);

	/** document.kcmTransparencyItemCount - the XPManager's item-has-transparency list, counted
	    (2026-08-20). ★This is the one property here that reads the HOST's state rather than our
	    own: it exists so that "did we leave ourselves on that list when the document was saved?"
	    can be answered from outside, and the list persists in the .indd. */
	ErrorCode ReadTransparencyItemCount(ScriptID propID, IScriptRequestData* data, IScript* script);
};

CREATE_PMINTERFACE(KCMScriptProvider, kKCMScriptProviderImpl)

ErrorCode KCMScriptProvider::AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script)
{
	const int32 id = propID.Get();

	const bool16 isAppString = (id == p_KCMStatus || id == p_KCMBookResult);
	const bool16 isStoryCounter = (id == p_KCMChangeCount || id == p_KCMTextChangeCount ||
								   id == p_KCMAttrChangeCount || id == p_KCMOtherChangeCount);
	const bool16 isDocXPCount = (id == p_KCMTransparencyItemCount);

	if (!isAppString && !isStoryCounter && !isDocXPCount)
		return CScriptProvider::AccessProperty(propID, data, script);

	// ★NO nil CHECK ON data OR script, and that is measured rather than assumed (2026-08-18, bug
	// recheck B11). The framework touches both before it can reach us: CScriptProvider
	// ::AccessProperties reads data->GetRequestInfo() and returns early on a nil script
	// (CScriptProvider.cpp:262-265), and AccessPropertyOnObjects reads data->GetNumReturnData(...)
	// at :148-155 before calling AccessProperty at :168. The `if (data == nil) return kFailure;`
	// that stood here was therefore unreachable code returning the one value the guide singles out:
	// "If it returns kFailure, you will get an assert" (vol1-11:456).
	// Read-only, all six. The declarations in KCM.fr say kReadOnly, so the engine refuses an
	// assignment before it ever reaches here; this is the backstop, and it refuses rather than
	// quietly accepting a value that would then not be there on the next read.
	//
	// ★MEASURED 2026-08-16: `app.kcmStatus = "x"` comes back as error 30474 "'kcmStatus' のプロパティ
	// は読み取り専用です。" - so the sentence above is not a hope, the engine really does stop it and
	// this line is not reached in normal use.
	//
	// ★Refused the way the base class refuses a put on the read-only properties IT owns: all five of
	// CScriptProvider's own read-only refusals end with this same call (CScriptProvider.cpp:369,
	// 1096, 1116, 1135, 1250), as do snippetrunner (SnpRunnableScriptProvider.cpp:226) and basicshape
	// (BscShpScriptProvider.cpp:268). A bare kFailure says nothing about why - and the guide is
	// blunter than that: "If it returns kFailure, you will get an assert" (vol1-11:456), which for a
	// plug-in that gets driven by scripts under a Debug build is a stopped test rather than a
	// message. ⚠KBS had already moved to this call (KBSScriptProvider.cpp:93, 2026-08-11); this file
	// was written from that one and did not bring the change with it.
	if (data->IsPropertyPut())
		return Utils<IScriptErrorUtils>()->SetReadOnlyPropertyErrorData(data, propID);

	if (!data->IsPropertyGet())
		return CScriptProvider::AccessProperty(propID, data, script);

	// ★NO "a run is going" GUARD HERE, and that is a decision rather than an omission (2026-08-16).
	// KBS has one (KBSScriptProvider.cpp:107 - KBSRunGuard::IsAnyRunning): a run of its stands behind
	// a modal progress bar, the bar PUMPS EVENTS, so a COM read dispatched mid-run would be answered
	// with the PREVIOUS run's sentence and a polling harness would read that as "finished".
	// KCM shows the same kind of bar (the TaskProgressBar inside KCMDoMarkChangesDoc), so the machinery is
	// there - what is missing is the reader: every harness that reads these properties invokes the
	// action and reads afterwards, on ONE COM connection, where the read cannot start until the
	// comparison has returned.
	// ⚠It stops being true the moment anything POLLS from a second connection (a Start-Job in
	// PowerShell, a CEP panel on a timer). Then this needs KBS's shape: a flag the comparison sets,
	// and a "busy" sentence returned instead of the stale one.

	if (isAppString)
		return this->ReadAppString(id, propID, data, script);
	if (isDocXPCount)
		return this->ReadTransparencyItemCount(propID, data, script);
	return this->ReadStoryCounter(id, propID, data, script);
}

ErrorCode KCMScriptProvider::ReadAppString(int32 id, ScriptID propID, IScriptRequestData* data, IScript* script)
{
	PMString value;
	if (id == p_KCMStatus)
		KCMGetSessionStatus(value);		// the panel's status line
	else
		KCMGetBookResultText(value);		// the last book comparison, one line per chapter

	ScriptData outputData;
	outputData.SetWideString(WideString(value));
	data->AppendReturnData(script, propID, outputData);
	return kSuccess;
}

ErrorCode KCMScriptProvider::ReadStoryCounter(int32 id, ScriptID propID, IScriptRequestData* data, IScript* script)
{
	// The script object names the story to read. Same move as BPIScriptProvider.cpp:144, which
	// takes the page item whose data is wanted straight off the script object.
	UIDList target(script);
	if (target.Length() < 1)
		return kInvalidScriptTargetError;

	KCMStoryStamp stamp;
	if (!KCMStoryEdits::ReadStamp(target.GetRef(0), stamp))
		return kInvalidScriptTargetError;	// handed something that is not a story

	uint32 value = stamp.fChangeCount;
	if (id == p_KCMTextChangeCount)
		value = stamp.fTextCount;
	else if (id == p_KCMAttrChangeCount)
		value = stamp.fAttrCount;
	else if (id == p_KCMOtherChangeCount)
		value = stamp.fOtherCount;

	// Int32Type in the .fr, so the script gets a number it can compare with < and ==, rather than
	// a string it would have to parse. The counters are small in practice (single or double digits
	// after ordinary editing), so the cast cannot lose anything a reader would notice.
	ScriptData outputData;
	outputData.SetInt32(static_cast<int32>(value));
	data->AppendReturnData(script, propID, outputData);
	return kSuccess;
}

ErrorCode KCMScriptProvider::ReadTransparencyItemCount(ScriptID propID, IScriptRequestData* data, IScript* script)
{
	// The script object names the document to read - same move as ReadStoryCounter above, except
	// that here only the DATABASE is wanted: the item-has-transparency list is per-document state
	// held by the host's XPManager, not something hanging off a particular UID of ours.
	UIDList target(script);
	IDataBase* const db = target.GetDataBase();
	if (db == nil)
		return kInvalidScriptTargetError;

	const int32 count = KCMGetNumItemsWithXP(db);
	if (count < 0)
		return kInvalidScriptTargetError;	// XPManager could not be reached for that database

	// Int32Type in the .fr, like the story counters - a script compares this with 0 to answer
	// "was anything left on the list?", so it must not arrive as a string.
	ScriptData outputData;
	outputData.SetInt32(count);
	data->AppendReturnData(script, propID, outputData);
	return kSuccess;
}

// End, KCMScriptProvider.cpp.
