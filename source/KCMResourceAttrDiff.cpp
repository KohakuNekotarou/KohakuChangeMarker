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
			const std::string name = body.substr(start, eq - start);

			// ★★★`type` IS A DATATYPE DECLARATION, NOT A VALUE, AND IS NEVER REPORTED.
			//   IDML writes every property element as
			//       <BasedOn type="string">$ID/[No paragraph style]</BasedOn>
			//   so `type` repeats on every one of them, is never the thing a reader changed, and
			//   was what filled the panel with a dozen rows called "type" (2026-09-09, the user:
			//   "the Kind column says type, but it ought to say BasedOn").
			//   ⚠A value that changes KIND still shows: the VALUE itself changes with it, and that
			//     row is the honest one. Nothing is lost by dropping the declaration.
			if (name != "type")
			{
				KCMAttr attr;
				attr.fName = name;
				attr.fValue = body.substr(valueStart, valueEnd - valueStart);
				out.push_back(attr);
			}
		}

		i = valueEnd + 1;
	}
}

/** True for the characters that end an element's name inside its opening tag. */
bool IsTagNameEnd(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '>' || c == '/';
}

/** Pull the PROPERTY ELEMENTS out of one body: `<BasedOn type="string">VALUE</BasedOn>` becomes
	the pair (BasedOn, VALUE).

	★★★THIS IS WHERE MOST OF A STYLE ACTUALLY LIVES. The opening tag carries the numbers
	(`PointSize="8.5"`), but everything else is written as a child element whose NAME is the
	property and whose CONTENT is the value - and until 2026-09-09 this differ saw none of it, only
	the `type` attributes hanging off them.

	Three shapes, and only the first has a value:
	  `<X ...>text</X>`   -> (X, text)   the property and what it is set to
	  `<X ...></X>`       -> (X, "")     ★an EMPTY value, which is not the same as no property
	  `<X ...><Y ...>`    -> nothing     a wrapper such as <Properties>; its children speak for it
	`<X ... />` is self-closing and has no content either.

	⚠**Values come back verbatim, `$ID/` and all.** Shortening `$ID/[No paragraph style]` to its
	  last segment is the panel's doing, not this file's: the model keeps what the document says.
*/
void ParseElements(const std::string& body, std::vector<KCMAttr>& out)
{
	const std::size_t n = body.size();
	std::size_t i = 0;

	while (i < n)
	{
		const std::size_t lt = body.find('<', i);
		if (lt == std::string::npos || lt + 1 >= n)
			break;

		if (body[lt + 1] == '/')	// a closing tag: nothing of its own
		{
			i = lt + 1;
			continue;
		}

		std::size_t nameEnd = lt + 1;
		while (nameEnd < n && !IsTagNameEnd(body[nameEnd]))
			++nameEnd;

		const std::size_t gt = body.find('>', nameEnd);
		if (gt == std::string::npos)
			break;					// the body was cut mid-tag; nothing after it can be trusted

		if (gt > lt && body[gt - 1] == '/')	// self-closing: no content
		{
			i = gt + 1;
			continue;
		}

		const std::size_t contentStart = gt + 1;
		const std::size_t next = body.find('<', contentStart);

		// ★A wrapper is told from a valued element by WHAT FOLLOWS IT: its own closing tag means
		//   the text between is its value (possibly empty); another opening tag means it holds
		//   elements rather than a value.
		if (next != std::string::npos && body[next + 1] != '/')
		{
			i = gt + 1;
			continue;
		}

		KCMAttr attr;
		attr.fName = body.substr(lt + 1, nameEnd - (lt + 1));
		attr.fValue = (next == std::string::npos)
					  ? body.substr(contentStart)
					  : body.substr(contentStart, next - contentStart);
		if (!attr.fName.empty())
			out.push_back(attr);

		i = gt + 1;
	}
}

/** Everything one body has to say: the attributes of its tags, then its property elements.

	★The two passes are independent and BOTH SIDES ARE READ THE SAME WAY, which is all the
	comparison below needs - it matches by name and by occurrence, never by position in the body. */
void ParseBody(const std::string& body, std::vector<KCMAttr>& out)
{
	ParseAttributes(body, out);
	ParseElements(body, out);
}

/** How many attributes BEFORE position i carry the same name as the one at i.

	★★★A NAME IS NOT UNIQUE IN A BODY, and pretending it is manufactures differences that do not
	exist. Measured 2026-09-09 in the panel: a `<TextDefault>` body carries a dozen child elements
	that each write `type="string"` or `type="enumeration"`, and matching by name alone measured
	EVERY occurrence on one side against the FIRST on the other — **ten reported changes, none of
	them real**, in a definition where exactly one attribute had moved.

	⚠**THE BODY IS ALL THIS DIFFER IS GIVEN.** It cannot say which child element an attribute
	belongs to, so it cannot pair them by owner. Order is the one thing both sides agree on: the
	export writes the same elements in the same order, so the n-th `type` on one side is the n-th
	`type` on the other. ★When that assumption breaks - a child element inserted in the middle -
	the rows after it are reported as changed, which is a HONEST over-report: something in there
	really did change.
*/
int OccurrenceIndex(const std::vector<KCMAttr>& attrs, std::size_t i)
{
	int nth = 0;
	for (std::size_t j = 0; j < i; ++j)
	{
		if (attrs[j].fName == attrs[i].fName)
			++nth;
	}
	return nth;
}

/** The index of the `nth` attribute called `name` (0-based), or -1 when there are not that many.

	Linear, and deliberately so: a body carries a few hundred attributes at most, and an index
	would have to be built again for every row the panel asks about. */
int FindNthByName(const std::vector<KCMAttr>& attrs, const std::string& name, int nth)
{
	int seen = 0;
	for (std::size_t i = 0; i < attrs.size(); ++i)
	{
		if (attrs[i].fName != name)
			continue;
		if (seen == nth)
			return static_cast<int>(i);
		++seen;
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
	ParseBody(source, sourceAttrs);
	ParseBody(target, targetAttrs);

	// The target's order first: the reader is looking at the newer document.
	// ★EACH OCCURRENCE IS MATCHED WITH THE SAME-NUMBERED ONE on the other side, never with the
	//   first (see OccurrenceIndex for what that cost when it was by name alone).
	for (std::size_t i = 0; i < targetAttrs.size(); ++i)
	{
		const KCMAttr& t = targetAttrs[i];
		const int s = FindNthByName(sourceAttrs, t.fName, OccurrenceIndex(targetAttrs, i));

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
	// ★Same rule, the other way round: the source's n-th `type` is missing only when the target
	//   has fewer than n+1 of them - not when the target happens to have none with that name.
	for (std::size_t i = 0; i < sourceAttrs.size(); ++i)
	{
		const KCMAttr& s = sourceAttrs[i];
		if (FindNthByName(targetAttrs, s.fName, OccurrenceIndex(sourceAttrs, i)) < 0)
		{
			KCMAttrChange change;
			change.fName = s.fName;
			change.fSource = s.fValue;	// fTarget stays empty
			out.push_back(change);
		}
	}
}

// End, KCMResourceAttrDiff.cpp.
