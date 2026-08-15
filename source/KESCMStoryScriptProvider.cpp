//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  Scripting: ITextModel's four change counters, read straight off a story.
//
//      app.documents[0].stories[2].kcmChangeCount        - the aggregate
//      app.documents[0].stories[2].kcmTextChangeCount    - characters
//      app.documents[0].stories[2].kcmAttrChangeCount    - formatting
//      app.documents[0].stories[2].kcmOtherChangeCount   - everything else
//
//  WHY THIS EXISTS (2026-08-15, at the user's request)
//
//  The aggregate counter is what decides whether a story appears in the panel's Story Edits list:
//  KESCMStoryStamp.cpp compares the target's reading with the source's and skips the story when
//  they match. That number could not be seen from outside, and the day this was written that cost
//  a whole investigation - a Story Edits list came back EMPTY for two documents whose text plainly
//  differed, and there was no way to tell "the plug-in is broken" from "the two files genuinely
//  read the same" without reading the source code.
//
//  They read the same. Both documents had been built by the same script, in the same order, with
//  the same number of edits - and a counter is a VERSION NUMBER for the story's state, not a count
//  of edits (measured 2026-08-08, docs/ai-notes/text-change-counters-2026-08-08.md). Same number of
//  edits, same counter, however different the words are. Story Edits therefore needs a target that
//  was SAVED FROM the source and then changed; that is the relationship it is built to read, and
//  KESCMStoryStamp.h says so in its header comment.
//
//  With these four properties that whole diagnosis is one line of JavaScript, on either document,
//  without a panel and without a screenshot.
//
//  READ-ONLY, all four. They are the application's own counters. A script that could write them
//  would be able to make the panel report a change that never happened.
//
//  WHY A SEPARATE PROVIDER BOSS from KESCMScriptProvider (app.kcmStatus / app.kcmBookResult):
//  a Provider block in the .fr puts EVERY property it lists onto EVERY object it lists
//  (BPI.fr:603-612 lists two objects deliberately, to put one property on both). Merging the two
//  would grow app.kcmChangeCount and stories[n].kcmStatus, which are both nonsense. Splitting by
//  object is what the official candlechart sample does as well (kCdlChartScriptProviderBoss and
//  kCdlStockScriptProviderBoss).
//
//  Shape follows the SDK's basicpersistinterface sample, which is the official example of adding
//  a property to an object the application already has.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IScript.h"
#include "IScriptRequestData.h"

// General includes:
#include "CScriptProvider.h"
#include "ScriptData.h"
#include "ScriptingID.h"	// kInvalidScriptTargetError (ScriptingID.h:286)
#include "UIDList.h"

// Project includes:
#include "KESCMID.h"
#include "KESCMScriptingDefs.h"
#include "KESCMStoryStamp.h"	// KESCMStoryEdits::ReadStamp - the SAME reading the panel uses

/** Serves the four change-counter properties on the story object. Read-only, all of them. */
class KESCMStoryScriptProvider : public CScriptProvider
{
public:
	KESCMStoryScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	virtual ~KESCMStoryScriptProvider() {}

	/** Read (or refuse to write) one of our four counters. Anything not ours goes to the base
	    class, which is what keeps the rest of the story's scripting working. */
	virtual ErrorCode AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script);
};

CREATE_PMINTERFACE(KESCMStoryScriptProvider, kKESCMStoryScriptProviderImpl)

ErrorCode KESCMStoryScriptProvider::AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script)
{
	const int32 id = propID.Get();
	if (id != p_KESCMChangeCount && id != p_KESCMTextChangeCount &&
		id != p_KESCMAttrChangeCount && id != p_KESCMOtherChangeCount)
		return CScriptProvider::AccessProperty(propID, data, script);

	if (data == nil)
		return kFailure;

	// Read-only. The declarations in KESCM.fr say kReadOnly, so the engine should refuse an
	// assignment before it reaches here; this is the backstop, and it fails rather than quietly
	// accepting a value that would then not be there on the next read.
	if (data->IsPropertyPut())
		return kFailure;

	if (!data->IsPropertyGet())
		return CScriptProvider::AccessProperty(propID, data, script);

	// The script object names the story to read. Same move as BPIScriptProvider.cpp:144, which
	// takes the page item whose data is wanted straight off the script object.
	UIDList target(script);
	if (target.Length() < 1)
		return kInvalidScriptTargetError;

	KESCMStoryStamp stamp;
	if (!KESCMStoryEdits::ReadStamp(target.GetRef(0), stamp))
		return kInvalidScriptTargetError;	// handed something that is not a story

	uint32 value = stamp.fChangeCount;
	if (id == p_KESCMTextChangeCount)
		value = stamp.fTextCount;
	else if (id == p_KESCMAttrChangeCount)
		value = stamp.fAttrCount;
	else if (id == p_KESCMOtherChangeCount)
		value = stamp.fOtherCount;

	// Int32Type in the .fr, so the script gets a number it can compare with < and ==, rather than
	// a string it would have to parse. The counters are small in practice (single or double
	// digits after ordinary editing), so the cast cannot lose anything a reader would notice.
	ScriptData outputData;
	outputData.SetInt32(static_cast<int32>(value));
	data->AppendReturnData(script, propID, outputData);
	return kSuccess;
}

// End, KESCMStoryScriptProvider.cpp.
