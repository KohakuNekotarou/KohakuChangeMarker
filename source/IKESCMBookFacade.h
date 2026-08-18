//========================================================================================
//
//  IKESCMBookFacade.h
//
//  Comparing two open books chapter by chapter: which two they are, what to call them, and
//  running the comparison.
//
//  Created 2026-08-14 for the model/UI split (Stage 1), Task 15 -- the fifth and last boundary.
//
//  Chapters are opened one PAIR at a time behind the scenes and closed again, so no document is
//  left open and none is left dirty. A chapter's verdict is settled as soon as the first
//  difference is found, and a differing page count settles it without opening a page at all.
//
//  ★WHAT IS DELIBERATELY NOT HERE, and why -- each one was decided by grepping its callers:
//
//    - KESCMGetBookResultText (app.kcmBookResult). Its only caller is KESCMScriptProvider.cpp,
//      which is model-side itself: a ScriptProvider has to answer whether or not a panel exists.
//      On the boundary it would be a method nobody calls. (The plan's draft had it as
//      GetLastResultText -- the same finding as Task 14's Rebuild, one task earlier.)
//    - KESCMElidePathFront. Shortening a path so it fits is the view's decision, and all three
//      callers were UI-side, so it moved to KESCMBookDialog.h in this same task instead of
//      crossing the boundary.
//    - KESCMBuildChapterPairing. Model-internal -- only KESCMCompareBooks calls it.
//
//  ✅CLOSED IN STAGE 2 (Task 9B, 2026-08-15). This header carried a "KNOWN, AND LEFT TO STAGE 2"
//  paragraph here saying that ResolveBookPair reaches into the UI itself to ask which book tab is
//  in FRONT. It does not any more: the UI observes the front tab
//  (ui/KESCMBookPanelLookup.h's KESCMGetPanelBookFile) and hands the file in, which is what the
//  `panelBookFile` parameter below is for -- and the note two dozen lines down has said so since
//  the day it was done. ⚠The two paragraphs contradicted each other in one file for three days
//  (found 2026-08-18, bug recheck B8), and the stale one pointed at KESCMBookPair.cpp:186-249,
//  where nothing of the sort has lived since the walk moved out. The prediction it ended on was
//  right, and is why the debt had to be paid: IBookUIUtils, IPanelMgr and PaletteRefUtils are all
//  out of a model plug-in's reach.
//
//========================================================================================

#ifndef __IKESCMBookFacade_h__
#define __IKESCMBookFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "IDFile.h"		// ResolveBookPair takes the front tab's book file, observed by the UI half
#include "PMString.h"
#include <vector>

// Project includes:
#include "KESCMBoundaryID.h"	// IID_IKESCMBOOKFACADE。★2026-08-17 に KESCMID.h から絞った
								// (理由は IKESCMCompareFacade.h の同じ位置)
#include "KESCMBookResult.h"	// KESCMChapterResult / KESCMChapterState -- plain data and one
								// inline function, so this header crosses the boundary fine.
								// ★2026-08-17 に実測して確認＝free function の宣言は **0 本**で、
								// 申告どおりなのは境界が借りる4本の型ヘッダーのうちこれだけだった

class IBook;

class IKESCMBookFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKESCMBOOKFACADE };

	/** Which two books a comparison would run on: Target is the book in `panelBookFile`, Source is
		the first OTHER open book. kTrue only when both were found; whichever could not be resolved
		is left nil, so a caller can word its message from which is missing.

		★THE POINTERS ARE NON-OWNING. They belong to IBookManager -- do not release them.

		★★2026-08-15 (Stage 2, Task 9B): `panelBookFile` IS NEW, and it is what closed the debt
		this header used to describe as "KNOWN, AND LEFT TO STAGE 2". Answering "which tab is in
		FRONT" needs PaletteRefUtils (WidgetBin.lib), Utils<IBookUIUtils>() and IPanelMgr, and a
		model plug-in may reach none of them -- so the CALLER observes it, through
		ui/KESCMBookPanelLookup.h's KESCMGetPanelBookFile, and hands the answer across.
		⚠When that observation fails, DO NOT CALL THIS. Handing in a blank file, or substituting
		  the active book, is precisely what the old model-side walk refused to do (IBookManager's
		  active book does not follow tab switches).

		★Two callers, which is the whole point: the flyout item's grey state and the command behind
		it both ask this, so what the menu promises and what choosing it does cannot drift apart.
		(They can still differ in time -- the front tab may change between the menu being built and
		the item being chosen -- which is why the command re-asks rather than trusting the grey.) */
	virtual bool16		ResolveBookPair(const IDFile& panelBookFile,
									IBook*& outTarget, IBook*& outSource) = 0;

	/** The book's FULL PATH as the file system spells it, falling back to its title when it has no
		file yet, so a caller never has to handle an empty string.

		★Full path, not the name. Two books being compared are usually two versions of one job and
		the job keeps its file name across versions, so "a.indb" and "a.indb" would identify the
		pair as one book twice over -- the one thing the Target/Source lines exist to rule out.

		⚠NOBODY SHORTENS IT ANY MORE. This said "shortening it to fit is the caller's business
		(KESCMBookDialog.h's KESCMElidePathFront)" until 2026-08-18 (bug recheck B-U5), naming a
		function that no longer exists: it moved to the UI on 2026-08-14 (the note at the head of
		this file) and was DELETED there the next day. The whole path now goes into the widget and
		kEllipsizeBeginning does the shortening, so all a caller decides is how the separators are
		spelled (ui/KESCMPathDisplay.h) -- and the one place with no widget, the confirmation
		alert, shows the path in full on purpose. */
	virtual PMString	GetBookDisplayPath(IBook* book) = 0;

	/** Run the comparison. Raises a progress bar naming the chapter being examined, and that bar
		carries the Cancel. Blocks until it is done or cancelled.

		★CANCELLING KEEPS THE VERDICTS ALREADY REACHED. Chapters that were never examined come back
		as kKESCMChapterNotCompared, and that must not be folded into NoChange: reporting an
		unexamined chapter as unchanged would claim something was checked that was not.

		@param outChapters one entry per chapter pair, cleared first.
		@param outReport the one-line summary. It always states how many chapters were looked at, so
			"nothing listed" can never be read as "nothing could be opened". */
	virtual ErrorCode	CompareBooks(IBook* target, IBook* source,
								std::vector<KESCMChapterResult>& outChapters,
								PMString& outReport) = 0;
};

#endif // __IKESCMBookFacade_h__

// End, IKESCMBookFacade.h.
