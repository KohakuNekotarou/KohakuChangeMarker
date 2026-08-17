//========================================================================================
//
//  IKESCMMarkData.h
//
//  Reading the comparison result: which pages carry marks, how much of each page changed,
//  which pages overflow the pairing, and where the overset is.
//
//  Created 2026-08-13 for the model/UI split (Stage 1), Task 12.
//
//  ★READ ONLY, on purpose. Marks are produced in exactly one place -- IKESCMCompareFacade --
//  and if writing were possible here there would be two answers to "who builds the marks".
//  The scrollbar map, Prev/Next, the Pages panel thumbnails, the TSV export and the press
//  gesture are all readers; none of them may write.
//
//  This interface replaces the UI reaching directly into KESCMDrawEventHandler::sEntries and
//  its siblings. Those are public static members, which worked only because everything shared
//  one .pln: a static lives in the plug-in that defines it, so once the UI is its own .pln the
//  linker has nothing to bind to.
//
//  ★WHAT IS DELIBERATELY NOT HERE: the display toggles (Show Marks on Source, Show Original
//  Page Numbers, Hold to Hide Marks) and the press-time display state. The UI writes those, so
//  they sit on IKESCMCompareFacade beside GetPrintMarks() rather than breaking the read-only
//  rule here.
//
//========================================================================================

#ifndef __IKESCMMarkData_h__
#define __IKESCMMarkData_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "UIDRef.h"				// UID
#include <vector>
#include <set>

// Project includes:
#include "KESCMBoundaryID.h"	// IID_IKESCMMARKDATA。★2026-08-17 に KESCMID.h から絞った(理由は
								// IKESCMCompareFacade.h の同じ位置。境界が要るのは自分の IID だけ)
#include "KESCMOversetScan.h"	// KESCMOversetLoc を借りるため。⚠2026-08-17 訂正＝旧「a type only」は
								// 不正確で、このヘッダーは free function の宣言も 1 本連れてくる(実測)。
								// 型が目的なのは本当だが、UI 側にはその 1 本も見えている(呼べばリンクエラー)

class IDataBase;

class IKESCMMarkData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKESCMMARKDATA };

	// ---- which documents the marks belong to --------------------------------------------

	/** The documents the current marks were built from, or nil when there are none. These are
		identity only: compare the pointer, never dereference it. A closed database is a
		dangling pointer whose address gets reused. */
	virtual IDataBase*	GetMarkedTargetDB() = 0;
	virtual IDataBase*	GetMarkedSourceDB() = 0;

	// ---- per page ------------------------------------------------------------------------

	/** kTrue when this Target page has a mark entry, i.e. it was compared and found different. */
	virtual bool16		HasEntryForPage(UID pageUID) = 0;

	/** kTrue when this SOURCE page is the partner of a changed Target page. The Source side has
		no entries of its own -- it borrows the Target's ring image through the pairing table --
		so "is this Source page marked" is a different question from HasEntryForPage. */
	virtual bool16		IsSourcePageMarked(UID sourcePageUID) = 0;

	/** How much of the page changed, as the raw counts: outChanged = low-resolution cells that
		differed, outTotal = cells on the page (the entry's image is the denominator). kFalse
		when the page has no entry, in which case both are 0.

		★The counts, not a percentage. The one place that turns them into text builds the
		digits itself with integer arithmetic, because PMString's real formatting would print a
		comma for the decimal point in some locales. Computing a ratio here as well would put
		the same decision in two places. */
	virtual bool16		GetChangeCells(UID pageUID, int32& outChanged, int32& outTotal) = 0;

	/** kTrue when this page is "overflow": the two documents have a different number of pages
		and this one has no partner at all (drawn as "/"). isTargetSide picks which side's set
		to ask; db must be the database that side belongs to, and kFalse comes back when the
		cached pairing was built for a different document.
		The cache is brought up to date internally, so callers do not have to. */
	virtual bool16		IsOverflowPage(IDataBase* db, UID pageUID, bool16 isTargetSide) = 0;

	// ---- cheap "is there anything at all" ------------------------------------------------

	/** kTrue when there is anything the reveal gesture could show: a changed page, an overflow
		page, or a page registered as Added/Removed on either side. The gesture asks this before
		it paints, so that pressing the button over an untouched document does nothing. */
	virtual bool16		HasAnyMarkableContent() = 0;

	// ---- overset (the Find Overset feature, independent of the comparison) ----------------

	/** The Find Overset toggle and the document that was scanned. The database is identity
		only, like the two above. */
	virtual bool16		GetOversetOn() = 0;
	virtual IDataBase*	GetOversetDB() = 0;

	/** kTrue when the last scan found overset on this page. Which document the page belongs to
		is the caller's business -- compare against GetOversetDB() first. */
	virtual bool16		IsOversetPage(UID pageUID) = 0;

	/** How many pages carry overset, for the status line. */
	virtual int32		GetOversetPageCount() = 0;

	/** Every page that carries overset. out is cleared first. Used to repaint exactly those
		thumbnails when the feature is switched off. */
	virtual void		GetOversetPageUIDs(std::vector<UID>& out) = 0;

	/** Every individual overset location, in scan order -- one per "+" rather than one per
		page, because Prev/Next stops at each of them. out is cleared first. */
	virtual void		GetOversetLocations(std::vector<KESCMOversetLoc>& out) = 0;

	// ---- the page flags, read side (2026-08-13, Task 13) ---------------------------------
	//
	// Writing them is IKESCMPageFlagsFacade. Reading them is here, with the rest of the
	// read-only questions, so that "what is registered" has one answer and one place.

	/** Every page of db registered as Added/Removed. ADDS to out -- it does not clear it --
		because both callers merge it into a set they are already filling. */
	virtual void		GetRegisteredPages(IDataBase* db, std::set<UID>& out) = 0;

	// ---- the page pairing ----------------------------------------------------------------
	//
	// Which page of the Source document corresponds to which page of the Target. Registered
	// pages are taken out first, then what is left is matched in order, so inserting or
	// deleting a page shifts the rest without breaking the correspondence.
	//
	// ★The whole table, not one page at a time. Both callers build their own map out of it and
	// keep it (the view sync caches it for a 250 ms generation, because it is asked dozens of
	// times a second while scrolling). Asking page by page across the boundary would turn one
	// call into hundreds.

	/** The normal-spread pairing. Both vectors come back the same length: outTargetPages[i]
		pairs with outSourcePages[i]. Both are cleared first. */
	virtual void		GetPagePairing(IDataBase* targetDB, IDataBase* sourceDB,
								std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages) = 0;

	/** The master-spread pairing, which is matched BY NAME rather than in order -- a master
		that exists on only one side simply has no partner. Same output shape as above. */
	virtual void		GetMasterPagePairing(IDataBase* targetDB, IDataBase* sourceDB,
								std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages) = 0;

	// ---- walking a document's pages (2026-08-14, Task 16) --------------------------------
	//
	// Not comparison results, but reads all the same, and every UI-side caller was reaching
	// KESCMCore.h's free functions for them. A free function cannot be linked from the other
	// .pln, so they come through here with the rest of the reading.

	/** Every page of the document, flattened in spread order and then page order. out is
		cleared first.

		★NORMAL SPREADS ONLY (ISpreadList). Masters are collected separately and on purpose:
		this same walk is what the comparison pairs pages by, so folding masters in would change
		what gets compared rather than just what gets listed. */
	virtual void		GetAllPageUIDs(IDataBase* db, std::vector<UID>& out) = 0;

	/** The master spreads' pages, in master-spread order and then page order.

		★ADDS to out -- it does NOT clear it -- because the callers append masters after the
		normal pages and remember the count in between as the boundary. */
	virtual void		GetMasterPageUIDs(IDataBase* db, std::vector<UID>& out) = 0;

	/** Every page of db that could be carrying a mark right now: the changed ring, the overflow
		"/" and the registered "/". ADDS to outPages, and answers kTrue only when db is one of
		the two documents currently being compared -- otherwise it touches nothing and answers
		kFalse.

		★"What counts as marked" is defined in this one place, so adding a kind of mark keeps
		the pre-comparison save and the thumbnail purge in step without either being edited. */
	virtual bool16		GetMarkablePageUIDs(IDataBase* db, std::set<UID>& outPages) = 0;

	/** The page a page item sits on, or kInvalidUID when it sits on none. Always a real page
		(kPageBoss) -- a spread's UID never comes back from this. */
	virtual UID			GetFramePageUID(IDataBase* db, UID frameUID) = 0;
};

#endif // __IKESCMMarkData_h__
