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

// General includes:
#include "DocFrameworkID.h"	// kDocumentObjectScriptElement - the document as a scripting object
#include "INXCoreID.h"
#include "TextID.h"			// IID_IPARASTYLEGROUPMANAGER / IID_ISTYLEGROUPHIERARCHY		// kINXTraditionalImportScriptManagerBoss - the INX script context
#include "PersistUtils.h"
#include "StreamUtil.h"
#include "UIDList.h"
#include "Utils.h"

#include <windows.h>		// ::GetTickCount - each route reports how long it took
#include <stdio.h>			// the log survives a crash; the report string does not
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

void Line(PMString& out, const char* text)
{
	out.Append(text);
	out.Append("\n");
}

void LineNum(PMString& out, const char* label, int32 value)
{
	out.Append(label);
	out.AppendNumber(value);
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

	Log("=== probe done ===");
}

// End, KCMInxProbe.cpp.
