//========================================================================================
//
//  KCMThreadSafety.cpp
//
//  The thread-safety helpers. The reasoning behind them, and the official shapes they follow,
//  are at the top of the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"

// General includes:
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain
#include "FileUtils.h"				// FileUtils::IsEqual(IDFile,IDFile)
#include "IDFile.h"
#include "PMString.h"				// what IDataBase::GetDocumentID() returns (identity of an unsaved document)

// Project includes:
#include "KCMThreadSafety.h"
#include "KCMExternalSource.h"	// KCMIsExternalSource -- the lent Source matches nothing but its own pointer

//----------------------------------------------------------------------------------------
bool16 KCMIsMainThread()
{
	return IDThreading::IsMainThreadDomain() ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
bool16 KCMIsSameDoc(IDataBase* a, IDataBase* b)
{
	// Same pointer = same document (main's ordinary path). Two nils are not "the same".
	if (a == b)
		return (a != nil) ? kTrue : kFalse;
	if (a == nil || b == nil)
		return kFalse;

	// **A lent Source (KCMExternalSource.h) is nothing but its own pointer.** It is a clone of a
	// document that is still open, so by file -- and by document ID -- it would come out "the
	// same" as that document, and every window of the Target would count as a Source window too
	// (the Source marks would land on the Target). It has no window and no export of its own, so
	// there is no second pointer that could legitimately name it. The pointer test above has
	// already answered for the equal case.
	if (KCMIsExternalSource(a) || KCMIsExternalSource(b))
		return kFalse;

	// From here on is the background's path: its clone DB is a different pointer but names the
	// same file as the original. @warning GetSysFile() returns nil for an unsaved document -- it
	// is documented as "Returns nil if there is no file associated yet".
	const IDFile* fa = a->GetSysFile();
	const IDFile* fb = b->GetSysFile();
	if (fa != nil && fb != nil)
		return FileUtils::IsEqual(*fa, *fb);

	// **The second door, for unsaved documents.** This used to return kFalse here, so
	// **comparing two documents that had never been saved produced no marks at all on the
	// background thread** (the asynchronous PDF export) while they appeared on screen -- the
	// "screen and export disagree" shape. GetDocumentID() has a value even when unsaved, and
	// measurement showed the BG clone DB returning the same one as main; the header says why
	// this door was chosen over the external one.
	// @warning **a pair where only one side has a file arrives here too** (saved vs unsaved).
	//   Those two have different IDs, so the answer is correctly false. Cutting them off by
	//   "one side has no file" instead would break silently if a BG clone ever failed to return
	//   a GetSysFile (not observed), so the judgement is left to the ID.
	const PMString ida = a->GetDocumentID();
	const PMString idb = b->GetDocumentID();
	if (ida.IsEmpty() || idb.IsEmpty())
		return kFalse;	// two empties are not "the same" (having no name is no evidence of identity)

	return (ida.Compare(kTrue /*caseSensitive*/, idb) == 0) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// **File-scope static, not a function-local one.** Guide vol1-07 names them: "Remove any
// **function-local static** variables", so this follows the hyphenator and keeps the mutex at
// class/file scope. The mutex is never assigned to after construction, so static
// initialisation is safe for it.
//----------------------------------------------------------------------------------------
static boost::recursive_mutex sKCMMarkStateMutex;

boost::recursive_mutex& KCMMarkStateMutex()
{
	return sKCMMarkStateMutex;
}
