//========================================================================================
//
//  KCMPageDocx.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "KCMPageDocx.h"
#include "KCMStoryDocx.h"		// WriteBlocks / WriteStyles / PutWordsIntoPlaceholders / ReadBlocksIn - the words' spelling
#include "KCMXmlTree.h"

namespace KCMPageDocx
{

const char* const kPageTagNamespace = "urn:kohaku:kcm:page:1";

namespace
{

const char* const kXmlDeclaration = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
const char* const kWordNamespace = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
const char* const kVmlNamespaces = "xmlns:v=\"urn:schemas-microsoft-com:vml\" "
								   "xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
								   "xmlns:w10=\"urn:schemas-microsoft-com:office:word\"";
const char* const kBoxShapePrefix = "kcmbox";		// a box's shape: v:shape@id, and wp:docPr@name once Word made it DrawingML
const char* const kBoxStylePrefix = "KCMBox";
const char* const kPageFormat = "2";		// the story tag is format 1

void AppendEscaped(const std::string& s, std::string& out)
{
	for (size_t i = 0; i < s.size(); ++i)
	{
		const char c = s[i];
		if (c == '&')		out += "&amp;";
		else if (c == '<')	out += "&lt;";
		else if (c == '>')	out += "&gt;";
		else if (c == '"')	out += "&quot;";
		else				out += c;
	}
}

void AppendInt(long long n, std::string& out)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
	out += buf;
}

/** Points with at most two decimals and no trailing zeros ("82.91", "4.5", "0") - the same number is the same bytes. */
void AppendPoints(double v, std::string& out)
{
	char buf[48];
	std::snprintf(buf, sizeof(buf), "%.2f", v);
	std::string s(buf);
	const size_t dot = s.find('.');
	if (dot != std::string::npos)
	{
		size_t end = s.size();
		while (end > dot + 1 && s[end - 1] == '0')
			--end;
		if (end == dot + 1)
			end = dot;
		s.erase(end);
	}
	if (s == "-0")
		s = "0";
	out += s;
	out += "pt";
}

/** A page dimension in twentieths of a point, as w:pgSz wants it. */
int32 Twips(double points)
{
	return static_cast<int32>(std::floor(points * 20.0 + 0.5));
}

void AddPart(const char* name, const std::string& bytes, std::vector<KCMZipStore::Entry>& parts)
{
	KCMZipStore::Entry e;
	e.fName = name;
	e.fBytes = bytes;
	parts.push_back(e);
}

/** What one box's paragraphs are styled with: its first character's size and font. Boxes that agree share one style,
	numbered in the order the boxes first use them. */
struct BoxLook
{
	double		fPointSize;
	std::string	fFontName;
};

int32 LookIndex(std::vector<BoxLook>& looks, const Box& box)
{
	for (size_t k = 0; k < looks.size(); ++k)
	{
		if (looks[k].fPointSize == box.fPointSize && looks[k].fFontName == box.fFontName)
			return static_cast<int32>(k) + 1;
	}
	BoxLook look;
	look.fPointSize = box.fPointSize;
	look.fFontName = box.fFontName;
	looks.push_back(look);
	return static_cast<int32>(looks.size());
}

/** Every paragraph of `blocks` put in style `styleId`. ★Safe because a box's blocks are paragraphs only - no table, no
	note (the caller has refused those) - and WriteBlocks opens a paragraph as "<w:p>" or "<w:p/>" and in no other way;
	text in a run is escaped, so "<w:p>" cannot stand in it. */
std::string StyleParagraphs(const std::string& blocks, const std::string& styleId)
{
	const std::string pPr = "<w:pPr><w:pStyle w:val=\"" + styleId + "\"/></w:pPr>";
	std::string out;
	out.reserve(blocks.size() + 64);
	size_t at = 0;
	for (;;)
	{
		const size_t open = blocks.find("<w:p", at);
		if (open == std::string::npos)
		{
			out.append(blocks, at, std::string::npos);
			break;
		}
		const size_t after = open + 4;
		if (blocks.compare(after, 1, ">") == 0)
		{
			out.append(blocks, at, after + 1 - at);
			out += pPr;
			at = after + 1;
		}
		else if (blocks.compare(after, 2, "/>") == 0)
		{
			out.append(blocks, at, open - at);
			out += "<w:p>" + pPr + "</w:p>";
			at = after + 2;
		}
		else
		{
			out.append(blocks, at, after - at);		// <w:pPr, <w:pict, ... - not a paragraph's start
			at = after;
		}
	}
	return out;
}

void AppendBoxStyle(int32 k, const BoxLook& look, std::string& out)
{
	out += "<w:style w:type=\"paragraph\" w:customStyle=\"1\" w:styleId=\"";
	out += kBoxStylePrefix;
	AppendInt(k, out);
	out += "\"><w:name w:val=\"KCM Box ";
	AppendInt(k, out);
	if (look.fPointSize > 0)
	{
		out += " ";
		std::string size;
		AppendPoints(look.fPointSize, size);
		out += size;
	}
	if (!look.fFontName.empty())
	{
		out += " ";
		AppendEscaped(look.fFontName, out);
	}
	out += "\"/><w:basedOn w:val=\"Normal\"/><w:qFormat/>";
	if (look.fPointSize > 0 || !look.fFontName.empty())
	{
		out += "<w:rPr>";
		if (!look.fFontName.empty())
		{
			out += "<w:rFonts w:ascii=\"";
			AppendEscaped(look.fFontName, out);
			out += "\" w:hAnsi=\"";
			AppendEscaped(look.fFontName, out);
			out += "\" w:eastAsia=\"";
			AppendEscaped(look.fFontName, out);
			out += "\"/>";
		}
		if (look.fPointSize > 0)
		{
			const long long halfPoints = static_cast<long long>(std::floor(look.fPointSize * 2.0 + 0.5));
			out += "<w:sz w:val=\"";
			AppendInt(halfPoints, out);
			out += "\"/><w:szCs w:val=\"";
			AppendInt(halfPoints, out);
			out += "\"/>";
		}
		out += "</w:rPr>";
	}
	out += "</w:style>";
}

/** The VML shape type every text box names - Word's own, written once. */
const char* const kTextBoxShapeType =
	"<v:shapetype id=\"_x0000_t202\" coordsize=\"21600,21600\" o:spt=\"202\" path=\"m,l,21600r21600,l21600,xe\">"
	"<v:stroke joinstyle=\"miter\"/><v:path gradientshapeok=\"t\" o:connecttype=\"rect\"/></v:shapetype>";

/** One box: a VML text box named for the box ("kcmbox<n>" - ★THE NAME IS THE BOX, see the header), its words straight
	in its w:txbxContent. ★A HORIZONTAL BOX GROWS AND SHRINKS WITH ITS WORDS (mso-fit-shape-to-text) from its top left, the
	corner InDesign starts the text from; ★A VERTICAL ONE KEEPS THE FRAME'S SIZE - Word fits a vertical box by its WIDTH and
	keeps its left edge (measured in Word 2007), while InDesign sets the first column at the frame's right edge. */
void AppendBox(const Box& box, const std::string& styledBlocks, bool16 first, std::string& out)
{
	out += "<w:r><w:pict>";
	if (first)
		out += kTextBoxShapeType;
	out += "<v:shape id=\"";
	out += kBoxShapePrefix;
	AppendInt(box.fN, out);
	out += "\" type=\"#_x0000_t202\" style=\"position:absolute;margin-left:";
	AppendPoints(box.fX, out);
	out += ";margin-top:";
	AppendPoints(box.fY, out);
	out += ";width:";
	AppendPoints(box.fW, out);
	out += ";height:";
	AppendPoints(box.fH, out);
	out += ";z-index:";
	AppendInt(box.fN, out);
	out += ";mso-position-horizontal-relative:page;mso-position-vertical-relative:page\""
		   " filled=\"f\" strokecolor=\"#BFBFBF\" strokeweight=\".5pt\">";
	out += "<v:textbox inset=\"";
	AppendPoints(box.fInsetL, out);
	out += ",";
	AppendPoints(box.fInsetT, out);
	out += ",";
	AppendPoints(box.fInsetR, out);
	out += ",";
	AppendPoints(box.fInsetB, out);
	out += "\" style=\"";
	out += box.fVertical ? "layout-flow:vertical-ideographic" : "mso-fit-shape-to-text:t";
	out += "\"><w:txbxContent>";
	out += styledBlocks;
	out += "</w:txbxContent></v:textbox>"
		   "<w10:wrap type=\"none\" anchorx=\"page\" anchory=\"page\"/></v:shape></w:pict></w:r>";
}

}	// anonymous namespace

bool16 WriteParts(const Page& page, std::vector<KCMZipStore::Entry>& outParts, std::string& whyNot)
{
	outParts.clear();
	whyNot.clear();

	// ---- everything that can refuse, before any part is made -------------------------------------
	for (size_t i = 0; i < page.fBoxes.size(); ++i)
	{
		for (size_t j = 0; j < i; ++j)
		{
			if (page.fBoxes[i].fN == page.fBoxes[j].fN)
			{
				whyNot = "two boxes share the number ";
				AppendInt(page.fBoxes[i].fN, whyNot);
				return kFalse;
			}
		}
	}

	std::vector<BoxLook> looks;
	std::string boxes;
	KCMStoryShape::Story every;			// every box's paragraphs - only for the kenten marks styles.xml has to name
	for (size_t i = 0; i < page.fBoxes.size(); ++i)
	{
		const Box& box = page.fBoxes[i];
		std::string blocks;
		std::string why;
		if (!KCMStoryDocx::WriteBlocks(box.fContent, -1, 0, 0, blocks, why))
		{
			whyNot = "box ";
			AppendInt(box.fN, whyNot);
			whyNot += ": " + why;
			return kFalse;
		}
		// A box always holds a paragraph: Word's text box cannot be empty, and the reader reads one back.
		if (blocks.empty())
			blocks = "<w:p/>";
		std::string styleId = kBoxStylePrefix;
		AppendInt(LookIndex(looks, box), styleId);
		AppendBox(box, StyleParagraphs(blocks, styleId), (i == 0) ? kTrue : kFalse, boxes);
		every.fBody.insert(every.fBody.end(), box.fContent.fBody.begin(), box.fContent.fBody.end());
	}

	std::string styles;
	if (!KCMStoryDocx::WriteStyles(every, styles, whyNot))
		return kFalse;
	const std::string stylesEnd = "</w:styles>";
	if (styles.size() < stylesEnd.size() || styles.compare(styles.size() - stylesEnd.size(), stylesEnd.size(), stylesEnd) != 0)
	{
		whyNot = "the styles part does not end the way the page format adds to it";
		return kFalse;
	}
	styles.erase(styles.size() - stylesEnd.size());
	for (size_t k = 0; k < looks.size(); ++k)
		AppendBoxStyle(static_cast<int32>(k) + 1, looks[k], styles);
	styles += stylesEnd;

	// ---- the document: one paragraph anchoring every box, the page's own size --------------------------
	std::string document = kXmlDeclaration;
	document += "<w:document ";
	document += kWordNamespace;
	document += " ";
	document += kVmlNamespaces;
	document += "><w:body><w:p>";
	document += boxes;
	document += "</w:p><w:sectPr><w:pgSz w:w=\"";
	AppendInt(Twips(page.fWidth), document);
	document += "\" w:h=\"";
	AppendInt(Twips(page.fHeight), document);
	document += "\"";
	if (page.fWidth > page.fHeight)
		document += " w:orient=\"landscape\"";
	document += "/><w:pgMar w:top=\"0\" w:right=\"0\" w:bottom=\"0\" w:left=\"0\" w:header=\"0\" w:footer=\"0\" w:gutter=\"0\"/>"
				"</w:sectPr></w:body></w:document>";
	KCMStoryDocx::PutWordsIntoPlaceholders(document);

	// ---- the parts - the story format's skeleton (KCMStoryDocx::WriteParts), with no footnotes -------
	std::string types = kXmlDeclaration;
	types += "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
			 "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
			 "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
			 "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
			 "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
			 "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>"
			 "<Override PartName=\"/customXml/itemProps1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.customXmlProperties+xml\"/>"
			 "</Types>";
	AddPart("[Content_Types].xml", types, outParts);

	std::string rootRels = kXmlDeclaration;
	rootRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
				"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
				"</Relationships>";
	AddPart("_rels/.rels", rootRels, outParts);

	AddPart("word/document.xml", document, outParts);

	std::string docRels = kXmlDeclaration;
	docRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
			   "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
			   "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
			   "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml\" Target=\"../customXml/item1.xml\"/>"
			   "</Relationships>";
	AddPart("word/_rels/document.xml.rels", docRels, outParts);

	AddPart("word/styles.xml", styles, outParts);

	std::string settings = kXmlDeclaration;
	settings += "<w:settings ";
	settings += kWordNamespace;
	settings += "></w:settings>";
	AddPart("word/settings.xml", settings, outParts);

	// ---- the page tag: which frame and story each box is, and nothing of their words ------------------
	// ⚠NO LINE BREAK AFTER THE DECLARATION - the story tag's reason: Word parses this part and writes it out again
	//  without one, and written that way the part is the same bytes before and after a save.
	std::string tag = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><kcm:page xmlns:kcm=\"";
	tag += kPageTagNamespace;
	tag += "\" format=\"";
	tag += kPageFormat;
	tag += "\" page=\"";
	AppendInt(page.fPageUid, tag);
	tag += "\" name=\"";
	AppendEscaped(page.fPageName, tag);
	// ★WHICH DOCUMENT, BY ID (2026-10-26 - Page::fDocumentId): only what is known is written. Format 2 still - a reader that
	//  does not know the two attributes leaves them, as it leaves whole below.
	if (!page.fDocumentId.empty())
	{
		tag += "\" doc=\"";
		AppendEscaped(page.fDocumentId, tag);
	}
	if (!page.fOriginalDocumentId.empty())
	{
		tag += "\" orig=\"";
		AppendEscaped(page.fOriginalDocumentId, tag);
	}
	tag += "\">";
	for (size_t i = 0; i < page.fBoxes.size(); ++i)
	{
		const Box& box = page.fBoxes[i];
		tag += "<kcm:box n=\"";
		AppendInt(box.fN, tag);
		tag += "\" frame=\"";
		AppendInt(box.fFrame, tag);
		tag += "\" story=\"";
		AppendInt(box.fStory, tag);
		tag += "\" start=\"";
		AppendInt(box.fStart, tag);
		tag += "\" end=\"";
		AppendInt(box.fEnd, tag);
		tag += "\" bytes=\"";
		AppendInt(box.fPrintBytes, tag);
		tag += "\" crc=\"";
		AppendInt(static_cast<long long>(box.fPrintCrc), tag);
		if (box.fWhole)
			tag += "\" whole=\"1";		// the box is its whole story (Box::fWhole)
		tag += "\"/>";
	}
	tag += "</kcm:page>";
	AddPart("customXml/item1.xml", tag, outParts);

	// The item's id is fixed, as the story format's is - and not the story format's own.
	std::string props = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\r\n"
						"<ds:datastoreItem ds:itemID=\"{5B0F4C3A-1D2E-4F60-8A7B-9C0D1E2F3A4C}\" "
						"xmlns:ds=\"http://schemas.openxmlformats.org/officeDocument/2006/customXml\">"
						"<ds:schemaRefs><ds:schemaRef ds:uri=\"";
	props += kPageTagNamespace;
	props += "\"/></ds:schemaRefs></ds:datastoreItem>";
	AddPart("customXml/itemProps1.xml", props, outParts);

	std::string itemRels = kXmlDeclaration;
	itemRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
				"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps\" Target=\"itemProps1.xml\"/>"
				"</Relationships>";
	AddPart("customXml/_rels/item1.xml.rels", itemRels, outParts);

	return kTrue;
}

//========================================================================================
//  THE READING HALF
//========================================================================================

namespace
{

const char* const kW = KCMXmlTree::kWordNs;
const char* const kMc = KCMXmlTree::kMcNs;

/** A decimal number of at most `maxDigits` digits -> `out`. kFalse for anything else. */
bool16 ParseUnsigned(const std::string* text, int maxDigits, unsigned long long& out)
{
	if (text == nil || text->empty() || static_cast<int>(text->size()) > maxDigits)
		return kFalse;
	unsigned long long v = 0;
	for (size_t i = 0; i < text->size(); ++i)
	{
		const char c = (*text)[i];
		if (c < '0' || c > '9')
			return kFalse;
		v = v * 10 + static_cast<unsigned long long>(c - '0');
	}
	out = v;
	return kTrue;
}

bool16 ParseInt32(const std::string* text, int32& out)
{
	unsigned long long v = 0;
	if (!ParseUnsigned(text, 10, v) || v > 0x7FFFFFFFull)
		return kFalse;
	out = static_cast<int32>(v);
	return kTrue;
}

bool16 ParseUInt32(const std::string* text, uint32& out)
{
	unsigned long long v = 0;
	if (!ParseUnsigned(text, 10, v) || v > 0xFFFFFFFFull)
		return kFalse;
	out = static_cast<uint32>(v);
	return kTrue;
}

const char* const kVml = "urn:schemas-microsoft-com:vml";
const char* const kWp = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";

/** "kcmbox<n>" -> n. kFalse for any other name. */
bool16 BoxNumberOfName(const std::string* name, int32& out)
{
	const std::string prefix = kBoxShapePrefix;
	if (name == nil || name->size() <= prefix.size() || name->compare(0, prefix.size(), prefix) != 0)
		return kFalse;
	const std::string digits = name->substr(prefix.size());
	return ParseInt32(&digits, out);
}

/** One box's text met in the document: the w:txbxContent of a shape named for a box. */
struct Seen
{
	int32	fN;
	int32	fContent;		// the w:txbxContent
	bool16	fInFallback;	// inside mc:Fallback - the VML copy Word 2010 and later keep beside the DrawingML one
};

/** The walk over the document: every box's text, and whether words stand outside every box. `box` is the number of the
	box whose shape the walk is inside (0: none). ★A TEXT BOX OF NO BOX'S SHAPE - one copied in Word, say - holds words that
	will not be read, so they count as outside. */
void Walk(const KCMXmlTree& t, int32 node, bool16 inFallback, int32 box, bool16 inBoxText, std::vector<Seen>& seen,
		  bool16& outsideText)
{
	const KCMXmlNode& n = t.At(node);
	if (n.IsText())
		return;
	if (n.fNs == kMc && n.fName == "Fallback")
		inFallback = kTrue;
	// ★WHICH BOX: the VML shape's id (Word 2007 keeps it, and names the shape by it), or - once Word has made the shape
	//   DrawingML - the name on its anchor's wp:docPr.
	if (n.fNs == kVml && n.fName == "shape")
	{
		int32 number = 0;
		box = BoxNumberOfName(t.Attr(node, "id"), number) ? number : 0;
	}
	else if (n.fNs == kWp && (n.fName == "anchor" || n.fName == "inline"))
	{
		const int32 docPr = t.Child(node, kWp, "docPr");
		int32 number = 0;
		box = (docPr >= 0 && BoxNumberOfName(t.Attr(docPr, "name"), number)) ? number : 0;
	}
	if (n.fNs == kW && n.fName == "txbxContent" && box > 0)
	{
		Seen s;
		s.fN = box;
		s.fContent = node;
		s.fInFallback = inFallback;
		seen.push_back(s);
		inBoxText = kTrue;
	}
	if (n.fNs == kW && n.fName == "t" && !inBoxText && !inFallback)
	{
		const std::string words = t.TextBelow(node);
		for (size_t i = 0; i < words.size(); ++i)
		{
			const char c = words[i];
			if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			{
				outsideText = kTrue;
				break;
			}
		}
	}
	for (size_t k = 0; k < n.fChildren.size(); ++k)
		Walk(t, n.fChildren[k], inFallback, box, inBoxText, seen, outsideText);
}

const std::string* FindPartBytes(const std::vector<KCMZipStore::Entry>& parts, const char* name)
{
	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (parts[i].fName == name)
			return &parts[i].fBytes;
	}
	return nil;
}

}	// anonymous namespace

bool16 ReadParts(const std::vector<KCMZipStore::Entry>& parts, ReadResult& out, std::string& whyNot)
{
	out = ReadResult();
	whyNot.clear();

	// ---- the tag: is this a page at all ------------------------------------------------------------------
	KCMXmlTree tag;
	int32 tagRoot = -1;
	for (size_t i = 0; i < parts.size() && tagRoot < 0; ++i)
	{
		const std::string& name = parts[i].fName;
		if (name.compare(0, 14, "customXml/item") != 0 || name.compare(0, 19, "customXml/itemProps") == 0
			|| name.size() < 4 || name.compare(name.size() - 4, 4, ".xml") != 0)
			continue;
		std::string ignored;
		if (!tag.Parse(parts[i].fBytes.data(), parts[i].fBytes.size(), ignored))
			continue;							// not even XML: somebody else's part, none of ours
		const int32 root = tag.Root();
		if (root >= 0 && tag.Is(root, kPageTagNamespace, "page"))
			tagRoot = root;
	}
	if (tagRoot < 0)
		return kTrue;							// no page tag: not a page (a story file, or a stranger's)

	out.fIsPage = kTrue;
	const std::string* format = tag.Attr(tagRoot, "format");
	if (format == nil || *format != kPageFormat)
	{
		whyNot = "a page tag of format " + ((format != nil) ? *format : std::string("(none)")) + ", which this build does not read";
		return kFalse;
	}
	if (!ParseInt32(tag.Attr(tagRoot, "page"), out.fPageUid))
	{
		whyNot = "the page tag's page is not a number";
		return kFalse;
	}
	const std::string* name = tag.Attr(tagRoot, "name");
	if (name != nil)
		out.fPageName = *name;
	// (2026-10-26) which document - absent from a file of 2026-10-25, and then not known
	const std::string* docId = tag.Attr(tagRoot, "doc");
	if (docId != nil)
		out.fDocumentId = *docId;
	const std::string* originalId = tag.Attr(tagRoot, "orig");
	if (originalId != nil)
		out.fOriginalDocumentId = *originalId;
	const KCMXmlNode& rootNode = tag.At(tagRoot);
	for (size_t k = 0; k < rootNode.fChildren.size(); ++k)
	{
		const int32 c = rootNode.fChildren[k];
		if (!tag.Is(c, kPageTagNamespace, "box"))
			continue;
		ReadBox rb;
		Box& b = rb.fBox;
		if (!ParseInt32(tag.Attr(c, "n"), b.fN) || !ParseInt32(tag.Attr(c, "frame"), b.fFrame)
			|| !ParseInt32(tag.Attr(c, "story"), b.fStory) || !ParseInt32(tag.Attr(c, "start"), b.fStart)
			|| !ParseInt32(tag.Attr(c, "end"), b.fEnd) || !ParseInt32(tag.Attr(c, "bytes"), b.fPrintBytes)
			|| !ParseUInt32(tag.Attr(c, "crc"), b.fPrintCrc))
		{
			whyNot = "a box of the page tag cannot be read";
			return kFalse;
		}
		const std::string* whole = tag.Attr(c, "whole");
		b.fWhole = (whole != nil && *whole == "1") ? kTrue : kFalse;
		out.fBoxes.push_back(rb);
	}

	// ---- the document: where each box's words stand ------------------------------------------------------
	const std::string* documentXml = FindPartBytes(parts, "word/document.xml");
	if (documentXml == nil)
	{
		whyNot = "the page has no word/document.xml";
		return kFalse;
	}
	KCMXmlTree document;
	if (!document.Parse(documentXml->data(), documentXml->size(), whyNot))
	{
		whyNot = "word/document.xml: " + whyNot;
		return kFalse;
	}
	const int32 root = document.Root();
	const int32 body = (root >= 0 && document.Is(root, kW, "document")) ? document.Child(root, kW, "body") : -1;
	if (body < 0)
	{
		whyNot = "word/document.xml has no w:body";
		return kFalse;
	}
	const std::string* stylesXml = FindPartBytes(parts, "word/styles.xml");
	const std::string styles = (stylesXml != nil) ? *stylesXml : std::string();

	std::vector<Seen> seen;
	Walk(document, body, kFalse, 0, kFalse, seen, out.fTextOutsideBoxes);

	for (size_t i = 0; i < out.fBoxes.size(); ++i)
	{
		ReadBox& rb = out.fBoxes[i];
		// ★THE ONE OUTSIDE mc:Fallback COUNTS; the Fallback's only when there is no other - Word 2010 and later keep a
		//   VML copy of every DrawingML box there, holding the same words.
		int32 outside = 0;
		int32 inFallback = 0;
		int32 contentOutside = -1;
		int32 contentFallback = -1;
		for (size_t s = 0; s < seen.size(); ++s)
		{
			if (seen[s].fN != rb.fBox.fN)
				continue;
			if (seen[s].fInFallback)
			{
				++inFallback;
				contentFallback = seen[s].fContent;
			}
			else
			{
				++outside;
				contentOutside = seen[s].fContent;
			}
		}
		const int32 count = (outside > 0) ? outside : inFallback;
		const int32 content = (outside > 0) ? contentOutside : contentFallback;
		if (count == 0)
		{
			rb.fWhyNot = "the box was not found in the file (deleted in Word?) - nothing of it goes in";
			continue;
		}
		if (count > 1)
		{
			rb.fWhyNot = "the box is in the file twice (copied in Word?)";
			continue;
		}
		// ★THE WHOLE TEXT BOX IS THE BOX'S WORDS - whatever Word did to the paragraphs inside it.
		std::string why;
		if (!KCMStoryDocx::ReadBlocksIn(document, content, styles, rb.fBox.fContent, why))
			rb.fWhyNot = "the box's words cannot be read: " + why;
	}
	return kTrue;
}

}	// namespace KCMPageDocx

// End, KCMPageDocx.cpp.
