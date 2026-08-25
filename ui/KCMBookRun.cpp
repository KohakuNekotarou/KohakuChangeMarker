//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  Book comparison: the command behind the flyout item. See KCMBookRun.h for why it exists.
//
//  ***** THE CONFIRMATION IS THE POINT OF THIS FILE. ***** Target is whichever book has the FRONT
//  TAB in the Book panel, which is not the same thing as InDesign's active book, and the two drift
//  apart silently. The alert is where the user gets to see the two books before anything is opened
//  or read - and it names them BY FULL PATH, because two versions of the same job keep the same
//  file name and "a.indb / a.indb" identifies nothing (the user asked for paths, 2026-08-12).
//
//  ***** ANSWERING NO COSTS NOTHING. ***** Nothing has been opened at that point: the pairing and
//  the chapter opening all happen after the alert, inside KCMCompareBooks.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IMenuUtils.h"			// InsertAmpersandForDisplay - see ShownPath below

// General includes:
#include "CAlert.h"				// ModalAlert / kOKString / kCancelString / the icon enum
#include "CoreResTypes.h"		// kLineSeparatorString - the platform's own line break
#include "Utils.h"

#include <vector>

// Project includes:
#include "IKCMBookFacade.h"	// ResolveBookPair / GetBookDisplayPath / CompareBooks
#include "KCMBookPanelLookup.h"	// KCMGetPanelBookFile (observing the front tab; a UI-side job)
#include "KCMBookDialog.h"	// KCMBookDialogSetResult / KCMOpenBookDialog
#include "KCMBookResult.h"	// KCMChapterResult
#include "KCMBookRun.h"
#include "KCMPathDisplay.h"	// KCMPathForDisplay - how a path is shown, shared with the panel
#include "KCMUIID.h"
#include "KCMLoc.h"			// Japanese in a Japanese UI, English everywhere else

namespace
{

/** A path as an ALERT can print it.

    ⚠★A LONE '&' IS EATEN. The alert text goes through the same accelerator handling as a menu
    item, so a folder called "Q&A" arrives as "QA" - and a path is exactly the kind of string a user
    would not think to check. Doubling it up is what the product does for user-supplied names that
    reach an alert, and Utils<IMenuUtils>()->InsertAmpersandForDisplay is the API for it
    (IMenuUtils.h:81).

    ★KCM has never needed this before: every other variable string it shows goes into a WIDGET,
    and there the same problem is turned off in the resource instead ("Convert ampersands" = kFalse,
    which is why the dialog lines below are not put through here). This is the plug-in's first
    variable string in an alert - the widget-side switch does not reach it. */
PMString ShownPath(const PMString& path)
{
	// ***** THE WHOLE PATH, SHOWN THE WAY THE PANEL SHOWS ONE. ***** (User, 2026-08-15: "make the
	// paths match the way the panel puts them out".) The panel hands its widget the complete path and
	// lets the widget's kEllipsizeBeginning do the shortening; since the same day the comparison
	// dialog does that too. So no C++ shortening is left anywhere - the rule is "give out the whole
	// path" and one place decides how it is spelled (KCMPathDisplay.h).
	// ★AN ALERT HAS NO WIDGET, so nothing elides this one and the alert grows as wide as the path.
	//   That is the accepted cost of matching: this alert exists to answer "are these the two books
	//   you meant?", and a complete path answers it better than a shortened one.
	// ⚠ Separators are normalised BEFORE the ampersand doubling below, so what gets escaped for
	//   display is the path as it will be read.
	PMString shown(KCMPathForDisplay(path));
	shown.SetTranslatable(kFalse);

	if (Utils<IMenuUtils>().Exists())
		Utils<IMenuUtils>()->InsertAmpersandForDisplay(&shown);

	return shown;
}

}	// anonymous namespace

void KCMRunBookComparison()
{
	IBook* target = nil;
	IBook* source = nil;

	// ★Queried once and held: this function asks the boundary FOUR times (resolve, two paths, the
	//   comparison), and Utils.h:74-80 asks for exactly this shape when an interface is used "in
	//   several places" - one QueryInterface and one Release instead of one of each per call.
	// ⚠ The header sets no number: "several places" is all it says, so where the line falls is a
	//   judgement, not a rule. This note claimed "above three calls" until 2026-08-17 (audit B-U5) -
	//   the same over-reading that B-U3 corrected in three other files a day earlier.
	InterfacePtr<IKCMBookFacade> books(Utils<IKCMBookFacade>().QueryUtilInterface());

	// ***** The same resolver the greying uses. ***** UpdateActionStates calls this too, so the
	// menu's appearance and the result of choosing it cannot disagree. Reaching here with no pair
	// therefore means the front tab changed between the menu being built and the item being chosen.
	// ★★Observing the front tab is **this side's (the UI's) job** (ui/KCMBookPanelLookup.h).
	//   ⚠**The meaning of the branch did not change**: failing to observe and failing to resolve the
	//   pair both fall into the same "two books are not there" warning. Both used to be answered by
	//   the model side's ResolveBookPair returning kFalse.
	IDFile panelBookFile;
	if (!KCMGetPanelBookFile(panelBookFile)
		|| !books->ResolveBookPair(panelBookFile, target, source))
	{
		CAlert::ModalAlert(
			KCMLoc::Text(kKCMBookNoPairKey, KCMJa::kBookNoPair),
			kOKString, kNullString, kNullString,
			1,							// OK is the default button
			CAlert::eWarningIcon);
		return;
	}

	// ★Resolved ONCE, here, and carried from here on. What is confirmed, what is compared and what
	//   the dialog ends up displaying are then the same two strings by construction.
	const PMString targetPath = books->GetBookDisplayPath(target);
	const PMString sourcePath = books->GetBookDisplayPath(source);

	// ⚠kLineSeparatorString rather than "\n": it is "\r" on the Mac (CoreResTypes.h:150/159), and
	//   CAlert's own documentation names this define as the way to break a line (CAlert.h:119-121).
	// ***** ENGLISH IN EVERY UI LANGUAGE. ***** (User: "the alert that says the books are about to
	// be compared - English is fine".) It went through KCMLoc::Text until then, which is
	// the plug-in's "say this one in Japanese on a Japanese UI" helper; taking it out of there puts
	// this alert back with the rest of KCM's UI, which has been English-only in every locale since
	// 2026-08-06. What is left in KCMLoc is the case it was built for: text that ASKS THE USER TO
	// DECIDE something with consequences (How to Use, the Hide Unchanged confirmation).
	// ⚠ The paired warning for "no two books to compare" (kKCMBookNoPairKey, further up) is STILL
	//   Japanese on a Japanese UI - it was not part of the request. Two alerts of one feature now
	//   answer in different languages.
	PMString message(kKCMBookCompareConfirmKey);
	message.Translate();				// from the enUS table, like every other English string here
	message.Append(kLineSeparatorString);
	message.Append(kLineSeparatorString);
	message.Append("target: ");
	message.Append(ShownPath(targetPath));
	message.Append(kLineSeparatorString);
	message.Append("source: ");
	message.Append(ShownPath(sourcePath));
	// ★Assembled text is no longer a string key. Left translatable, it would be looked up in the
	//   built-in table and could be replaced wholesale by whatever happened to match - a fault this
	//   plug-in has actually had ("Source:" came out as a paragraph style name).
	message.SetTranslatable(kFalse);

	// ⚠Windows only renders the standard buttons (CAlert.h:102-114), and OK / Cancel are among
	//   them, so these two arrive as themselves rather than as Yes / No.
	if (CAlert::ModalAlert(message, kOKString, kCancelString, kNullString,
			1,							// OK is the default button
			CAlert::eQuestionIcon) != 1)
		return;						// Cancel: nothing has been opened, so there is nothing to undo

	// ⚠This blocks until the comparison is done. The wait is not silent: KCMCompareBooks raises a
	// progress bar with a Cancel on it, naming the chapter it is working on. A cancelled run comes
	// back with the chapters it never reached marked NotCompared, which is why the result is stored
	// without asking whether the run finished - "not compared" is an answer worth showing.
	std::vector<KCMChapterResult> chapters;
	PMString report;
	books->CompareBooks(target, source, chapters, report);

	// ★Hand the dialog the whole result BEFORE opening it: the tree asks for its rows while it is
	//   being built, so the dialog has to be able to answer from the moment it opens.
	KCMBookDialogSetResult(targetPath, sourcePath, report, chapters);
	KCMOpenBookDialog();
}

// End, KCMBookRun.cpp.
