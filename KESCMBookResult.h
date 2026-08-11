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
	kKESCMChapterFailed			// could not be opened; fWhy says what the book knows about it
};

/** One chapter's outcome. */
struct KESCMChapterResult
{
	PMString			fName;			// what the list shows: the chapter's file name
	IDFile				fTargetFile;	// empty when the chapter exists in the source book only
	IDFile				fSourceFile;	// empty when the chapter exists in the target book only
	KESCMChapterState	fState;
	PMString			fWhy;			// only filled in for kKESCMChapterFailed

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
		case kKESCMChapterAdded:	return "ChapterAdded";
		case kKESCMChapterDeleted:	return "ChapterDeleted";
		case kKESCMChapterFailed:	return "Failed";
		default:					return "Unknown";
	}
}

#endif // __KESCMBookResult_h__

// End, KESCMBookResult.h.
