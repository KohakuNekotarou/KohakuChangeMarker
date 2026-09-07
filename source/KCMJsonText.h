//========================================================================================
//
//  KCMJsonText.h
//
//  Escaping and unescaping ONE JSON string literal, in UTF-8.
//
//  ***** WHY THIS IS A FILE AND NOT TWO COPIES *****
//
//  KCM writes JSON in two unrelated places and they have to agree on this to the character:
//    - KCMPageCheck.cpp   -- KCM's own preferences file (KCMPageChecks.json), which holds a
//                            document's path as a key;
//    - KCMPageMarksDoc.cpp -- the cat paws inside a page's SCRIPT LABEL, which since 2026-09-07
//                            carry the word the reader typed beside them.
//  Both were about to hold arbitrary text the reader chose, and text the reader chose is exactly
//  the text that contains a quote. Two copies of an escape rule are two rules the day one is
//  fixed ([[one-question-one-place]]), and the failure would be silent in the worst way: a
//  document that saves and then reads back as something else.
//
//  Inline in a header rather than a .cpp because these are eight lines each and have no state --
//  and because a .cpp has to be registered in TWO vcxproj files, of which the one that matters is
//  outside the repository ([[vcxproj-registration-not-build-dependency]]).
//
//  ⚠**These are deliberately NOT a JSON parser.** They handle one string literal. What reads the
//    structure around it is the caller, in both files, and both are lenient on purpose: a broken
//    entry is skipped and the rest is kept, because giving up on the whole file would silently
//    discard everything the reader saved.
//
//========================================================================================

#ifndef __KCMJsonText_h__
#define __KCMJsonText_h__

#include "BaseType.h"		// bool16
#include <string>

/** Escape a UTF-8 string for a JSON string literal (backslash, quote, control characters).
	★A UTF-8 continuation byte is never 0x5C or 0x22, so walking it byte by byte is safe -- which
	 is why this needs no notion of characters at all. */
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
