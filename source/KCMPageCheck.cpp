//========================================================================================
//
//  KCMPageCheck.cpp
//
//  The "Check" feature (see KCMPageCheck.h). Select pages in the Pages panel, then the
//  context-menu toggle "Check" puts a "seen" mark on them and takes it off again. A checked page
//  gets a blue tick, drawn as vector strokes, in the middle of its Pages panel thumbnail (by
//  KCMDrawEventHandler's isThumb branch). The set is independent of the registrations
//  (KCMPageMap), lives for the session only, and Stop clears it.
//
//  The structure follows KCMPageMap.cpp: the same shared reader for the selection
//  (KCMPageMapReadSelection), the same per-document UID set, and a close sweep that compares
//  pointers without ever dereferencing one.
//
//  **Master pages count too.** The only difference from Register is that the shared reader is
//  called with includeMasters=kTrue here (Register passes kFalse): master spreads are compared
//  and do get frames, so "mark the page with a frame as seen" means exactly what it does
//  elsewhere. While the reader returned no master, the toggle state (now
//  KCMPageCheckGetToggleState) always answered "disabled", and since a context menu does not show
//  disabled items, it looked as though Check had vanished on masters alone.
//  The drawing side worked on masters from the start: both the thumbnail tick and the layout tick
//  simply walk the pages of the spread being drawn, the purge is per page UID, and
//  KCMForceRedrawPagesPanelNow redraws the Master sub-panel as well.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"			// GetSysFile (the save key is the document's file path)
#include "PMString.h"
#include "FileUtils.h"			// GetAppRoamingDataFolder / AppendPath / OpenFile / DoesFileExist / SysFileToPMString
#include "IDFile.h"

#include <map>
#include <set>
#include <vector>
#include <string>
#include <cstdio>				// FILE / fread / fwrite / fclose

#include "KCMCore.h"			// KCMCollectPageUIDs / KCMCollectMasterPageUIDs / KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB / KCMDoMarkChangesDoc
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it
#include "KCMComparisonRun.h"	// KCMToggleStartStop
#include "KCMPageCheck.h"
#include "KCMPageMap.h"		// KCMPageMapCollectRegistered (save) / KCMPageMapReplaceRegistered (load)
#include "KCMDocUidSet.h"		// the shared "document -> page UID set" container (Register uses it too)
#include "KCMThreadSafety.h"	// the shared-state lock, for reaching inside the container through GetMap
#include "KCMID.h"				// kKCMPageFlagsChangedMessage (the notification's ID)
// This file deliberately does not include the UI's KCMThumbnailRefresh.h: rebuilding a thumbnail
// is the job of whoever receives the notification, which is the UI.

// The ticked pages: document database -> set of page UIDs, session only.
// An entry whose set became empty disappears at once (KCMDocUidSet's rule).
static KCMDocUidSet sChecked;

// **Which pages may be ticked depends on the mode.** The answer is built in one place,
//   KCMCollectCheckablePageUIDs in KCMCore.cpp, and this file only asks it (the reasoning is with
//   the declaration in KCMCore.h). In short:
//     - Pixel mode ... only pages carrying a mark (a frame, a registered "/", an overflow "/")
//     - Story mode ... **every page** of the Target and the Source
//   @warning **four routes ask this question** -- the toggle, the toggle's state, the prune, and
//     Load's restore -- and every one of them goes through KCMCollectCheckable /
//     KCMFilterToCheckable below. Calling KCMCollectChangedPageUIDs directly instead only ever
//     gives the Pixel answer ([[one-question-one-place]]).
//
// **Ask once and then use Includes() when judging several pages.** The Pixel branch walks the
//   whole of sEntries each time, so asking per page costs O(pages x changes).
static bool16 KCMCollectCheckable(IDataBase* db, KCMCheckablePages& outCheckable)
{
	return KCMCollectCheckablePageUIDs(db, outCheckable);
}

// Keep only the selected pages that may be ticked. The candidate set is built once (see above).
static void KCMFilterToCheckable(IDataBase* db, const std::vector<UID>& pages, std::vector<UID>& out)
{
	out.clear();
	KCMCheckablePages checkable;
	if (!KCMCollectCheckable(db, checkable))
		return;		// db is not one of the compared documents = nothing may be ticked
	for (size_t i = 0; i < pages.size(); ++i)
		if (checkable.Includes(pages[i]))
			out.push_back(pages[i]);
}

//========================================================================================
// KCMPageCheckToggleSelectedPages (declared in KCMPageCheck.h)
//========================================================================================
void KCMPageCheckToggleSelectedPages()
{
	IDataBase* db = nil;
	std::vector<UID> selPages;
	if (!KCMPageMapReadSelection(db, selPages, kTrue /*includeMasters*/))
		return;		// kCustomEnabling should already have greyed the menu out; belt and braces

	// Ticking is only possible while a comparison is running (armed) and the selected document is
	// the Target or the Source.
	if (!KCMIsComparedDoc(db))
		return;

	// Narrow the selection to the pages that may be ticked (which depends on the mode -- see
	// KCMFilterToCheckable above).
	std::vector<UID> pages;
	KCMFilterToCheckable(db, selPages, pages);
	if (pages.empty())
		return;		// nothing eligible in the selection; the menu should be disabled anyway

	const bool16 anyUnchecked = sChecked.ToggleAll(db, pages);

	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(anyUnchecked ? "check +" : "check -");
	msg.AppendNumber((int32)pages.size());

	// The total is counted after the change (Erase has already dropped the document's entry when
	// unticking emptied it, so 0 comes back).
	msg.Append(", total ");
	msg.AppendNumber(sChecked.CountIn(db));

	// Refresh the toggled pages' thumbnails so the tick shows at once. No re-comparison is needed;
	// a tick changes nothing about the comparison itself.
	// The page set travels on the notification (ISubject::Change's changedBy parameter, see
	//   KCMModelNotify.h), so the UI purges per UID. Ticking and unticking change the picture of
	//   the touched pages and of nothing else, so this set cannot miss one.
	{
		const std::set<UID> touched(pages.begin(), pages.end());
		KCMNotifyPages(kKCMPageFlagsChangedMessage, db, touched);
	}

	// The layout view's tick has to be refreshed as well, and by a different route. It is drawn
	// whenever marks are visible, so without invalidating the toggled document's layout views the
	// change does not reach the screen until something else redraws them -- which showed up as
	// "the tick is still there after I switched it off", until the user scrolled.
	KCMInvalidateDB(db);

	KCMNotifyStatus(msg);
}

//========================================================================================
// KCMPageCheckGetToggleState (declared in KCMPageCheck.h)
//   Like the Register side, this **only answers** and no longer takes an IActionStateList (the
//   reasoning is with KCMPageToggleState in KCMPageMap.h).
//========================================================================================
KCMPageToggleState KCMPageCheckGetToggleState()
{
	KCMPageToggleState st;	// disabled by default

	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KCMPageMapReadSelection(db, pages, kTrue /*includeMasters*/))
		return st;

	// Enabled only while a comparison is running and the selected document is the Target or the
	// Source; grey otherwise.
	if (!KCMIsComparedDoc(db))
		return st;

	// Disabled when the selection holds no page that may be ticked. In the Pixel mode Check does
	//   not appear on pages without a frame or a "/", while in the Story mode every page counts;
	//   that difference lives inside KCMFilterToCheckable and nowhere else.
	std::vector<UID> eligible;
	KCMFilterToCheckable(db, pages, eligible);
	if (eligible.empty())
		return st;

	const int32 chkCount = sChecked.CountIn(db, eligible);

	st.fEnabled = kTrue;
	if (chkCount == (int32)eligible.size())
		st.fTick = kKCMPageTickAll;		// every eligible selected page is ticked
	else if (chkCount > 0)
		st.fTick = kKCMPageTickSome;		// only some of them = the mixed tick

	// fRole is deliberately left alone: Check's menu name is fixed, so there is nothing to pick.
	return st;
}

//========================================================================================
// KCMPageCheckSweepClosedDocs (declared in KCMPageCheck.h)
//========================================================================================
void KCMPageCheckSweepClosedDocs()
{
	sChecked.SweepClosedDocs();	// the container owns both the shutdown nil guards and the
								// no-dereference rule (KCMDocUidSet.cpp)
}

//========================================================================================
// KCMPageCheckClearAllDocs (declared in KCMPageCheck.h)
//========================================================================================
void KCMPageCheckClearAllDocs()
{
	sChecked.ClearAllDocs();
}

//========================================================================================
// KCMPageCheckPruneToMarked (declared in KCMPageCheck.h)
//   After a re-comparison, narrow each document's ticks to the pages that may still be ticked,
//   forgetting the rest. KCMCollectCheckablePageUIDs only answers for the two documents being
//   compared; for any other one it comes back empty, which unticks that document entirely.
//   No pointer is dereferenced.
//   @warning **the name still says "ToMarked"** while the meaning has widened to "keep only what
//     may still be ticked" -- see the header. Renaming belongs in a commit of its own.
//========================================================================================
void KCMPageCheckPruneToMarked(std::map<IDataBase*, std::set<UID> >* outUnchecked)
{
	if (sChecked.IsEmpty())
		return;
	// The eligibility set is built once per document and then filtered against, which needs the
	//   entry point that hands out the sets themselves (GetMap). The entries emptied by that are
	//   dropped by PruneEmptyDocs() at the end (KCMDocUidSet.h's rule).
	// **GetMap() hands out the raw map, so the lock the container's own methods take does not
	//   apply**; it is taken explicitly here. Erasing while a background thread's drawing pass
	//   reads the same set corrupts it. The lock is recursive, so PruneEmptyDocs() taking it
	//   again below is fine.
	KCMMarkStateLock lock(KCMMarkStateMutex());
	KCMDocUidSet::Map& m = sChecked.GetMap();
	for (KCMDocUidSet::Map::iterator it = m.begin(); it != m.end(); ++it)
	{
		// **What is asked here is "may this page still be ticked", not "does it still carry a
		//   mark".** The two look alike only in the Pixel mode: in the Story mode every page is
		//   eligible, so **nothing comes off**, which is correct. Asking
		//   KCMCollectChangedPageUIDs here instead would wipe out every tick made in the Story
		//   mode at the next re-comparison -- the side that puts ticks on and the side that takes
		//   them off would be asking two different questions.
		KCMCheckablePages checkable;
		KCMCollectCheckable(it->first, checkable);		// empty unless db is compared = all come off
		std::set<UID>& chk = it->second;
		for (std::set<UID>::iterator c = chk.begin(); c != chk.end(); )
		{
			if (!checkable.Includes(*c))
			{
				// Tell the caller which page was unticked, when it asked to be told. Losing a
				//   tick changes the thumbnail, but the page is in no set once the tick is gone,
				//   so **not catching it here means it never gets purged at all**
				//   (see KCMPageCheck.h).
				if (outUnchecked != nil)
					(*outUnchecked)[it->first].insert(*c);
				chk.erase(c++);		// no longer eligible: forget the tick
			}
			else
				++c;
		}
	}
	sChecked.PruneEmptyDocs();
}

//========================================================================================
// KCMPageCheckIsChecked (declared in KCMPageCheck.h)
//========================================================================================
bool16 KCMPageCheckIsChecked(IDataBase* db, UID pageUID)
{
	return sChecked.Contains(db, pageUID);
}

//========================================================================================
// KCMPageCheckHasAny (declared in KCMPageCheck.h)
//========================================================================================
bool16 KCMPageCheckHasAny(IDataBase* db)
{
	return sChecked.HasAny(db);
}

//========================================================================================
// Saving and loading the ticks and registrations (the flyout items "Save Check & Register" and
// "Load Check & Register")
//
//   The file, KCMPageChecks.json, is KCM's own JSON and sits directly in the roaming preferences
//   folder, the same one the panel settings use (KCMPanelState.cpp); no sub-folder is created.
//   Nothing is ever written into InDesign's own data.
//   The key is the document's full file path (GetSysFile -> SysFileToPMString). **Not the file
//     name alone**: a Target and a Source of the same name in different folders -- which is what
//     an older and a newer version of one document usually are -- would collide on it. Page UIDs
//     are persistent inside a saved document, so they still match after it is reopened. The
//     trade-off is that moving the document, or saving it under a new name, breaks the match.
//   The value is two sets: the ticked page UIDs and the registered (Added/Removed = green "/")
//   page UIDs. Paths are stored as UTF-8, which keeps Shift-JIS's 0x5C out of Japanese paths.
//   "checks" **can hold master page UIDs**; "registered" holds ordinary pages only, since a
//     master is never registered. UIDs are unique within a document, so this changes neither the
//     file format nor compatibility with older files -- the loading side always checks that a
//     page really exists in the document it is restoring into.
//
//   The format (version 2):
//     {
//       "version": 2,
//       "docs": [
//         { "path": "<utf8 path>", "checks": [12, 45], "registered": [3, 7] }
//       ]
//     }
//   Reading is lenient: the version is not looked at, and an old v1 file's "pages" array is
//     accepted as checks (an old file without "registered" simply has no registrations).
//
//  --------------------------------------------------------------------------------------
//  **WHY THIS READS AND WRITES ITS OWN JSON INSTEAD OF USING THE SDK'S CLASS.**
//    (Settled; do not re-open the question without reading this.)
//
//    The official one exists: `class PUBLIC_DECL JSON` in
//    `public/interfaces/utils/IJsonUtils.h`, a wrapper around boost property_tree. So do users of
//    it in the product -- `publiclib/links/HTTPAssetLinkResourceStateUpdater.cpp` (addValue ->
//    write_json), `open/components/linksui/aem/ChromiumImportHelperAEMLinks.cpp` (read_json in a
//    try/catch -> GetListAt -> checkKey/GetString) -- and in the sample
//    `CustomHttpLink/CusHttpLnkResourceServerAPIWrapper.cpp`.
//    **It was measured to work here**: dropping `#include "IJsonUtils.h"` and a few
//      `JSON j; j.addValue(...); j.write_json(s); j.GetListAt(...)` calls into one file of this
//      plug-in **compiled and linked with no build changes at all** (the include path comes from
//      $(BOOST_HEADER_SEARCH_PATH) in `build/win/prj/Base.props`, which every configuration
//      inherits). **It is not that the class cannot be used.**
//
//    There are four reasons not to, and **none of them is "the dependency is heavy"** -- as
//    above, the dependency costs nothing:
//      1. **Nothing official writes with it.** What is written here is
//         `docs:[{path, checks:[numbers], registered:[numbers]}]` -- an array of objects holding
//         arrays of numbers. Every official use is `addValue(key, string)` into a flat object,
//         and neither the product nor the samples write an array at all (`AddValue(key,
//         JSONArray)` and `PushValue` exist in the API with no caller anywhere). A stock type
//         with no worked example is not a road.
//      2. Moving only the reading over would **split the knowledge of this format between an
//         official parser and a hand-written writer**.
//      3. **The lenient parsing would be lost.** boost's read_json throws on the whole document
//         when anything in it is broken. The code below skips one broken doc entry and keeps the
//         rest, which is deliberate: giving up on everything means the next Save merges from
//         nothing and **silently deletes what was saved for every other document**.
//      4. It would go through std::stringstream, putting one more layer between this code and the
//         UTF-8 it currently handles explicitly.
//
//  **WHY stdio (FileUtils::OpenFile) AND NOT IPMStream.** (Settled on the KBS side; the full
//    reasoning is under "WHY stdio AND NOT IPMStream" at the top of KBS's KBSPanelState.cpp.)
//    In short: the SDK's usual road is `StreamUtil::CreateFileStreamRead/Write`, but
//    **`IPMStream::Close()` and `Flush()` both return void** (`IPMStream.h:321, 368`), so **a
//    write that fails while being flushed -- a full disk -- has no documented way of being
//    noticed**. `fclose` reports it. Moving to the mainstream API would quietly weaken the check
//    below that keeps this from saying "saved" when nothing was.
//========================================================================================

// What is saved for one document: the ticked and the registered (Added/Removed) pages, as raw
// UID values (uint32).
struct KCMDocSets
{
	std::set<uint32> checks;
	std::set<uint32> registered;
};

static const char* const kKCMPageChecksFileName = "KCMPageChecks.json";

// The IDFile of KCMPageChecks.json, directly in the (locale-specific) roaming preferences folder.
// **No sub-folder is created.** Passing a FILE name as GetAppRoamingDataFolder's subFolderName
//   returns the IDFile of a file in that folder, which is what the SDK's own uses do
//   (SnpShareAppResources.cpp, SuppUISysFileData.cpp). InDesign has already created the parent
//   folder for its preferences, so no CreateFolderIfNeeded is needed. kFalse when it cannot be
//   worked out.
static bool16 KCMPageChecksFile(IDFile& outFile)
{
	return FileUtils::GetAppRoamingDataFolder(&outFile, PMString(kKCMPageChecksFileName));
}

// Escape a UTF-8 string for a JSON string literal (backslash, quote, control characters). A UTF-8
// continuation byte is never 0x5C or 0x22, so walking it byte by byte is safe.
static void KCMJsonEscape(const std::string& in, std::string& out)
{
	out.clear();
	out.reserve(in.size() + 8);
	for (size_t i = 0; i < in.size(); ++i)
	{
		const char c = in[i];
		switch (c)
		{
			case '\\': out += "\\\\"; break;
			case '\"': out += "\\\""; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:   out += c;      break;
		}
	}
}

// With text[pos] on an opening quote, unescape up to the closing quote into out and leave pos
// just past it. kFalse when it does not start on a quote.
static bool16 KCMJsonReadString(const std::string& text, size_t& pos, std::string& out)
{
	out.clear();
	if (pos >= text.size() || text[pos] != '\"')
		return kFalse;
	++pos;	// step over the opening quote
	while (pos < text.size())
	{
		const char c = text[pos++];
		if (c == '\"')
			return kTrue;	// the closing quote
		if (c == '\\' && pos < text.size())
		{
			const char e = text[pos++];
			switch (e)
			{
				case 'n':  out += '\n'; break;
				case 'r':  out += '\r'; break;
				case 't':  out += '\t'; break;
				case '\\': out += '\\'; break;
				case '\"': out += '\"'; break;
				case '/':  out += '/';  break;
				default:   out += e;    break;	// an escape we do not know is kept as it is
			}
		}
		else
		{
			out += c;
		}
	}
	return kFalse;	// no closing quote = broken
}

// Read the whole file into a std::string. Missing or unopenable gives kFalse and an empty string.
static bool16 KCMReadWholeFile(const IDFile& file, std::string& outText)
{
	outText.clear();
	if (!FileUtils::DoesFileExist(file))
		return kFalse;
	FILE* fp = FileUtils::OpenFile(file, "rb");
	if (fp == nil)
		return kFalse;
	char buf[2048];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
		outText.append(buf, n);
	// A read error partway through counts as failure: a partial read used as the merge source of
	//   the next Save would silently overwrite -- and so delete -- everything it failed to read.
	const bool16 ok = ferror(fp) ? kFalse : kTrue;
	fclose(fp);
	if (!ok)
		outText.clear();
	return ok;
}

// Find key (say "\"checks\"") from pos on, but only where it is **a JSON key**. npos when there
// is none.
// A plain find would also match inside a value -- a document path containing an escaped
//   \"checks\" has the bytes "checks" in it verbatim. In JSON the last non-space character before
//   a key is always '{' or ',', so that is checked and anything else discarded.
//   @warning Windows will not put a quote in a file name, so this cannot bite there today. macOS
//   will.
static size_t KCMFindJsonKey(const std::string& text, size_t pos, const std::string& key)
{
	while (pos < text.size())
	{
		const size_t hit = text.find(key, pos);
		if (hit == std::string::npos)
			return std::string::npos;
		size_t b = hit;
		while (b > 0 && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\n' || text[b - 1] == '\r'))
			--b;
		if (b > 0 && (text[b - 1] == '{' || text[b - 1] == ','))
			return hit;		// preceded by '{' or ',' = a real key
		pos = hit + 1;		// a match inside a value; keep looking
	}
	return std::string::npos;
}

// Within [regionBegin, regionEnd), find the [ ... ] that follows key (say "\"checks\"") and
// collect the unsigned integers inside it into out. kTrue when the key was found in the region,
// even if the array was empty; kFalse when it was not. A '[' or ']' past regionEnd is rejected so
// that the search cannot cross into the next document's entry.
static bool16 KCMParseUintArray(const std::string& text, size_t regionBegin, size_t regionEnd,
	const char* key, std::set<uint32>& out)
{
	out.clear();
	const std::string k(key);
	const size_t kk = KCMFindJsonKey(text, regionBegin, k);
	if (kk == std::string::npos || kk >= regionEnd)
		return kFalse;
	const size_t lb = text.find('[', kk + k.size());
	if (lb == std::string::npos || lb >= regionEnd)
		return kFalse;
	const size_t rb = text.find(']', lb + 1);
	if (rb == std::string::npos || rb > regionEnd)
		return kFalse;

	size_t r = lb + 1;
	while (r < rb)
	{
		while (r < rb && (text[r] < '0' || text[r] > '9'))
			++r;
		if (r >= rb)
			break;
		uint32 val = 0;
		bool16 any = kFalse;
		while (r < rb && text[r] >= '0' && text[r] <= '9')
		{
			val = val * 10u + (uint32)(text[r] - '0');
			any = kTrue;
			++r;
		}
		if (any)
			out.insert(val);
	}
	return kTrue;	// the key was there, empty array or not
}

// Read KCMPageChecks.json into path (UTF-8) -> (checks / registered). A missing file gives kFalse
// and an empty map.
// The parsing is lenient: each "path" is found in turn, and "registered" and "checks" are read
// inside that document's region -- from this "path" to the next one. An old v1 file with no
// "checks" has its "pages" accepted as checks.
static bool16 KCMReadSetsMap(std::map<std::string, KCMDocSets>& out, bool16* outReadError = nil)
{
	out.clear();
	if (outReadError)
		*outReadError = kFalse;

	IDFile file;
	if (!KCMPageChecksFile(file))
		return kFalse;
	if (!FileUtils::DoesFileExist(file))
		return kFalse;	// no file = nothing has been saved, which is normal and not an error
	std::string text;
	if (!KCMReadWholeFile(file, text))
	{
		// The file is there but cannot be read (open or fread failed). Merging from that into a
		//   Save would wipe out everything already saved, so it is reported separately and Save
		//   gives up.
		if (outReadError)
			*outReadError = kTrue;
		return kFalse;
	}
	if (text.empty())
		return kFalse;	// an empty file holds nothing to preserve, so overwriting loses nothing

	const std::string kPathKey = "\"path\"";
	size_t p = 0;
	while (true)
	{
		const size_t kpath = KCMFindJsonKey(text, p, kPathKey);
		if (kpath == std::string::npos)
			break;

		// This document's region ends at the next "path", or at the end of the text. The array
		// search is kept inside it.
		const size_t next = KCMFindJsonKey(text, kpath + kPathKey.size(), kPathKey);
		const size_t regionEnd = (next == std::string::npos) ? text.size() : next;

		// After "path" comes ':', then the opening quote, then the string itself.
		// **A failure here skips this document rather than breaking out of the loop**: giving up
		//   on the remaining documents means the next Save merges from what was read so far and
		//   deletes everything after the one broken entry.
		size_t q = text.find(':', kpath + kPathKey.size());
		if (q == std::string::npos || q >= regionEnd)
		{
			p = regionEnd;
			continue;
		}
		++q;
		while (q < text.size() && (text[q] == ' ' || text[q] == '\t' || text[q] == '\n' || text[q] == '\r'))
			++q;
		std::string pathStr;
		if (!KCMJsonReadString(text, q, pathStr))
		{
			p = regionEnd;
			continue;
		}

		KCMDocSets sets;
		KCMParseUintArray(text, q, regionEnd, "\"registered\"", sets.registered);
		// v2's "checks"; failing that, an old v1 file's "pages" is taken as checks.
		if (!KCMParseUintArray(text, q, regionEnd, "\"checks\"", sets.checks))
			KCMParseUintArray(text, q, regionEnd, "\"pages\"", sets.checks);

		if (!sets.checks.empty() || !sets.registered.empty())
			out[pathStr] = sets;

		p = regionEnd;	// on to the next document
	}

	// Text, but not one entry read and not even the structural marker ("docs") in it = a broken
	//   file. Merging from that would delete everything, so it counts as a read error. A proper
	//   file holding an empty docs array does not.
	if (out.empty() && text.find("\"docs\"") == std::string::npos && outReadError)
		*outReadError = kTrue;

	return !out.empty();
}

// Append a set of uint32 to json as "1, 2, 3" (the brackets are the caller's).
static void KCMAppendUintList(std::string& json, const std::set<uint32>& s)
{
	bool16 first = kTrue;
	for (std::set<uint32>::const_iterator u = s.begin(); u != s.end(); ++u)
	{
		if (!first)
			json += ", ";
		first = kFalse;
		char num[16];
		std::snprintf(num, sizeof(num), "%lu", (unsigned long)(*u));
		json += num;
	}
}

// Write path (UTF-8) -> (checks / registered) out to KCMPageChecks.json (version 2). The file
// written to comes back in outFile.
static bool16 KCMWriteSetsMap(const std::map<std::string, KCMDocSets>& in, IDFile& outFile)
{
	if (!KCMPageChecksFile(outFile))
		return kFalse;

	std::string json;
	json += "{\n";
	json += "  \"version\": 2,\n";
	json += "  \"docs\": [\n";
	bool16 firstDoc = kTrue;
	for (std::map<std::string, KCMDocSets>::const_iterator d = in.begin(); d != in.end(); ++d)
	{
		if (d->second.checks.empty() && d->second.registered.empty())
			continue;	// an empty entry is not written out
		if (!firstDoc)
			json += ",\n";
		firstDoc = kFalse;

		std::string esc;
		KCMJsonEscape(d->first, esc);
		json += "    { \"path\": \"";
		json += esc;
		json += "\", \"checks\": [";
		KCMAppendUintList(json, d->second.checks);
		json += "], \"registered\": [";
		KCMAppendUintList(json, d->second.registered);
		json += "] }";
	}
	json += "\n  ]\n}\n";

	FILE* fp = FileUtils::OpenFile(outFile, "wb");
	if (fp == nil)
		return kFalse;
	// Both the byte count and fclose are checked, so that a partial write -- a full disk -- is
	//   never reported as "saved". (The TSV export checks GetStreamState after its Flush for the
	//   same reason.)
	const size_t wrote = fwrite(json.data(), 1, json.size(), fp);
	const int closed = fclose(fp);
	return (wrote == json.size() && closed == 0) ? kTrue : kFalse;
}

// db's full file path as UTF-8 -- the key saving and loading tell documents apart by. kFalse for
// an unsaved document, which has no path.
// **The full path, not the file name**: a Target and a Source of the same name in different
//   folders would otherwise collide. The trade-off is that moving the document, or saving it
//   under a new name, breaks the match.
static bool16 KCMDocUtf8Path(IDataBase* db, std::string& outUtf8)
{
	outUtf8.clear();
	if (db == nil)
		return kFalse;
	const IDFile* f = db->GetSysFile();
	if (f == nil)
		return kFalse;
	PMString p = FileUtils::SysFileToPMString(*f);
	if (p.IsEmpty())
		return kFalse;
	outUtf8 = p.GetUTF8String();
	return !outUtf8.empty();
}

//----------------------------------------------------------------------------------------
// KCMPageCheckSaveToFile (declared in KCMPageCheck.h)
//----------------------------------------------------------------------------------------
void KCMPageCheckSaveToFile()
{
	if (!KCMIsArmed())
	{
		KCMSayStatus("Save: start first", kTrue /*forceRedrawNow*/);	// the status line is small (its Frame is in ui/KCMUI.fr), so keep it short
		return;
	}

	// Read the existing file first, so that what was saved for other documents survives, then
	// overwrite (or delete) the entries of the two documents being compared with their current
	// ticks and registrations.
	std::map<std::string, KCMDocSets> merged;
	bool16 readError = kFalse;
	KCMReadSetsMap(merged, &readError);	// empty when there is no file
	if (readError)
	{
		// The file exists but cannot be read, or is broken. Overwriting it now would delete what
		//   was saved for every other document, so give up instead.
		KCMSayStatus("Save failed (read old)", kTrue /*forceRedrawNow*/);
		return;
	}

	IDataBase* dbs[2] = { KCMArmedTargetDB(), KCMArmedSourceDB() };
	int32 savedDocs = 0;
	int32 skippedUnsaved = 0;
	for (int i = 0; i < 2; ++i)
	{
		IDataBase* db = dbs[i];
		if (db == nil)
			continue;
		std::string path;
		if (!KCMDocUtf8Path(db, path))
		{
			++skippedUnsaved;	// an unsaved document has no path to key on
			continue;
		}

		KCMDocSets sets;
		// The ticks
		std::set<UID> chk;
		sChecked.CollectInto(db, chk);
		for (std::set<UID>::const_iterator u = chk.begin(); u != chk.end(); ++u)
			sets.checks.insert((uint32)u->Get());
		// The registrations (Added/Removed = green "/"), which another module owns.
		std::set<UID> reg;
		KCMPageMapCollectRegistered(db, reg);
		for (std::set<UID>::const_iterator u = reg.begin(); u != reg.end(); ++u)
			sets.registered.insert((uint32)u->Get());

		if (!sets.checks.empty() || !sets.registered.empty())
		{
			merged[path] = sets;
			++savedDocs;
		}
		else
		{
			merged.erase(path);	// neither ticks nor registrations now: drop it from the file too
		}
	}

	// **With nothing to save, the file is not touched at all.** It used to be written even then,
	//   so pressing Save while both documents were empty -- right after a restart, say, Stop
	//   having cleared every tick -- wrote out a file from which merged.erase above had already
	//   removed what was saved before, while the status line said "Nothing to save". It looked
	//   as though nothing had happened, and Load could not bring it back. **Deleting from the
	//   file only ever rides along with a save that has something to write.**
	if (savedDocs == 0)
	{
		KCMSayStatus(skippedUnsaved > 0 ? "Save doc first" : "Nothing to save", kTrue /*forceRedrawNow*/);
		return;
	}

	IDFile outFile;
	if (!KCMWriteSetsMap(merged, outFile))
	{
		KCMSayStatus("Save failed (write)", kTrue /*forceRedrawNow*/);	// covers a failed open, write or close alike
		return;
	}

	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(FileUtils::SysFileToPMString(outFile));	// the path alone: a label or a count overflows the status line
	KCMNotifyStatus(msg, kTrue /*forceRedrawNow*/);
}

//----------------------------------------------------------------------------------------
// KCMPageCheckLoadFromFile (declared in KCMPageCheck.h)
//----------------------------------------------------------------------------------------
void KCMPageCheckLoadFromFile()
{
	if (!KCMIsArmed())
	{
		KCMSayStatus("Load: start first", kTrue /*forceRedrawNow*/);	// the status line is small, so keep it short
		return;
	}

	std::map<std::string, KCMDocSets> saved;
	if (!KCMReadSetsMap(saved))
	{
		KCMSayStatus("No saved data", kTrue /*forceRedrawNow*/);
		return;
	}

	IDataBase* tgt = KCMArmedTargetDB();
	IDataBase* src = KCMArmedSourceDB();
	IDataBase* dbs[2] = { tgt, src };

	// Which of the two compared documents have saved data, and where that data is.
	std::map<std::string, KCMDocSets>::const_iterator saveIt[2] = { saved.end(), saved.end() };
	bool16 anyDocFound = kFalse;

	// Each document's flat page list is collected in phase 1 and used again in phase 3 -- a
	// re-comparison adds and removes no pages -- and only for the documents that have saved data.
	// **The master pages are cached separately.** Phase 3 (restoring the ticks) looks at both,
	//   while phase 1 (restoring the registrations) looks at **ordinary pages only**: a master is
	//   never registered, since Register calls the shared reader with includeMasters=kFalse.
	//   Using one concatenated list for both would write the assumption "a master could be
	//   registered too" into the code.
	std::vector<UID> flatCache[2];
	std::vector<UID> masterCache[2];

	//--- Phase 1: apply the registrations to both documents, before the re-comparison so that ---
	//--- they reach the pairing. ---------------------------------------------------------------
	// Of the saved registered UIDs, only the pages that really exist in this document go into the
	// set. A document that has saved data is set even when its saved registrations are empty,
	// which is what restores the state as it was saved.
	int32 regApplied = 0;
	for (int i = 0; i < 2; ++i)
	{
		IDataBase* db = dbs[i];
		if (db == nil)
			continue;
		std::string path;
		if (!KCMDocUtf8Path(db, path))
			continue;
		std::map<std::string, KCMDocSets>::const_iterator s = saved.find(path);
		if (s == saved.end())
			continue;	// nothing saved for this document: its ticks and registrations stay as they are
		saveIt[i] = s;
		anyDocFound = kTrue;

		std::vector<UID> regPages;
		std::vector<UID>& flat = flatCache[i];
		KCMCollectPageUIDs(db, flat);		// phase 3 reuses this very list
		KCMCollectMasterPageUIDs(db, masterCache[i]);	// for phase 3 only; not used here
		for (size_t k = 0; k < flat.size(); ++k)
		{
			const UID u = flat[k];
			if (s->second.registered.count((uint32)u.Get()) > 0)
				regPages.push_back(u);
		}
		KCMPageMapReplaceRegistered(db, regPages);	// empty clears the document's registrations
		regApplied += (int32)regPages.size();
	}

	if (!anyDocFound)
	{
		KCMSayStatus("No saved data for docs", kTrue /*forceRedrawNow*/);
		return;
	}

	//--- Phase 2: re-compare, once. -------------------------------------------------------------
	// The registrations changed, so the pairing is rebuilt exactly as Start would. The
	// re-comparison is incremental, reusing the previous result for pages whose partner is
	// unchanged, and it refreshes the Added/Removed "/" thumbnails on the way (the purge includes
	// the registered pages). It also prunes the current ticks down to what may still be ticked,
	// which does not matter here: phase 3 replaces them with the saved ones anyway.
	// **If the re-comparison is cancelled** -- enough registration changes and the progress bar
	//   offers Cancel -- KCMDoMarkChangesDoc has already thrown every mark away. Walking on into
	//   phase 3 would find nothing eligible, restore not a single saved tick, and report
	//   "load chk0" as though that were the answer.
	//   So nothing is restored and, as on the Start route, everything goes back to Stop, leaving
	//   no running comparison without frames. The saved file is untouched, so starting again and
	//   loading again works.
	if (tgt != nil && src != nil)
	{
		PMString report;
		if (KCMDoMarkChangesDoc(tgt, src, report, kTrue /*allowIncremental*/) != kSuccess)
		{
			KCMToggleStartStop();		// armed, so this takes the Stop branch: strip removed,
										// disarmed, ticks and registrations dropped
			KCMSayStatus("Load cancelled", kTrue /*forceRedrawNow*/);
			return;
		}
	}

	//--- Phase 3: restore the ticks, keeping only the pages that may still be ticked after the ---
	//--- re-comparison. -------------------------------------------------------------------------
	int32 checksRestored = 0;
	for (int i = 0; i < 2; ++i)
	{
		IDataBase* db = dbs[i];
		if (db == nil || saveIt[i] == saved.end())
			continue;

		const std::set<uint32>& savedChecks = saveIt[i]->second.checks;

		// Build the "may this be ticked" answer once per document; asking per page costs
		// O(pages x changes). In the Story mode every page is eligible, so a saved tick comes
		// back whether the page carries a mark or not.
		KCMCheckablePages checkable;
		KCMCollectCheckable(db, checkable);

		// Restore from both the ordinary pages and the master pages: masters are compared, get
		//   frames, and therefore get ticks.
		//   @warning **the saving side always wrote master ticks out** -- it simply writes the
		//   UIDs in sChecked, which tell ordinary pages and masters apart not at all. While this
		//   loop only checked the ordinary page list, a master's tick could be saved but vanished
		//   silently on load.
		std::set<UID> newSet;
		const std::vector<UID>* lists[2] = { &flatCache[i], &masterCache[i] };	// collected in phase 1; not collected again
		for (int L = 0; L < 2; ++L)
		{
			const std::vector<UID>& flat = *lists[L];
			for (size_t k = 0; k < flat.size(); ++k)
			{
				const UID u = flat[k];
				if (savedChecks.count((uint32)u.Get()) > 0 && checkable.Includes(u))
					newSet.insert(u);
			}
		}

		// Refresh the thumbnails of the affected pages -- the old ticks together with the new ones
		// -- so that both the ticks gained and the ticks lost show. CollectInto does not clear
		// its out parameter, so adding the old ticks to newSet gives exactly that union.
		std::set<UID> affected = newSet;
		sChecked.CollectInto(db, affected);

		// Replace this document's ticks with the restored set (an empty one drops the entry).
		sChecked.Replace(db, newSet);
		checksRestored += (int32)newSet.size();

		if (!affected.empty())
		{
			// What travels is the union, not just the new ticks: **a tick that came off is in
			//   none of the new sets**, so it cannot be worked out from the current state
			//   afterwards. That is the whole reason the page set is carried on the notification.
			KCMNotifyPages(kKCMPageFlagsChangedMessage, db, affected);
			// The layout view's tick needs invalidating again. Phase 2's re-comparison
			// (KCMDoMarkChangesDoc) invalidated both documents, but against the ticks as they
			// were **before** the restore; without a second invalidation here the restored and
			// removed ticks do not reach the layout view -- the same reasoning as in the toggle.
			KCMInvalidateDB(db);
		}
	}

	// The outcome, abbreviated to fit the narrow status line.
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append("load chk");
	msg.AppendNumber(checksRestored);
	msg.Append(" reg");
	msg.AppendNumber(regApplied);
	KCMNotifyStatus(msg, kTrue /*forceRedrawNow*/);
}

// End of KCMPageCheck.cpp
