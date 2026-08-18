//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: what a result IS.
//
//  One entry per chapter, and the answer per chapter is a single bit - changed or not. The user
//  asked for exactly that (2026-08-11): "per document, just show changed / not changed", and
//  "once a change is found in a document, that document is done - move to the next one".
//
//  ***** A result holds NO UID and NO UIDRef. ***** Chapters are opened for the comparison and
//  closed again, so a UID would already be dead by the time the list is shown - and worse than
//  dead: (IDataBase*, UID) pairs get reused, so a stale one can MATCH A DIFFERENT DOCUMENT and go
//  on working, silently pointing at the wrong file. What is kept instead is the chapter's file
//  (unique, and enough to reopen it later if row clicks are added) and strings made while the
//  document was still open.
//
//  Story Edits can hold UIDs because its documents stay open. This one cannot. Same plug-in,
//  different premise.
//
//========================================================================================
#ifndef __KESCMBookResult_h__
#define __KESCMBookResult_h__

#include "IDFile.h"
#include "PMString.h"

/** What the comparison concluded about one chapter. */
enum KESCMChapterState
{
	kKESCMChapterUnknown = 0,	// paired, not judged yet - never survives into a finished result
	kKESCMChapterChanged,		// at least one page differs (or the page counts differ)
	kKESCMChapterNoChange,		// every page compared equal
	kKESCMChapterAdded,			// present in the target book only
	kKESCMChapterDeleted,		// present in the source book only
	kKESCMChapterFailed,		// could not be opened; fWhy says what the book knows about it
	kKESCMChapterNotCompared	// the run was cancelled before this chapter was judged (see below)
};

//  ***** WHY kKESCMChapterNotCompared IS NOT THE SAME AS kKESCMChapterUnknown. *****
//  Unknown is the internal "paired, not judged yet" value and never reaches the screen. NotCompared
//  is a FINISHED answer: the user cancelled, and this chapter was never looked at. They have to be
//  separate from NoChange for the reason that cost KBS a day - "could not be processed" and
//  "processed, and nothing had changed" must never share a word. A cancelled run that reported its
//  untouched chapters as NoChange would be claiming they are clean, which is the one thing it does
//  not know. Cancelling mid-chapter also lands here: the pages already compared showed no
//  difference, but the rest were never read, so the chapter as a whole has no answer.

/** One chapter's outcome. */
struct KESCMChapterResult
{
	PMString			fName;			// what the list shows: the chapter's file name
	IDFile				fTargetFile;	// empty when the chapter exists in the source book only
	IDFile				fSourceFile;	// empty when the chapter exists in the target book only
	KESCMChapterState	fState;

	/** WHY this chapter failed - only filled in for kKESCMChapterFailed.

	    ***** IT SAYS THE REASON AND NOT THE VERDICT. ***** Everywhere this string is shown, the
	    word "Failed" is shown beside it: the dialog puts it in the row's state column and
	    app.kcmBookResult puts it in the field before this one. So "could not be opened (missing)"
	    spends the row on saying "Failed" twice, and "missing" says everything the reader did not
	    already have. (User's call, 2026-08-19: "it already says Failed, so the row does not have to
	    say the open failed too".)

	    ⚠ IT IS NOT ONLY WORDINESS - THE ROOM IS REAL, AND IT WAS MEASURED (bug recheck B-U5, third
	    pass). The dialog draws the chapter's file name and this reason in ONE cell, and that cell
	    ellipsizes in the MIDDLE - a choice KCMUI.fr justifies by "both the start and the extension
	    survive", which is true of a cell holding a file name and false as soon as this string is
	    appended to it. A Failed row read

	        ch3.ind...ould not be opened (missing)

	    - naming "ch3.ind", a file that does not exist, while the reason was unreadable at both ends.
	    The cell's boss also answers "no tip" (kKESCMNoTipImpl), so nothing on screen could recover
	    it. ★Anything added here has to fit next to a file name in one row; if a future reason cannot,
	    the answer is a shorter reason, not a wider dialog. */
	PMString			fWhy;

	KESCMChapterResult() : fState(kKESCMChapterUnknown) {}
};

/** The word this state is reported by - in the panel, in the status line, and in the script
    property. English throughout, like the rest of this plug-in's UI. Not translatable: these are
    the result's vocabulary, and a test reads them back verbatim. */
inline const char* KESCMChapterStateText(KESCMChapterState state)
{
	switch (state)
	{
		case kKESCMChapterChanged:	return "Changed";
		case kKESCMChapterNoChange:	return "NoChange";
		case kKESCMChapterAdded:		return "ChapterAdded";
		case kKESCMChapterDeleted:		return "ChapterDeleted";
		case kKESCMChapterFailed:		return "Failed";
		case kKESCMChapterNotCompared:	return "NotCompared";
		default:						return "Unknown";
	}
}

#endif // __KESCMBookResult_h__

// End, KESCMBookResult.h.
