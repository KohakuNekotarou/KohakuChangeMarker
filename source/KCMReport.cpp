//========================================================================================
//
//  KCMReport.cpp -- see the header.
//
//  THE SHAPE OF THE FILE: a handful of small steps, each a function that takes everything it
//  needs as parameters and holds no interface on any document past its own return - the same
//  discipline KCMRehydrate.cpp keeps, and for the same reason: a document closed while an
//  InterfacePtr still stands on it is a protective shutdown, not an error.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <windows.h>				// GetTempPathW / DeleteFileW / ShellExecuteW - Windows only, like the rest of KCM's file work
#include <shellapi.h>
#include <shlobj.h>				// CSIDL_DESKTOPDIRECTORY (KCMOrigin.cpp does the same for the raw XML)
#include <string>
#include <vector>
#include <map>
#include <set>
#include <ctime>

#include "IApplication.h"
#include "IBoolData.h"
#include "ICommand.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "IDocumentUtils.h"		// QueryDocFileHandler
#include "IDocFileHandler.h"
#include "IGeometry.h"
#include "IOutputPages.h"
#include "IPageList.h"
#include "IPDFExportPrefs.h"
#include "IPDFPlacePrefs.h"
#include "IPDFPostProcessPrefs.h"
#include "IPDFSecurityPrefs.h"
#include "ISession.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "ISysFileData.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "ITextAttrAlign.h"		// the captions are typed LEFT-ALIGNED, whatever the application's default paragraph says
#include "ITextAttrUtils.h"		// BuildApplyTextAttrCmd
#include "ICompositionStyle.h"		// kTextAlignLeft
#include "CreateObject.h"			// CreateObject2 - the attribute boss
#include "TextAttrID.h"			// kTextAttrAlignmentBoss
#include "TextID.h"				// kParaAttrStrandBoss
#include "IUIFlagData.h"
#include "IWorkspace.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "PersistUtils.h"			// ::GetUIDRef
#include "PreferenceUtils.h"		// ::QuerySessionPreferences
#include "TransformUtils.h"		// ::InnerToParentMatrix - a page's rectangle in spread coordinates
#include "SDKLayoutHelper.h"
#include "UIDList.h"
#include "Utils.h"
#include "WideString.h"
#include "PDFID.h"					// kPDFExportCmdBoss / kSetPDFPlacePrefsCmdBoss / IID_IUSEPROGRESSINDICATOR
#include "DocumentID.h"			// IID_ISYSFILEDATA
#include "TextChar.h"				// kTextChar_CR

#include "KCMReport.h"
#include "KCMCore.h"				// KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB / KCMCollectPageUIDs / KCMGetCompareMode
#include "KCMDrawEventHandler.h"	// sEntries / sOverflowT / sPrintMarks - what "changed" means, and the rings
#include "KCMPageMap.h"			// KCMBuildPairing / KCMMapTargetToSource - the partner of a changed page
#include "KCMOriginCompare.h"		// KCMOriginArmed / KCMOriginScopedCopy - the task-start copy, rehydrated for the report
#include "KCMOrigin.h"				// KCMOriginLabel - "Task Start HH:MM:SS"
#include "KCMRehydrate.h"			// KCMMarkRehydratedClean - the report document is closed without a save prompt
#include "KCMStoryList.h"			// the Story Edits rows for the summary page
#include "KCMStoryKinds.h"
#include "KCMResourceStore.h"		// GetSummary - the Resources line for the summary page
#include "KCMExternalSource.h"		// KCMExternalSourceLabel - a lent Source's name

namespace
{

// ---- layout constants (points) ------------------------------------------------------------
const PMReal kGutter     = 24.0;	// between the two pictures, and to the page edges
const PMReal kHeaderBand = 48.0;	// the heading above the pictures
const PMReal kCaptionH   = 18.0;	// the caption line under each picture
const int32  kMaxStoryRows = 40;	// Story Edits rows on the summary page before "... and N more"

/** One report page: a Target page (kInvalidUID for a removed page) and its partner on the
    report's Source (kInvalidUID for an added page). fBefore/fAfter are the 1-based page numbers
    inside the two temporary PDFs, filled in once the export order is settled. */
struct Pair
{
	UID		fTarget;
	UID		fSource;
	int32	fBefore;
	int32	fAfter;
	Pair(UID t, UID s) : fTarget(t), fSource(s), fBefore(0), fAfter(0) {}
};

PMString Ascii(const char* ascii)
{
	PMString s(ascii);
	s.SetTranslatable(kFalse);
	return s;
}

/** The document's name as the panel shows it, or the lent Source's label. */
PMString NameOf(IDataBase* db)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (db == nil)
		return out;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	IDocument* d = (docList != nil) ? docList->FindDocByDataBase(db) : nil;
	if (d == nil)
	{
		KCMExternalSourceLabel(db, out);
		return out;
	}
	d->GetName(out);
	out.SetTranslatable(kFalse);
	return out;
}

/** The Pages panel's number for a page ("3", "iv", "A-2"), or "?" when it cannot be read. */
PMString PageLabel(IDataBase* db, UID page)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (db != nil && page != kInvalidUID)
	{
		InterfacePtr<IPageList> pages(db, db->GetRootUID(), UseDefaultIID());
		if (pages != nil)
			pages->GetPageString(page, &out, kTrue /*section name*/, kTrue /*arabic numerals*/);
	}
	if (out.IsEmpty())
		out.Append("?");
	out.SetTranslatable(kFalse);
	return out;
}

/** The changed pages, in the order the report shows them: the Target's pages that carry a
    ring or are added (document order), then the Source's removed pages (document order). */
void CollectPairs(IDataBase* targetDB, IDataBase* sourceDB, std::vector<Pair>& out)
{
	out.clear();
	std::vector<UID> tPages, sPages, tOverflow, sOverflow;
	KCMBuildPairing(targetDB, sourceDB, tPages, sPages, &tOverflow, &sOverflow);
	std::map<UID, UID> partner;
	for (size_t i = 0; i < tPages.size(); ++i)
		partner[tPages[i]] = sPages[i];
	const std::set<UID> added(tOverflow.begin(), tOverflow.end());
	const std::set<UID> removed(sOverflow.begin(), sOverflow.end());

	std::vector<UID> tOrder;
	KCMCollectPageUIDs(targetDB, tOrder);
	for (size_t i = 0; i < tOrder.size(); ++i)
	{
		const UID t = tOrder[i];
		const bool16 ringed = (KCMDrawEventHandler::sEntries.find(t) != KCMDrawEventHandler::sEntries.end()) ? kTrue : kFalse;
		if (added.count(t) > 0)
			out.push_back(Pair(t, kInvalidUID));
		else if (ringed)
		{
			std::map<UID, UID>::const_iterator p = partner.find(t);
			out.push_back(Pair(t, (p != partner.end()) ? p->second : kInvalidUID));
		}
	}
	std::vector<UID> sOrder;
	KCMCollectPageUIDs(sourceDB, sOrder);
	for (size_t i = 0; i < sOrder.size(); ++i)
		if (removed.count(sOrder[i]) > 0)
			out.push_back(Pair(kInvalidUID, sOrder[i]));
}

/** A temporary file path: <TEMP>\KCM-<stem>-<tick>.pdf */
IDFile TempPDF(const char* stem)
{
	wchar_t dir[MAX_PATH] = { 0 };
	::GetTempPathW(MAX_PATH, dir);
	std::wstring path(dir);
	wchar_t name[128];
	swprintf_s(name, L"KCM-%S-%llu.pdf", stem, static_cast<unsigned long long>(::GetTickCount64()));
	path += name;
	PMString s;
	s.SetTranslatable(kFalse);
	s.AppendW(reinterpret_cast<const UTF16TextChar*>(path.c_str()));
	return FileUtils::PMStringToSysFile(s);
}

std::wstring WidePath(const IDFile& file)
{
	PMString s;
	FileUtils::IDFileToPMString(file, s);
	int32 n = 0;
	const UTF16TextChar* b = s.GrabUTF16Buffer(&n);
	return (b != nil && n > 0) ? std::wstring(reinterpret_cast<const wchar_t*>(b), static_cast<size_t>(n)) : std::wstring();
}

/** A paragraph end for the text frames (the model's paragraph separator is CR). */
void EndParagraph(PMString& text)
{
	text.AppendW(UTF32TextChar(kTextChar_CR));
}

/** Export `pages` (in this order, one PDF page each) of db's document to `file`.
    The session's PDF preferences (the last ones the user set) minus reader spreads, minus
    security, no UI, no progress bar, no "view after export". */
bool16 ExportPagesToPDF(IDataBase* db, const std::vector<UID>& pages, const IDFile& file, PMString& why)
{
	if (db == nil || pages.empty())
	{
		why = Ascii("nothing to export");
		return kFalse;
	}
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kPDFExportCmdBoss));
	InterfacePtr<IOutputPages> out(cmd, IID_IOUTPUTPAGES);
	InterfacePtr<ISysFileData> sys(cmd, IID_ISYSFILEDATA);
	if (cmd == nil || out == nil || sys == nil)
	{
		why = Ascii("the PDF export command could not be assembled");
		return kFalse;
	}

	UIDList list(db);
	for (size_t i = 0; i < pages.size(); ++i)
		list.Append(pages[i]);
	cmd->SetItemList(list);

	InterfacePtr<IPDFExportPrefs> appPrefs((IPDFExportPrefs*)::QuerySessionPreferences(IID_IPDFEXPORTPREFS));
	InterfacePtr<IPDFExportPrefs> prefs(cmd, IID_IPDFEXPORTPREFS);
	if (prefs != nil)
	{
		if (appPrefs != nil)
			prefs->CopyPrefs(appPrefs);
		prefs->SetPDFExReaderSpreads(IPDFExportPrefs::kExportReaderSpreadsOFF);	// one page per PDF page, or the numbers below are wrong
	}
	// No security on a temporary the placer has to open, whatever the session's setting says.
	InterfacePtr<IPDFSecurityPrefs> security(cmd, IID_IPDFSECURITYPREFS);
	if (security != nil)
		security->SetUseSecurity(kFalse);
	// The temporaries must not pop up in a viewer.
	InterfacePtr<IPDFPostProcessPrefs> post(cmd, IID_IPDFPOSTPROCESSPREFS);
	if (post != nil)
		post->SetViewAfterExport(kFalse);
	InterfacePtr<IBoolData> progress(cmd, IID_IUSEPROGRESSINDICATOR);
	if (progress != nil)
		progress->Set(kFalse);
	InterfacePtr<IUIFlagData> ui(cmd, IID_IUIFLAGDATA);
	if (ui != nil)
		ui->Set(kSuppressUI);
	sys->Set(file);
	out->InitializeFrom(list, kFalse /*spreads*/);
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		PMString name;
		if (doc != nil)
			doc->GetName(name);
		out->SetName(name);
	}

	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess || ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		why = Ascii("the PDF export failed");
		return kFalse;
	}
	return kTrue;
}

/** Pick which page of a placed PDF the next PlaceFileInFrame takes (1-based), through the
    preferences command. The caller restores the user's setting afterwards with the command
    `restore` (made before the first change, so it holds the values as they were). */
bool16 SetPlacePage(int32 page1, IPDFPlacePrefs* current, PMString& why)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetPDFPlacePrefsCmdBoss));
	InterfacePtr<IPDFPlacePrefs> data(cmd, UseDefaultIID());
	if (cmd == nil || data == nil)
	{
		why = Ascii("the PDF place preferences command could not be assembled");
		return kFalse;
	}
	if (current != nil)
		data->CopyData(current);
	data->SetPage(page1);
	data->SetAllPages(0xffffffff, kFalse);
	data->SetCropTo(IPDFPlacePrefs::kCropToMedia);		// the whole sheet, so both sides share one origin
	data->SetTransparentBackground(kFalse);
	data->SetShowPreview(kFalse);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		why = Ascii("the PDF place preferences could not be set");
		return kFalse;
	}
	return kTrue;
}

/** Type `text` into a new text frame at `bounds` (spread coordinates) on `layer`. */
void TypeAt(SDKLayoutHelper& helper, const UIDRef& layer, const PMRect& bounds, const PMString& text)
{
	UIDRef story;
	const UIDRef frame = helper.CreateTextFrame(layer, bounds, 1, kFalse, &story);
	if (frame == UIDRef::gNull || story == UIDRef::gNull)
		return;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (model == nil || cmds == nil)
		return;
	boost::shared_ptr<WideString> data(new WideString(text));
	InterfacePtr<ICommand> insert(cmds->InsertCmd(0, data));
	if (insert != nil)
		CmdUtils::ProcessCommand(insert);

	// Left-aligned, explicitly. Measured 2026-09-13: the report document is made from the
	// application's defaults, and on this machine those spread every line across the frame
	// (justified), which made the headings unreadable. The paragraph attribute is applied as an
	// override over the whole story (kParaAttrStrandBoss), the way SnpManipulateTextModel does.
	InterfacePtr<ITextAttrAlign> align(::CreateObject2<ITextAttrAlign>(kTextAttrAlignmentBoss));
	if (align != nil && data->Length() > 0)
	{
		align->SetAlignment(ICompositionStyle::kTextAlignLeft);
		InterfacePtr<ICommand> apply(Utils<ITextAttrUtils>()->BuildApplyTextAttrCmd(model, 0, static_cast<uint32>(data->Length()), align, kParaAttrStrandBoss));
		if (apply != nil)
			CmdUtils::ProcessCommand(apply);
	}
}

/** The rectangle of the n-th page of the report document, in its spread's coordinates, and
    the spread's content layer. kFalse when the document has no such page. */
bool16 ReportPageAt(SDKLayoutHelper& helper, IDataBase* db, int32 n, PMRect& outPageRect, UIDRef& outLayer)
{
	InterfacePtr<ISpreadList> spreads(db, db->GetRootUID(), UseDefaultIID());
	if (spreads == nil || n < 0 || n >= spreads->GetSpreadCount())
		return kFalse;
	const UIDRef spreadRef(db, spreads->GetNthSpreadUID(n));
	InterfacePtr<ISpread> spread(spreadRef, UseDefaultIID());
	if (spread == nil || spread->GetNumPages() < 1)
		return kFalse;
	const UIDRef pageRef(db, spread->GetNthPageUID(0));
	InterfacePtr<IGeometry> geometry(pageRef, UseDefaultIID());
	if (geometry == nil)
		return kFalse;
	outPageRect = geometry->GetStrokeBoundingBox(::InnerToParentMatrix(geometry));
	outLayer = helper.GetActiveSpreadLayerRef(spreadRef);
	return (outLayer != UIDRef::gNull) ? kTrue : kFalse;
}

/** Lay the report out: the summary page, then one page per pair. Every interface on the
    report document is released when this returns; the caller then exports and closes it. */
bool16 BuildReport(IDataBase* reportDB, IDataBase* targetDB, IDataBase* sourceDB,
				   const std::vector<Pair>& pairs, const IDFile& beforePDF, const IDFile& afterPDF,
				   const PMReal& pageW, const PMReal& pageH, const PMString& sourceName,
				   PMString& why)
{
	SDKLayoutHelper helper;
	const PMString targetName = NameOf(targetDB);

	// The user's PDF place preferences, kept in a command that puts them back at the end.
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IWorkspace> workspace(session != nil ? session->QueryWorkspace() : nil);
	InterfacePtr<IPDFPlacePrefs> currentPlace(workspace, UseDefaultIID());
	InterfacePtr<ICommand> restorePlace(CmdUtils::CreateCommand(kSetPDFPlacePrefsCmdBoss));
	{
		InterfacePtr<IPDFPlacePrefs> keep(restorePlace, UseDefaultIID());
		if (keep != nil && currentPlace != nil)
			keep->CopyData(currentPlace);
	}

	// ---- the summary page --------------------------------------------------------------
	{
		PMRect page;
		UIDRef layer;
		if (!ReportPageAt(helper, reportDB, 0, page, layer))
		{
			why = Ascii("the report document has no first page");
			return kFalse;
		}
		PMString text;
		text.SetTranslatable(kFalse);
		text.Append("Kohaku Change Marker - Before / After report");
		EndParagraph(text);
		text.Append("Before (Source): "); text.Append(sourceName); EndParagraph(text);
		text.Append("After (Target): ");  text.Append(targetName); EndParagraph(text);
		{
			const KCMCompareMode mode = KCMGetCompareMode();
			text.Append("Mode: ");
			text.Append(mode == kKCMModePixel ? "Pixel Changes" : (mode == kKCMModeStory ? "Story Changes" : "Resources Changes"));
			EndParagraph(text);
		}
		{
			int32 ringed = 0, added = 0, removed = 0;
			for (size_t i = 0; i < pairs.size(); ++i)
			{
				if (pairs[i].fTarget == kInvalidUID)      ++removed;
				else if (pairs[i].fSource == kInvalidUID) ++added;
				else                                      ++ringed;
			}
			text.Append("Pages: changed="); text.AppendNumber(ringed);
			text.Append("  added=");        text.AppendNumber(added);
			text.Append("  removed=");      text.AppendNumber(removed);
			EndParagraph(text);
		}
		{
			char stamp[64] = { 0 };
			time_t now = ::time(nil);
			struct tm local;
			::localtime_s(&local, &now);
			::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
			text.Append("Made: "); text.Append(stamp); EndParagraph(text);
		}
		EndParagraph(text);

		// Story Edits, as the panel lists them: the story's first words and what kind of change.
		const int32 rows = KCMStoryList::GetRowCount();
		text.Append("Story Edits: "); text.AppendNumber(rows); EndParagraph(text);
		for (int32 i = 0; i < rows && i < kMaxStoryRows; ++i)
		{
			const KCMStoryRow* row = KCMStoryList::GetRow(i);
			if (row == nil)
				continue;
			text.Append("  ");
			if (row->fKinds & kKCMStoryKindAdded)        text.Append("+ ");
			else if (row->fKinds & kKCMStoryKindRemoved) text.Append("- ");
			else                                         text.Append("* ");
			text.Append(row->fText);
			text.Append("  [");
			bool16 first = kTrue;
			if (row->fKinds & kKCMStoryKindText)    { text.Append("text"); first = kFalse; }
			if (row->fKinds & kKCMStoryKindAttr)    { text.Append(first ? "attr" : ", attr"); first = kFalse; }
			if (row->fKinds & kKCMStoryKindOther)   { text.Append(first ? "other" : ", other"); first = kFalse; }
			if (row->fKinds & kKCMStoryKindAdded)   { text.Append(first ? "added" : ", added"); first = kFalse; }
			if (row->fKinds & kKCMStoryKindRemoved) { text.Append(first ? "removed" : ", removed"); first = kFalse; }
			text.Append("]");
			EndParagraph(text);
		}
		if (rows > kMaxStoryRows)
		{
			text.Append("  ... and "); text.AppendNumber(rows - kMaxStoryRows); text.Append(" more");
			EndParagraph(text);
		}
		EndParagraph(text);
		{
			PMString resources;
			KCMResourceStore::GetSummary(resources);
			text.Append("Resources: "); text.Append(resources); EndParagraph(text);
		}
		const PMRect box(page.Left() + kGutter, page.Top() + kGutter, page.Right() - kGutter, page.Bottom() - kGutter);
		TypeAt(helper, layer, box, text);
	}

	// ---- one page per pair ---------------------------------------------------------------
	const IDFile beforeFile = beforePDF;
	const IDFile afterFile = afterPDF;
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		PMRect page;
		UIDRef layer;
		if (!ReportPageAt(helper, reportDB, static_cast<int32>(i) + 1, page, layer))
		{
			why = Ascii("the report document ran out of pages");
			return kFalse;
		}
		const Pair& pair = pairs[i];
		const PMReal top = page.Top() + kHeaderBand;
		const PMRect leftBox (page.Left() + kGutter,              top, page.Left() + kGutter + pageW,              top + pageH);
		const PMRect rightBox(page.Left() + 2 * kGutter + pageW,  top, page.Left() + 2 * kGutter + 2 * pageW,      top + pageH);

		// the heading: "p.5   Before: old.indd p.3   After: new.indd p.5"
		PMString head;
		head.SetTranslatable(kFalse);
		head.Append("p.");
		head.Append(PageLabel(pair.fTarget != kInvalidUID ? targetDB : sourceDB,
							  pair.fTarget != kInvalidUID ? pair.fTarget : pair.fSource));
		head.Append("     Before: "); head.Append(sourceName);
		if (pair.fSource != kInvalidUID) { head.Append(" p."); head.Append(PageLabel(sourceDB, pair.fSource)); }
		else                             head.Append(" - (added: no page there)");
		head.Append("     After: "); head.Append(targetName);
		if (pair.fTarget != kInvalidUID) { head.Append(" p."); head.Append(PageLabel(targetDB, pair.fTarget)); }
		else                             head.Append(" - (removed: no page here)");
		TypeAt(helper, layer, PMRect(page.Left() + kGutter, page.Top() + 12, page.Right() - kGutter, page.Top() + kHeaderBand - 4), head);

		// the two pictures
		if (pair.fSource != kInvalidUID && pair.fBefore > 0)
		{
			if (!SetPlacePage(pair.fBefore, currentPlace, why))
				return kFalse;
			helper.PlaceFileInFrame(beforeFile, layer, leftBox, kSuppressUI);
		}
		else
			TypeAt(helper, layer, PMRect(leftBox.Left(), leftBox.Top() + pageH / 2 - 12, leftBox.Right(), leftBox.Top() + pageH / 2 + 12), Ascii("(added - nothing on the Before side)"));

		if (pair.fTarget != kInvalidUID && pair.fAfter > 0)
		{
			if (!SetPlacePage(pair.fAfter, currentPlace, why))
				return kFalse;
			helper.PlaceFileInFrame(afterFile, layer, rightBox, kSuppressUI);
		}
		else
			TypeAt(helper, layer, PMRect(rightBox.Left(), rightBox.Top() + pageH / 2 - 12, rightBox.Right(), rightBox.Top() + pageH / 2 + 12), Ascii("(removed - nothing on the After side)"));

		// the captions
		TypeAt(helper, layer, PMRect(leftBox.Left(),  leftBox.Bottom() + 2,  leftBox.Right(),  leftBox.Bottom() + kCaptionH),  Ascii("Before"));
		TypeAt(helper, layer, PMRect(rightBox.Left(), rightBox.Bottom() + 2, rightBox.Right(), rightBox.Bottom() + kCaptionH), Ascii("After (comparison marks printed)"));
	}

	// the user's place preferences, back as they were
	if (restorePlace != nil)
		CmdUtils::ProcessCommand(restorePlace);
	return kTrue;
}

/** Where the report goes: next to the Target, or on the Desktop for a document never saved. */
bool16 ReportPath(IDataBase* targetDB, IDFile& outFile, PMString& why)
{
	PMString stem = NameOf(targetDB);
	// drop the extension
	{
		const int32 n = stem.NumUTF16TextChars();
		const UTF16TextChar* b = stem.GrabUTF16Buffer(nil);
		int32 cut = n;
		for (int32 i = n - 1; i > 0; --i)
			if (b[i] == '.') { cut = i; break; }
		PMString bare;
		bare.SetTranslatable(kFalse);
		for (int32 i = 0; i < cut; ++i)
			bare.AppendW(UTF32TextChar(b[i]));
		stem = bare;
	}
	if (stem.IsEmpty())
		stem = Ascii("Untitled");
	stem.Append(".compare-report.pdf");
	stem.SetTranslatable(kFalse);

	const IDFile* saved = targetDB->GetSysFile();
	IDFile folder;
	if (saved != nil && FileUtils::GetParentDirectory(*saved, folder))
	{
		outFile = folder;
	}
	else if (FileUtils::CoverSHGetFolderPath(CSIDL_DESKTOPDIRECTORY, &outFile) != kSuccess)
	{
		why = Ascii("neither the document's folder nor the Desktop could be found");
		return kFalse;
	}
	FileUtils::AppendPath(&outFile, stem);
	return kTrue;
}

void CloseReportDocument(const UIDRef& doc)
{
	if (doc == UIDRef::gNull)
		return;
	KCMMarkRehydratedClean(doc.GetDataBase());		// no save prompt for a throwaway
	KCMCloseRehydrated(doc, kFalse /*now*/);			// windowless: kProcess is safe (KCMRehydrate.h)
}

}	// namespace

bool16 KCMExportBeforeAfterReport(PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	IDataBase* const targetDB = KCMArmedTargetDB();
	if (!KCMIsArmed() || targetDB == nil)
	{
		outMessage = Ascii("Start a comparison first.");
		return kFalse;
	}

	// The Source for the report: the armed Source, or the task-start copy rehydrated for the
	// occasion (its page pairing works through the labels the copy's pages carry).
	KCMOriginScopedCopy copy;
	IDataBase* sourceDB = KCMArmedSourceDB();
	PMString sourceName;
	if (KCMOriginArmed())
	{
		PMString whyNot;
		if (!copy.Open(whyNot))
		{
			outMessage = Ascii("could not rebuild the task-start copy: ");
			outMessage.Append(whyNot);
			return kFalse;
		}
		sourceDB = copy.DB();
		KCMOriginLabel(sourceName);
	}
	else
		sourceName = NameOf(sourceDB);
	if (sourceDB == nil)
	{
		outMessage = Ascii("the comparison has no Source to show on the Before side.");
		return kFalse;
	}

	std::vector<Pair> pairs;
	CollectPairs(targetDB, sourceDB, pairs);
	if (pairs.empty())
	{
		outMessage = Ascii("No changed pages - nothing to report.");
		return kFalse;
	}

	// The export orders (and so the PDF page numbers): every pair's Source page, every pair's
	// Target page, each in report order.
	std::vector<UID> beforePages, afterPages;
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		if (pairs[i].fSource != kInvalidUID) { beforePages.push_back(pairs[i].fSource); pairs[i].fBefore = static_cast<int32>(beforePages.size()); }
		if (pairs[i].fTarget != kInvalidUID) { afterPages.push_back(pairs[i].fTarget);  pairs[i].fAfter  = static_cast<int32>(afterPages.size()); }
	}

	// Page size from the Target's first page.
	PMReal pageW = 595.0, pageH = 842.0;
	{
		std::vector<UID> tOrder;
		KCMCollectPageUIDs(targetDB, tOrder);
		if (!tOrder.empty())
		{
			InterfacePtr<IGeometry> g(targetDB, tOrder[0], UseDefaultIID());
			if (g != nil)
			{
				const PMRect r = g->GetStrokeBoundingBox();
				if (r.Width() > 0 && r.Height() > 0) { pageW = r.Width(); pageH = r.Height(); }
			}
		}
	}

	// ---- the two temporary PDFs ------------------------------------------------------------
	const IDFile beforePDF = TempPDF("before");
	const IDFile afterPDF  = TempPDF("after");
	PMString why;
	bool16 ok = kTrue;
	const bool16 printMarksWas = KCMDrawEventHandler::sPrintMarks;
	{
		IDataBase::SaveRestoreModifiedState targetGuard(targetDB);
		IDataBase::SaveRestoreModifiedState sourceGuard(sourceDB);
		if (!beforePages.empty())
		{
			KCMDrawEventHandler::sPrintMarks = kFalse;		// the older version plain
			ok = ExportPagesToPDF(sourceDB, beforePages, beforePDF, why);
		}
		if (ok && !afterPages.empty())
		{
			KCMDrawEventHandler::sPrintMarks = kTrue;		// the newer version with its rings
			ok = ExportPagesToPDF(targetDB, afterPages, afterPDF, why);
		}
		KCMDrawEventHandler::sPrintMarks = printMarksWas;
	}

	// ---- the report document ---------------------------------------------------------------
	IDFile reportFile;
	UIDRef reportDoc = UIDRef::gNull;
	if (ok)
		ok = ReportPath(targetDB, reportFile, why);
	if (ok)
	{
		SDKLayoutHelper helper;
		const PMReal reportW = 2 * pageW + 3 * kGutter;
		const PMReal reportH = pageH + kHeaderBand + kCaptionH + 2 * kGutter;
		reportDoc = helper.CreateDocument(kSuppressUI, reportW, reportH, static_cast<int32>(pairs.size()) + 1, 1, 0);
		if (reportDoc == UIDRef::gNull)
		{
			why = Ascii("the report document could not be created");
			ok = kFalse;
		}
	}
	if (ok)
		ok = BuildReport(reportDoc.GetDataBase(), targetDB, sourceDB, pairs, beforePDF, afterPDF, pageW, pageH, sourceName, why);
	if (ok)
	{
		std::vector<UID> all;
		KCMCollectPageUIDs(reportDoc.GetDataBase(), all);
		ok = ExportPagesToPDF(reportDoc.GetDataBase(), all, reportFile, why);
	}
	CloseReportDocument(reportDoc);
	::DeleteFileW(WidePath(beforePDF).c_str());
	::DeleteFileW(WidePath(afterPDF).c_str());

	if (!ok)
	{
		outMessage = Ascii("Report failed: ");
		outMessage.Append(why);
		return kFalse;
	}

	// The reader wants to see it now.
	::ShellExecuteW(nil, L"open", WidePath(reportFile).c_str(), nil, nil, SW_SHOWNORMAL);

	outMessage = Ascii("Report: ");
	outMessage.AppendNumber(static_cast<int32>(pairs.size()) + 1);
	outMessage.Append(" pages -> ");
	{
		PMString path;
		FileUtils::IDFileToPMString(reportFile, path);
		outMessage.Append(path);
	}
	return kTrue;
}

// End, KCMReport.cpp.
