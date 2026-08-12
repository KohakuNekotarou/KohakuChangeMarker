//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: the command behind the flyout item. See KESCMBookRun.h for why it exists.
//
//  ***** THE CONFIRMATION IS THE POINT OF THIS FILE. ***** Target is whichever book has the FRONT
//  TAB in the Book panel, which is not the same thing as InDesign's active book, and the two drift
//  apart silently. The alert is where the user gets to see the two books before anything is opened
//  or read - and it names them BY FULL PATH, because two versions of the same job keep the same
//  file name and "a.indb / a.indb" identifies nothing (the user asked for paths, 2026-08-12).
//
//  ***** ANSWERING NO COSTS NOTHING. ***** Nothing has been opened at that point: the pairing and
//  the chapter opening all happen after the alert, inside KESCMCompareBooks.
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
#include "KESCMBookCompare.h"	// KESCMCompareBooks
#include "KESCMBookDialog.h"	// KESCMBookDialogSetResult / KESCMOpenBookDialog
#include "KESCMBookPair.h"		// KESCMResolveBookPair / KESCMBookDisplayPath
#include "KESCMBookResult.h"	// KESCMChapterResult
#include "KESCMBookRun.h"
#include "KESCMID.h"
#include "KESCMLoc.h"			// Japanese in a Japanese UI, English everywhere else

namespace
{

/** A path as an ALERT can print it.

    ⚠★A LONE '&' IS EATEN. The alert text goes through the same accelerator handling as a menu
    item, so a folder called "Q&A" arrives as "QA" - and a path is exactly the kind of string a user
    would not think to check. Doubling it up is what the product does for user-supplied names that
    reach an alert, and Utils<IMenuUtils>()->InsertAmpersandForDisplay is the API for it
    (IMenuUtils.h:81).

    ★KESCM has never needed this before: every other variable string it shows goes into a WIDGET,
    and there the same problem is turned off in the resource instead ("Convert ampersands" = kFalse,
    which is why the dialog lines below are not put through here). This is the plug-in's first
    variable string in an alert - the widget-side switch does not reach it. */
PMString ShownPath(const PMString& path)
{
	PMString shown(path);
	shown.SetTranslatable(kFalse);

	if (Utils<IMenuUtils>().Exists())
		Utils<IMenuUtils>()->InsertAmpersandForDisplay(&shown);

	return shown;
}

}	// anonymous namespace

void KESCMRunBookComparison()
{
	IBook* target = nil;
	IBook* source = nil;

	// ***** The same resolver the greying uses. ***** UpdateActionStates calls this too, so the
	// menu's appearance and the result of choosing it cannot disagree. Reaching here with no pair
	// therefore means the front tab changed between the menu being built and the item being chosen.
	if (!KESCMResolveBookPair(target, source))
	{
		CAlert::ModalAlert(
			KESCMLoc::Text(kKESCMBookNoPairKey, KESCMJa::kBookNoPair),
			kOKString, kNullString, kNullString,
			1,							// OK is the default button
			CAlert::eWarningIcon);
		return;
	}

	// ★Resolved ONCE, here, and carried from here on. What is confirmed, what is compared and what
	//   the dialog ends up displaying are then the same two strings by construction.
	const PMString targetPath = KESCMBookDisplayPath(target);
	const PMString sourcePath = KESCMBookDisplayPath(source);

	// ⚠kLineSeparatorString rather than "\n": it is "\r" on the Mac (CoreResTypes.h:150/159), and
	//   CAlert's own documentation names this define as the way to break a line (CAlert.h:119-121).
	PMString message = KESCMLoc::Text(kKESCMBookCompareConfirmKey, KESCMJa::kBookCompareConfirm);
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

	// ⚠This blocks until the comparison is done. The wait is not silent: KESCMCompareBooks raises a
	// progress bar with a Cancel on it, naming the chapter it is working on. A cancelled run comes
	// back with the chapters it never reached marked NotCompared, which is why the result is stored
	// without asking whether the run finished - "not compared" is an answer worth showing.
	std::vector<KESCMChapterResult> chapters;
	PMString report;
	KESCMCompareBooks(target, source, chapters, report);

	// ★Hand the dialog the whole result BEFORE opening it: the tree asks for its rows while it is
	//   being built, so the dialog has to be able to answer from the moment it opens.
	KESCMBookDialogSetResult(targetPath, sourcePath, report, chapters);
	KESCMOpenBookDialog();
}

// End, KESCMBookRun.cpp.
