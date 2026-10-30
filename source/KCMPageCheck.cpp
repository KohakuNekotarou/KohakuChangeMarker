//========================================================================================
//
//  KCMPageCheck.cpp
//
//  The "Check" feature (see KCMPageCheck.h). Select pages in the Pages panel, then the
//  context-menu toggle "Check" puts a "seen" mark on them and takes it off again. A checked page
//  gets a blue tick, drawn as vector strokes, in the middle of its Pages panel thumbnail (by
//  KCMDrawEventHandler's isThumb branch). The set is independent of the registrations
//  (KCMPageMap). ⚠It is a CACHE: the tick lives IN THE DOCUMENT, as a script label on the page
//  (KCMPageMarksDoc.h - see "THIS IS A CACHE" below).
//
//  **A TICK DOES NOT DEPEND ON A COMPARISON** (the author's decision, spec map FLG-11 to FLG-13).
//  It can be put on any page of any open document, compared or not, in any mode; it survives Stop,
//  and it travels with the .indd -- so the mark means "I have looked at this page" rather than
//  "I have looked at this changed page". What takes one off is the toggle or the flyout's
//  "Clear Checks in This Document"; closing the document or shutting down only empties this cache
//  (FLG-26).
//
//  The structure follows KCMPageMap.cpp: the same one place the selection is decided on
//  (KCMPageMapPagesOf - on the list the UI read), the same per-document UID set, and a close sweep that compares
//  pointers without ever dereferencing one.
//
//  **Master pages count too.** The only difference from Register is that the shared reader is
//  called with includeMasters=kTrue here (Register passes kFalse): master spreads are compared
//  and do get frames, so a tick means on a master what it means anywhere else. A context menu does
//  not show disabled items, so a reader that returned no master would make Check look as though it
//  had vanished on masters alone.
//  The drawing side needs nothing for this: both the thumbnail tick and the layout tick
//  simply walk the pages of the spread being drawn, the purge is per page UID, and
//  KCMForceRedrawPagesPanelNow redraws the Master sub-panel as well.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "PMString.h"

#include <set>
#include <vector>

#include "KCMCore.h"			// KCMCollectCheckablePageUIDs / KCMCheckablePages - which pages may take a tick
#include "KCMModelNotify.h"	// KCMNotifyStatus - the model tells the UI, it never calls it
#include "KCMPageCheck.h"
#include "KCMPageMap.h"		// KCMPageMapPagesOf - the Pages panel's selection (the UI reads it), shared with Register
#include "KCMPageMarksCmd.h"	// KCMPageMarks / KCMMarksWriteForReader -- the reader's door to a change
#include "KCMPageMarksDoc.h"	// KCMMarksSyncFromDocument / KCMPageMarksNow -- what the document says
#include "KCMDocUidSet.h"		// the shared "document -> page UID set" container (Register uses it too)
// This file notifies nobody, so it has no KCMID.h either: KCMMarksSyncFromDocument does (the marks
//   observer drives it), because only it compares the store before and after, and so can name the
//   pages whose picture moved -- including the ones that LOST a mark.
// This file deliberately does not include the UI's KCMThumbnailRefresh.h: rebuilding a thumbnail
// is the job of whoever receives the notification, which is the UI.

// The ticked pages: document database -> set of page UIDs.
// An entry whose set became empty disappears at once (KCMDocUidSet's rule).
//
// **THIS IS A CACHE, NOT THE TRUTH.** A tick lives in the document, as a script
//   label on the page that carries it (KCMPageMarksDoc.h). What is held here is a copy, kept
//   because the drawing side asks "is this page ticked" once per page per draw, on background
//   threads as well as the main one.
// ⚠**NOTHING IN THIS FILE MAY WRITE IT except KCMPageCheckReplaceAll**, which exists solely for
//   KCMMarksSyncFromDocument to call (KCMPageMarksDoc.h) -- the one road from the document to this
//   cache, driven by the marks observer, by the document-opened responder, and by whoever is about
//   to count what it holds (the toggle and the Clear below, the paws' Clear). Everything else here
//   READS it and then asks the command to write the DOCUMENT. That is what makes Ctrl+Z work: undo
//   puts the label back and the same road refills the cache from it.
//   Two writers are outside that road and belong outside it, because neither is a change to any
//   document: the closed-document sweep and the shutdown clear.
// ⚠**WHEN the refill happens was MEASURED, and it is not "later"**: it has already
//   run by the next statement after KCMMarksWrite returns. Do not write code that waits for it,
//   and do not write code that assumes it has not happened yet -- read what you need BEFORE the
//   write instead. The toggle below carries the account of getting this wrong once.
static KCMDocUidSet sChecked;

// **Which pages may be ticked is answered in one place**, KCMCollectCheckablePageUIDs (KCMCore.h,
//   with the reasoning): every page of any open document (spec map FLG-12). This file only asks it.
//   (KCMCollectChangedPageUIDs - "could this page carry a mark" - is another question: KCMCore.h.)
//   **The toggle and the toggle's state ask it, and both are about PUTTING a tick on**, both
//     through KCMFilterToCheckable below (memory one-question-one-place).
//   ⚠**"Which ticks may STAY" is a different question with a different answer** - asking this one
//     instead throws ticks away. **If another route ever needs this, check which of the two
//     questions it is asking.**
// Keep only the selected pages that may be ticked: the answer is asked once, and Includes()
// judges each page.
static void KCMFilterToCheckable(IDataBase* db, const std::vector<UID>& pages, std::vector<UID>& out)
{
	out.clear();
	KCMCheckablePages checkable;
	if (!KCMCollectCheckablePageUIDs(db, checkable))
		return;		// there is no document at all -- every real one answers kTrue (KCMCore.h)
	for (size_t i = 0; i < pages.size(); ++i)
		if (checkable.Includes(pages[i]))
			out.push_back(pages[i]);
}

//========================================================================================
// KCMPageCheckToggleSelectedPages (declared in KCMPageCheck.h)
//========================================================================================
void KCMPageCheckToggleSelectedPages(const UIDList& selection)
{
	IDataBase* db = nil;
	std::vector<UID> selPages;
	if (!KCMPageMapPagesOf(selection, kTrue /*includeMasters*/, db, selPages))
		return;		// kCustomEnabling should already have greyed the menu out; belt and braces

	// NO COMPARISON IS REQUIRED (the head of this file). **Which pages may take a tick is still
	//   asked** -- of KCMCollectCheckablePageUIDs, through KCMFilterToCheckable above -- so the
	//   answer stays in one place; today it is every page.
	std::vector<UID> pages;
	KCMFilterToCheckable(db, selPages, pages);
	if (pages.empty())
		return;		// nothing eligible in the selection; the menu should be disabled anyway

	// What the press means: any eligible page still unticked ticks them all, otherwise they all
	// come off. **Asked without changing anything.** The store is a cache, so the change is
	// made by writing the document and this only works out what to write.
	// ⚠**The cache is brought back to what the document says before it is counted**
	//   (KCMMarksSyncFromDocument): it goes on naming a ticked page after the page is deleted, and the
	//   total on the status line counted that tick too (measured: "total 3" with two pages ticked).
	KCMMarksSyncFromDocument(db, nil, nil);
	const int32  wasChecked   = sChecked.CountIn(db, pages);
	const int32  totalBefore  = sChecked.CountIn(db);		// ⚠**read BEFORE the write** -- see the status line below
	const bool16 anyUnchecked = (wasChecked < (int32)pages.size()) ? kTrue : kFalse;

	// Each page as it stands (KCMPageMarksNow - its paws come along), with the tick changed.
	std::vector<KCMPageMarks> wanted;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		KCMPageMarks m = KCMPageMarksNow(db, pages[i]);
		m.fCheck = anyUnchecked;
		wanted.push_back(m);
	}

	// One command for the whole selection, so five ticked pages are ONE Ctrl+Z and not five.
	// ⚠**A failure has to SAY so** - the reader's door does (KCMMarksWriteForReader): the write is
	//   the whole of what this menu item does, and the sequence has already rolled the document
	//   back, so there is not even a half-result on screen to hint at it.
	if (KCMMarksWriteForReader(db, wanted, anyUnchecked ? "Check Pages" : "Uncheck Pages") != kSuccess)
		return;

	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(anyUnchecked ? "check +" : "check -");
	msg.AppendNumber((int32)pages.size());

	// **THE TOTAL IS THE ONE READ BEFORE THE WRITE, PLUS WHAT THIS PRESS DID.** Never the store
	//   read back afterwards, and never the store read afterwards MINUS the delta -- both of those
	//   are bets on WHEN the observer refills it, and the bet is not needed.
	// ⚠Measured, and it went the other way from the guess: the observer had ALREADY run
	//   by the time this line was built (the lazy notification is flushed when the command sequence
	//   ends, not at some later idle), so a first attempt that subtracted the delta from the store
	//   printed "check -1, total -1". Reading before and adding is right whichever way the timing
	//   goes, which is the whole reason to write it this way rather than to correct the sign.
	msg.Append(", total ");
	msg.AppendNumber(totalBefore + (anyUnchecked ? (int32)pages.size() - wasChecked : -wasChecked));

	// Neither the thumbnails nor the layout view are refreshed here: the marks observer's road does
	// both when the write lands -- and does them again on undo and on redo, which this could not.
	KCMNotifyStatus(msg);
}

//========================================================================================
// KCMPageCheckGetToggleState (declared in KCMPageCheck.h)
//   Like the Register side, this **only answers**: writing to the menu is the UI's job (the
//   reasoning is with KCMPageToggleState in KCMPageFlagState.h).
//========================================================================================
KCMPageToggleState KCMPageCheckGetToggleState(const UIDList& selection)
{
	KCMPageToggleState st;	// disabled by default

	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KCMPageMapPagesOf(selection, kTrue /*includeMasters*/, db, pages))
		return st;

	// Disabled when the selection holds no page that may be ticked -- **the very same rule the toggle
	//   itself uses**: greying the item here while the toggle would have accepted the click is a
	//   menu that lies about what the command does. Both go through KCMFilterToCheckable above.
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
//   **THE PAGE SET IS TAKEN FIRST**: the write has to name the pages it clears, and once the
//     ticks are gone nothing can say which pages carried one. (Refreshing the screen is not done
//     here at all -- the observer's road works out what moved by comparing the store before with
//     the store after.)
//   ⚠**AND THE STORE IS BROUGHT BACK TO WHAT THE DOCUMENT SAYS BEFORE THAT** (KCMMarksSyncFromDocument).
//     It goes on naming a ticked page after the page is deleted, and the write fails as a whole on a
//     page that is gone: the remaining ticks stayed and the status line said "cleared chk0"
//     (measured).
//   @return how many ticks were dropped (0 when there were none), or -1 when the write failed and
//     none were - the reader's door (KCMMarksWriteForReader) has said so on the status line.
//========================================================================================
int32 KCMPageCheckClearDoc(IDataBase* db)
{
	if (db == nil)
		return 0;

	KCMMarksSyncFromDocument(db, nil, nil);

	std::set<UID> cleared;
	sChecked.CollectInto(db, cleared);		// **before**, never after
	if (cleared.empty())
		return 0;							// nothing to do, and nothing to tell anyone about

	// Each of those pages as it stands (KCMPageMarksNow - its paws come along), with the tick off.
	std::vector<KCMPageMarks> wanted;
	for (std::set<UID>::const_iterator it = cleared.begin(); it != cleared.end(); ++it)
	{
		KCMPageMarks m = KCMPageMarksNow(db, *it);
		m.fCheck = kFalse;
		wanted.push_back(m);
	}

	if (KCMMarksWriteForReader(db, wanted, "Clear Checks") != kSuccess)
		return -1;							// nothing came off (the sequence rolled it back); the door said so

	return (int32)cleared.size();
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
// The doors KCMMarksSyncFromDocument (KCMPageMarksDoc.cpp) needs: the set as it stands, and its
// replacement. Both go straight to the container, which does its own locking.
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

// End of KCMPageCheck.cpp
