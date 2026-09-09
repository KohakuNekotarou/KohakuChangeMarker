//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  See KCMResourceParse.h for what is compared and what is deliberately left to the other two
//  modes.
//
//  ★THE PARSER IS THE HOST'S OWN, not a hand-written one. The six steps are the only worked
//  example in the SDK (sdksamples/xmlcataloghandler/XCatHndFacade.cpp:205-240):
//      QueryServiceProviderByClassID(kXMLParserService, kXMLParserServiceBoss)
//        -> ISAXServices -> ::CreateObject2<ISAXContentHandler> -> ParseStream
//  ⚠ParseStream RETURNS TRUE ON FAILURE. The header does not say so; only the sample's comment
//    does. Reading it as an ordinary success flag inverts every error path.
//
//  ★NO ELEMENT NAMES ARE REGISTERED, and that is what makes the blacklist possible.
//  ParseStream's second argument is the DEFAULT handler, so every element reaches us whatever it
//  is called; HandlesSubElements() returning kTrue keeps their children coming too. Had we used
//  RegisterElementHandler, we would have had to name each element we wanted -- which is a
//  whitelist wearing a different hat, and would lose exactly the kinds nobody thought to list.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "IPMStream.h"
#include "ISAXAttributes.h"
#include "ISAXContentHandler.h"
#include "ISAXParserOptions.h"
#include "ISAXServices.h"
#include "ISession.h"			// GetExecutionContextSession - where the service registry lives

// General includes:
#include "CPMUnknown.h"
#include "CSAXContentHandler.h"	// the SDK's base class for a SAX handler - empty bodies for all of it
#include "StreamUtil.h"
#include "Utils.h"
#include "XMLParserID.h"		// kXMLParserService / kXMLParserServiceBoss / IID_ISAXCONTENTHANDLER

#include <stdio.h>				// TEMPORARY: the diagnostic log below

// Project includes:
#include "KCMID.h"
#include "KCMResourceParse.h"
#include "KCMResourceBytes.h"

//========================================================================================
// A STEP LOG, OFF BY DEFAULT.
//
// ⚠kKCMParseLogging IS kFalse AND MUST STAY kFalse in anything shipped: it opens, appends to
//   and closes a file on EVERY element, which is far too slow to leave running, and it writes
//   to a path that exists only on the author's machine. Turn it on for one build while chasing
//   a fault in this file, then turn it back off.
//
// ★It earns its place because a crash takes the return value with it. On 2026-09-09 the first
//   version of this file died inside ParseResources with an access violation, and the crash
//   report could say only which function - the log said which STEP, which is what turned
//   "something in here is wrong" into "the service registry came back nil". A file survives the
//   process; a report string does not. The throwaway INX probe had already written that lesson
//   down after losing three stages to a hang, and this is it being used rather than re-learned.
//========================================================================================

static const bool16 kKCMParseLogging = kFalse;

static const char* const kKCMParseLogPath =
	"C:/Users/user/Desktop/plugin_sdk_21.0.0.192/work/kcm-resource-parse-log.txt";

static void KCMParseLog(const char* text)
{
	if (!kKCMParseLogging)
		return;
	FILE* f = nil;
	if (::fopen_s(&f, kKCMParseLogPath, "a") == 0 && f != nil)
	{
		::fprintf(f, "%s\n", text);
		::fclose(f);
	}
}

static void KCMParseLogNum(const char* text, int32 n)
{
	if (!kKCMParseLogging)
		return;
	FILE* f = nil;
	if (::fopen_s(&f, kKCMParseLogPath, "a") == 0 && f != nil)
	{
		::fprintf(f, "%s %d\n", text, static_cast<int>(n));
		::fclose(f);
	}
}

//========================================================================================
// The blacklist
//========================================================================================

bool16 KCMIsExcludedResource(const PMString& elementName)
{
	// Pixel mode photographs the pages, so the spreads and master spreads are already covered.
	// Story mode diffs the placed body text, so <Story> is covered.
	// The XMP packet holds nothing this mode needs -- see the header.
	// ⚠XmlStory is NOT here on purpose: it is the backing store (unplaced XML elements, the DTD,
	//   comments), which Story mode never looks at.
	return elementName == "Spread"
		|| elementName == "MasterSpread"
		|| elementName == "Story"
		|| elementName == "MetadataPacketPreference";
}

//========================================================================================
// Where the handler puts what it reads
//========================================================================================

/** A place for the handler to write into, and a way for the caller to read it back afterwards.

    ⚠It exists because the SAX route hands a handler nothing of the sort: ParseStream takes a
    stream and a handler, and the `importer` that Register() would receive is nil here (it is
    the XML importer, and this is not an import). Both interfaces sit on
    kKCMResourceSaxHandlerBoss, so the handler finds this one by asking its own boss. */
class IKCMResourceSink : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMRESOURCESINK };

	virtual void				SetList(KCMResourceList* list) = 0;
	virtual KCMResourceList*	GetList() const = 0;
	virtual void				NoteMalformed() = 0;
	virtual int32				GetMalformedCount() const = 0;
};

class KCMResourceSink : public CPMUnknown<IKCMResourceSink>
{
public:
	KCMResourceSink(IPMUnknown* boss)
		: CPMUnknown<IKCMResourceSink>(boss), fList(nil), fMalformed(0) {}
	virtual ~KCMResourceSink() {}

	virtual void				SetList(KCMResourceList* list)	{ fList = list; fMalformed = 0; }
	virtual KCMResourceList*	GetList() const					{ return fList; }
	virtual void				NoteMalformed()					{ ++fMalformed; }
	virtual int32				GetMalformedCount() const		{ return fMalformed; }

private:
	KCMResourceList*	fList;		// not owned: it belongs to the caller of KCMParseResources
	int32				fMalformed;
};

CREATE_PMINTERFACE(KCMResourceSink, kKCMResourceSinkImpl)

//========================================================================================
// The handler
//========================================================================================

namespace
{

/** Writes "<Name attr="value" ...>" onto `body`, leaving StyleUniqueId out. */
void AppendOpenTag(PMString& body, const PMString& name, ISAXAttributes* attrs, PMString& uniqueIdOut)
{
	body.Append("<");
	body.Append(name);

	if (attrs != nil)
	{
		const int32 count = attrs->GetLength();
		for (int32 i = 0; i < count; ++i)
		{
			WideString qname;
			WideString value;
			if (!attrs->GetQName(static_cast<uint32>(i), qname))
				continue;
			if (!attrs->GetValue(static_cast<uint32>(i), value))
				continue;

			const PMString attrName(qname);
			if (attrName == "StyleUniqueId")
			{
				// Held apart rather than dropped: reissued on every edit, so it is a sieve, not
				// a difference. See KCMResourceItem::fUniqueId.
				uniqueIdOut = PMString(value);
				continue;
			}

			body.Append(" ");
			body.Append(attrName);
			body.Append("=\"");
			body.Append(PMString(value));
			body.Append("\"");
		}
	}
	body.Append(">");
}

}	// anonymous namespace

/** Reads every element and keeps the ones directly under <Document> that the blacklist lets
    through, flattening each one (attributes and descendants) back into text. */
/** ★It derives from CSAXContentHandler, which is the SDK's own base class for this: it supplies
    an empty body for every method of ISAXContentHandler and keeps the document locator, so only
    the ones that do something appear below. The first version of this file implemented all
    sixteen by hand on CPMUnknown<ISAXContentHandler>, which is not how the SDK's one worked
    example does it (xmlcataloghandler/XCatHndSAXContentHandler.cpp:65). */
class KCMResourceSaxHandler : public CSAXContentHandler
{
public:
	KCMResourceSaxHandler(IPMUnknown* boss)
		: CSAXContentHandler(boss), fSAXServices(nil), fDepth(0), fSeen(0),
		  fCollecting(kFalse), fList(nil), fSinkChecked(kFalse) {}
	virtual ~KCMResourceSaxHandler();

	// ----- the seven that do something; the rest are CSAXContentHandler's empty bodies
	virtual void	Register(ISAXServices* saxServices, IPMUnknown* importer = nil);
	virtual void	Characters(const WideString& chars);
	virtual void	StartDocument(ISAXServices* saxServices);
	virtual void	EndDocument();
	virtual void	StartElement(const WideString& uri, const WideString& localname,
								 const WideString& qname, ISAXAttributes* attrs);
	virtual void	EndElement(const WideString& uri, const WideString& localname,
							   const WideString& qname);

	/** kTrue, and that is the whole point: it is what keeps the children of an element we are
	    collecting coming to us instead of falling through to the default handler. */
	virtual bool16	HandlesSubElements() const						{ return kTrue; }

private:
	/** The list to fill, fetched once from our own boss. */
	KCMResourceList*	List();

	/** Held from Register to the destructor, AddRef'd - the sample does the same and says why:
	    the SAX services object outlives the handler, so caching the pointer is safe here even
	    though caching an interface generally is not. */
	ISAXServices*		fSAXServices;

	int32				fDepth;			// 1 = <Document>, 2 = the definitions we keep
	int32				fSeen;			// TEMPORARY (2026-09-09): elements arrived so far
	bool16				fCollecting;	// inside a kept element
	KCMResourceItem		fCurrent;
	KCMResourceList*	fList;
	bool16				fSinkChecked;
};

CREATE_PMINTERFACE(KCMResourceSaxHandler, kKCMResourceSaxHandlerImpl)

KCMResourceSaxHandler::~KCMResourceSaxHandler()
{
	if (fSAXServices != nil)
		fSAXServices->Release();
}

KCMResourceList* KCMResourceSaxHandler::List()
{
	if (!fSinkChecked)
	{
		fSinkChecked = kTrue;
		KCMParseLog("    List(): asking our own boss for the sink");
		InterfacePtr<IKCMResourceSink> sink(this, IID_IKCMRESOURCESINK);
		fList = (sink != nil) ? sink->GetList() : nil;
		KCMParseLogNum("    List(): sink found =", (sink != nil) ? 1 : 0);
		KCMParseLogNum("    List(): list found =", (fList != nil) ? 1 : 0);
	}
	return fList;
}

void KCMResourceSaxHandler::Register(ISAXServices* saxServices, IPMUnknown* /*importer*/)
{
	// ★NO ELEMENT NAMES ARE CLAIMED, on purpose. RegisterElementHandler is how a handler says
	//   "these are mine"; claiming none is what leaves us the DEFAULT handler, and the default
	//   handler is the one every element reaches. The sample claims two names because it wants a
	//   single element and its children; this mode wants everything except four, which is the
	//   opposite shape and needs the opposite registration.
	//
	// The services pointer is cached the way the sample caches it, with its reasoning: the SAX
	// services object outlives this handler, so holding it is safe here even though holding an
	// interface generally is not.
	if (saxServices != nil)
	{
		fSAXServices = saxServices;
		fSAXServices->AddRef();
	}
	KCMParseLog("  >> Register (claiming no element names: we are the default handler)");
}

void KCMResourceSaxHandler::StartDocument(ISAXServices* /*saxServices*/)
{
	KCMParseLog("  >> StartDocument");
	fDepth = 0;
	fCollecting = kFalse;
	fSinkChecked = kFalse;
	fList = nil;
	fSeen = 0;
}

void KCMResourceSaxHandler::EndDocument()
{
	KCMParseLogNum("  >> EndDocument, elements seen =", fSeen);
	if (fCollecting)
	{
		// An element was open when the document ended: the XML is malformed. Say so rather than
		// filing a half-read definition that would compare as "changed" against a whole one.
		InterfacePtr<IKCMResourceSink> sink(this, IID_IKCMRESOURCESINK);
		if (sink != nil)
			sink->NoteMalformed();
		fCollecting = kFalse;
	}
}

void KCMResourceSaxHandler::StartElement(const WideString& /*uri*/, const WideString& localname,
										 const WideString& /*qname*/, ISAXAttributes* attrs)
{
	++fDepth;
	++fSeen;
	if (fSeen <= 8 || fDepth == 2)
	{
		KCMParseLogNum("    StartElement seen =", fSeen);
		KCMParseLogNum("      depth =", fDepth);
	}

	const PMString name(localname);
	if (fSeen <= 8 || fDepth == 2)
		KCMParseLog("      name built ok");

	if (fDepth == 2)
	{
		fCollecting = !KCMIsExcludedResource(name);
		if (fCollecting)
		{
			fCurrent.fKind = name;
			fCurrent.fKey.Clear();
			fCurrent.fBody.Clear();
			fCurrent.fUniqueId.Clear();
			fCurrent.fKind.SetTranslatable(kFalse);
			fCurrent.fKey.SetTranslatable(kFalse);
			fCurrent.fBody.SetTranslatable(kFalse);
			fCurrent.fUniqueId.SetTranslatable(kFalse);

			// [B] and most of [C]: Self is the key when there is one. [A]: there is no Self,
			// because the element occurs once, so its own name identifies it.
			if (attrs != nil && attrs->HasAttribute(PMString("Self")))
				fCurrent.fKey = attrs->GetAttributeString(PMString("Self"));
			else
				fCurrent.fKey = name;

			AppendOpenTag(fCurrent.fBody, name, attrs, fCurrent.fUniqueId);
		}
		return;
	}

	if (fCollecting && fDepth > 2)
	{
		PMString ignored;
		ignored.SetTranslatable(kFalse);
		AppendOpenTag(fCurrent.fBody, name, attrs, ignored);
	}
}

void KCMResourceSaxHandler::EndElement(const WideString& /*uri*/, const WideString& localname,
									   const WideString& /*qname*/)
{
	if (fCollecting && fDepth > 2)
	{
		fCurrent.fBody.Append("</");
		fCurrent.fBody.Append(PMString(localname));
		fCurrent.fBody.Append(">");
	}
	else if (fCollecting && fDepth == 2)
	{
		fCurrent.fBody.Append("</");
		fCurrent.fBody.Append(PMString(localname));
		fCurrent.fBody.Append(">");

		KCMResourceList* const list = this->List();
		if (list != nil)
		{
			// ⚠A container throws when it cannot grow, and an exception crossing the SAX
			//   boundary would take InDesign down with it. Caught here, the parse simply ends up
			//   short - and KCMParseResources refuses a short result rather than handing back
			//   a document that would read as "these definitions were removed".
			//   The sample wraps its whole element handler the same way (processElement's
			//   try/catch), for the same reason.
			try
			{
				list->push_back(fCurrent);
			}
			catch (...)
			{
				InterfacePtr<IKCMResourceSink> sink(this, IID_IKCMRESOURCESINK);
				if (sink != nil)
					sink->NoteMalformed();
				KCMParseLog("    !! push_back threw - the list could not grow");
			}
		}
		fCollecting = kFalse;
	}

	if (fDepth > 0)
		--fDepth;
}

void KCMResourceSaxHandler::Characters(const WideString& chars)
{
	if (fCollecting)
		fCurrent.fBody.Append(PMString(chars));
}

//========================================================================================
// The entry point
//========================================================================================

bool16 KCMParseResources(const KCMResourceBytes& xml, KCMResourceList& out, PMString& whyNot)
{
	out.clear();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	if (xml.Size() == 0 || xml.Bytes() == nil)
	{
		whyNot = "there is nothing to parse";
		return kFalse;
	}

	KCMParseLogNum("=== KCMParseResources: enter, bytes =", static_cast<int32>(xml.Size()));

	// ★★★THE SERVICE REGISTRY COMES FROM THE SESSION, NOT FROM Utils<>.
	//   Measured 2026-09-09: Utils<IK2ServiceRegistry>() is nil here, and the first version of
	//   this function dereferenced it -- EXCEPTION_ACCESS_VIOLATION, with the crash report
	//   naming KCMParseResources. Utils<T>() looks its interface up on kUtilsBoss, and the
	//   registry does not live there; it lives on the session. The SDK's own worked example does
	//   it this way (xmlcataloghandler/XCatHndFacade.cpp:211), which is what should have been
	//   copied in the first place.
	//   ⚠And Utils<T>() cannot be checked after the fact: it has no nil guard, so testing the
	//     RESULT of -> is already too late (memory utils-boss-facade-access).
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		KCMParseLog("  FAILED: no service registry");
		whyNot = "the service registry is not available";
		return kFalse;
	}
	KCMParseLog("  step 1: registry ok");

	InterfacePtr<IK2ServiceProvider> xmlProvider(
		registry->QueryServiceProviderByClassID(kXMLParserService, kXMLParserServiceBoss));
	if (xmlProvider == nil)
	{
		KCMParseLog("  FAILED: no XML parser service");
		whyNot = "the XML parser service is not available";
		return kFalse;
	}
	KCMParseLog("  step 2: xmlProvider ok");

	InterfacePtr<ISAXServices> saxServices(xmlProvider, UseDefaultIID());
	if (saxServices == nil)
	{
		KCMParseLog("  FAILED: no ISAXServices");
		whyNot = "the parser service does not offer ISAXServices";
		return kFalse;
	}
	KCMParseLog("  step 3: saxServices ok");

	InterfacePtr<ISAXContentHandler> handler(
		::CreateObject2<ISAXContentHandler>(kKCMResourceSaxHandlerBoss));
	if (handler == nil)
	{
		KCMParseLog("  FAILED: could not create the handler boss");
		whyNot = "could not create the content handler";
		return kFalse;
	}
	KCMParseLog("  step 4: handler ok");

	InterfacePtr<IKCMResourceSink> sink(handler, IID_IKCMRESOURCESINK);
	if (sink == nil)
	{
		// The boss is missing its second interface: the .fr and this file disagree.
		KCMParseLog("  FAILED: the handler boss has no sink interface");
		whyNot = "the content handler has no sink";
		return kFalse;
	}
	KCMParseLog("  step 5: sink ok");
	sink->SetList(&out);

	// ★The sample calls Register itself rather than leaving it to ParseStream
	//   (XCatHndFacade.cpp:222). Ours claims no element names -- that is what keeps it the
	//   default handler -- but the call is what hands it the ISAXServices it is running under.
	handler->Register(saxServices);
	KCMParseLog("  step 5b: Register called");

	InterfacePtr<ISAXParserOptions> parserOptions(saxServices, UseDefaultIID());
	if (parserOptions != nil)
	{
		parserOptions->SetNamespacesFeature(kTrue);
		// ⚠kFALSE, where the sample passes kTrue. This runs from a script property, and a
		//   warning alert would stop the script dead with a dialog nobody asked for -- the
		//   failure mode the live-testing notes call "it stops on an alert". A malformed
		//   document is reported through the return value instead.
		parserOptions->SetShowWarningAlert(kFalse);
		KCMParseLog("  step 5c: parser options set");
	}
	else
	{
		KCMParseLog("  step 5c: no ISAXParserOptions (carrying on with the defaults)");
	}

	// The parser reads a stream, and the bytes are already in memory: read them back rather
	// than going anywhere near a file.
	KCMResourceBytes& mutableBytes = const_cast<KCMResourceBytes&>(xml);
	mutableBytes.Seek(0, kSeekFromStart);		// it is sitting at the end after the export
	InterfacePtr<IPMStream> readStream(StreamUtil::CreateMemoryStreamRead(&mutableBytes));
	if (readStream == nil)
	{
		KCMParseLog("  FAILED: no read stream");
		sink->SetList(nil);
		whyNot = "could not open the bytes for reading";
		return kFalse;
	}
	KCMParseLog("  step 6: read stream ok - about to ParseStream");

	// ⚠TRUE MEANS IT FAILED. Only the sample's comment says so; the header does not.
	const bool16 parseFailed = saxServices->ParseStream(readStream, handler);
	KCMParseLogNum("  step 7: ParseStream returned, failed =", parseFailed ? 1 : 0);

	const int32 malformed = sink->GetMalformedCount();
	sink->SetList(nil);			// the list belongs to the caller; do not keep a pointer to it

	if (parseFailed)
	{
		whyNot = "the XML did not parse";
		out.clear();
		return kFalse;
	}
	if (malformed > 0)
	{
		whyNot = "an element was left open at the end of the document";
		out.clear();
		return kFalse;
	}
	return kTrue;
}

// End, KCMResourceParse.cpp.
