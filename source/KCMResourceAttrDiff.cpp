//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KCMResourceAttrDiff.cpp -- see the header for what this answers and why it uses no SDK types.
//
//========================================================================================

// ⚠**THIS INCLUDE MUST BE FIRST AND MUST NOT BE GUARDED.** The plug-in builds with /Yu, and that
//   makes the compiler DISCARD EVERYTHING UP TO AND INCLUDING this line - so an `#ifndef` written
//   around it is discarded too and its `#endif` is left orphaned ("C1020: unexpected #endif",
//   measured 2026-09-09). The offline harness supplies an empty VCPlugInHeaders.h of its own
//   instead, which is how work/kescm-snippet-test already stubs BaseType.h and OMTypes.h.
#include "VCPlugInHeaders.h"

#include "KCMResourceAttrDiff.h"

#include <cstddef>

namespace {

/** One `name="value"` pair, in the order it was written. */
struct KCMAttr
{
	std::string	fName;
	std::string	fValue;
};

/** The characters an attribute name is made of.

	★XML allows more than this, but what has to be right is the SEPARATION: the run walks backwards
	from the `=` and has to stop at the space before the name. Anything that is not a name
	character stops it, so a generous set costs nothing and a mean one would split names.
*/
bool IsNameChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		|| c == '_' || c == '-' || c == '.' || c == ':';
}

/** Pull the `name="value"` pairs out of one body, in order.

	★IT ANCHORS ON `="`, NOT ON THE NAME. Bodies arrive with a leading element name and sometimes
	with the opening bracket (`<Layer Self="..."`), and a parser that looked for names first would
	have to decide whether `<Layer` is one. Anchoring on the `="` that only an attribute has means
	the element name is skipped because nothing follows it, rather than because of a rule about it.

	⚠THE VALUE ENDS AT THE FIRST `"`, and that is correct rather than lucky: XML escapes a literal
	  quote inside an attribute as `&quot;`, so an unescaped `"` is always the closing one.
*/
void ParseAttributes(const std::string& body, std::vector<KCMAttr>& out)
{
	const std::size_t n = body.size();
	std::size_t i = 0;

	while (i < n)
	{
		const std::size_t eq = body.find('=', i);
		if (eq == std::string::npos || eq + 1 >= n)
			break;

		// Not an attribute: an `=` that is not followed by a quote. Step past it rather than
		// stopping, so one stray character does not hide the attributes after it.
		if (body[eq + 1] != '"')
		{
			i = eq + 1;
			continue;
		}

		std::size_t start = eq;
		while (start > 0 && IsNameChar(body[start - 1]))
			--start;

		const std::size_t valueStart = eq + 2;
		const std::size_t valueEnd = body.find('"', valueStart);
		if (valueEnd == std::string::npos)
			break;					// an unterminated value: nothing after it can be trusted

		if (start < eq)				// a bare `="` with no name in front is not an attribute
		{
			KCMAttr attr;
			attr.fName = body.substr(start, eq - start);
			attr.fValue = body.substr(valueStart, valueEnd - valueStart);
			out.push_back(attr);
		}

		i = valueEnd + 1;
	}
}

/** The index of an attribute by name, or -1. Linear because an element carries a handful of
	attributes and building a map would cost more than the walk. */
int FindByName(const std::vector<KCMAttr>& attrs, const std::string& name)
{
	for (std::size_t i = 0; i < attrs.size(); ++i)
	{
		if (attrs[i].fName == name)
			return static_cast<int>(i);
	}
	return -1;
}

}	// anonymous namespace

void KCMDiffAttributes(const std::string& source, const std::string& target,
					   std::vector<KCMAttrChange>& out)
{
	out.clear();

	std::vector<KCMAttr> sourceAttrs;
	std::vector<KCMAttr> targetAttrs;
	ParseAttributes(source, sourceAttrs);
	ParseAttributes(target, targetAttrs);

	// The target's order first: the reader is looking at the newer document.
	for (std::size_t i = 0; i < targetAttrs.size(); ++i)
	{
		const KCMAttr& t = targetAttrs[i];
		const int s = FindByName(sourceAttrs, t.fName);

		if (s < 0)
		{
			KCMAttrChange change;
			change.fName = t.fName;
			change.fTarget = t.fValue;	// fSource stays empty: the older side does not have it
			out.push_back(change);
		}
		else if (sourceAttrs[static_cast<std::size_t>(s)].fValue != t.fValue)
		{
			KCMAttrChange change;
			change.fName = t.fName;
			change.fSource = sourceAttrs[static_cast<std::size_t>(s)].fValue;
			change.fTarget = t.fValue;
			out.push_back(change);
		}
	}

	// Then what only the Source had. There is nowhere else to put these, and leaving them out
	// would make a removed attribute indistinguishable from one that never existed.
	for (std::size_t i = 0; i < sourceAttrs.size(); ++i)
	{
		const KCMAttr& s = sourceAttrs[i];
		if (FindByName(targetAttrs, s.fName) < 0)
		{
			KCMAttrChange change;
			change.fName = s.fName;
			change.fSource = s.fValue;	// fTarget stays empty
			out.push_back(change);
		}
	}
}

// End, KCMResourceAttrDiff.cpp.
