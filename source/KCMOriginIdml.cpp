//========================================================================================
//
//  KCMOriginIdml.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cstring>
#include <string>

#include "KCMOriginIdml.h"
#include "KCMResourceBytes.h"

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

// End, KCMOriginIdml.cpp.
