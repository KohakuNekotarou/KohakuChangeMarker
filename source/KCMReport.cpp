//========================================================================================
//
//  KCMReport.cpp -- see the header.
//
//  THE SHAPE OF THE FILE: a handful of small steps, each a function that takes everything it
//  needs as parameters and holds no interface on any document past its own return - the same
//  discipline KCMRehydrate.cpp keeps, and for the same reason: a document closed while an
//  InterfacePtr still stands on it is a protective shutdown, not an error.
//
//  THE THREE SECTIONS (2026-09-13, the user's ask): the Pixel pages (pictures only), then the
//  Story table, then the Resources table. Whatever mode the comparison ran in, the two tables are
//  filled: the Story detail and the Resources result are BORROWED for the report when the mode
//  did not produce them, and put back exactly as they were (StoryDetailLoan / ResourceLoan below).
//  The Pixel pages alone cannot be borrowed - rasterising rewrites the rings, the pairing and the
//  overflow cache the screen is showing - so outside the Pixel mode the report says so on its
//  first page and shows the added / removed pages only.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <windows.h>				// GetTempPathW / DeleteFileW / ShellExecuteW - Windows only, like the rest of KCM's file work
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <ctime>					// the export time stamp on the first page

#include "IApplication.h"
#include "IBoolData.h"
#include "ICommand.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "IGeometry.h"
#include "IOutputPages.h"
#include "IPageList.h"
#include "IPDFExportPrefs.h"
#include "IPDFPlacePrefs.h"
#include "IPDFPostProcessPrefs.h"
#include "IPDFSecurityPrefs.h"
#include "ISession.h"
#include "ISysFileData.h"
#include "SDKFileHelper.h"			// SDKFileSaveChooser - where the report goes is the user's choice
#include "IUIFlagData.h"
#include "IWorkspace.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "PreferenceUtils.h"		// ::QuerySessionPreferences
#include "SDKLayoutHelper.h"
#include "UIDList.h"
#include "Utils.h"
#include "WideString.h"
#include "PDFID.h"					// kPDFExportCmdBoss / kSetPDFPlacePrefsCmdBoss / IID_IUSEPROGRESSINDICATOR
#include "DocumentID.h"			// IID_ISYSFILEDATA
#include "TextChar.h"				// kTextChar_CR

#include "KCMReport.h"
#include "KCMReportTable.h"		// the page helpers and the table sections
#include "KCMReportPaws.h"			// the cat's trail on the first page
#include "KCMCore.h"				// KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB / KCMCollectPageUIDs / KCMGetCompareMode
#include "KCMDrawEventHandler.h"	// sEntries / sOverflowT / sPrintMarks - what "changed" means, and the rings
#include "KCMPageMap.h"			// KCMBuildPairing / KCMMapTargetToSource - the partner of a changed page
#include "KCMOriginCompare.h"		// KCMOriginArmed / KCMOriginScopedCopy - the task-start copy, rehydrated for the report
#include "KCMOrigin.h"				// KCMOriginLabel - "Task Start HH:MM:SS"
#include "KCMRehydrate.h"			// KCMMarkRehydratedClean - the report document is closed without a save prompt
#include "KCMStoryList.h"			// the Story Edits rows and their changes
#include "KCMStoryKinds.h"
#include "KCMStoryDiffRun.h"		// Run - the Story detail, borrowed when the mode did not produce it
#include "KCMStoryRestore.h"		// KCMKentenKindOf - a kenten name to its kind
#include "KCMResourceStore.h"		// the Resources result, borrowed when the mode did not produce it
#include "KCMResourceShortValue.h"	// KCMShortResourceValue - what a value reads as, the same as the panel
#include "KCMResourceUnits.h"		// KCMResourceValueWithUnit - and the document's own unit beside it
#include "KCMXmlPretty.h"			// KCMDecodePercentEscapes - a definition's key, readable
#include "KCMModelNotify.h"		// KCMNotify - the panel is told when the borrowed Resources result goes
#include "KCMBoundaryID.h"			// kKCMStoryEditsRebuiltMessage
#include "KCMExternalSource.h"		// KCMExternalSourceLabel - a lent Source's name

namespace
{

const int32 kMaxTableRows = 400;	// rows of one table section before "... and N more"

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

/** Is db one of the session's open documents (the rehydrated copy included)? A lent database
    clone is not, and must not be exported. Pointer comparison only, nothing dereferenced. */
bool16 IsSessionDocument(IDataBase* db)
{
	if (db == nil)
		return kFalse;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	return (docList != nil && docList->FindDocByDataBase(db) != nil) ? kTrue : kFalse;
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

/** Holds the PDF place preferences as they were, in a command made on construction, and
    processes it on destruction - so the user's setting comes back whichever way BuildReport
    leaves (a failed SetPlacePage used to leave the page number changed). */
class PlacePrefsRestorer
{
public:
	explicit PlacePrefsRestorer(IPDFPlacePrefs* current)
		: fRestore(CmdUtils::CreateCommand(kSetPDFPlacePrefsCmdBoss))
	{
		InterfacePtr<IPDFPlacePrefs> keep(fRestore, UseDefaultIID());
		if (keep != nil && current != nil)
			keep->CopyData(current);
	}
	~PlacePrefsRestorer()
	{
		if (fRestore != nil)
		{
			GlobalErrorStatePreserver errorState;		// a destructor must not change the error the caller is reporting
			CmdUtils::ProcessCommand(fRestore);
		}
	}
private:
	InterfacePtr<ICommand> fRestore;
	PlacePrefsRestorer(const PlacePrefsRestorer&);
	PlacePrefsRestorer& operator=(const PlacePrefsRestorer&);
};

// ---- the Story detail, borrowed ------------------------------------------------------------------

/** Runs the text diff over the Story Edits rows for the report when the comparison's mode did
    not (Pixel and Resources leave the rows without children), and puts every row back as it was
    on the way out - what a row held, whether it was compared, its text counter. The panel is
    never told, because from its point of view nothing changed.
    ⚠Outside the Story mode the rows have already been through DropRowsWithNoContentChange, so a
      story whose only edit is a ruby or a kenten has no row to diff; that is the Pixel mode's
      known limit, not the report's. */
class StoryDetailLoan
{
public:
	StoryDetailLoan(IDataBase* targetDB, IDataBase* sourceDB, bool16 needed)
		: fLent(kFalse), fCancelled(kFalse)
	{
		if (!needed || targetDB == nil || sourceDB == nil)
			return;
		const int32 n = KCMStoryList::GetRowCount();
		fKeep.resize(static_cast<size_t>(n));
		for (int32 i = 0; i < n; ++i)
		{
			const KCMStoryRow* row = KCMStoryList::GetRow(i);
			if (row == nil)
				continue;
			fKeep[i].fChanges = row->fChanges;
			fKeep[i].fTextCompared = row->fTextCompared;
			fKeep[i].fTargetTextCount = row->fTargetTextCount;
		}
		fLent = kTrue;
		KCMStoryDiffRun::Run(targetDB, sourceDB, &fCancelled);
	}
	~StoryDetailLoan()
	{
		if (!fLent)
			return;
		const int32 n = KCMStoryList::GetRowCount();
		for (int32 i = 0; i < n && i < static_cast<int32>(fKeep.size()); ++i)
		{
			KCMStoryList::SetRowChanges(i, fKeep[i].fChanges, fKeep[i].fTextCompared);
			KCMStoryList::SetRowTargetTextCount(i, fKeep[i].fTargetTextCount);
		}
	}
	bool16 WasCancelled() const { return fCancelled; }
private:
	struct Keep
	{
		std::vector<KCMStoryChange>	fChanges;
		bool16						fTextCompared;
		uint32						fTargetTextCount;
		Keep() : fTextCompared(kFalse), fTargetTextCount(0) {}
	};
	std::vector<Keep>	fKeep;
	bool16				fLent;
	bool16				fCancelled;
	StoryDetailLoan(const StoryDetailLoan&);
	StoryDetailLoan& operator=(const StoryDetailLoan&);
};

// ---- the Resources result, borrowed -----------------------------------------------------------------

/** Fills the Resources store for the report when the comparison's mode did not, and empties it
    again on the way out. The rebuild tells the panel (FinishRebuild notifies), so the emptying
    tells it too - a heading reading "(0)" over rows that are still drawn is the very defect that
    notification was written for. */
class ResourceLoan
{
public:
	ResourceLoan(IDataBase* targetDB, IDataBase* sourceDB)
		: fBorrowed(kFalse)
	{
		if (KCMResourceStore::HasResult())
			return;
		fBorrowed = kTrue;
		KCMResourceStore::RebuildForPair(targetDB, sourceDB, fWhyNot);
	}
	~ResourceLoan()
	{
		if (!fBorrowed)
			return;
		KCMResourceStore::Clear();
		KCMNotify(kKCMStoryEditsRebuiltMessage);
	}
	const PMString& WhyNot() const { return fWhyNot; }
private:
	bool16		fBorrowed;
	PMString	fWhyNot;
	ResourceLoan(const ResourceLoan&);
	ResourceLoan& operator=(const ResourceLoan&);
};

// ---- the rows of the two tables ------------------------------------------------------------------

/** The Story Edits rows as table rows: a heading row per story, then one row per change - the
    older side on the left, the newer on the right, in the three pieces the panel shows. */
void BuildStoryRows(IDataBase* targetDB, IDataBase* sourceDB, std::vector<KCMReportRow>& out, int32& outStories, int32& outEdits)
{
	out.clear();
	outStories = 0;
	outEdits = 0;
	const int32 n = KCMStoryList::GetRowCount();
	for (int32 i = 0; i < n; ++i)
	{
		const KCMStoryRow* row = KCMStoryList::GetRow(i);
		if (row == nil)
			continue;
		++outStories;
		const bool16 removed = (row->fKinds & kKCMStoryKindRemoved) ? kTrue : kFalse;
		const bool16 unpaired = (row->fKinds & kKCMStoryKindUnpaired) ? kTrue : kFalse;
		IDataBase* const db = removed ? sourceDB : targetDB;

		// The story row as the panel shows it: ID | Δ | the story's first words (with its page).
		KCMReportRow head;
		head.fHeading = kTrue;
		head.fLabel.AppendNumber(static_cast<int32>(row->fStoryUID.Get()));
		head.fLabel.SetTranslatable(kFalse);
		if (row->fKinds & kKCMStoryKindAdded)                 head.fSign = KCMReportSign::Plus();
		else if (removed)                                     head.fSign = KCMReportSign::Minus();
		else if (row->fTextCompared && row->fChanges.empty()) head.fSign = KCMReportSign::Equal();
		else                                                  head.fSign = KCMReportSign::NotEqual();
		PMString& h = head.fLeft.fMid;
		h.SetTranslatable(kFalse);
		h.Append("p.");
		h.Append(PageLabel(db, row->fPageUID));
		h.Append("  ");
		h.Append(row->fText);
		out.push_back(head);

		if (unpaired)
		{
			KCMReportRow r;
			r.fJoinLabel = kTrue;		// under its story's ID
			r.fSign = head.fSign;
			if (removed) { r.fLeft.fMid = row->fText; r.fRight.fMid = Ascii("(removed story)"); }
			else         { r.fLeft.fMid = Ascii("(added story)"); r.fRight.fMid = row->fText; }
			out.push_back(r);
			continue;
		}
		for (size_t c = 0; c < row->fChanges.size(); ++c)
		{
			const KCMStoryChange& ch = row->fChanges[c];
			++outEdits;
			KCMReportRow r;
			r.fJoinLabel = kTrue;		// under its story's ID: one ID cell for the story and its edits
			// The change's sign, as the panel's child rows carry it: + inserted, - deleted, ≠ replaced.
			r.fSign = (ch.fKind == KCMStoryChange::kInsert) ? KCMReportSign::Plus()
					: (ch.fKind == KCMStoryChange::kDelete) ? KCMReportSign::Minus()
					: KCMReportSign::NotEqual();
			r.fLeft.fPre   = ch.fOtherTextPre;
			r.fLeft.fMid   = ch.fOtherText;
			r.fLeft.fPost  = ch.fOtherTextPost;
			r.fRight.fPre  = ch.fTextPre;
			r.fRight.fMid  = ch.fText;
			r.fRight.fPost = ch.fTextPost;
			// An empty middle on one side is a PLACE (the words were typed in here / taken out
			// from here): a bar marks it, as the panel's caret does.
			if (ch.fWhat == KCMStoryChange::kText)
			{
				if (r.fLeft.fMid.IsEmpty() && !r.fRight.fMid.IsEmpty())  r.fLeft.fMid = Ascii("|");
				if (r.fRight.fMid.IsEmpty() && !r.fLeft.fMid.IsEmpty()) r.fRight.fMid = Ascii("|");
			}
			else if (ch.fAttrKind == kKCMStoryAttrRuby)
			{
				r.fLeft.fRuby = ch.fOtherRuby;   r.fLeft.fRubyGroup = ch.fOtherRubyGroup;
				r.fRight.fRuby = ch.fRuby;       r.fRight.fRubyGroup = ch.fRubyGroup;
			}
			else if (ch.fAttrKind == kKCMStoryAttrKenten)
			{
				// The value is a KIND NAME ("BlackCircle"); a kind this build can write is drawn
				// as the mark itself, any other (a custom mark) is named after the text.
				int16 kind = 0;
				if (!ch.fOtherRuby.IsEmpty())
				{
					if (KCMKentenKindOf(ch.fOtherRuby, kind)) r.fLeft.fKentenKind = kind;
					else { r.fLeft.fNote = Ascii(" ["); r.fLeft.fNote.Append(ch.fOtherRuby); r.fLeft.fNote.Append("]"); }
				}
				if (!ch.fRuby.IsEmpty())
				{
					if (KCMKentenKindOf(ch.fRuby, kind)) r.fRight.fKentenKind = kind;
					else { r.fRight.fNote = Ascii(" ["); r.fRight.fNote.Append(ch.fRuby); r.fRight.fNote.Append("]"); }
				}
			}
			else	// footnote / endnote: the value is the number the page prints
			{
				r.fLeft.fNote = ch.fOtherRuby;
				r.fRight.fNote = ch.fRuby;
			}
			out.push_back(r);
			if (static_cast<int32>(out.size()) >= kMaxTableRows)
				break;
		}
		if (static_cast<int32>(out.size()) >= kMaxTableRows)
		{
			KCMReportRow more;
			more.fLeft.fMid = Ascii("... (the table stops here)");
			out.push_back(more);
			break;
		}
	}
}

/** The Resources rows as table rows: a heading row per definition, then one row per differing
    attribute - the older value on the left, the newer on the right, shortened as the panel
    shortens them. */
void BuildResourceRows(IDataBase* targetDB, std::vector<KCMReportRow>& out)
{
	out.clear();
	const int32 n = KCMResourceStore::GetChangeCount();
	for (int32 i = 0; i < n; ++i)
	{
		PMString kind, key;
		KCMResourceChangeKind what = kKCMResourceChanged;
		if (!KCMResourceStore::GetNthChange(i, kind, key, what))
			continue;
		// The definition row as the panel shows it: Kind | Δ | the key (its escapes read).
		KCMReportRow head;
		head.fHeading = kTrue;
		head.fLabel = kind;
		head.fSign = (what == kKCMResourceAdded) ? KCMReportSign::Plus()
				   : (what == kKCMResourceRemoved) ? KCMReportSign::Minus()
				   : KCMReportSign::NotEqual();
		head.fLeft.fMid.SetUTF8String(KCMDecodePercentEscapes(key.GetUTF8String()));
		head.fLeft.fMid.SetTranslatable(kFalse);
		out.push_back(head);

		const int32 attrs = KCMResourceStore::GetNthAttrCount(i);
		if (attrs == 0)
		{
			KCMReportRow r;
			r.fSign = head.fSign;
			if (what == kKCMResourceAdded)        { r.fLeft.fMid = Ascii("-"); r.fRight.fMid = Ascii("(new definition)"); }
			else if (what == kKCMResourceRemoved) { r.fLeft.fMid = Ascii("(definition removed)"); r.fRight.fMid = Ascii("-"); }
			else                                  { r.fLeft.fMid = Ascii("(the difference is inside a child element)"); }
			out.push_back(r);
		}
		for (int32 j = 0; j < attrs; ++j)
		{
			PMString name, source, target;
			if (!KCMResourceStore::GetNthAttr(i, j, name, source, target))
				continue;
			// The attribute row as the panel's child rows: the name in the first column, then the
			// sign (+ the Source lacks it, - the Target lacks it, ≠ both have it and differ).
			KCMReportRow r;
			r.fLabel = name;
			r.fSign = source.IsEmpty() ? KCMReportSign::Plus()
					: target.IsEmpty() ? KCMReportSign::Minus()
					: KCMReportSign::NotEqual();
			// The value as the panel shows it, then the document's own unit beside it (Q, mm ...).
			r.fLeft.fMid  = source.IsEmpty() ? Ascii("-") : KCMResourceValueWithUnit(name, KCMShortResourceValue(source), targetDB);
			r.fRight.fMid = target.IsEmpty() ? Ascii("-") : KCMResourceValueWithUnit(name, KCMShortResourceValue(target), targetDB);
			out.push_back(r);
			if (static_cast<int32>(out.size()) >= kMaxTableRows)
				break;
		}
		if (static_cast<int32>(out.size()) >= kMaxTableRows)
		{
			KCMReportRow more;
			more.fLeft.fMid = Ascii("... (the table stops here)");
			out.push_back(more);
			break;
		}
	}
}

// ---- the pages ---------------------------------------------------------------------------------

/** Lay the report out: the first page, the picture pages, the Story table, the Resources table.
    Every interface on the report document is released when this returns; the caller then
    exports and closes it. */
bool16 BuildReport(IDataBase* reportDB, IDataBase* targetDB, IDataBase* sourceDB,
				   const std::vector<Pair>& pairs, const IDFile& beforePDF, const IDFile& afterPDF,
				   const PMReal& pageW, const PMReal& pageH, const PMString& sourceName,
				   const std::vector<KCMReportRow>& storyRows, const PMString& storyHeading,
				   const std::vector<KCMReportRow>& resourceRows, const PMString& resourceHeading,
				   PMString& why)
{
	SDKLayoutHelper helper;
	const PMString targetName = NameOf(targetDB);

	// The user's PDF place preferences, kept in a command that puts them back when this function
	// returns - by ANY path, a failed SetPlacePage included (the restorer's destructor).
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IWorkspace> workspace(session != nil ? session->QueryWorkspace() : nil);
	InterfacePtr<IPDFPlacePrefs> currentPlace(workspace, UseDefaultIID());
	PlacePrefsRestorer restorePlace(currentPlace);

	// ---- the first page: three lines, and the cat's trail (the user's asks) ---------------------
	{
		PMRect page;
		UIDRef layer;
		if (!KCMReportPageAt(helper, reportDB, 0, page, layer))
		{
			why = Ascii("the report document has no first page");
			return kFalse;
		}
		// The cat's trail goes down FIRST, so that everything typed after it lies on top - the
		// user's call (2026-09-13): "the paws at the very bottom, the words over them, then an
		// overlap does no harm".
		KCMReportDrawPawTrail(reportDB, layer, page);

		PMString text;
		text.SetTranslatable(kFalse);
		text.Append("Kohaku Change Marker - Before / After PDF Report");
		EndParagraph(text);
		text.Append("Before (Source): "); text.Append(sourceName); EndParagraph(text);
		text.Append("After (Target): ");  text.Append(targetName); EndParagraph(text);
		if (KCMGetCompareMode() != kKCMModePixel)
		{
			// The pictures cannot be borrowed the way the two tables are (the file comment says why).
			EndParagraph(text);
			text.Append("Pixel: not compared in this mode (only added / removed pages are shown)");
			EndParagraph(text);
		}
		// At twice the heading size (the user's ask, 2026-09-13: "only the first page's words, twice").
		KCMReportTypeAt(helper, layer, PMRect(page.Left() + kKCMReportGutter, page.Top() + kKCMReportGutter, page.Right() - kKCMReportGutter, page.Top() + kKCMReportGutter + 6 * kKCMReportHeadingPt * 3.2), text, kKCMReportHeadingPt * 2);

		// (A "Before" / "After" stood at the foot of this page for an hour that day; the user
		//  moved the two words to the top of every picture page instead.)

		// When the report was written, at the very foot, on the right (the user's asks, 2026-09-13).
		{
			char stamp[64] = { 0 };
			time_t now = ::time(nil);
			struct tm local;
			::localtime_s(&local, &now);
			::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
			PMString made = Ascii("Exported: ");
			made.Append(stamp);
			KCMReportTypeAt(helper, layer, PMRect(page.Left() + kKCMReportGutter, page.Bottom() - kKCMReportGutter - kKCMReportCaptionH, page.Right() - kKCMReportGutter, page.Bottom() - kKCMReportGutter), made, kKCMReportHeadingPt, kTrue /*right*/);
		}
	}

	// ---- one page per pair: "Before" / "After" over the two pictures, and nothing else --------
	const IDFile beforeFile = beforePDF;
	const IDFile afterFile = afterPDF;
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		PMRect page;
		UIDRef layer;
		if (!KCMReportPageAt(helper, reportDB, static_cast<int32>(i) + 1, page, layer))
		{
			why = Ascii("the report document ran out of pages");
			return kFalse;
		}
		const Pair& pair = pairs[i];
		const PMReal top = page.Top() + kKCMReportHeaderBand;
		const PMRect leftBox (page.Left() + kKCMReportGutter,              top, page.Left() + kKCMReportGutter + pageW,              top + pageH);
		const PMRect rightBox(page.Left() + 2 * kKCMReportGutter + pageW,  top, page.Left() + 2 * kKCMReportGutter + 2 * pageW,      top + pageH);
		KCMReportTypeAt(helper, layer, PMRect(leftBox.Left(),  page.Top() + 12, leftBox.Right(),  page.Top() + kKCMReportHeaderBand - 4), Ascii("Before"), kKCMReportHeadingPt);
		KCMReportTypeAt(helper, layer, PMRect(rightBox.Left(), page.Top() + 12, rightBox.Right(), page.Top() + kKCMReportHeaderBand - 4), Ascii("After"),  kKCMReportHeadingPt);
		if (pair.fSource != kInvalidUID && pair.fBefore > 0)
		{
			if (!SetPlacePage(pair.fBefore, currentPlace, why))
				return kFalse;
			helper.PlaceFileInFrame(beforeFile, layer, leftBox, kSuppressUI);
		}
		if (pair.fTarget != kInvalidUID && pair.fAfter > 0)
		{
			if (!SetPlacePage(pair.fAfter, currentPlace, why))
				return kFalse;
			helper.PlaceFileInFrame(afterFile, layer, rightBox, kSuppressUI);
		}
	}

	// ---- the Story table, then the Resources table ----------------------------------------------
	int32 next = static_cast<int32>(pairs.size()) + 1;
	if (!KCMReportWriteTable(reportDB, next, storyHeading, Ascii("ID"), storyRows, next, why))
		return kFalse;
	if (!KCMReportWriteTable(reportDB, next, resourceHeading, Ascii("Kind"), resourceRows, next, why))
		return kFalse;

	return kTrue;		// the place preferences go back as restorePlace leaves scope
}

PMString SuggestedReportName(IDataBase* targetDB);

/** Where the report goes: the user chooses (a save dialog, as the TSV export raises one), with
    "<Target name>.compare-report.pdf" offered as the name. kFalse with an empty `why` when the
    dialog was cancelled - the caller says "cancelled" and nothing else.
    **This is a file dialog raised from the model half**, on the same grounds as the TSV export's
    (KCMChangedPagesTSV.cpp): SDKFileSaveChooser is a helper from sdksamples/common, not a boss of
    a UI plug-in, and this path is entered from the flyout only, never from a drawing thread. */
bool16 ReportPath(IDataBase* targetDB, IDFile& outFile, PMString& why)
{
	why.Clear();
	SDKFileSaveChooser chooser;
	chooser.SetTitle(Ascii("Export Before/After PDF Report"));
	chooser.SetFilename(SuggestedReportName(targetDB));
	chooser.AddFilter('CARO', 'PDF ', "pdf", Ascii("PDF (pdf)"));
	chooser.ShowDialog();
	if (!chooser.IsChosen())
		return kFalse;
	outFile = chooser.GetIDFile();
	return kTrue;
}

/** "<Target name>.compare-report.pdf" - the name the save dialog opens with. */
PMString SuggestedReportName(IDataBase* targetDB)
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
	return stem;
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
	// occasion (its page pairing works through the labels the copy's pages carry; while the copy
	// stands, the uid translators know it, so the borrowed story diff reads it as the run did).
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

	// ⚠A DATABASE THAT IS NOT A SESSION DOCUMENT IS NOT EXPORTED. A Source lent by Kohaku InDesign
	//   MCP is a database CLONE, and ExportINX on a clone was measured to kill InDesign outright
	//   (kcm-clone-export-and-compression-2026-09-09.md); the PDF export has not been tried on one
	//   and is not going to be tried here. The rehydrated task-start copy IS a session document.
	if (!IsSessionDocument(sourceDB))
	{
		outMessage = Ascii("the Source is a lent copy (not an open document), so it cannot be exported for the Before side.");
		return kFalse;
	}

	std::vector<Pair> pairs;
	CollectPairs(targetDB, sourceDB, pairs);

	// Where it goes - asked FIRST, so a cancel costs nothing (no export, no diff, no rehydration wasted).
	IDFile reportFile;
	{
		PMString why;
		if (!ReportPath(targetDB, reportFile, why))
		{
			outMessage = why.IsEmpty() ? Ascii("Report cancelled.") : why;
			return kFalse;
		}
	}

	// ---- the two tables' rows, read while the borrowed results stand --------------------------
	// (Both loans give back on leaving this block, before any document is exported or closed.)
	std::vector<KCMReportRow> storyRows, resourceRows;
	PMString storyHeading, resourceHeading;
	{
		StoryDetailLoan storyLoan(targetDB, sourceDB, (KCMGetCompareMode() != kKCMModeStory) ? kTrue : kFalse);
		int32 stories = 0, edits = 0;
		if (storyLoan.WasCancelled())
		{
			KCMReportRow r;
			r.fLeft.fMid = Ascii("(the story comparison was cancelled)");
			storyRows.push_back(r);
			storyHeading = Ascii("Story Changes (cancelled)");
		}
		else
		{
			BuildStoryRows(targetDB, sourceDB, storyRows, stories, edits);
			storyHeading = Ascii("Story Changes: ");
			storyHeading.AppendNumber(stories); storyHeading.Append(stories == 1 ? " story, " : " stories, ");
			storyHeading.AppendNumber(edits);   storyHeading.Append(edits == 1 ? " edit" : " edits");
		}

		ResourceLoan resourceLoan(targetDB, sourceDB);
		BuildResourceRows(targetDB, resourceRows);	// the Target's units name the brackets
		resourceHeading = Ascii("Resources Changes: ");
		{
			PMString summary;
			KCMResourceStore::GetSummary(summary);
			resourceHeading.Append(summary);
		}
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
	// THE MARKS GO ON THE BEFORE SIDE (the user's ask, 2026-09-13): the older version carries the
	// rings of the changed pages and the "/" of the removed ones, the newer version is shown clean.
	// The drawing marks a Source page only when the document is the one it knows as sSrcDB and the
	// page is in sSrcPageToTarget - which a task-start copy is not, having been detached and closed
	// right after the comparison. So for the copy rehydrated here, the two (and the Source-side
	// overflow set) are LENT for the length of the Before export and put back to the detached state
	// afterwards. The overflow cache is stamped as built for this Source first, so no draw rebuilds
	// it against the copy and then empties it against nil (which would cost the Target its "/").
	const bool16 printMarksWas = KCMDrawEventHandler::sPrintMarks;
	// The story ID labels follow the report on its After side while their toggle is on (the user's
	// call, 2026-09-13; the Before side, marked by sRingFrameOff, gets none - a Task Start copy's
	// UIDs are renumbered): this flag is what tells them an export is the report's, since the
	// After side is exported with sPrintMarks off. Reset below, next to sPrintMarks.
	KCMDrawEventHandler::sReportExport = kTrue;
	{
		IDataBase::SaveRestoreModifiedState targetGuard(targetDB);
		IDataBase::SaveRestoreModifiedState sourceGuard(sourceDB);

		const bool16 lend = (KCMDrawEventHandler::sSrcDB == nil) ? kTrue : kFalse;
		if (lend)
		{
			std::vector<UID> tp, sp, tov, sov;
			KCMBuildPairing(targetDB, sourceDB, tp, sp, &tov, &sov);
			KCMMarkStateLock lock(KCMMarkStateMutex());
			KCMDrawEventHandler::sSrcDB = sourceDB;
			KCMDrawEventHandler::sOverflowCacheSrcDB = sourceDB;
			KCMDrawEventHandler::sSrcPageToTarget.clear();
			for (size_t i = 0; i < sp.size() && i < tp.size(); ++i)
				KCMDrawEventHandler::sSrcPageToTarget[sp[i]] = tp[i];
			KCMDrawEventHandler::sOverflowS.clear();
			KCMDrawEventHandler::sOverflowS.insert(sov.begin(), sov.end());
		}
		if (!beforePages.empty())
		{
			// The marks, without the frame along the page edge (the user's ask, 2026-09-13): only
			// the rings around what changed. The ring images are cached, so both flips invalidate.
			KCMDrawEventHandler::sPrintMarks = kTrue;		// the older version with the marks
			KCMDrawEventHandler::sRingFrameOff = kTrue;
			KCMDrawEventHandler::InvalidateRingCache();
			ok = ExportPagesToPDF(sourceDB, beforePages, beforePDF, why);
			KCMDrawEventHandler::sRingFrameOff = kFalse;
			KCMDrawEventHandler::InvalidateRingCache();
		}
		if (lend)
		{
			KCMMarkStateLock lock(KCMMarkStateMutex());
			KCMDrawEventHandler::sSrcDB = nil;
			KCMDrawEventHandler::sOverflowCacheSrcDB = nil;
			KCMDrawEventHandler::sSrcPageToTarget.clear();
			KCMDrawEventHandler::sOverflowS.clear();
		}
		if (ok && !afterPages.empty())
		{
			KCMDrawEventHandler::sPrintMarks = kFalse;		// the newer version clean
			ok = ExportPagesToPDF(targetDB, afterPages, afterPDF, why);
		}
		KCMDrawEventHandler::sPrintMarks = printMarksWas;
	}
	KCMDrawEventHandler::sReportExport = kFalse;

	// ---- the report document ---------------------------------------------------------------
	// Made with the first page and the picture pages; the two tables append their own.
	UIDRef reportDoc = UIDRef::gNull;
	if (ok)
	{
		SDKLayoutHelper helper;
		const PMReal reportW = 2 * pageW + 3 * kKCMReportGutter;
		const PMReal reportH = pageH + kKCMReportHeaderBand + kKCMReportCaptionH + 2 * kKCMReportGutter;
		reportDoc = helper.CreateDocument(kSuppressUI, reportW, reportH, static_cast<int32>(pairs.size()) + 1, 1, 0);
		if (reportDoc == UIDRef::gNull)
		{
			why = Ascii("the report document could not be created");
			ok = kFalse;
		}
	}
	if (ok)
		ok = BuildReport(reportDoc.GetDataBase(), targetDB, sourceDB, pairs, beforePDF, afterPDF, pageW, pageH, sourceName,
						 storyRows, storyHeading, resourceRows, resourceHeading, why);
	int32 pageCount = 0;
	if (ok)
	{
		std::vector<UID> all;
		KCMCollectPageUIDs(reportDoc.GetDataBase(), all);
		pageCount = static_cast<int32>(all.size());
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
	outMessage.AppendNumber(pageCount);
	outMessage.Append(" pages -> ");
	{
		PMString path;
		FileUtils::IDFileToPMString(reportFile, path);
		outMessage.Append(path);
	}
	return kTrue;
}

// End, KCMReport.cpp.
