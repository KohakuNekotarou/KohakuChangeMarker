//========================================================================================
//
//  KCMPdfDoc.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <windows.h>				// GetFileAttributesExW - the PDF's write time and size (KCM ships for Windows only)

// Interface includes:
#include "IApplyMasterCmdData.h"	// the new spreads' master (none) and page size rule
#include "ICmdHistory.h"			// Discard - nothing of KCM's making left to undo
#include "ICommand.h"
#include "ICommandProcessor.h"		// only to reach the boss ICmdHistory lives on
#include "IDataBase.h"
#include "IDocumentList.h"
#include "IGeometry.h"
#include "IIntData.h"				// IID_IBINDINGLOCATION
#include "INewSpreadCmdData.h"
#include "IPDFPlacePrefs.h"			// kCropToTrim / kCropToPage
#include "ISession.h"				// GetExecutionContextSession
#include "ISpread.h"				// kDefaultBinding
#include "ISpreadList.h"			// kAtTheEnd
#include "IStringData.h"			// kSetDocNameCmdBoss carries the name here

// General includes:
#include "CmdUtils.h"
#include "DocumentID.h"				// kSetDocNameCmdBoss
#include "ErrorUtils.h"
#include "FileUtils.h"				// GetFileName / IsEqual / IDFileToPMString
#include "PMPageSize.h"
#include "SDKLayoutHelper.h"		// CreateDocument / PlaceFileInFrame / OpenLayoutWindow - the SDK's own recipe
#include "SpreadID.h"				// kNewSpreadCmdBoss / IID_IBINDINGLOCATION
#include "TransformUtils.h"			// ::InnerToParentMatrix
#include "UIDList.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

// Project includes:
#include "KCMPdfDoc.h"
#include "KCMPdfPages.h"			// the reader: the pages, their boxes, rotation and InDesign page uids
#include "KCMPlacePdf.h"			// one PDF page placed, the reader's place settings given back
#include "KCMPairChoice.h"		// KCMPairEnd, the file choices
#include "KCMReportTable.h"		// KCMReportPageAt - a spread's page rectangle (spread coordinates) and its layer
#include "KCMRehydrate.h"			// KCMMarkRehydratedClean / KCMCloseRehydrated - a document KCM made, closed cleanly
#include "KCMProgressBar.h"		// KCMProgressStepper - a bar that appears only after three seconds
#include "KCMCore.h"				// KCMIsDocDBOpen / KCMCollectPageUIDs
#include "KCMPageMap.h"			// KCMGetPairPagesByUid - the note says when the toggle put order first

namespace
{
	const int32 kMaxPdfPages = 9999;		// InDesign's ceiling for a document's pages (its UI says so; unmeasured here)
	const double kMinPagePt = 1.0;			// ...and for a page's sides in points (1 pt to 216 in; unmeasured here)
	const double kMaxPagePt = 15552.0;

	enum IdsState { kIdsOne, kIdsNone, kIdsMixed };

	struct PdfDoc
	{
		IDFile				fFile;			// the PDF
		uint64				fWriteTime;		// its last write time when the document was made (a FILETIME)
		uint64				fSize;			// ...and its size
		IDataBase*			fDB;			// compared, never dereferenced unless the document list holds it
		std::map<UID, UID>	fKeys;			// document page -> the InDesign page uid its PDF page carries (kIdsOne only)
		IdsState			fIds;
		PMString			fName;			// "<file>.pdf (KCM)"
		PMString			fMisfits;		// "3, 7": pages that did not come out at their page size; empty when none

		PdfDoc() : fWriteTime(0), fSize(0), fDB(nil), fIds(kIdsNone) {}
	};

	std::vector<PdfDoc> gDocs;

	const PdfDoc* Find(IDataBase* db)
	{
		if (db == nil)
			return nil;
		for (size_t i = 0; i < gDocs.size(); ++i)
			if (gDocs[i].fDB == db)
				return &gDocs[i];
		return nil;
	}

	/** The path as wide characters, for the reader and Win32 - the four lines KCMReport.cpp's WidePath has. */
	std::wstring WideOf(const IDFile& file)
	{
		PMString s;
		FileUtils::IDFileToPMString(file, s);
		int32 n = 0;
		const UTF16TextChar* b = s.GrabUTF16Buffer(&n);
		return (b != nil && n > 0) ? std::wstring(reinterpret_cast<const wchar_t*>(b), static_cast<size_t>(n)) : std::wstring();
	}

	/** The file's last write time and size - what says "the PDF was written again" (design 3.4). */
	bool16 FileStamp(const IDFile& file, uint64& writeTime, uint64& size)
	{
		const std::wstring path = WideOf(file);
		WIN32_FILE_ATTRIBUTE_DATA a;
		if (path.empty() || !::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
			return kFalse;
		writeTime = (static_cast<uint64>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
		size = (static_cast<uint64>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
		return kTrue;
	}

	bool16 SameSize(const KCMPdf::PageInfo& a, const KCMPdf::PageInfo& b)
	{
		return (std::fabs(a.ShownWidth() - b.ShownWidth()) < 0.001 && std::fabs(a.ShownHeight() - b.ShownHeight()) < 0.001)
			? kTrue : kFalse;
	}

	/** Pages 2..n as spreads of one page at the end, a run of one size in one command - KCMReportTable.cpp's AppendPage
		with the page's own size in place of its neighbour's (the SDK's own: SnpManipulateSpreadsAndPages.cpp).
		⛔kSetPageSizeCmdBoss is not used: the SDK holds no caller of it and the form of its page range is unknown. */
	bool16 AddPages(IDataBase* db, const KCMPdf::Info& info, PMString& why)
	{
		const size_t n = info.pages.size();
		size_t i = 1;
		while (i < n)
		{
			size_t j = i + 1;
			while (j < n && SameSize(info.pages[j], info.pages[i]))
				++j;
			InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kNewSpreadCmdBoss));
			InterfacePtr<INewSpreadCmdData> data(cmd, UseDefaultIID());
			InterfacePtr<IApplyMasterCmdData> master(cmd, IID_IAPPLYMASTERCMDDATA);
			InterfacePtr<IIntData> binding(cmd, IID_IBINDINGLOCATION);
			if (cmd == nil || data == nil || master == nil || binding == nil)
			{
				why = "The PDF document could not be given its pages.";
				return kFalse;
			}
			data->SetNewSpreadCmdData(UIDRef(db, db->GetRootUID()), static_cast<int32>(j - i), ISpreadList::kAtTheEnd,
									  INewSpreadCmdData::kPagesMayShuffleThroughNewSpread, 1,
									  PMPageSize(PMReal(info.pages[i].ShownWidth()), PMReal(info.pages[i].ShownHeight())));
			master->SetApplyMasterCmdData(kInvalidUIDRef, IApplyMasterCmdData::kKeepCurrentPageSize);
			binding->Set(ISpread::kDefaultBinding);
			if (CmdUtils::ProcessCommand(cmd) != kSuccess)
			{
				ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				why = "The PDF document could not be given page ";
				why.AppendNumber(static_cast<int32>(i) + 1);
				why.Append(".");
				return kFalse;
			}
			i = j;
		}
		return kTrue;
	}

	/** Whether a placed frame covers its page within half a point - the PDF page came out at the size the page was made
		for (a rotated page, or a box InDesign reads otherwise than the reader, would not). */
	bool16 FitsPage(const UIDRef& frame, const PMRect& page)
	{
		if (frame == UIDRef::gNull)
			return kFalse;
		InterfacePtr<IGeometry> g(frame, UseDefaultIID());
		if (g == nil)
			return kFalse;
		const PMRect r = g->GetPathBoundingBox(::InnerToParentMatrix(g));
		const double tol = 0.5;
		return (std::fabs(::ToDouble(r.Left() - page.Left())) <= tol && std::fabs(::ToDouble(r.Top() - page.Top())) <= tol &&
				std::fabs(::ToDouble(r.Right() - page.Right())) <= tol && std::fabs(::ToDouble(r.Bottom() - page.Bottom())) <= tol)
			? kTrue : kFalse;
	}

	/** Every PDF page onto its own page, cropped to the trim box (the crop box for a page that names none), the reader's place
		settings given back however this ends. kFalse with `why` when a page does not place. The cancel is asked between pages
		(a placing cannot be interrupted) and never after the last. */
	bool16 PlacePages(SDKLayoutHelper& helper, IDataBase* db, const IDFile& pdf, const KCMPdf::Info& info,
					  PMString& misfits, PMString& why)
	{
		InterfacePtr<IPDFPlacePrefs> current(KCMQuerySessionPdfPlacePrefs());
		KCMPdfPlacePrefsRestorer restore(current);
		const int32 n = static_cast<int32>(info.pages.size());
		PMString title("Making the PDF document...");
		title.SetTranslatable(kFalse);
		KCMProgressStepper progress(title, n);
		for (int32 i = 0; i < n; ++i)
		{
			PMString step("Page ");
			step.AppendNumber(i + 1);
			step.Append(" / ");
			step.AppendNumber(n);
			step.SetTranslatable(kFalse);
			progress.Step(i, step);
			if (i > 0 && progress.WasCancelled())
			{
				why = "Cancelled.";
				return kFalse;
			}
			PMRect page;
			UIDRef layer;
			if (!KCMReportPageAt(helper, db, i, page, layer))
			{
				why = "The PDF document lost a page while it was being made.";
				return kFalse;
			}
			PMString placeWhy;
			// ★THE BOX THE PAGE NAMES (2026-10-14, the live test): InDesign refuses "crop to trim" on a page that names no
			//   /TrimBox - "仕上がりサイズにトリミングできません。仕上がりサイズが定義されていないか、空です。" - where the spec falls back to
			//   the crop box, and the crop box is what the reader took for such a page's size (KCMPdfPages.h). kCropToPage is
			//   InDesign's "Crop": the crop box, the media box when the page names none (both measured on the crafted PDFs).
			const int32 cropTo = info.pages[i].hasTrim ? IPDFPlacePrefs::kCropToTrim : IPDFPlacePrefs::kCropToPage;
			if (!KCMSetPdfPlacePage(i + 1, cropTo, current, placeWhy))
			{
				why = "The PDF document could not be made: ";
				why.Append(placeWhy);
				why.Append(".");
				return kFalse;
			}
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			const UIDRef frame = helper.PlaceFileInFrame(pdf, layer, page, kSuppressUI);
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// InDesign's own words are its language; the refusal below is said in KCM's
			// ★A PAGE INDESIGN DID NOT PLACE REFUSES THE PDF (2026-10-14, the live test): it would stay an empty page, and two
			//   empty pages compare unchanged - the one answer a check of an export must never give wrongly. (The crafted PDFs
			//   did exactly that before the crop above: "pages compared=2 changed=0" over two blank documents.)
			if (frame == UIDRef::gNull)
			{
				why = "The PDF could not be placed: InDesign refused page ";
				why.AppendNumber(i + 1);
				why.Append(".");
				return kFalse;
			}
			if (!FitsPage(frame, page))
			{
				if (!misfits.IsEmpty())
					misfits.Append(", ");
				misfits.AppendNumber(i + 1);
			}
		}
		return kTrue;
	}

	bool16 NameDoc(const UIDRef& docRef, const PMString& name, PMString& why)
	{
		// ⚠THROUGH THE COMMAND, not IDocument::SetName - the header says third parties use kSetDocNameCmdBoss
		//  (IDocument.h:141-143); the shape is KCM's own of 2026-09-15 (git 33b0ec6, NameCopyAfterOrigin).
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetDocNameCmdBoss));
		InterfacePtr<IStringData> data(cmd, IID_ISTRINGDATA);
		if (cmd == nil || data == nil)
		{
			why = "The PDF document could not be named.";
			return kFalse;
		}
		cmd->SetItemList(UIDList(docRef));
		data->Set(name);
		if (CmdUtils::ProcessCommand(cmd) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			why = "The PDF document could not be named.";
			return kFalse;
		}
		return kTrue;
	}

	/** ★NOTHING OF KCM'S MAKING TO UNDO (2026-10-14, the live test): the pages were placed by commands, and InDesign keeps
		them in the new document's undo history - measured after a Start: "配置" / "フレームを内容に合わせる", five steps on one
		of the two documents (InDesign had dropped the other's at a script boundary, which it does at random: memory
		command-history-and-undo-stack). A Ctrl+Z there would take KCM's pages away under a running comparison and leave the
		document modified. ICmdHistory::Discard - "delete all history data for the given target", the public part of
		ICmdHistory.h (only DiscardRedo is internal) - empties it; the document was made a moment ago, so no step of the
		reader's is in it. The target is the document, never UIDRef::gNull (the header: gNull is the application). */
	void DiscardHistory(const UIDRef& docRef)
	{
		ISession* const session = GetExecutionContextSession();
		if (session == nil)
			return;
		InterfacePtr<ICommandProcessor> processor(session->QueryCommandProcessor());
		if (processor == nil)
			return;
		InterfacePtr<ICmdHistory> history(processor, UseDefaultIID());
		if (history != nil)
			history->Discard(docRef);
	}

	bool16 MakePdfDoc(const IDFile& pdf, const KCMPdf::Info& info, const PMString& name, PdfDoc& out, PMString& why)
	{
		SDKLayoutHelper helper;
		const UIDRef docRef = helper.CreateDocument(kSuppressUI, PMReal(info.pages[0].ShownWidth()),
												   PMReal(info.pages[0].ShownHeight()), 1, 1, 0);
		if (docRef == UIDRef::gNull)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			why = "The PDF document could not be made.";
			return kFalse;
		}
		IDataBase* const db = docRef.GetDataBase();
		PMString misfits;
		bool16 ok = AddPages(db, info, why);
		if (ok)
			ok = PlacePages(helper, db, pdf, info, misfits, why);
		if (ok)
			ok = NameDoc(docRef, name, why);
		if (!ok)
		{
			// ⚠No interface on the document is held here - each step let go of its own - which is what makes the close
			//  legal (KCMRehydrate.h). No window yet: it closes at once.
			KCMMarkRehydratedClean(db);
			KCMCloseRehydrated(docRef, kFalse);
			return kFalse;
		}
		if (helper.OpenLayoutWindow(docRef) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			KCMMarkRehydratedClean(db);
			KCMCloseRehydrated(docRef, kTrue);		// scheduled: whether a window came up is not certain here
			why = "The PDF document opened without a window.";
			return kFalse;
		}
		DiscardHistory(docRef);				// and nothing to undo (the function says why)
		KCMMarkRehydratedClean(db);			// nothing in it to save, so closing it asks nothing (design 3.3-6)
		out.fDB = db;
		out.fMisfits = misfits;
		if (info.HasPageIds())
		{
			std::vector<UID> pages;
			KCMCollectPageUIDs(db, pages);
			for (size_t i = 0; i < pages.size() && i < info.pages.size(); ++i)
				out.fKeys[pages[i]] = UID(info.pages[i].uids[0]);	// a spread's PDF page answers to its first page
			out.fIds = kIdsOne;
		}
		else
			out.fIds = info.HasMixedPageIds() ? kIdsMixed : kIdsNone;
		return kTrue;
	}

	bool16 RealisePdfEnd(const IDFile& pdf, IDataBase*& outDB, PMString& why)
	{
		uint64 writeTime = 0;
		uint64 size = 0;
		if (!FileStamp(pdf, writeTime, size))
		{
			why = "The PDF could not be read: the file could not be opened.";
			return kFalse;
		}

		// 1. THE DOCUMENT MADE BEFORE, while it is open and the PDF has not changed since (design 3.4).
		for (size_t i = 0; i < gDocs.size(); )
		{
			if (!FileUtils::IsEqual(gDocs[i].fFile, pdf))
			{
				++i;
				continue;
			}
			IDataBase* const old = gDocs[i].fDB;
			if (!KCMIsDocDBOpen(old))
			{
				gDocs.erase(gDocs.begin() + static_cast<ptrdiff_t>(i));	// closed without the sweep seeing it
				continue;
			}
			if (gDocs[i].fWriteTime == writeTime && gDocs[i].fSize == size)
			{
				outDB = old;
				return kTrue;
			}
			// The PDF was written again: this document shows what it was. It is forgotten either way, and closed when
			// nobody has changed it - scheduled, since it has a window (KCMRehydrate.h).
			gDocs.erase(gDocs.begin() + static_cast<ptrdiff_t>(i));
			if (!old->IsModified())
				KCMCloseRehydrated(UIDRef(old, old->GetRootUID()), kTrue);
		}

		// 2. READ IT.
		KCMPdf::Info info;
		std::string readWhy;
		const std::wstring path = WideOf(pdf);
		if (!KCMPdf::ReadInfo(path.c_str(), info, readWhy))
		{
			why = "The PDF could not be read: ";
			why.Append(readWhy.c_str());
			why.Append(".");
			return kFalse;
		}
		const int32 n = static_cast<int32>(info.pages.size());
		if (n > kMaxPdfPages)
		{
			why = "The PDF could not be read: it has ";
			why.AppendNumber(n);
			why.Append(" pages, more than a document holds (9999).");
			return kFalse;
		}
		for (int32 i = 0; i < n; ++i)
		{
			const KCMPdf::PageInfo& p = info.pages[static_cast<size_t>(i)];
			if (p.ShownWidth() < kMinPagePt || p.ShownHeight() < kMinPagePt || p.ShownWidth() > kMaxPagePt || p.ShownHeight() > kMaxPagePt)
			{
				why = "The PDF could not be read: page ";
				why.AppendNumber(i + 1);
				why.Append(" is too small or too large for a document page.");
				return kFalse;
			}
		}

		// 3. MAKE IT.
		PMString name;
		FileUtils::GetFileName(pdf, name);
		name.Append(" (KCM)");
		name.SetTranslatable(kFalse);
		PdfDoc made;
		if (!MakePdfDoc(pdf, info, name, made, why))
			return kFalse;
		made.fFile = pdf;
		made.fWriteTime = writeTime;
		made.fSize = size;
		made.fName = name;
		gDocs.push_back(made);
		outDB = made.fDB;
		return kTrue;
	}

	void AppendMisfits(PMString& out, const PdfDoc* d)
	{
		if (d == nil || d->fMisfits.IsEmpty())
			return;
		out.Append(" Pages not at their page size in ");
		out.Append(d->fName);
		out.Append(": ");
		out.Append(d->fMisfits);
		out.Append(".");
	}
}	// namespace

bool16 KCMRealisePdfEnd(const IDFile& pdf, IDataBase*& outDB, PMString& why)
{
	outDB = nil;
	why.Clear();
	const bool16 ok = RealisePdfEnd(pdf, outDB, why);
	why.SetTranslatable(kFalse);
	return ok;
}

bool16 KCMFileIsPdf(const IDFile& file)
{
	const std::wstring path = WideOf(file);
	return (!path.empty() && KCMPdf::LooksLikePdf(path.c_str())) ? kTrue : kFalse;
}

bool16 KCMPairEndIsPdf(const KCMPairEnd& end)
{
	return end.fIsFile ? KCMFileIsPdf(end.fFile) : KCMIsPdfDoc(end.fDB);
}

bool16 KCMIsPdfDoc(IDataBase* db)
{
	return (Find(db) != nil) ? kTrue : kFalse;
}

bool16 KCMPdfDocsPairByIds(IDataBase* targetDB, IDataBase* sourceDB)
{
	const PdfDoc* t = Find(targetDB);
	const PdfDoc* s = Find(sourceDB);
	if (t == nil && s == nil)
		return kTrue;
	return (t != nil && s != nil && t->fIds == kIdsOne && s->fIds == kIdsOne) ? kTrue : kFalse;
}

void KCMPdfDocPageKeys(IDataBase* db, const std::vector<UID>& pages, std::vector<UID>& outKeys)
{
	outKeys.assign(pages.size(), kInvalidUID);
	const PdfDoc* d = Find(db);
	if (d == nil)
		return;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		const std::map<UID, UID>::const_iterator it = d->fKeys.find(pages[i]);
		if (it != d->fKeys.end())
			outKeys[i] = it->second;
	}
}

void KCMPdfPairingNote(IDataBase* targetDB, IDataBase* sourceDB, PMString& out)
{
	out.Clear();
	const PdfDoc* t = Find(targetDB);
	const PdfDoc* s = Find(sourceDB);
	if (t == nil && s == nil)
		return;
	if (!KCMGetPairPagesByUid())
		out = "PDF pages paired in order (Pair Pages by UID is off).";
	else if (KCMPdfDocsPairByIds(targetDB, sourceDB))
		out = "PDF pages paired by their InDesign page ids.";
	else if ((t != nil && t->fIds == kIdsMixed) || (s != nil && s->fIds == kIdsMixed))
		out = "A PDF holds pages of several documents, or pages without ids - pages paired in order.";
	else
		out = "These PDFs carry no page ids - pages paired in order.";
	AppendMisfits(out, t);
	AppendMisfits(out, s);
	out.SetTranslatable(kFalse);
}

bool16 KCMChoosePdfEnd(bool16 asTarget, const IDFile& pdf, PMString& outMessage)
{
	outMessage.Clear();
	PMString name;
	FileUtils::GetFileName(pdf, name);
	if (!KCMFileIsPdf(pdf))
	{
		outMessage = "Not a PDF: ";
		outMessage.Append(name);
		outMessage.Append(" (its first bytes are not a PDF's).");
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}
	if (asTarget)
		KCMSetChosenTargetFile(pdf);
	else
		KCMSetChosenSourceFile(pdf);
	IDFile other;
	const bool16 otherIsPdf = asTarget
		? ((KCMChosenSourceFile(other) && KCMFileIsPdf(other)) || KCMIsPdfDoc(KCMChosenSourceDB()))
		: ((KCMChosenTargetFile(other) && KCMFileIsPdf(other)) || KCMIsPdfDoc(KCMChosenTargetDB()));
	if (otherIsPdf)
		outMessage = "Both ends are PDFs. Start compares them.";
	else
	{
		outMessage = asTarget ? "PDF chosen as the Target: " : "PDF chosen as the Source: ";
		outMessage.Append(name);
		outMessage.Append(asTarget ? ". Choose a PDF as the Source too, then Start." : ". Choose a PDF as the Target too, then Start.");
	}
	outMessage.SetTranslatable(kFalse);
	return kTrue;
}

void KCMPdfDocSweep(IDocumentList* docList)
{
	if (docList == nil)
		return;
	for (size_t i = 0; i < gDocs.size(); )
	{
		if (docList->FindDocByDataBase(gDocs[i].fDB) == nil)
			gDocs.erase(gDocs.begin() + static_cast<ptrdiff_t>(i));
		else
			++i;
	}
}

void KCMPdfDocShutdown()
{
	std::vector<PdfDoc>().swap(gDocs);		// a fresh value, not clear(): nothing of it may reach static destruction
}

// End, KCMPdfDoc.cpp.
