//========================================================================================
//
//  KCMPdfSpike.cpp -- see the header.
//
//  THE SHAPE OF THE FILE: one entry point that runs the steps in order and writes a line
//  about each one into the answer. **A step that fails does not stop the rest** - the ones
//  that depend on it say so and are skipped, and the reading still tells the whole story of
//  that run. That is the whole point of a spike: one press, every question answered.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <windows.h>				// GetTempPathW - the crash trace, and nothing else
#include <fstream>
#include <string>
#include <vector>

#include "IBoolData.h"
#include "ICommand.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDOMElement.h"			// the root a snippet is imported under
#include "IGeometry.h"
#include "IHierarchy.h"
#include "IImportProvider.h"
#include "IImportProviderUtils.h"
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "IMasterSpreadUtils.h"		// AppendMasterPageItems - the page's furniture, which GetItemsOnPage leaves out
#include "IPDFExportPrefs.h"
#include "IPDFSecurityPrefs.h"
#include "IPMStream.h"
#include "IPMUnknownData.h"
#include "ISession.h"
#include "ISnippetExport.h"
#include "ISnippetImport.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IUIFlagData.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "OpenPlaceID.h"			// kImportProviderService - the service every import filter registers under
#include "PDFID.h"					// kPDFExportItemsCmdBoss / IID_IPDFCLIPBOARDEXPORTPREFS / IID_IUSEPROGRESSINDICATOR
#include "PMFlavorTypes.h"			// kPDFExternalFlavor / kPageItemFlavor
#include "PreferenceUtils.h"		// ::QuerySessionPreferences
#include "SDKLayoutHelper.h"
#include "StreamUtil.h"
#include "TextChar.h"				// kTextChar_CR
#include "TransformUtils.h"			// InnerToSpreadMatrix
#include "UIDList.h"
#include "Utils.h"

#include "KCMPdfSpike.h"
#include "KCMCore.h"				// KCMActiveDocDB / KCMCollectPageUIDs
#include "KCMDrawEventHandler.h"	// sPrintMarks - what carries the marks into output
#include "KCMMemXferBytes.h"		// where the PDF lands instead of a file
#include "KCMRehydrate.h"			// KCMMarkRehydratedClean / KCMCloseRehydrated - the throwaway document

namespace
{

PMString Ascii(const char* ascii)
{
	PMString s(ascii);
	s.SetTranslatable(kFalse);
	return s;
}

/** ★A CRASH TRACE, and the reason it exists: the first run of this spike took InDesign down
    with it, and a reading that is handed back at the END says nothing when there is no end.
    Each step writes its name here BEFORE it runs and again after, flushed every line, so the
    last line of the file names the step that did not come back.
    ⚠**Diagnostic only.** It is the one file this experiment writes, it lives in %TEMP%, and it
      goes when the spike does - the whole point of the work is that the REPORT stops writing
      files, not that nothing ever may. */
void Trace(const char* text)
{
	wchar_t dir[MAX_PATH] = { 0 };
	::GetTempPathW(MAX_PATH, dir);
	std::wstring path(dir);
	path += L"kcm-spike-trace.txt";
	std::ofstream file(path.c_str(), std::ios::app);
	if (file)
		file << text << std::endl;		// endl, not '\n': it flushes, and a crash keeps what was flushed
}

/** One line of the reading - and the same line into the trace, because a run that does not
    come back takes the reading with it (measured: the first run reached S5 and every answer
    S1..S4 had already produced was lost with it). */
void Say(PMString& out, const PMString& line)
{
	out.Append(line);
	out.AppendW(UTF32TextChar(kTextChar_CR));
	Trace(line.GetUTF8String().c_str());
}

void Say(PMString& out, const char* line)
{
	Say(out, Ascii(line));
}

/** What an ImportAbility reads as. */
const char* AbilityName(IImportProvider::ImportAbility ability)
{
	switch (ability)
	{
		case IImportProvider::kCannotImport:	return "kCannotImport";
		case IImportProvider::kPartialImport:	return "kPartialImport";
		case IImportProvider::kFullImport:		return "kFullImport";
	}
	return "an unknown ImportAbility";
}

/** Export `items` of `db` to a PDF that never touches disk, and answer whether any bytes
    arrived. `why` says what went wrong when they did not.

    ⚠**THE DOOR THE GUIDE GETS WRONG.** vol2-05:351 puts the stream pointer in IID_IINTDATA,
      which this boss does not have - and which would cut a 64-bit pointer in half if it did,
      IIntData::ValueType being int32. The measured interface list (work/Boss.txt) has
      IID_IPMUNKNOWNDATA on kPMUnknownData_SoftReference_Impl, and that is what is used here.
    ⚠**SOFT reference**: that implementation does not AddRef what it is given, so the stream
      has to outlive the command. It does - both live in this function. */
bool16 ExportToMemory(IDataBase* db, const UIDList& items, KCMMemXferBytes& bytes, PMString& why)
{
	if (db == nil || items.Length() == 0)
	{
		why = Ascii("nothing to export");
		return kFalse;
	}
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kPDFExportItemsCmdBoss));
	if (cmd == nil)
	{
		why = Ascii("the command could not be created");
		return kFalse;
	}
	InterfacePtr<IPMUnknownData> streamData(cmd, IID_IPMUNKNOWNDATA);
	if (streamData == nil)
	{
		why = Ascii("IID_IPMUNKNOWNDATA is not on kPDFExportItemsCmdBoss after all");
		return kFalse;
	}
	cmd->SetItemList(items);

	// The CLIPBOARD PDF preferences, which is what the guide's example copies for this command -
	// this boss exists to put PDF on the clipboard - rather than the export preferences the
	// report's file route takes.
	InterfacePtr<IPDFExportPrefs> appPrefs((IPDFExportPrefs*)::QuerySessionPreferences(IID_IPDFCLIPBOARDEXPORTPREFS));
	InterfacePtr<IPDFExportPrefs> prefs(cmd, IID_IPDFEXPORTPREFS);
	if (prefs != nil && appPrefs != nil)
		prefs->CopyPrefs(appPrefs);
	InterfacePtr<IPDFSecurityPrefs> security(cmd, IID_IPDFSECURITYPREFS);
	if (security != nil)
		security->SetUseSecurity(kFalse);
	InterfacePtr<IBoolData> progress(cmd, IID_IUSEPROGRESSINDICATOR);
	if (progress != nil)
		progress->Set(kFalse);
	InterfacePtr<IUIFlagData> ui(cmd, IID_IUIFLAGDATA);
	if (ui != nil)
		ui->Set(kSuppressUI);

	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes, kFalse, kFalse));
	if (stream == nil)
	{
		why = Ascii("the memory stream could not be created");
		return kFalse;
	}
	streamData->SetPMUnknown(stream);

	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	stream->Flush();
	const ErrorCode global = ErrorUtils::PMGetGlobalErrorCode();
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (err != kSuccess || global != kSuccess)
	{
		why = Ascii("the export command answered an error");
		return kFalse;
	}
	if (bytes.GetSize() == 0)
	{
		why = Ascii("the command succeeded but wrote nothing");
		return kFalse;
	}
	return kTrue;
}

/** The page's index inside its own spread, and the spread, which GetItemsOnPage wants. */
bool16 PagePosition(IDataBase* db, UID pageUID, InterfacePtr<ISpread>& spread, int32& pgPos)
{
	InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
	if (pageHier == nil)
		return kFalse;
	InterfacePtr<ISpread> found(db, pageHier->GetSpreadUID(), UseDefaultIID());
	if (found == nil)
		return kFalse;
	const int32 count = found->GetNumPages();
	for (int32 p = 0; p < count; ++p)
	{
		if (found->GetNthPageUID(p) == pageUID)
		{
			spread = found;
			pgPos = p;
			return kTrue;
		}
	}
	return kFalse;
}

/** Ask every registered import provider what it makes of these bytes, and hand back the first
    one that says it can take them. The three routes are all measured rather than assumed:
    the flavour pair, the Mac file type, and this full sweep - so a run says which of them
    finds the PDF filter and which do not. */
IImportProvider* QueryProviderBySweep(KCMMemXferBytes& bytes, PMString& out)
{
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		Say(out, "S2c sweep of kImportProviderService: FAILED - no service registry");
		return nil;
	}
	IImportProvider* winner = nil;
	PMString line(Ascii("S2c sweep of kImportProviderService: "));
	int32 sayers = 0;
	const int32 count = registry->GetServiceProviderCount(kImportProviderService);
	line.AppendNumber(count);
	line.Append(" providers, ");
	for (int32 i = 0; i < count; ++i)
	{
		InterfacePtr<IK2ServiceProvider> service(registry->QueryNthServiceProvider(kImportProviderService, i));
		InterfacePtr<IImportProvider> provider(service, IID_IIMPORTPROVIDER);
		if (provider == nil)
			continue;
		// A fresh read stream for each one: a provider that sniffs the head leaves the position
		// wherever it stopped, and the next one would then read from the middle.
		InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&bytes, kFalse, kFalse));
		if (read == nil)
			continue;
		const IImportProvider::ImportAbility ability = provider->CanImportThisStream(read);
		read->Close();
		if (ability == IImportProvider::kCannotImport)
			continue;
		++sayers;
		if (sayers > 1)
			line.Append(", ");
		line.Append(provider->GetFormatName(0));
		line.Append(" says ");
		line.Append(AbilityName(ability));
		if (winner == nil)
		{
			winner = provider;
			winner->AddRef();		// handed back; the caller releases it
		}
	}
	if (sayers == 0)
		line.Append("not one of them will take these bytes");
	Say(out, line);
	return winner;
}

}	// namespace

void KCMProbePdfRoute(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	Trace("=== run begins ===");

	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
	{
		Say(out, "S0: FAILED - there is no active document to measure");
		return;
	}
	std::vector<UID> pages;
	KCMCollectPageUIDs(db, pages);
	if (pages.empty())
	{
		Say(out, "S0: FAILED - the active document has no pages");
		return;
	}
	const UID pageUID = pages[0];
	{
		PMString line(Ascii("S0 active document: "));
		line.AppendNumber(static_cast<int32>(pages.size()));
		line.Append(" pages, measuring page 1 (UID ");
		line.AppendNumber(static_cast<int32>(pageUID.Get()));
		line.Append(")");
		Say(out, line);
	}

	// ---- S1a: the page's own UID as the item list ------------------------------------------
	// If this works the route is simple, because a page carries its master items and its own
	// bounds with it. If it does not, S1b below is the fallback the guide's example describes.
	Trace("S1a begin - the page UID through kPDFExportItemsCmdBoss");
	KCMMemXferBytes pageBytes;
	bool16 pageRouteWorks = kFalse;
	{
		UIDList one(db);
		one.Append(pageUID);
		PMString why;
		pageRouteWorks = ExportToMemory(db, one, pageBytes, why);
		PMString line(Ascii("S1a the page UID as the item list: "));
		if (pageRouteWorks)
		{
			line.Append("OK, ");
			line.AppendNumber(static_cast<int32>(pageBytes.GetSize()));
			line.Append(" bytes");
		}
		else
		{
			line.Append("FAILED - ");
			line.Append(why);
		}
		Say(out, line);
	}

	// ---- S1b: the items standing on that page ----------------------------------------------
	// ⚠GetItemsOnPage does NOT include master items (ISpread.h:124), so this route would lose
	//   whatever the master draws. Measured anyway, because it is the shape the guide's only
	//   example uses and the answer decides which one the report is built on.
	Trace("S1b begin - the items on that page through kPDFExportItemsCmdBoss");
	KCMMemXferBytes itemBytes;
	bool16 itemRouteWorks = kFalse;
	{
		InterfacePtr<ISpread> spread;
		int32 pgPos = -1;
		PMString line(Ascii("S1b the items on that page: "));
		if (!PagePosition(db, pageUID, spread, pgPos))
		{
			line.Append("FAILED - the page's spread could not be found");
		}
		else
		{
			UIDList onPage(db);
			spread->GetItemsOnPage(pgPos, &onPage, kFalse /*not the page shape*/, kFalse /*no pasteboard*/, kTrue /*bleed and slug*/);
			line.AppendNumber(onPage.Length());
			line.Append(" items, ");
			PMString why;
			itemRouteWorks = ExportToMemory(db, onPage, itemBytes, why);
			if (itemRouteWorks)
			{
				line.Append("OK, ");
				line.AppendNumber(static_cast<int32>(itemBytes.GetSize()));
				line.Append(" bytes");
			}
			else
			{
				line.Append("FAILED - ");
				line.Append(why);
			}
		}
		Say(out, line);
	}

	// ★★THE ITEMS BYTES ARE THE ONES THAT MATTER, and the first run is why: the page-UID route
	//   answered 2,206 bytes where the items route answered 394,764 for the same page - a
	//   page-sized sheet with nothing drawn on it. So everything below measures the route that
	//   actually carries content, and the page route is kept only as the fallback.
	KCMMemXferBytes& bytes = itemRouteWorks ? itemBytes : pageBytes;
	const bool16 haveBytes = pageRouteWorks || itemRouteWorks;
	if (!haveBytes)
	{
		Say(out, "S2..S4: skipped - there are no bytes to import");
		return;
	}
	Say(out, itemRouteWorks ? "S2..S4 are measured on the S1b bytes (the items route)"
							: "S2..S4 are measured on the S1a bytes (the page route)");

	// ---- S2: who will take these bytes back in ---------------------------------------------
	Trace("S2a begin - QueryImportProviderFor");
	InterfacePtr<IImportProvider> provider;
	{
		Utils<IImportProviderUtils> utils;		// ⚠asked for BEFORE it is used: a nil Utils<> cannot be checked afterwards
		PMString line(Ascii("S2a QueryImportProviderFor(kPDFExternalFlavor, kPageItemFlavor): "));
		if (!utils)
		{
			line.Append("FAILED - Utils<IImportProviderUtils> is nil");
		}
		else
		{
			InterfacePtr<IImportProvider> byFlavor(utils->QueryImportProviderFor(kPDFExternalFlavor, kPageItemFlavor));
			if (byFlavor == nil)
			{
				line.Append("no provider");
			}
			else
			{
				line.Append("found ");
				line.Append(byFlavor->GetFormatName(0));
				provider = byFlavor;
			}
		}
		Say(out, line);
	}
	Trace("S2b begin - QueryImportProviderForFileType");
	{
		Utils<IImportProviderUtils> utils;
		PMString line(Ascii("S2b QueryImportProviderForFileType('PDF '): "));
		if (!utils)
		{
			line.Append("FAILED - Utils<IImportProviderUtils> is nil");
		}
		else
		{
			InterfacePtr<IImportProvider> byType(utils->QueryImportProviderForFileType(static_cast<SysOSType>('PDF ')));
			if (byType == nil)
			{
				line.Append("no provider");
			}
			else
			{
				line.Append("found ");
				line.Append(byType->GetFormatName(0));
				if (provider == nil)
					provider = byType;
			}
		}
		Say(out, line);
	}
	Trace("S2c begin - the sweep of every import provider");
	{
		InterfacePtr<IImportProvider> bySweep(QueryProviderBySweep(bytes, out));
		if (provider == nil && bySweep != nil)
			provider = bySweep;
	}
	if (provider == nil)
	{
		Say(out, "S3..S4: skipped - no import provider was found");
		return;
	}
	Trace("S2d begin - CanImportThisStream on the chosen provider");
	{
		InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&bytes, kFalse, kFalse));
		PMString line(Ascii("S2d the chosen provider on these bytes: "));
		if (read == nil)
		{
			line.Append("FAILED - no read stream");
		}
		else
		{
			line.Append(AbilityName(provider->CanImportThisStream(read)));
			read->Close();
		}
		Say(out, line);
	}

	// ---- S3: import it into a document of its own ------------------------------------------
	Trace("S3 begin - ImportThis into a windowless document");
	{
		SDKLayoutHelper helper;
		UIDRef temp = helper.CreateDocument(kSuppressUI, PMReal(600), PMReal(800), 1, 1, 0);
		PMString line(Ascii("S3 ImportThis into a windowless document: "));
		if (temp == UIDRef::gNull)
		{
			line.Append("FAILED - the document could not be created");
			Say(out, line);
		}
		else
		{
			// Same rule as S5 below: nothing may still hold the document when it is closed.
			{
			InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&bytes, kFalse, kFalse));
			if (read == nil)
			{
				line.Append("FAILED - no read stream");
			}
			else
			{
				UIDRef imported = UIDRef::gNull;
				PMString asFormat;
				ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				provider->ImportThis(temp.GetDataBase(), read, kSuppressUI, &imported, nil, &asFormat);
				const ErrorCode global = ErrorUtils::PMGetGlobalErrorCode();
				ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				read->Close();
				if (imported == UIDRef::gNull)
				{
					line.Append("FAILED - nothing came back");
					if (global != kSuccess)
						line.Append(" (and the global error code was set)");
				}
				else
				{
					line.Append("OK, UID ");
					line.AppendNumber(static_cast<int32>(imported.GetUID().Get()));
					if (!asFormat.IsEmpty())
					{
						line.Append(" as ");
						line.Append(asFormat);
					}
					// Where did it land, and how big is it? Both decide how the report would
					// place it: an item with no parent has to be put on a spread by hand.
					InterfacePtr<IHierarchy> hier(imported, UseDefaultIID());
					line.Append(hier != nil && hier->GetParentUID() != kInvalidUID ? ", parented" : ", NO PARENT");
					InterfacePtr<IGeometry> geo(imported, UseDefaultIID());
					if (geo != nil)
					{
						const PMRect box = geo->GetStrokeBoundingBox();
						line.Append(", bounds ");
						line.AppendNumber(static_cast<int32>(::ToDouble(box.Width())));
						line.Append(" x ");
						line.AppendNumber(static_cast<int32>(::ToDouble(box.Height())));
					}
					else
					{
						line.Append(", no IGeometry");
					}
				}
			}
			Say(out, line);
			}	// the read stream and everything else on the document go here
			// The throwaway goes, whatever happened - a document left open here would be the
			// kind of leak that shows up an hour later as "InDesign will not quit".
			Trace("S3 closing the throwaway document");
			KCMMarkRehydratedClean(temp.GetDataBase());
			KCMCloseRehydrated(temp, kFalse /*now*/);
			Trace("S3 closed");
		}
	}

	// ---- S4: do the comparison marks travel with it? ----------------------------------------
	// ⚠★★★**S4 ANSWERS "NO" AND S7 EXPLAINS WHY, AND THE ANSWER IS "NOBODY ASKED".** Read S7
	//   before drawing any conclusion from this step: the marks are drawn once per SPREAD, and a
	//   list of items contains no spread, so the adornment turns back at
	//   KCMRingAdornment.cpp:498-504 for every item it is called on. Put the spread in the list
	//   and they come (S7: +3,031 bytes). This step is kept because it is the control - it is
	//   what the number looks like when the spread is NOT there.
	// The marks are a DRAWING (KCMDrawEventHandler), not content, and sPrintMarks is the one
	// switch that puts them into output. If the same export is bigger with it on, something of
	// theirs reached the PDF. ⚠A size difference is evidence, not proof; the eye comes next.
	Trace("S4 begin - the same export with sPrintMarks on");
	{
		const bool16 was = KCMDrawEventHandler::sPrintMarks;
		KCMMemXferBytes marked;
		PMString why;
		bool16 ok = kFalse;
		KCMDrawEventHandler::sPrintMarks = kTrue;
		{
			UIDList list(db);
			if (itemRouteWorks)
			{
				InterfacePtr<ISpread> spread;
				int32 pgPos = -1;
				if (PagePosition(db, pageUID, spread, pgPos))
					spread->GetItemsOnPage(pgPos, &list, kFalse, kFalse, kTrue);
			}
			else
			{
				list.Append(pageUID);
			}
			ok = ExportToMemory(db, list, marked, why);
		}
		KCMDrawEventHandler::sPrintMarks = was;

		PMString line(Ascii("S4 the same export with sPrintMarks on: "));
		if (!ok)
		{
			line.Append("FAILED - ");
			line.Append(why);
		}
		else
		{
			const int32 before = static_cast<int32>(bytes.GetSize());
			const int32 after = static_cast<int32>(marked.GetSize());
			line.AppendNumber(after);
			line.Append(" bytes against ");
			line.AppendNumber(before);
			line.Append(after == before ? " - IDENTICAL, so nothing of the marks reached it"
									    : " - DIFFERENT, so something of the marks reached it");
		}
		Say(out, line);
	}

	// ---- S5: the OTHER vector route - the page's items as a SNIPPET, in memory ---------------
	// ★NOTHING HERE IS PDF. ISnippetExport writes the items as INX and ISnippetImport builds
	//   them again in the other document, so what arrives is REAL PAGE ITEMS: text that is
	//   still text and curves that are still curves, at any zoom, for ever. That is the answer
	//   to the user's "raster is hard to read when you zoom in" (2026-09-14) in its strongest
	//   form - there is no picture at all, only the objects.
	// ⚠WHAT IT CANNOT DO, and both are measured rather than hoped: it cannot carry the
	//   comparison marks (a drawing, not content - S4 is the question for the PDF route), and
	//   GetItemsOnPage leaves out the MASTER's items, so a page whose furniture comes from its
	//   master would arrive bare. Which of the two routes the report is built on depends on
	//   what this run says.
	Trace("S5 begin - the items as a snippet, in memory");
	{
		PMString line(Ascii("S5 the items as a snippet, in memory: "));
		KCMMemXferBytes snippet;
		int32 exported = 0;
		bool16 wrote = kFalse;
		{
			InterfacePtr<ISpread> spread;
			int32 pgPos = -1;
			if (PagePosition(db, pageUID, spread, pgPos))
			{
				UIDList onPage(db);
				spread->GetItemsOnPage(pgPos, &onPage, kFalse, kFalse, kTrue);
				exported = onPage.Length();
				if (exported > 0)
				{
					InterfacePtr<IPMStream> write(StreamUtil::CreateMemoryStreamWrite(&snippet, kFalse, kFalse));
					Utils<ISnippetExport> exporter;
					if (write != nil && exporter)
					{
						Trace("S5.1 calling ExportPageitems");
						wrote = (exporter->ExportPageitems(write, onPage) == kSuccess);
						write->Flush();
						Trace("S5.2 ExportPageitems came back");
					}
				}
			}
		}
		if (!wrote || snippet.GetSize() == 0)
		{
			line.Append("FAILED - the snippet export wrote nothing");
			Say(out, line);
		}
		else
		{
			line.AppendNumber(exported);
			line.Append(" items out, ");
			line.AppendNumber(static_cast<int32>(snippet.GetSize()));
			line.Append(" bytes, ");

			Trace("S5.3 creating the document to import into");
			SDKLayoutHelper helper;
			UIDRef temp = helper.CreateDocument(kSuppressUI, PMReal(600), PMReal(800), 1, 1, 0);
			if (temp == UIDRef::gNull)
			{
				line.Append("but the document to import into could not be created");
				Say(out, line);
			}
			else
			{
				// ★★★EVERYTHING THAT HOLDS THE DOCUMENT LIVES INSIDE THIS BLOCK, and that is not
				//   tidiness - the first run of this spike TOOK INDESIGN DOWN right here (trace:
				//   "S5.6 closing the throwaway document" was the last line). A document closed
				//   while an InterfacePtr - or a UIDList, which carries its database - still
				//   stands on it is a protective shutdown, the same rule KCMReport.cpp states at
				//   the top of the file and the same one KCMRehydrate.cpp keeps.
				{
					InterfacePtr<IDocument> doc(temp, UseDefaultIID());
					InterfacePtr<ISpreadList> spreads(doc, UseDefaultIID());
					const UID spreadUID = (spreads != nil && spreads->GetSpreadCount() > 0) ? spreads->GetNthSpreadUID(0) : kInvalidUID;
					InterfacePtr<ISpread> spread(temp.GetDataBase(), spreadUID, UseDefaultIID());
					// ★The root is the SPREAD's own IDOMElement, taken straight off its UIDRef
					//   (codesnippets/SnpImportExportSnippet.cpp:333). No script manager, no proxy.
					InterfacePtr<IDOMElement> frag(temp.GetDataBase(), spreadUID, UseDefaultIID());
					InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&snippet, kFalse, kFalse));
					Utils<ISnippetImport> importer;		// asked for before it is used, like every other Utils<> here
					if (spread == nil || frag == nil || read == nil || !importer)
					{
						line.Append("but the import could not be set up (");
						line.Append(spread == nil ? "no spread" : (frag == nil ? "no IDOMElement on the spread"
											 : (read == nil ? "no read stream" : "no Utils<ISnippetImport>")));
						line.Append(")");
					}
					else
					{
						// What appeared is "what is on the spread afterwards and was not before" -
						// ImportFromStream does not say what it made (the shape KIDMCPRevert uses).
						int32 had = 0;
						{
							UIDList before(temp.GetDataBase());
							spread->GetItemsOnPage(0, &before, kFalse, kTrue /*pasteboard too*/, kTrue);
							had = before.Length();
						}
						Trace("S5.4 calling ImportFromStream");
						ErrorUtils::PMSetGlobalErrorCode(kSuccess);
						const ErrorCode err = importer->ImportFromStream(read, frag, kInvalidClass, kSuppressUI, nil);
						ErrorUtils::PMSetGlobalErrorCode(kSuccess);
						Trace("S5.5 ImportFromStream came back");
						read->Close();
						int32 now = 0;
						PMRect first(0, 0, 0, 0);
						{
							UIDList after(temp.GetDataBase());
							spread->GetItemsOnPage(0, &after, kFalse, kTrue, kTrue);
							now = after.Length();
							if (now > 0)
							{
								InterfacePtr<IGeometry> geo(after.GetRef(now - 1), UseDefaultIID());
								if (geo != nil)
									first = geo->GetStrokeBoundingBox();
							}
						}
						if (err != kSuccess)
						{
							line.Append("but ImportFromStream answered an error");
						}
						else
						{
							line.Append("imported ");
							line.AppendNumber(now - had);
							line.Append(" items onto the spread (");
							line.AppendNumber(had);
							line.Append(" -> ");
							line.AppendNumber(now);
							line.Append("), last one ");
							line.AppendNumber(static_cast<int32>(::ToDouble(first.Width())));
							line.Append(" x ");
							line.AppendNumber(static_cast<int32>(::ToDouble(first.Height())));
						}
					}
					Say(out, line);
				}	// every reference on the document is gone before the next line
				Trace("S5.6 closing the throwaway document");
				KCMMarkRehydratedClean(temp.GetDataBase());
				KCMCloseRehydrated(temp, kFalse /*now*/);
				Trace("S5.7 closed");
			}
		}
	}
	// ---- S6: the page, the items on it and the MASTER's items, in one list ------------------
	// ★THE QUESTION THE REPORT ACTUALLY HAS TO ANSWER. S1a showed the page UID alone writes an
	//   empty sheet and S1b showed the items alone leave the master's furniture behind, so the
	//   route that could replace the temporary file has to carry all three at once. The master
	//   items are asked for exactly the way KCMPageNumberMarker.cpp:385-399 asks - the one place
	//   in this plug-in that already needed them.
	// ⚠A master item lives on the MASTER spread, so its own coordinates are that spread's. If it
	//   lands in the wrong place, the bounds of the imported item will say so.
	Trace("S6 begin - page + its items + the master's items");
	{
		PMString line(Ascii("S6 the page, its items and its master's items in one list: "));
		UIDList list(db);
		list.Append(pageUID);
		int32 own = 0;
		int32 fromMaster = 0;
		{
			InterfacePtr<ISpread> spread;
			int32 pgPos = -1;
			if (PagePosition(db, pageUID, spread, pgPos))
			{
				spread->GetItemsOnPage(pgPos, &list, kFalse, kFalse, kTrue);
				own = list.Length() - 1;
				InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
				InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
				Utils<IMasterSpreadUtils> masters;
				if (pageGeo != nil && pageHier != nil && masters)
				{
					PMRect boundsInSpread = pageGeo->GetPathBoundingBox();
					::InnerToSpreadMatrix(pageGeo).Transform(&boundsInSpread);
					UIDList onThesePages(db);
					onThesePages.Append(pageUID);
					PMRectCollection boundsList;
					boundsList.push_back(boundsInSpread);
					UIDList masterItems(db);
					UIDList itemPages(db);
					PMMatrixCollection offsets;
					masters->AppendMasterPageItems(db, pageHier->GetSpreadUID(), onThesePages, boundsList,
												   masterItems, itemPages, offsets);
					fromMaster = masterItems.Length();
					for (int32 i = 0; i < fromMaster; ++i)
						list.Append(masterItems[i]);
				}
			}
		}
		line.AppendNumber(own);
		line.Append(" own + ");
		line.AppendNumber(fromMaster);
		line.Append(" from the master, ");
		KCMMemXferBytes all;
		PMString why;
		if (!ExportToMemory(db, list, all, why))
		{
			line.Append("FAILED - ");
			line.Append(why);
			Say(out, line);
		}
		else
		{
			line.AppendNumber(static_cast<int32>(all.GetSize()));
			line.Append(" bytes (S1b alone was ");
			line.AppendNumber(static_cast<int32>(itemBytes.GetSize()));
			line.Append("), ");
			// And where does it land? The bounds decide whether this can simply replace
			// PlaceFileInFrame in the report.
			SDKLayoutHelper helper;
			UIDRef temp = helper.CreateDocument(kSuppressUI, PMReal(600), PMReal(800), 1, 1, 0);
			if (temp == UIDRef::gNull)
			{
				line.Append("but no document to import into");
				Say(out, line);
			}
			else
			{
				{
					InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&all, kFalse, kFalse));
					if (read == nil || provider == nil)
					{
						line.Append("but there was nothing to import with");
					}
					else
					{
						UIDRef imported = UIDRef::gNull;
						provider->ImportThis(temp.GetDataBase(), read, kSuppressUI, &imported);
						read->Close();
						if (imported == UIDRef::gNull)
						{
							line.Append("but nothing came back from ImportThis");
						}
						else
						{
							InterfacePtr<IGeometry> geo(imported, UseDefaultIID());
							if (geo != nil)
							{
								const PMRect box = geo->GetStrokeBoundingBox();
								line.Append("imported at ");
								line.AppendNumber(static_cast<int32>(::ToDouble(box.Width())));
								line.Append(" x ");
								line.AppendNumber(static_cast<int32>(::ToDouble(box.Height())));
							}
							else
							{
								line.Append("imported, no IGeometry");
							}
						}
					}
					Say(out, line);
				}
				Trace("S6 closing the throwaway document");
				KCMMarkRehydratedClean(temp.GetDataBase());
				KCMCloseRehydrated(temp, kFalse /*now*/);
			}
		}
	}

	// ---- S7: put the SPREAD in the list, and the marks should follow -------------------------
	// ★★★WHY S4 SAID NO, found by reading rather than guessing (KCMRingAdornment.cpp:498-504):
	//
	//       InterfacePtr<ISpread> spread(iShape, UseDefaultIID());
	//       if (spread == nil) { KCMDrawStoryIdLabel(iShape, gd, flags); return; }
	//
	//   The adornment is on the GLOBAL list, so it is called for every page item on the spread -
	//   and KCM draws the marks ONCE PER SPREAD, so every call whose iShape is not the spread
	//   itself turns back there. kPDFExportItemsCmdBoss draws the items it was handed and never
	//   draws the spread, so that line is reached for every item and the marks are never drawn.
	//   Nothing is broken: the marks were never asked for.
	// ⇒ THE TEST: hand it the SPREAD as well. If the export draws the spread shape, the adornment
	//   gets its one call with iShape == the spread, and the marks go into the PDF after all.
	Trace("S7 begin - the spread in the list, marks off then on");
	{
		PMString line(Ascii("S7 the spread in the list: "));
		UIDList list(db);
		UID spreadUID = kInvalidUID;
		{
			InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
			if (pageHier != nil)
				spreadUID = pageHier->GetSpreadUID();
		}
		if (spreadUID == kInvalidUID)
		{
			line.Append("FAILED - the page has no spread");
			Say(out, line);
		}
		else
		{
			list.Append(spreadUID);
			list.Append(pageUID);
			{
				InterfacePtr<ISpread> spread;
				int32 pgPos = -1;
				if (PagePosition(db, pageUID, spread, pgPos))
					spread->GetItemsOnPage(pgPos, &list, kFalse, kFalse, kTrue);
			}
			const bool16 was = KCMDrawEventHandler::sPrintMarks;
			KCMMemXferBytes plain;
			KCMMemXferBytes marked;
			PMString why;
			KCMDrawEventHandler::sPrintMarks = kFalse;
			const bool16 okPlain = ExportToMemory(db, list, plain, why);
			KCMDrawEventHandler::sPrintMarks = kTrue;
			const bool16 okMarked = ExportToMemory(db, list, marked, why);
			KCMDrawEventHandler::sPrintMarks = was;

			if (!okPlain || !okMarked)
			{
				line.Append("FAILED - ");
				line.Append(why);
			}
			else
			{
				const int32 a = static_cast<int32>(plain.GetSize());
				const int32 b = static_cast<int32>(marked.GetSize());
				line.AppendNumber(list.Length());
				line.Append(" in the list, marks off ");
				line.AppendNumber(a);
				line.Append(" bytes, marks on ");
				line.AppendNumber(b);
				line.Append(b == a ? " - IDENTICAL, the spread did not bring them"
								   : " - DIFFERENT, THE SPREAD BROUGHT THE MARKS");
			}
			Say(out, line);
		}
	}

	Trace("=== run ends, every step came back ===");
}

// End, KCMPdfSpike.cpp.
