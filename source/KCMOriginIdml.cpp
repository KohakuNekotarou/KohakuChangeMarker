//========================================================================================
//
//  KCMOriginIdml.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cstring>
#include <string>

#include "IDFile.h"
#include "IPMStream.h"
#include "IUCFPackageUtils.h"		// the IDML container - CreatePackage / CreateStream / ClosePackage
#include "StreamUtil.h"
#include "Utils.h"
#include "WideString.h"

#include "KCMCore.h"				// KCMActiveDoc - the document in front (KCMSaveActiveDocXml)
#include "KCMOrigin.h"				// KCMOriginBytes - the held snapshot
#include "KCMOriginIdml.h"
#include "KCMResourceBytes.h"
#include "KCMResourceSnapshot.h"	// KCMTakeResourceSnapshot - ExportINX into memory

namespace
{

/** The processing instruction says what kind of file this is. An INX is an "action"; the
    designmap of an IDML is a "document". */
const char* const kPiFrom = "type=\"action\"";
const char* const kPiTo   = "type=\"document\"";

/** The element the namespace goes on, and the declaration itself - read out of a real IDML on
    2026-09-14, byte for byte, and not composed here. */
const char* const kDocOpen = "<Document ";
const char* const kIdPkgNs = "xmlns:idPkg=\"http://ns.adobe.com/AdobeInDesign/idml/1.0/packaging\" ";

}	// namespace

bool16 KCMInxToDesignmap(KCMResourceBytes& bytes, PMString& whyNot)
{
	if (bytes.Bytes() == nil || bytes.Size() == 0)
	{
		whyNot = PMString("the snapshot is empty");
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}
	// ⚠The whole snapshot is copied here, once. It is ~180KB and this runs once per Task Start,
	//   which the user presses by hand - the alternative, editing in place, would mean moving the
	//   tail of the buffer for the insertion anyway.
	std::string xml(bytes.Bytes(), bytes.Size());

	const size_t atPi = xml.find(kPiFrom);
	if (atPi == std::string::npos)
	{
		// ★NOT AN ERROR FOR THE CALLER TO PANIC ABOUT: the snapshot is still a valid INX and every
		//   reader of it keys off element names. Say so, change nothing, and let Task Start stand.
		whyNot = PMString("the processing instruction does not say type=\"action\"");
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}
	const size_t atDoc = xml.find(kDocOpen);
	if (atDoc == std::string::npos)
	{
		whyNot = PMString("there is no <Document> element to put the namespace on");
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	// ★★★BOTH ANCHORS FOUND BEFORE EITHER EDIT IS MADE. Rewriting the PI and then failing to find
	//   <Document> would leave a file that is neither an INX nor a designmap - and the caller,
	//   seeing kFalse, would keep it believing it was untouched.
	xml.replace(atPi, std::strlen(kPiFrom), kPiTo);

	// ⚠Found again AFTER the replacement: "type=\"action\"" and "type=\"document\"" are different
	//   lengths, so the earlier offset is stale by three bytes if <Document> came after the PI -
	//   which it always does, and which is exactly why this is not reused.
	const size_t atDocNow = xml.find(kDocOpen);
	if (atDocNow == std::string::npos)
	{
		whyNot = PMString("the <Document> element went missing between the two edits");
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}
	xml.insert(atDocNow + std::strlen(kDocOpen), kIdPkgNs);

	bytes.Reset();
	bytes.Write(const_cast<char*>(xml.c_str()), static_cast<uint32>(xml.size()));
	if (!bytes.IsWhole())
	{
		// ⚠An allocation failed part way, so what is in there now is SHORT. There is no way back
		//   to the original from here, so the caller must treat this as a failed Task Start.
		whyNot = PMString("out of memory while writing the designmap back");
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}
	return kTrue;
}

//========================================================================================
//  The container.
//========================================================================================

namespace
{

/** ★Copied out of a real IDML on 2026-09-14, byte for byte. Not composed here: the tabs and the
    empty <rootfile> element are what InDesign's own writer puts there, and a package that differs
    from it is a package nobody has opened. */
const char* const kContainerXml =
	"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
	"<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
	"\t<rootfiles>\n"
	"\t\t<rootfile full-path=\"designmap.xml\" media-type=\"text/xml\">\n"
	"\t\t</rootfile>\n"
	"\t</rootfiles>\n"
	"</container>\n";

/** ★Read out of the SDK's own devtools/idmltools sample package, not guessed at. */
const char* const kIdmlMimeType = "application/vnd.adobe.indesign-idml-package";

/** Write one entry. kFalse when the entry could not be created or the bytes did not all go. */
bool16 WriteEntry(IUCFPackageUtils::PackageRefPtr ref, const char* name,
                  const char* data, int32 len)
{
	Utils<IUCFPackageUtils> ucf;
	if (!ucf || data == nil || len <= 0)
		return kFalse;
	InterfacePtr<IPMStream> entry(ucf->CreateStream(ref, WideString(name),
	                                               IUCFPackageUtils::kStandard));
	if (entry == nil)
		return kFalse;
	entry->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(data)), len);
	const bool16 failed = (entry->GetStreamState() == kStreamStateFailure) ? kTrue : kFalse;
	entry->Close();
	return failed ? kFalse : kTrue;
}

}	// namespace

int32 KCMOriginSaveIdml(const IDFile& file, PMString& whyNot)
{
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	const KCMResourceBytes* const held = KCMOriginBytes();
	if (held == nil || held->Bytes() == nil || held->Size() == 0)
	{
		whyNot = "no Task Start origin is held";
		return 1;
	}
	Utils<IUCFPackageUtils> ucf;
	if (!ucf)
	{
		whyNot = "IUCFPackageUtils is not available";
		return 2;
	}

	// ⚠★★★THE STREAM HAS TO BE A **LAZY** FILE STREAM. CreatePackage was handed eleven different
	//   kinds on 2026-09-14 and accepted exactly one: a file stream that knows its path and has
	//   NOT been opened yet. A memory stream, UCF's own kUCFWriteStreamBoss, an eager file stream,
	//   an eager one closed first - all of them came back nil with UCFErrorCode 1.
	InterfacePtr<IPMStream> zipOut(StreamUtil::CreateFileStreamWriteLazy(file, kOpenOut | kOpenTrunc));
	if (zipOut == nil)
	{
		whyNot = "the file could not be created";
		return 2;
	}

	IUCFPackageUtils::UCFErrorCode err = IUCFPackageUtils::kSuccess;
	IUCFPackageUtils::PackageRefPtr ref =
		ucf->CreatePackage(zipOut, AString(kIdmlMimeType), kFalse /*no manifest*/, err);
	if (ref == nil)
	{
		whyNot = "the IDML container could not be started, UCFErrorCode ";
		whyNot.AppendNumber(static_cast<int32>(err));
		return 2;
	}

	const bool16 wroteContainer = WriteEntry(ref, "META-INF/container.xml", kContainerXml,
	                                         static_cast<int32>(std::strlen(kContainerXml)));
	const bool16 wroteMap = WriteEntry(ref, "designmap.xml", held->Bytes(),
	                                   static_cast<int32>(held->Size()));
	// ⚠ClosePackage is what writes the zip's index, so it runs even when an entry failed - a
	//   package left unclosed would leave a half-written file behind with nothing to say so.
	const IUCFPackageUtils::UCFErrorCode closed = ucf->ClosePackage(ref);
	zipOut.reset(nil);					// flush before anybody reads the file back

	if (!wroteContainer || !wroteMap)
	{
		whyNot = wroteContainer ? "the designmap entry could not be written"
		                       : "the container.xml entry could not be written";
		return 3;
	}
	if (closed != IUCFPackageUtils::kSuccess)
	{
		whyNot = "the IDML container could not be closed, UCFErrorCode ";
		whyNot.AppendNumber(static_cast<int32>(closed));
		return 3;
	}
	return 0;
}

int32 KCMSaveActiveDocXml(const IDFile& file, PMString& whyNot)
{
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	// 1. the same photograph the Resources mode takes, of the document in front. KCMActiveDoc
	//    resolves through IActiveContext::GetContextDocument - the only one of the three "front
	//    document" routes that means what it says. ⚠A nil document is KCMTakeResourceSnapshot's to
	//    refuse, with a reason, so it is not tested twice here.
	KCMResourceBytes bytes;
	PMString why;
	if (!KCMTakeResourceSnapshot(KCMActiveDoc(), bytes, why))
	{
		whyNot = why;
		return 1;
	}

	// 2. ...labelled as a designmap, exactly as Task Start labels the origin (KCMInxToDesignmap).
	//    ⚠A failure here leaves the bytes UNCHANGED and they are still a perfectly good INX - but
	//     the two sides would then not be the same kind of thing, which is the whole point of
	//     writing this one, so it is reported rather than shrugged off.
	PMString labelWhy;
	if (!KCMInxToDesignmap(bytes, labelWhy))
	{
		whyNot = "the snapshot could not be labelled as a designmap: ";
		whyNot.Append(labelWhy);
		return 1;
	}

	// 3. write, Flush, THEN read the state - XferByte may only reach the buffer, so a failed write
	//    can surface at the Flush (the same three steps, and the same reason, as KCMOriginSaveRaw).
	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(file, kOpenOut | kOpenTrunc, 'TEXT', 'CWIE'));
	if (stream == nil)
	{
		whyNot = "the file could not be created";
		return 2;
	}
	stream->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(bytes.Bytes())), static_cast<int32>(bytes.Size()));
	stream->Flush();
	const bool16 failed = (stream->GetStreamState() == kStreamStateFailure) ? kTrue : kFalse;
	stream->Close();
	if (failed)
	{
		whyNot = "the file could not be written";
		return 3;
	}
	return 0;
}

// End, KCMOriginIdml.cpp.
