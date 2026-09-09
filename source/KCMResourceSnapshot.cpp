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
#include "IDocument.h"
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

/** Forward-declared in the SDK and nowhere defined, which is why it is only ever a pointer. */
class IINXExportPolicy;

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

	// ★Without this, an UNSAVED edit does not come out at all. It is the difference between a
	//   tool that can follow a document being edited and one that can only read what was saved.
	docElement->Reset();

	IDOMElement::ElementList roots;
	roots.push_back(docElement);

	inx->BeginExportSession();
	const ErrorCode err = inx->ExportINX(roots, policy, stream, kSuppressUI);
	inx->EndExportSession();
	stream->Flush();

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
}

// End, KCMResourceSnapshot.cpp.
