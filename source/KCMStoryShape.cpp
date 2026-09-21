//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the shape a story is read into, and the rules every spelling of it
//  shares
//
//  WHAT THIS IS FOR. The reader exports a document's stories, edits them outside InDesign, and
//  imports them again. WHAT travels is here; HOW it is spelt on disk is KCMStoryDocx. The two were
//  one file until 2026-09-21, when the HTML spelling was retired on the user's word ("Word format
//  only") and what stayed behind was this: the structs, the rule for which characters cannot be
//  written as themselves, the short names a kenten kind travels under, and the comparison that
//  says whether two readings of a story are the same.
//
//  *** EVERYTHING HERE IS A PURE FUNCTION. No SDK type, no document, no file. ***
//  That is deliberate and it is why the format can be trusted: the writer and the reader are built
//  and run OUTSIDE InDesign, against each other, in work/kcm-storydocx-test (build.cmd - one
//  command, about a second). The property being tested is one sentence:
//
//      Read(Write(x)) == x
//
//  A round trip that loses nothing is not something a comparison can be asked about afterwards - a
//  document that comes back subtly different reads as "the user edited it". So it is settled here,
//  where a failure is a failing test rather than a wrong mark on somebody's page.
//  (KCMXmlInject.h is the same shape for the same reason.)
//
//  *** THE TEXT IS KCMTextRead's TEXT. *** Para::fText is a paragraph exactly as ReadStory reports
//  it, invisible characters included. Turning U+FFFC into something a file can hold is the
//  WRITER's business and turning it back is the READER's; nothing upstream or downstream has to
//  know those characters were ever markup. The four characters ReadStory takes OUT (U+0016 and
//  U+0017 for tables, U+0004 and U+0005 for note references) are not in fText either - the tables
//  carry their own places, and a note's reference is carried separately (Para says why).
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storydocx-test).
#include "VCPlugInHeaders.h"

#include "KCMStoryShape.h"

#include "KCMParaText.h"		// AppendUtf8 - a code point back into UTF-8
#include "KCMTextDiff.h"		// ToCodePoints - one UTF-8 walk, owned in one place

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace KCMStoryShape
{

namespace
{

/** The characters a class name may hold. ★The kenten names travel as class names, which is why
	this rule lives beside them rather than beside any one file format. */
bool16 IsClassChar(char c)
{
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '-' || c == '_') ? kTrue : kFalse;
}

/** One hexadecimal digit, or -1. A custom kenten mark is written as its code point in hex. */
int32 HexDigit(char c)
{
	if (c >= '0' && c <= '9')	return c - '0';
	if (c >= 'a' && c <= 'f')	return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')	return c - 'A' + 10;
	return -1;
}

/** Add to `seen` every kenten value these paragraphs use, skipping any already there. */
void CollectKenten(const std::vector<Para>& paras, std::vector<std::string>& seen)
{
	for (size_t i = 0; i < paras.size(); ++i)
	{
		for (size_t k = 0; k < paras[i].fKenten.size(); ++k)
		{
			const std::string& value = paras[i].fKenten[k].fValue;
			if (value.empty())
				continue;
			if (std::find(seen.begin(), seen.end(), value) == seen.end())
				seen.push_back(value);
		}
	}
}

}	// anonymous namespace

/*	kKentenDefaultValue
	What an <em> with no class of ours means.

	★★**BECAUSE SOMEBODY WILL WRITE ONE.** The whole point of this format being HTML is that a
	person - or Claude, asked in a chat to "put kenten on this word" - can write the obvious thing
	and have it work. The obvious thing is <em>, and refusing it would teach nobody anything: the
	file would come back with the words marked up and the mark thrown away. So a bare <em> is a
	kenten with InDesign's own default mark, and the stylesheet this file writes says as much in
	plain CSS, where anybody opening the document can see it.
	⚠A class of OURS that cannot be read is still refused - "kenten-Custom-zzz" is a mistake, not a
	 request - and that is the difference: no class at all is a plain HTML author, a broken one of
	 ours is a broken one of ours.
*/
const char* const kKentenDefaultValue = "BlackSesameDot";

bool16 KentenClassOf(const std::string& value, std::string& outClass)
{
	outClass.clear();
	if (value.empty())
		return kFalse;

	// "Custom:X" -> "Custom-<hex of X>". The character is the reader's own, so it is written the
	// way this format writes every character that cannot be a class: as its code point.
	const size_t kCustomLen = 7;			// "Custom:"
	if (value.size() > kCustomLen && value.compare(0, kCustomLen, "Custom:") == 0)
	{
		std::vector<int32> cps;
		KCMTextDiff::ToCodePoints(value.substr(kCustomLen), &cps, nil);
		if (cps.empty())
			return kFalse;

		char hex[16];
		std::snprintf(hex, sizeof(hex), "%x", static_cast<unsigned int>(cps[0]));
		outClass = "Custom-";
		outClass += hex;
		return kTrue;
	}

	for (size_t i = 0; i < value.size(); ++i)
	{
		if (!IsClassChar(value[i]))
			return kFalse;
	}
	outClass = value;
	return kTrue;
}

bool16 KentenValueOfClass(const std::string& cls, std::string& outValue)
{
	outValue.clear();
	if (cls.empty())
		return kFalse;

	for (size_t i = 0; i < cls.size(); ++i)
	{
		if (!IsClassChar(cls[i]))
			return kFalse;
	}

	const size_t kCustomLen = 7;			// "Custom-"
	if (cls.size() > kCustomLen && cls.compare(0, kCustomLen, "Custom-") == 0)
	{
		int32 cp = 0;
		for (size_t i = kCustomLen; i < cls.size(); ++i)
		{
			const int32 digit = HexDigit(cls[i]);
			if (digit < 0)
				return kFalse;
			cp = cp * 16 + digit;
			if (cp > 0x10FFFF)
				return kFalse;
		}
		if (cp <= 0)
			return kFalse;

		outValue = "Custom:";
		KCMParaText::AppendUtf8(outValue, cp);
		return kTrue;
	}

	outValue = cls;
	return kTrue;
}

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

void CollectKentenValues(const Story& s, std::vector<std::string>& inOutSeen)
{
	CollectKenten(s.fBody, inOutSeen);
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
		{
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				CollectKenten(s.fTables[t].fRows[r].fCells[c].fParas, inOutSeen);
		}
	}
	for (size_t n = 0; n < s.fNotes.size(); ++n)
		CollectKenten(s.fNotes[n], inOutSeen);
}

namespace
{

/** A number as text, for the places a difference has to be named. */
std::string Num(size_t n)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned int>(n));
	return std::string(buf);
}

bool16 SameSpans(const KCMAttrSpanList& a, const KCMAttrSpanList& b, const std::string& where,
				 const char* what, std::string& outWhy)
{
	if (a.size() != b.size())
	{
		outWhy = where + ": " + what + " count " + Num(a.size()) + " became " + Num(b.size());
		return kFalse;
	}
	for (size_t k = 0; k < a.size(); ++k)
	{
		// ⚠**EVERYTHING IS COMPARED, THE KIND INCLUDED.** A reading over one character used to be
		//  the one thing that could not survive the trip - mono and group are spelt the same way
		//  there - and the check had to look away from it. It does not any more: both sides settle
		//  a one-character reading as MONO before it is written (the user's rule, 2026-09-16), so
		//  a difference here is a real one.
		if (a[k].fStart != b[k].fStart || a[k].fLen != b[k].fLen || a[k].fValue != b[k].fValue
			|| a[k].fGroup != b[k].fGroup)
		{
			outWhy = where + ": " + what + " " + Num(k) + " differs";
			return kFalse;
		}
	}
	return kTrue;
}

/** Where the footnotes' references stand, compared only when the caller's format carries them
	(KCMStoryShape.h, Same's withNoteRefs says when the places are compared). */
bool16 SameNoteRefs(const std::vector<NoteRef>& a, const std::vector<NoteRef>& b,
					const std::string& where, std::string& outWhy)
{
	if (a.size() != b.size())
	{
		outWhy = where + ": " + Num(a.size()) + " note reference(s) became " + Num(b.size());
		return kFalse;
	}
	for (size_t k = 0; k < a.size(); ++k)
	{
		if (a[k].fAt != b[k].fAt || a[k].fNote != b[k].fNote)
		{
			outWhy = where + ": note reference " + Num(k) + " differs";
			return kFalse;
		}
	}
	return kTrue;
}

bool16 SameParas(const std::vector<Para>& a, const std::vector<Para>& b, const std::string& where,
				 bool16 withNoteRefs, std::string& outWhy)
{
	if (a.size() != b.size())
	{
		outWhy = where + ": " + Num(a.size()) + " paragraph(s) became " + Num(b.size());
		return kFalse;
	}
	for (size_t i = 0; i < a.size(); ++i)
	{
		const std::string here = where + " paragraph " + Num(i);
		if (a[i].fText != b[i].fText)
		{
			outWhy = here + ": the text differs";
			return kFalse;
		}
		if (!SameSpans(a[i].fRuby, b[i].fRuby, here, "ruby", outWhy))
			return kFalse;
		if (!SameSpans(a[i].fKenten, b[i].fKenten, here, "kenten", outWhy))
			return kFalse;
		if (!SameSpans(a[i].fTcy, b[i].fTcy, here, "tate-chu-yoko", outWhy))
			return kFalse;
		if (!SameSpans(a[i].fWarichu, b[i].fWarichu, here, "warichu", outWhy))
			return kFalse;
		if (withNoteRefs && !SameNoteRefs(a[i].fNoteRefs, b[i].fNoteRefs, here, outWhy))
			return kFalse;
	}
	return kTrue;
}

}	// anonymous namespace

bool16 Same(const Story& a, const Story& b, std::string& outWhy, bool16 withNoteRefs)
{
	outWhy.clear();

	if (a.fVertical != b.fVertical)
	{
		outWhy = "the writing direction changed";
		return kFalse;
	}

	if (!SameParas(a.fBody, b.fBody, "the body", withNoteRefs, outWhy))
		return kFalse;

	if (a.fNotes.size() != b.fNotes.size())
	{
		outWhy = Num(a.fNotes.size()) + " note(s) became " + Num(b.fNotes.size());
		return kFalse;
	}
	for (size_t n = 0; n < a.fNotes.size(); ++n)
	{
		if (!SameParas(a.fNotes[n], b.fNotes[n], "note " + Num(n + 1), withNoteRefs, outWhy))
			return kFalse;
	}

	if (a.fTables.size() != b.fTables.size())
	{
		outWhy = Num(a.fTables.size()) + " table(s) became " + Num(b.fTables.size());
		return kFalse;
	}
	for (size_t t = 0; t < a.fTables.size(); ++t)
	{
		const Table& x = a.fTables[t];
		const Table& y = b.fTables[t];
		const std::string where = "table " + Num(t);

		if (x.fInTable != y.fInTable || x.fInRow != y.fInRow || x.fInCell != y.fInCell)
		{
			outWhy = where + ": it stands somewhere else";
			return kFalse;
		}
		if (x.fParaIndex != y.fParaIndex || x.fOffset != y.fOffset)
		{
			outWhy = where + ": its place in the paragraph changed";
			return kFalse;
		}
		if (x.fRows.size() != y.fRows.size())
		{
			outWhy = where + ": " + Num(x.fRows.size()) + " row(s) became " + Num(y.fRows.size());
			return kFalse;
		}
		for (size_t r = 0; r < x.fRows.size(); ++r)
		{
			if (x.fRows[r].fHeader != y.fRows[r].fHeader)
			{
				outWhy = where + " row " + Num(r) + ": header or not changed";
				return kFalse;
			}
			if (x.fRows[r].fCells.size() != y.fRows[r].fCells.size())
			{
				outWhy = where + " row " + Num(r) + ": " + Num(x.fRows[r].fCells.size())
						 + " cell(s) became " + Num(y.fRows[r].fCells.size());
				return kFalse;
			}
			for (size_t c = 0; c < x.fRows[r].fCells.size(); ++c)
			{
				const Cell& p = x.fRows[r].fCells[c];
				const Cell& q = y.fRows[r].fCells[c];
				const std::string cell = where + " row " + Num(r) + " cell " + Num(c);
				if (p.fColSpan != q.fColSpan || p.fRowSpan != q.fRowSpan)
				{
					outWhy = cell + ": its span changed";
					return kFalse;
				}
				if (!SameParas(p.fParas, q.fParas, cell, withNoteRefs, outWhy))
					return kFalse;
			}
		}
	}

	return kTrue;
}

}	// namespace KCMStoryShape

// End, KCMStoryShape.cpp.
