//========================================================================================
//
//  KCMDocxPackage.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDFile.h"
#include "IPMStream.h"
#include "IUCFPackageUtils.h"		// OpenPackage / FileExists / OpenStream / ClosePackage
#include "Utils.h"
#include "WideString.h"

#include "KCMDocxPackage.h"

namespace
{

/** One entry of an open package, whole. kFalse when the library would not open it. */
bool16 ReadEntry(IUCFPackageUtils* ucf, IUCFPackageUtils::PackageRefPtr ref, const char* name, std::string& out)
{
	out.clear();
	InterfacePtr<IPMStream> s(ucf->OpenStream(ref, WideString(name)));
	if (s == nil)
		return kFalse;
	uchar buf[8192];
	for (;;)
	{
		const int32 n = s->XferByte(buf, static_cast<int32>(sizeof(buf)));
		if (n <= 0)
			break;
		out.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
	}
	s->Close();
	return kTrue;
}

/** The parts read when the package has them. The tag is looked for under nine names because Word
	renumbers the custom XML items it keeps, and the library has no way to list what is there. */
const char* const kOptionalParts[] =
{
	"word/footnotes.xml", "word/styles.xml",
	"customXml/item1.xml", "customXml/item2.xml", "customXml/item3.xml",
	"customXml/item4.xml", "customXml/item5.xml", "customXml/item6.xml",
	"customXml/item7.xml", "customXml/item8.xml", "customXml/item9.xml"
};

}	// anonymous namespace

bool16 KCMReadDocxParts(const IDFile& file, std::vector<KCMZipStore::Entry>& outParts, PMString& whyNot)
{
	outParts.clear();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	Utils<IUCFPackageUtils> ucf;
	if (!ucf)
	{
		whyNot = "IUCFPackageUtils is not available";
		return kFalse;
	}

	IUCFPackageUtils::UCFErrorCode err = IUCFPackageUtils::kSuccess;
	IUCFPackageUtils::PackageRefPtr ref = ucf->OpenPackage(file, err);
	if (ref == nil)
	{
		whyNot = "the file could not be opened as a package, UCFErrorCode ";
		whyNot.AppendNumber(static_cast<int32>(err));
		return kFalse;
	}

	KCMZipStore::Entry document;
	document.fName = "word/document.xml";
	const bool16 hasDocument = ReadEntry(ucf, ref, document.fName.c_str(), document.fBytes);
	if (hasDocument)
		outParts.push_back(document);

	for (size_t i = 0; i < sizeof(kOptionalParts) / sizeof(kOptionalParts[0]); ++i)
	{
		if (!ucf->FileExists(ref, WideString(kOptionalParts[i])))
			continue;
		KCMZipStore::Entry e;
		e.fName = kOptionalParts[i];
		if (ReadEntry(ucf, ref, e.fName.c_str(), e.fBytes))
			outParts.push_back(e);
	}

	// ⚠Closed whatever was read: a package left open holds the file.
	ucf->ClosePackage(ref);

	if (!hasDocument)
	{
		outParts.clear();
		whyNot = "the package has no word/document.xml: not a Word document";
		return kFalse;
	}
	return kTrue;
}

// End, KCMDocxPackage.cpp.
