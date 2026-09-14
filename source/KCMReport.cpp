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

#include <windows.h>				// ShellExecuteW - opening the finished report. (GetTempPathW and DeleteFileW went with the temporary PDFs, 2026-09-14)
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
// (IPDFPlacePrefs.h was included for SetPlacePage / PlacePrefsRestorer, both gone 2026-09-14.)
#include "IPDFPostProcessPrefs.h"
#include "IPDFSecurityPrefs.h"
#include "ISession.h"
#include "ISysFileData.h"
#include "SDKFileHelper.h"			// SDKFileSaveChooser - where the report goes is the user's choice
#include "IUIFlagData.h"
// (IWorkspace.h went with them: the report no longer reads the session's place preferences.)
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
#include "KCMProgressBar.h"		// KCMDeferredProgressBar - the bar that appears only after three seconds
#include "KCMReportTable.h"		// the page helpers and the table sections
#include "KCMReportPlace.h"		// ★one page into the report, as a PDF held in memory - no file anywhere
#include "KCMReportPaws.h"			// the cat's trail on the first page
#include "KCMCore.h"				// KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB / KCMCollectPageUIDs / KCMGetCompareMode
#include "KCMComparisonRun.h"		// KCMToggleStartStop / KCMCanStartComparison - the report runs the comparison it needs
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
#include "KCMResourceDisplay.h"		// KCMResourceDisplayValue - what a value reads as, the same as the panel
#include "KCMXmlPretty.h"			// KCMDecodePercentEscapes - a definition's key, readable
#include "KCMModelNotify.h"		// KCMNotify - the panel is told when the borrowed Resources result goes
#include "KCMBoundaryID.h"			// kKCMStoryEditsRebuiltMessage
#include "KCMExternalSource.h"		// KCMExternalSourceLabel - a lent Source's name

namespace
{

const int32 kMaxTableRows = 400;	// rows of one table section before "... and N more"

/** One report page: a Target page (kInvalidUID for a removed page) and its partner on the
    report's Source (kInvalidUID for an added page).
    ⚠fBefore/fAfter (the 1-based page numbers inside the two temporary PDFs) went with the
      temporary PDFs on 2026-09-14: each page is exported at the moment it is placed, so there
      is no file to number a page of. */
struct Pair
{
	UID		fTarget;
	UID		fSource;
	Pair(UID t, UID s) : fTarget(t), fSource(s) {}
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

// (TempPDF() stood here - it built the "<TEMP>\KCM-before-<tick>.pdf" names. **There are no
//  temporary files anywhere in this feature since 2026-09-14**, so it had no caller left.)

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

// (SetPlacePage() and PlacePrefsRestorer stood here. Both existed for ONE reason: the old route
//  placed a temporary PDF FILE, and a file has to be told which of its pages to take and how to
//  crop it - and the user's own place preferences had to be put back afterwards. The picture now
//  arrives as a page item that is already exactly one page (KCMReportPlace.cpp), so nothing here
//  reads or writes those preferences any more, and there is nothing to restore.)

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
			// The value as the panel shows it - shortened, in the document's own unit, and a
			// keyboard shortcut spelled the way InDesign spells it. ★ONE call rather than a chain
			// repeated here and in the panel: KCMResourceDisplay.h carries why.
			r.fLeft.fMid  = source.IsEmpty() ? Ascii("-") : KCMResourceDisplayValue(name, source, targetDB);
			r.fRight.fMid = target.IsEmpty() ? Ascii("-") : KCMResourceDisplayValue(name, target, targetDB);
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
				   const std::vector<Pair>& pairs,
				   const PMReal& pageW, const PMReal& pageH, const PMString& sourceName,
				   const std::vector<KCMReportRow>& storyRows, const PMString& storyHeading,
				   const std::vector<KCMReportRow>& resourceRows, const PMString& resourceHeading,
				   KCMDeferredProgressBar& bar, int32& units, PMString& why)
{
	SDKLayoutHelper helper;
	const PMString targetName = NameOf(targetDB);

	// (The user's PDF PLACE preferences used to be saved and restored around this function: the
	//  old route placed a file and had to say which page of it to take, with kCropToMedia for the
	//  sheet. Nothing is placed from a file any more - the picture arrives as a page item through
	//  KCMPlacePageIntoReport - so those preferences are neither read nor changed, and there is
	//  nothing to put back.)

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
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		// ★The bar's page units, and **the only place a cancel is read during the placing**: a page
		//   goes down as two exports and nothing inside them can be interrupted, so the question is
		//   asked between pages (the same rule as the comparison's own loop).
		{
			PMString step(Ascii("Page "));
			step.SetTranslatable(kFalse);
			step.AppendNumber(static_cast<int32>(i) + 1);
			step.Append(" of ");
			step.AppendNumber(static_cast<int32>(pairs.size()));
			bar.Step(units++, step);
			if (bar.WasCancelled())
			{
				why = Ascii("cancelled");	// the caller asks the bar itself, not this word
				return kFalse;
			}
		}
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
		// ★★★NO FILE ANYWHERE (2026-09-14). Each side is exported to a PDF held in MEMORY and
		//   comes straight back in as a page item - KCMReportPlace.cpp, and its header says what
		//   had to be measured before this line could be written. The Before side carries the
		//   comparison marks, the After side is shown clean (the user's ask, 2026-09-13).
		// ⚠What makes the marks reach the Before picture is sMarksOnPage, which
		//   KCMPlacePageIntoReport raises for the length of the export: this export draws items
		//   and never draws a spread, and the marks are drawn once per spread.
		if (pair.fSource != kInvalidUID)
		{
			if (!KCMPlacePageIntoReport(sourceDB, pair.fSource, layer, leftBox, kTrue /*with the marks*/, why))
				return kFalse;
		}
		if (pair.fTarget != kInvalidUID)
		{
			if (!KCMPlacePageIntoReport(targetDB, pair.fTarget, layer, rightBox, kFalse /*clean*/, why))
				return kFalse;
		}
	}

	// ---- the Story table, then the Resources table ----------------------------------------------
	int32 next = static_cast<int32>(pairs.size()) + 1;
	// ⚠One unit per TABLE, not per row: a table is written in one call that flows its own pages, so
	//   there is no safe point inside it to step or to ask about a cancel.
	bar.Step(units++, Ascii("Story changes table"));
	if (bar.WasCancelled())
	{
		why = Ascii("cancelled");
		return kFalse;
	}
	if (!KCMReportWriteTable(reportDB, next, storyHeading, Ascii("ID"), storyRows, next, why))
		return kFalse;
	bar.Step(units++, Ascii("Resources changes table"));
	if (bar.WasCancelled())
	{
		why = Ascii("cancelled");
		return kFalse;
	}
	if (!KCMReportWriteTable(reportDB, next, resourceHeading, Ascii("Kind"), resourceRows, next, why))
		return kFalse;

	return kTrue;		// the place preferences go back as restorePlace leaves scope
}

PMString SuggestedReportName(IDataBase* targetDB);

/** Where the report goes: the user chooses (a save dialog, as the TSV export raises one), with
    "<Target name>.compare-report.pdf" offered as the name. kFalse with an empty `why` when the
    dialog was cancelled - the caller says "cancelled" and nothing else.
    **This is a file dialog raised from the model half**, and the grounds are these: SDKFileSaveChooser
    is a helper from sdksamples/common, not a boss of a UI plug-in, and this path is entered from the
    flyout only, never from a drawing thread. ⚠(The TSV export was named here as the precedent for the
    same move. It went on 2026-09-14, and this is now the only place the reasoning lives.) */
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

	// ★★**Not comparing yet? Run the comparison first** (the user's instruction, 2026-09-14: "let it
	//   be pressed whenever a Target and a Source are there, and run whatever comparison it needs" -
	//   before that this said "Start a comparison first." and gave up).
	//   KCMToggleStartStop IS the flyout's own Start while nothing is armed, so the pair this report
	//   ends up describing is exactly the pair a Start would have chosen - chosen Target/Source
	//   where the reader set them, the automatic rule where they did not.
	// ⚠★★**The comparison it runs STAYS ARMED.** It is not borrowed and given back the way the Story
	//   and the Resources results are further down: a pixel comparison rebuilds the rings, the page
	//   pairing and the overflow caches, and there is no earlier state to put back (the same reason
	//   the report cannot show pixel pages in the other two modes). So pressing this before a Start
	//   leaves the marks on screen, which is also what a reader who asked for a Before/After report
	//   would expect to see.
	if (!KCMIsArmed() || KCMArmedTargetDB() == nil)
	{
		// ⚠Asked through the resolver, not by counting documents: KCMComparisonRun.h promises this
		//   goes through the same one as the toggle's start branch, so "the item was live" and "the
		//   start found a pair" cannot disagree ([[one-question-one-place]]).
		if (!KCMCanStartComparison())
		{
			outMessage = Ascii("Two open documents are needed for a report.");
			return kFalse;
		}
		KCMToggleStartStop();		// nothing is armed, so this is the START branch
	}
	IDataBase* const targetDB = KCMArmedTargetDB();
	if (!KCMIsArmed() || targetDB == nil)
	{
		// The start did not arm: it was cancelled at its progress bar, or it refused the pair.
		// Its own words are already on the status line, so this says only what did not happen.
		outMessage = Ascii("Report cancelled.");
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

	// (Two lists of page UIDs stood here, in export order, so that each pair could remember WHICH
	//  PAGE of the temporary PDF was its own. Nothing is numbered any more: a pair's page is
	//  exported at the moment it is placed, and the pair already knows its UID.)

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

	// ---- the progress bar, from here to the end ---------------------------------------------
	// ★★**It covers the PDF work only, and that is the whole of the design.** The two loans above
	//   raise a bar of their own (the Story comparison's), and **two KCMDeferredProgressBars must
	//   not be alive at once**: the second one's bar is refused registration and, worse, the first
	//   one's Cancel stops being readable (KCMProgressBar.h's warning, measured 2026-09-05). So this
	//   one is created only after those loans have given back - the reader sees at most two bars in
	//   sequence, never two at a time.
	// ★★★**NO DELAY - this one appears at the first Step** (2026-09-14, measured with the user on a
	//   60-page pair: "the bar came up very late, I want it from the start").
	//   ⚠**THE THREE-SECOND RULE COULD NOT WORK HERE, and the reason is worth keeping.** The delay is
	//   judged INSIDE Step - nowhere else - so a bar appears only when the next Step comes round. The
	//   order below is: Step "Exporting the Before pages" (at 0 ms, too early to show anything), then
	//   **the Before export itself, which is the heaviest thing in the whole report** (60 pages came
	//   to 1,086 KB and took the best part of a minute), and only then the next Step. So the bar sat
	//   invisible through exactly the stretch it was wanted for, and appeared as the work was ending.
	//   ⇒ A report is never the "over before you see it" case the three seconds exist for: it writes
	//     two PDFs and builds a document. It shows at once instead.
	//   ⚠It will not MOVE during that first export - there is no safe point inside ExportPagesToPDF
	//     to step from - but "something is running, and here is its name" is what was missing.
	// Units: the Before export, the After export, the report document, one per report page, the two
	//   tables, and the final write.
	PMString barTitle(Ascii("Export Before/After PDF Report"));
	barTitle.SetTranslatable(kFalse);
	KCMDeferredProgressBar bar(barTitle, static_cast<int32>(pairs.size()) + 5, 0 /*delayMs: show at once*/);
	int32 units = 0;

	// ---- the report document, FIRST ----------------------------------------------------------
	// ⚠**THE ORDER CHANGED WITH THE FILES** (2026-09-14). The old route wrote both documents out
	//   to temporary PDFs and only afterwards had somewhere to put them. Now each page is
	//   exported to MEMORY at the moment it is placed, so the report has to exist first.
	PMString why;
	bool16 ok = kTrue;
	UIDRef reportDoc = UIDRef::gNull;
	{
		bar.Step(units++, Ascii("Building the report document"));
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
		// ★★**THE WHOLE REPORT IS BUILT HERE NOW**, inside the lending and the guards, because
		//   every picture in it is exported at the moment it is placed rather than beforehand.
		//   The marks are drawn without the frame along the page edge (the user's ask,
		//   2026-09-13): only the rings around what changed. The ring images are cached, so both
		//   flips invalidate.
		// ⚠**sPrintMarks is NOT set here.** KCMPlacePageIntoReport raises it - and sMarksOnPage
		//   with it - for the length of each picture's own export, because the Before side
		//   carries the marks and the After side must not.
		if (ok)
		{
			KCMDrawEventHandler::sRingFrameOff = kTrue;
			KCMDrawEventHandler::InvalidateRingCache();
			ok = BuildReport(reportDoc.GetDataBase(), targetDB, sourceDB, pairs, pageW, pageH, sourceName,
							 storyRows, storyHeading, resourceRows, resourceHeading, bar, units, why);
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
		KCMDrawEventHandler::sPrintMarks = printMarksWas;
	}
	KCMDrawEventHandler::sReportExport = kFalse;

	int32 pageCount = 0;
	if (ok)
	{
		std::vector<UID> all;
		KCMCollectPageUIDs(reportDoc.GetDataBase(), all);
		pageCount = static_cast<int32>(all.size());
		// The last unit. ⚠No cancel is read after this one: the write is a single call, and once it
		//   has run the file exists - stopping "after" it would only mean not opening the viewer.
		bar.Step(units++, Ascii("Writing the PDF"));
		ok = ExportPagesToPDF(reportDoc.GetDataBase(), all, reportFile, why);
	}
	CloseReportDocument(reportDoc);
	// (Two ::DeleteFileW calls stood here, for the two temporary PDFs. **There are none to
	//  delete now** - that is the whole of the 2026-09-14 change, seen from this end.)

	if (!ok)
	{
		// ★**The bar itself is asked, never the word in `why`.** A cancel can come from four places
		//   (the two exports here, and the page loop or either table inside BuildReport), and having
		//   each of them spell the same word is exactly the kind of agreement that drifts. The bar
		//   knows whether the button was pressed.
		if (bar.WasCancelled())
			outMessage = Ascii("Report cancelled.");
		else
		{
			outMessage = Ascii("Report failed: ");
			outMessage.Append(why);
		}
		return kFalse;
	}
	// (The bar comes down as this function returns, a moment after the viewer is asked to open the
	//  file. Taking it down first would mean scoping it away from the message above, which is where
	//  the cancel is read.)

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
