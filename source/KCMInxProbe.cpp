//========================================================================================
//
//  KCMInxProbe.cpp -- THROWAWAY. See KCMInxProbe.h for the question and for what to delete.
//
//  HOW THIS FILE IS BUILT, and it is the point: EVERY ROUTE IS ITS OWN FUNCTION, even where two
//  of them differ by one argument. A crash report resolves the stack to function names, so a
//  route with a name of its own says which one died; a shared helper would have said only that
//  "the helper" died. That is how the 2026-09-08 crash was placed in one reading.
//
//  AND EVERY STAGE WRITES A LINE TO A FILE BEFORE IT TRIES ANYTHING. The report string is lost
//  when the process dies, so the log is what survives: work/kcm-inx-probe-log.txt says which
//  stage was entered and which one was left. "Entered 4, left 3" names the killer exactly.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocument.h"
#include "IDocumentList.h"
#include "IDOMElement.h"
#include "IINXManager.h"
#include "IPMStream.h"
#include "IScript.h"
#include "IScriptEngine.h"
#include "IScriptManager.h"
#include "IScriptRequestData.h"	// the request our own property arrived on
#include "IScriptRequestHandler.h"	// GetProperties - the C++ side of obj.properties
#include "IScriptUtils.h"
#include "IStyleGroupHierarchy.h"	// GetHierarchyType - style vs group
#include "IStyleGroupManager.h"	// GetRootHierarchy
#include "ISession.h"
#include "ISnippetExport.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IStoryList.h"	// stage 9 - a story on its own
#include "IXferBytes.h"
#include "IExportProvider.h"	// ExportToStream - the document, into memory
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "IUCFPackageUtils.h"	// a UCF package (IDML) opened from a stream, not a file
#include "ISelectionManager.h"	// the active selection - what SnpExportEPub passes as targetboss
#include "ISelectionUtils.h"

// General includes:
#include "DocFrameworkID.h"	// kDocumentObjectScriptElement - the document as a scripting object
#include "INXCoreID.h"
#include "TextID.h"			// IID_IPARASTYLEGROUPMANAGER / IID_ISTYLEGROUPHIERARCHY		// kINXTraditionalImportScriptManagerBoss - the INX script context
#include "PersistUtils.h"
#include "StreamUtil.h"
#include "UIDList.h"
#include "Utils.h"
#include "ErrorUtils.h"		// ExportToStream returns void; failure lands on the global code
#include "SaveBackID.h"		// kSaveBackExportProviderBoss - the IDML export provider
#include "SnippetID.h"		// kSnippetExportProviderBoss - named in the catalogue
#include "XMLID.h"			// kXMLExportProviderBoss - named in the catalogue
#include "AssignmentID.h"	// kAssignmentExport{All,Spreads,Frames}PolicyBoss
#include "JBXID.h"			// kJBXExportPolicyBoss - the only policy that carries settings
#include "AppFrameworkID.h"	// kActionExportPolicyBoss
#include "PackageAndPreflightID.h"	// kPreflightProfileExportPolicyBoss

#include <windows.h>		// ::GetTickCount - each route reports how long it took
#include <stdio.h>			// the log survives a crash; the report string does not
#include <string.h>		// strcmp - the resume file is read back as plain lines
#include <map>
#include <string>

// Project includes:
#include "KCMInxProbe.h"

namespace
{

const char* const kLogPath =
	"C:\\Users\\user\\Desktop\\plugin_sdk_21.0.0.192\\work\\kcm-inx-probe-log.txt";

/** Append one line to the log and CLOSE THE FILE EVERY TIME.

    Closing per line is the whole point: a buffered handle loses its tail when the process is
    killed, and the tail is precisely the line naming the call that killed it. */
void Log(const char* text)
{
	FILE* f = nil;
	if (::fopen_s(&f, kLogPath, "a") == 0 && f != nil)
	{
		::fprintf(f, "%s\n", text);
		::fclose(f);
	}
}

/** The bytes an in-memory stream is written into.

    SDK's own MemXferBytes is not shipped (public/libs/publiclib/strings/WideString.cpp includes
    the header, and that header exists nowhere in the SDK), so IXferBytes is implemented here.
    Shape taken from sdksamples/hostadapter/IDHAMemoryXferBytes, cut down to what a probe needs.

    Throwaway licence: this grows with std::string, which throws. Acceptable HERE - the probe is
    reached from a script property, not a draw event - and the thing to rewrite (nothrow buffer,
    K2::scoped_array) if any route measured here is ever kept. */
class ProbeBytes : public IXferBytes
{
public:
	ProbeBytes() : fPos(0), fState(kStreamStateGood) {}
	virtual ~ProbeBytes() {}

	virtual uint32 Read(void* buffer, uint32 num)
	{
		if (buffer == nil || fPos >= fData.size())
			return 0;
		const size_t left = fData.size() - fPos;
		const size_t take = (num < left) ? static_cast<size_t>(num) : left;
		::memcpy(buffer, fData.data() + fPos, take);
		fPos += take;
		return static_cast<uint32>(take);
	}

	virtual uint32 Write(void* buffer, uint32 num)
	{
		if (buffer == nil || num == 0)
			return 0;
		if (fPos != fData.size())
			fData.resize(fPos);
		fData.append(static_cast<const char*>(buffer), num);
		fPos = fData.size();
		return num;
	}

	virtual uint64 Seek(int64 numberOfBytes, SeekFromWhere fromHere)
	{
		int64 want = numberOfBytes;
		if (fromHere == kSeekFromCurrent)
			want += static_cast<int64>(fPos);
		else if (fromHere == kSeekFromEnd)
			want += static_cast<int64>(fData.size());

		if (want < 0)
			want = 0;
		if (want > static_cast<int64>(fData.size()))
			want = static_cast<int64>(fData.size());

		fPos = static_cast<size_t>(want);
		return fPos;
	}

	virtual void Flush() {}
	virtual StreamState GetStreamState() { return fState; }
	virtual void SetEndOfStream() {}

	size_t Size() const { return fData.size(); }
	const std::string& Data() const { return fData; }

	PMString FirstLine(size_t limit) const
	{
		PMString out;
		out.SetTranslatable(kFalse);
		for (size_t i = 0; i < fData.size() && i < limit; ++i)
		{
			const char c = fData[i];
			if (c == '\r' || c == '\n')
				break;
			out.Append(&c, 1);
		}
		return out;
	}

private:
	std::string	fData;
	size_t		fPos;
	StreamState	fState;
};

IDocument* FirstDocument()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	InterfacePtr<IDocumentList> docList(session->QueryDocumentList());
	if (docList == nil || docList->GetDocCount() < 1)
		return nil;
	return docList->GetNthDoc(0);
}

/** The policies already tried, kept in a file so that a crash does not force a rebuild.

    Each candidate is recorded BEFORE it is tried. If InDesign dies inside one - as it did in
    kSaveBackExportPolicyBoss on 2026-09-09 - the name is already on disk and the next run skips
    straight past it, instead of stopping the sweep at the same place forever. */
const char* const kDonePath =
	"C:/Users/user/Desktop/plugin_sdk_21.0.0.192/work/kcm-policy-done.txt";
/** Where stage 23 writes the whole export, so it can be diffed from outside InDesign. */
const char* const kDumpPath =
	"C:/Users/user/Desktop/plugin_sdk_21.0.0.192/work/kcm-inx-action.xml";

bool16 AlreadyTried(const char* name)
{
	FILE* f = nil;
	if (::fopen_s(&f, kDonePath, "r") != 0 || f == nil)
		return kFalse;
	char line[256];
	bool16 found = kFalse;
	while (::fgets(line, sizeof(line), f) != nil)
	{
		size_t n = ::strlen(line);
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (::strcmp(line, name) == 0)
		{
			found = kTrue;
			break;
		}
	}
	::fclose(f);
	return found;
}

void MarkTried(const char* name)
{
	FILE* f = nil;
	if (::fopen_s(&f, kDonePath, "a") == 0 && f != nil)
	{
		::fprintf(f, "%s\n", name);
		::fclose(f);
	}
}

/** Log a label and a number. Same reason as Log: what is in the file is what survives. */
void LogNum(const char* label, int32 value)
{
	char buf[512];
	::sprintf_s(buf, sizeof(buf), "    %s%d", label, static_cast<int>(value));
	Log(buf);
}

void Line(PMString& out, const char* text)
{
	out.Append(text);
	out.Append("\n");
	Log(text);	// see LineNum: the report dies with the run, the log survives it
}

void LineNum(PMString& out, const char* label, int32 value)
{
	out.Append(label);
	out.AppendNumber(value);
	// ALSO to the log. The report string is built in memory and returned at the END of the run,
	// so a stage that HANGS or dies takes every earlier answer with it - which is exactly what
	// happened on 2026-09-09: stage 16 wedged InDesign and stages 13-15 became unreadable.
	LogNum(label, value);
	out.Append("\n");
}

/** Report an export's outcome the same way for every route. Not a shared EXPORT - the calls stay
    apart so a crash names one - just the four numbers afterwards. */
void Report(PMString& out, const char* what, ErrorCode err, const ProbeBytes& bytes, DWORD took)
{
	out.Append(what);
	out.Append(" ErrorCode (0 = kSuccess): ");
	out.AppendNumber(static_cast<int32>(err));
	out.Append("\n");
	LineNum(out, "bytes: ", static_cast<int32>(bytes.Size()));
	LineNum(out, "milliseconds: ", static_cast<int32>(took));
	if (bytes.Size() > 0)
	{
		out.Append("first line: ");
		out.Append(bytes.FirstLine(160));
		out.Append("\n");
	}
}


/** Open a stream if it is not already usable, and report the state either way.

    SnpImportExportXML.cpp:211-216 opens its stream and checks GetStreamState() before handing it
    to ExportToStream. Omitting that was the ONE difference between the snippet's call and mine,
    and the call that omitted it KILLED INDESIGN on 2026-09-09 (the log ends at
    "stage16: about to call ExportToStream (XML)", with no "returned" after it).

    Shared on purpose: this is the preparation BEFORE a route, not the route itself, so it does
    not blur which route died. */
bool16 EnsureStreamOpen(IPMStream* stream, PMString& out)
{
	if (stream == nil)
		return kFalse;
	if (stream->GetStreamState() != kStreamStateGood)
		stream->Open();
	LineNum(out, "stream state handed to ExportToStream (good = ", static_cast<int32>(kStreamStateGood));
	LineNum(out, "  actual state: ", static_cast<int32>(stream->GetStreamState()));
	return (stream->GetStreamState() == kStreamStateGood);
}
/** Count the names that answer the real question: does THIS snippet carry styles, swatches
    and fonts, or only a reference to them?

    Counted as plain substrings rather than as elements, because a style appears BOTH ways: as
    a definition (<ParagraphStyle ...>) and as a reference on something that uses it
    (AppliedParagraphStyle="..."). Both matter, and the two together are what says whether a
    diff of this XML would notice a style being edited. */
void FindNamed(const std::string& xml, PMString& out)
{
	static const char* const kWanted[] = {
		"ParagraphStyle", "CharacterStyle", "ObjectStyle", "TableStyle", "CellStyle",
		"Color", "Swatch", "Gradient", "Tint", "Ink", "MixedInk",
		"Font", "AppliedFont", "PointSize", "Language",
		"Layer", "Section", "Page", "Spread", "MasterSpread",
		"Story", "Table", "Cell", "Footnote", "Ruby", "Kenten",
		"Hyperlink", "Condition", "Note", "TransparencySetting"
	};

	for (size_t k = 0; k < sizeof(kWanted) / sizeof(kWanted[0]); ++k)
	{
		const std::string needle(kWanted[k]);
		int32 n = 0;
		size_t at = xml.find(needle);
		while (at != std::string::npos)
		{
			++n;
			at = xml.find(needle, at + 1);
		}
		if (n > 0)
		{
			out.Append("  ");
			out.Append(kWanted[k]);
			out.Append(" x ");
			out.AppendNumber(n);
			out.Append("\n");
		}
	}
}

/** Count the element names in a lump of INX and report the commonest.

    Crude on purpose - it looks for '<' followed by a letter and takes the name - which is enough
    to say what KINDS of thing are in there without reading 300KB by eye. */
void CountElements(const std::string& xml, PMString& out, int32 topN)
{
	std::map<std::string, int32> counts;
	for (size_t i = 0; i + 1 < xml.size(); ++i)
	{
		if (xml[i] != '<')
			continue;
		const char c = xml[i + 1];
		const bool isLetter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		if (!isLetter)
			continue;
		size_t j = i + 1;
		while (j < xml.size() && xml[j] != ' ' && xml[j] != '>' && xml[j] != '/' && xml[j] != '\r' && xml[j] != '\n')
			++j;
		counts[xml.substr(i + 1, j - i - 1)] += 1;
	}

	LineNum(out, "distinct element names: ", static_cast<int32>(counts.size()));

	// The commonest few, picked by repeated maximum - the map is small and this avoids sorting.
	for (int32 n = 0; n < topN; ++n)
	{
		std::map<std::string, int32>::iterator best = counts.end();
		for (std::map<std::string, int32>::iterator it = counts.begin(); it != counts.end(); ++it)
		{
			if (best == counts.end() || it->second > best->second)
				best = it;
		}
		if (best == counts.end() || best->second <= 0)
			break;
		out.Append("  ");
		out.Append(best->first.c_str());
		out.Append(" x ");
		out.AppendNumber(best->second);
		out.Append("\n");
		best->second = 0;
	}
}

//----------------------------------------------------------------------------------------
// The routes that are known to work (measured 2026-09-08)
//----------------------------------------------------------------------------------------

/** ROUTE A - NOT RUN. It crashed InDesign, and this function's body is the record.

    2026-09-08: reading app.kcmInxProbe raised EXCEPTION_ACCESS_VIOLATION inside ExportINX.
    Adobe's crash report named the stack: unknown / unknown / RouteA / KCMRunInxProbe /
    KCMScriptProvider::AccessProperty. Saved whole at work/kcm-crash-2026-09-08-inxprobe.xml.

    The root handed over was an IDOMElement queried STRAIGHT OFF THE DOCUMENT BOSS. That query
    answers - the pointer was not nil, which is why the code went on - but IDOMElement.h:58-60
    says results outside an INX context are unpredictable and may crash.
    => NOT NIL IS NOT THE SAME AS USABLE. Stages 3 to 6 below try the routes that build the
    element properly instead. */
void RouteA(PMString& out)
{
	Line(out, "-- ROUTE A (2026-09-08): crashed InDesign. Not run. --");
	Line(out, "IDOMElement straight off the document boss is not an INX element.");
}

/** ROUTE B - page items through the snippet API. Worked examples exist; measured 29,816 bytes. */
void RouteB(IDocument* doc, PMString& out)
{
	Log("B: enter");
	Line(out, "-- ROUTE B: ISnippetExport::ExportPageitems --");

	IDataBase* db = ::GetDataBase(doc);
	InterfacePtr<ISpreadList> spreadList(doc, UseDefaultIID());
	if (db == nil || spreadList == nil || spreadList->GetSpreadCount() < 1)
	{
		Line(out, "no spread, so not attempted");
		Log("B: leave (no spread)");
		return;
	}

	InterfacePtr<ISpread> spread(db, spreadList->GetNthSpreadUID(0), UseDefaultIID());
	if (spread == nil)
	{
		Line(out, "no spread interface, so not attempted");
		Log("B: leave (no spread interface)");
		return;
	}

	UIDList items(db);
	spread->GetItemsOnPage(0, &items, kFalse, kFalse);
	LineNum(out, "page items on the first page: ", items.Length());
	if (items.Length() < 1)
	{
		Line(out, "nothing to export");
		Log("B: leave (no items)");
		return;
	}

	ProbeBytes bytes;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "no memory stream");
		Log("B: leave (no stream)");
		return;
	}

	const DWORD began = ::GetTickCount();
	const ErrorCode err = Utils<ISnippetExport>()->ExportPageitems(stream, items);
	const DWORD took = ::GetTickCount() - began;
	stream->Flush();

	Report(out, "ExportPageitems", err, bytes, took);
	if (bytes.Size() > 0)
	{
		Line(out, "what a PAGE ITEM snippet carries:");
		FindNamed(bytes.Data(), out);
		CountElements(bytes.Data(), out, 8);
	}
	Log("B: leave (ok)");
}

/** ROUTE C - application resources: styles, swatches, preferences. Measured 331,797 bytes.
    Now also reports WHAT IS IN THERE, which is the question that decides how much of "the
    changes the eye cannot see" this one call already covers. */
void RouteC(PMString& out)
{
	Log("C: enter");
	Line(out, "-- ROUTE C: ISnippetExport::ExportAppPrefs --");

	ProbeBytes bytes;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "no memory stream");
		Log("C: leave (no stream)");
		return;
	}

	const DWORD began = ::GetTickCount();
	const ErrorCode err = Utils<ISnippetExport>()->ExportAppPrefs(stream);
	const DWORD took = ::GetTickCount() - began;
	stream->Flush();

	Report(out, "ExportAppPrefs", err, bytes, took);
	if (bytes.Size() > 0)
	{
		Line(out, "what the APPLICATION RESOURCES carry:");
		FindNamed(bytes.Data(), out);
		CountElements(bytes.Data(), out, 10);
	}
	Log("C: leave (ok)");
}

//----------------------------------------------------------------------------------------
// The staged attempt on the real question. Each stage is its own function ON PURPOSE.
//----------------------------------------------------------------------------------------

/** STAGE 1 - can the document answer IScript at all? Takes it and lets go. Cannot crash.
    basicme/BscMEScriptProvider.cpp:471 does exactly this, so it is expected to answer. */
void Stage1_DocScript(IDocument* doc, PMString& out)
{
	Log("stage1: enter");
	InterfacePtr<IScript> docScript(doc, UseDefaultIID());
	out.Append("stage 1  document answers IScript: ");
	Line(out, (docScript != nil) ? "yes" : "no");
	Log("stage1: leave");
}

/** STAGE 2 - and does THAT answer IDOMElement? Still only takes it. */
void Stage2_DocScriptToElement(IDocument* doc, PMString& out)
{
	Log("stage2: enter");
	InterfacePtr<IScript> docScript(doc, UseDefaultIID());
	if (docScript == nil)
	{
		Line(out, "stage 2  skipped (no IScript)");
		Log("stage2: leave (no script)");
		return;
	}
	InterfacePtr<IDOMElement> elem(docScript, UseDefaultIID());
	out.Append("stage 2  that IScript answers IDOMElement: ");
	Line(out, (elem != nil) ? "yes" : "no");
	Log("stage2: leave");
}

/** STAGE 3 - the INX script context itself: manager, engine, request context. Takes only. */
void Stage3_InxContext(PMString& out)
{
	Log("stage3: enter");
	InterfacePtr<IScriptManager> mgr(
		Utils<IScriptUtils>()->QueryScriptManager(kINXTraditionalImportScriptManagerBoss));
	out.Append("stage 3  INX script manager: ");
	Line(out, (mgr != nil) ? "yes" : "no");
	if (mgr == nil)
	{
		Log("stage3: leave (no manager)");
		return;
	}
	InterfacePtr<IScriptEngine> engine(mgr->QueryDefaultEngine());
	out.Append("stage 3  its default engine: ");
	Line(out, (engine != nil) ? "yes" : "no");
	Log("stage3: leave");
}

/** STAGE 4 - a proxy script object for the document, MADE IN THE INX CONTEXT, and the element
    from it. Takes only; the export is stage 5. This is the step the crashed route skipped. */
void Stage4_ProxyElement(IDocument* doc, PMString& out)
{
	Log("stage4: enter");
	InterfacePtr<IScriptManager> mgr(
		Utils<IScriptUtils>()->QueryScriptManager(kINXTraditionalImportScriptManagerBoss));
	InterfacePtr<IScriptEngine> engine(mgr != nil ? mgr->QueryDefaultEngine() : nil);
	if (engine == nil)
	{
		Line(out, "stage 4  skipped (no INX engine)");
		Log("stage4: leave (no engine)");
		return;
	}

	InterfacePtr<IScript> proxy(Utils<IScriptUtils>()->CreateProxyScriptObject(
		engine->GetRequestContext(), kDocBoss, kDocumentObjectScriptElement, doc));
	out.Append("stage 4  proxy script object for the document: ");
	Line(out, (proxy != nil) ? "yes" : "no");
	if (proxy == nil)
	{
		Log("stage4: leave (no proxy)");
		return;
	}

	InterfacePtr<IDOMElement> elem(proxy, UseDefaultIID());
	out.Append("stage 4  proxy answers IDOMElement: ");
	Line(out, (elem != nil) ? "yes" : "no");
	Log("stage4: leave");
}

/** STAGE 5 - THE QUESTION. Export the whole document through the proxy element.
    First route here that can crash; everything above only took pointers. */
void Stage5_ExportViaProxy(IDocument* doc, PMString& out)
{
	Log("stage5: enter -- FIRST STAGE THAT CAN CRASH");
	Line(out, "-- STAGE 5: ExportINX with a proxy element made in the INX context --");

	InterfacePtr<IScriptManager> mgr(
		Utils<IScriptUtils>()->QueryScriptManager(kINXTraditionalImportScriptManagerBoss));
	InterfacePtr<IScriptEngine> engine(mgr != nil ? mgr->QueryDefaultEngine() : nil);
	if (engine == nil)
	{
		Line(out, "skipped (no INX engine)");
		Log("stage5: leave (no engine)");
		return;
	}
	InterfacePtr<IScript> proxy(Utils<IScriptUtils>()->CreateProxyScriptObject(
		engine->GetRequestContext(), kDocBoss, kDocumentObjectScriptElement, doc));
	InterfacePtr<IDOMElement> elem(proxy, UseDefaultIID());
	if (elem == nil)
	{
		Line(out, "skipped (no element)");
		Log("stage5: leave (no element)");
		return;
	}

	ISession* session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inx(session != nil ? session->QueryINXManager() : nil);
	if (inx == nil)
	{
		Line(out, "skipped (no IINXManager)");
		Log("stage5: leave (no manager)");
		return;
	}

	ProbeBytes bytes;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage5: leave (no stream)");
		return;
	}

	IDOMElement::ElementList roots;
	roots.push_back(elem);

	Log("stage5: about to call ExportINX");
	const DWORD began = ::GetTickCount();
	const ErrorCode err = inx->ExportINX(roots, nil, stream, kSuppressUI);
	const DWORD took = ::GetTickCount() - began;
	Log("stage5: ExportINX returned");
	stream->Flush();

	Report(out, "ExportINX(proxy)", err, bytes, took);
	if (bytes.Size() > 0)
		CountElements(bytes.Data(), out, 14);
	Log("stage5: leave (ok)");
}

/** STAGE 6 - the same, wrapped in Begin/EndExportSession. The header says a session is for
    exporting SEVERAL snippets efficiently, not that it is required - so this asks whether it is
    required in practice. Only reached if stage 5 survived. */
void Stage6_ExportInSession(IDocument* doc, PMString& out)
{
	Log("stage6: enter");
	Line(out, "-- STAGE 6: the same, inside Begin/EndExportSession --");

	InterfacePtr<IScriptManager> mgr(
		Utils<IScriptUtils>()->QueryScriptManager(kINXTraditionalImportScriptManagerBoss));
	InterfacePtr<IScriptEngine> engine(mgr != nil ? mgr->QueryDefaultEngine() : nil);
	InterfacePtr<IScript> proxy(engine != nil ? Utils<IScriptUtils>()->CreateProxyScriptObject(
		engine->GetRequestContext(), kDocBoss, kDocumentObjectScriptElement, doc) : nil);
	InterfacePtr<IDOMElement> elem(proxy, UseDefaultIID());
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inx(session != nil ? session->QueryINXManager() : nil);
	if (elem == nil || inx == nil)
	{
		Line(out, "skipped (no element or no manager)");
		Log("stage6: leave (missing part)");
		return;
	}

	ProbeBytes bytes;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage6: leave (no stream)");
		return;
	}

	IDOMElement::ElementList roots;
	roots.push_back(elem);

	Log("stage6: about to call ExportINX in a session");
	const DWORD began = ::GetTickCount();
	inx->BeginExportSession();
	const ErrorCode err = inx->ExportINX(roots, nil, stream, kSuppressUI);
	inx->EndExportSession();
	const DWORD took = ::GetTickCount() - began;
	Log("stage6: returned");
	stream->Flush();

	Report(out, "ExportINX(proxy, in session)", err, bytes, took);
	Log("stage6: leave (ok)");
}

/** STAGE 9 - a STORY on its own, through the InCopy interchange export.

    ISnippetExport.h:71 says in as many words: 'Export a single page item OR STORY to a stream'.
    That is the unit the eye-invisible text changes live in - character attributes, applied
    styles, overrides - without the frame around it.
    (The same Doxygen also demands an IXMLFragment interface. That type does not exist anywhere
    in the SDK - a stale name copied into three places - so only IDOMElement is really wanted.) */
void Stage9_ExportStory(IDocument* doc, PMString& out)
{
	Log("stage9: enter -- CAN CRASH");
	Line(out, "-- STAGE 9: ExportInCopyInterchange on ONE STORY --");

	InterfacePtr<IStoryList> storyList(doc, UseDefaultIID());
	if (storyList == nil)
	{
		Line(out, "skipped (the document has no IStoryList)");
		Log("stage9: leave (no story list)");
		return;
	}
	LineNum(out, "user accessible stories: ", storyList->GetUserAccessibleStoryCount());
	if (storyList->GetUserAccessibleStoryCount() < 1)
	{
		Line(out, "skipped (no story)");
		Log("stage9: leave (no story)");
		return;
	}

	const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(0);
	InterfacePtr<IDOMElement> elem(storyRef, UseDefaultIID());
	out.Append("the story answers IDOMElement: ");
	Line(out, (elem != nil) ? "yes" : "no");

	ProbeBytes bytes;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage9: leave (no stream)");
		return;
	}

	UIDList one(storyRef);
	Log("stage9: about to call ExportInCopyInterchange");
	const DWORD began = ::GetTickCount();
	const ErrorCode err = Utils<ISnippetExport>()->ExportInCopyInterchange(stream, one);
	const DWORD took = ::GetTickCount() - began;
	Log("stage9: returned");
	stream->Flush();

	Report(out, "ExportInCopyInterchange(story)", err, bytes, took);
	if (bytes.Size() > 0)
		CountElements(bytes.Data(), out, 12);
	Log("stage9: leave (ok)");
}

/** STAGE 4b - the proxy again, with the other plausible parents. The header documents none of
    its five arguments, so which one is 'parent' is a question for the machine, not for reading.
    Takes pointers only; cannot crash. */
void Stage4b_ProxyOtherParents(IDocument* doc, PMString& out)
{
	Log("stage4b: enter");
	InterfacePtr<IScriptManager> mgr(
		Utils<IScriptUtils>()->QueryScriptManager(kINXTraditionalImportScriptManagerBoss));
	InterfacePtr<IScriptEngine> engine(mgr != nil ? mgr->QueryDefaultEngine() : nil);
	if (engine == nil)
	{
		Line(out, "stage 4b skipped (no INX engine)");
		Log("stage4b: leave (no engine)");
		return;
	}

	// (a) parent = the application's script object, which is what a document hangs under.
	InterfacePtr<IScript> appScript(Utils<IScriptUtils>()->QueryApplicationScript());
	InterfacePtr<IScript> viaApp(appScript != nil ? Utils<IScriptUtils>()->CreateProxyScriptObject(
		engine->GetRequestContext(), kDocBoss, kDocumentObjectScriptElement, appScript) : nil);
	out.Append("stage 4b proxy with parent = application script: ");
	Line(out, (viaApp != nil) ? "yes" : "no");

	// (b) parent = nil, in case the call means 'no parent, this IS the root'.
	InterfacePtr<IScript> viaNil(Utils<IScriptUtils>()->CreateProxyScriptObject(
		engine->GetRequestContext(), kDocBoss, kDocumentObjectScriptElement, nil));
	out.Append("stage 4b proxy with parent = nil: ");
	Line(out, (viaNil != nil) ? "yes" : "no");

	// (c) the document's own IScript as the parent - the shape stage 2 proved reachable.
	InterfacePtr<IScript> docScript(doc, UseDefaultIID());
	InterfacePtr<IScript> viaDoc(docScript != nil ? Utils<IScriptUtils>()->CreateProxyScriptObject(
		engine->GetRequestContext(), kDocBoss, kDocumentObjectScriptElement, docScript) : nil);
	out.Append("stage 4b proxy with parent = the document\'s own IScript: ");
	Line(out, (viaDoc != nil) ? "yes" : "no");
	Log("stage4b: leave");
}

/** STAGE 7 - CLOSED. ExportINX on a SPREAD's element killed InDesign on 2026-09-08.

    The probe log is what said so, and it said it exactly: 'stage7: about to call ExportINX
    on a spread' with no matching 'returned'. Entered and never left.

    Taken with Route A (document root, same call, same ending), the finding is that ExportINX
    dies WHATEVER root it is given - so the fault is not the size or kind of the root, and
    building the element some other way is not going to help. The route is closed. */
void Stage7_ExportSpreadElement(IDocument* /*doc*/, PMString& out)
{
	Line(out, "-- STAGE 7: CLOSED. ExportINX on a spread killed InDesign (2026-09-08). --");
}

/** STAGE 8 - CLOSED for the same reason, without being tried: stage 7 showed a SMALLER root
    dies too, so a document-sized one has nothing left to prove. */
void Stage8_ExportDocScriptElement(IDocument* /*doc*/, PMString& out)
{
	Line(out, "-- STAGE 8: CLOSED. A smaller root already died, so this was not tried. --");
}

/** STAGE 11 - GetProperties on the DOCUMENT, using the request data our own property arrived on.

    CScriptProvider.cpp:1254-1268 does exactly this to implement obj.properties: ask IScriptUtils
    for the handler belonging to the request's context, call GetProperties, then read the answer
    back out of the SAME data with GetAllReturnData.

    THE RETURN DATA IS CLEARED AFTERWARDS, and that is not tidiness: this data carries OUR
    property's answer back to the script, so anything left on it would be handed to the caller as
    part of app.kcmInxProbe. The stock implementation clears it for the same reason. */
void Stage11_PropertiesOfDocument(IDocument* doc, IScriptRequestData* data, PMString& out)
{
	Log("stage11: enter");
	Line(out, "-- STAGE 11: GetProperties on the document --");
	if (data == nil)
	{
		Line(out, "skipped (no request data - not called from a script)");
		Log("stage11: leave (no data)");
		return;
	}

	InterfacePtr<IScript> docScript(doc, UseDefaultIID());
	InterfacePtr<IScriptRequestHandler> handler(
		Utils<IScriptUtils>()->QueryScriptRequestHandler(data->GetRequestContext()));
	out.Append("script: ");
	out.Append((docScript != nil) ? "yes" : "no");
	out.Append("   request handler: ");
	Line(out, (handler != nil) ? "yes" : "no");
	if (docScript == nil || handler == nil)
	{
		Log("stage11: leave (missing part)");
		return;
	}

	Log("stage11: about to call GetProperties");
	const ErrorCode err = handler->GetProperties(docScript, data);
	Log("stage11: GetProperties returned");
	LineNum(out, "GetProperties ErrorCode (0 = kSuccess): ", static_cast<int32>(err));

	const ScriptRecordData srd =
		IScriptRequestData::ConvertToScriptRecordData(data->GetAllReturnData(docScript));
	LineNum(out, "properties returned: ", static_cast<int32>(srd.size()));
	data->ClearReturnData(docScript);	// ours must reach the caller unpolluted
	Log("stage11: leave (ok)");
}

/** STAGE 12 - the same, on a PARAGRAPH STYLE. The one the feature needs: it is how 'what changed
    inside this style' gets answered without listing hundreds of attributes by hand.

    The style is found the way KIDMCPDefs finds all five kinds - one walk of the hierarchy,
    telling a style from a group by GetHierarchyType. */
void Stage12_PropertiesOfStyle(IDocument* doc, IScriptRequestData* data, PMString& out)
{
	Log("stage12: enter");
	Line(out, "-- STAGE 12: GetProperties on a PARAGRAPH STYLE --");
	if (data == nil)
	{
		Line(out, "skipped (no request data)");
		Log("stage12: leave (no data)");
		return;
	}

	InterfacePtr<IStyleGroupManager> manager(doc->GetDocWorkSpace(), IID_IPARASTYLEGROUPMANAGER);
	IStyleGroupHierarchy* const root = (manager != nil) ? manager->GetRootHierarchy() : nil;
	IDataBase* const db = (manager != nil) ? ::GetDataBase(manager) : nil;
	out.Append("style manager: ");
	Line(out, (root != nil && db != nil) ? "yes" : "no");
	if (root == nil || db == nil)
	{
		Log("stage12: leave (no manager)");
		return;
	}

	UIDList nodes(db);
	root->GetDescendents(&nodes, IID_ISTYLEGROUPHIERARCHY);
	LineNum(out, "nodes under the paragraph style set: ", nodes.Length());

	UIDRef styleRef = UIDRef::gNull;
	for (int32 i = 0; i < nodes.Length(); ++i)
	{
		InterfacePtr<IStyleGroupHierarchy> node(nodes.GetRef(i), UseDefaultIID());
		if (node != nil && node->GetHierarchyType() == IStyleGroupHierarchy::kHierarchyTypeStyle)
		{
			styleRef = nodes.GetRef(i);
			break;
		}
	}
	if (styleRef == UIDRef::gNull)
	{
		Line(out, "no style found under the set");
		Log("stage12: leave (no style)");
		return;
	}

	InterfacePtr<IScript> styleScript(styleRef, UseDefaultIID());
	InterfacePtr<IScriptRequestHandler> handler(
		Utils<IScriptUtils>()->QueryScriptRequestHandler(data->GetRequestContext()));
	out.Append("the style answers IScript: ");
	Line(out, (styleScript != nil) ? "yes" : "no");
	if (styleScript == nil || handler == nil)
	{
		Log("stage12: leave (missing part)");
		return;
	}

	Log("stage12: about to call GetProperties on a style");
	const ErrorCode err = handler->GetProperties(styleScript, data);
	Log("stage12: GetProperties returned");
	LineNum(out, "GetProperties ErrorCode (0 = kSuccess): ", static_cast<int32>(err));

	const ScriptRecordData srd =
		IScriptRequestData::ConvertToScriptRecordData(data->GetAllReturnData(styleScript));
	LineNum(out, "properties returned: ", static_cast<int32>(srd.size()));
	data->ClearReturnData(styleScript);
	Log("stage12: leave (ok)");
}

/** Name the export providers we already know, so the catalogue reads as something other than a
    list of anonymous formats. Comparison only: ClassID compares, and that is all that is needed
    here - the SDK promises no numeric value for one. */
const char* KnownProviderName(const ClassID& cls)
{
	if (cls == kSaveBackExportProviderBoss)			return "SaveBack -- IDML";
	if (cls == kSnippetExportProviderBoss)			return "Snippet -- IDMS";
	if (cls == kSnippetStructureExportProviderBoss)	return "Snippet structure";
	if (cls == kXMLExportProviderBoss)				return "XML";
	return "";
}

/** STAGE 13 - the catalogue. Every registered export provider, and every format name it answers
    to. Reading only: CountFormats and GetNthFormatName ask the provider about itself and touch
    no document, so this stage cannot break one.

    It has to come first because STAGE 14 needs a format name and THE SDK NEVER WRITES ONE DOWN
    for IDML - not in a header, not in a sample. The only way to learn it is to ask the running
    application. Worth keeping for its own sake too: this is the only list of what this InDesign
    can be asked to write. */
void Stage13_ListExportFormats(IDocument* doc, PMString& out)
{
	Log("stage13: enter -- reading only");
	Line(out, "-- STAGE 13: every export provider and the format names it answers to --");

	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		Line(out, "skipped (no service registry)");
		Log("stage13: leave (no registry)");
		return;
	}

	const int32 count = registry->GetServiceProviderCount(kExportProviderService);
	LineNum(out, "export providers registered: ", count);

	for (int32 i = 0; i < count; ++i)
	{
		InterfacePtr<IK2ServiceProvider> provider(
			registry->QueryNthServiceProvider(kExportProviderService, i));
		if (provider == nil)
			continue;
		InterfacePtr<IExportProvider> exporter(provider, IID_IEXPORTPROVIDER);
		if (exporter == nil)
			continue;

		const ClassID cls = ::GetClass(provider);
		const char* const known = KnownProviderName(cls);
		const int32 formats = exporter->CountFormats();

		for (int32 f = 0; f < formats; ++f)
		{
			PMString name = exporter->GetNthFormatName(f);
			name.SetTranslatable(kFalse);
			out.Append("  [");
			out.Append(name);
			out.Append("] toFile=");
			out.Append(exporter->CanExportToFile() ? "y" : "n");
			out.Append(" thisDoc=");
			out.Append(exporter->CanExportThisFormat(doc, nil, name) ? "y" : "n");
			if (known[0] != 0)
			{
				out.Append("   <== ");
				out.Append(known);
			}
			out.Append("\n");
		}
	}
	Log("stage13: leave (ok)");
}

/** STAGE 14 - THE QUESTION. Ask the IDML export provider to write the WHOLE DOCUMENT into a
    stream that lives in memory.

    IExportProvider.h:77 puts ExportToStream beside ExportToFile and hands it an IDocument*, so
    the unit is the document, not the selection. kSaveBackExportProviderBoss is the IDML provider
    (SaveBack is the plug-in behind 'export as INX'; ScriptingDefs.h:622 still says so in its
    heading), and the dictionary taken from the running application says it implements
    IID_IEXPORTPROVIDER.

    Shape copied from codesnippets/SnpImportExportXML.cpp:189-221: address the provider by
    ClassID, ask CanExportThisFormat, then ExportToStream. That snippet is also the only place
    that says where the error goes - ExportToStream returns void, and failure arrives on the
    GLOBAL error code. */
void Stage14_ExportDocumentToStream(IDocument* doc, ProbeBytes& bytes, PMString& out, PMString& usedFormat)
{
	Log("stage14: enter -- CAN CRASH");
	Line(out, "-- STAGE 14: ExportToStream on the WHOLE DOCUMENT, into memory --");

	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		Line(out, "skipped (no service registry)");
		Log("stage14: leave (no registry)");
		return;
	}

	InterfacePtr<IK2ServiceProvider> provider(
		registry->QueryServiceProviderByClassID(kExportProviderService, kSaveBackExportProviderBoss));
	out.Append("the SaveBack (IDML) provider answers: ");
	Line(out, (provider != nil) ? "yes" : "no");
	if (provider == nil)
	{
		Log("stage14: leave (no provider)");
		return;
	}

	InterfacePtr<IExportProvider> exporter(provider, IID_IEXPORTPROVIDER);
	out.Append("it answers IExportProvider: ");
	Line(out, (exporter != nil) ? "yes" : "no");
	if (exporter == nil)
	{
		Log("stage14: leave (no IExportProvider)");
		return;
	}

	// Take the first format it says it can write FOR THIS DOCUMENT. Asking is the point: the
	// name is not knowable from the SDK.
	PMString chosen;
	chosen.SetTranslatable(kFalse);
	const int32 formats = exporter->CountFormats();
	LineNum(out, "formats this provider offers: ", formats);
	for (int32 f = 0; f < formats; ++f)
	{
		PMString name = exporter->GetNthFormatName(f);
		name.SetTranslatable(kFalse);
		const bool16 can = exporter->CanExportThisFormat(doc, nil, name);
		out.Append("  format [");
		out.Append(name);
		out.Append("] canExportThisFormat=");
		Line(out, can ? "yes" : "no");
		if (can && chosen.IsEmpty())
			chosen = name;
	}
	if (chosen.IsEmpty())
	{
		Line(out, "no format was accepted for this document - NOTHING WAS TRIED");
		Log("stage14: leave (no format accepted)");
		return;
	}

	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage14: leave (no stream)");
		return;
	}

	if (!EnsureStreamOpen(stream, out))
	{
		Line(out, "the stream is NOT good - nothing was tried (this is the 2026-09-09 crash guard)");
		return;
	}
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// clear whatever was standing before we ask
	Log("stage14: about to call ExportToStream");
	const DWORD began = ::GetTickCount();
	exporter->ExportToStream(stream, doc, nil, chosen, kSuppressUI);
	const DWORD took = ::GetTickCount() - began;
	Log("stage14: ExportToStream returned");
	stream->Flush();

	const ErrorCode err = ErrorUtils::PMGetGlobalErrorCode();
	out.Append("ExportToStream global ErrorCode (0 = kSuccess): ");
	out.AppendNumber(static_cast<int32>(err));
	out.Append("\n");
	LineNum(out, "bytes: ", static_cast<int32>(bytes.Size()));
	LineNum(out, "milliseconds: ", static_cast<int32>(took));

	// A UCF package IS a zip, so the first four bytes have to be 'P','K',03,04. Checking the
	// signature rather than printing the head keeps binary out of a PMString, and it answers a
	// sharper question than a byte count does: did we get a PACKAGE, or some text?
	if (bytes.Size() >= 4)
	{
		const std::string& d = bytes.Data();
		const bool16 isZip = (d[0] == 'P' && d[1] == 'K' &&
			static_cast<uchar>(d[2]) == 0x03 && static_cast<uchar>(d[3]) == 0x04);
		out.Append("starts with the zip signature PK 03 04: ");
		Line(out, isZip ? "yes" : "no");
		if (!isZip)
		{
			out.Append("first line: ");
			out.Append(bytes.FirstLine(160));
			out.Append("\n");
		}
	}

	if (bytes.Size() > 0)
		usedFormat = chosen;
	Log("stage14: leave (ok)");
}

/** STAGE 15 - and can we read it back without ever touching the disk?

    IUCFPackageUtils has an IPMStream overload beside every IDFile one (IUCFPackageUtils.h:134,137),
    so a package that only exists in memory can be opened and a single member read out of it -
    no unzip, no temporary folder.

    The count at the end is the part that matters. A byte count only proves that something came
    out; comparing the Story components named in designmap.xml against the stories the live
    document actually has is what proves the bytes are THIS DOCUMENT. (2026-09-08 taught this the
    hard way: ExportAppPrefs produced 331,797 bytes that did not move when the document changed.) */
void Stage15_OpenAsUcfPackage(IDocument* doc, ProbeBytes& bytes, PMString& out)
{
	Log("stage15: enter");
	Line(out, "-- STAGE 15: open those bytes as a UCF package, in memory --");

	if (bytes.Size() == 0)
	{
		Line(out, "skipped (stage 14 produced no bytes)");
		Log("stage15: leave (no bytes)");
		return;
	}

	bytes.Seek(0, kSeekFromStart);
	InterfacePtr<IPMStream> in(StreamUtil::CreateMemoryStreamRead(&bytes));
	if (in == nil)
	{
		Line(out, "skipped (no read stream)");
		Log("stage15: leave (no stream)");
		return;
	}

	IUCFPackageUtils::UCFErrorCode err = IUCFPackageUtils::kSuccess;
	Log("stage15: about to call OpenPackage");
	IUCFPackageUtils::PackageRefPtr ref = Utils<IUCFPackageUtils>()->OpenPackage(in, err);
	Log("stage15: OpenPackage returned");
	LineNum(out, "OpenPackage UCFErrorCode (0 = success): ", static_cast<int32>(err));
	out.Append("package opened: ");
	Line(out, (ref != nil) ? "yes" : "no");
	if (ref == nil)
	{
		Log("stage15: leave (not a package)");
		return;
	}

	static const char* const kPaths[] = {
		"mimetype", "designmap.xml", "META-INF/container.xml",
		"Resources/Styles.xml", "Resources/Fonts.xml", "Resources/Graphic.xml",
		"Resources/Preferences.xml", "XML/Tags.xml", "XML/BackingStory.xml" };

	Line(out, "members present:");
	for (int32 p = 0; p < static_cast<int32>(sizeof(kPaths) / sizeof(kPaths[0])); ++p)
	{
		PMString path(kPaths[p]);
		path.SetTranslatable(kFalse);
		const bool16 exists = Utils<IUCFPackageUtils>()->FileExists(ref, WideString(path));
		out.Append("  [");
		out.Append(exists ? "y" : "n");
		out.Append("] ");
		out.Append(kPaths[p]);
		out.Append("\n");
	}

	// designmap.xml is the one member worth reading: it names every other component, so it says
	// how many stories and spreads the package believes the document has.
	PMString dmName("designmap.xml");
	dmName.SetTranslatable(kFalse);
	Log("stage15: about to open designmap.xml");
	InterfacePtr<IPMStream> dm(Utils<IUCFPackageUtils>()->OpenStream(ref, WideString(dmName)));
	Log("stage15: OpenStream returned");
	if (dm != nil)
	{
		std::string text;
		uchar buf[4096];
		int32 got = 0;
		while ((got = dm->XferByte(buf, static_cast<int32>(sizeof(buf)))) > 0)
			text.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(got));
		dm->Close();

		LineNum(out, "designmap.xml bytes read: ", static_cast<int32>(text.size()));

		int32 stories = 0;
		for (size_t at = text.find("Stories/Story"); at != std::string::npos;
			 at = text.find("Stories/Story", at + 1))
			++stories;
		int32 spreads = 0;
		for (size_t at = text.find("Spreads/Spread"); at != std::string::npos;
			 at = text.find("Spreads/Spread", at + 1))
			++spreads;

		LineNum(out, "Story components named in designmap:  ", stories);
		LineNum(out, "Spread components named in designmap: ", spreads);

		InterfacePtr<IStoryList> storyList(doc, UseDefaultIID());
		if (storyList != nil)
			LineNum(out, "  ...the LIVE document has this many user stories: ",
				storyList->GetUserAccessibleStoryCount());
		InterfacePtr<ISpreadList> spreadList(doc, UseDefaultIID());
		if (spreadList != nil)
			LineNum(out, "  ...the LIVE document has this many spreads:       ",
				spreadList->GetSpreadCount());
	}
	else
	{
		Line(out, "designmap.xml could not be opened");
	}

	Utils<IUCFPackageUtils>()->ClosePackage(ref);
	Log("stage15: leave (ok)");
}

/** STAGE 16 - THE CONTROL, and it has to come before any conclusion about stage 14.

    Stage 14 got 0 bytes and no error. That has two possible causes and they are not the same:
    the provider wrote nothing, or MY STREAM cannot be written to through this route. Measuring
    one without the other cannot tell them apart.

    So: the XML provider, called exactly the way codesnippets/SnpImportExportXML.cpp:189-221
    calls it - ClassID-addressed, targetboss nil, format "XML" - into the same kind of memory
    stream. The DOM says an XML export of this document is about 70 bytes, so ~70 here means the
    route works and stage 14's silence belongs to SaveBack; 0 here means the fault is mine. */
void Stage16_ControlXmlProviderToStream(IDocument* doc, PMString& out)
{
	Log("stage16: enter -- CONTROL");
	Line(out, "-- STAGE 16: CONTROL - the XML provider into a memory stream (nil targetboss) --");

	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		Line(out, "skipped (no service registry)");
		Log("stage16: leave (no registry)");
		return;
	}
	InterfacePtr<IK2ServiceProvider> provider(
		registry->QueryServiceProviderByClassID(kExportProviderService, kXMLExportProviderBoss));
	InterfacePtr<IExportProvider> exporter(provider, IID_IEXPORTPROVIDER);
	if (exporter == nil)
	{
		Line(out, "skipped (no XML export provider)");
		Log("stage16: leave (no provider)");
		return;
	}

	PMString formatName("XML");
	formatName.SetTranslatable(kFalse);
	out.Append("CanExportThisFormat(doc, nil, \"XML\"): ");
	Line(out, exporter->CanExportThisFormat(doc, nil, formatName) ? "yes" : "no");

	ProbeBytes bytes;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage16: leave (no stream)");
		return;
	}

	if (!EnsureStreamOpen(stream, out))
	{
		Line(out, "the stream is NOT good - nothing was tried (this is the 2026-09-09 crash guard)");
		return;
	}
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	Log("stage16: about to call ExportToStream (XML)");
	const DWORD began = ::GetTickCount();
	exporter->ExportToStream(stream, doc, nil, formatName, kSuppressUI);
	const DWORD took = ::GetTickCount() - began;
	Log("stage16: ExportToStream returned");
	stream->Flush();

	const ErrorCode err = ErrorUtils::PMGetGlobalErrorCode();
	Report(out, "ExportToStream(XML, nil targetboss)", err, bytes, took);
	Line(out, (bytes.Size() > 0)
		? ">> THE ROUTE WORKS. A memory stream can be written through ExportToStream."
		: ">> THE ROUTE PRODUCED NOTHING EITHER - suspect the stream, not the provider.");
	Log("stage16: leave (ok)");
}

/** STAGE 17 - stage 14 again, changing ONE thing: targetboss.

    Why this one: the only ExportToStream implementation shipped in source form
    (open/components/incopyexport/export/InCopyStoryExportProvider.cpp:339-353) does this and
    nothing else -

        InterfacePtr<IExportProvider> p(targetBoss, IID_IINCOPYEXPORTSUITE);
        if (p) { p->ExportToStream(...); }        // and if targetBoss is nil: RETURNS SILENTLY

    - which is exactly stage 14's symptom: no bytes, no time, no error. codesnippets/
    SnpExportEPub.cpp:102-119 passes the active selection there, so that is what we pass. */
void Stage17_SaveBackWithSelection(IDocument* doc, ProbeBytes& bytes, PMString& out)
{
	Log("stage17: enter -- CAN CRASH");
	Line(out, "-- STAGE 17: SaveBack again, with the ACTIVE SELECTION as targetboss --");

	if (bytes.Size() > 0)
	{
		Line(out, "skipped (an earlier stage already produced bytes)");
		Log("stage17: leave (already have bytes)");
		return;
	}

	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		Line(out, "skipped (no service registry)");
		Log("stage17: leave (no registry)");
		return;
	}
	InterfacePtr<IK2ServiceProvider> provider(
		registry->QueryServiceProviderByClassID(kExportProviderService, kSaveBackExportProviderBoss));
	InterfacePtr<IExportProvider> exporter(provider, IID_IEXPORTPROVIDER);
	if (exporter == nil)
	{
		Line(out, "skipped (no SaveBack provider)");
		Log("stage17: leave (no provider)");
		return;
	}

	InterfacePtr<ISelectionManager> selection(Utils<ISelectionUtils>()->QueryActiveSelection());
	out.Append("the active selection answers: ");
	Line(out, (selection != nil) ? "yes" : "no");

	PMString formatName("InDesignMarkup");
	formatName.SetTranslatable(kFalse);
	out.Append("CanExportThisFormat(doc, selection, \"InDesignMarkup\"): ");
	Line(out, exporter->CanExportThisFormat(doc, selection, formatName) ? "yes" : "no");

	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage17: leave (no stream)");
		return;
	}

	if (!EnsureStreamOpen(stream, out))
	{
		Line(out, "the stream is NOT good - nothing was tried (this is the 2026-09-09 crash guard)");
		return;
	}
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	Log("stage17: about to call ExportToStream (selection as targetboss)");
	const DWORD began = ::GetTickCount();
	exporter->ExportToStream(stream, doc, selection, formatName, kSuppressUI);
	const DWORD took = ::GetTickCount() - began;
	Log("stage17: ExportToStream returned");
	stream->Flush();

	const ErrorCode err = ErrorUtils::PMGetGlobalErrorCode();
	out.Append("ExportToStream(selection) global ErrorCode (0 = kSuccess): ");
	out.AppendNumber(static_cast<int32>(err));
	out.Append("\n");
	LineNum(out, "bytes: ", static_cast<int32>(bytes.Size()));
	LineNum(out, "milliseconds: ", static_cast<int32>(took));
	if (bytes.Size() >= 4)
	{
		const std::string& d = bytes.Data();
		const bool16 isZip = (d[0] == 'P' && d[1] == 'K' &&
			static_cast<uchar>(d[2]) == 0x03 && static_cast<uchar>(d[3]) == 0x04);
		out.Append("starts with the zip signature PK 03 04: ");
		Line(out, isZip ? "yes" : "no");
	}
	Log("stage17: leave (ok)");
}

/** STAGE 18 - and if the selection was not the missing piece, try the OTHER difference between
    my call and the snippet's: SnpImportExportXML opens its stream and checks the state before
    handing it over. A memory stream is usually live already - RouteA writes to one without
    opening it - but "usually" is not a measurement. One variable again: Open() added. */
void Stage18_SaveBackWithOpenedStream(IDocument* doc, ProbeBytes& bytes, PMString& out)
{
	Log("stage18: enter -- CAN CRASH");
	Line(out, "-- STAGE 18: SaveBack with selection AND an explicitly opened stream --");

	if (bytes.Size() > 0)
	{
		Line(out, "skipped (an earlier stage already produced bytes)");
		Log("stage18: leave (already have bytes)");
		return;
	}

	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		Line(out, "skipped (no service registry)");
		Log("stage18: leave (no registry)");
		return;
	}
	InterfacePtr<IK2ServiceProvider> provider(
		registry->QueryServiceProviderByClassID(kExportProviderService, kSaveBackExportProviderBoss));
	InterfacePtr<IExportProvider> exporter(provider, IID_IEXPORTPROVIDER);
	if (exporter == nil)
	{
		Line(out, "skipped (no SaveBack provider)");
		Log("stage18: leave (no provider)");
		return;
	}

	InterfacePtr<ISelectionManager> selection(Utils<ISelectionUtils>()->QueryActiveSelection());
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage18: leave (no stream)");
		return;
	}

	stream->Open();
	LineNum(out, "stream state after Open() (0 = kStreamStateGood): ",
		static_cast<int32>(stream->GetStreamState()));

	PMString formatName("InDesignMarkup");
	formatName.SetTranslatable(kFalse);

	if (!EnsureStreamOpen(stream, out))
	{
		Line(out, "the stream is NOT good - nothing was tried (this is the 2026-09-09 crash guard)");
		return;
	}
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	Log("stage18: about to call ExportToStream (opened stream)");
	const DWORD began = ::GetTickCount();
	exporter->ExportToStream(stream, doc, selection, formatName, kSuppressUI);
	const DWORD took = ::GetTickCount() - began;
	Log("stage18: ExportToStream returned");
	stream->Flush();

	const ErrorCode err = ErrorUtils::PMGetGlobalErrorCode();
	out.Append("ExportToStream(opened) global ErrorCode (0 = kSuccess): ");
	out.AppendNumber(static_cast<int32>(err));
	out.Append("\n");
	LineNum(out, "bytes: ", static_cast<int32>(bytes.Size()));
	LineNum(out, "milliseconds: ", static_cast<int32>(took));
	Log("stage18: leave (ok)");
}

/** STAGE 19 - the INX question, with the one variable that was never tried: a POLICY.

    Stage 5 and 6 never reached ExportINX at all (CreateProxyScriptObject returns nil), and the
    two crashes on 2026-09-08 were on a root taken OUTSIDE the INX context with policy = nil.
    IINXExportPolicy has no header in the SDK - only a forward declaration - but 14 bosses
    implement it in the running application, and the product shows how to hold a type like that:
    open/components/incopyimport/import/InCopyImportProvider.cpp:413 casts the result of
    ::CreateObject with a C cast. kDocElementExportBoss is the one export policy with no public
    method of its own, which is why it is the candidate.

    THIS ONE CAN KILL INDESIGN. It is last for that reason, and the log names it before it goes. */
void Stage19_ExportInxWithPolicy(IDocument* doc, PMString& out)
{
	Log("stage19: enter -- CAN CRASH (ExportINX with a real policy)");
	Line(out, "-- STAGE 19: ExportINX with policy = kDocElementExportBoss --");

	ISession* session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inx(session != nil ? session->QueryINXManager() : nil);
	if (inx == nil)
	{
		Line(out, "skipped (no IINXManager)");
		Log("stage19: leave (no manager)");
		return;
	}

	InterfacePtr<IDOMElement> docElement(doc, UseDefaultIID());
	out.Append("the document answers IDOMElement directly: ");
	Line(out, (docElement != nil) ? "yes" : "no");
	if (docElement == nil)
	{
		Line(out, "skipped (no element)");
		Log("stage19: leave (no element)");
		return;
	}

	// The policy type is incomplete here, so hold the reference as IPMUnknown and cast the
	// pointer we pass. The QI was by IID_IINXEXPORTPOLICY, so the vtable is the right one.
	InterfacePtr<IPMUnknown> policyHolder(
		(IPMUnknown*)::CreateObject(kDocElementExportBoss, IID_IINXEXPORTPOLICY));
	out.Append("kDocElementExportBoss gave an IINXEXPORTPOLICY: ");
	Line(out, (policyHolder != nil) ? "yes" : "no");
	if (policyHolder == nil)
	{
		Line(out, "skipped (no policy) - NOTHING WAS TRIED");
		Log("stage19: leave (no policy)");
		return;
	}
	IINXExportPolicy* policy = (IINXExportPolicy*)policyHolder.get();

	ProbeBytes bytes;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage19: leave (no stream)");
		return;
	}

	IDOMElement::ElementList roots;
	roots.push_back(docElement);

	if (!EnsureStreamOpen(stream, out))
	{
		Line(out, "the stream is NOT good - nothing was tried");
		return;
	}
	Log("stage19: BeginExportSession");
	inx->BeginExportSession();
	Log("stage19: about to call ExportINX WITH A POLICY");
	const DWORD began = ::GetTickCount();
	const ErrorCode err = inx->ExportINX(roots, policy, stream, kSuppressUI);
	const DWORD took = ::GetTickCount() - began;
	Log("stage19: ExportINX returned");
	inx->EndExportSession();
	Log("stage19: EndExportSession done");
	stream->Flush();

	Report(out, "ExportINX(document root, kDocElementExportBoss)", err, bytes, took);
	Log("stage19: leave (ok)");
}

/** Write the bytes out as text. INX is XML, so this is readable; it goes to the log as well,
    which is the only copy that survives a later stage hanging. Capped, because the whole point
    of the sweep is to find the policy that produces something BIG. */
void DumpText(PMString& out, const ProbeBytes& bytes, int32 limit)
{
	const std::string& d = bytes.Data();
	const int32 have = static_cast<int32>(d.size());
	const int32 n = (have < limit) ? have : limit;
	std::string safe;
	safe.reserve(static_cast<size_t>(n));
	for (int32 i = 0; i < n; ++i)
	{
		const char c = d[static_cast<size_t>(i)];
		safe += (c == '\r') ? '\n' : c;
	}
	Line(out, "---- content ----");
	out.Append(safe.c_str());
	out.Append("\n");
	Log(safe.c_str());
	if (have > n)
		LineNum(out, "...truncated. total bytes: ", have);
	Line(out, "---- end of content ----");
}

/** STAGE 21 - THE SWEEP. ExportINX once per export-policy boss we can name.

    Stage 19 proved the call itself is sound when a policy is supplied: kSuccess, 184 bytes, no
    crash, where the same call with policy=nil killed InDesign twice on 2026-09-08. 184 bytes is
    an XML declaration and very little else, so the question is no longer "does it work" but
    "which policy opens it up".

    The policy type has no header in the SDK - only a forward declaration - so each one is made
    by ClassID and cast, the way open/components/incopyimport/import/InCopyImportProvider.cpp:413
    makes an IINXImportValidation.

    EVERY CANDIDATE IS NAMED IN THE LOG BEFORE IT IS TRIED. If one of them takes InDesign down,
    the last name in the file is the one that did it - the same discipline that placed the
    2026-09-08 crash and the 2026-09-09 hang in one reading each. */
/** STAGE 21 - THE SWEEP. ExportINX once per export-policy boss, over the 14 that the running
    application actually implements IID_IINXEXPORTPOLICY on.

    Stage 19 proved the call is sound when a policy is supplied (kSuccess, 184 bytes, no crash,
    where policy=nil killed InDesign twice on 2026-09-08). The 184 bytes turned out to be a
    declaration and two processing instructions with NO CONTENT - 'SnippetType="DocumentElement"'
    - so kDocElementExportBoss means the XML structure's document element, not the whole document.
    The question this sweep answers is whether ANY policy opens it up.

    RESUMABLE ON PURPOSE. Each candidate is written to a file BEFORE it is tried, and a name
    already in that file is skipped. kSaveBackExportPolicyBoss killed InDesign on 2026-09-09
    inside CINXExportPolicy::OnElementBegin_Internal (crash report read from the reporter window),
    and it took the six candidates after it down with it, unrun. Without this file, every crash
    costs a five-minute rebuild to get past one name; with it, the next run carries on.
    Delete work/kcm-policy-done.txt to start the sweep over.

    ORDER IS BY RISK. The part-shaped policies come first: handed a document root they should
    decline rather than reach into it. The ones carrying their own data (Assignment's IUIDData,
    JBX's policy data) come last, because a policy expecting a target that was never set is the
    shape that just crashed. */
/** STAGE 23 - the winning policy, dumped IN FULL, on every run.

    kActionExportPolicyBoss produced 376,004 bytes with <Document> at the root, in 125ms, into
    memory, with nothing written to disk. Two things are still unknown and both need the WHOLE
    file rather than its first 700 bytes:

      (a) is the CONTENT actually in there - stories, page items, spreads - or only the
          document's own attributes, dressed up by sheer size to look like more?
      (b) does the output MOVE when the document changes?

    (b) is the one that decides it, and it is the lesson of 2026-09-08: ExportAppPrefs produced
    331,797 bytes that did not shift by a SINGLE BYTE when a paragraph style was added. Large and
    useless. A byte count is not evidence; a byte count that changes with the document is.

    So this stage writes the whole thing out every time, unconditionally and OUTSIDE the resume
    list, so it can be run before an edit and again after and the two files compared from
    outside InDesign. */
void Stage23_FullDump(IDocument* doc, PMString& out)
{
	Log("stage23: enter");
	Line(out, "-- STAGE 23: kActionExportPolicyBoss, dumped in full to a file --");

	ISession* session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inx(session != nil ? session->QueryINXManager() : nil);
	InterfacePtr<IDOMElement> docElement(doc, UseDefaultIID());
	if (inx == nil || docElement == nil)
	{
		Line(out, "skipped (no manager or no document element)");
		Log("stage23: leave (missing part)");
		return;
	}

	InterfacePtr<IPMUnknown> holder(
		(IPMUnknown*)::CreateObject(kActionExportPolicyBoss, IID_IINXEXPORTPOLICY));
	if (holder == nil)
	{
		Line(out, "skipped (no policy)");
		Log("stage23: leave (no policy)");
		return;
	}
	IINXExportPolicy* policy = (IINXExportPolicy*)holder.get();

	ProbeBytes bytes;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
	if (stream == nil)
	{
		Line(out, "skipped (no stream)");
		Log("stage23: leave (no stream)");
		return;
	}
	if (stream->GetStreamState() != kStreamStateGood)
		stream->Open();
	stream->SetEndOfStream();

	IDOMElement::ElementList roots;
	roots.push_back(docElement);

	// ★ IDOMElement.h:52-56 - "Since DOM elements CACHE INFORMATION during use, it is best to
	// call the Reset() method on the topmost node ... This method will recursively reset all
	// nodes beneath it."
	//
	// 2026-09-09: measured that an UNSAVED edit does not appear in the export, and that SAVING
	// makes it appear even in the same process. That proves saving is sufficient - it does NOT
	// prove the cache is innocent. If Reset() also makes an unsaved edit appear, then the export
	// can follow a document being EDITED, which is a different tool entirely.
	docElement->Reset();
	Log("stage23: Reset() called on the document element");
	Log("stage23: about to call ExportINX");
	const DWORD began = ::GetTickCount();
	inx->BeginExportSession();
	const ErrorCode err = inx->ExportINX(roots, policy, stream, kSuppressUI);
	inx->EndExportSession();
	const DWORD took = ::GetTickCount() - began;
	Log("stage23: ExportINX returned");
	stream->Flush();

	LineNum(out, "ErrorCode (0 = kSuccess): ", static_cast<int32>(err));
	LineNum(out, "bytes: ", static_cast<int32>(bytes.Size()));
	LineNum(out, "milliseconds: ", static_cast<int32>(took));
	if (bytes.Size() == 0)
	{
		Line(out, "nothing came out");
		Log("stage23: leave (no bytes)");
		return;
	}

	const std::string& d = bytes.Data();
	FILE* f = nil;
	if (::fopen_s(&f, kDumpPath, "wb") == 0 && f != nil)
	{
		::fwrite(d.data(), 1, d.size(), f);
		::fclose(f);
		Line(out, "written in full to work/kcm-inx-action.xml");
	}
	else
	{
		Line(out, "COULD NOT WRITE THE DUMP FILE");
	}

	// (a): what KINDS of thing are in there, without reading 376KB by eye.
	CountElements(d, out, 20);
	Log("stage23: leave (ok)");
}

void Stage21_PolicySweep(IDocument* doc, PMString& out)
{
	Log("stage21: enter -- CAN CRASH (one ExportINX per policy)");
	Line(out, "-- STAGE 21: ExportINX with every export policy boss we can name --");

	ISession* session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inx(session != nil ? session->QueryINXManager() : nil);
	InterfacePtr<IDOMElement> docElement(doc, UseDefaultIID());
	if (inx == nil || docElement == nil)
	{
		Line(out, "skipped (no manager or no document element)");
		Log("stage21: leave (missing part)");
		return;
	}

	struct Candidate { ClassID boss; const char* name; };
	const Candidate candidates[] = {
		// known-good control, and the one whose 184 bytes we already understand
		{ kDocElementExportBoss,			"kDocElementExportBoss" },
		// part-shaped: should decline a document root rather than walk into it
		{ kPageItemExportBoss,				"kPageItemExportBoss" },
		{ kXMLElementExportBoss,			"kXMLElementExportBoss" },
		{ kInCopyInterchangeExportBoss,		"kInCopyInterchangeExportBoss" },
		{ kGraphicStoryExportBoss,			"kGraphicStoryExportBoss" },
		{ kAppPrefsExportBoss,				"kAppPrefsExportBoss" },
		{ kAutoCorrectExportBoss,			"kAutoCorrectExportBoss" },
		{ kActionExportPolicyBoss,			"kActionExportPolicyBoss" },
		{ kPreflightProfileExportPolicyBoss,"kPreflightProfileExportPolicyBoss" },
		// carry their own target data, which is the shape that crashed - so, last
		{ kAssignmentExportFramesPolicyBoss,"kAssignmentExportFramesPolicyBoss" },
		{ kAssignmentExportSpreadsPolicyBoss,"kAssignmentExportSpreadsPolicyBoss" },
		{ kAssignmentExportAllPolicyBoss,	"kAssignmentExportAllPolicyBoss" },
		{ kJBXExportPolicyBoss,				"kJBXExportPolicyBoss" },
		// kSaveBackExportPolicyBoss IS NOT HERE. It crashes: see the note above.
	};
	const int32 count = static_cast<int32>(sizeof(candidates) / sizeof(candidates[0]));

	for (int32 i = 0; i < count; ++i)
	{
		Line(out, "");
		out.Append("== policy: ");
		Line(out, candidates[i].name);

		if (AlreadyTried(candidates[i].name))
		{
			Line(out, "  (tried in an earlier run - skipped)");
			continue;
		}
		MarkTried(candidates[i].name);	// recorded BEFORE it is tried, so a crash is not repeated
		Log(candidates[i].name);

		InterfacePtr<IPMUnknown> holder(
			(IPMUnknown*)::CreateObject(candidates[i].boss, IID_IINXEXPORTPOLICY));
		if (holder == nil)
		{
			Line(out, "  this boss does not give an IINXEXPORTPOLICY - skipped");
			continue;
		}
		IINXExportPolicy* policy = (IINXExportPolicy*)holder.get();

		ProbeBytes bytes;
		InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes));
		if (stream == nil)
		{
			Line(out, "  no stream - skipped");
			continue;
		}
		if (stream->GetStreamState() != kStreamStateGood)
			stream->Open();
		// SnpImportExportSnippet.cpp:183 empties the stream before an INX export.
		stream->SetEndOfStream();

		IDOMElement::ElementList roots;
		roots.push_back(docElement);

		Log("  about to call ExportINX");
		const DWORD began = ::GetTickCount();
		inx->BeginExportSession();
		const ErrorCode err = inx->ExportINX(roots, policy, stream, kSuppressUI);
		inx->EndExportSession();
		const DWORD took = ::GetTickCount() - began;
		Log("  ExportINX returned");
		stream->Flush();

		LineNum(out, "  ErrorCode (0 = kSuccess): ", static_cast<int32>(err));
		LineNum(out, "  bytes: ", static_cast<int32>(bytes.Size()));
		LineNum(out, "  milliseconds: ", static_cast<int32>(took));
		if (bytes.Size() > 0)
			DumpText(out, bytes, 700);
	}
	Log("stage21: leave (ok)");
}

}	// anonymous namespace

void KCMRunInxProbe(PMString& out, IScriptRequestData* data)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	Log("=== probe run ===");

	IDocument* doc = FirstDocument();
	if (doc == nil)
	{
		Line(out, "no document is open - open one and read this property again");
		Log("no document");
		return;
	}

	// Known-good first, so that a crash later still leaves a report with the useful part in it.
	RouteA(out);
	Line(out, "");
	RouteB(doc, out);
	Line(out, "");
	RouteC(out);
	Line(out, "");

	// Then the staged attempt, in order of how much each one can break.
	Line(out, "== staged attempt on the whole document ==");
	Stage1_DocScript(doc, out);
	Stage2_DocScriptToElement(doc, out);
	Stage3_InxContext(out);
	Stage4_ProxyElement(doc, out);
	Line(out, "");
	Stage4b_ProxyOtherParents(doc, out);
	Line(out, "");
	Stage5_ExportViaProxy(doc, out);
	Line(out, "");
	Stage6_ExportInSession(doc, out);
	Line(out, "");
	Stage7_ExportSpreadElement(doc, out);
	Line(out, "");
	Stage8_ExportDocScriptElement(doc, out);
	Line(out, "");
	Stage11_PropertiesOfDocument(doc, data, out);
	Line(out, "");
	Stage12_PropertiesOfStyle(doc, data, out);

	// The 2026-09-09 question: can the WHOLE document come out as XML without touching the disk?
	//
	// ORDER IS BY RISK, LEAST FIRST, and it is not the order I used the first time. Stage 16 -
	// the one I called "the safe control" - is the one that killed InDesign, so it now runs
	// AFTER everything whose answer I want to keep. A stage that dies takes the rest with it.
	Line(out, "");
	Stage13_ListExportFormats(doc, out);
	Line(out, "");
	ProbeBytes docBytes;
	PMString usedFormat;
	usedFormat.SetTranslatable(kFalse);
	Stage14_ExportDocumentToStream(doc, docBytes, out, usedFormat);
	Line(out, "");
	Stage17_SaveBackWithSelection(doc, docBytes, out);
	Line(out, "");
	Stage15_OpenAsUcfPackage(doc, docBytes, out);
	Line(out, "");
	// STAGE 16 IS DELIBERATELY NOT CALLED.
	//
	// It wedged InDesign TWICE on 2026-09-09 - responding=False, still inside ExportToStream,
	// CPU spinning - once with a stream I had not opened, and once with a stream measured good
	// (actual state 0) right before the call. So the hang is NOT about Open(), and the stage has
	// already told us everything it can. Calling it again only buys another forced kill, and it
	// takes stage 19 down with it, which is the whole reason stage 19 has never once run.
	//
	// Kept compiled, not deleted: the finding is "this call hangs", and the code that establishes
	// it should stay readable next to the finding.
	// Stage16_ControlXmlProviderToStream(doc, out);
	Line(out, "");
	Stage19_ExportInxWithPolicy(doc, out);
	Line(out, "");
	Stage23_FullDump(doc, out);
	Line(out, "");
	// STAGE 21 IS NOT CALLED ANY MORE. It has done its job: 11 of the 14 policies are measured
	// and recorded in work/kcm-policy-done.txt, and the three that remain unmeasured all CRASH
	// (kSaveBack..., and the three Assignment ones - every policy that carries its own data and
	// was handed none). Leaving it in the run meant InDesign died on every single probe, which
	// made the change-detection experiment impossible to run twice in a row.
	// To finish the sweep later: set IUIDData on the Assignment policies first, then re-enable.
	// Stage21_PolicySweep(doc, out);

	Log("=== probe done ===");
}

// End, KCMInxProbe.cpp.
