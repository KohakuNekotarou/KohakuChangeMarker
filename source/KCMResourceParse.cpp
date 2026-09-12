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
// What a UID looks like
//========================================================================================

bool16 KCMIsOpaqueSelf(const PMString& value)
{
	// InDesign writes a UID as 'u' followed by lower-case hexadecimal: "ueb", "uf4", "u13f".
	// Every name this mode meets carries something a UID cannot -- a '/' ("Color/Black"), a
	// capital letter ("dABullet0"), a space, or a character outside ASCII (a font name).
	//
	// ★★★AND A THIRD SHAPE EXISTS, which the sentence above read as a name for as long as this
	//   function has been here: A UID WITH A SUFFIX. "u10aGradientStop0", "u18ColorGroupSwatch0",
	//   "ua8BuildingBlock0" -- the owner's UID, the element's name, and an index within that
	//   owner. It carries capital letters, so the test called it a name, and KCMResourceKeyOf
	//   then used it VERBATIM as the pairing key (its route [B]).
	//   ⚠A verbatim key built out of a UID can never match in another document, so every element
	//     shaped like this came back as Added AND Removed at the same time -- the exact outcome
	//     route [C] exists to prevent. Measured 2026-09-10 on a pair whose only difference was
	//     one added gradient: "Added GradientStop x2, Removed GradientStop x2".
	//   ★WHY IT STAYED HIDDEN FOR SO LONG: two documents built by the SAME steps allocate the
	//     SAME UIDs, so the verbatim keys happened to match and the control run read
	//     "232 paired, 0 added, 0 removed". It appears the moment one side is edited -- which is
	//     every real comparison. One nearly empty document already holds 39 of them
	//     (BuildingBlock x27, ColorGroupSwatch x10, GradientStop x2), and a real one was seen on
	//     2026-09-09: "Added ColorGroupSwatch u18ColorGroupSwatchb".
	//   ⚠THE SUFFIX IS NOT A KEY EITHER: the index restarts inside each owner, so
	//     "GradientStop0" occurs once per gradient. Answering "opaque" hands the item to route
	//     [C], which pairs the nth of a kind with the nth of that kind -- the same assumption [C]
	//     already makes for a Section, and the same trade it already accepts there ("a wrong
	//     pairing shows one difference, no pairing shows two").
	const int32 length = static_cast<int32>(value.CharCount());
	if (length < 2)
		return kFalse;			// "" is not a UID, and neither is a single letter

	if (value.GetWChar(0).GetValue() != 'u')
		return kFalse;

	int32 i = 1;
	for (; i < length; ++i)
	{
		const uint32 c = value.GetWChar(i).GetValue();
		const bool16 isHexDigit = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
		if (!isHexDigit)
			break;
	}

	if (i == length)
		return kTrue;			// nothing but hexadecimal after the 'u' -- a bare UID

	// Something follows the hexadecimal, so this is the suffixed shape or it is a name. It is the
	// suffixed shape only if a UID actually came first (at least one hexadecimal digit) and the
	// suffix begins with a capital, which is how InDesign spells the element name it appends.
	// ⚠A name that merely begins with 'u' does not reach this test: in "uName" the 'N' is not
	//   preceded by a hexadecimal digit, so i is still 1 and the answer below is no.
	if (i == 1)
		return kFalse;

	const uint32 firstOfSuffix = value.GetWChar(i).GetValue();
	if (firstOfSuffix < 'A' || firstOfSuffix > 'Z')
		return kFalse;

	// The rest of a generated suffix is ASCII letters and digits and nothing else. Requiring that
	// keeps the answer to the shape InDesign really writes, rather than to "anything after a
	// capital" -- which would start swallowing values a person typed.
	for (++i; i < length; ++i)
	{
		const uint32 c = value.GetWChar(i).GetValue();
		const bool16 isAlnum = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
								(c >= 'A' && c <= 'Z'));
		if (!isAlnum)
			return kFalse;
	}
	return kTrue;
}

bool16 KCMIsOpaqueReference(const PMString& value)
{
	// One UID, or a whitespace-separated list of nothing but UIDs. ★The list form is not a
	// refinement: measured 2026-09-09, the document element's StoryList reads "u100 u119 ud0",
	// and with only the single-UID test it counted as a name and made <Document> differ between
	// every pair of documents there could ever be.
	const int32 length = static_cast<int32>(value.CharCount());
	if (length == 0)
		return kFalse;

	PMString piece;
	piece.SetTranslatable(kFalse);
	bool16 sawOne = kFalse;

	for (int32 i = 0; i <= length; ++i)
	{
		const uint32 c = (i < length) ? value.GetWChar(i).GetValue() : 0x20;
		if (c == 0x20 || c == 0x09)
		{
			if (!piece.IsEmpty())
			{
				if (!KCMIsOpaqueSelf(piece))
					return kFalse;		// something in the list is not a UID
				sawOne = kTrue;
				piece.Clear();
			}
			continue;
		}
		piece.AppendW(UTF32TextChar(c));
	}
	return sawOne;
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
	//
	// ★★TextDefault, added 2026-09-09 (the user's call: "it doesn't look like something you can
	//   change from an InDesign panel - hide it"). It is the document's default text formatting,
	//   and what it produced in practice was noise ABOUT OTHER CHANGES rather than a change of its
	//   own: it names the default paragraph style by name, so **renaming a style makes this element
	//   differ too** and the reader is shown a second row for one edit. Its own body is also the
	//   largest in the document, which made it the row whose flattened attributes filled the list.
	//   ⚠What is lost with it: an edit made ONLY to the text defaults, and nothing else, is no
	//     longer reported. That is the cost the user accepted for the noise.
	//
	// ★★★Language, added 2026-09-09 (the user's call, on the measurement below). **It does not
	//   describe the document; it describes how the document was OPENED.** A document still open
	//   from the session that CREATED it holds all of InDesign's languages in memory; the same file
	//   reopened from disk holds only the ones it uses. Measured on one pair, whose only real
	//   difference was one paragraph style: the side made and never closed answered 66 languages,
	//   the side reopened answered 1, and the comparison reported **65 added**. Closing that side
	//   and reopening it - the same bytes on disk, nothing edited - brought it to 1 and the
	//   comparison to **0 added**. Every one of those 65 rows was an artefact of the open route.
	//   ⚠What is lost with it: a change to a language definition itself (a user dictionary, a
	//     hyphenation exception - if those even ride in this element) is no longer reported.
	//     Not measured either way.
	//   ★★**AND IT IS THE ONLY KIND THAT DOES THIS** - measured, not assumed. Two pairs were built
	//     whose members were saved one after the other with NO edit in between (so every row that
	//     comes out is an artefact by construction), one member left open from the session that
	//     made it and the other reopened from disk. The second pair carried 22 kinds of definition
	//     on purpose - styles of five kinds, a style group, colour, gradient, tint, layer,
	//     condition, XML tag, numbering list, cross-reference format, TOC style, master spread,
	//     section, text variable, hyperlink destination, table, footnote. **Both pairs came back
	//     with `added 65 Language, removed 0, changed 0` and nothing else.**
	//   ⚠**THE CAUSE IS STILL NOT Language, and this line does not fix the cause.** ExportINX
	//     photographs the document AS IT STANDS IN MEMORY, so any element InDesign fills in lazily
	//     could differ for the same reason; what was measured is that no OTHER kind does today, on
	//     documents built this way. If a phantom Added ever appears under another name, the fault
	//     is this same one wearing a different coat - do not read this line as "handled".
	//
	// ⚠★★★Spread AND MasterSpread ARE NO LONGER HERE (2026-09-10, the user's request: "I want to
	//   see a page item's lock change too"). They are not compared as themselves either - what
	//   happens to them now is a FILTERED DESCENT rather than a skip, and the rule is
	//   KCMIsSpreadContainer plus KCMPageItemAttributeWanted. Everything a page renders is still
	//   Pixel's, and only what a page CANNOT render is taken.
	return elementName == "Story"
		|| elementName == "TextDefault"
		|| elementName == "Language"
		|| elementName == "MetadataPacketPreference";
}

//========================================================================================
// Inside a spread: the filtered descent
//========================================================================================

bool16 KCMIsSpreadContainer(const PMString& elementName)
{
	// The two subtrees that hold page items. They are not compared as items themselves: a spread
	// carries its own geometry and its page list, all of which Pixel photographs.
	return elementName == "Spread" || elementName == "MasterSpread";
}

bool16 KCMIsSpreadStructureElement(const PMString& elementName)
{
	// Elements inside a spread that carry a Self but are NOT page items. <Page> is the one that
	// matters: it has both a Self and a Name (the folio), so without this test every page would
	// become an item keyed "Page#1" - and page numbering already has an owner, <Section>.
	return KCMIsSpreadContainer(elementName)
		|| elementName == "Page"
		|| elementName == "FlattenerPreference";
}

bool16 KCMIsUnnamedPageItemName(const PMString& nameValue)
{
	// ★★★AN UNNAMED PAGE ITEM DOES NOT HAVE AN EMPTY Name - IT HAS THIS. InDesign writes the
	//   string-table placeholder "$ID/" for an object nobody has named, so a test for "the name
	//   is empty" lets every unnamed object through. Measured 2026-09-10, on the counter-test
	//   written to catch exactly this: a plain rectangle added to one side came back as
	//   "Added Rectangle#$ID/" - one row for an object nobody can pair, which is the noise this
	//   filter exists to prevent, arriving through the filter itself.
	// ⚠It is an EXACT match on purpose. "$ID/" alone means untitled; "$ID/something" is a real
	//   reference into the string table and is a name.
	return nameValue == "$ID/";
}

bool16 KCMPageItemAttributeWanted(const PMString& attributeName)
{
	// ★★★THE WHOLE POINT OF THE FILTER. A page item has upwards of forty attributes and nearly
	//   all of them are geometry, colour and applied styles -- things a rendered page SHOWS, so
	//   Pixel already owns them. Taking them here would put a second row against every object
	//   somebody moved, which is exactly the noise TextDefault was blacklisted for.
	//   These two are what a page cannot render: they are true of the object without being
	//   visible in it. The third, the script label, is not an attribute at all and is collected
	//   from the <Label> subtree instead.
	// ⚠**Nonprinting is deliberately NOT here**: switching it changes what the plate carries, so
	//   the pixels move and Pixel reports it. Adding it would be the double report again.
	return attributeName == "Name" || attributeName == "Locked";
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

			// ★THE DOCUMENT'S OWN FILE NAME IS NOT A DEFINITION. The two documents being
			//   compared are different files by definition, so this attribute differs every
			//   single time and would put a permanent row in every result. It is the ONE
			//   attribute named here rather than tested by shape, because "a name that is
			//   always different" has no shape - and the panel already shows both file names
			//   on its Target and Source lines, so nothing is lost by leaving it out.
			if (name == "Document" && attrName == "Name")
				continue;

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

			// ★A UID IS NOT CONTENT: it is written down as "(uid)" so that the attribute's
			//   PRESENCE is still compared while its value - which two documents can never
			//   share - is not. See KCMIsOpaqueSelf for what this cost before it was here.
			const PMString attrValue(value);
			if (KCMIsOpaqueReference(attrValue))
				body.Append("(uid)");
			else
				body.Append(attrValue);

			body.Append("\"");
		}
	}
	body.Append(">");
}

/** kTrue when `text` is nothing but whitespace - the indentation between two elements.

    ★The exported XML is indented, so every element is wrapped in whitespace. Kept, it makes a
    CONTAINER differ whenever its number of children changes: measured 2026-09-09, adding one
    swatch reported the colour group as Changed and the only difference in its entire body was two
    spaces.

    ⚠★★THE FIRST VERSION OF THIS TEST ALSO REQUIRED A LINE BREAK, on the reasoning that a real
    value would never carry one. IT DID NOT WORK, and the way it failed is worth keeping: the
    parser hands one run of indentation over in SEVERAL PIECES, and the pieces without the line
    break went straight through - after that build the colour group was still Changed, with 21
    spaces on one side and 23 on the other. A per-piece test cannot ask a question about the whole
    run.

    ★★★AND SO THIS IS NO LONGER ASKED PER PIECE (2026-09-12). It is asked once, of the WHOLE RUN,
    when the next structural event arrives (KCMResourceSaxHandler::FlushPendingText). The
    contract that forced it is ISaxContentHandler.h:83-87 - "SAX parsers may return all
    contiguous character data in a single chunk, or they may split it into several chunks" - and
    the split falls wherever the parser's buffer happens to end, which is a BYTE OFFSET. Asked per
    piece, a real value that ends in a space ("Chapter ") could arrive as "Chapter" + " " and lose
    its last character on ONE side only, because the two documents are different lengths and
    their buffers end in different places; the same value would then read as Changed. Asked of
    the run, whitespace is dropped only when the run holds nothing else, which is XML's own
    ignorable whitespace and is safe here because these elements hold no mixed content: a
    definition's values live in its attributes or alone between its tags, never beside a child. */
bool16 IsIgnorableWhitespace(const PMString& text)
{
	const int32 length = static_cast<int32>(text.CharCount());
	if (length == 0)
		return kFalse;

	for (int32 i = 0; i < length; ++i)
	{
		const uint32 c = text.GetWChar(i).GetValue();
		if (c != 0x20 && c != 0x09 && c != 0x0A && c != 0x0D)
			return kFalse;
	}
	return kTrue;
}

}	// anonymous namespace

/** An item that has been opened and not yet closed, together with the depth it began at.

    ★The two travel in ONE container rather than in two parallel ones so that a failure to grow
    cannot leave them disagreeing about how many items are open. */
struct KCMOpenItem
{
	KCMResourceItem	fItem;
	int32			fDepth;

	/** kTrue for the page items collected out of a <Spread>. They are built by a different rule
	    from every other item -- only three things about them are kept -- so the two places that
	    finish an item apart (EndElement and Characters) have to be able to tell them apart. */
	bool16			fFromSpread;

	/** kTrue while inside that page item's <Label>, which is where a script label lives. */
	bool16			fInLabel;

	/** kTrue once a script label has supplied this item's key, so a second pair cannot replace
	    it. A key has to be one string, and the first pair is the one a reader sees first. */
	bool16			fLabelled;

	KCMOpenItem() : fDepth(0), fFromSpread(kFalse), fInLabel(kFalse), fLabelled(kFalse) {}
};

/** Reads every element and keeps the ones the blacklist lets through, flattening each one back
    into text.

    ★★★WHAT COUNTS AS ONE ITEM, and the second rule is the one that was MEASURED INTO EXISTENCE.
    An element becomes an item when
       (1) it sits directly under <Document> -- the 83 kinds this mode is stated in; or
       (2) it carries a Self, however deep it is.
    Rule (2) was added on 2026-09-09 after the first live reading: the STYLES DO NOT SIT UNDER
    <Document>. They hang inside RootParagraphStyleGroup and its four siblings, so with rule (1)
    alone a newly added paragraph style came back as "RootParagraphStyleGroup changed" -- and ten
    edited styles would have come back as that same single line, with no way to say which. A Self
    is exactly what InDesign gives the things a person names and changes one at a time, which
    makes it the right test rather than a list of kinds (a list would be a whitelist, and the next
    document may hold a kind this one did not).

    ★AND AN ITEM'S BODY EXCLUDES ITS ITEM CHILDREN. Without that, one change is reported twice --
    measured in the same reading: adding one swatch produced both "Color added" and "ColorGroup
    changed", because the group's body carried the swatch reference too. Handing each item only
    what no other item claims makes the double report impossible rather than tolerated. */
/** ★It derives from CSAXContentHandler, which is the SDK's own base class for this: it supplies
    an empty body for every method of ISAXContentHandler and keeps the document locator, so only
    the ones that do something appear below. The first version of this file implemented all
    sixteen by hand on CPMUnknown<ISAXContentHandler>, which is not how the SDK's one worked
    example does it (xmlcataloghandler/XCatHndSAXContentHandler.cpp:65). */
class KCMResourceSaxHandler : public CSAXContentHandler
{
public:
	KCMResourceSaxHandler(IPMUnknown* boss)
		: CSAXContentHandler(boss), fSAXServices(nil), fDepth(0), fSeen(0), fSkipDepth(0),
		  fSpreadDepth(0), fList(nil), fSinkChecked(kFalse) {}
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

	/** StartElement's rule for everything inside a <Spread>: open a page item when the element
	    is one and a person has given it an identity, take only the three things a rendered page
	    cannot show, and ignore the rest. Kept apart from StartElement because it is a DIFFERENT
	    rule, not a special case of the same one - down here a Self is not enough to make an item
	    and the body is built from a filter rather than from the whole tag. */
	void				StartInSpread(const PMString& name, ISAXAttributes* attrs);

	/** Tell the caller, through the sink, that what came back is not a whole reading. */
	void				NoteMalformed();

	/** Hand the character run gathered since the last structural event to the innermost open
	    item, unless the whole run is whitespace. Called at the head of StartElement and EndElement
	    - the two events that END a run - so that the ignorable-whitespace question is asked of the
	    run and never of a piece of it (see IsIgnorableWhitespace for what a piece cost). */
	void				FlushPendingText();

	/** Characters gathered since the last structural event. ⚠The parser may hand one contiguous
	    run over in several calls (ISaxContentHandler.h:83-87), so a piece is never judged alone. */
	PMString			fPendingText;

	/** Held from Register to the destructor, AddRef'd - the sample does the same and says why:
	    the SAX services object outlives the handler, so caching the pointer is safe here even
	    though caching an interface generally is not. */
	ISAXServices*		fSAXServices;

	int32				fDepth;			// 1 = <Document>, 2 = the kinds this mode is stated in
	int32				fSeen;			// elements arrived so far (for the step log)
	int32				fSkipDepth;		// >0 while inside an excluded subtree; counts its depth
	int32				fSpreadDepth;	// >0 while inside a spread; counts its depth (filtered, not skipped)

	/** The items open right now, outermost first. ⚠A stack rather than one current item because
	    items NEST: a style sits inside a style group, which sits under <Document>. */
	K2Vector<KCMOpenItem>	fOpen;

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

void KCMResourceSaxHandler::NoteMalformed()
{
	InterfacePtr<IKCMResourceSink> sink(this, IID_IKCMRESOURCESINK);
	if (sink != nil)
		sink->NoteMalformed();
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
	fSkipDepth = 0;
	fSpreadDepth = 0;
	fOpen.clear();
	fPendingText.Clear();
	fPendingText.SetTranslatable(kFalse);
	fSinkChecked = kFalse;
	fList = nil;
	fSeen = 0;
}

void KCMResourceSaxHandler::EndDocument()
{
	KCMParseLogNum("  >> EndDocument, elements seen =", fSeen);
	// Text after the root's closing tag belongs to nothing; whatever is pending is dropped with
	// the items below rather than filed.
	fPendingText.Clear();
	if (!fOpen.empty())
	{
		// Items were still open when the document ended: the XML is malformed. Say so rather than
		// filing half-read definitions that would compare as "changed" against whole ones.
		this->NoteMalformed();
		fOpen.clear();
	}
}

void KCMResourceSaxHandler::StartInSpread(const PMString& name, ISAXAttributes* attrs)
{
	// ----- already collecting a page item -------------------------------------------------
	// The only thing worth taking from inside one is the script label. Everything else down here
	// - PathGeometry, TextWrapPreference, the transform - is drawn, and drawn things are Pixel's.
	if (!fOpen.empty() && fOpen.back().fFromSpread)
	{
		KCMOpenItem& top = fOpen.back();
		if (name == "Label")
		{
			top.fInLabel = kTrue;
			return;
		}
		if (top.fInLabel && name == "KeyValuePair" && attrs != nil)
		{
			PMString key;
			PMString value;
			key.SetTranslatable(kFalse);
			value.SetTranslatable(kFalse);
			if (attrs->HasAttribute(PMString("Key")))
				key = attrs->GetAttributeString(PMString("Key"));
			if (attrs->HasAttribute(PMString("Value")))
				value = attrs->GetAttributeString(PMString("Value"));

			// Written as a pseudo-attribute so that the attribute diff splits it out on its own
			// row, the same way a real attribute gets one. A script label IS a key and a value;
			// this is the shape that says so.
			top.fItem.fBody.Append(" ScriptLabel.");
			top.fItem.fBody.Append(key);
			top.fItem.fBody.Append("=\"");
			top.fItem.fBody.Append(value);
			top.fItem.fBody.Append("\"");

			// ★★THE LABEL WINS AS THE KEY, and that is the point of collecting it. A person
			//   attaches a script label to say "this is the object I mean", and unlike the name
			//   IT SURVIVES A RENAME - so an item that has one reports "Name: A -> B" as a
			//   change, while an item identified only by its name reports a rename as a removal
			//   and an addition (which is what renaming a style does too, by the user's own
			//   decision of 2026-09-09).
			if (!top.fLabelled)
			{
				top.fLabelled = kTrue;
				top.fItem.fName = key;
				top.fItem.fName.Append("=");
				top.fItem.fName.Append(value);
			}
		}
		return;
	}

	// ----- not inside one yet: does this element start one? --------------------------------
	if (attrs == nil || !attrs->HasAttribute(PMString("Self")))
		return;
	if (KCMIsSpreadStructureElement(name))
		return;

	KCMOpenItem opened;
	opened.fDepth = fDepth;
	opened.fFromSpread = kTrue;
	opened.fItem.fKind = name;
	opened.fItem.fOrdinal = 0;
	opened.fItem.fKind.SetTranslatable(kFalse);
	opened.fItem.fSelf.SetTranslatable(kFalse);
	opened.fItem.fName.SetTranslatable(kFalse);
	opened.fItem.fBody.SetTranslatable(kFalse);
	opened.fItem.fUniqueId.SetTranslatable(kFalse);

	// The Self is a bare UID here, which is what sends this item down KCMResourceKeyOf's route
	// [C] - kind plus the name a person gave it. That is the whole reason the item is only kept
	// when it HAS such a name: without one, [C] falls through to "the nth of a kind", and one
	// object inserted anywhere would shift every pair after it.
	opened.fItem.fSelf = attrs->GetAttributeString(PMString("Self"));

	opened.fItem.fBody.Append("<");
	opened.fItem.fBody.Append(name);

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
		if (!KCMPageItemAttributeWanted(attrName))
			continue;

		const PMString attrValue(value);

		// The placeholder an unnamed object carries is not written down at all: it is not a name,
		// so it must not become the key, and showing it in the body would put "$ID/" on the panel
		// as though somebody had typed it.
		if (attrName == "Name" && KCMIsUnnamedPageItemName(attrValue))
			continue;

		opened.fItem.fBody.Append(" ");
		opened.fItem.fBody.Append(attrName);
		opened.fItem.fBody.Append("=\"");
		opened.fItem.fBody.Append(attrValue);
		opened.fItem.fBody.Append("\"");

		if (attrName == "Name")
			opened.fItem.fName = attrValue;
	}

	try
	{
		fOpen.push_back(opened);
	}
	catch (...)
	{
		this->NoteMalformed();
		KCMParseLog("    !! the open-item stack could not grow (in a spread)");
	}
}

void KCMResourceSaxHandler::FlushPendingText()
{
	if (fPendingText.IsEmpty())
		return;

	// The run ends here. Only now can "is it nothing but whitespace" be answered truthfully.
	if (!IsIgnorableWhitespace(fPendingText) && !fOpen.empty())
		fOpen.back().fItem.fBody.Append(fPendingText);

	fPendingText.Clear();
}

void KCMResourceSaxHandler::StartElement(const WideString& /*uri*/, const WideString& localname,
										 const WideString& /*qname*/, ISAXAttributes* attrs)
{
	// A new tag ends whatever character run was in progress; file it before anything of the new
	// element reaches a body, so the body keeps the document's order.
	this->FlushPendingText();

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

	// Inside <Story> or the XMP packet: another mode's territory, and its elements carry a Self of
	// their own, so the whole subtree has to be stepped over rather than filtered element by
	// element.
	if (fSkipDepth > 0)
	{
		++fSkipDepth;
		return;
	}

	// ★★A SPREAD IS FILTERED, NOT SKIPPED (2026-09-10). Its rendered content is Pixel's and is
	//   left alone; what comes out of here is the handful of things a page item can carry that a
	//   rendered page CANNOT show - its name, its lock, and its script label. StartInSpread is
	//   where that rule lives.
	if (fSpreadDepth > 0)
	{
		++fSpreadDepth;
		this->StartInSpread(name, attrs);
		return;
	}
	if (fDepth == 2 && KCMIsSpreadContainer(name))
	{
		fSpreadDepth = 1;
		return;
	}
	if (fDepth == 2 && KCMIsExcludedResource(name))
	{
		fSkipDepth = 1;
		return;
	}

	const bool16 hasSelf = (attrs != nil && attrs->HasAttribute(PMString("Self")));
	const bool16 startsItem = ((fDepth == 2) || hasSelf);

	if (!startsItem)
	{
		// Part of whatever item is open around it -- its attributes and text belong in that
		// item's body, which is what a change to it will be seen as.
		if (!fOpen.empty())
		{
			PMString ignored;
			ignored.SetTranslatable(kFalse);
			AppendOpenTag(fOpen.back().fItem.fBody, name, attrs, ignored);
		}
		return;
	}

	KCMOpenItem opened;
	opened.fDepth = fDepth;
	opened.fItem.fKind = name;
	opened.fItem.fOrdinal = 0;			// filled in once the whole list is known
	opened.fItem.fKind.SetTranslatable(kFalse);
	opened.fItem.fSelf.SetTranslatable(kFalse);
	opened.fItem.fName.SetTranslatable(kFalse);
	opened.fItem.fBody.SetTranslatable(kFalse);
	opened.fItem.fUniqueId.SetTranslatable(kFalse);

	// The RAW MATERIALS a key can be made from, and nothing more. Both are taken for every item:
	// which of them a given kind actually needs is KCMResourceKeyOf's question
	// (KCMResourceDiff.h), and asking it here as well would be the same question answered in two
	// files.
	if (attrs != nil)
	{
		if (hasSelf)
			opened.fItem.fSelf = attrs->GetAttributeString(PMString("Self"));
		if (attrs->HasAttribute(PMString("Name")))
			opened.fItem.fName = attrs->GetAttributeString(PMString("Name"));
	}

	// ★The opening tag goes into the NEW item's body and NOT into its parent's. That single
	//   choice is what stops one change being reported twice, as itself and as the thing around
	//   it.
	AppendOpenTag(opened.fItem.fBody, name, attrs, opened.fItem.fUniqueId);

	try
	{
		fOpen.push_back(opened);
	}
	catch (...)
	{
		// The stack could not grow. Everything from here on would be filed against the wrong
		// parent, so the reading is declared unusable rather than quietly reshaped.
		this->NoteMalformed();
		KCMParseLog("    !! the open-item stack could not grow");
	}
}

void KCMResourceSaxHandler::EndElement(const WideString& /*uri*/, const WideString& localname,
									   const WideString& /*qname*/)
{
	// The closing tag ends the run too - and it has to be filed BEFORE the closing tag is
	// appended below, or the value would land after its own end tag.
	this->FlushPendingText();

	if (fSkipDepth > 0)
	{
		--fSkipDepth;
		if (fDepth > 0)
			--fDepth;
		return;
	}

	// ★Inside a spread nothing but a page item is being collected, so the closing tag of the
	//   spread, of a page, or of any geometry must not reach a body. This branch exists to stop
	//   that: the ordinary path below appends every closing tag to whatever item is open.
	if (fSpreadDepth > 0)
	{
		const PMString endName(localname);

		if (!fOpen.empty() && fOpen.back().fFromSpread)
		{
			KCMOpenItem& top = fOpen.back();
			if (top.fInLabel && endName == "Label")
				top.fInLabel = kFalse;

			if (top.fDepth == fDepth)
			{
				// ★★KEPT ONLY IF A PERSON GAVE IT AN IDENTITY - a name, or a script label.
				//   Without one there is nothing in the other document to pair it with: route
				//   [C] would fall through to "the nth of a kind", and a single object inserted
				//   anywhere would make every pair after it wrong. Silence is better than that.
				if (!top.fItem.fName.IsEmpty())
				{
					top.fItem.fBody.Append("></");
					top.fItem.fBody.Append(endName);
					top.fItem.fBody.Append(">");

					KCMResourceList* const list = this->List();
					if (list != nil)
					{
						try
						{
							list->push_back(top.fItem);
						}
						catch (...)
						{
							this->NoteMalformed();
							KCMParseLog("    !! push_back threw for a page item");
						}
					}
				}
				fOpen.pop_back();
			}
		}

		--fSpreadDepth;
		if (fDepth > 0)
			--fDepth;
		return;
	}

	if (!fOpen.empty())
	{
		KCMResourceItem& innermost = fOpen.back().fItem;
		innermost.fBody.Append("</");
		innermost.fBody.Append(PMString(localname));
		innermost.fBody.Append(">");

		// Is this the element that OPENED the innermost item? Then the item is complete.
		if (fOpen.back().fDepth == fDepth)
		{
			KCMResourceList* const list = this->List();
			if (list != nil)
			{
				// ⚠A container throws when it cannot grow, and an exception crossing the SAX
				//   boundary would take InDesign down with it. Caught here, the parse simply ends
				//   up short - and KCMParseResources refuses a short result rather than handing
				//   back a document that would read as "these definitions were removed".
				//   The sample wraps its whole element handler the same way (processElement's
				//   try/catch), for the same reason.
				try
				{
					list->push_back(innermost);
				}
				catch (...)
				{
					this->NoteMalformed();
					KCMParseLog("    !! push_back threw - the list could not grow");
				}
			}
			fOpen.pop_back();
		}
	}

	if (fDepth > 0)
		--fDepth;
}

void KCMResourceSaxHandler::Characters(const WideString& chars)
{
	// ★★★INSIDE AN EXCLUDED SUBTREE THERE IS NOTHING TO COLLECT, and forgetting this line was a
	//   real defect rather than a tidiness point. StartElement and EndElement both step over
	//   <Spread>, <Story> and the XMP packet; Characters did not, so THEIR TEXT was appended to
	//   whichever item was open around them - which is <Document>. Measured 2026-09-09: two
	//   documents that differed in nothing reported <Document> as Changed, and the difference was
	//   the XMP packet's namespace declarations, 1,114 characters into a body that had no
	//   business holding them. The same route was feeding every story's body text into
	//   <Document>, so an edit Story mode owns would have been reported here as well.
	if (fSkipDepth > 0)
		return;

	// ★Same reasoning one step further in (2026-09-10): inside a spread a page item's body is
	//   built from a FILTER, not from what arrives, so text that turns up down there - the
	//   contents of a <Properties> block, a path's numbers - belongs to nothing here. Letting it
	//   through would put geometry back into a body that was carefully built without it, and the
	//   item would differ every time an object moved: the double report all over again.
	if (fSpreadDepth > 0)
		return;

	// Text belongs to the INNERMOST open item, for the same reason its child elements do: it is
	// the item a change to those characters should be reported against.
	if (fOpen.empty())
		return;

	// ★GATHERED, NOT FILED. This may be one piece of a run the parser has split
	//   (ISaxContentHandler.h:83-87), and whether the run is only indentation cannot be known from
	//   a piece. The next StartElement or EndElement ends the run and FlushPendingText decides.
	//   ⚠The tests above are safe to make per piece: nothing structural can happen between two
	//     pieces of one run, so fSkipDepth, fSpreadDepth and fOpen are the same for all of them.
	fPendingText.Append(PMString(chars));
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
	// takeOwnership kFalse, recycleBoss kFalse: the bytes belong to the caller and may be a stack
	// object, and StreamUtil.h:236-239 warns that a recycled stream boss can keep hold of the
	// IXferBytes past its life. KCMResourceSnapshot's write stream says the same.
	InterfacePtr<IPMStream> readStream(StreamUtil::CreateMemoryStreamRead(&mutableBytes, kFalse, kFalse));
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

	// ----- the ordinals: which one each item is among its OWN KIND, in the order they finished.
	//
	// ★It is done here rather than inside the handler, and that is not tidiness. The handler runs
	// inside the parser, where anything that has to grow is a throw waiting to cross the SAX
	// boundary (the push_back above is wrapped for exactly that reason). This loop runs after the
	// parse is over and touches nothing but ints.
	// The shape is O(n^2) in the number of items -- 171 for a four-page document -- which is the
	// same shape KCMDescribeResourceSnapshot already uses to count kinds, and at these sizes the
	// straightforward loop is the one that can be read.
	for (int32 i = 0; i < static_cast<int32>(out.size()); ++i)
	{
		int32 seen = 0;
		for (int32 j = 0; j < i; ++j)
		{
			if (out[j].fKind == out[i].fKind)
				++seen;
		}
		out[i].fOrdinal = seen;
	}
	return kTrue;
}

// End, KCMResourceParse.cpp.
