//========================================================================================
//
//  KCMXmlDeepCompare.cpp
//
//  The deep half of the round-trip check. The reasoning is at the head of KCMXmlDeepCompare.h.
//
//  No SDK type appears here on purpose (work/kcm-origin-test compiles it outside InDesign), so the
//  file leans on <string>, <vector> and the C library only.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "KCMXmlDeepCompare.h"

#include <cstdlib>
#include <cstring>

namespace
{

/** One element, flattened out of the document in the order it is written. */
struct Node
{
	Node() : fDepth(0), fSelfClosing(false) {}
	std::string											fName;
	std::vector<std::pair<std::string, std::string> >	fAttrs;
	std::string											fText;		//!< the text written directly inside it
	std::string											fPath;		//!< Story[2]/ParagraphStyleRange[1]/...
	int32												fDepth;
	bool												fSelfClosing;
};

bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool IsNameChar(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
		|| c == '_' || c == '-' || c == '.' || c == ':';
}

/** An index as text, without <cstdio>: sprintf raises C4996 in this build, where warnings are
    errors, and a five-line loop is cheaper than an exception to the rule. */
std::string IndexText(int32 n)
{
	char buf[16];
	int32 at = (int32)sizeof(buf);
	buf[--at] = 0;
	if (n <= 0)
		buf[--at] = '0';
	while (n > 0 && at > 0)
	{
		buf[--at] = (char)('0' + (n % 10));
		n /= 10;
	}
	return std::string(buf + at);
}

/** Trim the whitespace an IDML file indents its elements with. The text that matters (a <Content>'s)
    may begin or end with a real space, but IDML writes no indentation inside <Content>, so this only
    ever touches text standing between elements - which is indentation by definition. */
std::string TrimIndent(const std::string& s)
{
	size_t b = 0, e = s.size();
	while (b < e && IsSpace(s[b])) ++b;
	while (e > b && IsSpace(s[e - 1])) --e;
	return s.substr(b, e - b);
}

/** Flatten a designmap into elements in document order, each with its attributes, its own text and
    the path that leads to it. A comment, a PI and a DOCTYPE are skipped: none of them is content. */
void Flatten(const char* xml, size_t size, std::vector<Node>& out)
{
	out.clear();
	if (xml == nil || size == 0)
		return;
	out.reserve(4096);

	// The open elements, and for each level how many children of each name it has had so far -
	// which is what turns a name into a path a reader can find again (Cell[3], not "some Cell").
	std::vector<size_t> open;
	std::vector<std::vector<std::pair<std::string, int32> > > childCounts;
	childCounts.push_back(std::vector<std::pair<std::string, int32> >());

	size_t pos = 0;
	while (pos < size)
	{
		if (xml[pos] != '<')
		{
			const size_t begin = pos;
			while (pos < size && xml[pos] != '<') ++pos;
			if (!open.empty())
			{
				const std::string piece = TrimIndent(std::string(xml + begin, pos - begin));
				if (!piece.empty())
					out[open.back()].fText += piece;
			}
			continue;
		}
		if (pos + 1 < size && (xml[pos + 1] == '!' || xml[pos + 1] == '?'))
		{
			const char* end = (xml[pos + 1] == '?') ? "?>" : ">";
			const size_t n = ::strlen(end);
			size_t j = pos + 2;
			while (j + n <= size && ::memcmp(xml + j, end, n) != 0) ++j;
			pos = (j + n <= size) ? j + n : size;
			continue;
		}
		if (pos + 1 < size && xml[pos + 1] == '/')				// a closing tag
		{
			while (pos < size && xml[pos] != '>') ++pos;
			if (pos < size) ++pos;
			if (!open.empty())
			{
				open.pop_back();
				if (childCounts.size() > 1)
					childCounts.pop_back();
			}
			continue;
		}

		// An open (or self-closing) tag.
		size_t j = pos + 1;
		const size_t nameBegin = j;
		while (j < size && IsNameChar(xml[j])) ++j;
		Node node;
		node.fName.assign(xml + nameBegin, j - nameBegin);
		node.fDepth = (int32)open.size();

		while (j < size && xml[j] != '>' && xml[j] != '/')
		{
			while (j < size && IsSpace(xml[j])) ++j;
			if (j < size && (xml[j] == '/' || xml[j] == '>'))
				break;
			const size_t keyBegin = j;
			while (j < size && IsNameChar(xml[j])) ++j;
			if (j == keyBegin)			// something we do not understand: step over it
			{
				++j;
				continue;
			}
			const std::string key(xml + keyBegin, j - keyBegin);
			while (j < size && IsSpace(xml[j])) ++j;
			std::string value;
			if (j < size && xml[j] == '=')
			{
				++j;
				while (j < size && IsSpace(xml[j])) ++j;
				if (j < size && (xml[j] == '"' || xml[j] == '\''))
				{
					const char quote = xml[j++];
					const size_t valueBegin = j;
					while (j < size && xml[j] != quote) ++j;
					value.assign(xml + valueBegin, j - valueBegin);
					if (j < size) ++j;
				}
			}
			node.fAttrs.push_back(std::make_pair(key, value));
		}
		// ⚠**THE SLASH FIRST, THEN THE BRACKET.** Reading the character before '>' instead was a
		//  defect found in review: the attribute loop stops ON the '/', so "the one before" is the
		//  end of the last attribute and every self-closing element came out as an open one.
		if (j < size && xml[j] == '/')
		{
			node.fSelfClosing = true;
			++j;
		}
		while (j < size && xml[j] != '>') ++j;
		if (j < size) ++j;										// past '>'

		int32 index = 1;
		{
			std::vector<std::pair<std::string, int32> >& siblings = childCounts.back();
			bool found = false;
			for (size_t k = 0; k < siblings.size(); ++k)
				if (siblings[k].first == node.fName)
				{
					index = ++siblings[k].second;
					found = true;
					break;
				}
			if (!found)
				siblings.push_back(std::make_pair(node.fName, 1));
		}
		node.fPath = (open.empty() ? std::string() : out[open.back()].fPath + "/");
		node.fPath += node.fName;
		node.fPath += "[";
		node.fPath += IndexText(index);
		node.fPath += "]";

		out.push_back(node);
		if (!node.fSelfClosing)
		{
			open.push_back(out.size() - 1);
			childCounts.push_back(std::vector<std::pair<std::string, int32> >());
		}
		pos = j;
	}
}

/** Both values numeric, and equal to within a hair? IDML writes doubles, and "12" and "12.0" are the
    same row height. ⚠Only when BOTH parse whole - "12pt" is text and compares as text. */
bool SameNumber(const std::string& a, const std::string& b)
{
	if (a.empty() || b.empty())
		return false;
	char* endA = nil;
	char* endB = nil;
	const double va = ::strtod(a.c_str(), &endA);
	const double vb = ::strtod(b.c_str(), &endB);
	if (endA == nil || *endA != 0 || endB == nil || *endB != 0)
		return false;
	const double diff = (va > vb) ? (va - vb) : (vb - va);
	const double aa = (va < 0) ? -va : va;
	const double ab = (vb < 0) ? -vb : vb;
	const double scale = (aa > ab) ? aa : ab;
	return diff <= 1e-9 + 1e-9 * scale;
}

const char* const kOurLabelValues[] = { "KcmOriginUid", "KcmDummyStory" };

/** The attribute names measured to appear in the copy alone, with the application's default, where
    the origin inherited them (the header's 6.). ⚠The list grows only with a measurement beside it. */
const char* const kImportWritesOut[] =
	{ "TopInset", "LeftInset", "BottomInset", "RightInset", "ClipContentToCell" };

bool IsImportDefaultName(const std::string& key)
{
	for (size_t i = 0; i < sizeof(kImportWritesOut) / sizeof(kImportWritesOut[0]); ++i)
		if (key == kImportWritesOut[i])
			return true;
	return false;
}

/** Nothing but U+FEFF (EF BB BF in UTF-8), and not empty: the backing store's tag markers. */
bool IsMarkerOnly(const std::string& s)
{
	if (s.empty() || (s.size() % 3) != 0)
		return false;
	for (size_t i = 0; i + 2 < s.size() + 1; i += 3)
		if ((unsigned char)s[i] != 0xEF || (unsigned char)s[i + 1] != 0xBB
			|| (unsigned char)s[i + 2] != 0xBF)
			return false;
	return true;
}

bool IsOurLabelValue(const std::string& value)
{
	for (size_t i = 0; i < sizeof(kOurLabelValues) / sizeof(kOurLabelValues[0]); ++i)
		if (value == kOurLabelValues[i])
			return true;
	return false;
}

/** The end of this element's subtree in the flat list: everything after it that is deeper. */
size_t SubtreeEnd(const std::vector<Node>& nodes, size_t at)
{
	const int32 depth = nodes[at].fDepth;
	size_t j = at + 1;
	while (j < nodes.size() && nodes[j].fDepth > depth) ++j;
	return j;
}

/** ★★**IS THIS THE <Properties> / <Label> / <KeyValuePair> KCM ITSELF WROTE?** The copy carries
    elements the origin does not (KCMXmlInject.h: the label that names the original uid), so a
    lockstep walk MUST be able to step over them - that was a defect found in review: without this
    the deep pass diverged at the first label and compared nothing at all.
    ⚠It is not "any Properties": the subtree has to carry one of OUR keys. A reader's own script
     label goes through the ordinary comparison, which is the whole point of asking. */
bool IsOurLabelSubtree(const std::vector<Node>& nodes, size_t at)
{
	const std::string& name = nodes[at].fName;
	if (name != "Properties" && name != "Label" && name != "KeyValuePair")
		return false;
	const size_t end = SubtreeEnd(nodes, at);
	for (size_t i = at; i < end; ++i)
		for (size_t k = 0; k < nodes[i].fAttrs.size(); ++k)
			if (IsOurLabelValue(nodes[i].fAttrs[k].second))
				return true;
	return false;
}

/** Classify one difference. The header carries the reasoning for every kind but the first. */
int32 Classify(const std::string& elementName, const std::string& key,
			   const std::string& inA, const std::string& inB)
{
	if (elementName == "Language" || key == "Language")
		return kKCMXmlDiffLanguage;
	// ⚠**THE VALUE, NEVER THE KEY.** Forgiving the attribute NAME "Key" was a defect found in
	//  review: every label in the document is written with that name, the reader's own included.
	if (IsOurLabelValue(inA) || IsOurLabelValue(inB))
		return kKCMXmlDiffOurLabel;
	if (KCMLooksLikeUid(inA) && KCMLooksLikeUid(inB))
		return kKCMXmlDiffUid;
	// The window's own preferences in a document the rehydration created (the header's 5.).
	if (elementName == "ViewPreference")
		return kKCMXmlDiffNewDocPref;
	// An inherited attribute the import wrote out (the header's 6.) - ONLY when the origin has none.
	if (inA.empty() && !inB.empty() && IsImportDefaultName(key))
		return kKCMXmlDiffImportDefault;
	// Tag markers, and nothing else, on both sides (the header's 7.).
	if (IsMarkerOnly(inA) && IsMarkerOnly(inB))
		return kKCMXmlDiffMarkerOnly;
	// The Liquid Layout rule of a page the import created (the header's 8.). Only "Off" in the copy.
	if (elementName == "Page" && key == "LayoutRule" && inB == "Off")
		return kKCMXmlDiffNewPage;
	// ⚠A uid against nothing is NOT forgiven here. It could be a reference the import dropped, and
	//  the rule of this file is that a name is forgiven only where a measurement says to.
	return kKCMXmlDiffReal;
}

void Tally(KCMXmlDeepTally& t, int32 kind)
{
	switch (kind)
	{
		case kKCMXmlDiffUid:		++t.fUids;		break;
		case kKCMXmlDiffOurLabel:	++t.fOurLabels;	break;
		case kKCMXmlDiffLanguage:	++t.fLanguage;	break;
		case kKCMXmlDiffMetadata:	++t.fMetadata;	break;
		case kKCMXmlDiffNewDocPref:	++t.fNewDocPrefs;		break;
		case kKCMXmlDiffImportDefault:	++t.fImportDefaults;	break;
		case kKCMXmlDiffMarkerOnly:	++t.fMarkers;	break;
		case kKCMXmlDiffNewPage:	++t.fNewPages;	break;
		default:					++t.fRealDiffs;	break;
	}
}

/** Record a REAL difference (the classified ones live in the tally, so the cap cannot be eaten by
    renumbered uids - another review finding). */
void Record(std::vector<KCMXmlDifference>& out, int32 maxDiffs, const Node& node,
			const std::string& what, const std::string& inA, const std::string& inB)
{
	if (maxDiffs > 0 && (int32)out.size() >= maxDiffs)
		return;
	KCMXmlDifference row;
	row.fPath = node.fPath;
	row.fWhat = what;
	row.fInA = inA.empty() ? std::string("(absent)") : inA;
	row.fInB = inB.empty() ? std::string("(absent)") : inB;
	row.fKind = kKCMXmlDiffReal;
	out.push_back(row);
}

const std::string* Find(const std::vector<std::pair<std::string, std::string> >& attrs,
						const std::string& key)
{
	for (size_t i = 0; i < attrs.size(); ++i)
		if (attrs[i].first == key)
			return &attrs[i].second;
	return nil;
}

/** The value of the KcmOriginUid label inside this subtree, or empty when it has none. */
std::string OriginUidLabel(const std::vector<Node>& nodes, size_t at, size_t end)
{
	for (size_t i = at; i < end; ++i)
		if (nodes[i].fName == "KeyValuePair")
		{
			const std::string* key = Find(nodes[i].fAttrs, "Key");
			const std::string* value = Find(nodes[i].fAttrs, "Value");
			if (key != nil && value != nil && *key == "KcmOriginUid")
				return *value;
		}
	return std::string();
}

/** The top-level <Story> subtrees of a flattened document, in file order. */
void CollectStories(const std::vector<Node>& nodes, std::vector<std::pair<size_t, size_t> >& out)
{
	out.clear();
	for (size_t i = 0; i < nodes.size(); )
	{
		if (nodes[i].fName == "Story" && nodes[i].fDepth == 1)
		{
			const size_t end = SubtreeEnd(nodes, i);
			out.push_back(std::make_pair(i, end));
			i = end;
			continue;
		}
		++i;
	}
}

/** ★★★**PUT THE COPY'S STORIES BACK INTO THE ORIGIN'S ORDER** (the header says why). Does nothing
    unless every story on both sides can be paired: the same number of them, the origin's own Self
    known, and every copy story carrying its KcmOriginUid label. A document that cannot be paired is
    left exactly as it was, and the walk reports what it finds. */
void ReorderStoriesLikeOrigin(const std::vector<Node>& na, std::vector<Node>& nb, int32& outMoved)
{
	outMoved = 0;
	std::vector<std::pair<size_t, size_t> > inA, inB;
	CollectStories(na, inA);
	CollectStories(nb, inB);
	if (inA.size() < 2 || inA.size() != inB.size())
		return;						// one story cannot be out of order; a mismatch is not ours to fix

	// origin Self -> which copy story carries that label
	std::vector<std::string> wanted;
	for (size_t k = 0; k < inA.size(); ++k)
	{
		const std::string* self = Find(na[inA[k].first].fAttrs, "Self");
		if (self == nil)
			return;
		wanted.push_back(*self);
	}
	std::vector<size_t> pick;			// for each origin story, the index into inB
	for (size_t k = 0; k < wanted.size(); ++k)
	{
		size_t found = inB.size();
		for (size_t m = 0; m < inB.size(); ++m)
			if (OriginUidLabel(nb, inB[m].first, inB[m].second) == wanted[k])
			{
				found = m;
				break;
			}
		if (found == inB.size())
			return;						// a story without its label: leave everything alone
		pick.push_back(found);
	}
	bool anyMove = false;
	for (size_t k = 0; k < pick.size(); ++k)
		if (pick[k] != k)
			anyMove = true;
	if (!anyMove)
		return;

	// Rebuild the copy with the story blocks in the origin's order, everything else untouched.
	std::vector<Node> rebuilt;
	rebuilt.reserve(nb.size());
	size_t slot = 0;
	for (size_t i = 0; i < nb.size(); )
	{
		if (slot < inB.size() && i == inB[slot].first)
		{
			const std::pair<size_t, size_t>& src = inB[pick[slot]];
			for (size_t k = src.first; k < src.second; ++k)
				rebuilt.push_back(nb[k]);
			if (pick[slot] != slot)
				++outMoved;
			i = inB[slot].second;
			++slot;
			continue;
		}
		rebuilt.push_back(nb[i]);
		++i;
	}
	nb.swap(rebuilt);
}

}	// namespace

// KCMLooksLikeUid (declared in KCMXmlDeepCompare.h)
bool16 KCMLooksLikeUid(const std::string& value)
{
	// u + hex, then any number of tails: i<hex>, Row<digits>, Column<digits>.
	// ★**AND THE d-ROOTED FAMILY**: the document's own Self is "d" and the XML backing store's
	//   elements are "di2", "di3"... which the import renumbers exactly like a uid (measured
	//   2026-09-21: di2 against di3). One rule, two roots.
	if (value.size() < 2 || (value[0] != 'u' && value[0] != 'd'))
		return kFalse;
	size_t i = 1;
	size_t hex = 0;
	while (i < value.size())
	{
		const char c = value[i];
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
		{
			++hex;
			++i;
			continue;
		}
		// ⚠**i == 1 IS THE d-ROOT**: the document's own id is the single letter "d", so its children
		//  are "di2" with no hex digit in between - measured, and the reason this is not `hex > 0`
		//  alone (which left di2/di3 reported as a real difference).
		if (c == 'i' && (hex > 0 || i == 1))	// a nested uid: u101i119i0i123, or d's own child di2
		{
			++i;
			hex = 0;
			continue;
		}
		if (hex > 0 && (::strncmp(value.c_str() + i, "Row", 3) == 0
					 || ::strncmp(value.c_str() + i, "Column", 6) == 0))
		{
			i += (value[i] == 'R') ? 3 : 6;
			size_t digits = 0;
			while (i < value.size() && value[i] >= '0' && value[i] <= '9') { ++i; ++digits; }
			if (digits == 0)
				return kFalse;
			continue;
		}
		return kFalse;
	}
	return (hex > 0) ? kTrue : kFalse;
}

// KCMCompareXmlDeep (declared in KCMXmlDeepCompare.h)
bool16 KCMCompareXmlDeep(const char* a, size_t aSize, const char* b, size_t bSize,
						 std::vector<KCMXmlDifference>& outDiffs, KCMXmlDeepTally& outTally,
						 int32 maxDiffs)
{
	outDiffs.clear();
	outTally = KCMXmlDeepTally();
	if (a == nil || b == nil)
		return kFalse;

	std::vector<Node> na, nb;
	Flatten(a, aSize, na);
	Flatten(b, bSize, nb);
	ReorderStoriesLikeOrigin(na, nb, outTally.fStoriesReordered);

	size_t i = 0, j = 0;
	while (i < na.size() && j < nb.size())
	{
		const Node& x = na[i];
		const Node& y = nb[j];

		// ★**THE XMP PACKET IS SKIPPED ON BOTH SIDES** (the header's 4.): its dates say when the
		//   document was made and its elements come back in another order, which ends the walk
		//   before the content is reached.
		if (x.fName == "MetadataPacketPreference" && y.fName == "MetadataPacketPreference")
		{
			const size_t endA = SubtreeEnd(na, i);
			const size_t endB = SubtreeEnd(nb, j);
			outTally.fMetadata += (int32)((endA - i) + (endB - j));
			i = endA;
			j = endB;
			continue;
		}
		if (x.fName != y.fName || x.fDepth != y.fDepth)
		{
			// The label KCM writes into the copy: step over it, count it, carry on.
			if (IsOurLabelSubtree(nb, j))
			{
				const size_t end = SubtreeEnd(nb, j);
				outTally.fOurLabels += (int32)(end - j);
				j = end;
				continue;
			}
			// ★**STOP HERE, AND SAY WHERE.** Past a structural difference the two walks look at
			//   different things, and every attribute after it would be reported as changed. The
			//   element-name counts are the pass that names what went missing.
			outTally.fDivergedAt = (int32)outTally.fElements;
			outTally.fDivergedPath = x.fPath + " against " + y.fPath;
			return kFalse;
		}
		++outTally.fElements;

		// The text written directly inside the element (a <Content>'s words, mostly). Counted only
		// when there IS text, so that "texts equal" means what it says.
		if (!x.fText.empty() || !y.fText.empty())
		{
			++outTally.fTexts;
			if (x.fText != y.fText)
			{
				const int32 kind = Classify(x.fName, std::string("(text)"), x.fText, y.fText);
				Tally(outTally, kind);
				if (kind == kKCMXmlDiffReal)
					Record(outDiffs, maxDiffs, x, "(text)", x.fText, y.fText);
			}
		}

		// Every attribute either side carries. ⚠Both directions: an attribute the copy GAINED is as
		//  much a difference as one it lost, and it is how the import's own defaults show up.
		for (size_t k = 0; k < x.fAttrs.size(); ++k)
		{
			const std::string& key = x.fAttrs[k].first;
			const std::string& va = x.fAttrs[k].second;
			const std::string* vb = Find(y.fAttrs, key);
			++outTally.fAttributes;
			if (vb != nil && (*vb == va || SameNumber(va, *vb)))
				continue;
			const std::string other = (vb != nil) ? *vb : std::string();
			const int32 kind = Classify(x.fName, key, va, other);
			Tally(outTally, kind);
			if (kind == kKCMXmlDiffReal)
				Record(outDiffs, maxDiffs, x, key, va, other);
		}
		for (size_t k = 0; k < y.fAttrs.size(); ++k)
		{
			const std::string& key = y.fAttrs[k].first;
			if (Find(x.fAttrs, key) != nil)
				continue;						// already compared above
			++outTally.fAttributes;
			const int32 kind = Classify(y.fName, key, std::string(), y.fAttrs[k].second);
			Tally(outTally, kind);
			if (kind == kKCMXmlDiffReal)
				Record(outDiffs, maxDiffs, y, key, std::string(), y.fAttrs[k].second);
		}
		++i;
		++j;
	}

	// Whatever is left over: our own labels at the end are forgiven the same way, anything else is a
	// difference in shape.
	while (j < nb.size() && IsOurLabelSubtree(nb, j))
	{
		const size_t end = SubtreeEnd(nb, j);
		outTally.fOurLabels += (int32)(end - j);
		j = end;
	}
	if (i != na.size() || j != nb.size())
	{
		outTally.fDivergedAt = (int32)outTally.fElements;
		outTally.fDivergedPath = "one side has more elements than the other";
		return kFalse;
	}
	return (outTally.fRealDiffs == 0) ? kTrue : kFalse;
}

// End of KCMXmlDeepCompare.cpp.
