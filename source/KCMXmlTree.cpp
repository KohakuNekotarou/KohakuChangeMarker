//========================================================================================
//
//  KCMXmlTree.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storyhtml-test).
#include "VCPlugInHeaders.h"

#include "KCMXmlTree.h"

#include <cstdio>
#include <utility>

const char* const KCMXmlTree::kWordNs = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
const char* const KCMXmlTree::kMcNs = "http://schemas.openxmlformats.org/markup-compatibility/2006";

namespace
{

bool16 IsSpace(char c)
{
	return (c == ' ' || c == '\t' || c == '\r' || c == '\n') ? kTrue : kFalse;
}

/** A name character: letters, digits, the four punctuation marks XML allows, and any byte of a
	multi-byte UTF-8 sequence (a Japanese element name is a name too). */
bool16 IsNameChar(char c)
{
	const unsigned char u = static_cast<unsigned char>(c);
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '_' || c == '-' || c == '.' || c == ':' || u >= 0x80) ? kTrue : kFalse;
}

void AppendUtf8(std::string& out, unsigned int cp)
{
	if (cp < 0x80)
	{
		out += static_cast<char>(cp);
	}
	else if (cp < 0x800)
	{
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

std::string Num(size_t n)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned int>(n));
	return std::string(buf);
}

/** raw[from, to) with its entities decoded into out. The five named ones and the two numeric
	forms; anything else is refused by name, because a character read wrong is a character lost. */
bool16 DecodeEntities(const std::string& raw, size_t from, size_t to, std::string& out, std::string& whyNot)
{
	size_t i = from;
	while (i < to)
	{
		const char c = raw[i];
		if (c != '&')
		{
			out += c;
			++i;
			continue;
		}
		const size_t semi = raw.find(';', i + 1);
		if (semi == std::string::npos || semi >= to)
		{
			whyNot = "an '&' with no ';' after it near byte " + Num(i);
			return kFalse;
		}
		const std::string name = raw.substr(i + 1, semi - i - 1);
		if (name == "amp")		out += '&';
		else if (name == "lt")	out += '<';
		else if (name == "gt")	out += '>';
		else if (name == "quot")	out += '"';
		else if (name == "apos")	out += '\'';
		else if (name.size() >= 2 && name[0] == '#')
		{
			unsigned int cp = 0;
			const bool16 hex = (name[1] == 'x' || name[1] == 'X') ? kTrue : kFalse;
			size_t k = hex ? 2 : 1;
			if (k >= name.size())
			{
				whyNot = "an entity this reader does not know: &" + name + ";";
				return kFalse;
			}
			for (; k < name.size(); ++k)
			{
				const char d = name[k];
				unsigned int v = 0;
				if (d >= '0' && d <= '9')				v = static_cast<unsigned int>(d - '0');
				else if (hex && d >= 'a' && d <= 'f')	v = static_cast<unsigned int>(d - 'a' + 10);
				else if (hex && d >= 'A' && d <= 'F')	v = static_cast<unsigned int>(d - 'A' + 10);
				else
				{
					whyNot = "an entity this reader does not know: &" + name + ";";
					return kFalse;
				}
				cp = cp * (hex ? 16u : 10u) + v;
				if (cp > 0x10FFFF)
				{
					whyNot = "a character reference past U+10FFFF: &" + name + ";";
					return kFalse;
				}
			}
			AppendUtf8(out, cp);
		}
		else
		{
			whyNot = "an entity this reader does not know: &" + name + ";";
			return kFalse;
		}
		i = semi + 1;
	}
	return kTrue;
}

/** "w:val" -> ("w", "val"); "p" -> ("", "p"). */
void SplitName(const std::string& qualified, std::string& prefix, std::string& local)
{
	const size_t colon = qualified.find(':');
	if (colon == std::string::npos)
	{
		prefix.clear();
		local = qualified;
	}
	else
	{
		prefix = qualified.substr(0, colon);
		local = qualified.substr(colon + 1);
	}
}

typedef std::vector< std::pair<std::string, std::string> > Bindings;	// prefix ("" = default) -> URI

/** The URI `prefix` is bound to, looking from the innermost scope outwards; nil when unbound. */
const std::string* Resolve(const std::vector<Bindings>& scopes, const std::string& prefix)
{
	for (size_t s = scopes.size(); s > 0; --s)
	{
		const Bindings& b = scopes[s - 1];
		for (size_t k = b.size(); k > 0; --k)
		{
			if (b[k - 1].first == prefix)
				return &b[k - 1].second;
		}
	}
	return nil;
}

struct Open
{
	int32		fNode;
	std::string	fLocal;
	std::string	fNs;
};

}	// anonymous namespace

bool16 KCMXmlTree::Parse(const char* xml, size_t size, std::string& whyNot)
{
	fNodes.clear();
	whyNot.clear();

	if (xml == nil || size == 0)
	{
		whyNot = "there is nothing to read";
		return kFalse;
	}

	std::string s(xml, size);
	if (s.size() >= 3
		&& static_cast<unsigned char>(s[0]) == 0xEF
		&& static_cast<unsigned char>(s[1]) == 0xBB
		&& static_cast<unsigned char>(s[2]) == 0xBF)
	{
		s.erase(0, 3);
	}

	fNodes.push_back(KCMXmlNode());		// [0], the document
	std::vector<Open> open;				// the elements open right now; open.back() is the parent
	std::vector<Bindings> scopes;		// one per open element
	int32 root = -1;

	// The document-level scope: the xml prefix is bound by the language itself.
	{
		Bindings base;
		base.push_back(std::make_pair(std::string("xml"), std::string("http://www.w3.org/XML/1998/namespace")));
		scopes.push_back(base);
	}

	size_t i = 0;
	while (i < s.size())
	{
		const int32 parent = open.empty() ? 0 : open.back().fNode;

		if (s[i] != '<')
		{
			// ---- text, up to the next tag -------------------------------------------------------
			const size_t end = s.find('<', i);
			const size_t to = (end == std::string::npos) ? s.size() : end;
			if (!open.empty())
			{
				KCMXmlNode text;
				if (!DecodeEntities(s, i, to, text.fText, whyNot))
					return kFalse;
				fNodes.push_back(text);
				fNodes[static_cast<size_t>(parent)].fChildren.push_back(static_cast<int32>(fNodes.size()) - 1);
			}
			else
			{
				// Outside the root element only whitespace may stand.
				for (size_t k = i; k < to; ++k)
				{
					if (!IsSpace(s[k]))
					{
						whyNot = "text stands outside the document element near byte " + Num(k);
						return kFalse;
					}
				}
			}
			i = to;
			continue;
		}

		// ---- a processing instruction, a comment, CDATA, a DOCTYPE ------------------------------
		if (s.compare(i, 2, "<?") == 0)
		{
			const size_t end = s.find("?>", i + 2);
			if (end == std::string::npos)
			{
				whyNot = "a processing instruction that never ends near byte " + Num(i);
				return kFalse;
			}
			i = end + 2;
			continue;
		}
		if (s.compare(i, 4, "<!--") == 0)
		{
			const size_t end = s.find("-->", i + 4);
			if (end == std::string::npos)
			{
				whyNot = "a comment that never ends near byte " + Num(i);
				return kFalse;
			}
			i = end + 3;
			continue;
		}
		if (s.compare(i, 9, "<![CDATA[") == 0)
		{
			const size_t end = s.find("]]>", i + 9);
			if (end == std::string::npos)
			{
				whyNot = "a CDATA section that never ends near byte " + Num(i);
				return kFalse;
			}
			if (open.empty())
			{
				whyNot = "text stands outside the document element near byte " + Num(i);
				return kFalse;
			}
			KCMXmlNode text;
			text.fText = s.substr(i + 9, end - i - 9);
			fNodes.push_back(text);
			fNodes[static_cast<size_t>(parent)].fChildren.push_back(static_cast<int32>(fNodes.size()) - 1);
			i = end + 3;
			continue;
		}
		if (s.compare(i, 2, "<!") == 0)
		{
			const size_t end = s.find('>', i + 2);
			if (end == std::string::npos)
			{
				whyNot = "a declaration that never ends near byte " + Num(i);
				return kFalse;
			}
			i = end + 1;
			continue;
		}

		// ---- an end tag ---------------------------------------------------------------------------
		if (s.compare(i, 2, "</") == 0)
		{
			size_t k = i + 2;
			while (k < s.size() && IsNameChar(s[k]))
				++k;
			const std::string qualified = s.substr(i + 2, k - i - 2);
			while (k < s.size() && IsSpace(s[k]))
				++k;
			if (k >= s.size() || s[k] != '>' || qualified.empty())
			{
				whyNot = "the XML is not well-formed near byte " + Num(i);
				return kFalse;
			}
			if (open.empty())
			{
				whyNot = "an end tag with nothing open: </" + qualified + ">";
				return kFalse;
			}
			std::string prefix, local;
			SplitName(qualified, prefix, local);
			const std::string* ns = Resolve(scopes, prefix);
			const std::string nsUri = (ns != nil) ? *ns : std::string();
			if (local != open.back().fLocal || nsUri != open.back().fNs)
			{
				whyNot = "an end tag that does not match its start tag: </" + qualified + "> near byte " + Num(i);
				return kFalse;
			}
			open.pop_back();
			scopes.pop_back();
			i = k + 1;
			continue;
		}

		// ---- a start tag, self-closing or not --------------------------------------------------
		size_t k = i + 1;
		while (k < s.size() && IsNameChar(s[k]))
			++k;
		const std::string qualified = s.substr(i + 1, k - i - 1);
		if (qualified.empty())
		{
			whyNot = "a '<' with no name after it near byte " + Num(i);
			return kFalse;
		}

		KCMXmlNode element;
		Bindings bound;
		bool16 selfClosing = kFalse;
		for (;;)
		{
			while (k < s.size() && IsSpace(s[k]))
				++k;
			if (k >= s.size())
			{
				whyNot = "a start tag that never ends near byte " + Num(i);
				return kFalse;
			}
			if (s[k] == '>')
			{
				++k;
				break;
			}
			if (s[k] == '/' && k + 1 < s.size() && s[k + 1] == '>')
			{
				selfClosing = kTrue;
				k += 2;
				break;
			}
			// an attribute: name = "value" or name = 'value'
			size_t nameEnd = k;
			while (nameEnd < s.size() && IsNameChar(s[nameEnd]))
				++nameEnd;
			if (nameEnd == k)
			{
				whyNot = "the XML is not well-formed near byte " + Num(k);
				return kFalse;
			}
			KCMXmlAttr attr;
			attr.fName = s.substr(k, nameEnd - k);
			k = nameEnd;
			while (k < s.size() && IsSpace(s[k]))
				++k;
			if (k >= s.size() || s[k] != '=')
			{
				whyNot = "an attribute with no value: " + attr.fName + " near byte " + Num(k);
				return kFalse;
			}
			++k;
			while (k < s.size() && IsSpace(s[k]))
				++k;
			if (k >= s.size() || (s[k] != '"' && s[k] != '\''))
			{
				whyNot = "an attribute value that is not quoted: " + attr.fName + " near byte " + Num(k);
				return kFalse;
			}
			const char quote = s[k];
			const size_t valueEnd = s.find(quote, k + 1);
			if (valueEnd == std::string::npos)
			{
				whyNot = "an attribute value that never closes: " + attr.fName + " near byte " + Num(k);
				return kFalse;
			}
			if (!DecodeEntities(s, k + 1, valueEnd, attr.fValue, whyNot))
				return kFalse;
			k = valueEnd + 1;

			if (attr.fName == "xmlns")
				bound.push_back(std::make_pair(std::string(), attr.fValue));
			else if (attr.fName.compare(0, 6, "xmlns:") == 0)
				bound.push_back(std::make_pair(attr.fName.substr(6), attr.fValue));
			element.fAttrs.push_back(attr);
		}

		// The element's own bindings are in force for its own name.
		scopes.push_back(bound);
		std::string prefix;
		SplitName(qualified, prefix, element.fName);
		const std::string* ns = Resolve(scopes, prefix);
		if (ns == nil && !prefix.empty())
		{
			whyNot = "a prefix bound to no namespace: " + qualified + " near byte " + Num(i);
			return kFalse;
		}
		if (ns != nil)
			element.fNs = *ns;

		if (open.empty() && root >= 0)
		{
			whyNot = "a second document element: <" + qualified + "> near byte " + Num(i);
			return kFalse;
		}

		fNodes.push_back(element);
		const int32 index = static_cast<int32>(fNodes.size()) - 1;
		fNodes[static_cast<size_t>(parent)].fChildren.push_back(index);
		if (open.empty())
			root = index;

		if (selfClosing)
		{
			scopes.pop_back();
		}
		else
		{
			Open o;
			o.fNode = index;
			o.fLocal = element.fName;
			o.fNs = element.fNs;
			open.push_back(o);
		}
		i = k;
	}

	if (!open.empty())
	{
		whyNot = "the document does not close: <" + open.back().fLocal + "> is still open at the end";
		return kFalse;
	}
	if (root < 0)
	{
		whyNot = "there is no document element";
		return kFalse;
	}
	return kTrue;
}

int32 KCMXmlTree::Root() const
{
	if (fNodes.empty())
		return -1;
	const std::vector<int32>& top = fNodes[0].fChildren;
	for (size_t k = 0; k < top.size(); ++k)
	{
		if (!fNodes[static_cast<size_t>(top[k])].IsText())
			return top[k];
	}
	return -1;
}

int32 KCMXmlTree::Count() const
{
	return static_cast<int32>(fNodes.size());
}

const KCMXmlNode& KCMXmlTree::At(int32 node) const
{
	return fNodes[static_cast<size_t>(node)];
}

const std::string* KCMXmlTree::Attr(int32 node, const char* localName) const
{
	if (node < 0 || static_cast<size_t>(node) >= fNodes.size() || localName == nil)
		return nil;
	const std::vector<KCMXmlAttr>& attrs = fNodes[static_cast<size_t>(node)].fAttrs;
	for (size_t k = 0; k < attrs.size(); ++k)
	{
		const std::string& name = attrs[k].fName;
		const size_t colon = name.find(':');
		const size_t from = (colon == std::string::npos) ? 0 : colon + 1;
		if (name.compare(from, std::string::npos, localName) == 0)
			return &attrs[k].fValue;
	}
	return nil;
}

int32 KCMXmlTree::Child(int32 node, const char* ns, const char* localName) const
{
	if (node < 0 || static_cast<size_t>(node) >= fNodes.size())
		return -1;
	const std::vector<int32>& kids = fNodes[static_cast<size_t>(node)].fChildren;
	for (size_t k = 0; k < kids.size(); ++k)
	{
		if (Is(kids[k], ns, localName))
			return kids[k];
	}
	return -1;
}

bool16 KCMXmlTree::Is(int32 node, const char* ns, const char* localName) const
{
	if (node < 0 || static_cast<size_t>(node) >= fNodes.size() || ns == nil || localName == nil)
		return kFalse;
	const KCMXmlNode& n = fNodes[static_cast<size_t>(node)];
	if (n.IsText())
		return kFalse;
	return (n.fNs == ns && n.fName == localName) ? kTrue : kFalse;
}

std::string KCMXmlTree::TextBelow(int32 node) const
{
	std::string out;
	if (node < 0 || static_cast<size_t>(node) >= fNodes.size())
		return out;
	// An explicit stack: the parts are shallow, but nothing here should depend on that.
	std::vector<int32> stack;
	stack.push_back(node);
	while (!stack.empty())
	{
		const int32 at = stack.back();
		stack.pop_back();
		const KCMXmlNode& n = fNodes[static_cast<size_t>(at)];
		if (n.IsText())
		{
			out += n.fText;
			continue;
		}
		for (size_t k = n.fChildren.size(); k > 0; --k)
			stack.push_back(n.fChildren[k - 1]);
	}
	return out;
}

// End, KCMXmlTree.cpp.
