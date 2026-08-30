//========================================================================================
//
//  IKCMBookFacade.h
//
//  Comparing two open books chapter by chapter: which two they are, what to call them, and
//  running the comparison.
//
//  Chapters are opened one PAIR at a time behind the scenes and closed again, so no document is
//  left open and none is left dirty. A chapter's verdict is settled as soon as the first
//  difference is found, and a differing page count settles it without opening a page at all.
//
//  WHAT IS DELIBERATELY NOT HERE, each one decided by grepping its callers:
//
//    - KCMGetBookResultText (app.kcmBookResult). Its only caller is KCMScriptProvider.cpp,
//      which is model-side itself: a ScriptProvider has to answer whether or not a panel exists.
//      On the boundary it would be a method nobody calls.
//    - KCMElidePathFront. Shortening a path so it fits is the view's decision and every caller
//      was UI-side, so it moved to the UI -- and was deleted there once the widget's
//      kEllipsizeBeginning took the job over. Nothing shortens a path by hand any more.
//    - KCMBuildChapterPairing. Model-internal -- only KCMCompareBooks calls it.
//
//========================================================================================

#ifndef __IKCMBookFacade_h__
#define __IKCMBookFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "IDFile.h"		// ResolveBookPair takes the front tab's book file, observed by the UI half
#include "PMString.h"
#include <vector>

// Project includes:
#include "KCMBoundaryID.h"	// IID_IKCMBOOKFACADE. The boundary header rather than KCMID.h,
							// for the reason given at the same spot in IKCMCompareFacade.h.
#include "KCMBookResult.h"	// KCMChapterResult / KCMChapterState -- plain data and one inline
							// function, and no free function declarations at all, so this one
							// crosses the boundary clean.

class IBook;

class IKCMBookFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMBOOKFACADE };

	/** Which two books a comparison would run on: Target is the book in `panelBookFile`, Source is
		the first OTHER open book. kTrue only when both were found; whichever could not be resolved
		is left nil, so a caller can word its message from which is missing.

		THE POINTERS ARE NON-OWNING. They belong to IBookManager -- do not release them.

		WHY THE FILE IS PASSED IN. Answering "which book tab is in FRONT" needs PaletteRefUtils
		(WidgetBin.lib), Utils<IBookUIUtils>() and IPanelMgr, and a model plug-in may reach none of
		them -- so the CALLER observes it, through ui/KCMBookPanelLookup.h's KCMGetPanelBookFile,
		and hands the answer across.
		@warning when that observation fails, DO NOT CALL THIS. Handing in a blank file, or
			substituting the active book, is what a model-side walk cannot get right: IBookManager's
			active book does not follow tab switches.

		Two callers, which is the whole point: the flyout item's grey state and the command behind
		it both ask this, so what the menu promises and what choosing it does cannot drift apart.
		(They can still differ in time -- the front tab may change between the menu being built and
		the item being chosen -- which is why the command re-asks rather than trusting the grey.) */
	virtual bool16		ResolveBookPair(const IDFile& panelBookFile,
									IBook*& outTarget, IBook*& outSource) = 0;

	/** The book's FULL PATH as the file system spells it, falling back to its title when it has no
		file yet, so a caller never has to handle an empty string.

		Full path, not the name. Two books being compared are usually two versions of one job and
		the job keeps its file name across versions, so "a.indb" and "a.indb" would identify the
		pair as one book twice over -- the one thing the Target/Source lines exist to rule out.

		NOBODY SHORTENS IT. The whole path goes into the widget and kEllipsizeBeginning does the
		cutting, so all a caller decides is how the separators are spelled (ui/KCMPathDisplay.h).
		The one place with no widget, the confirmation alert, shows the path in full on purpose. */
	virtual PMString	GetBookDisplayPath(IBook* book) = 0;

	/** Run the comparison. Raises a progress bar naming the chapter being examined, and that bar
		carries the Cancel. Blocks until it is done or cancelled.

		CANCELLING KEEPS THE VERDICTS ALREADY REACHED. Chapters that were never examined come back
		as kKCMChapterNotCompared, and that must not be folded into NoChange: reporting an
		unexamined chapter as unchanged would claim something was checked that was not.

		@param outChapters one entry per chapter pair, cleared first.
		@param outReport the one-line summary. It always states how many chapters were looked at, so
			"nothing listed" can never be read as "nothing could be opened".
		@return kSuccess, ALWAYS - and the only caller ignores it on purpose. A chapter that could
			not be opened or compared is not a failure of the run: it comes back as a VERDICT on that
			chapter (Failed / NotCompared, with a reason), which is the whole point of the result
			list, and a cancelled run is a normal ending too. The ErrorCode is here because that is
			the shape a facade method takes (the gs-04 guide's rule that a facade method which
			writes data returns one - stated by what it says, not by its number, which was cited
			wrongly here until 2026-08-30), not because there is a failure to report.
			@warning that rule is about METHODS THAT WRITE DATA, and this one writes none: it opens
			each chapter pair, reads them and closes them again. Keeping the ErrorCode is a choice
			to look like the rest of the boundary, not a requirement - which is why the caller may
			ignore it without a second thought.
			@warning anything that ever DOES return a failure from here has to change KCMBookRun.cpp,
			which today stores and shows the result without asking. */
	virtual ErrorCode	CompareBooks(IBook* target, IBook* source,
								std::vector<KCMChapterResult>& outChapters,
								PMString& outReport) = 0;
};

#endif // __IKCMBookFacade_h__

// End, IKCMBookFacade.h.
