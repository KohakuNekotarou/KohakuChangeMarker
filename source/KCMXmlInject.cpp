//========================================================================================
//
//  KCMXmlInject.cpp -- see the header. Pure byte work; nothing here touches the SDK.
//
//  The scan is a single pass with one output position (`pos`, the first input byte not yet
//  written) and one search position (`scan`). Each event -- a <Story, <Spread or <Page open tag,
//  and inside a story its first <ParagraphStyleRange or its </Story> -- writes the input up to
//  that point, writes what the event calls for, and moves on. The first <Story also has the decoy
//  backing story written in front of it. Nothing is buffered.
//
//========================================================================================

// The plug-in builds every .cpp through the precompiled header; the offline test supplies an
// empty VCPlugInHeaders.h of its own (work/kcm-origin-test), so this line serves both builds.
#include "VCPlugInHeaders.h"

#include "KCMXmlInject.h"
#include <string.h>
#include <stdlib.h>				// strtod - the page size of <DocumentPreference>
#include <algorithm>			// sort - the element-count rows, largest difference first
#include <map>					// the element-name counts of the two sides

const char* const kKCMOriginUidLabelKey = "KcmOriginUid";
const char* const kKCMDummyStoryLabelKey = "KcmDummyStory";

namespace
{

const char* const kStoryOpen   = "<Story ";
const char* const kSpreadOpen  = "<Spread ";
const char* const kPageOpen    = "<Page ";		// the pages of <Spread> and <MasterSpread> alike (2026-09-13)
const char* const kStoryClose  = "</Story>";		// also closes the dummy story written below
const char* const kPropsOpen   = "<Properties>";
const char* const kLabelOpen   = "<Label>";
const char* const kSelfAttr    = "Self=\"";
// The sacrificial range, in pieces around the token the caller supplies: head, token, the break
// that ends the paragraph, the decoy table below, and the closing tags.
const char* const kDummyHead   =
	"<ParagraphStyleRange AppliedParagraphStyle=\"ParagraphStyle/$ID/NormalParagraphStyle\">"
	"<CharacterStyleRange AppliedCharacterStyle=\"CharacterStyle/$ID/[No character style]\">"
	"<Content>";
const char* const kDummyMid    = "</Content><Br />";
const char* const kDummyTail   = "</CharacterStyleRange></ParagraphStyleRange>";


// ★★★THE DUMMY STORY AND ITS FRAME (the header's 1.) - 2026-09-20, the user's design. An ORDINARY
// story, written once, right before the first real <Story, with an ordinary frame of its own on the
// first spread. It takes the drop, and then it is deleted outright (KCMRehydrate.cpp,
// DeleteDummyStory) - frame and story together, because deleting the frame takes the story with it.
//
// ⚠**THE FRAME SITS FAR OUT ON THE PASTEBOARD** (ItemTransform's last two numbers): if a deletion
//  ever fails, a frame out there is on no page, so it cannot print, cannot rasterise into a Pixel
//  comparison and cannot be mistaken for the reader's own work. It is the cheap insurance against
//  the one failure this design can have.
// ⚠**ALL FOUR PathPoints CARRY LeftDirection AND RightDirection.** Anchor alone opens, but the
//  frame comes out collapsed (measured 2026-09-15: 47.98mm -> 3.53mm), which would be a frame of a
//  different size from the one written here - and a thing that "opens" is not the same as a thing
//  that is right.
// ⚠The two Selfs are uids no document reaches; the import renumbers them anyway.
// ★★★AND IT CARRIES A LABEL, WHICH IS THE ONLY WAY IT CAN BE FOUND AFTERWARDS (2026-09-20,
// measured): **the dummy is EMPTIED by the import** - that is its whole job - so the token is gone
// from it by the time anything looks, and a search for the token finds nothing. The label survives:
// the import keeps the labels of stories and spreads (only pages lose theirs, 2. below). So the
// deletion asks for the label, never for the words.
const char* const kDummyStoryOpen =
	"<Story Self=\"u7ffffff8\" AppliedTOCStyle=\"n\" UserText=\"true\" IsEndnoteStory=\"false\""
	" TrackChanges=\"false\" StoryTitle=\"$ID/\" AppliedNamedGrid=\"n\">"
	"<Properties><Label><KeyValuePair Key=\"KcmDummyStory\" Value=\"1\" /></Label></Properties>";
const char* const kDummyFrame =
	"<TextFrame Self=\"u7ffffff7\" ParentStory=\"u7ffffff8\" ContentType=\"TextType\""
	" ItemTransform=\"1 0 0 1 -5000 -5000\">"
	"<Properties><PathGeometry><GeometryPath PathOpen=\"false\"><PathPointArray>"
	"<PathPoint Anchor=\"0 0\" LeftDirection=\"0 0\" RightDirection=\"0 0\"/>"
	"<PathPoint Anchor=\"0 50\" LeftDirection=\"0 50\" RightDirection=\"0 50\"/>"
	"<PathPoint Anchor=\"100 50\" LeftDirection=\"100 50\" RightDirection=\"100 50\"/>"
	"<PathPoint Anchor=\"100 0\" LeftDirection=\"100 0\" RightDirection=\"100 0\"/>"
	"</PathPointArray></GeometryPath></PathGeometry></Properties>"
	"</TextFrame>";
/** The frame goes in front of this, so that every <Page> and every page item of the first spread
    is already written. ⚠"</Spread>" is NOT a substring of "</MasterSpread>", so a master spread
    cannot be mistaken for the first spread here. */
const char* const kSpreadClose = "</Spread>";

bool16 StartsWith(const char* xml, size_t size, size_t at, const char* literal)
{
	const size_t n = ::strlen(literal);
	return (at + n <= size && ::memcmp(xml + at, literal, n) == 0) ? kTrue : kFalse;
}

/** The index of `literal` at or after `from`, or size when absent. */
size_t Find(const char* xml, size_t size, size_t from, const char* literal)
{
	const size_t n = ::strlen(literal);
	if (n == 0 || size < n)
		return size;
	for (size_t i = from; i + n <= size; ++i)
		if (xml[i] == literal[0] && ::memcmp(xml + i, literal, n) == 0)
			return i;
	return size;
}

size_t SkipSpace(const char* xml, size_t size, size_t at)
{
	while (at < size && (xml[at] == ' ' || xml[at] == '\t' || xml[at] == '\r' || xml[at] == '\n'))
		++at;
	return at;
}

bool16 Emit(KCMByteSink& out, const char* bytes, size_t count)
{
	return (count == 0) ? kTrue : out.Write(bytes, count);
}

bool16 EmitLiteral(KCMByteSink& out, const char* literal)
{
	return Emit(out, literal, ::strlen(literal));
}

/** Writes <KeyValuePair Key="KcmOriginUid" Value="<self>" /> */
bool16 EmitPair(KCMByteSink& out, const char* self, size_t selfLen)
{
	return EmitLiteral(out, "<KeyValuePair Key=\"") && EmitLiteral(out, kKCMOriginUidLabelKey)
		&& EmitLiteral(out, "\" Value=\"") && Emit(out, self, selfLen) && EmitLiteral(out, "\" />");
}

/** Right after an open tag (pos is the index just past its '>'): write the label into the element,
    merging with a <Properties> / <Label> that is already there. Advances `pos` past whatever it
    consumed of the input. kFalse when the sink refused. */
bool16 EmitLabel(const char* xml, size_t size, size_t& pos, KCMByteSink& out, const char* self, size_t selfLen)
{
	const size_t next = SkipSpace(xml, size, pos);
	if (StartsWith(xml, size, next, kPropsOpen))
	{
		// keep the bytes up to and including <Properties>
		const size_t afterProps = next + ::strlen(kPropsOpen);
		if (!Emit(out, xml + pos, afterProps - pos))
			return kFalse;
		pos = afterProps;
		const size_t inner = SkipSpace(xml, size, pos);
		if (StartsWith(xml, size, inner, kLabelOpen))
		{
			const size_t afterLabel = inner + ::strlen(kLabelOpen);
			if (!Emit(out, xml + pos, afterLabel - pos))
				return kFalse;
			pos = afterLabel;
			return EmitPair(out, self, selfLen);
		}
		return EmitLiteral(out, kLabelOpen) && EmitPair(out, self, selfLen) && EmitLiteral(out, "</Label>");
	}
	return EmitLiteral(out, kPropsOpen) && EmitLiteral(out, kLabelOpen) && EmitPair(out, self, selfLen)
		&& EmitLiteral(out, "</Label></Properties>");
}

}	// namespace

namespace
{

/** kTrue for a character an XML element name may begin with. */
bool16 IsNameStart(char c)
{
	return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') ? kTrue : kFalse;
}

/** kTrue for a character an XML element name may continue with. */
bool16 IsNameChar(char c)
{
	return (IsNameStart(c) || (c >= '0' && c <= '9') || c == ':' || c == '.' || c == '-')
		? kTrue : kFalse;
}

/** Count the element names of xml[0..size) into out. An opening tag only: "</x", "<?x", "<!x" and
    "< " are all skipped, so an element is counted once however it ends. */
void CountElementNames(const char* xml, size_t size, std::map<std::string, int32>& out)
{
	size_t i = 0;
	while (i + 1 < size)
	{
		if (xml[i] != '<' || !IsNameStart(xml[i + 1]))
		{
			++i;
			continue;
		}
		size_t j = i + 1;
		while (j < size && IsNameChar(xml[j]))
			++j;
		out[std::string(xml + i + 1, j - i - 1)] += 1;
		i = j;
	}
}

/** Largest difference first; ties by name, so the same input always reads the same way. */
bool16 BySizeOfDifference(const KCMElementCount& l, const KCMElementCount& r)
{
	const int32 dl = (l.fInB > l.fInA) ? (l.fInB - l.fInA) : (l.fInA - l.fInB);
	const int32 dr = (r.fInB > r.fInA) ? (r.fInB - r.fInA) : (r.fInA - r.fInB);
	if (dl != dr)
		return (dl > dr) ? true : false;
	return (l.fName < r.fName) ? true : false;
}

}	// namespace

bool16 KCMCompareElementCounts(const char* a, size_t aSize, const char* b, size_t bSize,
							   std::vector<KCMElementCount>& outDiffs, int32* outSame)
{
	outDiffs.clear();
	if (outSame != nil)
		*outSame = 0;
	if (a == nil || b == nil)
		return kFalse;

	std::map<std::string, int32> inA, inB;
	CountElementNames(a, aSize, inA);
	CountElementNames(b, bSize, inB);

	// ⚠**Language is left out, and the header says why**: 66 in a document just created, 1 in one
	//   opened from a file. It reports how a document was OPENED, not what is in it - the same
	//   reason the Resources mode drops it.
	static const char* const kIgnored = "Language";

	int32 same = 0;
	for (std::map<std::string, int32>::const_iterator it = inA.begin(); it != inA.end(); ++it)
	{
		if (it->first == kIgnored)
			continue;
		const std::map<std::string, int32>::const_iterator other = inB.find(it->first);
		const int32 countB = (other != inB.end()) ? other->second : 0;
		if (countB == it->second)
		{
			++same;
			continue;
		}
		KCMElementCount row;
		row.fName = it->first;
		row.fInA = it->second;
		row.fInB = countB;
		outDiffs.push_back(row);
	}
	// ...and the names that are in B alone (nothing in the loop above could have seen them).
	for (std::map<std::string, int32>::const_iterator it = inB.begin(); it != inB.end(); ++it)
	{
		if (it->first == kIgnored || inA.find(it->first) != inA.end())
			continue;
		KCMElementCount row;
		row.fName = it->first;
		row.fInA = 0;
		row.fInB = it->second;
		outDiffs.push_back(row);
	}

	std::sort(outDiffs.begin(), outDiffs.end(), BySizeOfDifference);
	if (outSame != nil)
		*outSame = same;
	return outDiffs.empty() ? kTrue : kFalse;
}

bool16 KCMCollectSpreadPages(const char* xml, size_t size, std::vector<KCMXmlSpreadPages>& out)
{
	out.clear();
	if (xml == nil)
		return kFalse;

	const char* const kSpreadClose = "</Spread>";
	bool16 inSpread = kFalse;
	size_t scan = 0;
	while (scan < size)
	{
		const size_t spread = Find(xml, size, scan, kSpreadOpen);
		const size_t page   = Find(xml, size, scan, kPageOpen);
		const size_t close  = Find(xml, size, scan, kSpreadClose);
		size_t next = spread;
		if (page < next)  next = page;
		if (close < next) next = close;
		if (next >= size)
			break;

		if (next == close)
		{
			inSpread = kFalse;
			scan = next + 1;
			continue;
		}

		const size_t tagEnd = Find(xml, size, next, ">");
		if (tagEnd >= size)
			break;								// malformed: no closing '>' - nothing more to read
		const size_t selfAt = Find(xml, size, next, kSelfAttr);
		uint32 uid = 0;
		bool16 haveSelf = kFalse;
		if (selfAt < tagEnd)
		{
			const size_t selfBegin = selfAt + ::strlen(kSelfAttr);
			const size_t selfEnd = Find(xml, size, selfBegin, "\"");
			if (selfEnd < tagEnd)
				haveSelf = KCMParseSelfUid(xml + selfBegin, selfEnd - selfBegin, uid);
		}

		if (next == spread)
		{
			// A spread with no Self takes no pages; a self-closing one (<Spread .../>) has none
			// and must not be left "open", or the next <Page in the file - a master's, say -
			// would be attributed to it.
			const bool16 selfClosing = (tagEnd > 0 && xml[tagEnd - 1] == '/') ? kTrue : kFalse;
			inSpread = (haveSelf && !selfClosing) ? kTrue : kFalse;
			if (haveSelf)
			{
				KCMXmlSpreadPages entry;
				entry.fSpread = uid;
				out.push_back(entry);
			}
		}
		else if (inSpread && haveSelf)
		{
			out.back().fPages.push_back(uid);	// a page (self-closing or not - its identity is its Self)
		}
		scan = tagEnd + 1;
	}
	return kTrue;
}

bool16 KCMParseSelfUid(const char* text, size_t length, uint32& outUid)
{
	if (text == nil || length < 2 || text[0] != 'u')
		return kFalse;
	uint32 value = 0;
	for (size_t i = 1; i < length; ++i)
	{
		const char c = text[i];
		uint32 digit = 0;
		if (c >= '0' && c <= '9')      digit = static_cast<uint32>(c - '0');
		else if (c >= 'a' && c <= 'f') digit = static_cast<uint32>(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') digit = static_cast<uint32>(c - 'A' + 10);
		else return kFalse;
		if (value > 0x0FFFFFFF)
			return kFalse;		// more than 32 bits
		value = (value << 4) | digit;
	}
	outUid = value;
	return kTrue;
}

namespace
{

/** The value of attribute `name` inside tag [tagStart, tagEnd), or kFalse. The value is the bytes
    between the quotes, not copied. */
bool16 AttributeOf(const char* xml, size_t tagStart, size_t tagEnd, const char* name,
				   size_t& outValueBegin, size_t& outValueEnd)
{
	const size_t n = ::strlen(name);
	for (size_t i = tagStart; i + n + 2 < tagEnd; ++i)
	{
		// the name must start an attribute: preceded by a space, followed by ="
		if (xml[i - 1] != ' ' && xml[i - 1] != '\t' && xml[i - 1] != '\n' && xml[i - 1] != '\r')
			continue;
		if (::memcmp(xml + i, name, n) != 0 || xml[i + n] != '=' || xml[i + n + 1] != '"')
			continue;
		const size_t begin = i + n + 2;
		size_t end = begin;
		while (end < tagEnd && xml[end] != '"')
			++end;
		if (end >= tagEnd)
			return kFalse;
		outValueBegin = begin;
		outValueEnd = end;
		return kTrue;
	}
	return kFalse;
}

bool16 ValueIs(const char* xml, size_t begin, size_t end, const char* literal)
{
	const size_t n = ::strlen(literal);
	return (end - begin == n && ::memcmp(xml + begin, literal, n) == 0) ? kTrue : kFalse;
}

/** A positive number from the attribute's bytes (at most 63 of them). */
bool16 NumberOf(const char* xml, size_t begin, size_t end, double& out)
{
	if (end <= begin || end - begin > 63)
		return kFalse;
	char buf[64];
	::memcpy(buf, xml + begin, end - begin);
	buf[end - begin] = '\0';
	char* stop = nil;
	const double v = ::strtod(buf, &stop);
	if (stop == buf || v <= 0.0)
		return kFalse;
	out = v;
	return kTrue;
}

}	// namespace

bool16 KCMReadDocumentPreference(const char* xml, size_t size, KCMDocSetupFromXml& out)
{
	if (xml == nil)
		return kFalse;
	const size_t tag = Find(xml, size, 0, "<DocumentPreference ");
	if (tag >= size)
		return kFalse;
	const size_t tagEnd = Find(xml, size, tag, ">");
	if (tagEnd >= size)
		return kFalse;
	const size_t attrsFrom = tag + 1;		// past '<', so the name test can look one byte back

	size_t b = 0, e = 0;
	double w = 0, h = 0;
	if (AttributeOf(xml, attrsFrom, tagEnd, "PageWidth", b, e) && NumberOf(xml, b, e, w)
		&& AttributeOf(xml, attrsFrom, tagEnd, "PageHeight", b, e) && NumberOf(xml, b, e, h))
	{
		out.fPageWidth = w;
		out.fPageHeight = h;
		out.fHasSize = kTrue;
	}
	if (AttributeOf(xml, attrsFrom, tagEnd, "FacingPages", b, e))
	{
		if (ValueIs(xml, b, e, "true"))       { out.fFacingPages = kTrue;  out.fHasFacing = kTrue; }
		else if (ValueIs(xml, b, e, "false")) { out.fFacingPages = kFalse; out.fHasFacing = kTrue; }
	}
	if (AttributeOf(xml, attrsFrom, tagEnd, "PageBinding", b, e))
	{
		if (ValueIs(xml, b, e, "RightToLeft"))      { out.fBinding = 1;  out.fHasBinding = kTrue; }
		else if (ValueIs(xml, b, e, "LeftToRight")) { out.fBinding = 0;  out.fHasBinding = kTrue; }
		else if (ValueIs(xml, b, e, "DefaultValue")){ out.fBinding = -1; out.fHasBinding = kTrue; }
	}
	return kTrue;
}

// ★★★THE DECOY TABLE (2026-09-21, the user's design), WRITTEN INSIDE THE DUMMY STORY. Measured the
// same day on twenty-eight documents (docs/ai-notes/kcm-inx-roundtrip-and-nested-tables-2026-09-20.md
// §11, §12): when the document holds a table, the import takes a SECOND bite, and it lands on the
// first <Content> runs of the FIRST table in file order - up to two of them.
//   ⚠It is not per cell, per row or per table: a three-column table of six filled cells lost two
//    runs, a cell holding two paragraphs lost both of its own, a table holding a single run lost
//    that one and stopped, and the second and third tables of a story came back whole.
//   ⚠It is ONCE PER IMPORT, not once per story: with a table in each of two stories, only the
//    table of the first story in FILE ORDER lost anything. That is why ONE decoy is enough, and why
//    it belongs here - the dummy story is already first, and it is already deleted afterwards
//    (KCMRehydrate.cpp, DeleteDummyStory), so this costs no new clean-up.
//   ★★★AND IT IS WHY A NESTED TABLE SEEMED TO VANISH: a nested table in the first cell of the
//    first table IS what stands at the bitten position, so the bite took the whole subtree. The
//    same nested table placed in the SECOND table came back entire. ⇒ "ImportINX drops nested
//    tables" was never the rule; the position was.
//   ★★★**AND THE BITE IS TAKEN AT EVERY DEPTH**: with a flat decoy a nested table came back with
//    its first two cells empty, and with a two-level decoy a THREE-level table lost the innermost
//    cell's text (measured). ⇒ **the decoy is nested as deep as the origin's deepest table**, which
//    is what DecoyDepthFor measures. A document with no table gets one flat decoy, which costs
//    nothing and keeps the shape of this code the same in both cases.
//   ⚠FOUR runs per level, not two: the measured bite is "up to two", and the decoy <XmlStory> this
//    design replaced needed two ranges where one absorbed nothing - never explained. Two spare runs
//    cost nothing, because the whole story goes.
//   ⚠The Selfs are uids no document reaches (the import renumbers them anyway), and they must not
//    collide with the dummy story's own u7ffffff8 / u7ffffff7.

/** How deep the origin's tables nest: 1 for a document with a plain table, 2 when a table stands in
    a cell, 0 when there is no table at all. ★The decoy is built to this depth, so the bite at every
    level lands on the decoy rather than on the reader's table. */
int32 DecoyDepthFor(const char* xml, size_t size)
{
    int32 depth = 0, deepest = 0;
    size_t at = 0;
    while (at + 1 < size)
    {
        if (xml[at] == '<')
        {
            if (StartsWith(xml, size, at, "<Table "))
            {
                ++depth;
                if (depth > deepest)
                    deepest = depth;
            }
            else if (StartsWith(xml, size, at, "</Table>") && depth > 0)
                --depth;
        }
        ++at;
    }
    // ⚠**CLAMPED, BECAUSE THE DECOY'S UIDS ARE HEX** (found in review): DecoyName writes one hex
    //  digit per level, so a document nesting deeper than 15 would produce a uid that is not one.
    //  Eight levels of nested tables is already past anything a page carries, and the levels past
    //  the clamp simply go unprotected rather than writing a broken document.
    const int32 kDeepestDecoy = 8;
    return (deepest > kDeepestDecoy) ? kDeepestDecoy : deepest;
}

/** The cell that holds the next level down: text of its own, then the nested decoy table. */
bool16 EmitDecoyTable(KCMByteSink& out, int32 level, int32 maxLevel);

/** "u7fffe" + the level and cell in hex, without <cstdio> (sprintf is an error in this build). */
void DecoyName(char* buf, const char* prefix, int32 a, const char* mid, int32 b)
{
    size_t at = 0;
    for (const char* p = prefix; *p != 0; ++p) buf[at++] = *p;
    buf[at++] = (char)((a < 10) ? ('0' + a) : ('a' + a - 10));
    if (mid != nil)
    {
        for (const char* p = mid; *p != 0; ++p) buf[at++] = *p;
        buf[at++] = (char)('0' + b);
    }
    buf[at] = 0;
}

bool16 EmitDecoyCell(KCMByteSink& out, int32 level, int32 cell, int32 maxLevel, bool16 withNested)
{
    // Name is "column:row" - the same spelling the application writes (measured on a real table).
    const char* const kNames[] = { "0:0", "1:0", "0:1", "1:1" };
    char self[32];
    DecoyName(self, "u7fffe", level, "i", cell);
    char token[32];
    DecoyName(token, "KCMDECOY-L", level, "-C", cell);
    if (!EmitLiteral(out, "<Cell Self=\"") || !EmitLiteral(out, self)
        || !EmitLiteral(out, "\" Name=\"") || !EmitLiteral(out, kNames[cell])
        || !EmitLiteral(out, "\" RowSpan=\"1\" ColumnSpan=\"1\" CellType=\"TextTypeCell\""
                             " AppliedCellStyle=\"CellStyle/$ID/[None]\">"
                             "<ParagraphStyleRange AppliedParagraphStyle=\"ParagraphStyle/$ID/NormalParagraphStyle\">"
                             "<CharacterStyleRange AppliedCharacterStyle=\"CharacterStyle/$ID/[No character style]\">"
                             "<Content>")
        || !EmitLiteral(out, token) || !EmitLiteral(out, "</Content>"))
        return kFalse;
    if (withNested && level < maxLevel)
    {
        if (!EmitLiteral(out, "<Br />") || !EmitDecoyTable(out, level + 1, maxLevel))
            return kFalse;
    }
    return EmitLiteral(out, "</CharacterStyleRange></ParagraphStyleRange></Cell>");
}

bool16 EmitDecoyTable(KCMByteSink& out, int32 level, int32 maxLevel)
{
    char self[24];
    DecoyName(self, "u7fffe", level, nil, 0);
    if (!EmitLiteral(out, "<Table Self=\"") || !EmitLiteral(out, self)
        || !EmitLiteral(out, "\" HeaderRowCount=\"0\" FooterRowCount=\"0\" BodyRowCount=\"2\""
                             " ColumnCount=\"2\" AppliedTableStyle=\"TableStyle/$ID/[No table style]\""
                             " TableDirection=\"LeftToRightDirection\">"))
        return kFalse;
    for (int32 r = 0; r < 2; ++r)
    {
        char row[32];
        DecoyName(row, "u7fffe", level, "Row", r);
        if (!EmitLiteral(out, "<Row Self=\"") || !EmitLiteral(out, row)
            || !EmitLiteral(out, "\" Name=\"") || !EmitLiteral(out, (r == 0) ? "0" : "1")
            || !EmitLiteral(out, "\" SingleRowHeight=\"12\" />"))
            return kFalse;
    }
    for (int32 c = 0; c < 2; ++c)
    {
        char col[32];
        DecoyName(col, "u7fffe", level, "Column", c);
        if (!EmitLiteral(out, "<Column Self=\"") || !EmitLiteral(out, col)
            || !EmitLiteral(out, "\" Name=\"") || !EmitLiteral(out, (c == 0) ? "0" : "1")
            || !EmitLiteral(out, "\" SingleColumnWidth=\"40\" />"))
            return kFalse;
    }
    for (int32 cell = 0; cell < 4; ++cell)
        if (!EmitDecoyCell(out, level, cell, maxLevel, (cell == 0) ? kTrue : kFalse))
            return kFalse;
    return EmitLiteral(out, "</Table>");
}

bool16 KCMInjectForRehydration(const char* xml, size_t size, const char* sacrificialText,
							   KCMByteSink& out, int32* outStories, int32* outSpreads, int32* outPages)
{
	int32 stories = 0, spreads = 0, pages = 0;
	if (outStories) *outStories = 0;
	if (outSpreads) *outSpreads = 0;
	if (outPages)   *outPages = 0;
	if (xml == nil || sacrificialText == nil || sacrificialText[0] == '\0')
		return kFalse;

	size_t pos = 0;			// the first input byte not yet written
	size_t scan = 0;		// where the search for the next event starts
	bool16 frameWritten = kFalse;		// the dummy's frame goes at the end of the FIRST spread

	while (scan < size)
	{
		const size_t story  = Find(xml, size, scan, kStoryOpen);
		const size_t spread = Find(xml, size, scan, kSpreadOpen);
		const size_t page   = Find(xml, size, scan, kPageOpen);
		// Only looked for while it is still wanted: the search is a scan of the remaining bytes,
		// and after the frame is placed there is nothing to find it for.
		const size_t spreadEnd = frameWritten ? size : Find(xml, size, scan, kSpreadClose);

		// the nearest event decides
		size_t next = story;
		if (spread < next)    next = spread;
		if (page < next)      next = page;
		if (spreadEnd < next) next = spreadEnd;
		if (next >= size)
			break;

		// 1a. the dummy's FRAME, once, at the end of the first spread - after every <Page> and
		//     every page item that spread holds (the literal says why it sits on the pasteboard).
		if (!frameWritten && next == spreadEnd)
		{
			if (!Emit(out, xml + pos, next - pos) || !EmitLiteral(out, kDummyFrame))
				return kFalse;
			pos = next;
			scan = next + 1;
			frameWritten = kTrue;
			continue;
		}

		// a <Story, <Spread or <Page open tag
		const bool16 isStory = (next == story) ? kTrue : kFalse;
		const bool16 isPage  = (next == page) ? kTrue : kFalse;
		const size_t tagEnd = Find(xml, size, next, ">");
		if (tagEnd >= size)
			return kFalse;					// malformed: no closing '>'
		const size_t selfAt = Find(xml, size, next, kSelfAttr);
		if (selfAt >= tagEnd)
		{
			scan = tagEnd + 1;				// no Self: leave the element alone
			continue;
		}
		const size_t selfBegin = selfAt + ::strlen(kSelfAttr);
		const size_t selfEnd = Find(xml, size, selfBegin, "\"");
		if (selfEnd >= tagEnd || xml[tagEnd - 1] == '/')
		{
			scan = tagEnd + 1;				// self-closing, or an unterminated Self: leave it alone
			continue;
		}

		// 1b. the DUMMY STORY, once, in front of the first real story. The real <XmlStory> is the
		//     first text insertion in file order, so this one is the SECOND - which is the one the
		//     import swallows. It is swallowed in place of the reader's first story, and what is
		//     left of it is deleted afterwards, frame and all (KCMRehydrate.cpp, DeleteDummyStory).
		//     ★AND IT CARRIES THE DECOY TABLE (kDecoyTable, 2026-09-21): the import bites a second
		//     time when the document holds a table, and this makes the dummy's table the first one
		//     in file order, so that bite lands here instead of on the reader's first table.
		if (isStory && stories == 0)
		{
			const int32 decoyDepth = DecoyDepthFor(xml, size);
			if (!Emit(out, xml + pos, next - pos) || !EmitLiteral(out, kDummyStoryOpen)
				|| !EmitLiteral(out, kDummyHead) || !EmitLiteral(out, sacrificialText)
				|| !EmitLiteral(out, kDummyMid)
				|| !EmitDecoyTable(out, 1, (decoyDepth > 0) ? decoyDepth : 1)
				|| !EmitLiteral(out, kDummyTail) || !EmitLiteral(out, kStoryClose))
				return kFalse;
			pos = next;
		}

		if (!Emit(out, xml + pos, tagEnd + 1 - pos))
			return kFalse;
		pos = tagEnd + 1;
		if (!EmitLabel(xml, size, pos, out, xml + selfBegin, selfEnd - selfBegin))
			return kFalse;
		scan = pos;
		if (isStory)     { ++stories; }
		else if (isPage) { ++pages; }
		else             { ++spreads; }
	}

	if (!Emit(out, xml + pos, size - pos))
		return kFalse;
	if (outStories) *outStories = stories;
	if (outSpreads) *outSpreads = spreads;
	if (outPages)   *outPages = pages;
	return kTrue;
}

// End, KCMXmlInject.cpp.
