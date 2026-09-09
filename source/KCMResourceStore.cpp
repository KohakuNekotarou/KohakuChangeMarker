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
#include "IDocument.h"

// General includes:
#include <sstream>
#include <string>

// General includes (continued):
#include <vector>

// Project includes:
#include "KCMCore.h"				// KCMArmedTargetDB / KCMArmedSourceDB
#include "KCMResourceBytes.h"
#include "KCMResourceDiff.h"
#include "KCMResourceAttrDiff.h"	// which ATTRIBUTES of one definition differ
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

/** One document's definitions, or kFalse with the reason. Both sides go through here so that a
    failure says WHICH side failed without the caller writing the sentence twice. */
bool16 ReadOneSide(IDataBase* db, const char* which, KCMResourceList& out, PMString& whyNot)
{
	if (db == nil)
	{
		whyNot = which;
		whyNot.Append(": no database");
		return kFalse;
	}

	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc == nil)
	{
		whyNot = which;
		whyNot.Append(": the database has no document");
		return kFalse;
	}

	KCMResourceBytes xml;
	PMString why;
	if (!KCMTakeResourceSnapshot(doc.get(), xml, why))
	{
		whyNot = which;
		whyNot.Append(": ");
		whyNot.Append(why);
		return kFalse;
	}
	if (!KCMParseResources(xml, out, why))
	{
		whyNot = which;
		whyNot.Append(": ");
		whyNot.Append(why);
		return kFalse;
	}
	return kTrue;
}

std::string Num(int32 n)
{
	std::ostringstream os;
	os << n;
	return os.str();
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

	KCMResourceList sourceItems;
	KCMResourceList targetItems;
	if (!ReadOneSide(sourceDB, "source", sourceItems, whyNot))
	{
		gWhyNot = whyNot;
		return kFalse;
	}
	if (!ReadOneSide(targetDB, "target", targetItems, whyNot))
	{
		gWhyNot = whyNot;
		return kFalse;
	}

	if (!KCMDiffResources(sourceItems, targetItems, gChanges, gStats))
	{
		whyNot = "ran out of memory while pairing the definitions";
		gWhyNot = whyNot;
		gChanges.clear();
		return kFalse;
	}

	gHasResult = kTrue;
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
	KCMDiffAttributes(std::string(gChanges[n].fSourceBody.GetPlatformString().c_str()),
					  std::string(gChanges[n].fTargetBody.GetPlatformString().c_str()),
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

	outName = gAttrCache[i].fName.c_str();
	outSource = gAttrCache[i].fSource.c_str();
	outTarget = gAttrCache[i].fTarget.c_str();
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

	if (!gHasResult)
	{
		// ⚠"nothing held" and "nothing differs" are different answers and must read differently.
		out = gWhyNot.IsEmpty() ? PMString("Resources: not compared yet") : PMString("Resources: ");
		if (!gWhyNot.IsEmpty())
			out.Append(gWhyNot);
		out.SetTranslatable(kFalse);
		return;
	}

	const std::string s = "Resources: " + Num(gStats.fPaired) + " paired, "
						+ Num(gStats.fAdded) + " added, "
						+ Num(gStats.fRemoved) + " removed, "
						+ Num(gStats.fChanged) + " changed";
	out.SetUTF8String(s);
	out.SetTranslatable(kFalse);
}

// End, KCMResourceStore.cpp.
