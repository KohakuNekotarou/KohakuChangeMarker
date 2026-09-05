//========================================================================================
//
//  KCMPageCheck.cpp
//
//  The "Check" feature (see KCMPageCheck.h). Select pages in the Pages panel, then the
//  context-menu toggle "Check" puts a "seen" mark on them and takes it off again. A checked page
//  gets a blue tick, drawn as vector strokes, in the middle of its Pages panel thumbnail (by
//  KCMDrawEventHandler's isThumb branch). The set is independent of the registrations
//  (KCMPageMap) and lives for the session only.
//
//  ★**A TICK NO LONGER DEPENDS ON A COMPARISON** (2026-09-04, user decision). It can be put on any
//  open document, it survives Stop, and it is saved and restored on its own -- so the mark means
//  "I have looked at this page" rather than "I have looked at this changed page". What ends one is
//  the flyout's "Clear Checks in This Document", closing the document, or shutdown.
//  ⚠While a comparison IS running, which of the compared pages may take a tick is still the mode's
//  business (Pixel = the marked ones, Story = all) -- that part did not change.
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

#include "KCMCore.h"			// KCMCollectPageUIDs / KCMCollectMasterPageUIDs / KCMActiveDocDB / KCMIsComparedDoc / KCMArmedTargetDB / KCMArmedSourceDB / KCMDoMarkChangesDoc / KCMInvalidateDB
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it
#include "KCMComparisonRun.h"	// KCMToggleStartStop
#include "KCMPageCheck.h"
#include "KCMPageMap.h"		// KCMPageMapCollectRegistered (save) / KCMPageMapReplaceRegistered (load)
#include "KCMPawStamp.h"		// KCMPawStampGetForSave / KCMPawStampReplaceAll -- the cat-paw stamps ride this same file
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
//     - not being compared ... **every page** (a tick needs no comparison -- 2026-09-04)
//     - Pixel mode ....... only pages carrying a mark (a frame, a registered "/", an overflow "/")
//     - Story mode ....... **every page** of the Target and the Source
//   ★**TWO routes ask it, and both are about PUTTING a tick on**: the toggle and the toggle's
//     state. Both go through KCMCollectCheckable / KCMFilterToCheckable below; calling
//     KCMCollectChangedPageUIDs directly instead only ever gives the Pixel answer
//     ([[one-question-one-place]]).
//   ⚠**It used to be four**, and the two that went are the whole of 2026-09-04's bug: the prune
//     and Load's restore asked this same question to decide **which ticks may STAY**, which is a
//     different question and had a different right answer. Both now test only that the page
//     exists. **If a third route ever needs this, check which of the two questions it is asking.**
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
		return;		// there is no document at all -- every real one answers kTrue now (KCMCore.h)
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

	// ★NO COMPARISON IS REQUIRED (2026-09-04, user decision). A tick is the reader's own marker:
	//   it goes on any open document, it survives Stop, and it is saved and restored on its own.
	//   **Which pages may take one is still asked** -- of KCMCollectCheckablePageUIDs, which
	//   answers "every page" for a document nobody is comparing and keeps the Pixel rule for the
	//   two that are being compared.

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

	// ★NO COMPARISON IS REQUIRED (2026-09-04) -- **and it has to be the very same rule the toggle
	//   itself uses**: greying the item here while the toggle would have accepted the click is a
	//   menu that lies about what the command does. Both go through KCMFilterToCheckable below.

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
// KCMPageCheckClearDoc (declared in KCMPageCheck.h)
//   The flyout item "Clear Checks in This Document": drop ONE document's ticks and tell the UI
//   which pages changed, so that the Pages panel's thumbnails lose their ticks along with the
//   layout view.
//   ★**THE PAGE SET IS TAKEN BEFORE THE TICKS GO.** Once they are gone nothing can say which
//     pages carried one -- the notification carries a page set, and there would be no set left to
//     build it from. (Load's phase 3 carries the union of old and new ticks for the same reason.)
//   @return how many ticks were dropped, for the status line.
//========================================================================================
int32 KCMPageCheckClearDoc(IDataBase* db)
{
	if (db == nil)
		return 0;

	std::set<UID> cleared;
	sChecked.CollectInto(db, cleared);		// **before**, never after
	if (cleared.empty())
		return 0;							// nothing to do, and nothing to tell anyone about

	sChecked.Replace(db, std::set<UID>());	// an empty set drops the document's entry outright

	KCMNotifyPages(kKCMPageFlagsChangedMessage, db, cleared);
	KCMInvalidateDB(db);					// the layout view's ticks, which the notification does not cover
	return (int32)cleared.size();
}

//========================================================================================
// (KCMPageCheckPruneToMarked lived here and was REMOVED on 2026-09-04.)
//   It narrowed each document's ticks to the pages that still carried a mark, after every
//   re-comparison -- "the frame is gone, and the memory of having checked it goes with it".
//   ★**That reading died with the tick's own meaning.** A tick now says "I have looked at this
//     page", and looking at a page is not undone by the page turning out to be unchanged.
//   ⚠**It was doing real harm by the end**: ticking a document that nobody was comparing and then
//     starting a comparison ON that document threw those ticks away at the moment the comparison
//     began, because the prune ran at the end of every comparison and judged them by the Pixel
//     rule. Loading a saved set into a compared document lost the same ticks the same way.
//   Nothing replaced it. A tick on a page that has since been deleted is never drawn (the drawing
//     side walks the spread's real pages), is dropped on the way into Load (which walks them too),
//     and goes with the document at close (KCMPageCheckSweepClosedDocs).
//========================================================================================

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
//   The format (version 3):
//     {
//       "version": 3,
//       "docs": [
//         { "path": "<utf8 path>", "checks": [12, 45], "registered": [3, 7],
//           "paws": [ {"page":12,"x":12340,"y":5670,"c":1} ] }
//       ]
//     }
//   Reading is lenient: the version is not looked at, and an old v1 file's "pages" array is
//     accepted as checks (an old file without "registered" simply has no registrations).
//
//   ★★"paws" ARRIVED IN VERSION 3 (2026-09-04) -- the cat-paw stamps, which unlike the two arrays
//     above carry coordinates, so they are objects rather than bare UIDs.
//     - **x and y are HUNDREDTHS OF A POINT, as integers**, measured from the page's top-left.
//       ⚠Not a decimal: sprintf/strtod follow LC_NUMERIC, so a decimal would be written "123,4" on
//       a machine whose locale says so and read back as 123. 1/100 pt is 3.5 micrometres.
//     - **"c" is the colour** (0 pink / 1 cyan / 2 green), optional, defaulting to pink.
//     ★Because the version is not looked at, BOTH directions are safe: an older KCM reading a v3
//       file skips "paws", and this KCM reading a v2 file finds none.
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
// UID values (uint32), and the cat-paw stamps (which carry coordinates, so they are not a set).
struct KCMDocSets
{
	std::set<uint32> checks;
	std::set<uint32> registered;
	std::vector<KCMPawStamp> paws;		// version 3 onward; absent from a file written by an older KCM
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

// One integer that follows key inside [from, to). kFalse when the key is not there, so "absent"
// can be told from "zero" -- which matters for "c", where 0 is a real colour (pink).
// ⚠Digits and a leading '-' only. **No strtod, no locale**: see the note on KCMAppendPawList.
static bool16 KCMReadJsonInt(const std::string& text, size_t from, size_t to,
	const char* key, int32& out)
{
	const size_t k = text.find(key, from);
	if (k == std::string::npos || k >= to)
		return kFalse;
	size_t p = text.find(':', k);
	if (p == std::string::npos || p >= to)
		return kFalse;
	++p;
	while (p < to && (text[p] == ' ' || text[p] == '\t'))
		++p;

	bool16 neg = kFalse;
	if (p < to && text[p] == '-')
	{
		neg = kTrue;
		++p;
	}
	if (p >= to || text[p] < '0' || text[p] > '9')
		return kFalse;

	int32 v = 0;
	while (p < to && text[p] >= '0' && text[p] <= '9')
	{
		v = v * 10 + (int32)(text[p] - '0');
		++p;
	}
	out = neg ? -v : v;
	return kTrue;
}

// Read a "paws" array: [ {"page":12,"x":12340,"y":5670,"c":1}, ... ].
// ★Lenient in the same way as everything else in this file: an entry that cannot be made sense of
//   is skipped rather than failing the whole document, and a MISSING "paws" is simply no paws --
//   which is exactly what a version 2 file is.
// ★"c" is optional and defaults to pink, so a file written before the colours existed restores as
//   what it was drawn in.
static void KCMParsePawArray(const std::string& text, size_t regionBegin, size_t regionEnd,
	std::vector<KCMPawStamp>& out)
{
	out.clear();
	const std::string key("\"paws\"");
	const size_t k = KCMFindJsonKey(text, regionBegin, key);
	if (k == std::string::npos || k >= regionEnd)
		return;
	const size_t lb = text.find('[', k + key.size());
	if (lb == std::string::npos || lb >= regionEnd)
		return;
	const size_t rb = text.find(']', lb + 1);
	if (rb == std::string::npos || rb > regionEnd)
		return;

	size_t p = lb + 1;
	while (p < rb)
	{
		const size_t ob = text.find('{', p);
		if (ob == std::string::npos || ob >= rb)
			break;
		const size_t oe = text.find('}', ob + 1);
		if (oe == std::string::npos || oe > rb)
			break;

		int32 page = 0, xh = 0, yh = 0, colour = kKCMPawColourPink;
		if (KCMReadJsonInt(text, ob, oe, "\"page\"", page) && page > 0 &&
			KCMReadJsonInt(text, ob, oe, "\"x\"", xh) &&
			KCMReadJsonInt(text, ob, oe, "\"y\"", yh))
		{
			if (!KCMReadJsonInt(text, ob, oe, "\"c\"", colour) ||
				colour < kKCMPawColourPink || colour > kKCMPawColourGreen)
			{
				colour = kKCMPawColourPink;		// absent, or a value this build does not know
			}
			out.push_back(KCMPawStamp(UID((uint32)page),
			                          PMReal(xh) / PMReal(100.0),
			                          PMReal(yh) / PMReal(100.0),
			                          colour));
		}
		p = oe + 1;
	}
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
		// v3's cat-paw stamps. Absent in an older file, which reads as "no paws".
		KCMParsePawArray(text, q, regionEnd, sets.paws);

		if (!sets.checks.empty() || !sets.registered.empty() || !sets.paws.empty())
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

// Append the cat-paw stamps as {"page":12,"x":12340,"y":5670,"c":1}, ... (the brackets are the
// caller's).
//
//  ⚠★★★THE COORDINATES ARE HUNDREDTHS OF A POINT, AS INTEGERS, and that is not tidiness. Writing
//    them as decimals would hand them to the C runtime's LC_NUMERIC: sprintf("%f") writes "123,4"
//    where the locale's decimal separator is a comma, and strtod reads it back as 123. InDesign
//    runs in the host's locale, so this is not "one day on someone's machine" -- it is certain on
//    a machine set that way. 1/100 pt is 3.5 micrometres, far finer than anything a hand-placed
//    mark needs, so making it an integer removes the whole class of fault.
//  ★"c" is the colour (a KCMPawColour: 0 pink / 1 cyan / 2 green). Absent in a file written before
//    colours existed, and 0 is pink -- which is what those stamps were drawn in.
static void KCMAppendPawList(std::string& json, const std::vector<KCMPawStamp>& v)
{
	for (size_t i = 0; i < v.size(); ++i)
	{
		if (i > 0)
			json += ", ";
		char buf[96];
		// ⚠%d only: the locale can reach sprintf through the decimal point, and there is none here.
		std::snprintf(buf, sizeof(buf), "{\"page\":%lu,\"x\":%d,\"y\":%d,\"c\":%d}",
		              (unsigned long)v[i].fPageUID.Get(),
		              (int)::ToInt32(v[i].fX * PMReal(100.0)),
		              (int)::ToInt32(v[i].fY * PMReal(100.0)),
		              (int)v[i].fColour);
		json += buf;
	}
}

// Write path (UTF-8) -> (checks / registered / paws) out to KCMPageChecks.json (version 3). The
// file written to comes back in outFile.
static bool16 KCMWriteSetsMap(const std::map<std::string, KCMDocSets>& in, IDFile& outFile)
{
	if (!KCMPageChecksFile(outFile))
		return kFalse;

	std::string json;
	json += "{\n";
	// ★★VERSION 3 = the paws were added. ⚠**The reading side does not look at this number** (it
	//   asks for keys and ignores what it does not know), which is what makes both directions
	//   safe: an older KCM reading a v3 file simply skips "paws", and this KCM reading a v2 file
	//   simply finds none. The number is here for a human reading the file.
	json += "  \"version\": 3,\n";
	json += "  \"docs\": [\n";
	bool16 firstDoc = kTrue;
	for (std::map<std::string, KCMDocSets>::const_iterator d = in.begin(); d != in.end(); ++d)
	{
		if (d->second.checks.empty() && d->second.registered.empty() && d->second.paws.empty())
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
		json += "], \"paws\": [";
		KCMAppendPawList(json, d->second.paws);
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
	// ★THE ACTIVE DOCUMENT, ALWAYS -- with a comparison running or without one (2026-09-04, user
	//   decision). Two things were wrong with the old rule ("only while armed, and then both
	//   compared documents"): the ticks and the paws no longer need a comparison to EXIST, so
	//   refusing without one produced state that could not be saved at all (measured 2026-09-04 --
	//   after Stop the paws went on being drawn while Save answered "start first", the menu item
	//   staying enabled throughout); and letting "which documents" depend on the comparison is a
	//   second rule where one does.
	//   ⚠**Widening it to "every open document" instead would have cost data.** A document that
	//   holds saved paws, opened but not yet Loaded, holds nothing in memory -- and it would have
	//   been written out as empty. The active document is the one in front of the reader, so what
	//   gets saved is always what they are looking at.
	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
	{
		KCMSayStatus("Save: no document", kTrue /*forceRedrawNow*/);	// the status line is small (its Frame is in ui/KCMUI.fr), so keep it short
		return;
	}

	std::string path;
	if (!KCMDocUtf8Path(db, path))
	{
		KCMSayStatus("Save doc first", kTrue /*forceRedrawNow*/);	// an unsaved document has no path to key on
		return;
	}

	// What this document holds right now: the ticks, the registrations (Added/Removed = the green
	// "/", which another module owns) and the cat-paw stamps (a third module -- they carry
	// coordinates, so unlike the other two they travel as a list rather than a set of UIDs).
	KCMDocSets sets;
	std::set<UID> chk;
	sChecked.CollectInto(db, chk);
	for (std::set<UID>::const_iterator u = chk.begin(); u != chk.end(); ++u)
		sets.checks.insert((uint32)u->Get());
	std::set<UID> reg;
	KCMPageMapCollectRegistered(db, reg);
	for (std::set<UID>::const_iterator u = reg.begin(); u != reg.end(); ++u)
		sets.registered.insert((uint32)u->Get());
	KCMPawStampGetForSave(db, sets.paws);

	// **With nothing to save, the file is not touched at all** -- not even read, and this
	//   document's record in it is left exactly as it was.
	//   ★This is deliberate, and it is what keeps a saved record safe: writing an empty document
	//   out would mean "forget what was saved for it", and **nothing here can tell the two apart**
	//   -- "I cleared this document's marks" and "I have not Loaded them back yet" both look like
	//   a document holding nothing. So **a record is only ever replaced by a save that has
	//   something to write.**
	//   ⚠There is therefore no way to delete a record through Save, by design. A stale record
	//   costs a few bytes; deleting one the reader still wanted costs their work.
	//   (The rule predates this change -- it was written after a save-with-nothing wiped records
	//   for real -- and the change only narrows what "nothing" can mean.)
	if (sets.checks.empty() && sets.registered.empty() && sets.paws.empty())
	{
		KCMSayStatus("Nothing to save", kTrue /*forceRedrawNow*/);
		return;
	}

	// Read the existing file first, so that what was saved for OTHER documents survives, then
	// replace this one's entry.
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

	merged[path] = sets;

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
	// ★THE ACTIVE DOCUMENT, ALWAYS -- the rule Save follows, and it has to be the same one:
	//   a state that can be saved but not loaded back (or the reverse) is worse than either rule
	//   on its own. (2026-09-04, user decision.)
	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
	{
		KCMSayStatus("Load: no document", kTrue /*forceRedrawNow*/);	// the status line is small, so keep it short
		return;
	}

	std::map<std::string, KCMDocSets> saved;
	if (!KCMReadSetsMap(saved))
	{
		KCMSayStatus("No saved data", kTrue /*forceRedrawNow*/);
		return;
	}

	std::string path;
	if (!KCMDocUtf8Path(db, path))
	{
		KCMSayStatus("Save doc first", kTrue /*forceRedrawNow*/);	// an unsaved document has no path to key on
		return;
	}
	std::map<std::string, KCMDocSets>::const_iterator s = saved.find(path);
	if (s == saved.end())
	{
		KCMSayStatus("No saved data for doc", kTrue /*forceRedrawNow*/);
		return;		// nothing saved for it: its ticks, registrations and paws stay as they are
	}

	// The page list is collected here and used by both phases -- a re-comparison adds and removes
	// no pages, so collecting it twice would only cost time.
	// **The master pages are collected separately.** Phase 3 (the ticks and the paws) looks at
	//   both, while phase 1 (the registrations) looks at **ordinary pages only**: a master is
	//   never registered, since Register calls the shared reader with includeMasters=kFalse.
	//   Using one concatenated list for both would write the assumption "a master could be
	//   registered too" into the code.
	std::vector<UID> pageList;
	std::vector<UID> masterList;
	KCMCollectPageUIDs(db, pageList);
	KCMCollectMasterPageUIDs(db, masterList);

	//--- Phase 1: apply the registrations, before the re-comparison so that they reach the -------
	//--- pairing. -------------------------------------------------------------------------------
	// Of the saved registered UIDs, only the pages that really exist in this document go into the
	// set. An empty saved set is applied too, which is what restores the state as it was saved.
	//
	// ★★**THE REGISTRATIONS STAY TIED TO THE COMPARISON -- user decision, 2026-09-04. Do not
	//   re-propose.** The ticks and the paws stopped depending on it that same day; the
	//   registrations deliberately did not, because a registration is an INPUT to the comparison
	//   ("treat this page as added") rather than a mark the reader leaves behind.
	//   ⇒ **They are restored here even with nothing being compared, but the green "/" is drawn
	//   only for the two documents that ARE being compared** (the Target/Source loops in
	//   KCMDrawEventHandler). So a Load without a comparison restores them invisibly, and they
	//   appear the moment one starts. **That asymmetry is intended, not an oversight** -- it was
	//   put to the reader as a choice against making all three alike, and this is the answer.
	std::vector<UID> regPages;
	for (size_t k = 0; k < pageList.size(); ++k)
	{
		const UID u = pageList[k];
		if (s->second.registered.count((uint32)u.Get()) > 0)
			regPages.push_back(u);
	}
	KCMPageMapReplaceRegistered(db, regPages);	// empty clears the document's registrations
	const int32 regApplied = (int32)regPages.size();

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
	// ★**ONLY WHEN THE DOCUMENT JUST LOADED IS PART OF A RUNNING COMPARISON** (2026-09-04).
	//   With nothing being compared, or with the active document a third one, its registrations
	//   change nothing that is on screen and re-comparing would be work nobody asked for.
	//   ⚠**The pair re-compared is the ARMED pair, not the active document.** The comparison
	//   belongs to those two whichever of them the reader happens to be looking at, and one side's
	//   registrations changing is reason enough to rebuild the pairing -- it is built from both.
	if (KCMIsComparedDoc(db))
	{
		IDataBase* tgt = KCMArmedTargetDB();
		IDataBase* src = KCMArmedSourceDB();
		if (tgt != nil && src != nil)
		{
			PMString report;
			if (KCMDoMarkChangesDoc(tgt, src, report, kTrue /*allowIncremental*/) != kSuccess)
			{
				KCMToggleStartStop();		// armed, so this takes the Stop branch: strip removed,
											// disarmed, registrations dropped (⚠the ticks survive
											// a Stop now, so they are not among the casualties)
				KCMSayStatus("Load cancelled", kTrue /*forceRedrawNow*/);
				return;
			}
		}
	}

	//--- Phase 3: restore the ticks, keeping only the pages that may still be ticked after the ---
	//--- re-comparison. -------------------------------------------------------------------------
	int32 checksRestored = 0;
	int32 pawsRestored = 0;
	{
		const std::set<uint32>& savedChecks = s->second.checks;

		// ★**THE ONLY TEST IS "DOES THE PAGE STILL EXIST"** (2026-09-04) -- and the loops below
		//   already walk the pages that do, so there is nothing left to ask.
		//   It used to ask "may this page be ticked" as well, which meant a saved tick came back
		//   only where the page still carried a mark. That was right while a tick meant "I looked
		//   at this changed page"; it is wrong now that it means "I looked at this page", and it
		//   showed up as **a Load into a compared document silently restoring fewer ticks than
		//   were saved**. The same reading is what the prune used to do, and it went for the same
		//   reason -- the paws never asked the question at all.

		// Restore from both the ordinary pages and the master pages: masters are compared, get
		//   frames, and therefore get ticks.
		//   @warning **the saving side always wrote master ticks out** -- it simply writes the
		//   UIDs in sChecked, which tell ordinary pages and masters apart not at all. While this
		//   loop only checked the ordinary page list, a master's tick could be saved but vanished
		//   silently on load.
		std::set<UID> newSet;
		const std::vector<UID>* lists[2] = { &pageList, &masterList };	// collected above; not collected again
		for (int L = 0; L < 2; ++L)
		{
			const std::vector<UID>& flat = *lists[L];
			for (size_t k = 0; k < flat.size(); ++k)
			{
				const UID u = flat[k];
				if (savedChecks.count((uint32)u.Get()) > 0)
					newSet.insert(u);
			}
		}

		//--- The cat-paw stamps, restored beside the ticks. ---
		// ★THE ONLY TEST IS "DOES THE PAGE STILL EXIST", and deliberately so: a paw does not
		//   depend on a comparison having run -- it can be put on a document nobody is comparing --
		//   so the "may this be ticked" question above has nothing to say about it. What WOULD go
		//   wrong is a stamp pointing at a page deleted since the save, which is what this drops.
		// ⚠Both lists, ordinary pages and masters, for the reason the ticks read both.
		{
			std::set<uint32> livePages;
			for (int L = 0; L < 2; ++L)
			{
				const std::vector<UID>& flat = *lists[L];
				for (size_t k = 0; k < flat.size(); ++k)
					livePages.insert((uint32)flat[k].Get());
			}

			const std::vector<KCMPawStamp>& savedPaws = s->second.paws;
			std::vector<KCMPawStamp> livePaws;
			for (size_t k = 0; k < savedPaws.size(); ++k)
			{
				if (livePages.count((uint32)savedPaws[k].fPageUID.Get()) > 0)
					livePaws.push_back(savedPaws[k]);
			}
			// Replace, so that loading restores the saved state rather than adding to what is
			// there. An empty list clears the document's paws, which is the saved state too.
			KCMPawStampReplaceAll(db, livePaws);
			pawsRestored += (int32)livePaws.size();
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
		}

		// The layout view's tick needs invalidating again. Phase 2's re-comparison
		// (KCMDoMarkChangesDoc) invalidated both documents, but against the ticks as they
		// were **before** the restore; without a second invalidation here the restored and
		// removed ticks do not reach the layout view -- the same reasoning as in the toggle.
		// ⚠★UNCONDITIONAL since the paws joined this file (2026-09-04). It used to sit inside the
		//   test above, which asks about TICKS -- and a document whose ticks did not move can
		//   still have gained or lost paws, so the restored paws would have waited for some other
		//   reason to redraw. (The notification above stays conditional: it carries a page set,
		//   and there is no page set to carry when no tick moved.)
		KCMInvalidateDB(db);
	}

	// The outcome, abbreviated to fit the narrow status line.
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append("load chk");
	msg.AppendNumber(checksRestored);
	msg.Append(" reg");
	msg.AppendNumber(regApplied);
	// ★Only when there are any: the line is narrow, and a reader who never used the stamp tool
	//   should not have to read about it. (The two counts above are always shown because Load is
	//   about them.)
	if (pawsRestored > 0)
	{
		msg.Append(" paw");
		msg.AppendNumber(pawsRestored);
	}
	KCMNotifyStatus(msg, kTrue /*forceRedrawNow*/);
}

// End of KCMPageCheck.cpp
