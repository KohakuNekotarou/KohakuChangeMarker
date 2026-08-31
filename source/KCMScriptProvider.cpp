//========================================================================================
//
//  KCMScriptProvider.cpp
//
//  Scripting: everything this plug-in publishes, served from ONE provider boss.
//
//      app.kcmStatus                     - the last message the panel put on its status line
//      app.kcmBookResult                 - the last book comparison, one line per chapter
//      stories[n].kcmChangeCount         - ITextModel's aggregate change counter
//      stories[n].kcmTextChangeCount     - characters inserted, removed or replaced
//      stories[n].kcmAttrChangeCount     - formatting, applied styles and overrides
//      stories[n].kcmOtherChangeCount    - everything else
//      document.kcmTransparencyItemCount - entries in the host's item-has-transparency list
//
//  **Every one of them is READ-ONLY.** No methods and no script objects -- the ones this plug-in
//  used to have (kescmToast and the rest) were removed and are not coming back; the panel is the
//  interface. The toolbox tool's identity (en_KCMTool) lives on the UI side with the tool.
//  @warning **do not write a total into this list.** It said "six" while there were seven, for
//    the whole time the document property existed -- and the doc on AccessProperty below said
//    "seven", so one file stated both. KCMScriptingDefs.h has the same rule and the same scar.
//
//  **THE STORY COUNTERS AND THE DOCUMENT PROPERTY ARE HIDDEN FROM IDML/INX.** A property on a
//  document-resident object is written into the user's IDML as an attribute -- measured, every
//  <Story> carried four of ours -- so KCM.fr declares them a second time, in an INX-only
//  resource, under Provider{kNotSupported}. Scripts see them exactly as before; the file format
//  does not. The two application properties need nothing: the application object is not in a
//  document's IDML. Full record: docs/ai-notes/kescm-bug-recheck-b11-2026-08-18.md.
//
//  **ONE BOSS, ONE PROVIDER BLOCK PER SCRIPT OBJECT**
//
//  These properties sit on three different script objects -- Application, Story and Document --
//  and that was once done with SEPARATE provider bosses, on the belief that one boss could not
//  keep them apart. That belief was wrong, and the file that was folded in here stated it in its
//  own header:
//
//      "a Provider block in the .fr puts EVERY property it lists onto EVERY object it lists"
//
//  That sentence is ALMOST right, and what it is about is a BLOCK -- never a boss. The guide's
//  own field table for the Provider element says it exactly:
//
//      Object    "properties and methods referenced by SUBSEQUENT fields are added" to it
//      Property  "added to the script object referenced by the PRECEDING Object ... field"
//
//  So a property lands on whatever Object stands BEFORE it -- not on every object in the block.
//  BPI.fr lists its two Objects back to back precisely because it wants the one property on
//  both. And the guide states outright that a provider element "can be defined (used) in
//  multiple places": the .fr may give the SAME boss more than one Provider block. basicshape
//  does it with Adobe's own kPageItemScriptProviderBoss -- three blocks inside one
//  VersionedScriptElementInfo whose Contexts list has a single entry.
//
//  KCM.fr therefore names this one boss in three blocks -- Object{Application} carrying the two
//  strings, Object{Story} the four counters, Object{Document} the transparency count -- and this
//  class serves them all, splitting on the ScriptID.
//
//  MEASURED on the running application right after the change:
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
//  refresh updated, which page a jump landed on and how much of it changed, why a comparison
//  could not start. That line is the plug-in's whole account of what it just did -- and the only
//  way to read it used to be to look at the screen. A script property turns that into one line
//  of JavaScript, which means it is also reachable over COM from PowerShell:
//
//      $app = New-Object -ComObject "InDesign.Application.2026"
//      $app.DoScript("app.kcmStatus", 1246973031)
//
//  It answers even when the panel is CLOSED: the status line is kept by the model half
//  (KCMModelNotify.cpp holds it in statics of its own, and KCMGetSessionStatus reads them back)
//  whatever happens to the panel widget, so a comparison run from a script and read back from a
//  script needs no panel on screen at all.
//
//  The four story counters were added at the user's request. The aggregate one is what decides
//  whether a story appears in the panel's Story Edits list (KCMStoryEdits::Compare, in
//  KCMStoryStamp.cpp, compares the target's reading with the source's and skips the story when
//  they match), and not being able to see it from outside had just cost a whole investigation: a
//  list came back EMPTY for two documents whose text plainly differed, with no way to tell "the
//  plug-in is broken" from "the two files genuinely read the same" without reading the source.
//  They read the same -- a counter is a VERSION NUMBER for the story's state, not a count of
//  edits, so two documents built by the same script with the same number of edits carry the same
//  counters however different the words are (measured 2026-08-08,
//  docs/ai-notes/text-change-counters-2026-08-08.md).
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
#include "ScriptingID.h"	// kInvalidScriptTargetError
#include "UIDList.h"
#include "WideString.h"

// Project includes:
#include "KCMID.h"
#include "KCMScriptingDefs.h"
#include "KCMModelNotify.h"	// KCMGetSessionStatus - the status line, kept on the model side (Task 9)
// KCMUIShared.h is deliberately NOT included: it was added once, but this provider called none
// of the widget-touching functions in it -- a dead dependency. What it reads is
// KCMGetSessionStatus (declared in KCMModelNotify.h), which is not a reverse dependency.
#include "KCMBookCompare.h"	// KCMGetBookResultText - the last book comparison, also in the module
#include "KCMStoryStamp.h"	// KCMStoryEdits::ReadStamp - the SAME reading the panel uses
#include "KCMRingAdornment.h"	// KCMGetNumItemsWithXP - document.kcmTransparencyItemCount
#include "KCMTextRead.h"		// the parallel run's switch and report (⚠temporary - see the header)

/** Serves every scripting addition this plug-in makes -- the properties listed at the top of
    this file, on three different script objects. One boss, because the .fr splits them by
    Provider block rather than by boss (see the file header). */
class KCMScriptProvider : public CScriptProvider
{
public:
	KCMScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	virtual ~KCMScriptProvider() {}

	/** Read (or refuse to write) one of the properties listed at the top of this file. Anything
	    not ours goes to the base class, which is what keeps the rest of the scripting working on
	    whichever object we were asked about. */
	virtual ErrorCode AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script);

private:
	/** app.kcmStatus / app.kcmBookResult - both come from the module, not from a widget. */
	ErrorCode ReadAppString(int32 id, ScriptID propID, IScriptRequestData* data, IScript* script);

	/** stories[n].kcm*ChangeCount - read off the story the script object names. */
	ErrorCode ReadStoryCounter(int32 id, ScriptID propID, IScriptRequestData* data, IScript* script);

	/** document.kcmTransparencyItemCount -- the XPManager's item-has-transparency list, counted.
	    **This is the one property here that reads the HOST's state rather than our own**: it
	    exists so that "did we leave ourselves on that list when the document was saved?" can be
	    answered from outside, and the list persists in the .indd. */
	ErrorCode ReadTransparencyItemCount(ScriptID propID, IScriptRequestData* data, IScript* script);

	/** app.kcmStoryReadCompare -- the migration's parallel run.

	    ★**THE ONLY PROPERTY HERE THAT ACCEPTS A PUT**, and the only one that is temporary. "on"
	    arms it, anything else disarms it; reading returns the last report. ⚠It goes when the
	    direct-read migration lands (docs/superpowers/plans/2026-08-31-kcm-story-direct-read.md). */
	ErrorCode WriteStoryReadCompare(ScriptID propID, IScriptRequestData* data, IScript* script);
	ErrorCode ReadStoryReadCompare(ScriptID propID, IScriptRequestData* data, IScript* script);

	/** Hand an int32 back to the script. The two numeric properties above both end this way. */
	ErrorCode ReturnInt32(int32 value, ScriptID propID, IScriptRequestData* data, IScript* script);
};

CREATE_PMINTERFACE(KCMScriptProvider, kKCMScriptProviderImpl)

ErrorCode KCMScriptProvider::AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script)
{
	const int32 id = propID.Get();

	const bool16 isAppString = (id == p_KCMStatus || id == p_KCMBookResult);
	const bool16 isStoryCounter = (id == p_KCMChangeCount || id == p_KCMTextChangeCount ||
								   id == p_KCMAttrChangeCount || id == p_KCMOtherChangeCount);
	const bool16 isDocXPCount = (id == p_KCMTransparencyItemCount);
	const bool16 isReadCompare = (id == p_KCMStoryReadCompare);

	if (!isAppString && !isStoryCounter && !isDocXPCount && !isReadCompare)
		return CScriptProvider::AccessProperty(propID, data, script);

	// **NO nil CHECK ON data OR script**, and that is measured rather than assumed. The framework
	// touches both before it can reach us: CScriptProvider::AccessProperties reads
	// data->GetRequestInfo() and returns early on a nil script, and AccessPropertyOnObjects reads
	// data->GetNumReturnData(...) before calling AccessProperty. The `if (data == nil) return
	// kFailure;` that stood here was therefore unreachable code returning the one value the guide
	// singles out: "If it returns kFailure, you will get an assert".
	// Read-only, every one of them. The declarations in KCM.fr say kReadOnly, so the engine
	// refuses an assignment before it ever reaches here; this is the backstop, and it refuses
	// rather than quietly accepting a value that would then not be there on the next read.
	//
	// MEASURED: `app.kcmStatus = "x"` comes back as error 30474 (the engine's own "this property
	// is read-only" message), so the sentence above is not a hope -- the engine really does stop
	// it, and this line is not reached in normal use.
	//
	// Refused the way the base class refuses a put on the read-only properties IT owns: all five
	// of CScriptProvider's own read-only refusals end with this same call, as do snippetrunner
	// (SnpRunnableScriptProvider) and basicshape (BscShpScriptProvider). A bare kFailure says
	// nothing about why -- and the guide is blunter than that: "If it returns kFailure, you will
	// get an assert", which for a plug-in driven by scripts under a Debug build is a stopped test
	// rather than a message.
	// @warning KBS had already moved to this call (KBSScriptProvider.cpp) and this file, written
	//   from that one, did not bring the change with it.
	// ★**ONE EXCEPTION, AND IT IS DECLARED kReadWrite IN KCM.fr.** Everything else here refuses a
	//   put; app.kcmStoryReadCompare accepts one because arming the parallel run is what it is for.
	//   ⚠The two have to agree: a property the resource calls kReadOnly can never reach the branch
	//     below, and one the resource calls kReadWrite would silently accept nothing if this line
	//     refused it. The pair is the contract.
	if (data->IsPropertyPut())
	{
		if (isReadCompare)
			return this->WriteStoryReadCompare(propID, data, script);
		return Utils<IScriptErrorUtils>()->SetReadOnlyPropertyErrorData(data, propID);
	}

	if (!data->IsPropertyGet())
		return CScriptProvider::AccessProperty(propID, data, script);

	// **NO "a run is going" GUARD HERE**, and that is a decision rather than an omission. KBS has
	// one (KBSRunGuard::IsAnyRunning): a run of its stands behind a modal progress bar, the bar
	// PUMPS EVENTS, so a COM read dispatched mid-run would be answered with the PREVIOUS run's
	// sentence and a polling harness would read that as "finished". KCM shows the same kind of bar
	// (the TaskProgressBar inside KCMDoMarkChangesDoc), so the machinery is there -- what is
	// missing is the reader: every harness that reads these properties invokes the action and
	// reads afterwards, on ONE COM connection, where the read cannot start until the comparison
	// has returned.
	// @warning it stops being true the moment anything POLLS from a second connection (a Start-Job
	//   in PowerShell, a CEP panel on a timer). Then this needs KBS's shape: a flag the comparison
	//   sets, and a "busy" sentence returned instead of the stale one.

	if (isReadCompare)
		return this->ReadStoryReadCompare(propID, data, script);
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
	// The script object names the story to read. Same move as BPIScriptProvider, which takes the
	// page item whose data is wanted straight off the script object.
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

	// The counters are small in practice (single or double digits after ordinary editing), so the
	//   cast cannot lose anything a reader would notice.
	return this->ReturnInt32(static_cast<int32>(value), propID, data, script);
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

	// A script compares this with 0 to answer "was anything left on the list?" -- see ReturnInt32
	//   for why it must not arrive as a string.
	return this->ReturnInt32(count, propID, data, script);
}

ErrorCode KCMScriptProvider::WriteStoryReadCompare(ScriptID propID, IScriptRequestData* data,
												  IScript* script)
{
	ScriptData inputData;
	ErrorCode result = data->ExtractRequestData(propID, inputData);
	if (result != kSuccess)
		return result;

	PMString command;
	result = inputData.GetPMString(command);
	if (result != kSuccess)
		return result;
	command.SetTranslatable(kFalse);

	// ★"on" ARMS IT AND ANYTHING ELSE DISARMS IT - the same convention KIDMCP uses for
	//   app.kmcpHttp. Case-insensitive, because a switch that answers differently to "On" is a
	//   switch that will be reported as broken.
	PMString armed;
	armed.SetCString("on", PMString::kEncodingASCII);
	armed.SetTranslatable(kFalse);

	KCMSetStoryReadCompare((command.Compare(kFalse /*caseSensitive*/, armed) == 0) ? kTrue : kFalse);
	return kSuccess;
}

//----------------------------------------------------------------------------------------
ErrorCode KCMScriptProvider::ReadStoryReadCompare(ScriptID propID, IScriptRequestData* data,
												 IScript* script)
{
	PMString report;
	KCMGetStoryReadCompareReport(report);

	ScriptData outputData;
	outputData.SetWideString(WideString(report));
	data->AppendReturnData(script, propID, outputData);
	return kSuccess;
}

//----------------------------------------------------------------------------------------
ErrorCode KCMScriptProvider::ReturnInt32(int32 value, ScriptID propID, IScriptRequestData* data,
										 IScript* script)
{
	// **Int32Type in the .fr, for both of the callers.** A script gets a number it can compare
	//   with < and ==, rather than a string it would have to parse. Written once so that the two
	//   numeric properties cannot come to disagree about how a number is handed back; the one
	//   string property (ReadAppString) keeps its own three lines, there being nothing to share.
	ScriptData outputData;
	outputData.SetInt32(value);
	data->AppendReturnData(script, propID, outputData);
	return kSuccess;
}

// End, KCMScriptProvider.cpp.
