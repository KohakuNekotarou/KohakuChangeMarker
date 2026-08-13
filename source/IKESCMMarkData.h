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

// Project includes:
#include "KESCMID.h"
#include "KESCMOversetScan.h"	// KESCMOversetLoc (a type only -- types cross the boundary fine)

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
};

#endif // __IKESCMMarkData_h__
