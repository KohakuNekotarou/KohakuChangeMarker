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

#include <windows.h>				// GetTempPathW - the crash trace; CreateNamedPipeW - S15
#include <cstring>					// memset - the event structure before each GetNextPDFExportEvent
#include <fstream>
#include <string>
#include <vector>

#include "IBoolData.h"
#include "ICommand.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDOMElement.h"			// the root a snippet is imported under
#include "IExportManager.h"			// S17 - ExportIDMLDirect, the only public route to a whole IDML
#include "IGeometry.h"
#include "IHierarchy.h"
#include "IImportProvider.h"
#include "IImportProviderUtils.h"
#include "IINXManager.h"			// S17.8 - ExportINX on a root smaller than the document
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "IMasterSpreadUtils.h"		// AppendMasterPageItems - the page's furniture, which GetItemsOnPage leaves out
#include "IMemoryStreamData.h"		// S16 - how a memory stream is told where to put its bytes
#include "IOutputPages.h"			// S15 - the page exporter's list of pages
#include "IPDFExportController.h"	// ★StartUp(bExportPageItems) - "is the item list PAGES or page items?"
#include "IPDFExportPrefs.h"
#include "IPDFPostProcessPrefs.h"	// S15 - no viewer after the export
#include "IPDFSecurityPrefs.h"
#include "ISysFileData.h"			// S15 - the ONLY door kPDFExportCmdBoss has for "where to write"
#include "IUCFPackageUtils.h"		// S16 - the IDML container, which HAS a stream door
#include "IPMStream.h"
#include "IPMUnknownData.h"
#include "ISession.h"
#include "ISnippetExport.h"
#include "ISnippetImport.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IStoryList.h"			// S17.8 - the other part a real IDML keeps in a file of its own
#include "ISwatchList.h"			// what the report's swatch list looks like after both sides arrive
#include "ISwatchUtils.h"
#include "IUIFlagData.h"
#include "CmdUtils.h"
#include "DocumentID.h"				// IID_ISYSFILEDATA
#include "ErrorUtils.h"
#include "FileUtils.h"				// S15 - SysFileToPMString, to read back what IDFile made of a pipe path
#include "AppFrameworkID.h"		// S17.8 - kActionExportPolicyBoss, the policy measured to work
#include "ImportExportUIID.h"		// S17 - kExportMgrBoss / kExportMgrServiceID
#include "INXCoreID.h"			// S17.8 - IID_IINXEXPORTPOLICY
#include "OpenPlaceID.h"			// kImportProviderService - the service every import filter registers under
#include "PersistUtils.h"			// ::CreateObject - S16 builds its own unopened stream
#include "ShuksanID.h"				// kMemStreamWriteBoss
#include "PDFID.h"					// kPDFExportItemsCmdBoss / IID_IPDFCLIPBOARDEXPORTPREFS / IID_IUSEPROGRESSINDICATOR
#include "PMFlavorTypes.h"			// kPDFExternalFlavor / kPageItemFlavor
#include "PreferenceUtils.h"		// ::QuerySessionPreferences
#include "SDKLayoutHelper.h"
#include "StreamUtil.h"
#include "TextChar.h"				// kTextChar_CR
#include "TransformUtils.h"			// InnerToSpreadMatrix
#include "UIDList.h"
#include "Utils.h"
#include "WideString.h"				// S15 - IDFile(const WideString&)

#include "KCMPdfSpike.h"
#include "KCMCore.h"				// KCMActiveDocDB / KCMCollectPageUIDs
#include "KCMDrawEventHandler.h"	// sPrintMarks - what carries the marks into output
#include "KCMMemXferBytes.h"		// where the PDF lands instead of a file
#include "KCMRingAdornment.h"		// KCMBeginExportOn / KCMEndExportOnThisThread - announcing the export
#include "KCMRehydrate.h"			// KCMMarkRehydratedClean / KCMCloseRehydrated - the throwaway document
#include "KCMResourceBytes.h"		// S16 - where the whole document's XML already lands
#include "KCMResourceSnapshot.h"	// S16 - KCMTakeResourceSnapshot: the document as one XML, no file

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

/** ★DIAGNOSTIC ONLY, and it goes with the spike: drop the bytes into %TEMP% so the picture can
    be LOOKED at (KIDMCP's capture takes pdf:PATH). The whole point of the work is that the
    REPORT stops writing files; a measurement that has to be seen is a different thing, and a
    reading that is never looked at is how "the marks cannot travel" survived three runs. */
void DropForTheEye(KCMMemXferBytes& bytes, const wchar_t* name)
{
	if (bytes.GetSize() == 0)
		return;
	wchar_t dir[MAX_PATH] = { 0 };
	::GetTempPathW(MAX_PATH, dir);
	std::wstring path(dir);
	path += name;
	std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
	if (file)
		file.write(bytes.GetData(), static_cast<std::streamsize>(bytes.GetSize()));
}

//========================================================================================
//  ★★★S15's APPARATUS - a named pipe wearing the costume of a file.
//
//  THE QUESTION (the user, 2026-09-14): "C++ kPDFExportCmdBoss / a page / accurate / a FILE -
//  can that really only ever go to a file?" Measured so far: its 35 interfaces carry
//  IID_ISYSFILEDATA and nothing that takes a stream, and IPDFExportController::StartUp - the
//  one other door - takes InDesign down when called from outside (S12).
//
//  BUT ISysFileData TAKES A **PATH**, AND NOT EVERY PATH IS A FILE. On Windows,
//  \\.\pipe\<name> is a path that any file API will open, and what is written to it goes
//  straight into another piece of code's memory. No file system, no bytes on disk, nothing to
//  delete. If the exporter simply opens what it is handed and writes forward, the page export
//  lands in memory after all.
//
//  ⚠**AND THE REASON TO EXPECT IT NOT TO WORK**: a PDF writer SEEKS. The cross-reference table
//    is written last, and the offset at the head of the file is filled in afterwards. A pipe
//    cannot seek. So the honest prediction is "it opens, some bytes arrive, and then it fails" -
//    and even that is worth knowing, because it separates "the door is locked" from "the door
//    opens onto a room the exporter cannot use".
//========================================================================================

/** What one pipe caught. Heap-allocated and handed to the reader thread, so that a thread
    which does not come back cannot write into a dead stack frame (it is leaked instead). */
struct KCMPipeState
{
	HANDLE			pipe;
	volatile LONG	ready;			// the reader is at the door (see the race below)
	volatile LONG	connected;		// did anybody ever open the other end
	volatile LONG	bytes;			// how much arrived
	volatile LONG	headLen;
	char			head[16];		// the first bytes, so "%PDF-" can be recognised
	DWORD			connectErr;		// what ConnectNamedPipe said
	DWORD			lastErr;		// why the reading stopped
	// ★KEEPING THE CONTENT, not just counting it. "36,509 bytes arrived and they start PK" is
	//   not proof that a usable zip arrived - only a zip tool outside InDesign can say that, and
	//   it cannot say anything about bytes that were thrown away as they went past.
	char*			keep;			// nil when the allocation failed: then only the count is real
	LONG			keepCap;
	volatile LONG	keepLen;
};

/** Accept one connection and read until the writer closes or fails.

    ⚠★★★**THE RACE THAT MADE THE FIRST RUN LIE** (measured 2026-09-14): CreateThread returns
      before the new thread runs, so the writer can open, write and close before this thread
      reaches ConnectNamedPipe. Windows then answers **ERROR_NO_DATA (232)**, not
      ERROR_PIPE_CONNECTED - and the first version treated that as a failure and returned
      WITHOUT READING THE BUFFER. The control stage wrote 14 bytes and the instrument reported
      0, which is exactly the shape of an unarmed check: it does not fail, it reports success
      for the wrong reason. Both halves are fixed here - the flag below, and NO_DATA read as
      "the writer has already been and gone; drain what is left". */
DWORD WINAPI KCMPipeReaderProc(LPVOID param)
{
	KCMPipeState* const s = static_cast<KCMPipeState*>(param);
	::InterlockedExchange(&s->ready, 1);
	const BOOL ok = ::ConnectNamedPipe(s->pipe, nil);
	s->connectErr = ok ? 0 : ::GetLastError();
	if (!ok && s->connectErr != ERROR_PIPE_CONNECTED && s->connectErr != ERROR_NO_DATA)
	{
		s->lastErr = s->connectErr;
		return 0;
	}
	::InterlockedExchange(&s->connected, 1);
	char buf[8192];					// on this thread's own stack, so no allocation can throw
	for (;;)
	{
		DWORD got = 0;
		if (!::ReadFile(s->pipe, buf, static_cast<DWORD>(sizeof(buf)), &got, nil) || got == 0)
		{
			s->lastErr = ::GetLastError();
			break;
		}
		if (::InterlockedCompareExchange(&s->headLen, 0, 0) == 0)
		{
			const DWORD n = got < sizeof(s->head) ? got : static_cast<DWORD>(sizeof(s->head));
			std::memcpy(s->head, buf, n);
			::InterlockedExchange(&s->headLen, static_cast<LONG>(n));
		}
		if (s->keep != nil && s->keepLen + static_cast<LONG>(got) <= s->keepCap)
		{
			std::memcpy(s->keep + s->keepLen, buf, got);
			::InterlockedExchangeAdd(&s->keepLen, static_cast<LONG>(got));
		}
		::InterlockedExchangeAdd(&s->bytes, static_cast<LONG>(got));
	}
	return 0;
}

/** A named pipe standing open with a thread reading it, for the length of one measurement.
    ⚠Spike apparatus: it leaks its state and its handles rather than free them under a thread
      that did not return, because a crash inside this experiment must not be caused BY this
      experiment. */
class KCMPipeCatcher
{
public:
	/** `duplex` opens the pipe for reading AND writing. ★A writer that asks for
	    GENERIC_READ|GENERIC_WRITE - which is what a PDF writer asks for, because it patches the
	    cross-reference offset back into the head when it is done - CANNOT open an inbound-only
	    pipe at all, so the two are different questions and both get asked. */
	KCMPipeCatcher(const wchar_t* leafName, bool16 duplex, bool16 keepContent = kFalse, DWORD maxInstances = 1)
		: fPath(L"\\\\.\\pipe\\"), fState(nil), fThread(nil), fCreateErr(0), fDuplex(duplex), fEverListened(kFalse)
	{
		fPath += leafName;
		fState = new (std::nothrow) KCMPipeState;
		if (fState == nil)
			return;
		std::memset(fState, 0, sizeof(KCMPipeState));
		if (keepContent)
		{
			// 16MB is far beyond anything this spike sends; nothrow because a failed allocation
			// must leave the COUNT still honest rather than take InDesign down.
			fState->keepCap = 16 * 1024 * 1024;
			fState->keep = new (std::nothrow) char[fState->keepCap];
			if (fState->keep == nil)
				fState->keepCap = 0;
		}
		fState->pipe = ::CreateNamedPipeW(fPath.c_str(),
		                                  duplex ? PIPE_ACCESS_DUPLEX : PIPE_ACCESS_INBOUND,
		                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		                                  maxInstances,	// ★S17.6 - 9491 says "already open", so leave room for the check AND the write
		                                  64 * 1024,
		                                  64 * 1024,
		                                  0, nil);
		if (fState->pipe == INVALID_HANDLE_VALUE)
		{
			fCreateErr = ::GetLastError();
			fState->pipe = nil;
			return;
		}
		DWORD id = 0;
		fThread = ::CreateThread(nil, 0, KCMPipeReaderProc, fState, 0, &id);
		fEverListened = (fThread != nil) ? kTrue : kFalse;
		// ★Do not hand the path out until the reader is at the door: see the race in the proc.
		for (int32 spin = 0; spin < 1000 && fState->ready == 0; ++spin)
			::Sleep(1);
	}

	~KCMPipeCatcher()
	{
		Finish();
		// Only safe once Finish() has joined the reader - which it has, unless it timed out, and
		// in that case fThread is nil and fState is deliberately abandoned with its buffer.
		if (fThread == nil && fState != nil && fState->keep != nil)
		{
			delete[] fState->keep;
			fState->keep = nil;
			fState->keepCap = 0;
		}
	}

	bool16 Listening() const { return fThread != nil; }
	DWORD CreateError() const { return fCreateErr; }
	const wchar_t* Path() const { return fPath.c_str(); }

	int32 Bytes() const { return fState != nil ? static_cast<int32>(fState->bytes) : 0; }
	bool16 EverConnected() const { return fState != nil && fState->connected != 0 ? kTrue : kFalse; }
	DWORD LastReadError() const { return fState != nil ? fState->lastErr : 0; }

	/** Everything this pipe knows, in one phrase - ★so that every stage reports the SAME facts
	    and none of them can quietly leave out the one that would have shown the flaw. */
	void Describe(PMString& into) const
	{
		into.Append(fDuplex ? "[duplex] " : "[inbound] ");
		// ⚠NOT Listening(): that asks whether the thread is STILL there, and Finish() has just
		//   joined it. Asking the wrong question here reported "never listening" for a pipe that
		//   had worked perfectly - the second unarmed instrument in one afternoon.
		if (fState == nil || !fEverListened)
		{
			into.Append("the pipe was never listening, error ");
			into.AppendNumber(static_cast<int32>(fCreateErr));
			return;
		}
		into.Append(EverConnected() ? "OPENED by the other side, " : "★NEVER OPENED, ");
		into.AppendNumber(Bytes());
		into.Append(" bytes");
		if (Bytes() > 0)
		{
			into.Append(" \"");
			Head(into);
			into.Append("\"");
		}
		into.Append(" (connect said ");
		into.AppendNumber(static_cast<int32>(fState->connectErr));
		into.Append(", reading stopped on ");
		into.AppendNumber(static_cast<int32>(fState->lastErr));
		into.Append(")");
	}

	/** ★Drop everything that arrived into %TEMP%, so something that is not this code can judge
	    it. Answers how many bytes were written, or -1 when the content was not kept. */
	int32 SaveTo(const wchar_t* leafName) const
	{
		if (fState == nil || fState->keep == nil || fState->keepLen == 0)
			return -1;
		wchar_t dir[MAX_PATH] = { 0 };
		::GetTempPathW(MAX_PATH, dir);
		std::wstring path(dir);
		path += leafName;
		std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
		if (!file)
			return -1;
		file.write(fState->keep, static_cast<std::streamsize>(fState->keepLen));
		return static_cast<int32>(fState->keepLen);
	}

	/** The first bytes as printable ASCII, so "%PDF-1.7" can be read straight out of the answer. */
	void Head(PMString& into) const
	{
		if (fState == nil)
			return;
		const LONG n = fState->headLen;
		char printable[sizeof(fState->head) + 1] = { 0 };
		for (LONG i = 0; i < n; ++i)
		{
			const char c = fState->head[i];
			printable[i] = (c >= 0x20 && c < 0x7f) ? c : '.';
		}
		into.Append(Ascii(printable));
	}

	/** Stop the reader and join it. ★The knock is what makes this safe: a thread parked in
	    ConnectNamedPipe waits forever, so if nobody ever came, we open our own door once. */
	void Finish()
	{
		if (fThread == nil)
		{
			if (fState != nil && fState->pipe != nil)
			{
				::CloseHandle(fState->pipe);
				fState->pipe = nil;
			}
			return;
		}
		if (::InterlockedCompareExchange(&fState->connected, 0, 0) == 0)
		{
			HANDLE knock = ::CreateFileW(fPath.c_str(), GENERIC_WRITE, 0, nil, OPEN_EXISTING, 0, nil);
			if (knock != INVALID_HANDLE_VALUE)
				::CloseHandle(knock);
		}
		const DWORD waited = ::WaitForSingleObject(fThread, 5000);
		if (waited != WAIT_OBJECT_0)
		{
			fThread = nil;			// ⚠leave it, and leave fState with it: see the class comment
			return;
		}
		::CloseHandle(fThread);
		fThread = nil;
		::CloseHandle(fState->pipe);
		fState->pipe = nil;
		// ⚠The keep-buffer is NOT freed here: SaveTo is called after Finish, and it needs it.
		//   It goes with the catcher.
	}

private:
	KCMPipeCatcher(const KCMPipeCatcher&);
	KCMPipeCatcher& operator=(const KCMPipeCatcher&);

	std::wstring	fPath;
	KCMPipeState*	fState;
	HANDLE			fThread;
	DWORD			fCreateErr;
	bool16			fDuplex;
	bool16			fEverListened;
};

/** An IDFile built straight from a path string, with no directory behind it. */
IDFile PipeAsFile(const wchar_t* path)
{
	WideString w(reinterpret_cast<const UTF16TextChar*>(path));
	return IDFile(w);
}

/** ★★★S16 - build an IDML-shaped container ON A STREAM, then OPEN IT AGAIN FROM THE SAME BYTES.
    Written as a ROUND TRIP on purpose: "CreatePackage returned something" is not evidence that a
    package was made, and the only honest proof is that a known payload comes back out at the
    same length. The bytes are also dropped to %TEMP% so that a zip tool OUTSIDE InDesign gets a
    vote - the one check this code cannot fake. */
void IdmlInMemory(IDataBase* db, PMString& line)
{
	Utils<IUCFPackageUtils> ucf;
	if (!ucf)
	{
		line.Append("SKIPPED - IUCFPackageUtils is not available");
		return;
	}

	// The payload is THE WHOLE DOCUMENT AS XML, from the route that already works (ExportINX,
	// running in KCM's Resources mode every day). Using the real thing rather than a token
	// string measures capacity at the same time.
	KCMResourceBytes docXml;
	PMString whyNot;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	const bool16 gotXml = (doc != nil) && KCMTakeResourceSnapshot(doc, docXml, whyNot);
	const char* const kFallback = "<?xml version=\"1.0\"?><Document/>";
	const char* const payload = gotXml ? docXml.Bytes() : kFallback;
	const int32 payloadLen = gotXml ? static_cast<int32>(docXml.Size())
	                                : static_cast<int32>(std::strlen(kFallback));
	line.Append("payload ");
	line.AppendNumber(payloadLen);
	line.Append(gotXml ? " bytes of document XML; " : " bytes (ExportINX gave nothing - a token payload is used); ");

	// ★★★THE MATRIX, and the reason it is one. The first run asked ONE question - "does
	//   CreatePackage(stream) work" - got nil, and could not tell "the stream door is shut" from
	//   "you are calling it wrong". Two things are varied, one of them being THE TARGET ITSELF,
	//   so that the FILE overload acts as the control: if the file fails too, the fault is mine.
	//   ⚠The mimetype is NOT a guess: read out of the SDK's own
	//     devtools/idmltools/samples/conditionaltext/ConditionalText.idml, whose first entry is
	//     "mimetype", 43 bytes, STORED rather than deflated.
	const AString kIdmlMime("application/vnd.adobe.indesign-idml-package");
	wchar_t tempDir[MAX_PATH] = { 0 };
	::GetTempPathW(MAX_PATH, tempDir);

	// Two buffers, because the two stream attempts must not write into each other
	// (KCMMemXferBytes has no Reset), and `winner` points at whichever one came out with bytes.
	// ⚠ONE BUFFER PER ROW. KCMMemXferBytes has no Reset, so a shared one would let a later row
	//   append to an earlier row's bytes and every size after the first would be a lie.
	KCMMemXferBytes zipPerRow[8];
	KCMMemXferBytes* winner = nil;
	// ★Round two varies WHAT KIND OF STREAM as well as where it writes. The first matrix proved
	//   the call is right (the file overload packed 217KB into 36KB) and the STREAM overload
	//   returned nil - but "the overload is dead" and "OUR stream is not acceptable" are two
	//   different findings, and only a FILE-BACKED stream can tell them apart.
	// ⚠plain ASCII in these labels: a non-ASCII char in a narrow literal came out as "Z".
	// ★★★WHAT THE LAZY ROW TAUGHT. Only CreateFileStreamWriteLazy worked, and the one thing a
	//   lazy stream has that the others do not is that IT IS NOT OPEN YET. IPMStream.h:359-361:
	//   "Open() ... must be called before any Xfers are called. IT GETS CALLED FOR YOU BY THE
	//   StreamUtils FUNCTIONS." ⇒ CreatePackage evidently calls Open() itself, and an
	//   already-open stream refuses. If that is right, CLOSING the stream first should let any
	//   of them through - and the FILE row is the control that says whether it is right.
	// ⚠"Closed first" did NOT work, on a file or in memory - so the rule is not "not open", it
	//   is NEVER OPENED. StreamUtil has no lazy MEMORY stream, so the last two rows build one:
	//   ★kMemStreamWriteBoss carries exactly IID_IPMSTREAM + IID_IMEMORYSTREAMDATA, which is all
	//     a memory stream is - make it, Set() the bytes, and simply never call Open().
	//   ★kUCFWriteStreamBoss is UCF'S OWN stream boss (same two IIDs plus IID_IUCFSTREAMPROPERTY)
	//     - if anything is what CreatePackage expects to be handed, it is this.
	// ★★★WHAT TEN ROWS NARROWED IT TO. Only CreateFileStreamWriteLazy ever worked. Not an
	//   unopened memory stream (kMemStreamWriteBoss with the bytes Set and Open never called),
	//   not UCF's own kUCFWriteStreamBoss the same way, not an eager file stream Closed first.
	//   The one thing the winner has and none of the others do is A PATH IT HAS NOT OPENED YET.
	// ⇒ ★★★AND THAT IS WHERE TODAY'S TWO INVESTIGATIONS MEET. S15 measured that InDesign's own
	//   file layer writes to \\.\pipe\ perfectly well. So: a LAZY FILE STREAM AIMED AT A PIPE
	//   satisfies UCF's demand for a path, and delivers the bytes into memory anyway.
	//   ⚠A zip patches its own headers, and a pipe cannot really seek (S15: SetFilePointerEx
	//     *reports* success on a pipe and moves nothing) - so this may produce a broken zip
	//     rather than none. That is still worth knowing, and the byte count will say which.
	enum StreamKind { kNotAStream, kMemoryPlain, kMemoryRecycled, kFileBacked, kFileBackedRW,
	                  kFileLazy, kMemoryClosed, kFileClosed, kMemoryVirgin, kUcfVirgin, kPipeLazy };
	struct Aim { StreamKind kind; bool16 manifest; const char* what; };
	const Aim aims[] = {
		{ kNotAStream,     kTrue,  "file+manifest"          },	// the control: the call is right
		{ kFileLazy,       kTrue,  "FILE stream lazy"       },	// the only kind that ever worked
		{ kMemoryVirgin,   kTrue,  "MEM boss, never opened" },	// ruled the memory stream out
		{ kPipeLazy,       kTrue,  "LAZY STREAM ON A PIPE"  },	// ★the two threads, joined
	};
	const int32 kAims = static_cast<int32>(sizeof(aims) / sizeof(aims[0]));

	for (int32 attempt = 0; attempt < kAims; ++attempt)
	{
		const StreamKind kind = aims[attempt].kind;
		const bool16 toStream = (kind != kNotAStream);
		const bool16 manifest = aims[attempt].manifest;
		KCMMemXferBytes& zipBytes = zipPerRow[attempt % 8];
		line.Append("[");
		line.Append(Ascii(aims[attempt].what));
		line.Append(" ");

		const bool16 onDisk = (kind == kNotAStream || kind == kFileBacked
		                       || kind == kFileBackedRW || kind == kFileLazy || kind == kFileClosed);
		std::wstring filePath(tempDir);
		filePath += (attempt == 0) ? L"kcm-spike-ucf-a.idml"
		          : (kind == kNotAStream) ? L"kcm-spike-ucf-b.idml"
		          : L"kcm-spike-ucf-c.idml";
		if (onDisk)
			::DeleteFileW(filePath.c_str());

		InterfacePtr<IPMStream> zipOut;
		if (kind == kMemoryPlain)
			zipOut.reset(StreamUtil::CreateMemoryStreamWrite(&zipBytes, kFalse, kFalse));
		else if (kind == kMemoryRecycled)
			zipOut.reset(StreamUtil::CreateMemoryStreamWrite(&zipBytes, kFalse, kTrue));
		else if (kind == kFileBacked)
			zipOut.reset(StreamUtil::CreateFileStreamWrite(PipeAsFile(filePath.c_str()),
			                                               kOpenOut | kOpenTrunc));
		else if (kind == kFileBackedRW)
			zipOut.reset(StreamUtil::CreateFileStreamWrite(PipeAsFile(filePath.c_str()),
			                                               kOpenIn | kOpenOut | kOpenTrunc));
		else if (kind == kFileLazy)
			zipOut.reset(StreamUtil::CreateFileStreamWriteLazy(PipeAsFile(filePath.c_str()),
			                                                   kOpenOut | kOpenTrunc));
		else if (kind == kFileClosed)
			zipOut.reset(StreamUtil::CreateFileStreamWrite(PipeAsFile(filePath.c_str()),
			                                               kOpenOut | kOpenTrunc));
		else if (kind == kMemoryClosed)
			zipOut.reset(StreamUtil::CreateMemoryStreamWrite(&zipBytes, kFalse, kFalse));
		else if (kind == kMemoryVirgin || kind == kUcfVirgin)
		{
			// ★A memory stream built by hand and LEFT UNOPENED - the thing StreamUtil has no
			//   factory for. StreamUtil's own memory factories Open() it for you (IPMStream.h:361),
			//   and that is the only difference between them and this.
			const ClassID boss = (kind == kUcfVirgin) ? kUCFWriteStreamBoss : kMemStreamWriteBoss;
			zipOut.reset((IPMStream*)::CreateObject(boss, IID_IPMSTREAM));
			InterfacePtr<IMemoryStreamData> data(zipOut, IID_IMEMORYSTREAMDATA);
			if (data == nil)
			{
				line.Append("no IID_IMEMORYSTREAMDATA on that boss] ");
				continue;
			}
			data->Set(&zipBytes, kFalse);
		}
		// ★The pipe row needs its catcher standing before the stream names the path.
		// ⚠constructed every round because C++ wants it in scope, but it only ALLOCATES its
		//   keep-buffer for the row that needs it.
		KCMPipeCatcher catcher(L"kcm-idml-pipe", kTrue, (kind == kPipeLazy) ? kTrue : kFalse);
		if (kind == kPipeLazy)
		{
			if (!catcher.Listening())
			{
				line.Append("the pipe could not be created] ");
				continue;
			}
			zipOut.reset(StreamUtil::CreateFileStreamWriteLazy(PipeAsFile(catcher.Path()),
			                                                   kOpenOut | kOpenTrunc));
		}
		if (toStream && zipOut == nil)
		{
			line.Append("the stream itself could not be made] ");
			continue;
		}
		// ★The whole point of these two rows: hand it a stream that is NOT open.
		if (kind == kFileClosed || kind == kMemoryClosed)
			zipOut->Close();

		IUCFPackageUtils::UCFErrorCode err = IUCFPackageUtils::kSuccess;
		Trace("S16.1 CreatePackage");
		IUCFPackageUtils::PackageRefPtr ref = toStream
			? ucf->CreatePackage(zipOut, kIdmlMime, manifest, err)
			: ucf->CreatePackage(PipeAsFile(filePath.c_str()), kIdmlMime, manifest, err);
		Trace("S16.2 CreatePackage came back");
		if (ref == nil)
		{
			line.Append("CreatePackage nil, err ");
			line.AppendNumber(static_cast<int32>(err));
			line.Append("] ");
			continue;
		}

		Trace("S16.3 CreateStream for designmap.xml");
		InterfacePtr<IPMStream> entry(ucf->CreateStream(ref, WideString("designmap.xml"),
		                                               IUCFPackageUtils::kStandard));
		Trace("S16.4 CreateStream came back");
		if (entry == nil)
		{
			line.Append("CreateStream nil] ");
			ucf->ClosePackageWithoutSave(ref);
			continue;
		}
		entry->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(payload)), payloadLen);
		entry->Close();
		entry.reset(nil);

		Trace("S16.5 ClosePackage");
		const IUCFPackageUtils::UCFErrorCode closed = ucf->ClosePackage(ref);
		Trace("S16.6 ClosePackage came back");
		line.Append("Close -> ");
		line.AppendNumber(static_cast<int32>(closed));
		zipOut.reset(nil);		// ★flush before anything reads the bytes back

		if (kind == kPipeLazy)
		{
			catcher.Finish();
			line.Append(", pipe ");
			catcher.Describe(line);
			// ★THE VOTE THIS CODE CANNOT CAST FOR ITSELF: a zip tool outside InDesign either
			//   opens what came through the pipe or it does not. A zip patches its own headers
			//   after the fact and a pipe cannot really seek, so "36,509 bytes starting PK" is
			//   a promising shape, not a working package.
			const int32 saved = catcher.SaveTo(L"kcm-spike-memory.idml");
			line.Append(", saved ");
			line.AppendNumber(saved);
			line.Append(" B to %TEMP%\\kcm-spike-memory.idml (UNZIP IT)] ");
		}
		else if (kind == kFileBacked || kind == kFileBackedRW || kind == kFileLazy || kind == kFileClosed)
		{
			// ★The answer lands on DISK, not in zipBytes - the point of these rows is only
			//   "does CreatePackage(IPMStream*) work when the stream is a file".
			WIN32_FILE_ATTRIBUTE_DATA fad;
			std::memset(&fad, 0, sizeof(fad));
			line.Append(", ");
			if (::GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad))
				line.AppendNumber(static_cast<int32>(fad.nFileSizeLow));
			else
				line.Append("no");
			line.Append(" B through the stream] ");
			::DeleteFileW(filePath.c_str());
		}
		else if (toStream)
		{
			line.Append(", ");
			line.AppendNumber(static_cast<int32>(zipBytes.GetSize()));
			line.Append(" B] ");
			if (zipBytes.GetSize() > 2)
				winner = &zipBytes;
		}
		else
		{
			WIN32_FILE_ATTRIBUTE_DATA fad;
			std::memset(&fad, 0, sizeof(fad));
			line.Append(", ");
			if (::GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad))
				line.AppendNumber(static_cast<int32>(fad.nFileSizeLow));
			else
				line.Append("no");
			line.Append(" B on disk] ");
			// ★attempt 0's file is KEPT so a zip tool outside InDesign gets a vote on whether
			//   what the file overload produces is a real IDML: %TEMP%\kcm-spike-ucf-a.idml.
			if (attempt != 0)
				::DeleteFileW(filePath.c_str());
		}
	}

	if (winner == nil)
		return;
	KCMMemXferBytes& zipBytes = *winner;

	line.Append("the container is ");
	line.AppendNumber(static_cast<int32>(zipBytes.GetSize()));
	line.Append(" bytes");

	// "PK" is how every zip begins. Anything else means whatever came out is not one.
	const char* const z = zipBytes.GetData();
	char two[3] = { (z[0] >= 0x20 && z[0] < 0x7f) ? z[0] : '.',
	                (z[1] >= 0x20 && z[1] < 0x7f) ? z[1] : '.', 0 };
	line.Append(", starts \"");
	line.Append(Ascii(two));
	line.Append("\"");
	line.Append((z[0] == 'P' && z[1] == 'K') ? " ★IT IS A ZIP" : " ★NOT A ZIP");

	// ---- the return leg: open those very bytes and read the entry back ---------------------
	Trace("S16.7 OpenPackage from the same bytes");
	InterfacePtr<IPMStream> zipIn(StreamUtil::CreateMemoryStreamRead(&zipBytes, kFalse));
	IUCFPackageUtils::UCFErrorCode rerr = IUCFPackageUtils::kSuccess;
	IUCFPackageUtils::PackageRefPtr back = (zipIn != nil) ? ucf->OpenPackage(zipIn, rerr) : nil;
	Trace("S16.8 OpenPackage came back");
	if (back == nil)
	{
		line.Append("; OpenPackage(stream) returned nil, UCFErrorCode ");
		line.AppendNumber(static_cast<int32>(rerr));
	}
	else
	{
		line.Append("; OpenPackage(stream) OK, designmap.xml reads back ");
		InterfacePtr<IPMStream> readBack(ucf->OpenStream(back, WideString("designmap.xml")));
		if (readBack == nil)
		{
			line.Append("NOT AT ALL (OpenStream nil)");
		}
		else
		{
			int32 got = 0;
			uchar buf[4096];
			for (;;)
			{
				const int32 n = readBack->XferByte(buf, static_cast<int32>(sizeof(buf)));
				if (n <= 0)
					break;
				got += n;
			}
			readBack->Close();
			line.AppendNumber(got);
			line.Append(" of ");
			line.AppendNumber(payloadLen);
			line.Append(got == payloadLen ? " bytes ★ROUND TRIP COMPLETE" : " bytes ★MISMATCH");
		}
		ucf->ClosePackage(back);
	}
	// ★Dropped so that something which is not this code gets a vote: a zip tool outside
	//   InDesign either opens it or it does not.
	DropForTheEye(zipBytes, L"kcm-spike-memory.idml");
	line.Append(" -> %TEMP%\\kcm-spike-memory.idml (UNZIP IT)");
}

//========================================================================================
//  ★★★S17 - A REAL IDML, AND WHETHER ONE CAN EXIST WITHOUT A FILE.
//
//  THE QUESTION (the user, 2026-09-14 night): "what I am interested in is whether a REAL IDML
//  can be held internally, without ever being written to a file."
//
//  ⚠WHAT S16 DID **NOT** ANSWER, and the measurement that says so. S16 put a UCF container on a
//    stream and got 36KB of zip into memory - but what went INTO it was ONE entry named
//    "designmap.xml", holding the INX that KCM's Resources mode exports every day. Held side by
//    side with a real IDML (work/archive/2026-08/kescm-b11/unpacked/, read 2026-09-14):
//        INX  : <?aid style="50" type="action"   ...?>  <Document DOMVersion="21.0" Self="d" ...>
//        IDML : <?aid style="50" type="document" ...?>  <Document xmlns:idPkg="..." StoryList="ufe u115 ud0" ...>
//                 + ten <idPkg:Spread src="Spreads/Spread_uee.xml"/> style references, with the
//                   document's actual content living in those ten separate files.
//    The same vocabulary; a different PI, and SPLIT. So S16 built an IDML-SHAPED CONTAINER around
//    something that is not IDML. This step asks about the real thing.
//
//  ★THE MOVE THAT WAS NEVER PLAYED. IExportManager::ExportIDMLDirect (IExportManager.h:84) is the
//    only public route to a whole IDML, and it takes `const IDFile*` - exactly the shape S15 has
//    already defeated once: an IDFile holds \\.\pipe\<name>, and InDesign's own file layer writes
//    to it (S15.a, measured, 25 bytes through). The pipe has been aimed at the PDF exporter and at
//    UCF. It has never been aimed at THE IDML EXPORTER. That is S17.4.
//
//  ⚠TWO HONEST PREDICTIONS, both worth the measurement whichever way they fall:
//    1. A zip patches its own headers and a pipe cannot seek, so what arrives may be a BROKEN zip
//       rather than none - which is exactly what S16 saw, and every byte was there to repair.
//    2. IUCFPackageUtils.h:113 says mode 3 (rewriting) "uses a temp file". If the IDML writer
//       opens that way, a file appears on disk wherever the destination points - and THAT WOULD
//       ANSWER THE USER'S QUESTION WITH A NO. It is the one outcome this step must not hide.
//========================================================================================

/** Count of non-overlapping `needle` in `len` bytes at `data`. */
int32 CountIn(const char* data, uint32 len, const char* needle)
{
	if (data == nil)
		return 0;
	const uint32 nlen = static_cast<uint32>(std::strlen(needle));
	if (nlen == 0 || len < nlen)
		return 0;
	int32 count = 0;
	for (uint32 i = 0; i + nlen <= len; ++i)
	{
		if (std::memcmp(data + i, needle, nlen) == 0)
		{
			++count;
			i += nlen - 1;
		}
	}
	return count;
}

/** The name of the first real element, skipping the <?xml?> and <?aid?> instructions.
    ★Read rather than guessed at: the first version of S17.8 asked "does it contain
    <Document>?" and stopped at the first yes, which answered "the whole thing" for output that
    was plainly not (19KB against the document's 180KB). */
void FirstElementName(const char* data, uint32 len, PMString& into)
{
	if (data == nil || len == 0)
	{
		into.Append("(nothing)");
		return;
	}
	for (uint32 i = 0; i + 1 < len; ++i)
	{
		if (data[i] != '<')
			continue;
		const char c = data[i + 1];
		if (c == '?' || c == '!' || c == '/')
			continue;
		char name[64] = { 0 };
		uint32 n = 0;
		uint32 j = i + 1;
		while (j < len && n < sizeof(name) - 1 && data[j] != ' ' && data[j] != '>'
		       && data[j] != '/' && data[j] != '\r' && data[j] != '\n' && data[j] != '\t')
		{
			name[n++] = data[j++];
		}
		into.Append(Ascii(name));
		return;
	}
	into.Append("(no element)");
}

/** Read a whole file into memory. Answers the byte count, or -1 when it could not be opened. */
int32 LoadFileInto(const wchar_t* path, KCMMemXferBytes& into)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return -1;
	char buf[8192];
	int32 total = 0;
	while (file)
	{
		file.read(buf, sizeof(buf));
		const std::streamsize got = file.gcount();
		if (got <= 0)
			break;
		into.Write(buf, static_cast<uint32>(got));
		total += static_cast<int32>(got);
	}
	return total;
}

/** The export manager. ★The SDK contains not one caller of IExportManager, so both routes are
    tried and the reading says which one answered. */
IExportManager* QueryExportManager(PMString& line)
{
	IExportManager* made = (IExportManager*)::CreateObject(kExportMgrBoss, IID_IEXPORTMANAGER);
	if (made != nil)
	{
		line.Append("via ::CreateObject(kExportMgrBoss), ");
		return made;
	}
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		line.Append("★NO EXPORT MANAGER - ::CreateObject gave nil and there is no service registry");
		return nil;
	}
	InterfacePtr<IK2ServiceProvider> service(registry->QueryDefaultServiceProvider(kExportMgrServiceID));
	InterfacePtr<IExportManager> found(service, IID_IEXPORTMANAGER);
	if (found == nil)
	{
		line.Append("★NO EXPORT MANAGER - neither ::CreateObject nor kExportMgrServiceID answers");
		return nil;
	}
	line.Append("via kExportMgrServiceID, ");
	found->AddRef();		// handed back; the caller releases it
	return found;
}

/** One ExportIDMLDirect, aimed wherever `target` names.

    ★★★THE INSTRUMENT IS ARMED HERE, and what it is armed against is A BACKGROUND EXPORT: the
    call would return, the file would not be there yet, and a size of 0 reads exactly like a
    refusal. TaskInfo is asked for and waited on when it is real (TaskInfo.h:429 WaitForTask), so
    a slow success cannot be reported as a failure. */
void ExportIdmlTo(const UIDRef& docRef, const IDFile& target, IExportManager* mgr,
                  PMString& line, int32& msTaken)
{
	TaskInfo task;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	const DWORD t0 = ::GetTickCount();
	mgr->ExportIDMLDirect(docRef, &target, kFalse /*bShowUI*/, &task);
	msTaken = static_cast<int32>(::GetTickCount() - t0);
	const ErrorCode global = ErrorUtils::PMGetGlobalErrorCode();
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	line.Append("returned in ");
	line.AppendNumber(msTaken);
	line.Append("ms");
	if (task.IsValid())
	{
		line.Append(", ★IT IS A BACKGROUND TASK (");
		line.Append(task.GetName());
		line.Append(") - waiting for it");
		const TaskStatusInfo::TaskState state = task.WaitForTask();
		msTaken = static_cast<int32>(::GetTickCount() - t0);
		line.Append(", ended in state ");
		line.AppendNumber(static_cast<int32>(state));
		line.Append(" (3=Completed, 5=Cancelled, 7=ForceKilled) after ");
		line.AppendNumber(msTaken);
		line.Append("ms total");
	}
	else
	{
		line.Append(", synchronous (no task)");
	}
	if (global != kSuccess)
	{
		line.Append(", global error ");
		line.AppendNumber(static_cast<int32>(global));
	}
}

/** ★THE TEST FOR "IS THIS A REAL IDML", and it is the same two facts the two real files were
    compared on: the processing instruction's type, and how many <idPkg:* src="..."/> references
    designmap.xml carries. An INX inside a container answers "action" and zero. */
void DescribeIdmlPackage(IUCFPackageUtils::PackageRefPtr ref, PMString& line)
{
	Utils<IUCFPackageUtils> ucf;
	if (!ucf)
	{
		line.Append("no IUCFPackageUtils to describe it with");
		return;
	}
	InterfacePtr<IPMStream> map(ucf->OpenStream(ref, WideString("designmap.xml")));
	if (map == nil)
	{
		line.Append("but there is no designmap.xml in it");
		return;
	}
	KCMMemXferBytes text;
	uchar buf[4096];
	int32 total = 0;
	for (;;)
	{
		const int32 n = map->XferByte(buf, static_cast<int32>(sizeof(buf)));
		if (n <= 0)
			break;
		text.Write(buf, static_cast<uint32>(n));
		total += n;
	}
	map->Close();
	const char* const data = text.GetData();
	const uint32 len = text.GetSize();
	line.Append("designmap.xml ");
	line.AppendNumber(total);
	line.Append(" B, PI type=");
	if (CountIn(data, len, "type=\"document\"") > 0)
		line.Append("\"document\" ★A REAL IDML");
	else if (CountIn(data, len, "type=\"action\"") > 0)
		line.Append("\"action\" ★INX, NOT IDML");
	else
		line.Append("neither");
	line.Append(", idPkg refs ");
	line.AppendNumber(CountIn(data, len, "<idPkg:"));
	line.Append(", META-INF/container.xml ");
	line.Append(ucf->FileExists(ref, WideString("META-INF/container.xml")) ? "yes" : "NO");
	line.Append(", Resources/Styles.xml ");
	line.Append(ucf->FileExists(ref, WideString("Resources/Styles.xml")) ? "yes" : "NO");
}

/** ★★★S17 - see the block above. */
/** ★★★THE GATE ROWS ARE DONE, AND THEY ARE OFF.
    S17.4 / S17.6 / S19 all aim an export at a destination it will refuse, to find out WHY it
    refuses. The answer came in on 2026-09-14: global error 9491 = "Cannot save to the file,
    because it is already open", and B1/B2/E narrowed it to "anything the caller is holding open",
    not "a pipe". Nothing is left to learn from running them.
    ⚠AND THEY COST SOMETHING TO RUN: a background export that fails puts InDesign's own
      "background task warning" dialog in front of whoever is at the machine. That happened to the
      user during the 21:37 run. Turn this back on only to re-measure the gate. */
static const bool16 kProbeTheGate = kFalse;

void RealIdmlWithoutAFile(IDataBase* db, PMString& out)
{
	Utils<IUCFPackageUtils> ucf;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	const UIDRef docRef(db, db->GetRootUID());

	PMString mgrLine(Ascii("S17.1 IExportManager: "));
	InterfacePtr<IExportManager> mgr(QueryExportManager(mgrLine));
	Say(out, mgrLine);
	if (mgr == nil || doc == nil || !ucf)
	{
		Say(out, "S17 SKIPPED - no export manager, no document, or no IUCFPackageUtils");
		return;
	}

	wchar_t tempDir[MAX_PATH] = { 0 };
	::GetTempPathW(MAX_PATH, tempDir);
	std::wstring realPath(tempDir);
	realPath += L"kcm-spike-real.idml";
	::DeleteFileW(realPath.c_str());

	// ---- S17.2 the baseline: a real IDML, on disk, where it is allowed to be ----------------
	// ★This row is the MATERIAL and the STANDARD at once. Without it, "the pipe caught 0 bytes"
	//   could not be told apart from "this document cannot be exported to IDML at all".
	// ⚠The file is LEFT THERE on purpose: a zip tool outside InDesign is the only thing that can
	//   vote on whether what came out is a real package, and this spike does not delete what it
	//   makes - that is the very complaint the report work started from.
	int32 baselineSize = 0;
	Trace("S17.2 ExportIDMLDirect to a real file");
	{
		PMString line(Ascii("S17.2 ExportIDMLDirect to a REAL FILE (the baseline): "));
		int32 ms = -1;
		ExportIdmlTo(docRef, PipeAsFile(realPath.c_str()), mgr, line, ms);
		WIN32_FILE_ATTRIBUTE_DATA fad;
		std::memset(&fad, 0, sizeof(fad));
		line.Append(", ");
		if (::GetFileAttributesExW(realPath.c_str(), GetFileExInfoStandard, &fad))
		{
			baselineSize = static_cast<int32>(fad.nFileSizeLow);
			line.AppendNumber(baselineSize);
			line.Append(" B at %TEMP%\\kcm-spike-real.idml");
		}
		else
		{
			line.Append("★NO FILE WAS WRITTEN");
		}
		Say(out, line);
	}

	// ---- S17.3 the READ door, tried with a HEALTHY zip ---------------------------------------
	// ★★★THE CONTROL THAT HAS TO COME FIRST. If OpenPackage(IPMStream*) refuses a healthy zip
	//   held in memory, then nothing done on the writing side could ever be read back, and every
	//   row below would be measuring the wrong door. The bytes are the baseline's, read off disk -
	//   where they came from is not the question; whether THE READER TAKES A STREAM is.
	//   ⚠The writing side took 11 tries to find its one acceptable stream (S16), so the reading
	//     side is not assumed to be any easier.
	KCMMemXferBytes baselineBytes;
	Trace("S17.3 OpenPackage on a stream");
	if (baselineSize > 0)
	{
		PMString line(Ascii("S17.3 OpenPackage(IPMStream*) on those same bytes in memory: "));
		const int32 loaded = LoadFileInto(realPath.c_str(), baselineBytes);
		line.AppendNumber(loaded);
		line.Append(" B loaded, ");
		InterfacePtr<IPMStream> zipIn(StreamUtil::CreateMemoryStreamRead(&baselineBytes, kFalse, kFalse));
		IUCFPackageUtils::UCFErrorCode err = IUCFPackageUtils::kSuccess;
		IUCFPackageUtils::PackageRefPtr ref = (zipIn != nil) ? ucf->OpenPackage(zipIn, err) : nil;
		if (ref == nil)
		{
			line.Append("★REFUSED, UCFErrorCode ");
			line.AppendNumber(static_cast<int32>(err));
			line.Append(" - the read door does not take this stream");
		}
		else
		{
			line.Append("★OPENED: ");
			DescribeIdmlPackage(ref, line);
			ucf->ClosePackage(ref);
		}
		Say(out, line);
	}
	else
	{
		Say(out, "S17.3 SKIPPED - the baseline produced no bytes to read");
	}

	// ---- S17.4 ★★★THE MOVE THAT WAS NEVER PLAYED ------------------------------------------
	Trace("S17.4 ExportIDMLDirect aimed at a pipe");
	if (kProbeTheGate)
	{
		PMString line(Ascii("S17.4 ★ExportIDMLDirect AIMED AT A PIPE: "));
		// duplex, because a writer that patches its own headers asks for GENERIC_READ|GENERIC_WRITE
		// and cannot open an inbound-only pipe at all (S15's two rows).
		KCMPipeCatcher catcher(L"kcm-idml-export", kTrue /*duplex*/, kTrue /*keep the bytes*/);
		if (!catcher.Listening())
		{
			line.Append("the pipe could not be created, error ");
			line.AppendNumber(static_cast<int32>(catcher.CreateError()));
		}
		else
		{
			int32 ms = -1;
			ExportIdmlTo(docRef, PipeAsFile(catcher.Path()), mgr, line, ms);
			catcher.Finish();
			line.Append(", pipe ");
			catcher.Describe(line);
			const int32 saved = catcher.SaveTo(L"kcm-spike-idml-from-pipe.idml");
			if (saved > 0)
			{
				line.Append(", saved ");
				line.AppendNumber(saved);
				line.Append(" B to %TEMP%\\kcm-spike-idml-from-pipe.idml");
				if (baselineSize > 0 && saved == baselineSize)
				{
					line.Append(" ★THE SAME SIZE AS THE BASELINE");
				}
				else if (baselineSize > 0)
				{
					line.Append(" (the baseline was ");
					line.AppendNumber(baselineSize);
					line.Append(" B)");
				}
			}
		}
		Say(out, line);
	}

	// ---- S17.5 the control S15 established ---------------------------------------------------
	// ★\\.\NUL is the same SHAPE of path as \\.\pipe\ but a device that swallows everything.
	//   S15 found the PDF page exporter refuses a pipe and yet writes to NUL happily - which is
	//   what separated "the path is wrong" from "the writer needs something a pipe cannot do".
	//   The same pair of rows puts the same question to the IDML writer.
	Trace("S17.5 the same export to NUL");
	if (kProbeTheGate)
	{
		PMString line(Ascii("S17.5 the same export to \\\\.\\NUL (the control): "));
		int32 ms = -1;
		ExportIdmlTo(docRef, PipeAsFile(L"\\\\.\\NUL"), mgr, line, ms);
		Say(out, line);
	}

	// ---- S17.6 ★★★THE DOOR HAS A NAME NOW, AND IT IS NOT "PIPE" ---------------------------
	// S17.4 came back with global error 9491, and the error table says what that is:
	//     0x2513 (9491) "Cannot save to the file ^2, because it is ALREADY OPEN."
	// ⇒ The exporter is not refusing a pipe. It is refusing a destination SOMEBODY HAS OPEN - and
	//   the somebody is us: KCMPipeCatcher holds the server end of it. That also settles what S15
	//   had to leave open (LockFile or GetFileType): IT IS THE LOCK CHECK. The same gate turned the
	//   PDF exporter back in 41ms and this one in 0ms, before either had drawn a thing.
	// ★SO THE CHECK IS GIVEN ROOM TO PASS. A pipe made with nMaxInstances=1 has exactly one end to
	//   hand out: the check takes it, and the real write finds nothing left. With more instances,
	//   and a reader standing at each, the check can have the first and the write the second.
	//   ⚠If this works, the answer to the user's whole question turns from "no" into "yes", so it
	//     is written to be disbelieved: BOTH readers report separately, and the bytes of each are
	//     dropped where a zip tool outside InDesign can judge them.
	Trace("S17.6 the same export at a pipe with room for two");
	if (kProbeTheGate)
	{
		PMString line(Ascii("S17.6 ★ExportIDMLDirect at a MULTI-INSTANCE pipe: "));
		KCMPipeCatcher first(L"kcm-idml-multi", kTrue, kTrue, PIPE_UNLIMITED_INSTANCES);
		KCMPipeCatcher second(L"kcm-idml-multi", kTrue, kTrue, PIPE_UNLIMITED_INSTANCES);
		if (!first.Listening() || !second.Listening())
		{
			line.Append("could not stand up two readers (first error ");
			line.AppendNumber(static_cast<int32>(first.CreateError()));
			line.Append(", second error ");
			line.AppendNumber(static_cast<int32>(second.CreateError()));
			line.Append(")");
		}
		else
		{
			int32 ms = -1;
			ExportIdmlTo(docRef, PipeAsFile(first.Path()), mgr, line, ms);
			first.Finish();
			second.Finish();
			line.Append(" | reader A ");
			first.Describe(line);
			line.Append(" | reader B ");
			second.Describe(line);
			const int32 a = first.SaveTo(L"kcm-spike-idml-multi-a.idml");
			const int32 b = second.SaveTo(L"kcm-spike-idml-multi-b.idml");
			if (a > 0 || b > 0)
			{
				line.Append(" | saved A=");
				line.AppendNumber(a);
				line.Append(" B=");
				line.AppendNumber(b);
				line.Append(" to %TEMP%\\kcm-spike-idml-multi-*.idml (UNZIP THEM)");
			}
		}
		Say(out, line);
	}

	// ---- S17.7 THE READ DOOR, asked the way the WRITE door had to be asked ------------------
	// S17.3 tried ONE kind of stream, got UCFErrorCode 1, and that is exactly the answer the write
	// door gave to ten of its eleven candidates (S16). The one that worked there was the odd one -
	// CreateFileStreamWriteLazy, "a path it has not opened yet" - so one refusal here is not an
	// answer either. ⚠The IDFile row is the CONTROL: if even that fails, the fault is in the call
	// and not in the kind of stream, and every other row this step prints would be noise.
	Trace("S17.7 the read door, four ways");
	if (baselineSize > 0)
	{
		const int32 kWays = 4;
		PMString line(Ascii("S17.7 OpenPackage, four ways: "));
		for (int32 i = 0; i < kWays; ++i)
		{
			const char* const what = (i == 0) ? "IDFile (the control)"
			                       : (i == 1) ? "CreateMemoryStreamRead"
			                       : (i == 2) ? "CreateFileStreamRead"
			                                  : "CreateFileStreamReadLazy";
			line.Append("[");
			line.Append(Ascii(what));
			line.Append(" ");

			// ⚠One buffer per row, alive until the package is closed: a memory stream reads
			//   THROUGH this object, and a shared one would be read by the next row as well.
			KCMMemXferBytes bytes;
			InterfacePtr<IPMStream> stream;
			if (i == 1)
			{
				LoadFileInto(realPath.c_str(), bytes);
				stream.reset(StreamUtil::CreateMemoryStreamRead(&bytes, kFalse, kFalse));
			}
			else if (i == 2)
			{
				stream.reset(StreamUtil::CreateFileStreamRead(PipeAsFile(realPath.c_str())));
			}
			else if (i == 3)
			{
				stream.reset(StreamUtil::CreateFileStreamReadLazy(PipeAsFile(realPath.c_str())));
			}

			IUCFPackageUtils::UCFErrorCode err = IUCFPackageUtils::kSuccess;
			IUCFPackageUtils::PackageRefPtr ref = nil;
			if (i == 0)
			{
				ref = ucf->OpenPackage(PipeAsFile(realPath.c_str()), err);
			}
			else if (stream != nil)
			{
				ref = ucf->OpenPackage(stream, err);
			}
			else
			{
				line.Append("the stream itself could not be made] ");
				continue;
			}

			if (ref == nil)
			{
				line.Append("nil, err ");
				line.AppendNumber(static_cast<int32>(err));
				line.Append("] ");
				continue;
			}
			line.Append("★OPENED: ");
			DescribeIdmlPackage(ref, line);
			line.Append("] ");
			ucf->ClosePackage(ref);
		}
		Say(out, line);
	}
	else
	{
		Say(out, "S17.7 SKIPPED - there is no baseline package to open");
	}

	// ---- S17.8 ROUTE B: can the PARTS be exported one at a time? -----------------------------
	// If the exporter will write to nothing but a file, the other way to hold a real IDML is TO
	// BUILD ONE. Two thirds of that are already in hand: the container goes onto a stream (S16,
	// measured again today at 28,428 B through a pipe), and S17.2 showed exactly what has to go
	// inside it - 13 entries, designmap + META-INF x2 + Resources x4 + Spreads + MasterSpreads +
	// Stories + XML x2. The only unmeasured piece is whether ExportINX will take a root SMALLER
	// than the whole document.
	// ⚠2026-09-08 recorded "a spread as the root kills InDesign" - but THAT run passed policy=nil,
	//   which was later found to be the true cause of both crashes that day. So this is UNTESTED,
	//   not known-bad, and the policy used here is the one measured to work.
	Trace("S17.8 parts as roots");
	{
		PMString line(Ascii("S17.8 ExportINX on a root SMALLER than the document: "));
		ISession* const session = GetExecutionContextSession();
		InterfacePtr<IINXManager> inx(session != nil ? session->QueryINXManager() : nil);
		InterfacePtr<IPMUnknown> holder(
			(IPMUnknown*)::CreateObject(kActionExportPolicyBoss, IID_IINXEXPORTPOLICY));
		if (inx == nil || holder == nil)
		{
			line.Append("no INX manager, or the policy could not be made");
			Say(out, line);
		}
		else
		{
			IINXExportPolicy* const policy = (IINXExportPolicy*)holder.get();

			// The two parts a real IDML keeps in files of their own.
			UID spreadUID = kInvalidUID;
			{
				InterfacePtr<ISpreadList> spreads(doc, UseDefaultIID());
				if (spreads != nil && spreads->GetSpreadCount() > 0)
					spreadUID = spreads->GetNthSpreadUID(0);
			}
			UID storyUID = kInvalidUID;
			{
				InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
				if (stories != nil && stories->GetUserAccessibleStoryCount() > 0)
					storyUID = stories->GetNthUserAccessibleStoryUID(0).GetUID();
			}
			const UID parts[2] = { spreadUID, storyUID };
			const char* const names[2] = { "a SPREAD", "a STORY" };

			for (int32 i = 0; i < 2; ++i)
			{
				line.Append("[");
				line.Append(Ascii(names[i]));
				line.Append(" ");
				if (parts[i] == kInvalidUID)
				{
					line.Append("none in this document] ");
					continue;
				}
				InterfacePtr<IDOMElement> element(db, parts[i], UseDefaultIID());
				if (element == nil)
				{
					line.Append("★no IDOMElement on that boss] ");
					continue;
				}
				KCMMemXferBytes bytes;
				InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes, kFalse, kFalse));
				if (stream == nil)
				{
					line.Append("no stream] ");
					continue;
				}
				IDOMElement::ElementList roots;
				roots.push_back(element);
				// Same shape as KCMResourceSnapshot: Reset inside the session, both ends.
				inx->BeginExportSession();
				element->Reset();
				const ErrorCode err = inx->ExportINX(roots, policy, stream, kSuppressUI);
				element->Reset();
				inx->EndExportSession();
				stream->Close();

				line.Append("err ");
				line.AppendNumber(static_cast<int32>(err));
				line.Append(", ");
				line.AppendNumber(static_cast<int32>(bytes.GetSize()));
				line.Append(" B");
				// ★WHAT CAME OUT ON TOP is the whole point: a part, or the whole document again?
				const char* const data = bytes.GetData();
				const uint32 len = bytes.GetSize();
				if (data != nil && len > 0)
				{
					// ★EVERY count, not the first hit. A wrapper element says nothing about what
					//   is inside it, and "is there a <Document> tag" was answering the wrong
					//   question: of course there is, it is the root INX always writes.
					line.Append(", root <");
					FirstElementName(data, len, line);
					line.Append(">, Document x");
					line.AppendNumber(CountIn(data, len, "<Document "));
					line.Append(", Spread x");
					line.AppendNumber(CountIn(data, len, "<Spread "));
					line.Append(", MasterSpread x");
					line.AppendNumber(CountIn(data, len, "<MasterSpread "));
					line.Append(", Page x");
					line.AppendNumber(CountIn(data, len, "<Page "));
					line.Append(", Story x");
					line.AppendNumber(CountIn(data, len, "<Story "));
					line.Append(", TextFrame x");
					line.AppendNumber(CountIn(data, len, "<TextFrame "));
					line.Append(", ParagraphStyle x");
					line.AppendNumber(CountIn(data, len, "<ParagraphStyle "));
					line.Append(", Content x");
					line.AppendNumber(CountIn(data, len, "<Content>"));
				}
				// ★The one check this code cannot make for itself: a person reading the XML.
				DropForTheEye(bytes, (i == 0) ? L"kcm-spike-part-spread.xml"
				                              : L"kcm-spike-part-story.xml");
				line.Append(" -> %TEMP%] ");
			}
			Say(out, line);
		}
	}

	// ---- S19 ★★★WHAT DOES THE GATE ACTUALLY CHECK? -----------------------------------------
	// 9491 reads "Cannot save to the file, because it is already open". Three different mechanisms
	// would produce that, and which one it is decides whether anything can be done about it:
	//   (a) the exporter opens the destination EXCLUSIVELY, and loses to anyone already holding it
	//   (b) the exporter refuses a handle whose GetFileType says FILE_TYPE_PIPE
	//       (\\.\NUL is FILE_TYPE_CHAR, and NUL passes - S17.5)
	//   (c) something about the shape of the path
	// ★(c) IS ALREADY OUT: \\.\NUL and \\.\pipe\x have the same shape and one of them passes.
	// These three rows separate (a) from (b), and each one is a single export:
	//   B1  a REAL FILE this code holds open with NO sharing    - 9491 here means (a)
	//   B2  the same, but opened WITH sharing                   - passing here means it is the LOCK
	//   E   a PIPE PATH with nobody listening at all            - anything but 9491 means (b)
	// ⚠B1/B2 write to files this code makes and removes itself; they are not the user's.
	Trace("S19 what the gate checks");
	if (kProbeTheGate)
	{
		for (int32 shared = 0; shared < 2; ++shared)
		{
			std::wstring heldPath(tempDir);
			heldPath += (shared == 0) ? L"kcm-spike-held-excl.idml" : L"kcm-spike-held-share.idml";
			::DeleteFileW(heldPath.c_str());
			const DWORD share = (shared == 0) ? 0
			                                  : (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
			HANDLE held = ::CreateFileW(heldPath.c_str(), GENERIC_WRITE, share, nil,
			                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nil);
			PMString line(Ascii(shared == 0 ? "S19.B1 a REAL FILE we hold open, NO sharing: "
			                                : "S19.B2 a REAL FILE we hold open, WITH sharing: "));
			if (held == INVALID_HANDLE_VALUE)
			{
				line.Append("could not create it, Win32 error ");
				line.AppendNumber(static_cast<int32>(::GetLastError()));
			}
			else
			{
				int32 ms = -1;
				ExportIdmlTo(docRef, PipeAsFile(heldPath.c_str()), mgr, line, ms);
				::CloseHandle(held);
				WIN32_FILE_ATTRIBUTE_DATA fad;
				std::memset(&fad, 0, sizeof(fad));
				line.Append(", ");
				if (::GetFileAttributesExW(heldPath.c_str(), GetFileExInfoStandard, &fad))
				{
					line.AppendNumber(static_cast<int32>(fad.nFileSizeLow));
					line.Append(" B on disk");
				}
				else
				{
					line.Append("no file");
				}
				::DeleteFileW(heldPath.c_str());
			}
			Say(out, line);
		}
		{
			// ★Nobody has called CreateNamedPipe for this name, so there is nothing to be "already
			//   open". If 9491 still comes back, the message is not describing what is measured.
			PMString line(Ascii("S19.E a PIPE PATH with NO listener at all: "));
			int32 ms = -1;
			ExportIdmlTo(docRef, PipeAsFile(L"\\\\.\\pipe\\kcm-nobody-is-home"), mgr, line, ms);
			Say(out, line);
		}
	}
}

/** The UID an IDML part name carries: "Stories/Story_u101.xml" -> 0x101. kInvalidUID when the
    name is not of that shape. ★This is how a part is matched back to the object it came from,
    without having to enumerate anything. */
UID UidFromPartName(const char* name)
{
	const char* p = std::strstr(name, "_u");
	if (p == nil)
		return kInvalidUID;
	p += 2;
	uint32 v = 0;
	bool16 any = kFalse;
	while (*p != 0 && *p != '.')
	{
		const char c = *p++;
		uint32 digit = 0;
		if (c >= '0' && c <= '9')			digit = static_cast<uint32>(c - '0');
		else if (c >= 'a' && c <= 'f')		digit = 10 + static_cast<uint32>(c - 'a');
		else if (c >= 'A' && c <= 'F')		digit = 10 + static_cast<uint32>(c - 'A');
		else								return kInvalidUID;
		v = v * 16 + digit;
		any = kTrue;
	}
	return any ? UID(v) : kInvalidUID;
}

/** Find one <Tag ...> ... </Tag> subtree and hand back its byte range.
    ⚠Good enough because none of the elements this is used on nest inside themselves. */
bool16 FindSubtree(const char* d, uint32 len, const char* open, const char* close,
                   uint32& begin, uint32& end)
{
	const uint32 ol = static_cast<uint32>(std::strlen(open));
	const uint32 cl = static_cast<uint32>(std::strlen(close));
	for (uint32 i = 0; i + ol <= len; ++i)
	{
		if (std::memcmp(d + i, open, ol) != 0)
			continue;
		for (uint32 j = i; j + cl <= len; ++j)
		{
			if (std::memcmp(d + j, close, cl) != 0)
				continue;
			begin = i;
			end = j + cl;
			return kTrue;
		}
		return kFalse;
	}
	return kFalse;
}

/** ExportINX with one element as the root - the call S17.8 measured, in a form both callers use. */
bool16 ExportElementAsInx(InterfacePtr<IDOMElement>& element, KCMMemXferBytes& into)
{
	if (element == nil)
		return kFalse;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inx(session != nil ? session->QueryINXManager() : nil);
	InterfacePtr<IPMUnknown> holder(
		(IPMUnknown*)::CreateObject(kActionExportPolicyBoss, IID_IINXEXPORTPOLICY));
	if (inx == nil || holder == nil)
		return kFalse;
	IINXExportPolicy* const policy = (IINXExportPolicy*)holder.get();
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&into, kFalse, kFalse));
	if (stream == nil)
		return kFalse;
	IDOMElement::ElementList roots;
	roots.push_back(element);
	inx->BeginExportSession();
	element->Reset();
	const ErrorCode err = inx->ExportINX(roots, policy, stream, kSuppressUI);
	element->Reset();
	inx->EndExportSession();
	stream->Close();
	return (err == kSuccess) ? kTrue : kFalse;
}

/** ★★★P2/P3 - BUILD AN IDML PART OURSELVES, out of what ExportINX gives.
    The two were compared byte for byte on 2026-09-14: what INX writes inside <Story> is what IDML
    writes inside <Story>, to the character, and the repacked package opened in InDesign. The
    difference is only the dress:
        INX : <?aid ... type="action"?> <Document ...> [dependencies] <Story>...</Story> </Document>
        IDML: <idPkg:Story xmlns:idPkg="..." DOMVersion="21.0">        <Story>...</Story> </idPkg:Story>
    ★Nothing is EDITED - the subtree is CUT OUT and re-dressed. A cut cannot corrupt what it does
      not touch, where a search-and-replace over the whole file could.
    ★The indentation lines up by luck that is not luck: the object sits one level under the root
      in both, so the tabs INX already wrote are the tabs IDML wants.

    ⚠WHY THIS ONLY COVERS THE PARTS NAMED AFTER A UID. Stories, Spreads and MasterSpreads are
      single objects with a UID in the file name, so each one can be asked for by itself. The
      Resources parts are COLLECTIONS (every colour, every style) and XML/BackingStory has no UID
      in its name - those need a different move and are still copied from the original. */
struct PartRecipe
{
	const char* prefix;		// how the part's name begins
	const char* innerOpen;	// the element to cut out, with its trailing space
	const char* innerClose;	// and its closing tag
	const char* pkgTag;		// the idPkg dress to put on it
};

/** The recipe for a part name, or nil when this code cannot build that one.
    ⚠"</Spread>" does NOT occur inside "</MasterSpread>" (checked: the characters do not line
      up), so the two recipes cannot be confused for each other. */
const PartRecipe* RecipeFor(const char* name)
{
	static const PartRecipe kRecipes[] = {
		{ "Stories/",       "<Story ",        "</Story>",        "idPkg:Story"        },
		{ "Spreads/",       "<Spread ",       "</Spread>",       "idPkg:Spread"       },
		{ "MasterSpreads/", "<MasterSpread ", "</MasterSpread>", "idPkg:MasterSpread" },
	};
	for (int32 i = 0; i < 3; ++i)
	{
		const size_t plen = std::strlen(kRecipes[i].prefix);
		if (std::strlen(name) > plen && std::memcmp(name, kRecipes[i].prefix, plen) == 0)
			return &kRecipes[i];
	}
	return nil;
}

/** Build one IDML part from the object it is named after. */
bool16 MakeObjectPart(IDataBase* db, UID uid, const PartRecipe& recipe, KCMMemXferBytes& into)
{
	InterfacePtr<IDOMElement> element(db, uid, UseDefaultIID());
	if (element == nil)
		return kFalse;
	KCMMemXferBytes inx;
	if (!ExportElementAsInx(element, inx))
		return kFalse;
	const char* const d = inx.GetData();
	const uint32 dl = inx.GetSize();
	uint32 b = 0;
	uint32 e = 0;
	if (d == nil || !FindSubtree(d, dl, recipe.innerOpen, recipe.innerClose, b, e))
		return kFalse;
	// Byte for byte the shape of a real part (measured: LF, tabs, no <?aid?> instruction).
	std::string head("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<");
	head += recipe.pkgTag;
	head += " xmlns:idPkg=\"http://ns.adobe.com/AdobeInDesign/idml/1.0/packaging\" DOMVersion=\"21.0\">\n\t";
	std::string tail("\n</");
	tail += recipe.pkgTag;
	tail += ">\n";
	into.Write(const_cast<char*>(head.c_str()), static_cast<uint32>(head.size()));
	into.Write(const_cast<char*>(d + b), e - b);
	into.Write(const_cast<char*>(tail.c_str()), static_cast<uint32>(tail.size()));
	return kTrue;
}

/** Read one entry of an open package into memory. Answers the byte count, or -1. */
int32 ReadPackageEntry(IUCFPackageUtils::PackageRefPtr ref, const char* name, KCMMemXferBytes& into)
{
	Utils<IUCFPackageUtils> ucf;
	if (!ucf)
		return -1;
	InterfacePtr<IPMStream> s(ucf->OpenStream(ref, WideString(name)));
	if (s == nil)
		return -1;
	uchar buf[8192];
	int32 total = 0;
	for (;;)
	{
		const int32 n = s->XferByte(buf, static_cast<int32>(sizeof(buf)));
		if (n <= 0)
			break;
		into.Write(buf, static_cast<uint32>(n));
		total += n;
	}
	s->Close();
	return total;
}

//========================================================================================
//  ★★★S18 - ROUTE B, STEP ONE: REPACK.
//
//  S17 settled that the product will not write an IDML anywhere but a real, unopened file. So the
//  other way to hold one is TO BUILD IT - and the pieces are all measured now:
//     the container goes onto a stream, and a stream can be a pipe   (S16, 28,428 B into memory)
//     ExportINX takes a root smaller than the document               (S17.8)
//     what it produces IS the real thing inside                      (S17.8, byte for byte)
//     a package can be read back from a file stream                  (S17.7)
//
//  ⚠BUT DO NOT BUILD EVERYTHING AT ONCE. Two things could be wrong - the CONTAINER (are the
//    entries named, ordered and compressed the way a real IDML is?) and the PARTS (does the
//    <idPkg:...> dress fit?) - and if both are built at once, a failure says nothing about which.
//
//  SO THIS STEP CHANGES ONLY THE CONTAINER. It reads a real IDML apart and writes THE SAME BYTES
//  back into a container this code made. If InDesign opens the result, the container is right and
//  every later failure belongs to the parts. If it does not, there is no point writing a single
//  <idPkg:Story> until it does.
//
//  ★The destination is a real file ON PURPOSE for this step. Not because the pipe does not work -
//    it does, S16 proves it twice a run - but because a pipe also breaks the zip's index (nothing
//    can seek back to patch it), and a broken index would be indistinguishable from a container
//    built wrong. One unknown at a time: the file first, the pipe once the recipe is known good.
//========================================================================================
void RepackIdml(IDataBase* db, PMString& out, bool16 ownStories)
{
	Utils<IUCFPackageUtils> ucf;
	if (!ucf)
	{
		Say(out, "S18 SKIPPED - no IUCFPackageUtils");
		return;
	}
	wchar_t tempDir[MAX_PATH] = { 0 };
	::GetTempPathW(MAX_PATH, tempDir);
	std::wstring srcPath(tempDir);
	srcPath += L"kcm-spike-real.idml";
	std::wstring dstPath(tempDir);
	dstPath += ownStories ? L"kcm-spike-repacked-own.idml" : L"kcm-spike-repacked.idml";
	::DeleteFileW(dstPath.c_str());

	PMString line(Ascii(ownStories
		? "S20 repack, and BUILD EVERY UID-NAMED PART OURSELVES: "
		: "S18 repack a real IDML into a container of our own: "));

	IUCFPackageUtils::UCFErrorCode err = IUCFPackageUtils::kSuccess;
	IUCFPackageUtils::PackageRefPtr srcRef = ucf->OpenPackage(PipeAsFile(srcPath.c_str()), err);
	if (srcRef == nil)
	{
		line.Append("the baseline could not be opened (err ");
		line.AppendNumber(static_cast<int32>(err));
		line.Append(") - S17.2 has to have run first");
		Say(out, line);
		return;
	}

	// ---- what is in it. ★UCF HAS NO ENUMERATE, so the names come from designmap.xml itself:
	//      every part but the fixed three is named there as <idPkg:Xxx src="..."/>.
	KCMMemXferBytes mapBytes;
	const int32 mapLen = ReadPackageEntry(srcRef, "designmap.xml", mapBytes);
	if (mapLen <= 0)
	{
		line.Append("designmap.xml did not read back");
		Say(out, line);
		ucf->ClosePackage(srcRef);
		return;
	}
	std::vector<std::string> names;
	names.push_back("designmap.xml");
	names.push_back("META-INF/container.xml");
	names.push_back("META-INF/metadata.xml");
	{
		const char* const d = mapBytes.GetData();
		const uint32 dl = mapBytes.GetSize();
		for (uint32 i = 0; i + 5 < dl; ++i)
		{
			if (std::memcmp(d + i, "src=\"", 5) != 0)
				continue;
			uint32 j = i + 5;
			std::string one;
			while (j < dl && d[j] != '"')
				one += d[j++];
			if (!one.empty())
				names.push_back(one);
			i = j;
		}
	}
	line.Append("the baseline holds ");
	line.AppendNumber(static_cast<int32>(names.size()));
	line.Append(" named parts (+mimetype); ");

	// ---- a container of our own.
	// ⚠createManifest is kFALSE here: S16 asked for one and got an entry called "manifest.xml",
	//   which a real IDML does NOT have - it carries META-INF/container.xml instead, and that one
	//   is copied across like any other part.
	const AString kIdmlMime("application/vnd.adobe.indesign-idml-package");
	InterfacePtr<IPMStream> zipOut(StreamUtil::CreateFileStreamWriteLazy(
		PipeAsFile(dstPath.c_str()), kOpenOut | kOpenTrunc));
	if (zipOut == nil)
	{
		line.Append("could not make the lazy stream");
		Say(out, line);
		ucf->ClosePackage(srcRef);
		return;
	}
	Trace("S18.1 CreatePackage");
	IUCFPackageUtils::PackageRefPtr dstRef = ucf->CreatePackage(zipOut, kIdmlMime, kFalse, err);
	Trace("S18.2 CreatePackage came back");
	if (dstRef == nil)
	{
		line.Append("CreatePackage nil, err ");
		line.AppendNumber(static_cast<int32>(err));
		Say(out, line);
		ucf->ClosePackage(srcRef);
		return;
	}

	// ---- copy every part across, and count what did NOT make it.
	int32 copied = 0;
	int32 copiedBytes = 0;
	int32 failed = 0;
	for (size_t n = 0; n < names.size(); ++n)
	{
		KCMMemXferBytes part;
		int32 got = -1;
		// ★P3: every part whose name carries a UID is BUILT rather than copied. The rest still
		//   come from the original, so a failure to open the result names its own cause.
		const PartRecipe* const recipe = ownStories ? RecipeFor(names[n].c_str()) : nil;
		if (recipe != nil)
		{
			const UID uid = UidFromPartName(names[n].c_str());
			if (uid != kInvalidUID && MakeObjectPart(db, uid, *recipe, part))
			{
				got = static_cast<int32>(part.GetSize());
				line.Append("[★built ");
				line.Append(Ascii(names[n].c_str()));
				line.Append(" ");
				line.AppendNumber(got);
				line.Append(" B] ");
			}
			else
			{
				line.Append("[★COULD NOT BUILD ");
				line.Append(Ascii(names[n].c_str()));
				line.Append(" - copying it instead] ");
			}
		}
		if (got <= 0)
			got = ReadPackageEntry(srcRef, names[n].c_str(), part);
		if (got <= 0)
		{
			++failed;
			line.Append("[cannot read ");
			line.Append(Ascii(names[n].c_str()));
			line.Append("] ");
			continue;
		}
		InterfacePtr<IPMStream> entry(ucf->CreateStream(dstRef, WideString(names[n].c_str()),
		                                               IUCFPackageUtils::kStandard));
		if (entry == nil)
		{
			++failed;
			line.Append("[cannot write ");
			line.Append(Ascii(names[n].c_str()));
			line.Append("] ");
			continue;
		}
		entry->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(part.GetData())), got);
		entry->Close();
		++copied;
		copiedBytes += got;
	}
	Trace("S18.3 ClosePackage");
	const IUCFPackageUtils::UCFErrorCode closed = ucf->ClosePackage(dstRef);
	Trace("S18.4 ClosePackage came back");
	ucf->ClosePackage(srcRef);
	zipOut.reset(nil);		// ★flush before anything measures the file

	line.Append("copied ");
	line.AppendNumber(copied);
	line.Append(" parts / ");
	line.AppendNumber(copiedBytes);
	line.Append(" B, failed ");
	line.AppendNumber(failed);
	line.Append(", Close -> ");
	line.AppendNumber(static_cast<int32>(closed));

	WIN32_FILE_ATTRIBUTE_DATA fad;
	std::memset(&fad, 0, sizeof(fad));
	if (::GetFileAttributesExW(dstPath.c_str(), GetFileExInfoStandard, &fad))
	{
		line.Append(", the new package is ");
		line.AppendNumber(static_cast<int32>(fad.nFileSizeLow));
		line.Append(" B at %TEMP%\\kcm-spike-repacked.idml");
	}
	else
	{
		line.Append(", ★NO FILE WAS WRITTEN");
	}
	Say(out, line);

	// ---- ★the first judge: can UCF itself open what we just built, and does it still look like
	//      an IDML? A container InDesign cannot read is not worth carrying to the next step.
	Trace("S18.5 reopen what we built");
	{
		PMString check(Ascii(ownStories ? "S20b reopening it: " : "S18b reopening our own package: "));
		IUCFPackageUtils::UCFErrorCode rerr = IUCFPackageUtils::kSuccess;
		IUCFPackageUtils::PackageRefPtr back = ucf->OpenPackage(PipeAsFile(dstPath.c_str()), rerr);
		if (back == nil)
		{
			check.Append("★REFUSED BY UCF ITSELF, err ");
			check.AppendNumber(static_cast<int32>(rerr));
		}
		else
		{
			check.Append("opened, ");
			DescribeIdmlPackage(back, check);
			ucf->ClosePackage(back);
			check.Append(" ⇒ NOW LET INDESIGN OPEN IT - that is the only verdict that counts");
		}
		Say(out, check);
	}
}

/** One page of `db` out through kPDFExportCmdBoss - THE PAGE EXPORTER, the one whose picture is
    right - aimed at whatever `target` names. The same settings the report uses (KCMReport.cpp,
    ExportPagesToPDF); the only thing being varied is where it points. */
void ExportOnePageTo(IDataBase* db, UID pageUID, const IDFile& target,
                     ErrorCode& err, ErrorCode& global, int32& msTaken)
{
	err = kFailure;
	global = kFailure;
	msTaken = -1;
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kPDFExportCmdBoss));
	InterfacePtr<IOutputPages> pagesOut(cmd, IID_IOUTPUTPAGES);
	InterfacePtr<ISysFileData> sys(cmd, IID_ISYSFILEDATA);
	if (cmd == nil || pagesOut == nil || sys == nil)
		return;

	UIDList onePage(db);
	onePage.Append(pageUID);
	cmd->SetItemList(onePage);

	InterfacePtr<IPDFExportPrefs> appPrefs((IPDFExportPrefs*)::QuerySessionPreferences(IID_IPDFEXPORTPREFS));
	InterfacePtr<IPDFExportPrefs> prefs(cmd, IID_IPDFEXPORTPREFS);
	if (prefs != nil && appPrefs != nil)
	{
		prefs->CopyPrefs(appPrefs);
		prefs->SetPDFExReaderSpreads(IPDFExportPrefs::kExportReaderSpreadsOFF);
	}
	InterfacePtr<IPDFSecurityPrefs> security(cmd, IID_IPDFSECURITYPREFS);
	if (security != nil)
		security->SetUseSecurity(kFalse);
	InterfacePtr<IPDFPostProcessPrefs> post(cmd, IID_IPDFPOSTPROCESSPREFS);
	if (post != nil)
		post->SetViewAfterExport(kFalse);
	InterfacePtr<IBoolData> progress(cmd, IID_IUSEPROGRESSINDICATOR);
	if (progress != nil)
		progress->Set(kFalse);
	InterfacePtr<IUIFlagData> ui(cmd, IID_IUIFLAGDATA);
	if (ui != nil)
		ui->Set(kSuppressUI);

	sys->Set(target);
	pagesOut->InitializeFrom(onePage, kFalse);
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		PMString name;
		if (doc != nil)
			doc->GetName(name);
		pagesOut->SetName(name);
	}

	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	Trace("        ProcessCommand in");
	LARGE_INTEGER freq, t0, t1;
	::QueryPerformanceFrequency(&freq);
	::QueryPerformanceCounter(&t0);
	err = CmdUtils::ProcessCommand(cmd);
	::QueryPerformanceCounter(&t1);
	Trace("        ProcessCommand out");
	global = ErrorUtils::PMGetGlobalErrorCode();
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	// ★★★HOW LONG IT TOOK IS THE ANSWER TO "WHERE DID IT REFUSE". An export that renders a page
	//   and then cannot write it takes as long as a real one; an export that looks at the path
	//   and says no is over in a blink. A failure alone cannot tell those apart.
	msTaken = (freq.QuadPart > 0)
	          ? static_cast<int32>(((t1.QuadPart - t0.QuadPart) * 1000) / freq.QuadPart)
	          : -1;
}

/** Export `items` of `db` to a PDF that never touches disk, and answer whether any bytes
    arrived. `why` says what went wrong when they did not.

    ⚠**THE DOOR THE GUIDE GETS WRONG.** vol2-05:351 puts the stream pointer in IID_IINTDATA,
      which this boss does not have - and which would cut a 64-bit pointer in half if it did,
      IIntData::ValueType being int32. The measured interface list (work/Boss.txt) has
      IID_IPMUNKNOWNDATA on kPMUnknownData_SoftReference_Impl, and that is what is used here.
    ⚠**SOFT reference**: that implementation does not AddRef what it is given, so the stream
      has to outlive the command. It does - both live in this function. */
bool16 ExportToMemory(IDataBase* db, const UIDList& items, KCMMemXferBytes& bytes, PMString& why,
					  bool16 useExportPrefs = kFalse, bool16 announce = kFalse)
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
	// ⚠**WHICH PREFERENCES.** The guide's example for this command copies the CLIPBOARD ones,
	//   because the command exists to put PDF on the clipboard - and a clipboard PDF is allowed
	//   to be a flattened approximation. The ordinary EXPORT preferences are what a real PDF is
	//   made with. Measured both ways here (2026-09-14) because the picture came out with its
	//   transparency gone and its stacking order reversed.
	InterfacePtr<IPDFExportPrefs> appPrefs((IPDFExportPrefs*)::QuerySessionPreferences(
		useExportPrefs ? IID_IPDFEXPORTPREFS : IID_IPDFCLIPBOARDEXPORTPREFS));
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

	// ⚠**ANNOUNCING THE EXPORT** puts a representative item on the transparency list, which is
	//   what makes the flattener run. kPDFExportItemsCmdBoss raises no kPDFExportSetupService
	//   event, so nothing announces it by itself.
	if (announce)
		KCMBeginExportOn(db);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	stream->Flush();
	if (announce)
		KCMEndExportOnThisThread();
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

/** ★★★THE INSTRUMENT, ARMED - and it had to be, because the bare byte count lied in BOTH
    directions on 2026-09-14:
      - 2 bytes of difference were once read as "something of the marks reached it" (they were
        compression noise), and
      - 4,057 bytes were read as "the pasteboard came too" (the picture says it did not, and the
        SAME export measured twice came out 400,237 then 398,756 - 1,481 bytes apart with
        nothing changed at all).
    So the noise is measured in the same run as the signal: the list goes out TWICE with the
    marks off, and whatever "marks on" differs by has to beat that spread before it is called a
    signal. `onBytes` keeps the marks-on export for the caller (the box, or the eye). */
void MarkTest(IDataBase* db, const UIDList& list, PMString& line, KCMMemXferBytes& onBytes)
{
	const bool16 was = KCMDrawEventHandler::sPrintMarks;
	KCMMemXferBytes off1;
	KCMMemXferBytes off2;
	PMString why;
	KCMDrawEventHandler::sPrintMarks = kFalse;
	const bool16 ok1 = ExportToMemory(db, list, off1, why);
	const bool16 ok2 = ExportToMemory(db, list, off2, why);
	KCMDrawEventHandler::sPrintMarks = kTrue;
	const bool16 ok3 = ExportToMemory(db, list, onBytes, why);
	KCMDrawEventHandler::sPrintMarks = was;
	if (!ok1 || !ok2 || !ok3)
	{
		line.Append("FAILED - ");
		line.Append(why);
		return;
	}
	const int32 a = static_cast<int32>(off1.GetSize());
	const int32 b = static_cast<int32>(off2.GetSize());
	const int32 c = static_cast<int32>(onBytes.GetSize());
	const int32 noise = (a > b) ? (a - b) : (b - a);
	const int32 mid = (a + b) / 2;
	const int32 signal = (c > mid) ? (c - mid) : (mid - c);
	line.Append("off ");
	line.AppendNumber(a);
	line.Append(" / ");
	line.AppendNumber(b);
	line.Append(" (noise ");
	line.AppendNumber(noise);
	line.Append("), on ");
	line.AppendNumber(c);
	line.Append(" (signal ");
	line.AppendNumber(signal);
	line.Append(") -> ");
	// Two conditions, and both earn their place: beating the noise of THIS run, and a floor so
	// that a run which happens to be quiet does not turn 50 bytes into a discovery.
	line.Append((signal > noise * 2 && signal > 500) ? "MARKS REACHED IT" : "NO - that is noise");
}

/** What one of IPDFExportController's eight event IDs reads as. */
const char* EventName(PDFExportEventID id)
{
	switch (id)
	{
		case kPDFExportEventBeginExport:	return "Begin";
		case kPDFExportEventDrawPage:		return "DrawPage";
		case kPDFExportEventDrawSpread:		return "DrawSpread";
		case kPDFExportEventDrawItem:		return "DrawItem";
		case kPDFExportEventPreserveInDesignEditingDetails:	return "PreserveIDML";
		case kPDFExportEventEndExport:		return "End";
		case kPDFExportEventNewDocument:	return "NewDocument";
		case kPDFExportEventDrawGalleyPage:	return "DrawGalleyPage";
	}
	return "?";
}

/** What one of the eight PDFExportErr values reads as. */
const char* ExportErrName(PDFExportErr e)
{
	switch (e)
	{
		case kPDFExportErrSuccess:				return " (success)";
		case kPDFExportErrAlreadyStartedUp:		return " (already started up)";
		case kPDFExportErrAlreadyShutDown:		return " (already shut down / not started)";
		case kPDFExportErrNoViewPort:			return " (the export view port could not be created)";
		case kPDFExportErrFileAlreadyOpen:		return " (the destination file is already open)";
		case kPDFExportErrFileLocked:			return " (the destination file is locked)";
		case kPDFExportErrUnknownFailure:		return " (unknown failure)";
		case kPDFExportErrStreamCreationFailure:	return " (the output stream could not be created)";
	}
	return " (an unlisted code)";
}

// Defined below; used by the snippet helper that follows.
bool16 PagePosition(IDataBase* db, UID pageUID, InterfacePtr<ISpread>& spread, int32& pgPos);

/** Carry page 1 of `from` into `into`'s first spread as a SNIPPET - real page items, not a
	picture - and answer how many arrived. ⚠Styles and swatches travel BY NAME, which is the whole
	question S14 asks. */
int32 SnippetOnePageInto(IDataBase* from, const UIDRef& into)
{
	if (from == nil || into == UIDRef::gNull)
		return -1;
	std::vector<UID> pages;
	KCMCollectPageUIDs(from, pages);
	if (pages.empty())
		return -1;

	KCMMemXferBytes snippet;
	{
		InterfacePtr<ISpread> spread;
		int32 pgPos = -1;
		if (!PagePosition(from, pages[0], spread, pgPos))
			return -1;
		UIDList onPage(from);
		spread->GetItemsOnPage(pgPos, &onPage, kFalse, kFalse, kTrue);
		if (onPage.Length() == 0)
			return 0;
		InterfacePtr<IPMStream> write(StreamUtil::CreateMemoryStreamWrite(&snippet, kFalse, kFalse));
		Utils<ISnippetExport> exporter;
		if (write == nil || !exporter)
			return -1;
		if (exporter->ExportPageitems(write, onPage) != kSuccess)
			return -1;
		write->Flush();
	}
	if (snippet.GetSize() == 0)
		return -1;

	InterfacePtr<IDocument> doc(into, UseDefaultIID());
	InterfacePtr<ISpreadList> spreads(doc, UseDefaultIID());
	const UID spreadUID = (spreads != nil && spreads->GetSpreadCount() > 0) ? spreads->GetNthSpreadUID(0) : kInvalidUID;
	InterfacePtr<ISpread> spread(into.GetDataBase(), spreadUID, UseDefaultIID());
	InterfacePtr<IDOMElement> frag(into.GetDataBase(), spreadUID, UseDefaultIID());
	InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&snippet, kFalse, kFalse));
	Utils<ISnippetImport> importer;
	if (spread == nil || frag == nil || read == nil || !importer)
		return -1;

	int32 had = 0;
	{
		UIDList before(into.GetDataBase());
		spread->GetItemsOnPage(0, &before, kFalse, kTrue, kTrue);
		had = before.Length();
	}
	const ErrorCode err = importer->ImportFromStream(read, frag, kInvalidClass, kSuppressUI, nil);
	read->Close();
	if (err != kSuccess)
		return -1;
	UIDList after(into.GetDataBase());
	spread->GetItemsOnPage(0, &after, kFalse, kTrue, kTrue);
	return after.Length() - had;
}

/** How many swatches the document has, and the first few names - enough to see whether a
	same-named swatch arrived as itself, as a copy, or not at all. */
void DescribeSwatches(IDataBase* db, PMString& into)
{
	Utils<ISwatchUtils> swatchUtils;
	InterfacePtr<ISwatchList> swatches(swatchUtils ? swatchUtils->QuerySwatchList(db) : nil);
	if (swatches == nil)
	{
		into.Append("(no swatch list)");
		return;
	}
	const int32 n = swatches->GetNumSwatches();
	into.AppendNumber(n);
	into.Append(" swatches:");
	for (int32 i = 0; i < n && i < 40; ++i)
	{
		const UIDRef swatch = swatches->GetNthSwatch(i);
		const PMString name = swatchUtils->GetSwatchName(swatch.GetDataBase(), swatch.GetUID());
		if (name.IsEmpty())
			continue;
		into.Append(" ");
		into.Append(name);
	}
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

//========================================================================================
//  ★★★S21 - THE SMALLEST IDML THIS CODE CAN BUILD, AND WHETHER THAT IS ENOUGH.
//
//  P3a proved the parts named after a UID can be built from ExportINX, byte for byte. What is
//  left are the COLLECTIONS (Resources x4, Tags) and designmap.xml itself - and routing those
//  would mean baking a table of ~58 element names into this file, one that grows with every
//  feature a document uses.
//
//  ★SO ASK A CHEAPER QUESTION FIRST: does the split matter at all?
//    ExportINX already writes the WHOLE document as one XML - every colour, every style, every
//    spread, every story, in one tree under <Document>. A real IDML holds the same tree, only
//    cut into 13 files with <idPkg:* src="..."/> standing where each cut was made.
//    IF INDESIGN WILL READ A designmap.xml THAT WAS NEVER CUT, then a whole IDML is three
//    entries - mimetype, META-INF/container.xml, designmap.xml - and the routing table is never
//    needed at all.
//
//  The transformation is two edits to the INX, and nothing else:
//    1. <?aid ... type="action" ...?>  ->  type="document"
//    2. <Document DOMVersion=...       ->  <Document xmlns:idPkg="...packaging" DOMVersion=...
//  ⚠What a real designmap has and this will not: a StoryList attribute, and the nine idPkg
//    references. Whether either is required is exactly what this step measures.
//========================================================================================
void MinimalIdml(IDataBase* db, PMString& out)
{
	Utils<IUCFPackageUtils> ucf;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	InterfacePtr<IDOMElement> docElement(doc, UseDefaultIID());
	PMString line(Ascii("S21 the smallest IDML we can build: "));
	if (!ucf || docElement == nil)
	{
		line.Append("no IUCFPackageUtils, or the document has no IDOMElement");
		Say(out, line);
		return;
	}

	// ---- the whole document, as one XML, in memory ----------------------------------------
	KCMMemXferBytes inx;
	if (!ExportElementAsInx(docElement, inx) || inx.GetSize() == 0)
	{
		line.Append("ExportINX gave nothing");
		Say(out, line);
		return;
	}
	line.AppendNumber(static_cast<int32>(inx.GetSize()));
	line.Append(" B of document XML; ");

	// ---- the two edits --------------------------------------------------------------------
	std::string xml(inx.GetData(), inx.GetSize());
	const std::string fromPI("type=\"action\"");
	const std::string toPI("type=\"document\"");
	const size_t atPI = xml.find(fromPI);
	if (atPI == std::string::npos)
	{
		line.Append("★the processing instruction does not say type=\"action\" - not touching it");
		Say(out, line);
		return;
	}
	xml.replace(atPI, fromPI.size(), toPI);

	const std::string anchor("<Document ");
	const size_t atDoc = xml.find(anchor);
	if (atDoc == std::string::npos)
	{
		line.Append("★no <Document> element to dress");
		Say(out, line);
		return;
	}
	xml.insert(atDoc + anchor.size(),
	           "xmlns:idPkg=\"http://ns.adobe.com/AdobeInDesign/idml/1.0/packaging\" ");
	line.Append("PI and namespace rewritten, ");
	line.AppendNumber(static_cast<int32>(xml.size()));
	line.Append(" B; ");

	// ---- three entries, and two of them are fixed text -------------------------------------
	// ★Read out of a real package, byte for byte (2026-09-14).
	static const char* const kContainer =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
		"\t<rootfiles>\n"
		"\t\t<rootfile full-path=\"designmap.xml\" media-type=\"text/xml\">\n"
		"\t\t</rootfile>\n"
		"\t</rootfiles>\n"
		"</container>\n";

	wchar_t tempDir[MAX_PATH] = { 0 };
	::GetTempPathW(MAX_PATH, tempDir);
	std::wstring dstPath(tempDir);
	dstPath += L"kcm-spike-minimal.idml";
	::DeleteFileW(dstPath.c_str());

	const AString kIdmlMime("application/vnd.adobe.indesign-idml-package");
	InterfacePtr<IPMStream> zipOut(StreamUtil::CreateFileStreamWriteLazy(
		PipeAsFile(dstPath.c_str()), kOpenOut | kOpenTrunc));
	IUCFPackageUtils::UCFErrorCode err = IUCFPackageUtils::kSuccess;
	IUCFPackageUtils::PackageRefPtr ref = (zipOut != nil)
		? ucf->CreatePackage(zipOut, kIdmlMime, kFalse, err) : nil;
	if (ref == nil)
	{
		line.Append("CreatePackage nil, err ");
		line.AppendNumber(static_cast<int32>(err));
		Say(out, line);
		return;
	}
	Trace("S21.1 writing the two entries");
	{
		InterfacePtr<IPMStream> c(ucf->CreateStream(ref, WideString("META-INF/container.xml"),
		                                           IUCFPackageUtils::kStandard));
		if (c != nil)
		{
			c->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(kContainer)),
			            static_cast<int32>(std::strlen(kContainer)));
			c->Close();
		}
		InterfacePtr<IPMStream> m(ucf->CreateStream(ref, WideString("designmap.xml"),
		                                           IUCFPackageUtils::kStandard));
		if (m != nil)
		{
			m->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(xml.c_str())),
			            static_cast<int32>(xml.size()));
			m->Close();
		}
	}
	Trace("S21.2 ClosePackage");
	const IUCFPackageUtils::UCFErrorCode closed = ucf->ClosePackage(ref);
	zipOut.reset(nil);
	line.Append("Close -> ");
	line.AppendNumber(static_cast<int32>(closed));

	WIN32_FILE_ATTRIBUTE_DATA fad;
	std::memset(&fad, 0, sizeof(fad));
	if (::GetFileAttributesExW(dstPath.c_str(), GetFileExInfoStandard, &fad))
	{
		line.Append(", ");
		line.AppendNumber(static_cast<int32>(fad.nFileSizeLow));
		line.Append(" B at %TEMP%\\kcm-spike-minimal.idml ⇒ LET INDESIGN OPEN IT");
	}
	else
	{
		line.Append(", ★NO FILE WAS WRITTEN");
	}
	Say(out, line);
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
				// For the eye: kcm-spike-spread.pdf is this very export, and looking at it is how
				// "the marks are there" and "the pasteboard came too" stop being byte counts.
				DropForTheEye(marked, L"kcm-spike-spread.pdf");
				// ★AND WHAT SIZE IS THE BOX? The report places this picture in a slot half its
				//   width, so a spread in the list costing two pages' width would change the
				//   shape of the whole thing. S7 weighed the bytes; this looks.
				SDKLayoutHelper helper;
				UIDRef temp = helper.CreateDocument(kSuppressUI, PMReal(600), PMReal(800), 1, 1, 0);
				if (temp != UIDRef::gNull)
				{
					{
						InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&marked, kFalse, kFalse));
						if (read != nil && provider != nil)
						{
							UIDRef imported = UIDRef::gNull;
							provider->ImportThis(temp.GetDataBase(), read, kSuppressUI, &imported);
							read->Close();
							InterfacePtr<IGeometry> geo(imported != UIDRef::gNull ? UIDRef(imported) : UIDRef::gNull, UseDefaultIID());
							if (geo != nil)
							{
								const PMRect box = geo->GetStrokeBoundingBox();
								line.Append(", box ");
								line.AppendNumber(static_cast<int32>(::ToDouble(box.Width())));
								line.Append(" x ");
								line.AppendNumber(static_cast<int32>(::ToDouble(box.Height())));
							}
							else
							{
								line.Append(", box unknown");
							}
						}
					}
					KCMMarkRehydratedClean(temp.GetDataBase());
					KCMCloseRehydrated(temp, kFalse /*now*/);
				}
			}
			Say(out, line);
		}
	}

	// ---- S8: the PAGE without the spread - does it bring the marks on its own? ---------------
	// ★★★THE COMBINATION NOBODY HAD TRIED. S4 measured the ITEMS with marks on/off (no), S7 the
	//   SPREAD with the page and the items (yes, but the box came out 1802x1002 - the pasteboard
	//   comes with the spread). What was never measured is the middle one: the PAGE and its items,
	//   which is the combination whose box is the page's own size (S6, 595x841).
	//   If the marks come with the PAGE, the report gets everything at once: a page-sized box,
	//   vector, marks included, no file. If they do not, KCMRingAdornment.cpp:497-499 says why -
	//   the drawing is written in SPREAD coordinates, and it is the spread that is looked for.
	Trace("S8 begin - the page and its items, marks off then on");
	{
		PMString line(Ascii("S8 the page and its items (no spread): "));
		UIDList list(db);
		list.Append(pageUID);
		{
			InterfacePtr<ISpread> spread;
			int32 pgPos = -1;
			if (PagePosition(db, pageUID, spread, pgPos))
				spread->GetItemsOnPage(pgPos, &list, kFalse, kFalse, kTrue);
		}
		KCMMemXferBytes onBytes;
		MarkTest(db, list, line, onBytes);
		Say(out, line);
	}

	// ---- S9: the PASTEBOARD, as items in the list ---------------------------------------------
	// ★★USER'S IDEA (2026-09-14): "a normal PDF export never shows the pasteboard - with this
	//   mechanism it looks as though the pasteboard's items COULD be put into a PDF. Remember it,
	//   I would like it as a feature one day."
	// ⚠THE FIRST ANSWER WAS WRONG. Putting the SPREAD in the list was read as carrying the
	//   pasteboard because the file grew by 4,057 bytes - and then the picture showed nothing
	//   beside the page, and the same export measured twice differed by 1,481 bytes on its own.
	//   kPDFExportItemsCmdBoss draws WHAT IS IN THE LIST; the spread brings its own adornments
	//   (the marks), not its children.
	// ⇒ SO ASK PROPERLY: GetItemsOnPage takes bIncludePasteboard (ISpread.h:129). Put those items
	//   in the list and they should be drawn like any other - which would mean a PDF with the
	//   pasteboard in it, something the application's own export cannot make.
	Trace("S9 begin - the pasteboard's items in the list");
	{
		PMString line(Ascii("S9 the pasteboard's items in the list: "));
		UIDList onPageOnly(db);
		UIDList withPasteboard(db);
		{
			InterfacePtr<ISpread> spread;
			int32 pgPos = -1;
			if (PagePosition(db, pageUID, spread, pgPos))
			{
				spread->GetItemsOnPage(pgPos, &onPageOnly, kFalse, kFalse /*no pasteboard*/, kTrue);
				spread->GetItemsOnPage(pgPos, &withPasteboard, kFalse, kTrue /*pasteboard too*/, kTrue);
			}
		}
		const int32 extra = withPasteboard.Length() - onPageOnly.Length();
		line.AppendNumber(onPageOnly.Length());
		line.Append(" on the page, ");
		line.AppendNumber(withPasteboard.Length());
		line.Append(" with the pasteboard (");
		line.AppendNumber(extra);
		line.Append(" extra), ");
		if (withPasteboard.Length() == 0)
		{
			line.Append("nothing to export");
			Say(out, line);
		}
		else
		{
			// The page goes in too, so the box is the page's - which is the shape the report
			// wants, and makes "did the pasteboard item get drawn" a question about the PICTURE
			// rather than about the size of the box.
			UIDList full(db);
			full.Append(pageUID);
			for (int32 i = 0; i < withPasteboard.Length(); ++i)
				full.Append(withPasteboard[i]);
			KCMMemXferBytes bytes2;
			PMString why;
			if (!ExportToMemory(db, full, bytes2, why))
			{
				line.Append("FAILED - ");
				line.Append(why);
			}
			else
			{
				line.AppendNumber(static_cast<int32>(bytes2.GetSize()));
				line.Append(" bytes -> %TEMP%\\kcm-spike-pasteboard.pdf (LOOK AT IT)");
				DropForTheEye(bytes2, L"kcm-spike-pasteboard.pdf");
			}
			Say(out, line);
		}
	}

	// ---- S10: THE WHOLE SPREAD - the spread shape AND everything standing on it ----------------
	// ★★USER'S QUESTION (2026-09-14): "is there no way to hand it the whole spread?"
	//   There is, and this is it. kPDFExportItemsCmdBoss draws WHAT IS IN THE LIST, so "the whole
	//   spread" is not one UID - it is the spread shape (which carries the adornments, i.e. the
	//   MARKS) plus every page item standing on it, pasteboard included. GetItemsOnPage is asked
	//   once per page of the spread with bIncludePage and bIncludePasteboard both on.
	// ⇒ If this works it is the strongest form of the route: marks, content, and the pasteboard -
	//   and the pasteboard is something the application's own PDF export never puts out.
	Trace("S10 begin - the whole spread: its shape and everything on it");
	{
		PMString line(Ascii("S10 the whole spread: "));
		UIDList list(db);
		UID spreadUID = kInvalidUID;
		int32 pages = 0;
		{
			InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
			if (pageHier != nil)
				spreadUID = pageHier->GetSpreadUID();
		}
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
		{
			line.Append("FAILED - no spread");
			Say(out, line);
		}
		else
		{
			list.Append(spreadUID);				// the shape the marks hang off
			pages = spread->GetNumPages();
			for (int32 p = 0; p < pages; ++p)
				spread->GetItemsOnPage(p, &list, kTrue /*the page shape too*/, kTrue /*pasteboard*/, kTrue);
			line.AppendNumber(pages);
			line.Append(" page(s), ");
			line.AppendNumber(list.Length());
			line.Append(" UIDs in the list, ");
			KCMMemXferBytes onBytes;
			MarkTest(db, list, line, onBytes);
			line.Append(" -> %TEMP%\\kcm-spike-whole-spread.pdf (LOOK AT IT)");
			DropForTheEye(onBytes, L"kcm-spike-whole-spread.pdf");
			Say(out, line);
		}
	}

	// ---- S11: THE ANSWER THE REPORT NEEDS - a page-sized box WITH the marks ------------------
	// ★★★Everything before this measured a trade-off: the page gives the right box and no marks
	//   (S6, S8), the spread gives the marks and the wrong box (S7, S10 - two pages wide on a
	//   facing-pages document, and the report wants one changed page). KCMDrawEventHandler::
	//   sMarksOnPage removes the trade-off by letting the adornment draw when it is handed a
	//   PAGE, in the page's own coordinates (KCMRingAdornment.cpp, KCMDrawMarksForPage).
	// ⇒ If the signal shows here, the report can be built on this and nothing else.
	Trace("S11 begin - page + items with sMarksOnPage");
	{
		PMString line(Ascii("S11 page + items with sMarksOnPage on: "));
		UIDList list(db);
		list.Append(pageUID);
		{
			InterfacePtr<ISpread> spread;
			int32 pgPos = -1;
			if (PagePosition(db, pageUID, spread, pgPos))
				spread->GetItemsOnPage(pgPos, &list, kFalse, kFalse, kTrue);
		}
		const bool16 wasOnPage = KCMDrawEventHandler::sMarksOnPage;
		KCMDrawEventHandler::sMarksOnPage = kTrue;
		KCMMemXferBytes onBytes;
		MarkTest(db, list, line, onBytes);		// takes care of sPrintMarks itself
		KCMDrawEventHandler::sMarksOnPage = wasOnPage;
		if (onBytes.GetSize() > 0)
		{
			line.Append(" -> %TEMP%\\kcm-spike-page-marks.pdf (LOOK AT IT)");
			DropForTheEye(onBytes, L"kcm-spike-page-marks.pdf");
			// And the box, because the whole point of this route is that it stays page-sized.
			SDKLayoutHelper helper;
			UIDRef temp = helper.CreateDocument(kSuppressUI, PMReal(600), PMReal(800), 1, 1, 0);
			if (temp != UIDRef::gNull)
			{
				{
					InterfacePtr<IPMStream> read(StreamUtil::CreateMemoryStreamRead(&onBytes, kFalse, kFalse));
					if (read != nil && provider != nil)
					{
						UIDRef imported = UIDRef::gNull;
						provider->ImportThis(temp.GetDataBase(), read, kSuppressUI, &imported);
						read->Close();
						if (imported != UIDRef::gNull)
						{
							InterfacePtr<IGeometry> geo(imported, UseDefaultIID());
							if (geo != nil)
							{
								const PMRect box = geo->GetStrokeBoundingBox();
								line.Append(", box ");
								line.AppendNumber(static_cast<int32>(::ToDouble(box.Width())));
								line.Append(" x ");
								line.AppendNumber(static_cast<int32>(::ToDouble(box.Height())));
							}
						}
					}
				}
				KCMMarkRehydratedClean(temp.GetDataBase());
				KCMCloseRehydrated(temp, kFalse /*now*/);
			}
		}
		Say(out, line);
	}

	// ---- S12: ★★★THE USER'S QUESTION - can this command export PAGES instead of items? -------
	// "Fundamentally, what we want is to export the DOCUMENT's PDF internally, isn't it - and you
	//  found no way to do that anywhere?" (2026-09-14). There is one place left that says
	//  otherwise, and it is on this very boss:
	//
	//    IPDFExportController::StartUp(bool16 bExportPageItems = kFalse)
	//    "If bExportPageItems is kTrue, the ItemList contains page items, and is NOT a list of
	//     pages."                                        (IPDFExportController.h:118-124)
	//
	// ⇒ kFalse means THE LIST IS PAGES. If the command can be run that way while still writing to
	//   the stream, the report gets the ORDINARY page export - flattener, transparency, stacking
	//   order, and marks at their real opacity - with no file. That would answer, in one stroke,
	//   the opacity problem AND the two questions asked of the items route ("what about stacking
	//   order, what about items that are themselves semi-transparent?").
	// ⚠THIS STEP ONLY ASKS. It calls StartUp, reads which events the controller offers, and shuts
	//   down again. It does NOT try to draw: the drawing is the command's own job, and stepping
	//   into that without knowing the shape of the session is how InDesign gets taken down.
	// ⚠⚠⚠**MEASURED 2026-09-14: THIS TAKES INDESIGN DOWN. DO NOT RUN IT AGAIN.**
	//   The trace ended on "S12.1 calling StartUp" and the crash report named
	//   KCMProbePdfRoute -> NormalizeFixedQuadDef (Adobe's own), EXCEPTION_ACCESS_VIOLATION.
	//   ⇒ **IPDFExportController::StartUp cannot be called from outside.** The controller belongs
	//     to the export command and is initialised BY it - reaching in before ProcessCommand has
	//     run hands it a session that does not exist yet. The interface is published so that a
	//     client can READ an export in progress (that is how kPDFExportSetupService's events are
	//     produced), not so that a caller can start one.
	//   ⇒ **So there is no public way to make kPDFExportCmdBoss - the PAGE exporter - write into a
	//     stream.** Its 35 interfaces carry IID_ISYSFILEDATA and no IID_IPMUNKNOWNDATA, and the
	//     controller cannot be driven by hand. The items command is the only stream door there is.
	//   The step is kept, disabled, because "we already tried that" is worth more than the four
	//   lines it costs - and because the next reader will have the same idea.
	const bool16 kS12WouldCrash = kTrue;
	Trace(kS12WouldCrash ? "S12 skipped - StartUp() crashes when called from outside (measured)" : "S12 begin");
	if (!kS12WouldCrash)
	{
		PMString line(Ascii("S12 StartUp(bExportPageItems=kFalse): "));
		KCMMemXferBytes pageModeBytes;
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kPDFExportItemsCmdBoss));
		InterfacePtr<IPMUnknownData> streamData(cmd, IID_IPMUNKNOWNDATA);
		InterfacePtr<IPDFExportController> controller(cmd, IID_IPDFEXPORTCONTROLLER);
		if (cmd == nil || controller == nil)
		{
			line.Append("FAILED - ");
			line.Append(cmd == nil ? "no command" : "no IPDFExportController on this boss");
			Say(out, line);
		}
		else
		{
			UIDList pages(db);
			pages.Append(pageUID);			// ★A LIST OF PAGES, which is what kFalse says it is
			cmd->SetItemList(pages);
			InterfacePtr<IPDFExportPrefs> appPrefs((IPDFExportPrefs*)::QuerySessionPreferences(IID_IPDFCLIPBOARDEXPORTPREFS));
			InterfacePtr<IPDFExportPrefs> prefs(cmd, IID_IPDFEXPORTPREFS);
			if (prefs != nil && appPrefs != nil)
				prefs->CopyPrefs(appPrefs);
			InterfacePtr<IBoolData> progress(cmd, IID_IUSEPROGRESSINDICATOR);
			if (progress != nil)
				progress->Set(kFalse);
			InterfacePtr<IUIFlagData> ui(cmd, IID_IUIFLAGDATA);
			if (ui != nil)
				ui->Set(kSuppressUI);
			InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&pageModeBytes, kFalse, kFalse));
			if (stream != nil && streamData != nil)
				streamData->SetPMUnknown(stream);

			Trace("S12.1 calling StartUp");
			const PDFExportErr started = controller->StartUp(kFalse);
			Trace("S12.2 StartUp came back");
			line.Append("StartUp -> ");
			line.AppendNumber(static_cast<int32>(started));
			line.Append(ExportErrName(started));

			if (started == kPDFExportErrSuccess)
			{
				line.Append(", events:");
				int32 guard = 0;
				for (; guard < 24; ++guard)
				{
					PDFExportEvent ev;
					std::memset(&ev, 0, sizeof(ev));
					Trace("S12.3 GetNextPDFExportEvent");
					const PDFExportErr got = controller->GetNextPDFExportEvent(&ev);
					if (got != kPDFExportErrSuccess)
					{
						line.Append(" [stopped: ");
						line.AppendNumber(static_cast<int32>(got));
						line.Append("]");
						break;
					}
					line.Append(" ");
					line.Append(EventName(ev.id));
					if (ev.targetPort == nil)
						line.Append("(no port)");
					if (ev.id == kPDFExportEventEndExport)
						break;
				}
				Trace("S12.4 calling ShutDown");
				const PDFExportErr ended = controller->ShutDown(kFalse);
				Trace("S12.5 ShutDown came back");
				line.Append(", ShutDown -> ");
				line.AppendNumber(static_cast<int32>(ended));
				line.Append(", ");
				line.AppendNumber(static_cast<int32>(pageModeBytes.GetSize()));
				line.Append(" bytes");
			}
			Say(out, line);
		}
	}
	if (kS12WouldCrash)
		Say(out, "S12 the page-export door: CLOSED - IPDFExportController::StartUp crashes when called from outside (measured 2026-09-14, crash report: NormalizeFixedQuadDef)");

	// ---- S13: ★★★DOES THE PICTURE COME OUT RIGHT AT ALL? ------------------------------------
	// The user asked the question that matters more than any of the above (2026-09-14): "handing
	// it the items one by one - what about the stacking order, what about items that are
	// themselves semi-transparent?" Measured on a page built for it: **the stacking order came
	// out REVERSED and the transparency was GONE.** A report whose pictures are wrong is worse
	// than a report with a temporary file, so the route cannot be used until this is understood.
	//
	// Three things the route had never been given, measured here one at a time:
	//   (a) the ordinary EXPORT preferences instead of the CLIPBOARD ones,
	//   (b) the export ANNOUNCED, so the flattener runs,
	//   (c) the list in REVERSE, in case the command draws it back to front.
	Trace("S13 begin - preferences, announcement, order");
	{
		UIDList pageAndItems(db);
		pageAndItems.Append(pageUID);
		{
			InterfacePtr<ISpread> spread;
			int32 pgPos = -1;
			if (PagePosition(db, pageUID, spread, pgPos))
				spread->GetItemsOnPage(pgPos, &pageAndItems, kFalse, kFalse, kTrue);
		}

		PMString line(Ascii("S13 "));
		line.AppendNumber(pageAndItems.Length());
		line.Append(" in the list. ");

		// (a) + (b): the export preferences, announced.
		{
			KCMMemXferBytes bytes4;
			PMString why;
			if (ExportToMemory(db, pageAndItems, bytes4, why, kTrue /*export prefs*/, kTrue /*announce*/))
			{
				line.Append("[a+b export-prefs+announced ");
				line.AppendNumber(static_cast<int32>(bytes4.GetSize()));
				line.Append(" bytes -> kcm-spike-xp-ab.pdf] ");
				DropForTheEye(bytes4, L"kcm-spike-xp-ab.pdf");
			}
			else
			{
				line.Append("[a+b FAILED: ");
				line.Append(why);
				line.Append("] ");
			}
		}

		// (c): the same, with the list reversed.
		{
			UIDList reversed(db);
			for (int32 i = pageAndItems.Length() - 1; i >= 0; --i)
				reversed.Append(pageAndItems[i]);
			KCMMemXferBytes bytes5;
			PMString why;
			if (ExportToMemory(db, reversed, bytes5, why, kTrue, kTrue))
			{
				line.Append("[c reversed ");
				line.AppendNumber(static_cast<int32>(bytes5.GetSize()));
				line.Append(" bytes -> kcm-spike-xp-rev.pdf]");
				DropForTheEye(bytes5, L"kcm-spike-xp-rev.pdf");
			}
			else
			{
				line.Append("[c FAILED: ");
				line.Append(why);
				line.Append("]");
			}
		}
		Say(out, line);
	}

	// ---- S14: ★★★THE QUESTION THAT DECIDES THE SNIPPET ROUTE --------------------------------
	// The items route cannot be used: measured 2026-09-14, it reverses the stacking order and
	// loses transparency (kPDFExportItemsCmdBoss is the CLIPBOARD exporter, and a clipboard PDF
	// is allowed to be an approximation). The snippet route carries the OBJECTS instead, so
	// stacking and transparency come with them by construction.
	//
	// ⚠**BUT THE REPORT HOLDS BOTH SIDES AT ONCE**, and the user named the hazard before any of
	//   this was written: "a style of the same name could hold different values in the two
	//   documents". Snippets carry styles BY NAME. So: does the second side's "Body" arrive as a
	//   copy, silently take the first side's values, or overwrite them?
	//
	// ★THE ANSWER IS WHAT IT LOOKS LIKE, so the document is LEFT OPEN for capture to photograph.
	Trace("S14 begin - both sides as snippets in one document");
	{
		PMString line(Ascii("S14 both sides as snippets in one document: "));
		IDataBase* const tgt = KCMArmedTargetDB();
		IDataBase* const src = KCMArmedSourceDB();
		if (tgt == nil || src == nil)
		{
			line.Append("skipped - no comparison is armed (press Start first)");
			Say(out, line);
		}
		else
		{
			SDKLayoutHelper helper;
			UIDRef temp = helper.CreateDocument(kSuppressUI, PMReal(1200), PMReal(900), 1, 1, 0);
			if (temp == UIDRef::gNull)
			{
				line.Append("FAILED - the document could not be created");
				Say(out, line);
			}
			else
			{
				Trace("S14.1 snippet the SOURCE page in");
				const int32 fromSource = SnippetOnePageInto(src, temp);
				Trace("S14.2 snippet the TARGET page in");
				const int32 fromTarget = SnippetOnePageInto(tgt, temp);
				Trace("S14.3 both in");
				line.Append("Source brought ");
				line.AppendNumber(fromSource);
				line.Append(" items, Target brought ");
				line.AppendNumber(fromTarget);
				line.Append(". Report now has ");
				DescribeSwatches(temp.GetDataBase(), line);
				// ★LEFT OPEN ON PURPOSE - the answer is visual. It is a windowless document, so it
				//   does not appear on screen, but capture draws it and app.documents lists it.
				line.Append(" ★LEFT OPEN for capture");
				Say(out, line);
			}
		}
	}

	// ---- S15: ★★★THE LAST UNMEASURED DOOR - a PATH THAT IS NOT A FILE ----------------------
	// See the apparatus at the head of this file for the whole argument. Three stages, cheapest
	// and safest first, so that a failure says WHICH layer refused:
	//   .0  plain Win32, no InDesign at all: can a writer open \\.\pipe\x the way a writer opens
	//       a file (CREATE_ALWAYS), or only the way a pipe client does (OPEN_EXISTING)?
	//   .a  InDesign's own file layer: does StreamUtil::CreateFileStreamWrite take the path, and
	//       does what it writes come out the other end?
	//   .b  the real thing: kPDFExportCmdBoss, one page, ISysFileData pointing at the pipe.
	Trace("S15 begin - a path that is not a file");
	{
		// .0 -------------------------------------------------------------------------------
		{
			// ★★★THE ARMED CONTROL. 14 bytes go in; if 14 do not come out, NOTHING BELOW MEANS
			//   ANYTHING - a zero from the stages that follow would be the instrument's zero,
			//   not the exporter's. The first run of this spike reported 0 here and the reading
			//   was published anyway; that is the mistake this line exists to make impossible.
			PMString line(Ascii("S15.0 plain Win32 on \\\\.\\pipe\\: "));
			const char* const probe = "%PDF-1.7 probe";
			const int32 probeLen = static_cast<int32>(std::strlen(probe));
			for (int32 pass = 0; pass < 2; ++pass)
			{
				const bool16 duplex = (pass == 1);
				KCMPipeCatcher catcher(duplex ? L"kcm-probe-w32-duplex" : L"kcm-probe-w32-in", duplex);
				line.Append(pass == 0 ? "" : "; ");
				if (!catcher.Listening())
				{
					line.Append("the pipe could not be created, error ");
					line.AppendNumber(static_cast<int32>(catcher.CreateError()));
					continue;
				}
				// CREATE_ALWAYS and GENERIC_READ|GENERIC_WRITE together: what an ordinary file
				// writer asks for, and what a PDF writer needs if it seeks back at the end.
				HANDLE h = ::CreateFileW(catcher.Path(),
				                         duplex ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE,
				                         0, nil, CREATE_ALWAYS, 0, nil);
				const DWORD openErr = ::GetLastError();
				line.Append(duplex ? "read+write -> " : "write-only -> ");
				if (h == INVALID_HANDLE_VALUE)
				{
					line.Append("REFUSED, error ");
					line.AppendNumber(static_cast<int32>(openErr));
					continue;
				}
				DWORD wrote = 0;
				const BOOL written = ::WriteFile(h, probe, static_cast<DWORD>(probeLen), &wrote, nil);
				const DWORD writeErr = written ? 0 : ::GetLastError();
				// ★And can it SEEK? This is the whole question for a PDF writer, asked directly -
				//   the cross-reference offset is patched into the head after the body is out.
				// ⚠SetFilePointerEx, not SetFilePointer: the old one answers with the new
				//   position, and position 0 is indistinguishable from "0 means look at
				//   GetLastError". A BOOL cannot be misread.
				LARGE_INTEGER zero;
				zero.QuadPart = 0;
				const BOOL seekOK = ::SetFilePointerEx(h, zero, nil, FILE_BEGIN);
				const DWORD seekErr = seekOK ? 0 : ::GetLastError();
				::CloseHandle(h);
				catcher.Finish();
				line.Append("opened, wrote ");
				line.AppendNumber(static_cast<int32>(wrote));
				if (writeErr != 0)
				{
					line.Append(" (write error ");
					line.AppendNumber(static_cast<int32>(writeErr));
					line.Append(")");
				}
				line.Append(", seek ");
				line.Append(seekErr == 0 ? "OK" : "REFUSED error ");
				if (seekErr != 0)
					line.AppendNumber(static_cast<int32>(seekErr));
				line.Append(", caught ");
				catcher.Describe(line);
				if (catcher.Bytes() != probeLen)
					line.Append(" ★★★THE CONTROL FAILED - every reading below is void");
			}
			Say(out, line);
		}

		// .a -------------------------------------------------------------------------------
		Trace("S15.a begin - InDesign's own file layer");
		{
			PMString line(Ascii("S15.a StreamUtil::CreateFileStreamWrite on a pipe: "));
			KCMPipeCatcher catcher(L"kcm-probe-idstream", kTrue);
			if (!catcher.Listening())
			{
				line.Append("skipped - the pipe could not be created");
			}
			else
			{
				const IDFile pipeFile = PipeAsFile(catcher.Path());
				// ★What did IDFile make of it? A path with no directory behind it may not survive
				//   the trip, and if it does not, nothing below means anything.
				PMString roundTrip = FileUtils::SysFileToPMString(pipeFile);
				roundTrip.SetTranslatable(kFalse);
				line.Append("IDFile reads back as \"");
				line.Append(roundTrip);
				line.Append("\"; ");
				Trace("S15.a1 opening the stream");
				InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(pipeFile, kOpenOut | kOpenTrunc));
				Trace("S15.a2 the stream came back");
				if (stream == nil)
				{
					line.Append("the stream is nil (InDesign would not open it)");
				}
				else
				{
					const char* probe = "%PDF-1.7 through InDesign";
					stream->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(probe)),
					                 static_cast<int32>(std::strlen(probe)));
					stream->Close();
					stream.reset(nil);
					catcher.Finish();
					line.Append("the stream opened, caught ");
					catcher.Describe(line);
				}
			}
			Say(out, line);
		}

		// .b -------------------------------------------------------------------------------
		// ⚠THE ONE THAT COULD TAKE INDESIGN DOWN. The trace lines around ProcessCommand are what
		//   tells a crash apart from a refusal, exactly as they did for S12.
		// ★THREE AIMINGS, not one. The first run measured a single inbound pipe with no
		//   extension and learned only "it opened and wrote nothing" - which has at least three
		//   explanations, and a measurement that cannot tell them apart has not finished.
		//   Each row below changes ONE thing about the target:
		Trace("S15.b begin - kPDFExportCmdBoss aimed at a pipe");
		{
			struct Aim { const wchar_t* leaf; bool16 duplex; const char* what; };
			const Aim aims[] = {
				{ L"kcm-probe-pdf-in",  kFalse, "b inbound, no extension" },
				{ L"kcm-probe-pdf-du",  kTrue,  "c duplex, no extension  " },
				{ L"kcm-probe.pdf",     kTrue,  "d duplex, named .pdf    " },
			};
			for (int32 i = 0; i < static_cast<int32>(sizeof(aims) / sizeof(aims[0])); ++i)
			{
				PMString line(Ascii("S15."));
				line.Append(Ascii(aims[i].what));
				line.Append(": ");
				KCMPipeCatcher catcher(aims[i].leaf, aims[i].duplex);
				if (!catcher.Listening())
				{
					line.Append("skipped - the pipe could not be created, error ");
					line.AppendNumber(static_cast<int32>(catcher.CreateError()));
					Say(out, line);
					continue;
				}
				Trace("S15.b aiming the export at the pipe");
				ErrorCode err = kFailure;
				ErrorCode global = kFailure;
				int32 ms = -1;
				ExportOnePageTo(db, pageUID, PipeAsFile(catcher.Path()), err, global, ms);
				catcher.Finish();
				line.Append("ProcessCommand -> ");
				line.AppendNumber(static_cast<int32>(err));
				line.Append(err == kSuccess ? " (kSuccess)" : " (failed)");
				line.Append(" in ");
				line.AppendNumber(ms);
				line.Append("ms, global ");
				line.AppendNumber(static_cast<int32>(global));
				line.Append("; pipe ");
				catcher.Describe(line);
				Say(out, line);
			}
		}

		// .e ---- ★★★THE BASELINE, and it is not optional ------------------------------------
		// Every line above says "failed". Without an export that SUCCEEDS, measured in the same
		// run with the same settings, "failed" could mean the page, the preferences, the
		// session - anything at all. This is the line that makes the three above mean
		// "the TARGET was refused", and it is also the clock the pipe's time is read against.
		Trace("S15.e begin - the same export to a real file");
		{
			PMString line(Ascii("S15.e the same export to a REAL FILE (the baseline): "));
			wchar_t dir[MAX_PATH] = { 0 };
			::GetTempPathW(MAX_PATH, dir);
			std::wstring real(dir);
			real += L"kcm-spike-baseline.pdf";
			::DeleteFileW(real.c_str());
			ErrorCode err = kFailure;
			ErrorCode global = kFailure;
			int32 ms = -1;
			ExportOnePageTo(db, pageUID, PipeAsFile(real.c_str()), err, global, ms);
			line.Append("ProcessCommand -> ");
			line.AppendNumber(static_cast<int32>(err));
			line.Append(err == kSuccess ? " (kSuccess)" : " (FAILED - then nothing above means anything)");
			line.Append(" in ");
			line.AppendNumber(ms);
			line.Append("ms, wrote ");
			{
				WIN32_FILE_ATTRIBUTE_DATA fad;
				std::memset(&fad, 0, sizeof(fad));
				if (::GetFileAttributesExW(real.c_str(), GetFileExInfoStandard, &fad))
					line.AppendNumber(static_cast<int32>(fad.nFileSizeLow));
				else
					line.Append("no");
			}
			line.Append(" bytes");
			::DeleteFileW(real.c_str());		// the baseline is a measurement, not a product
			Say(out, line);
		}

		// .f ---- is it PIPES, or is it EVERYTHING THAT IS NOT A DISK FILE? -------------------
		// NUL is the other well-known path that is not a file. It swallows everything and never
		// fails, so an export that gets as far as writing will SUCCEED here even though nothing
		// is kept. If NUL fails too, the exporter wants a real file object and the question is
		// closed; if NUL works, the refusal is about pipes in particular.
		Trace("S15.f begin - the NUL device");
		{
			PMString line(Ascii("S15.f the same export to NUL: "));
			ErrorCode err = kFailure;
			ErrorCode global = kFailure;
			int32 ms = -1;
			ExportOnePageTo(db, pageUID, PipeAsFile(L"\\\\.\\NUL"), err, global, ms);
			line.Append("ProcessCommand -> ");
			line.AppendNumber(static_cast<int32>(err));
			line.Append(err == kSuccess ? " (kSuccess)" : " (failed)");
			line.Append(" in ");
			line.AppendNumber(ms);
			line.Append("ms, global ");
			line.AppendNumber(static_cast<int32>(global));
			Say(out, line);
		}
	}

	// ---- S16: ★★★AN IDML CONTAINER BUILT IN MEMORY, AND OPENED AGAIN FROM MEMORY -----------
	// The user asked, after S15 closed the PDF door: "IDML is the whole-document export - can
	// THAT be held internally?" Two thirds of the answer were already measured:
	//   ✅ the CONTENT is already in memory      - IINXManager::ExportINX takes an IPMStream*,
	//      and KCM's Resources mode runs it every day (376-388KB, 78-157ms, nothing on disk).
	//   ⛔ the IDML EXPORT PROVIDER refuses a stream - measured 2026-09-09: IExportProvider::
	//      ExportToStream gives 0 bytes, 0ms and ErrorCode 0, a silent failure.
	// What was never measured is the third: the CONTAINER. IUCFPackageUtils carries an IDFile
	// overload and an IPMStream overload SIDE BY SIDE, in both directions:
	//      CreatePackage(const IDFile&,    mimeType, createManifest, err)
	//      CreatePackage(const IPMStream*, mimeType, createManifest, err)   ★
	//      OpenPackage  (const IDFile&,    err)
	//      OpenPackage  (const IPMStream*, err)                             ★
	// ⚠THE SDK HAS NOT ONE EXAMPLE of either. So this is a first run, and it is built as a
	//  ROUND TRIP - write a known payload in, read it back out, compare the byte count - because
	//  "CreatePackage returned something" is not evidence that a package was made.
	// ★The mimetype string is NOT a guess: it was read out of a real IDML in the SDK's own
	//   devtools (devtools/idmltools/samples/conditionaltext/ConditionalText.idml), whose first
	//   entry is "mimetype", 43 bytes, STORED not deflated.
	Trace("S16 begin - an IDML container in memory");
	{
		PMString line(Ascii("S16 IUCFPackageUtils on a stream: "));
		IdmlInMemory(db, line);
		Say(out, line);
	}

	// ---- S17: A REAL IDML, AND WHETHER ONE CAN EXIST WITHOUT A FILE -------------------------
	// The user, once S16 had shown a container could be built on a stream: "what I am interested
	// in is whether a REAL IDML can be held internally, without being written to a file." S16's
	// container held an INX and not an IDML (the two were compared byte for byte - see the block
	// at the head of RealIdmlWithoutAFile), so this step goes after the real thing. The move it
	// makes is the one S15 opened and nobody has yet played: ExportIDMLDirect takes a
	// `const IDFile*`, and an IDFile can be a pipe.
	Trace("S17 begin - a real IDML without a file");
	RealIdmlWithoutAFile(db, out);

	// ---- S18: route B, step one - repack a real IDML into a container of our own ------------
	Trace("S18 begin - repack");
	RepackIdml(db, out, kFalse);

	// ---- S20: the same repack, but one part is ours ----------------------------------------
	Trace("S20 begin - repack with our own story part");
	RepackIdml(db, out, kTrue);

	// ---- S21: everything in one designmap - is the split needed at all? ---------------------
	Trace("S21 begin - the smallest IDML");
	MinimalIdml(db, out);

	Trace("=== run ends, every step came back ===");
}

// End, KCMPdfSpike.cpp.
