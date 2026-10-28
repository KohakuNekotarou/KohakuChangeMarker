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
#include "IAdobeMediaMgmtMetaData.h"	// GetDocumentID - the identity of an unsaved document (the external door)
#include "IDataBase.h"

// General includes:
#include "IDThreading.h"			// IDThreading::ThreadLocal - the clone this thread exports
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain
#include "FileUtils.h"				// FileUtils::IsEqual(IDFile,IDFile)
#include "IDFile.h"
#include "PMString.h"				// what GetDocumentID() returns (metadata::URI)

#include <map>
#include <string>

// Project includes:
#include "KCMThreadSafety.h"
#include "KCMExternalSource.h"	// KCMIsExternalSource -- the lent Source matches nothing but its own pointer

//----------------------------------------------------------------------------------------
bool16 KCMIsMainThread()
{
	return IDThreading::IsMainThreadDomain() ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// THE IDENTITY OF AN UNSAVED DOCUMENT, READ THE EXTERNAL WAY (2026-10-29, the author's call "A": KCM goes to Exchange
// and calls nothing marked internal - IDataBase::GetDocumentID is "FOR INTERNAL USE ONLY" and names this door,
// IAdobeMediaMgmtMetaData::GetDocumentID, for external use; the same xmp.did KCMPageExport writes).
//   ⚠The external door INSTANTIATES the document's root (kDocBoss) to ask its metadata, and a thread may touch only an
//    object model of its own (guide vol1-07, rule 3): the main thread its documents, a background thread the clone it
//    exports. So a background thread never reads a main-thread document - what it needs of one comes from the table
//    below, which the main thread fills whenever the same-document question meets an unsaved document there (every
//    screen draw of one, and Start for the pair it arms).
//----------------------------------------------------------------------------------------
namespace
{
	// Main-thread documents' IDs, kept for a background thread. Guarded by KCMMarkStateMutex (recursive - a drawing asks
	// while holding it). std::string, not PMString: nothing of the application's is left for the DLL's unload. An entry
	// goes when its document closes (KCMForgetDocumentId) - an address can come back as another document.
	std::map<IDataBase*, std::string> sMainDocIds;

	// The clone this execution context is exporting (KCMBeginExportOn sets it every time - not only when the marks could
	// be translucent, as tl_ExportingDB is); nil when it exports nothing.
	IDThreading::ThreadLocal<IDataBase*> tl_ExportCloneDB(nil);

	// The xmp.did of a database the calling thread owns; "" when it cannot be read.
	std::string ReadDocumentId(IDataBase* db)
	{
		InterfacePtr<IAdobeMediaMgmtMetaData> metadata(db, db->GetRootUID(), UseDefaultIID());
		return (metadata != nil) ? std::string(metadata->GetDocumentID().GetUTF8String()) : std::string();
	}

	// The ID of db for the same-document question. Kept ones first; the main thread reads (and keeps) its own; a
	// background thread reads only the clone it exports - anything else answers "" (= not the same).
	std::string DocumentIdOf(IDataBase* db)
	{
		{
			boost::recursive_mutex::scoped_lock lock(KCMMarkStateMutex());
			const std::map<IDataBase*, std::string>::const_iterator kept = sMainDocIds.find(db);
			if (kept != sMainDocIds.end())
				return kept->second;
		}
		if (KCMIsMainThread())
		{
			const std::string id = ReadDocumentId(db);
			if (!id.empty())
			{
				boost::recursive_mutex::scoped_lock lock(KCMMarkStateMutex());
				sMainDocIds[db] = id;
			}
			return id;
		}
		return (db == tl_ExportCloneDB.Get()) ? ReadDocumentId(db) : std::string();
	}

	// Main thread: keep the ID of an unsaved document met here, for a background export that will ask for it.
	void KeepIdOfUnsaved(IDataBase* db)
	{
		if (db != nil && db->GetSysFile() == nil && !KCMIsExternalSource(db))
			(void)DocumentIdOf(db);
	}
}

void KCMSetExportCloneOnThisThread(IDataBase* db)
{
	tl_ExportCloneDB.Set(db);
}

void KCMForgetDocumentId(IDataBase* db)
{
	boost::recursive_mutex::scoped_lock lock(KCMMarkStateMutex());
	sMainDocIds.erase(db);
}

//----------------------------------------------------------------------------------------
bool16 KCMIsSameDoc(IDataBase* a, IDataBase* b)
{
	// The main thread keeps the IDs of the unsaved documents it meets - here, ahead of the pointer test that decides
	// most of its calls - for a background export that will ask for them (DocumentIdOf).
	if (KCMIsMainThread())
	{
		KeepIdOfUnsaved(a);
		if (b != a)
			KeepIdOfUnsaved(b);
	}

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
	// "screen and export disagree" shape. A document has its xmp.did even when unsaved, and the
	// BG clone carries the same one as main (measured 2026-08-16 through the internal door; the
	// external one reads the same metadata - see DocumentIdOf for which thread reads what).
	// @warning **a pair where only one side has a file arrives here too** (saved vs unsaved).
	//   Those two have different IDs, so the answer is correctly false. Cutting them off by
	//   "one side has no file" instead would break silently if a BG clone ever failed to return
	//   a GetSysFile (not observed), so the judgement is left to the ID.
	const std::string ida = DocumentIdOf(a);
	const std::string idb = DocumentIdOf(b);
	if (ida.empty() || idb.empty())
		return kFalse;	// two empties are not "the same" (having no name is no evidence of identity)

	return (ida == idb) ? kTrue : kFalse;	// case-sensitive, as the PMString compare was
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
