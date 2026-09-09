//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMXmlPretty.h for what this lays out and why it uses no SDK types.
//
//========================================================================================

// ⚠**THIS INCLUDE MUST BE FIRST AND MUST NOT BE GUARDED** - the plug-in builds with /Yu, which
//   discards everything up to and including this line, an `#ifndef` around it included. The
//   offline harness supplies an empty VCPlugInHeaders.h of its own. (The same note stands at the
//   head of KCMResourceAttrDiff.cpp, where it was learnt.)
#include "VCPlugInHeaders.h"

#include "KCMXmlPretty.h"

#include <cstddef>

namespace {

/** How far one level of nesting moves right. Two spaces: an IDML property block runs four or five
	deep, and a wider step pushes the values off a dialog that is only so wide. */
const int kIndentStep = 2;

bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void AppendIndent(std::string& out, int depth)
{
	for (int i = 0; i < depth * kIndentStep; ++i)
		out += ' ';
}

/** The run of text from `from` up to the next `<`, with the whitespace at both ends removed.
	⚠Only the ENDS. Whitespace inside a value is the document's own. */
std::string TrimmedText(const std::string& xml, std::size_t from, std::size_t to)
{
	std::size_t b = from;
	std::size_t e = to;
	while (b < e && IsSpace(xml[b]))
		++b;
	while (e > b && IsSpace(xml[e - 1]))
		--e;
	return xml.substr(b, e - b);
}

}	// anonymous namespace

std::string KCMPrettyXml(const std::string& xml)
{
	std::string out;
	out.reserve(xml.size() + xml.size() / 4);

	const std::size_t n = xml.size();
	std::size_t i = 0;
	int depth = 0;

	// ★Set while the line being written is an opening tag whose content was text: the closing tag
	//   then goes on the SAME line, so that `<BasedOn ...>value</BasedOn>` reads as one thing.
	bool inlineText = false;
	// Whether anything has been written yet, so the first line gets no leading newline.
	bool started = false;

	while (i < n)
	{
		if (xml[i] != '<')
		{
			// ----- TEXT between tags.
			const std::size_t textEnd = xml.find('<', i);
			const std::size_t stop = (textEnd == std::string::npos) ? n : textEnd;
			const std::string text = TrimmedText(xml, i, stop);
			if (!text.empty())
			{
				// It belongs to the element whose opening tag was just written, so it goes on that
				// line. ⚠Whitespace-only runs are the exporter's own layout and are dropped - the
				// one thing this function removes (see the header).
				out += text;
				inlineText = true;
			}
			i = stop;
			continue;
		}

		// ----- A TAG. Its end is the next '>'; if there is none the input was cut mid-tag, and the
		//   remainder is written out whole rather than dropped.
		const std::size_t close = xml.find('>', i);
		const std::size_t tagEnd = (close == std::string::npos) ? n : close + 1;
		const std::string tag = xml.substr(i, tagEnd - i);

		const bool isClosing = (i + 1 < n) && (xml[i + 1] == '/');
		const bool isSelfClosing = (tag.size() >= 2) && (tag[tag.size() - 2] == '/');
		// `<?xml ...?>`, `<!-- -->`, `<!DOCTYPE ...>`: they open nothing and close nothing.
		const bool isDeclaration = (i + 1 < n) && (xml[i + 1] == '?' || xml[i + 1] == '!');

		if (isClosing)
		{
			if (depth > 0)
				--depth;						// ⚠clamped: the input may close more than it opened

			if (inlineText)
			{
				out += tag;					// same line as its opening tag and its value
				inlineText = false;
			}
			else
			{
				if (started)
					out += '\n';
				AppendIndent(out, depth);
				out += tag;
				started = true;
			}
			i = tagEnd;
			continue;
		}

		if (started)
			out += '\n';
		AppendIndent(out, depth);
		out += tag;
		started = true;
		inlineText = false;

		if (!isSelfClosing && !isDeclaration)
			++depth;

		i = tagEnd;
	}

	return out;
}

// End, KCMXmlPretty.cpp.
