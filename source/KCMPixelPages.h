//========================================================================================
//
//  KCMPixelPages.h
//
//  THE PIXEL MODE'S ANSWER PAGE BY PAGE, FOR A TEST (app.kcmPixelPages, 2026-10-28 - the full test's review, the
//  author's "A": docs/ai-notes/kcm-full-test-review-2026-10-28.md). A TEST BUILD'S ONLY, like the other doors (KCM_DIAG:
//  its Property block, its line on app and the body below are all inside #ifdef KCM_DIAG; ship_check.py names it).
//
//  WHY IT EXISTS. What the Pixel mode decides is DRAWN - the ring on a changed page, the "/" on a page without a partner,
//  the change column's cells - and nothing outside the plug-in can read a drawing. The status line gives a count
//  ("pages compared=4 changed=1"), and a count proves nothing (app.kcmStoryRows says the same one size up): which page,
//  and that the control page is NOT among them, is the whole question.
//
//  IT ONLY READS, through IKCMMarkData - the one read side of the marks - so what it answers is what the scrollbar map,
//  Prev/Next and the Pages panel are answered: a test of the product, not of a second path to the same data.
//
//========================================================================================

#ifndef __KCMPixelPages_h__
#define __KCMPixelPages_h__

class PMString;

/** The armed pair's pages as text: a summary line, a header line, then one tab-separated line per page -
		side  index  uid  name  master  hidden  changed  cells  total  overflow  registered  partner
	side = T (the Target) or S (the Source); index = its place in the walk (normal spreads' pages first, then the masters',
	IKCMMarkData::GetAllPageUIDs / GetMasterPageUIDs); name = IPageList::GetPageString; master / hidden / changed /
	overflow / registered = 1 or 0; cells / total = IKCMMarkData::GetChangeCells (a Target page's; "-" on the Source side,
	which has no entries of its own - it borrows the Target's); partner = the paired page's uid, or "-".
	"changed" is HasEntryForPage on the Target side and IsSourcePageMarked on the Source side - the two questions the
	header of IKCMMarkData.h keeps apart.
	⚠Nothing armed is a real answer - "pixel: not armed" and the header, no rows - and reads differently from the property
	being absent (ERR:55). An empty string never comes back. */
void KCMDescribePixelPages(PMString& out);

#endif // __KCMPixelPages_h__
