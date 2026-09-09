//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  See KCMResourceSnapshot.h for the three things about ExportINX that had to be measured.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// GetRootUID - how KCMReadResourceList reaches a database's document
#include "IDocument.h"
#include "IDocumentList.h"		// FindDocByDataBase - is this document one the session has?
#include "IDOMElement.h"
#include "IINXManager.h"
#include "IPMStream.h"
#include "ISession.h"

// General includes:
#include "CmdUtils.h"			// kSuppressUI
#include "INXCoreID.h"			// IID_IINXEXPORTPOLICY
#include "AppFrameworkID.h"		// kActionExportPolicyBoss - the policy that yields the document
#include "PersistUtils.h"		// ::CreateObject
#include "StreamUtil.h"			// CreateMemoryStreamWrite

#include <windows.h>			// ::GetTickCount - how long an export took

// Project includes:
#include "KCMCore.h"			// KCMActiveDoc
#include "KCMResourceSnapshot.h"
#include "KCMResourceBytes.h"
#include "KCMResourceParse.h"	// what the snapshot is for: the definitions inside it

/** Forward-declared in the SDK and nowhere defined, which is why it is only ever a pointer. */
class IINXExportPolicy;

//========================================================================================
// A STEP LOG, OFF BY DEFAULT.
//
// ⚠kKCMSnapshotLogging MUST STAY kFalse in anything shipped: it opens, appends to and closes a
//   file at every step, and writes to a path that exists only on the author's machine.
//
// ★It earned its place on 2026-09-09 and is kept for the same reason KCMResourceParse.cpp keeps
//   one: A CRASH TAKES THE RETURN VALUE WITH IT. Handed a cloned database, ExportINX killed
//   InDesign outright, and the only thing the caller could report was that its socket closed.
//   Turning this on turned "something in here kills it" into "the last line is `about to call
//   ExportINX`, and Reset() came back fine" - which is what ruled Reset() out and produced the
//   guard below. A file survives the process; a string being returned does not.
//========================================================================================
static const bool16 kKCMSnapshotLogging = kFalse;

static void KCMSnapshotLog(const char* text)
{
	if (!kKCMSnapshotLogging)
		return;
	FILE* f = nil;
	if (::fopen_s(&f, "C:/Users/user/Desktop/plugin_sdk_21.0.0.192/work/kcm-resource-diff-log.txt", "a") == 0 && f != nil)
	{
		::fprintf(f, "%s\n", text);
		::fclose(f);
	}
}

bool16 KCMTakeResourceSnapshot(IDocument* doc, KCMResourceBytes& out, PMString& whyNot)
{
	out.Reset();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	if (doc == nil)
	{
		whyNot = "no document";
		return kFalse;
	}

	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IINXManager> inx(session != nil ? session->QueryINXManager() : nil);
	if (inx == nil)
	{
		whyNot = "no INX manager";
		return kFalse;
	}

	// ★★★THE DOCUMENT HAS TO BE ONE THE SESSION KNOWS ABOUT, and this test is the only thing
	//   standing between a caller and a dead InDesign.
	//
	//   Measured twice on 2026-09-09: given KIDMCP's task-start copy -- a database made by
	//   IDataBase::Clone, which no window and no app.documents entry points at -- ExportINX does
	//   not fail and does not return nil. THE PROCESS IS GONE. And everything leading up to it
	//   succeeds: the IDocument is there, the IDOMElement is there, Reset() returns normally, and
	//   the step log's last line is "about to call ExportINX". ⇒ THERE IS NOTHING TO NIL-CHECK.
	//   The only defence is to refuse before starting.
	//
	//   ★Why a clone is different at all, as far as the evidence goes: a snippet export is handed
	//   UIDs and walks the DATABASE, which a clone is; INX is handed IDOMElements and goes through
	//   the SCRIPTING DOM, which assumes a document the session has. KIDMCP compares snippets
	//   against this same clone perfectly happily. ⚠That explanation is a hypothesis - what was
	//   measured is the crash and where it happens, not the reason.
	//
	//   ⚠The test asks "is it open in this session", not "is it KCM's lent Source": the crash
	//   belongs to the document's standing, not to who lent it, and a test written in KCM's own
	//   vocabulary would not protect the next caller. A document opened WITHOUT A WINDOW is in
	//   the list and passes, which is the intended answer - that route is untested but not
	//   excluded by this guard.
	InterfacePtr<IDocumentList> docList(session->QueryDocumentList());
	if (docList == nil)
	{
		whyNot = "no document list";
		return kFalse;
	}
	if (docList->FindDocByDataBase(::GetDataBase(doc)) == nil)
	{
		whyNot = "the document is not open in this session - a cloned database cannot be exported";
		return kFalse;
	}

	InterfacePtr<IDOMElement> docElement(doc, UseDefaultIID());
	if (docElement == nil)
	{
		whyNot = "the document has no IDOMElement";
		return kFalse;
	}

	// ★The policy. nil is not "the default" here -- it is a crash. See the header.
	//   The C cast is the product's own idiom for a forward-declared interface.
	InterfacePtr<IPMUnknown> holder(
		(IPMUnknown*)::CreateObject(kActionExportPolicyBoss, IID_IINXEXPORTPOLICY));
	if (holder == nil)
	{
		whyNot = "could not create the export policy";
		return kFalse;
	}
	IINXExportPolicy* const policy = (IINXExportPolicy*)holder.get();

	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&out));
	if (stream == nil)
	{
		whyNot = "could not create the memory stream";
		return kFalse;
	}
	if (stream->GetStreamState() != kStreamStateGood)
		stream->Open();
	stream->SetEndOfStream();
	KCMSnapshotLog("    snap: manager, IDOMElement, policy and stream are all there");

	// ★Without this, an UNSAVED edit does not come out at all. It is the difference between a
	//   tool that can follow a document being edited and one that can only read what was saved.
	KCMSnapshotLog("    snap: about to call docElement->Reset()");
	docElement->Reset();
	KCMSnapshotLog("    snap: Reset() came back");

	IDOMElement::ElementList roots;
	roots.push_back(docElement);

	KCMSnapshotLog("    snap: about to call BeginExportSession");
	inx->BeginExportSession();
	KCMSnapshotLog("    snap: about to call ExportINX");
	const ErrorCode err = inx->ExportINX(roots, policy, stream, kSuppressUI);
	KCMSnapshotLog("    snap: ExportINX came back");
	inx->EndExportSession();
	stream->Flush();
	KCMSnapshotLog("    snap: EndExportSession and Flush came back");

	if (err != kSuccess)
	{
		whyNot = "ExportINX failed";
		return kFalse;
	}
	if (!out.IsWhole())
	{
		// An allocation failed part way. The bytes that ARE there would parse and would look like
		// a smaller document, so this has to be refused rather than returned.
		whyNot = "ran out of memory while collecting the export";
		return kFalse;
	}
	if (out.Size() == 0)
	{
		whyNot = "ExportINX produced no bytes";
		return kFalse;
	}
	return kTrue;
}

void KCMDescribeResourceSnapshot(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	// KCMActiveDoc resolves through IActiveContext::GetContextDocument, which is the only one of
	// the three "front document" routes that means what it says (GetNthDoc(0) has nothing to do
	// with the front, and GetFrontDocument follows the layout view).
	IDocument* const doc = KCMActiveDoc();

	KCMResourceBytes bytes;
	PMString whyNot;

	const uint32 began = ::GetTickCount();
	const bool16 ok = KCMTakeResourceSnapshot(doc, bytes, whyNot);
	const uint32 took = ::GetTickCount() - began;

	if (!ok)
	{
		out = "FAILED: ";
		out.Append(whyNot);
		return;
	}

	out.AppendNumber(static_cast<int32>(bytes.Size()));
	out.Append(" bytes, ");
	out.AppendNumber(static_cast<int32>(took));
	out.Append(" ms");

	// ----- and what the parse made of it (Task 2)
	KCMResourceList items;
	PMString parseWhy;
	const uint32 parseBegan = ::GetTickCount();
	const bool16 parsed = KCMParseResources(bytes, items, parseWhy);
	const uint32 parseTook = ::GetTickCount() - parseBegan;

	if (!parsed)
	{
		out.Append("; PARSE FAILED: ");
		out.Append(parseWhy);
		return;
	}

	// How many KINDS, not just how many items: the kinds are what the design is stated in (83
	// kept, 4 excluded), so it is the number that says whether the blacklist did what it says.
	K2Vector<PMString> kinds;
	for (size_t i = 0; i < items.size(); ++i)
	{
		bool16 seen = kFalse;
		for (size_t k = 0; k < kinds.size(); ++k)
		{
			if (kinds[k] == items[i].fKind)
			{
				seen = kTrue;
				break;
			}
		}
		if (!seen)
			kinds.push_back(items[i].fKind);
	}

	// ★HOW MUCH OF THE EXPORT IS ACTUALLY KEPT. The XML is the whole document; what this mode
	//   holds is the bodies, with the spreads, the stories and the XMP packet already dropped.
	//   The number decides a real question -- whether a kept snapshot needs compressing at all
	//   (IUCFPackageUtils can deflate into an IPMStream, so it could be) or whether the parsed
	//   form is small enough to keep as it is, which is both simpler and faster to compare.
	//   ⚠Characters, not bytes: near enough for a decision, and it costs no conversion.
	int32 bodyChars = 0;
	for (int32 i = 0; i < static_cast<int32>(items.size()); ++i)
		bodyChars += static_cast<int32>(items[i].fBody.CharCount());

	out.Append("; ");
	out.AppendNumber(static_cast<int32>(kinds.size()));
	out.Append(" kinds, ");
	out.AppendNumber(static_cast<int32>(items.size()));
	out.Append(" items, ");
	out.AppendNumber(static_cast<int32>(parseTook));
	out.Append(" ms to parse; kept ");
	out.AppendNumber(bodyChars);
	out.Append(" chars of body");

	// ⚠COMPRESSION WAS MEASURED HERE AND THEN TAKEN OUT (2026-09-09). Deflating a snapshot works
	//   -- 184KB to 30KB, 376KB to 157KB, byte-identical on the way back, under 16ms -- but KCM
	//   has no use for it: it takes two snapshots, compares them and drops them in the same call,
	//   so compressing would only add work. **The use is KIDMCP's**, which keeps the state a task
	//   began from for the whole session. The working code, the measurements and the traps are in
	//   docs/ai-notes/kcm-gzip-in-memory-2026-09-09.md, to be copied when that is built.
	//   ★The compressor is ISVGUtils::CreateZipStream - the name says SVG and means gzip.
}

//----------------------------------------------------------------------------------------
// KCMReadResourceList - one database's definitions, snapshot and parse in one call
//----------------------------------------------------------------------------------------

bool16 KCMReadResourceList(IDataBase* db, const char* which, KCMResourceList& out, PMString& whyNot)
{
	// ★EVERY FAILURE NAMES THE SIDE, and it is done here rather than by the caller so that a caller
	//   with two sides cannot get the two sentences out of step. See the header.
	if (db == nil)
	{
		whyNot = which;
		whyNot.Append(": no database");
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc == nil)
	{
		whyNot = which;
		whyNot.Append(": the database has no document");
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	KCMResourceBytes xml;
	PMString why;
	if (!KCMTakeResourceSnapshot(doc.get(), xml, why))
	{
		whyNot = which;
		whyNot.Append(": ");
		whyNot.Append(why);
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}
	if (!KCMParseResources(xml, out, why))
	{
		whyNot = which;
		whyNot.Append(": ");
		whyNot.Append(why);
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	return kTrue;
}

// End, KCMResourceSnapshot.cpp.
