//========================================================================================
//
//  KCMChangedPagesTSV.cpp
//
//  The implementation behind the panel flyout's "Export Changed Pages...". The changed pages
//  of the current comparison (after Start) are written as tab-separated text, in the same style
//  as KESCL's KESCLReportSave (KESCLReportPanel::SaveReportAsText). Two columns = Page / Type
//  (Changed / Inserted / Deleted); every label is English. Page is the page's own display name
//  (Changed and Inserted from the Target, Deleted from the Source).
//
//  - Page numbers come from IPageList::GetPageString -- section included, **and the number the
//    Pages panel shows**.
//    **InDesign has TWO page numbers** (measured):
//      (a) the Pages panel / the page-number field / the DOM's page.name /
//          GetPageString(..., kTrue) ... counts pages on hidden spreads too
//      (b) the folio actually composed onto the page / GetPageString(..., kFalse)
//          ... skips hidden spreads
//    This list tells a person which page to go and look at, so it is written with (a). The
//    detail is at PageDisplay.
//    @warning **KCMPageNumberMarker passes two arguments differently**: kFalse for
//      bIncludeSectionName (a real folio carries no section name) and kFalse for
//      bIncludePagesOfHiddenSpread (side (b)). **Both differences come from its purpose --
//      measuring the characters actually printed.** Same function, different question.
//  - **A page on a hidden spread is still listed, with its original number and a "(Hide)"**
//    ("2 (Hide)", the user's specification). The test is KCMIsPageOnHiddenSpread and the mark is
//    added in PageDisplay only.
//    @warning it was once built as "hidden pages are left out", and that produced **pages that
//      had changed but appeared nowhere in the list**. Handing over the number and the state in
//      one column leaves the reader nothing to work out.
//  - The output is UTF-8 + BOM + CRLF (identical to KESCL), so Japanese page names survive
//    Excel and Notepad.
//  - Success is silent; only a failure reaches the status line. Nothing started, or nothing
//    changed, is reported briefly and returns.
//  - **Strings are PMString from end to end.** The old implementation built them with
//    std::wstring and reinterpret_cast<const wchar_t*>(UTF16TextChar*), which only holds where
//    wchar_t is 16 bits. On macOS/clang it is 32, so the same cast reads twice the source buffer
//    (past its end) on top of producing mojibake. PMString stays UTF-16 and platform-
//    independent; every label is ASCII, so Append(const char*) is enough.
//  - **Master-spread pages are listed too, but only as Changed** -- masters are paired BY NAME
//    by KCMBuildMasterPairing, and a master with no counterpart is simply skipped rather than
//    paired, so there is no state matching Inserted/Deleted for them (the user's
//    specification). The Page column holds the master spread's name ("A-Master"), and the two
//    sides of a facing-page master are told apart by a trailing " (1)" / " (2)".
//  - Overset (sOverset*) is never consulted (the user's specification).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPMStream.h"
#include "IPageList.h"			// GetPageString (the displayed page number)
#include "IApplication.h"
#include "IDocumentList.h"		// FindDocByDataBase (liveness without a deref, and the document name)
#include "IDocument.h"			// GetName (suggested filename)
#include "ISession.h"			// GetExecutionContextSession
#include "IDataBase.h"			// GetRootUID
#include "IHierarchy.h"			// GetSpreadUID (master page -> its master spread)
#include "IMasterSpread.h"		// GetName ("A-Master")
#include "ISpread.h"			// GetNumPages / GetNthPageUID (telling the two sides of a facing master apart)

// General includes:
#include "PMString.h"
#include "StreamUtil.h"
#include "SDKFileHelper.h"		// SDKFileSaveChooser (sdksamples/common)

#include <string>
#include <vector>
#include <set>

// Project includes:
#include "KCMCore.h"				// KCMCollectPageUIDs / KCMCollectMasterPageUIDs /
									//   KCMIsPageOnHiddenSpread
// The UI-side header KCMUIShared.h is deliberately NOT included: the status line became a
// return value, so no path from this file calls the UI. The caller (the flyout item) shows the
// message, including the path it was saved to.
#include "KCMDrawEventHandler.h"	// sEntries / sDB / sSrcDB / sOverflowT / sOverflowS -- **all of it
									//   the "as compared" record, i.e. what the screen, the thumbnails and
									//   the map read**
#include "KCMPageMap.h"			// KCMPageMapCollectRegistered
#include "KCMChangedPagesTSV.h"

namespace
{

// Two columns (Page / Type); every label is English, as everywhere else in KCM. Type is
// Changed / Inserted / Deleted. There is no "old page" column: the page name is the page's own
// display name in its document (Changed and Inserted from the Target, Deleted from the Source).
// All ASCII, so char literals are enough.
const char* const kKindChanged  = "Changed";
const char* const kKindInserted = "Inserted";
const char* const kKindDeleted  = "Deleted";
const char* const kHeaderCol1   = "Page";
const char* const kHeaderCol2   = "Type";

// The separators are written with AppendW(UTF32TextChar), as elsewhere in KCM, so that nothing
// depends on how a "\t" / "\n" char literal is interpreted on its way into a PMString. The
// values are built where they are used, rather than in namespace-scope static objects that
// would bring initialisation order with them.
const int kTabCode = 0x09;
const int kLfCode  = 0x0A;

// One row (display name + kind). `page` comes from the document, so it is a PMString (UTF-16);
// `kind` only points at one of the fixed ASCII literals above, so it is not copied.
struct KCMChangeRow
{
	PMString    page;
	const char* kind;
	KCMChangeRow() : kind(kKindChanged) { page.SetTranslatable(kFalse); }
};

// One row, appended. **The four lines were written out four times** (Changed / Inserted /
// the master spreads / Deleted). What that risked is not the four lines but the PAIR: the page
// name and the kind have to be set together, and a fifth kind would have been a fifth place to
// remember both. The kind is one of the fixed literals above, so it is not copied.
void AppendRow(std::vector<KCMChangeRow>& rows, const PMString& page, const char* kind)
{
	KCMChangeRow row;
	row.page = page;
	row.kind = kind;
	rows.push_back(row);
}

// The status message (English and non-translatable, as in KESCL and the rest of KCM).
// **It is remembered for the caller rather than written to the status line.** A TSV export
// naturally answers with "did it work, and where did it go", and has no reason to raise a
// notification; this half is the model, and the flyout item that called it does the showing.
// The name and the call sites are unchanged, so every path in the body -- early returns
// included -- still reaches it the same way.
PMString gExportMessage;

void ShowStatus(const char* text)
{
	gExportMessage = PMString(text);
	gExportMessage.SetTranslatable(kFalse);
}

// The page number the Pages panel shows for pageUID (in db). Empty when it cannot be had.
// Arguments: bIncludeSectionName = kTrue ... the section name is included ("A:12"), because a
//   list read by a person is better off saying which section the 12 is in.
// bUseIntegerStyle = kFalse ... keep the section's own numbering style (roman numerals and the
//   like appear exactly as on screen).
//
// **bIncludePagesOfHiddenSpread = kTrue** (the default), changed from kFalse once it was
//   measured that **InDesign keeps two page numbers**:
//     - the Pages panel / the status bar's page field / the script DOM's page.name /
//       GetPageString(..., kTrue) ...... **counts pages on hidden spreads** (hiding one leaves
//       the others' numbers alone)
//     - the automatic page-number marker composed onto the page /
//       GetPageString(..., kFalse) ...... **skips hidden spreads** (hide the first spread and
//       the second page prints "1"; confirmed in a screenshot)
//   The old comment called kFalse "the number on screen", but **the screen is the kTrue side**.
//   This list tells a person which page to go and look at, and they will look in the Pages
//   panel, so it is written with the Pages panel's number.
//   **Left at kFalse, the standard way of using KCM broke it**: hide the unchanged spreads,
//     then export, and changed pages 2 and 3 came out as "1, 2" -- each one off by one
//     (measured).
//   @warning **KCMPageNumberMarker.cpp is right to stay at kFalse**: it measures the ink extent
//     of the digits actually printed, so it has to count the way the real folio counts. The
//     asymmetry is deliberate.
PMString PageDisplay(IDataBase* db, UID pageUID)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (db == nil || pageUID == kInvalidUID)
		return out;
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
		return out;
	pageList->GetPageString(pageUID, &out, kTrue, kFalse, kDefaultPageType, kTrue, kTrue);
	out.SetTranslatable(kFalse);	// set again, in case GetPageString put it back

	// **A page on a hidden spread is listed with its original number, plus a mark saying it is
	//   hidden** ("2 (Hide)", the user's specification), so that a reader who goes looking for
	//   page 2 and does not find it is not left stuck. The number and the state travel in the
	//   same column. The number itself is the one from before it was hidden.
	// @warning **there is no worry about the column ceasing to be numeric** -- master-spread rows
	//   have always been strings like "A-Master (2)"; this column was never a number column.
	// The mark is spelled "(Hide)", the same as in the panel's Prev/Next status line (one
	//   spelling, not two).
	if (KCMIsPageOnHiddenSpread(db, pageUID))
		out.Append(" (Hide)");
	return out;
}

// The display name of a master page (in db): IMasterSpread::GetName, e.g. "A-Master". A
// position inside the spread -- " (1)" / " (2)" -- is appended only when the master spread holds
// more than one page: the two sides of a facing master share a single name in InDesign, so if
// both pages changed the same string would appear twice (the user's specification). Returns
// empty for anything that is not a master page, or whose name cannot be had, and the caller
// then falls back to PageDisplay.
//
// It is separate from PageDisplay (GetPageString) because GetPageString on a master page
// returns only the prefix ("A"): that says which master, but not that it is one.
// IPageList's bAbbreviate parameter does mention the long form ("A-Master"), but that is about
// passing a **spreadUID**; it has no effect for a page UID.
PMString MasterPageDisplay(IDataBase* db, UID pageUID)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (db == nil || pageUID == kInvalidUID)
		return out;

	// Page -> the spread it sits on. IHierarchy::GetSpreadUID is contracted to return "the spread
	// of this hierarchy node" and is not restricted to pages (the same way KCMPeek and
	// KCMChangeNav ask).
	InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
	if (pageHier == nil)
		return out;
	const UID spreadUID = pageHier->GetSpreadUID();
	if (spreadUID == kInvalidUID)
		return out;

	// An ordinary spread has no IID_IMASTERSPREAD, so this Query doubles as the "is it a master" test.
	InterfacePtr<IMasterSpread> master(db, spreadUID, UseDefaultIID());
	if (master == nil)
		return out;
	master->GetName(&out);
	// This is document data (a name the user gave), so it must not be treated as a translation
	// key. Cleared after the call in case GetName raises the flag, as in PageDisplay.
	out.SetTranslatable(kFalse);
	if (out.NumUTF16TextChars() == 0)
		return out;

	const int32 posBase = 1;	// numbers shown to people start at 1
	InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
	if (spread != nil)
	{
		const int32 np = spread->GetNumPages();
		if (np > 1)
		{
			for (int32 p = 0; p < np; ++p)
			{
				if (spread->GetNthPageUID(p) == pageUID)
				{
					out.Append(" (");
					out.AppendNumber(p + posBase);
					out.Append(")");
					break;
				}
			}
		}
	}
	return out;
}

// Replace the characters a file name cannot hold with '-' -- the same nine as KESCL's
// SanitizeForFileName. Windows' forbidden set is the basis; macOS forbids only '/' and ':',
// so this set is the safe side on both. Copied one UTF-16 unit at a time, so surrogate pairs
// survive.
PMString SanitizeForFileName(const PMString& part)
{
	PMString out;
	out.SetTranslatable(kFalse);
	const int32 n = part.NumUTF16TextChars();
	const UTF16TextChar* b = part.GrabUTF16Buffer(nil);
	if (b == nil)
		return out;
	for (int32 i = 0; i < n; ++i)
	{
		const UTF16TextChar c = b[i];
		const bool16 bad = (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?'
			|| c == '"' || c == '<' || c == '>' || c == '|') ? kTrue : kFalse;
		out.AppendW(UTF32TextChar(bad ? (UTF16TextChar)'-' : c));
	}
	return out;
}

// The display name of the document owning db, extension included (for the suggested file
// name). Empty when it cannot be had. The resolution path is KCM's usual one:
// session -> app -> docList -> FindDocByDataBase.
PMString DocNameFromDB(IDataBase* db)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (db == nil)
		return out;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return out;
	IDocument* d = docList->FindDocByDataBase(db);	// no ref -- the name is read without a deref
	if (d == nil)
		return out;
	d->GetName(out);
	out.SetTranslatable(kFalse);
	return out;
}

// Suggested file name: "KohakuChangeMarker_ChangedPages_<targetDoc>.txt", or
// "KohakuChangeMarker_ChangedPages.txt" when the target's name cannot be had. The extension
// (.indd and the like) is dropped.
PMString BuildSuggestedFileName(IDataBase* targetDB)
{
	PMString result;
	result.SetTranslatable(kFalse);
	result.Append("KohakuChangeMarker_ChangedPages");

	// Drop the extension (everything after the last '.'). A leading '.' is kept -- that is a
	// hidden-file name, not an extension.
	const PMString doc = DocNameFromDB(targetDB);
	const int32 n = doc.NumUTF16TextChars();
	const UTF16TextChar* b = doc.GrabUTF16Buffer(nil);
	if (n > 0 && b != nil)
	{
		int32 cut = n;
		for (int32 i = n - 1; i > 0; --i)
			if (b[i] == '.') { cut = i; break; }
		PMString stem;
		stem.SetTranslatable(kFalse);
		for (int32 i = 0; i < cut; ++i)
			stem.AppendW(UTF32TextChar(b[i]));
		if (stem.NumUTF16TextChars() > 0)
		{
			result.Append("_");
			result.Append(SanitizeForFileName(stem));
		}
	}
	result.Append(".txt");
	return result;
}

// Collect the changed-page rows; kTrue when there is at least one. Order: the Target in
// document order (Changed + Inserted), then the Source in document order (Deleted).
bool16 CollectRows(IDataBase* targetDB, IDataBase* sourceDB, std::vector<KCMChangeRow>& rows)
{
	// The overflow sets that Inserted/Deleted are classified from.
	// **This reads the very cache the drawing uses.** The header of this file always declared
	// "Inserted = sOverflowT / Deleted = sOverflowS", but the implementation used to call
	// KCMBuildPairing here and work the overflow out **from the current document structure**. The
	// cache is frozen at the moment of the comparison (KCMDrawEventHandler.h: "does not follow
	// bare page insertions or deletions; fixed until the next Start or re-comparison, the same
	// way the frames behave"), so unless the user re-compared after adding pages, **pages
	// carrying no "/" on screen were written out as Inserted**. The list should be a copy of the
	// screen, so it reads the same set.
	// A side effect: KCMBuildPairing's tPairs/sPairs return values **were never used** once the
	//   old page column went, so a full walk of both documents' pages disappeared with the call.
	// @warning EnsureOverflowCache does nothing while the recorded (sDB, sSrcDB) still match the
	//   current ones. The caller (KCMExportChangedPagesTSVRun) takes targetDB/sourceDB from those
	//   two, so they always do.
	KCMDrawEventHandler::EnsureOverflowCache();
	const std::set<UID>& overflowT = KCMDrawEventHandler::sOverflowT;
	const std::set<UID>& overflowS = KCMDrawEventHandler::sOverflowS;

	// The manual registrations (Added on the Target side, Removed on the Source side).
	std::set<UID> registeredT, registeredS;
	KCMPageMapCollectRegistered(targetDB, registeredT);
	KCMPageMapCollectRegistered(sourceDB, registeredS);

	// ---- The Target in document order: Changed, then Inserted ----
	// **Pages on hidden spreads are listed too** (the user's specification). This was once built
	// as "the user hid them, so leave them out", and leaving them out **produced pages that had
	// changed and appeared nowhere**. They stay in, and PageDisplay adds "(Hide)" -- with the
	// number unchanged from before the hiding, i.e. the Pages panel's. The test lives in
	// PageDisplay and nowhere else.
	std::vector<UID> targetOrder;
	KCMCollectPageUIDs(targetDB, targetOrder);
	for (size_t i = 0; i < targetOrder.size(); ++i)
	{
		const UID t = targetOrder[i];
		if (KCMDrawEventHandler::sEntries.count(t) > 0)
		{
			AppendRow(rows, PageDisplay(targetDB, t), kKindChanged);
		}
		else if (registeredT.count(t) > 0 || overflowT.count(t) > 0)
		{
			AppendRow(rows, PageDisplay(targetDB, t), kKindInserted);
		}
	}

	// ---- The Target's master spreads: Changed only ----
	// Master spreads are held separately from IPageList and never appear in the KCMCollectPageUIDs
	// walk above -- **excluding them is the contract**, as IPageList puts it: "does not include
	// master pages". The order is borrowed from KCMCollectMasterPageUIDs, the same enumeration
	// Prev/Next (KCMBuildStops) and peek use, so that the order of masters is decided in one place
	// only. Their position -- after all the Target's ordinary pages and before the Source's
	// deletions -- is the order Prev/Next walks in.
	// **Inserted/Deleted do not arise here** (see the note at the top): a master with no
	// counterpart is not paired by KCMBuildMasterPairing, so it is never compared and appears
	// neither in sEntries nor in the pairing's overflow.
	std::vector<UID> masterOrder;
	KCMCollectMasterPageUIDs(targetDB, masterOrder);
	for (size_t i = 0; i < masterOrder.size(); ++i)
	{
		const UID m = masterOrder[i];
		if (KCMDrawEventHandler::sEntries.count(m) == 0)
			continue;
		PMString name = MasterPageDisplay(targetDB, m);
		if (name.NumUTF16TextChars() == 0)
			name = PageDisplay(targetDB, m);	// fallback when the name cannot be had (the prefix alone is better than nothing)
		AppendRow(rows, name, kKindChanged);
	}

	// ---- The Source in document order: Deleted (Source pages with no counterpart) ----
	// The counterpart of a changed page (the value in pairTargetToSource) is neither registered
	// nor in the overflow, so it never reaches here: nothing is counted twice.
	std::vector<UID> sourceOrder;
	KCMCollectPageUIDs(sourceDB, sourceOrder);
	for (size_t i = 0; i < sourceOrder.size(); ++i)
	{
		const UID s = sourceOrder[i];
		if (registeredS.count(s) > 0 || overflowS.count(s) > 0)
		{
			AppendRow(rows, PageDisplay(sourceDB, s), kKindDeleted);
		}
	}

	return !rows.empty();
}

// Build the collected rows into one text (header + rows, each ending in '\n'). The TAB and LF
// separators are ASCII, so Append(const char*) places them directly (PMString holds UTF-16).
PMString BuildReportText(const std::vector<KCMChangeRow>& rows)
{
	PMString text;
	text.SetTranslatable(kFalse);
	text.Append(kHeaderCol1);  text.AppendW(UTF32TextChar(kTabCode));
	text.Append(kHeaderCol2);  text.AppendW(UTF32TextChar(kLfCode));
	for (size_t i = 0; i < rows.size(); ++i)
	{
		text.Append(rows[i].page);
		text.AppendW(UTF32TextChar(kTabCode));
		text.Append(rows[i].kind);
		text.AppendW(UTF32TextChar(kLfCode));
	}
	return text;
}

} // anonymous namespace

//========================================================================================
// The body. It is static, with the public function a wrapper below: there are several early
// returns, and wrapping is what lets the message be written to `out` in one place.
//========================================================================================
static void KCMExportChangedPagesTSVRun()
{
	IDataBase* targetDB = KCMDrawEventHandler::sDB;
	IDataBase* sourceDB = KCMDrawEventHandler::sSrcDB;
	if (targetDB == nil || sourceDB == nil)
	{
		// The flyout item is only enabled while a comparison is running (sDB != nil), so this is belt and braces.
		ShowStatus("Start a comparison first.");
		return;
	}

	std::vector<KCMChangeRow> rows;
	if (!CollectRows(targetDB, sourceDB, rows))
	{
		ShowStatus("No changed pages to export.");
		return;
	}

	// The destination, through the shared chooser in sdksamples/common. The title and the initial
	// name are English, like the rest of KCM's UI.
	// 'TEXT'/'CWIE' are the classic Mac type/creator the SDK's own text export passes; they mean
	// nothing on Windows, but the API asks for them.
	// **This is the one file dialog the model half raises**, and it stays for the same reasons as
	//   the CAlert in KCMHideUnchanged.cpp:
	//   - `SDKFileSaveChooser` is a helper from `sdksamples/common`, **not a boss provided by a UI
	//     plug-in**;
	//   - this path is only ever entered from the flyout through the Facade, and **the drawing
	//     paths that run on background threads never reach it** (checked by grepping every call
	//     site).
	// @warning if an export is ever wanted from a background thread, **let the UI choose the
	//   destination and pass an `IDFile` in** -- the same shape as KCMGetPanelBookFile. A chooser
	//   cannot be opened from a background thread.
	SDKFileSaveChooser chooser;
	PMString title("Export Changed Pages");
	title.SetTranslatable(kFalse);
	chooser.SetTitle(title);
	chooser.SetFilename(BuildSuggestedFileName(targetDB));
	PMString filterName("Text file(txt)");
	filterName.SetTranslatable(kFalse);
	chooser.AddFilter('CWIE', 'TEXT', "txt", filterName);
	chooser.ShowDialog();
	if (!chooser.IsChosen())
		return;	// cancelling is silent

	// UTF-8 + BOM, '\n' -> '\r\n' (identical to KESCL: without the BOM, Excel and Notepad guess
	// the encoding of Japanese text wrongly).
	const PMString report = BuildReportText(rows);
	const std::string utf8 = report.GetUTF8String();
	std::string bytes;
	bytes.reserve(utf8.size() + utf8.size() / 8 + 3);
	bytes += "\xEF\xBB\xBF";
	for (std::string::const_iterator it = utf8.begin(); it != utf8.end(); ++it)
	{
		if (*it == '\n')
			bytes += "\r\n";
		else
			bytes += *it;
	}

	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(
		chooser.GetIDFile(), kOpenOut | kOpenTrunc, 'TEXT', 'CWIE'));
	if (stream == nil)
	{
		ShowStatus("Could not create the file. Is the folder writable?");
		return;
	}
	stream->XferByte(reinterpret_cast<uchar*>(&bytes[0]), static_cast<int32>(bytes.size()));
	// The state is read AFTER the Flush, as in KESCL: XferByte may only reach the buffer, so a
	// write failure can surface at the Flush. Reading before it reports "saved" for a file that
	// was not written.
	stream->Flush();
	const bool failed = (stream->GetStreamState() == kStreamStateFailure);
	stream->Close();

	// Silent on success -- the file is where the user put it. Failures only (KESCL's convention).
	if (failed)
		ShowStatus("Could not write the file.");
}

//========================================================================================
// KCMExportChangedPagesTSV (declared in KCMChangedPagesTSV.h)
//   **The message is returned in `out` rather than written to the status line.** A TSV export
//   naturally answers with "did it work, and where did it go" and has no reason to raise a
//   notification (the model/UI split design, 3.3). This half is the model; the caller -- the
//   flyout item, which is UI -- does the showing. With nothing to say, `out` comes back empty.
//========================================================================================
void KCMExportChangedPagesTSV(PMString& outMessage)
{
	gExportMessage.Clear();
	KCMExportChangedPagesTSVRun();
	outMessage = gExportMessage;
}

//========================================================================================
// KCMClearExportMessage (declared in KCMChangedPagesTSV.h)
//   One line, emptying the file-static PMString at shutdown. It touches neither the document
//   nor the UI, so it is safe at any point during teardown.
//========================================================================================
void KCMClearExportMessage()
{
	gExportMessage.Clear();
}

// End, KCMChangedPagesTSV.cpp.
