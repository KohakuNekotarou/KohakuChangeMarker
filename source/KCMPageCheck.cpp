//========================================================================================
//
//  KCMPageCheck.cpp
//
//  The "Check" feature (see KCMPageCheck.h). Select pages in the Pages panel, then the
//  context-menu toggle "Check" puts a "seen" mark on them and takes it off again. A checked page
//  gets a blue tick, drawn as vector strokes, in the middle of its Pages panel thumbnail (by
//  KCMDrawEventHandler's isThumb branch). The set is independent of the registrations
//  (KCMPageMap). ⚠It is a CACHE: since 2026-09-07 the tick lives IN THE DOCUMENT, as a script label
//  on the page (KCMPageMarksDoc.h - see "THIS IS A CACHE" below). (This line said "lives for the
//  session only" until 2026-09-27.)
//
//  ★**A TICK NO LONGER DEPENDS ON A COMPARISON** (2026-09-04, user decision). It can be put on any
//  open document, it survives Stop, and it travels with the .indd -- so the mark means
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

#include "IDataBase.h"
#include "PMString.h"

#include <set>
#include <vector>
// (FileUtils.h, IDFile.h, <map>, <string>, <cstdio> and KCMJsonText.h went on 2026-09-27 with the
//  private JSON store's last helpers - see the end of this file.)

#include "KCMCore.h"			// KCMCollectPageUIDs / KCMCollectMasterPageUIDs / KCMActiveDocDB / KCMIsComparedDoc / KCMArmedTargetDB / KCMArmedSourceDB / KCMDoMarkChangesDoc
								// ⚠KCMInvalidateDB is NOT among them any more (2026-09-07): redrawing moved to KCMMarksObserver, which is the only place that knows which pages moved
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it
#include "KCMComparisonRun.h"	// KCMStopComparison
#include "KCMPageCheck.h"
#include "KCMPageMap.h"		// KCMPageMapReadSelection - the Pages panel's selection, shared with Register
#include "KCMPawStamp.h"		// KCMPawStampsOnPage - a tick is written with the paws the page already carries
#include "KCMPageMarksCmd.h"	// KCMPageMarks / KCMMarksWrite -- the only door to a change
#include "KCMDocUidSet.h"		// the shared "document -> page UID set" container (Register uses it too)
#include "KCMThreadSafety.h"	// the shared-state lock, for reaching inside the container through GetMap
// ⚠KCMID.h was included here for kKCMPageFlagsChangedMessage and is gone with it (2026-09-07):
//   this file no longer notifies anybody. KCMMarksObserver does, because it is the only place that
//   can name the pages whose picture moved -- including the ones that LOST a mark.
// This file deliberately does not include the UI's KCMThumbnailRefresh.h: rebuilding a thumbnail
// is the job of whoever receives the notification, which is the UI.

// The ticked pages: document database -> set of page UIDs.
// An entry whose set became empty disappears at once (KCMDocUidSet's rule).
//
// ★★★**THIS IS A CACHE, NOT THE TRUTH** (2026-09-07). A tick lives in the document, as a script
//   label on the page that carries it (KCMPageMarksDoc.h). What is held here is a copy, kept
//   because the drawing side asks "is this page ticked" once per page per draw, on background
//   threads as well as the main one.
// ⚠**NOTHING IN THIS FILE MAY WRITE IT except KCMPageCheckReplaceAll**, which exists solely for
//   KCMMarksSyncFromDocument to call (KCMPageMarksDoc.h) -- the one road from the document to this
//   cache, driven by the marks observer and by the document-opened responder. Everything else here
//   READS it and then asks the command to write the DOCUMENT. That is what makes Ctrl+Z work: undo
//   puts the label back and the same road refills the cache from it.
//   Two writers are outside that road and belong outside it, because neither is a change to any
//   document: the closed-document sweep and the shutdown clear.
// ⚠**WHEN the refill happens was MEASURED, and it is not "later"** (2026-09-07): it has already
//   run by the next statement after KCMMarksWrite returns. Do not write code that waits for it,
//   and do not write code that assumes it has not happened yet -- read what you need BEFORE the
//   write instead. The toggle below carries the account of getting this wrong once.
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

	// What the press means: any eligible page still unticked ticks them all, otherwise they all
	// come off. ★**Asked without changing anything.** The store is a cache now, so the change is
	// made by writing the document and this only works out what to write.
	const int32  wasChecked   = sChecked.CountIn(db, pages);
	const int32  totalBefore  = sChecked.CountIn(db);		// ⚠**read BEFORE the write** -- see the status line below
	const bool16 anyUnchecked = (wasChecked < (int32)pages.size()) ? kTrue : kFalse;

	// ⚠**Each page's paws travel with its tick.** A write says what the page carries AFTERWARDS,
	//   so a page whose paws were left out of the list would lose them as a side effect of being
	//   ticked.
	std::vector<KCMPageMarks> wanted;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		KCMPageMarks m(pages[i], anyUnchecked);
		KCMPawStampsOnPage(db, pages[i], m.fPaws);
		wanted.push_back(m);
	}

	// One command for the whole selection, so five ticked pages are ONE Ctrl+Z and not five.
	// ⚠**A failure has to SAY so.** The write is the whole of what this menu item does, so a silent
	//   return leaves the reader looking at a menu item that did nothing and told them nothing --
	//   and the sequence has already rolled the document back, so there is not even a half-result
	//   on screen to hint at it.
	if (KCMMarksWrite(db, wanted, anyUnchecked ? "Check Pages" : "Uncheck Pages") != kSuccess)
	{
		KCMSayStatus("Could not write the marks into the document.");
		return;
	}

	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(anyUnchecked ? "check +" : "check -");
	msg.AppendNumber((int32)pages.size());

	// ★★**THE TOTAL IS THE ONE READ BEFORE THE WRITE, PLUS WHAT THIS PRESS DID.** Never the store
	//   read back afterwards, and never the store read afterwards MINUS the delta -- both of those
	//   are bets on WHEN the observer refills it, and the bet is not needed.
	// ⚠Measured 2026-09-07, and it went the other way from the guess: the observer had ALREADY run
	//   by the time this line was built (the lazy notification is flushed when the command sequence
	//   ends, not at some later idle), so a first attempt that subtracted the delta from the store
	//   printed "check -1, total -1". Reading before and adding is right whichever way the timing
	//   goes, which is the whole reason to write it this way rather than to correct the sign.
	msg.Append(", total ");
	msg.AppendNumber(totalBefore + (anyUnchecked ? (int32)pages.size() - wasChecked : -wasChecked));

	// Neither the thumbnails nor the layout view are refreshed here any more. KCMMarksObserver does
	// both when the write lands -- and does them again on undo and on redo, which this could not.
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
//   The flyout item "Clear Checks in This Document": take ONE document's ticks off, as a single
//   undoable step.
//   ★**THE PAGE SET IS STILL TAKEN FIRST**, though for a different reason than it used to be: the
//     write has to name the pages it clears, and once the ticks are gone nothing can say which
//     pages carried one. (Refreshing the screen is no longer done here at all -- the observer
//     works out what moved by comparing the store before with the store after.)
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

	// ⚠The paws of each of those pages are carried along untouched: this item clears TICKS, and a
	//   write says what the page carries afterwards.
	std::vector<KCMPageMarks> wanted;
	for (std::set<UID>::const_iterator it = cleared.begin(); it != cleared.end(); ++it)
	{
		KCMPageMarks m(*it, kFalse);
		KCMPawStampsOnPage(db, *it, m.fPaws);
		wanted.push_back(m);
	}

	if (KCMMarksWrite(db, wanted, "Clear Checks") != kSuccess)
		return 0;

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
//========================================================================================
// The doors KCMPageMarksDoc needs (2026-09-07): the same set the file-based save reads, reached
// from outside this file. Both go straight to the container, which does its own locking.
//========================================================================================
void KCMPageCheckCollect(IDataBase* db, std::set<UID>& out)
{
	out.clear();
	if (db != nil)
		sChecked.CollectInto(db, out);
}

void KCMPageCheckReplaceAll(IDataBase* db, const std::set<UID>& in)
{
	if (db != nil)
		sChecked.Replace(db, in);
}

bool16 KCMPageCheckHasAny(IDataBase* db)
{
	return sChecked.HasAny(db);
}

//----------------------------------------------------------------------------------------
// (KCMPageCheckSaveToFile and KCMPageCheckLoadFromFile stood here -- about 320 lines -- and
//  were removed on 2026-09-07 at the author's request, through the spec map's FLG-24:
//  "これはなくしましょう。 チェックは自動で書き込まれますし /は保存する必要無いかな".
//
//  They wrote the ticks, the paws and the registrations into a private JSON file beside the
//  document, and read them back. What made them unnecessary is that **the ticks and the paws now
//  go into the DOCUMENT itself the moment they are made** (KCMPageMarksDoc.h), undoably and
//  carried in the .indd -- so the file was a second copy of the same thing, keyed by a path that
//  broke whenever the document was moved. The registrations are an INPUT to one comparison and
//  are not worth keeping past it.
//
//  ⚠**Their two ActionIDs are dead numbers and are never reused** (ui/KCMUIID.h says why).
//  ⛔**AND THEIR HELPERS FOLLOWED ON 2026-09-27** (re-audit M5 round 3): the removal above took the two
//    public functions and left everything under them standing - the file's location, the whole-file
//    reader, the JSON key/array/object parsers, the v3 writer and the path key, about 530 lines that
//    nothing called, together with the "why not the SDK's IJsonUtils JSON class" reasoning they
//    carried. This comment said "git has all of it" while all of it was still here.
//  ⚠If a private store is ever wanted again, git has all of it: the JSON was v3 (paws added) and the
//    writer merged documents rather than replacing the file.
//----------------------------------------------------------------------------------------

// End of KCMPageCheck.cpp
