//========================================================================================
//
//  KCMOrigin.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "IPageList.h"
#include "IPMStream.h"			// the file the raw XML is saved to (KCMOriginSaveRaw)
#include "ISession.h"
#include "ISpreadList.h"
#include "IStoryList.h"
#include "ITextModel.h"

// General includes:
#include "PersistUtils.h"
#include "K2SmartPtr.h"
#include "IDFile.h"				// the file KCMOriginSaveRaw is handed
#include "StreamUtil.h"			// CreateFileStreamWrite

#include <time.h>
#include <stdio.h>
#include <new>
// FileUtils.h, <string> and <shlobj.h> went on 2026-09-14 with the Desktop path and the generated
// file name: the caller names the file now, so nothing here asks the shell for a folder, joins a
// path or walks a UTF-8 string any more.

// Project includes:
#include "KCMOrigin.h"
#include "KCMCore.h"				// KCMActiveDoc / KCMIsArmed / KCMArmedTargetDB
#include "KCMComparisonRun.h"		// KCMChooseOriginPair
#include "KCMExternalSource.h"		// KCMIsDbAlive
#include "KCMOriginPeek.h"			// KCMOriginPeekDrop / KCMOriginPeekDescribe
#include "KCMRehydrate.h"			// KCMRehydrateRaw - the test instrument's import
#include "KCMResourceBytes.h"
#include "KCMResourceSnapshot.h"	// KCMTakeResourceSnapshot - the export, as the Resources mode does it

namespace
{

K2::scoped_ptr<KCMResourceBytes>	sBytes;
KCMOriginShape						sShape;
std::vector<KCMStoryStamp>			sStamps;			// the stories' counters at Task Start, in the document's own uids
IDataBase*							sDocDB = nil;		// compared, never dereferenced without KCMIsDbAlive
PMString							sDocName;
PMString							sTakenAt;			// "12:34:56"

/** The session's document list, or nil during the shutdown sequence. */
IDocumentList* QueryDocList(InterfacePtr<IDocumentList>& holder)
{
	ISession* const session = GetExecutionContextSession();
	holder = InterfacePtr<IDocumentList>(session != nil ? session->QueryDocumentList() : nil);
	return holder.get();
}

void Now(PMString& out)
{
	time_t t = ::time(nil);
	struct tm local;
	::localtime_s(&local, &t);
	char buf[16];
	::sprintf_s(buf, sizeof(buf), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
	out = buf;
	out.SetTranslatable(kFalse);
}

}	// namespace

void KCMMeasureShape(IDataBase* db, KCMOriginShape& out)
{
	out = KCMOriginShape();
	if (db == nil)
		return;
	InterfacePtr<ISpreadList> spreads(db, db->GetRootUID(), UseDefaultIID());
	if (spreads != nil)
		out.fSpreads = spreads->GetSpreadCount();
	InterfacePtr<IPageList> pages(db, db->GetRootUID(), UseDefaultIID());
	if (pages != nil)
		out.fPages = pages->GetPageCount();
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories != nil)
	{
		out.fStories = stories->GetUserAccessibleStoryCount();
		for (int32 i = 0; i < out.fStories; ++i)
		{
			InterfacePtr<ITextModel> model(stories->GetNthUserAccessibleStoryUID(i), UseDefaultIID());
			if (model != nil)
				out.fTextLen += model->TotalLength();
		}
	}
}

bool16 KCMCanTakeTaskStart()
{
	if (sBytes.get() != nil)
		return kFalse;					// one slot
	if (KCMIsArmed() && KCMArmedTargetDB() != nil)
		return kFalse;					// not under a running comparison (as Set as Target / Source)
	return (KCMActiveDoc() != nil) ? kTrue : kFalse;
}

bool16 KCMTakeTaskStart(PMString& whyNot)
{
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (!KCMCanTakeTaskStart())
	{
		whyNot = (sBytes.get() != nil) ? "an origin is already held - Clear Target and Source first"
									   : "Task Start needs an active document and no running comparison";
		return kFalse;
	}
	IDocument* const doc = KCMActiveDoc();
	K2::scoped_ptr<KCMResourceBytes> bytes(new (std::nothrow) KCMResourceBytes());
	if (bytes.get() == nil)
	{
		whyNot = "out of memory";
		return kFalse;
	}
	if (!KCMTakeResourceSnapshot(doc, *bytes, whyNot))
		return kFalse;					// whyNot names the step

	IDataBase* const db = ::GetDataBase(doc);
	KCMMeasureShape(db, sShape);
	// The stories' change counters AS THEY STAND NOW, in the document's own uids. A rehydrated
	// copy is freshly imported and its counters say nothing; these are what the Story mode pairs
	// against (KCMRebuildStoryEdits), exactly as it would against a saved older version.
	KCMStoryEdits::CollectStamps(db, sStamps);
	sBytes.reset(bytes.release());		// K2::scoped_ptr has no swap; ownership moves here
	sDocDB = db;
	doc->GetName(sDocName);
	sDocName.SetTranslatable(kFalse);
	Now(sTakenAt);

	KCMChooseOriginPair(db);			// Target = this document, Source = the origin (KCMComparisonRun.cpp)
	return kTrue;
}

bool16 KCMHasOrigin()					{ return (sBytes.get() != nil) ? kTrue : kFalse; }

int32 KCMOriginSaveRaw(const IDFile& file, PMString& whyNot)
{
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (sBytes.get() == nil)
	{
		whyNot = "no Task Start origin is held";
		return 1;
	}

	// ⚠**THE CALLER NAMES THE FILE** (2026-09-14). What stood here - the Desktop asked of the shell
	// (FileUtils::CoverSHGetFolderPath with CSIDL_DESKTOPDIRECTORY) and a name built as
	// "<document name>.TaskStart-HHMMSS.xml" - was the flyout item "Save Task Start XML to Desktop"
	// speaking, and that item went with five of its neighbours. The script method that took its
	// place is handed a path, so a place and a name chosen in here could only override the caller's.
	// ★A script that wants the old name can still build it: app.kcmOriginStatus reports both the
	//   document the origin was taken from and the time it was taken.
	// ⚠That is also why this function is no longer Windows-only. Nothing in it asks the shell
	//   anything now, and the stream below is the same one the TSV export uses on either platform.

	// The same three steps as the TSV export (KCMChangedPagesTSV.cpp): write, Flush, THEN read
	// the state - XferByte may only reach the buffer, so a failed write can surface at the Flush.
	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(file, kOpenOut | kOpenTrunc, 'TEXT', 'CWIE'));
	if (stream == nil)
	{
		whyNot = "the file could not be created";
		return 2;
	}
	stream->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(sBytes->Bytes())), static_cast<int32>(sBytes->Size()));
	stream->Flush();
	const bool16 failed = (stream->GetStreamState() == kStreamStateFailure) ? kTrue : kFalse;
	stream->Close();
	if (failed)
	{
		whyNot = "the file could not be written";
		return 3;
	}
	return 0;
}

IDataBase* KCMOriginDocDB()
{
	if (sBytes.get() == nil || sDocDB == nil)
		return nil;
	InterfacePtr<IDocumentList> holder;
	IDocumentList* const docList = QueryDocList(holder);
	return (docList != nil && KCMIsDbAlive(docList, sDocDB)) ? sDocDB : nil;
}

const KCMResourceBytes*	KCMOriginBytes()	{ return sBytes.get(); }
const KCMOriginShape*	KCMOriginShapeOf()	{ return (sBytes.get() != nil) ? &sShape : nil; }
const std::vector<KCMStoryStamp>* KCMOriginStoryStamps()	{ return (sBytes.get() != nil) ? &sStamps : nil; }

void KCMOriginLabel(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	if (sBytes.get() == nil)
		return;
	out = "Task Start ";
	out.Append(sTakenAt);
}

void KCMReleaseOrigin(bool16 deferPeekClose)
{
	// The slot first, the peek document second: closing it raises kAfterCloseDoc, whose sweep
	// comes back through KCMForgetOriginIfDocClosed - and finds the slot empty.
	sBytes.reset();
	sShape = KCMOriginShape();
	sStamps.clear();
	sDocDB = nil;
	sDocName.Clear();
	sTakenAt.Clear();
	KCMOriginPeekDrop(deferPeekClose);	// the peek document stood on these bytes
}

void KCMForgetOriginIfDocClosed(IDocumentList* docList)
{
	if (docList == nil || sBytes.get() == nil || sDocDB == nil)
		return;
	if (!KCMIsDbAlive(docList, sDocDB))
		KCMReleaseOrigin(kTrue /*deferPeekClose: this is the close sweep*/);
}

void KCMOriginStatusLine(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	if (sBytes.get() == nil)
	{
		out = "held=no";
		return;
	}
	out = "held=yes doc=";
	out.Append(sDocName);
	out.Append(KCMOriginDocDB() != nil ? " (open)" : " (closed)");
	out.Append(" taken=");
	out.Append(sTakenAt);
	out.Append(" bytes=");
	out.AppendNumber(static_cast<int32>(sBytes->Size()));
	out.Append(" shape=");
	out.AppendNumber(sShape.fSpreads); out.Append("/");
	out.AppendNumber(sShape.fPages);   out.Append("/");
	out.AppendNumber(sShape.fStories); out.Append("/");
	out.AppendNumber(sShape.fTextLen);
	out.Append(" stamps=");
	out.AppendNumber(static_cast<int32>(sStamps.size()));
	out.Append(" peek=");
	PMString peek;
	KCMOriginPeekDescribe(peek);		// "-" or the target spread uid the peek document was built for
	out.Append(peek);
}

// End, KCMOrigin.cpp.
