//========================================================================================
//
//  KCMBookResult.h
//
//  Book comparison: what a result IS.
//
//  One entry per chapter, and the answer per chapter is a single bit -- changed or not. That is
//  what the user asked for: "per document, just show changed / not changed", and "once a change
//  is found in a document, that document is done -- move to the next one".
//
//  **A result holds NO UID and NO UIDRef.** Chapters are opened for the comparison and closed
//  again, so a UID would already be dead by the time the list is shown -- and worse than dead:
//  (IDataBase*, UID) pairs get reused, so a stale one can MATCH A DIFFERENT DOCUMENT and go on
//  working, silently pointing at the wrong file. What is kept instead is the chapter's file
//  (unique, and enough to reopen it later if row clicks are added) and strings made while the
//  document was still open.
//
//  Story Edits can hold UIDs because its documents stay open. This one cannot. Same plug-in,
//  different premise.
//
//========================================================================================
#ifndef __KCMBookResult_h__
#define __KCMBookResult_h__

#include "IDFile.h"
#include "PMString.h"

#include "KCMBookModeNames.h"	// KCMBookCompareMode - what the two mode fields below hold, and
								// KCMBookModesString, which spells them. ★A header with no SDK type
								// in it, so both halves of the plug-in may include it and it can be
								// checked outside InDesign (work/kcm-bookmodes-test).

/** What the comparison concluded about one chapter. */
enum KCMChapterState
{
	kKCMChapterUnknown = 0,	// paired, not judged yet - never survives into a finished result
	kKCMChapterChanged,		// at least one MODE found a difference (2026-09-10: three modes, not
								// only the pages - fChangedModes names which ones)
	kKCMChapterNoChange,		// every mode ran and none of them found anything
	kKCMChapterAdded,			// present in the target book only
	kKCMChapterDeleted,		// present in the source book only
	kKCMChapterFailed,		// nothing changed, and at least one mode could not be judged: the
								// chapter would not open, or it opened and a mode failed on it
								// (2026-09-10 - it meant "could not be opened" alone until then).
								// fWhy says which, for the first one
	kKCMChapterNotCompared	// the run was cancelled before this chapter was judged (see below)
};

//  **WHY kKCMChapterNotCompared IS NOT THE SAME AS kKCMChapterUnknown.**
//  Unknown is the internal "paired, not judged yet" value and never reaches the screen.
//  NotCompared is a FINISHED answer: the user cancelled, and this chapter was never looked at.
//  They have to be separate from NoChange for the reason that cost KBS a day -- "could not be
//  processed" and "processed, and nothing had changed" must never share a word. A cancelled run
//  that reported its untouched chapters as NoChange would be claiming they are clean, which is
//  the one thing it does not know. Cancelling mid-chapter also lands here: the pages already
//  compared showed no difference, but the rest were never read, so the chapter as a whole has
//  no answer.

/** One chapter's outcome. */
struct KCMChapterResult
{
	PMString			fName;			// what the list shows: the chapter's file name
	IDFile				fTargetFile;	// empty when the chapter exists in the source book only
	IDFile				fSourceFile;	// empty when the chapter exists in the target book only
	KCMChapterState	fState;

	/** WHY this chapter has no clean answer: the reason of the FIRST mode that could not be judged,
	    or, for a chapter that never opened, the word the book itself gave for that.

	    ⚠**NOT ONLY ON Failed ROWS, since 2026-09-10.** A chapter can be Changed - one mode found a
	    difference - while another mode failed on it, and that row carries this string too. What is
	    NOT here is a cancel: a mode the reader stopped is not filed at all (KCMBookCompare.cpp).
	    ⚠The DIALOG still shows it on Failed rows only, because it has to share one cell with the
	    file name (see the warning below); app.kcmBookResult carries it on every row that has one.

	    **IT SAYS THE REASON AND NOT THE VERDICT.** Everywhere this string is shown, the word
	    "Failed" is shown beside it: the dialog puts it in the row's state column and
	    app.kcmBookResult puts it in the field before this one. So "could not be opened (missing)"
	    spends the row on saying "Failed" twice, and "missing" says everything the reader did not
	    already have. (The user's call: "it already says Failed, so the row does not have to say
	    the open failed too".)

	    @warning **this is not only wordiness -- the room is real, and it was measured.** The
	    dialog draws the chapter's file name and this reason in ONE cell, and that cell ellipsizes
	    in the MIDDLE -- a choice KCMUI.fr justifies by "both the start and the extension survive",
	    which is true of a cell holding a file name and false as soon as this string is appended to
	    it. A Failed row read

		        ch3.ind...ould not be opened (missing)

	    -- naming "ch3.ind", a file that does not exist, while the reason was unreadable at both
	    ends. The cell's boss also answers "no tip" (kKCMNoTipImpl), so nothing on screen could
	    recover it. **Anything added here has to fit next to a file name in one row; if a future
	    reason cannot, the answer is a shorter reason, not a wider dialog.** */
	PMString			fWhy;

	/** WHICH of the three comparisons found a difference -- an OR of KCMBookCompareMode.

	    ★**fState IS DERIVED FROM THIS, not stored alongside it.** Changed when this is non-zero;
	    NotCompared when it is zero and the run was cancelled; Failed when it is zero and
	    fUnjudgedModes is not; NoChange when both are zero. Two fields answering "did this chapter
	    change?" would be one question in two places, which is the shape behind seven of this
	    project's real bugs.

	    ⚠**Zero for a chapter no comparison was run on** -- Added, Deleted and NotCompared. Their
	    Change column is empty, which is the right answer and not a gap: nothing was looked at. It
	    is the same rule the pairing already keeps for those chapters (having no counterpart IS the
	    answer, and must not be restated as a failure to do something that was never going to be
	    done). ★A cancel files nothing either, for that same reason (KCMBookCompare.cpp), so a
	    cancelled chapter's column is empty unless a mode had ALREADY failed on it before the
	    cancel arrived -- in which case the '?' is reporting that failure and not the cancel. */
	uint32			fChangedModes;

	/** WHICH comparisons could not be judged -- a page that could not be rasterised, an export that
	    failed. Named in the Change column with a '?' after them.

	    ⚠**NEVER FOLDED INTO "no change".** A mode nobody could judge and a mode that ran and found
	    nothing are different answers, and the note above on kKCMChapterNotCompared is entirely
	    about not letting those two share a word. This is that same rule one level down, at the
	    mode rather than at the chapter.

	    ⚠fWhy holds the reason for the FIRST mode that landed here and no more: it is shown in one
	    cell beside a file name, with about 38 characters to live in (see fWhy). Which modes failed
	    is not lost by that -- the Change column names every one of them. */
	uint32			fUnjudgedModes;

	KCMChapterResult() : fState(kKCMChapterUnknown), fChangedModes(0), fUnjudgedModes(0) {}
};

/** The word this state is reported by. TWO READERS, and NEITHER IS THE PANEL: the chapter
    list's state column (KCMBookTreeWidgetMgr) and app.kcmBookResult (this file's sibling,
    KCMBookCompare's gBookResultText).

        @warning this said "in the panel, in the status line, and in the script property"
        until 2026-09-08, and the first two were never true. The panel's status line is not
        written by the book comparison at all, and the DIALOG's summary line counts an
        unchanged chapter with the word "unchanged" - not with this one. Measured that day:
        one run showed Changed and ChapterDeleted in the list while app.kcmBookResult carried
        NoChange for the same run, which is the only place that word is ever spelt.

    English throughout, like the rest of this plug-in's UI. Not translatable: these
    are the result's vocabulary, and the live-test scripts read them back verbatim out of
    app.kcmBookResult (work/kescm-selftest/task9/s1-book.jsx, work/kescm-booktest/r3-verify.ps1),
    so a translated word would read as an unknown state rather than as a translation. */
inline const char* KCMChapterStateText(KCMChapterState state)
{
	switch (state)
	{
		case kKCMChapterChanged:	return "Changed";
		case kKCMChapterNoChange:	return "NoChange";
		case kKCMChapterAdded:		return "ChapterAdded";
		case kKCMChapterDeleted:		return "ChapterDeleted";
		case kKCMChapterFailed:		return "Failed";
		case kKCMChapterNotCompared:	return "NotCompared";
		default:						return "Unknown";
	}
}

#endif // __KCMBookResult_h__

// End, KCMBookResult.h.
