//========================================================================================
//
//  KCMJsonText.h
//
//  Escaping and unescaping ONE JSON string literal, in UTF-8.
//
//  WHY THIS IS A FILE AND NOT TWO COPIES
//
//  Two writers of JSON once had to agree on this to the character - KCM's own preferences file
//  (KCMPageChecks.json, a document's path as a key) and the cat paws inside a page's SCRIPT LABEL,
//  which carry the word the reader typed beside them. Text the reader chose is exactly the text
//  that contains a quote, and two copies of an escape rule are two rules the day one is fixed
//  (memory one-question-one-place) - silent in the worst way: a document that saves and then reads
//  back as something else. Only the paws are left (KCMPageMarksDoc.cpp; KCMPageChecks.json went
//  with the private store - the end of KCMPageCheck.cpp). The header stays a file of its own so
//  that the next writer of reader-typed text takes this rule rather than a copy of it.
//
//  Inline in a header rather than a .cpp because these have no state -- and because a .cpp has to
//  be registered in TWO vcxproj files, of which the one that matters is outside the repository
//  (memory vcxproj-registration-not-build-dependency).
//
//  ⚠**These are deliberately NOT a JSON parser.** They handle one string literal. What reads the
//    structure around it is the caller - KCMTextToPaws (KCMPageMarksDoc.cpp), lenient on purpose:
//    the first entry that does not parse ends the read and the paws read before it are kept,
//    because giving up on the whole label would discard everything the reader saved.
//
//========================================================================================

#ifndef __KCMJsonText_h__
#define __KCMJsonText_h__

#include "BaseType.h"		// bool16
#include <string>

/** Escape a UTF-8 string for a JSON string literal: backslash, quote, and \n \r \t.
	A UTF-8 continuation byte is never 0x5C or 0x22, so walking it byte by byte is safe -- which
	 is why this needs no notion of characters at all.
	⚠The other control characters go out as they are. KCMJsonReadString takes them back as they are,
	 so KCM's own round trip holds, but a strict JSON reader would refuse the label. Writing them as
	 \u00XX means teaching KCMJsonReadString to read \u in the same change - it keeps an escape it does
	 not know, so an older KCM would show "u0001" in the word. */
inline void KCMJsonEscape(const std::string& in, std::string& out)
{
	out.clear();
	out.reserve(in.size() + 8);
	for (size_t i = 0; i < in.size(); ++i)
	{
		const char c = in[i];
		switch (c)
		{
			case '\\': out += "\\\\"; break;
			case '\"': out += "\\\""; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:   out += c;      break;
		}
	}
}

/** With text[pos] on an opening quote, unescape up to the closing quote into out and leave pos
	just past it. kFalse when it does not start on a quote, or when there is no closing one. */
inline bool16 KCMJsonReadString(const std::string& text, size_t& pos, std::string& out)
{
	out.clear();
	if (pos >= text.size() || text[pos] != '\"')
		return kFalse;
	++pos;	// step over the opening quote
	while (pos < text.size())
	{
		const char c = text[pos++];
		if (c == '\"')
			return kTrue;	// the closing quote
		if (c == '\\' && pos < text.size())
		{
			const char e = text[pos++];
			switch (e)
			{
				case 'n':  out += '\n'; break;
				case 'r':  out += '\r'; break;
				case 't':  out += '\t'; break;
				case '\\': out += '\\'; break;
				case '\"': out += '\"'; break;
				case '/':  out += '/';  break;
				default:   out += e;    break;	// an escape we do not know is kept as it is
			}
		}
		else
		{
			out += c;
		}
	}
	return kFalse;	// no closing quote = broken
}

#endif // __KCMJsonText_h__

// End, KCMJsonText.h.
