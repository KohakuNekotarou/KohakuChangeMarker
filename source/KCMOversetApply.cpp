//========================================================================================
//
//  KCMOversetApply.cpp
//
//  Applying an overset scan (see KCMOversetApply.h): the scan's results -- the locations and the
//  set of pages -- go into the engine's state, and everything that displays them is brought up to
//  date (the Pages panel thumbnails, the scrollbar map, the Prev/Next cycle).
//
//  MODEL side. How the results are displayed is decided by the UI, on the notification at the end
//  of the function; nothing here calls into the UI.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// The object model:
#include "ISession.h"				// GetExecutionContextSession (the liveness check before scanning)
#include "IApplication.h"
#include "IDocumentList.h"			// FindDocByDataBase, so a closed database is never dereferenced
#include <vector>
#include <set>

// KCM's own headers:
#include "KCMOversetApply.h"
#include "KCMCore.h"				// KCMActiveDocDB / KCMInvalidateDB
#include "KCMDrawEventHandler.h"	// sOversetOn / sOversetDB / sOversetPages / sOversetLocs / sDB
#include "KCMOversetScan.h"		// KCMCollectOversetLocations / KCMOversetLoc (the detection itself)
#include "KCMID.h"				// kKCMOversetRescannedMessage
#include "KCMModelNotify.h"		// the model tells the UI, it never calls it
// The UI's KCMThumbnailRefresh.h, KCMScrollMap.h and KCMChangeNav.h are deliberately absent: how
//   the scan's results are shown is decided by the UI, on the notification.

/* KCMApplyOversetForDoc (declared in KCMOversetApply.h) -- scan db and apply the result. The four
   callers are enumerated in the header. When the document differs from the last scan, the previous
   document's marks are cleared as well. The status line is left to the caller, which words it for
   its own context. */
void KCMApplyOversetForDoc(IDataBase* db)
{
	if (db == nil)
		return;

	// The last line of defence. Most callers hand over a database they have just resolved and know
	//   to be alive; **Stop is the one that hands over the saved sOversetDB**. Should a close
	//   responder ever fail to fire, that pointer could be a closed document, and the scan below
	//   would dereference a released IDataBase -- so liveness is checked once, here. The check
	//   itself only compares pointers through FindDocByDataBase and dereferences nothing, which is
	//   KCM's rule everywhere. A dead one simply returns.
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil || docList->FindDocByDataBase(db) == nil)
		return;

	// Remember what the last scan looked at: moving to another document means clearing that one's
	// marks as well.
	IDataBase* prevDB = KCMDrawEventHandler::sOversetDB;

	// Scan db for the overset locations (the "+" points) and the pages they are on.
	std::vector<KCMOversetLoc> locs;
	KCMCollectOversetLocations(db, locs);
	std::set<UID> pages;
	for (size_t i = 0; i < locs.size(); ++i)
		pages.insert(locs[i].pageUID);

	KCMDrawEventHandler::sOversetOn = kTrue;
	KCMDrawEventHandler::sOversetDB = db;
	KCMDrawEventHandler::sOversetPages.swap(pages);
	KCMDrawEventHandler::sOversetLocs.swap(locs);

	// Did the scan move to a different document? If so the traversal's anchor goes too: **documents
	//   built the same way share their UIDs** (see KCMChangeNav.h), so carrying the previous
	//   document's anchor over means a coincidental UID match can start the cycle partway through.
	//   Moving between documents only happens while nothing is armed, so the comparison's own cycle
	//   is unaffected.
	//   @warning **a rescan of the same document must not drop it** -- the cycle would jump back to
	//   the start every time one overflow is fixed.
	const bool16 movedToAnotherDoc = (prevDB != nil && prevDB != db);

	// Invalidating the layout views is the model's job: this is the side that holds the drawing data.
	KCMInvalidateDB(db);
	if (movedToAnotherDoc)
		KCMInvalidateDB(prevDB);

	// Everything from here on -- injecting and redrawing the Pages panel thumbnails and the
	//   scrollbar map, and what Prev/Next now cycles through -- **belongs to the UI**, so it all
	//   travels on one notification. docA is what was just scanned; docB is the previously scanned
	//   document, and only when the scan moved between documents (nil otherwise, so the UI rebuilds
	//   one document's worth).
	// **The set of pages whose "+" may have changed travels with it**, which is what lets the UI
	//   purge per UID. There are only two cases: (1) the same document rescanned, where the set is
	//   **the new pages together with the old** -- a page that lost its "+" is not in the new set,
	//   so without the old one a stale "+" would stay; (2) the scan moved, where docA gets the new
	//   set and docB **the old set itself**, every "+" over there being gone.
	//   The old set is still in `pages`: the swap above put it there.
	std::set<UID> affectedA(KCMDrawEventHandler::sOversetPages);	// the new set, after the swap
	std::set<UID> affectedB;										// filled only when the scan moved
	if (movedToAnotherDoc)
		affectedB.swap(pages);										// the old set = the previous document's pages
	else
		affectedA.insert(pages.begin(), pages.end());				// same document = new together with old
	KCMNotifyDocsPages(kKCMOversetRescannedMessage,
	                     db, affectedA,
	                     movedToAnotherDoc ? prevDB : nil, affectedB,
	                     movedToAnotherDoc /*navReset*/);
}

// While a comparison is running the overset scan always looks at the comparison Target (sDB), so
// that the changes and the overset can be cycled through with one Prev/Next -- it has to agree
// with the navigation's own document. With no comparison running, the active document.
IDataBase* KCMOversetScanTargetDB()
{
	if (KCMDrawEventHandler::sDB != nil)
		return KCMDrawEventHandler::sDB;	// the same as KCMNavDoc()'s armed branch
	return KCMActiveDocDB();
}

// End of KCMOversetApply.cpp
