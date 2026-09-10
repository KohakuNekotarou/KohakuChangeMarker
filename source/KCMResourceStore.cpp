//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  See KCMResourceStore.h for why the answer is kept rather than recomputed.
//
//  ★THE LIST IS A FILE STATIC, the same shape KCMStoryList uses, and for the same reasons: one
//  comparison builds it, the panel reads it through a facade, and Stop empties it. A boss would
//  buy nothing here - there is one comparison at a time, and it belongs to the session rather
//  than to either document.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
// (IDocument.h went with ReadOneSide on 2026-09-10. Nothing left in this file reaches a document:
//  it hands two databases to KCMReadResourceList and reads what comes back.)

// General includes:
#include <sstream>
#include <string>

// General includes (continued):
#include <vector>

// Project includes:
#include "KCMCore.h"				// KCMArmedTargetDB / KCMArmedSourceDB
// (KCMResourceBytes.h went with ReadOneSide too - the bytes of an export never reach this file now.)
#include "KCMResourceDiff.h"
#include "KCMResourceAttrDiff.h"	// which ATTRIBUTES of one definition differ
#include "KCMBoundaryID.h"			// kKCMStoryEditsRebuiltMessage - the one the panel's list listens for
#include "KCMModelNotify.h"			// KCMNotify - the panel is told whenever this store is refilled
#include "KCMProgressBar.h"			// KCMDeferredProgressBar - here with no delay at all (see Rebuild)
#include "KCMResourceSnapshot.h"
#include "KCMResourceStore.h"

/** The held result. See the header: main thread only. */
static KCMResourceChangeList gChanges;
static KCMResourceDiffStats gStats;
static bool16 gHasResult = kFalse;

/** The attribute diff of ONE row, cached.

    ★ONE ROW, NOT A MAP. The panel asks about the row a person selected, and it asks in a burst -
    first how many attributes differ, then each of them - so a single-entry cache turns that burst
    into one parse. A map would hold every row's parse for a list nobody is looking at.
    ⚠It is dropped by Clear(), which Rebuild() calls first, so it can never describe a comparison
      that is no longer held. */
static int32 gAttrRow = -1;
static std::vector<KCMAttrChange> gAttrCache;

/** Why the last rebuild failed, so the summary can say so rather than looking like "no changes".
    ⚠★AN EMPTY LIST AND A FAILED COMPARISON MUST NOT READ ALIKE - that is the whole reason this
    is kept beside the list. */
static PMString gWhyNot;

namespace
{

/* (ReadOneSide stood here until 2026-09-10. The book comparison needed the same two steps -- take
   a snapshot, parse it, and name the side in the reason -- so it moved next to the snapshot it
   starts with, as KCMReadResourceList in KCMResourceSnapshot.h. Two copies of the wording is the
   half that would have drifted, because the wording is the part a reader sees.) */

std::string Num(int32 n)
{
	std::ostringstream os;
	os << n;
	return os.str();
}

/** A finished sentence for the progress bar, not a key.

	⚠**PMString HAS NO kNoTranslate** - the flag is set on the object, not chosen at construction
	(tried and rejected by the compiler, 2026-09-09). Left translatable, a line like "Done." is
	handed to the built-in table on its way to the screen, which is how KCM once showed
	"Source:" as a style-source phrase in a Japanese locale. */
PMString Phase(const char* text)
{
	PMString s(text);
	s.SetTranslatable(kFalse);
	return s;
}

}	// anonymous namespace

bool16 KCMResourceStore::Rebuild(IDataBase* targetDB, IDataBase* sourceDB, PMString& whyNot)
{
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	// ★DROPPED FIRST, unconditionally. A rebuild that fails must not leave the previous answer
	//   standing: it would be about two documents, one of which may no longer be there, and it
	//   would look exactly like a fresh result.
	KCMResourceStore::Clear();

	// ⚠**THE TWO DOCUMENTS ARE PASSED IN, NOT ASKED FOR** (2026-09-09). This used to call
	//   KCMArmedTargetDB()/KCMArmedSourceDB() itself, and the comparison run then could not use it:
	//   KCMDoMarkChangesDoc does its work BEFORE the pair is armed, so a rebuild from there answered
	//   "no comparison is armed" while holding both databases in its own parameters. The neighbour
	//   that had the same job, KCMRebuildStoryEdits, already took them as arguments.
	//   ⇒ Callers that mean "the armed pair" say so at the call site.
	if (targetDB == nil || sourceDB == nil)
	{
		whyNot = "no comparison is armed";
		gWhyNot = whyNot;
		return kFalse;
	}

	// ***** A BAR ON Start AND Refresh (2026-09-09, the user's request). *****
	//
	// ★★**IT WAITS THE SAME THREE SECONDS THE OTHER TWO MODES DO** - the user's call, the same day
	//   and in two steps: "show a bar, straight away for now", then, having seen it, "make it come
	//   up after three seconds like the others". ⇒ **One rule for all three modes.** Most
	//   comparisons are over before the delay (measured 200-2400ms for the two exports; 328-657ms
	//   on small documents), and a bar that flashes up for those is noise.
	//   ⚠The delay parameter stays on the class because the class needed one anyway to be told a
	//     number rather than to read a constant - but every caller passes the same value now, and a
	//     second value would need the same kind of reason this one lost.
	// ★THREE UNITS, and they are the three phases a caller can actually see: read the older
	//   document, read the newer one, pair them. Nothing inside a phase reports, so the bar moves
	//   in three steps rather than pretending to a smoothness it has not got.
	// ⚠**Cancel is polled BETWEEN phases only.** WasCancelled pumps events, and a phase is a single
	//   call into the SDK with no safe point inside it (KCMProgressBar.h says so).
	// ⚠No other bar may be alive here - KCMCore.cpp scopes the raster loop's to its loop, and this
	//   runs after it (the same warning the class carries).
	PMString barTitle("Kohaku Change Marker");
	barTitle.SetTranslatable(kFalse);
	KCMDeferredProgressBar progress(barTitle, 3);	// the default delay: three seconds, as Pixel and Story

	KCMResourceList sourceItems;
	KCMResourceList targetItems;

	progress.Step(0, Phase("Reading the older document's definitions..."));
	if (!KCMReadResourceList(sourceDB, "source", sourceItems, whyNot))
	{
		gWhyNot = whyNot;
		return kFalse;
	}

	if (progress.WasCancelled())
	{
		whyNot = "cancelled";
		gWhyNot = whyNot;
		return kFalse;
	}

	progress.Step(1, Phase("Reading the newer document's definitions..."));
	if (!KCMReadResourceList(targetDB, "target", targetItems, whyNot))
	{
		gWhyNot = whyNot;
		return kFalse;
	}

	if (progress.WasCancelled())
	{
		whyNot = "cancelled";
		gWhyNot = whyNot;
		return kFalse;
	}

	progress.Step(2, Phase("Pairing the definitions..."));
	if (!KCMDiffResources(sourceItems, targetItems, gChanges, gStats))
	{
		whyNot = "ran out of memory while pairing the definitions";
		gWhyNot = whyNot;
		gChanges.clear();
		return kFalse;
	}

	progress.Step(3, Phase("Done."));

	gHasResult = kTrue;

	// ***** ★★★WHOEVER FILLS THE STORE, THE PANEL IS TOLD. *****
	//
	// ⚠**WRITTEN AFTER A DEFECT THE USER SAW** (2026-09-10): "リソースが０とでてるのに、結果の
	//  ところに表示がある" - the section heading read `Resources (0)` while the list underneath it
	//  showed rows, and a recomputation at that same moment answered 9. THREE STATES, all
	//  disagreeing.
	//
	// The cause was that this function has three callers and only one of them was followed by a
	// notification. The heading takes its number from GetChangeCount() and the rows are built from
	// the same store, so both were correct about a store that had since been rewritten underneath
	// them - by app.kcmResourceDiff, which rebuilds here and used to say nothing.
	//   ⇒ ★THE NOTIFICATION BELONGS HERE, NOT AT THE CALLERS. Three callers meant three chances to
	//     forget, and one of them had (memory one-question-one-place). Anything that changes what
	//     the panel would show now says so from the one place that changed it.
	//
	// ★★AND IT IS WHAT MAKES app.kcmResourceDiff EVIDENCE AGAIN. The port's own comment says it
	//  reads back through the store "exactly as the panel will", so that its output is evidence
	//  about what the panel can see - and that claim only holds while the two are looking at the
	//  same moment. Without this line the port moved on and the panel did not.
	//
	// ⚠THE COST IS THE READER'S SELECTION: the UI rebuilds the whole tree on this message
	//  (KCMFacades' RefreshRow says so, and declines to send it when nothing changed). It is paid
	//  here on purpose - a list that disagrees with its own heading is worse than a lost selection,
	//  and unlike RefreshRow this function cannot tell "nothing changed" from "not compared yet"
	//  without comparing first, which is the work it has just done.
	KCMNotify(kKCMStoryEditsRebuiltMessage);

	return kTrue;
}

void KCMResourceStore::Clear()
{
	gChanges.clear();
	gStats = KCMResourceDiffStats();
	gHasResult = kFalse;
	gWhyNot.Clear();
	gWhyNot.SetTranslatable(kFalse);

	// ⚠The cached attribute diff belongs to a ROW OF gChanges, so it goes with them. Left behind,
	//   the next comparison's row 0 would be answered with the previous one's attributes - and it
	//   would look entirely plausible.
	gAttrRow = -1;
	gAttrCache.clear();
}

bool16 KCMResourceStore::HasResult()
{
	return gHasResult;
}

int32 KCMResourceStore::GetChangeCount()
{
	return static_cast<int32>(gChanges.size());
}

bool16 KCMResourceStore::GetNthChange(int32 n, PMString& outKind, PMString& outKey,
									  KCMResourceChangeKind& outWhat)
{
	if (n < 0 || n >= static_cast<int32>(gChanges.size()))
		return kFalse;

	outKind = gChanges[n].fKind;
	outKey = gChanges[n].fKey;
	outWhat = gChanges[n].fWhat;
	outKind.SetTranslatable(kFalse);
	outKey.SetTranslatable(kFalse);
	return kTrue;
}

bool16 KCMResourceStore::GetNthValues(int32 n, PMString& outSourceBody, PMString& outTargetBody)
{
	if (n < 0 || n >= static_cast<int32>(gChanges.size()))
		return kFalse;

	outSourceBody = gChanges[n].fSourceBody;
	outTargetBody = gChanges[n].fTargetBody;
	outSourceBody.SetTranslatable(kFalse);
	outTargetBody.SetTranslatable(kFalse);
	return kTrue;
}

/** Make sure gAttrCache holds row n's attributes. @return kFalse when n is out of range. */
static bool16 KCMEnsureAttrCache(int32 n)
{
	if (n < 0 || n >= static_cast<int32>(gChanges.size()))
		return kFalse;

	if (gAttrRow == n)
		return kTrue;

	// ⚠**The bodies are std::string on the way in.** The differ takes no SDK types on purpose
	//   (KCMResourceAttrDiff.h), so the conversion happens here, at the boundary, once per row
	//   rather than once per attribute.
	//
	// ⚠★★★**UTF-8, NOT GetPlatformString** (2026-09-09, the user: "when a font is applied to a
	//   style, the font part of the result comes out garbled - the Japanese"). A font is written
	//   as `<AppliedFont type="string">小塚明朝 Pr6N</AppliedFont>`, so the value travels through
	//   here as text. GetPlatformString hands over whatever the SYSTEM CODE PAGE can hold, and the
	//   PMString built back from a `const char*` on the way out reads it by its own rule - two
	//   ends, two assumptions, and a name in between that survives only if they happen to agree.
	//   ★GetUTF8String / SetUTF8String names the encoding at BOTH ends, so nothing has to agree by
	//     luck, and nothing is dropped for having no place in the code page.
	//   ★The differ is unaffected: every character it looks for (`<`, `>`, `=`, `"`, `/`) is
	//     ASCII, and no ASCII byte can occur inside a multi-byte UTF-8 sequence.
	KCMDiffAttributes(gChanges[n].fSourceBody.GetUTF8String(),
					  gChanges[n].fTargetBody.GetUTF8String(),
					  gAttrCache);
	gAttrRow = n;
	return kTrue;
}

int32 KCMResourceStore::GetNthAttrCount(int32 n)
{
	if (!KCMEnsureAttrCache(n))
		return 0;

	return static_cast<int32>(gAttrCache.size());
}

bool16 KCMResourceStore::GetNthAttr(int32 n, int32 i, PMString& outName,
									PMString& outSource, PMString& outTarget)
{
	if (!KCMEnsureAttrCache(n))
		return kFalse;
	if (i < 0 || i >= static_cast<int32>(gAttrCache.size()))
		return kFalse;

	// ⚠**SetUTF8String, NOT `= c_str()`.** The cache holds UTF-8 (see KCMEnsureAttrCache), and
	//   assigning a `const char*` to a PMString reads it as a PLATFORM string - which is how a
	//   Japanese font name came out garbled. Naming the encoding at both ends is the whole fix.
	//   ★SetUTF8String marks the string untranslatable itself (PMString.h:209), which is what the
	//     three calls below used to do; they are kept because that promise is worth stating where
	//     it matters rather than relying on a side effect of the setter.
	outName.SetUTF8String(gAttrCache[i].fName);
	outSource.SetUTF8String(gAttrCache[i].fSource);
	outTarget.SetUTF8String(gAttrCache[i].fTarget);
	outName.SetTranslatable(kFalse);
	outSource.SetTranslatable(kFalse);
	outTarget.SetTranslatable(kFalse);
	return kTrue;
}

void KCMResourceStore::GetStats(KCMResourceDiffStats& out)
{
	out = gStats;
}

void KCMResourceStore::GetSummary(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	// ★**THE MODE'S NAME IS NOT REPEATED HERE** (2026-09-10, the user's call: "the Resources part
	//   is not needed - the panel's own name says which mode it is in"). The panel tab carries the
	//   mode, and this line is read while looking at it. ⚠**The BOOK comparison's reasons keep
	//   their prefix** (KCMBookChapterModes.cpp): there, three modes report into one cell and the
	//   name is the only thing saying which of them spoke.
	if (!gHasResult)
	{
		// ⚠"nothing held" and "nothing differs" are different answers and must read differently.
		out = gWhyNot.IsEmpty() ? PMString("Not compared yet") : PMString();
		if (!gWhyNot.IsEmpty())
			out.Append(gWhyNot);
		out.SetTranslatable(kFalse);
		return;
	}

	const std::string s = Num(gStats.fPaired) + " paired, "
						+ Num(gStats.fAdded) + " added, "
						+ Num(gStats.fRemoved) + " removed, "
						+ Num(gStats.fChanged) + " changed";
	out.SetUTF8String(s);
	out.SetTranslatable(kFalse);
}

// End, KCMResourceStore.cpp.
