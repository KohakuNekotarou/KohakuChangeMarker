//========================================================================================
//
//  KCMStoryHtml.cpp -- see the header.
//
//  Body paragraphs only, so far. Ruby, the invisible characters, notes and tables are Tasks 2 to 5
//  of docs/superpowers/plans/2026-09-15-kcm-story-text-roundtrip.md, each arriving with its own
//  failing test in work/kcm-storyhtml-test/test.cpp.
//
//  *** NO NON-ASCII IN A STRING LITERAL. *** The plug-in is built without /utf-8, so this file
//  carries a UTF-8 BOM like every other source in KCM - that is what lets the marks in these
//  comments mean what they say. The rule the BOM does NOT cover is the literals: a non-ASCII
//  character that also exists in CP932 compiles cleanly and comes out wrong at run time, in a file
//  somebody else opens. So everything this file WRITES is ASCII, and stays that way.
//  (The labels the design puts in the stylesheet - the little marks shown for an anchored object
//  or a forced line break - are the first literals that will not be, and they are Task 3's. They
//  want \x-escaped UTF-8 bytes, not the characters themselves.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "KCMStoryHtml.h"

#include <cstdio>

namespace KCMStoryHtml
{

namespace
{

/*	kOurTags
	The tags this format writes, plus the ones it accepts without writing them (an editor that
	knows HTML adds tbody and colgroup by itself) and the two it REFUSES (br and wbr - the design
	says why: a writer who knows HTML reaches for <br> and would silently get a forced line break).

	★**THE LIST IS THE ESCAPE'S OTHER HALF.** A '<' in the reader's own words is written as it
	stands - "1 < 2" needs nothing - EXCEPT when what follows it spells one of these, and then the
	writer doubles it. So the same list decides what the writer escapes and what the reader takes
	as markup, and the two can never drift apart.
*/
const char* const kOurTags[] =
{
	"p", "ruby", "rt", "table", "tr", "td", "sup", "a", "ol", "li", "span",
	"tbody", "thead", "tfoot", "colgroup", "col", "br", "wbr"
};
const size_t kOurTagCount = sizeof(kOurTags) / sizeof(kOurTags[0]);

/*	kScaffolding
	The document around the text. The reader has to KNOW these rather than treat them as words,
	because <body> is what tells it where the text begins - but it never has to build them.
*/
const char* const kScaffolding[] = { "html", "head", "body", "meta", "title", "style", "link" };
const size_t kScaffoldingCount = sizeof(kScaffolding) / sizeof(kScaffolding[0]);

char Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool16 IsNameChar(char c)
{
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
		   ? kTrue : kFalse;
}

bool16 IsInList(const std::string& lowerName, const char* const* list, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		if (lowerName == list[i])
			return kTrue;
	}
	return kFalse;
}

/*	TagAt
	Is there a tag at `at`, where s[at] is '<'? Its name comes back lowercased, whether it closes,
	and where the '>' left off.

	⚠**"< 2" IS NOT A TAG AND NEITHER IS "<ruby" WITH NO '>'.** Both answer kFalse, which is what
	 makes the reader's tolerance work: anything this function refuses is a character the reader
	 typed. The quote walk is there for the same reason - an attribute value holding '>' must not
	 end the tag early, or the rest of a paragraph would vanish into it.
*/
bool16 TagAt(const std::string& s, size_t at, std::string& outName, bool16& outClosing,
			 size_t& outAfter)
{
	if (at >= s.size() || s[at] != '<')
		return kFalse;

	size_t i = at + 1;
	outClosing = kFalse;
	if (i < s.size() && s[i] == '/')
	{
		outClosing = kTrue;
		++i;
	}

	const size_t nameStart = i;
	while (i < s.size() && IsNameChar(s[i]))
		++i;
	if (i == nameStart)
		return kFalse;			// '<' with no name behind it

	outName.assign(s, nameStart, i - nameStart);
	for (size_t k = 0; k < outName.size(); ++k)
		outName[k] = Lower(outName[k]);

	if (i < s.size())
	{
		const char after = s[i];
		if (after != '>' && after != '/' && after != ' ' && after != '\t'
			&& after != '\r' && after != '\n')
			return kFalse;		// "<pineapple" is not "<p"
	}

	char quote = 0;
	while (i < s.size())
	{
		const char c = s[i];
		if (quote != 0)
		{
			if (c == quote)
				quote = 0;
		}
		else if (c == '"' || c == '\'')
		{
			quote = c;
		}
		else if (c == '>')
		{
			outAfter = i + 1;
			return kTrue;
		}
		++i;
	}
	return kFalse;				// never closed
}

/** kTrue when one of THIS FORMAT's tags begins at `at`. */
bool16 IsOurTagAt(const std::string& s, size_t at)
{
	std::string name;
	bool16 closing = kFalse;
	size_t after = 0;
	if (!TagAt(s, at, name, closing, after))
		return kFalse;
	return IsInList(name, kOurTags, kOurTagCount);
}

/*	WriteText
	The reader's words, with the one escape this format has.

	★**THE ESCAPE IS THE ONLY THING DONE TO THE TEXT.** No entities: '&' stays '&', '>' stays '>',
	and a browser shows them both correctly where they stand. The doubled '<' exists because a
	paragraph that really says "<ruby>" has to come back saying it, and there is no other way to
	tell that apart from markup.
*/
void WriteText(const std::string& text, std::string& out)
{
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '<' && IsOurTagAt(text, i))
			out += '<';
		out += text[i];
	}
}

}	// anonymous namespace

bool16 IsInvisible(int32 cp)
{
	// ⚠THE TWO EXCEPTIONS, FIRST, because both are inside a range that follows.
	if (cp == 0x0009)		// TAB - a character anybody can type, so it is written as itself
		return kFalse;
	if (cp == 0x000D)		// CR - never arrives here: it IS the paragraph boundary
		return kFalse;

	if (cp >= 0 && cp <= 0x001F)		// the control characters, U+000A forced line break among them
		return kTrue;
	if (cp == 0x007F)
		return kTrue;
	if (cp == 0x00AD)					// soft hyphen
		return kTrue;
	if (cp >= 0x200B && cp <= 0x200F)	// zero width space / non-joiner / joiner / direction marks
		return kTrue;
	if (cp == 0x2028 || cp == 0x2029)	// line and paragraph separators
		return kTrue;
	if (cp >= 0x202A && cp <= 0x202E)	// the bidirectional embedding controls
		return kTrue;
	if (cp == 0xFEFF)					// the XML tag's mark, and a note's anchor
		return kTrue;
	if (cp == 0xFFFC)					// an anchored object
		return kTrue;
	if (cp >= 0xE000 && cp <= 0xF8FF)	// the private use area - the index marker lives at U+E02C
		return kTrue;

	return kFalse;
}

void Write(const Story& s, int32 uid, std::string& out)
{
	out.clear();

	char uidText[32];
	std::snprintf(uidText, sizeof(uidText), "%d", static_cast<int>(uid));

	// ★THE UID IS THE TITLE, in decimal, because that is what "Show Story IDs" prints on the page:
	//   the reader matches the file against a story by reading the two side by side.
	out += "<!DOCTYPE html>\r\n";
	out += "<html lang=\"ja\">\r\n";
	out += "<head><meta charset=\"utf-8\"><title>";
	out += uidText;
	out += "</title>\r\n";
	out += "<style>\r\n";
	out += "body{line-height:2.2;margin:2em;max-width:40em}\r\n";
	out += "p{margin:0 0 .7em}\r\n";			// so a paragraph and a forced line break look different
	out += "table{border-collapse:collapse;margin:1em 0}\r\n";
	out += "td{border:1px solid #999;padding:.3em .8em;vertical-align:top}\r\n";
	out += "ol{border-top:1px solid #ccc;margin-top:2em;padding-top:1em}\r\n";
	out += "</style></head>\r\n";
	out += "<body>\r\n";

	for (size_t i = 0; i < s.fBody.size(); ++i)
	{
		out += "<p>";
		WriteText(s.fBody[i].fText, out);
		out += "</p>\r\n";
	}

	out += "</body>\r\n";
	out += "</html>\r\n";
}

bool16 Read(const char* html, size_t size, Story& out, std::string& whyNot)
{
	out = Story();
	whyNot.clear();

	if (html == nil)
	{
		whyNot = "there is no document to read";
		return kFalse;
	}

	std::string s(html, size);

	// ⚠A BOM IS SKIPPED, NEVER REQUIRED. The file carries one; a file that has been through a chat
	//   window does not, and has to read exactly the same.
	if (s.size() >= 3
		&& static_cast<unsigned char>(s[0]) == 0xEF
		&& static_cast<unsigned char>(s[1]) == 0xBB
		&& static_cast<unsigned char>(s[2]) == 0xBF)
	{
		s.erase(0, 3);
	}

	bool16 inBody = kFalse;
	bool16 inPara = kFalse;
	std::string para;

	size_t i = 0;
	while (i < s.size())
	{
		const char c = s[i];

		if (c != '<')
		{
			if (inBody && inPara)
				para += c;
			++i;
			continue;
		}

		// ---- the one escape: "<<" in front of a name we know is a '<' the reader typed --------
		if (i + 1 < s.size() && s[i + 1] == '<' && IsOurTagAt(s, i + 1))
		{
			if (inBody && inPara)
				para += '<';
			i += 2;					// both brackets are consumed; the name behind them is text
			continue;
		}

		// ---- a comment or a declaration, wherever it stands -----------------------------------
		if (i + 1 < s.size() && s[i + 1] == '!')
		{
			if (s.compare(i, 4, "<!--") == 0)
			{
				const size_t end = s.find("-->", i + 4);
				i = (end == std::string::npos) ? s.size() : end + 3;
			}
			else
			{
				const size_t end = s.find('>', i);
				i = (end == std::string::npos) ? s.size() : end + 1;
			}
			continue;
		}

		std::string name;
		bool16 closing = kFalse;
		size_t after = 0;
		const bool16 isTag = TagAt(s, i, name, closing, after);

		if (isTag && IsInList(name, kOurTags, kOurTagCount))
		{
			if (name == "p")
			{
				if (!inBody)
				{
					whyNot = "a paragraph stands outside <body>";
					return kFalse;
				}
				if (!closing)
				{
					if (inPara)
					{
						whyNot = "a <p> begins inside another one";
						return kFalse;
					}
					inPara = kTrue;
					para.clear();
				}
				else
				{
					if (!inPara)
					{
						whyNot = "a </p> closes a paragraph that never began";
						return kFalse;
					}
					Para p;
					p.fText = para;
					out.fBody.push_back(p);
					inPara = kFalse;
					para.clear();
				}
				i = after;
				continue;
			}

			// ⚠**REFUSED, NOT SKIPPED.** These are this format's own tags and they will be read -
			//   Tasks 2 to 5 - but until then, skipping one would drop the reader's words without
			//   saying so. A refusal with a reason is the only honest answer while the code that
			//   would build it does not exist.
			whyNot = "the <" + name + "> element is not read yet";
			return kFalse;
		}

		if (isTag && IsInList(name, kScaffolding, kScaffoldingCount))
		{
			if (name == "body")
				inBody = closing ? kFalse : kTrue;
			i = after;
			continue;
		}

		// ---- not one of ours ------------------------------------------------------------------
		// ★Inside a paragraph this is the reader's own '<' and it stays a character (design 3-5:
		//   "anything but the tags we know is text"). Outside one there is no text to add it to, so
		//   an unknown element is stepped over instead.
		if (inBody && inPara)
		{
			para += '<';
			++i;
		}
		else
		{
			i = isTag ? after : (i + 1);
		}
	}

	if (inPara)
	{
		whyNot = "the last paragraph was never closed";
		return kFalse;
	}

	return kTrue;
}

}	// namespace KCMStoryHtml

// End, KCMStoryHtml.cpp.
