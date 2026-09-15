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
#include "KCMTextDiff.h"	// ToCodePoints - the ONE place that knows how to walk UTF-8

#include <algorithm>
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
	"p", "ruby", "rt", "em", "table", "tr", "td", "sup", "a", "ol", "li", "span",
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

/** Where the next closing tag called `name` stands, and where it ends. */
bool16 FindClosing(const std::string& s, size_t from, const char* name, size_t& outAt, size_t& outAfter)
{
	size_t j = from;
	while (j < s.size())
	{
		if (s[j] != '<')
		{
			++j;
			continue;
		}
		std::string tag;
		bool16 closing = kFalse;
		size_t after = 0;
		if (TagAt(s, j, tag, closing, after) && closing && tag == name)
		{
			outAt = j;
			outAfter = after;
			return kTrue;
		}
		++j;
	}
	return kFalse;
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
	std::vector<int32> cps;
	std::vector<int32> byteAt;
	KCMTextDiff::ToCodePoints(text, &cps, &byteAt);

	for (size_t k = 0; k < cps.size(); ++k)
	{
		// ★★AN INVISIBLE CHARACTER IS WRITTEN AS ITS OWN NAME, never as itself. Left raw it would
		//   be a control character sitting in a file somebody edits, and the first thing an editor
		//   does with one of those is lose it. The class IS the code point, so a character this
		//   build has never heard of travels as correctly as one it has.
		if (IsInvisible(cps[k]))
		{
			char hex[16];
			std::snprintf(hex, sizeof(hex), "%04x", static_cast<unsigned int>(cps[k]));
			out += "<span class=\"u";
			out += hex;
			out += "\"></span>";
			continue;
		}

		const size_t from = static_cast<size_t>(byteAt[k]);
		const size_t to = (k + 1 < byteAt.size()) ? static_cast<size_t>(byteAt[k + 1]) : text.size();

		if (text[from] == '<' && IsOurTagAt(text, from))
			out += '<';					// the one escape
		out.append(text, from, to - from);
	}
}

bool16 IsClassChar(char c)
{
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '-' || c == '_') ? kTrue : kFalse;
}

/** A colspan or rowspan, which is at least 1 whatever it says. */
int32 ReadCount(const std::string& text)
{
	int32 value = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] < '0' || text[i] > '9')
			break;
		value = value * 10 + (text[i] - '0');
		if (value > 10000)
			break;
	}
	return (value > 0) ? value : 1;
}

int32 HexDigit(char c)
{
	if (c >= '0' && c <= '9')	return c - '0';
	if (c >= 'a' && c <= 'f')	return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')	return c - 'A' + 10;
	return -1;
}

/*	ClassOfTag
	The class="..." of the tag standing between `at` and `after`.

	⚠**THE ONLY ATTRIBUTE THIS FORMAT EVER READS on an element of its own**, and it is read rather
	 than parsed: a full attribute walk would be a bigger thing to be wrong about than the one
	 value anybody writes here.
*/
bool16 AttrOfTag(const std::string& s, size_t at, size_t after, const char* attr,
				 std::string& outValue)
{
	outValue.clear();

	size_t attrLen = 0;
	while (attr[attrLen] != '\0')
		++attrLen;
	if (attrLen == 0 || after <= at + attrLen)
		return kFalse;

	for (size_t i = at; i + attrLen < after; ++i)
	{
		bool16 same = kTrue;
		for (size_t k = 0; k < attrLen; ++k)
		{
			if (Lower(s[i + k]) != attr[k])
			{
				same = kFalse;
				break;
			}
		}
		if (!same)
			continue;

		size_t j = i + attrLen;
		while (j < after && (s[j] == ' ' || s[j] == '\t'))
			++j;
		if (j >= after || s[j] != '=')
			continue;
		++j;
		while (j < after && (s[j] == ' ' || s[j] == '\t'))
			++j;
		if (j >= after)
			return kFalse;

		char quote = 0;
		if (s[j] == '"' || s[j] == '\'')
		{
			quote = s[j];
			++j;
		}

		const size_t from = j;
		while (j < after)
		{
			const char c = s[j];
			if (quote != 0 && c == quote)
				break;
			if (quote == 0 && (c == ' ' || c == '\t' || c == '>' || c == '/'))
				break;
			++j;
		}
		outValue.assign(s, from, j - from);
		return kTrue;
	}
	return kFalse;
}

bool16 ClassOfTag(const std::string& s, size_t at, size_t after, std::string& outClass)
{
	return AttrOfTag(s, at, after, "class", outClass);
}

/** A table being read. fSlot is its place in Story::fTables, taken when it OPENS.

	★**THE SLOT IS RESERVED AT THE OPENING TAG** so that a nested table, which closes first, does
	not end up standing in front of the one it is inside. Document order is the order the tables
	BEGIN in, which is the order anybody reading the file sees them. */
struct TableFrame
{
	size_t	fSlot;
	Table	fTable;
	bool16	fInHead;

	TableFrame() : fSlot(0), fInHead(kFalse) {}
};

/** An <em> that has been opened and not yet closed. */
struct EmState
{
	bool16		fOpen;
	int32		fStart;		// where it began, in the paragraph's code points
	std::string	fValue;

	EmState() : fOpen(kFalse), fStart(0) {}
};

/** How many code points are in this UTF-8 string.

	★**ASKED OF KCMTextDiff, NEVER COUNTED HERE.** KCMAttrSpan's offsets are code points - its own
	comment is the contract - and the differ already owns the walk that turns bytes into them. A
	second walker in this file would be a second place to be wrong about a lead byte, and the two
	would disagree only on the documents nobody tests with. */
int32 CountCodePoints(const std::string& utf8)
{
	std::vector<int32> cps;
	KCMTextDiff::ToCodePoints(utf8, &cps, nil);
	return static_cast<int32>(cps.size());
}

/** The byte where code point `cp` begins; the string's length once cp is past the end. */
size_t ByteAtCodePoint(const std::string& text, const std::vector<int32>& byteAt, int32 cp)
{
	if (cp <= 0)
		return 0;
	if (cp >= static_cast<int32>(byteAt.size()))
		return text.size();
	return static_cast<size_t>(byteAt[static_cast<size_t>(cp)]);
}

/*	WriteRun
	The code points [from, to) of `text`, cut where the kenten changes and wrapped in <em>.

	★**EVERY PIECE OF TEXT THIS FILE WRITES GOES THROUGH HERE**, the base of a reading included -
	which is what lets a kenten and a ruby stand on the same word: the <em> simply ends up inside
	the <ruby>, where HTML is happy to have it. Writing the two separately would have meant deciding
	which of them owns a character they share, and there is no answer to that.

	⚠A MARK WHOSE NAME CANNOT BE A CLASS is written as plain text - the words are never lost, only
	 the mark. It cannot arise from KCMTextRead (its names are ASCII), and a silent loss would be
	 worse than the alternative only if there were an alternative: Write's contract is that it
	 never fails.
*/
void WriteRun(const std::string& text, const std::vector<int32>& byteAt,
			  const KCMAttrSpanList& kenten, int32 from, int32 to, std::string& out)
{
	int32 cp = from;
	while (cp < to)
	{
		const KCMAttrSpan* cover = nil;
		for (size_t k = 0; k < kenten.size(); ++k)
		{
			if (kenten[k].fLen > 0
				&& kenten[k].fStart <= cp
				&& cp < kenten[k].fStart + kenten[k].fLen)
			{
				cover = &kenten[k];
				break;
			}
		}

		int32 stop = to;
		if (cover != nil)
		{
			const int32 coverEnd = cover->fStart + cover->fLen;
			if (coverEnd < stop)
				stop = coverEnd;
		}
		else
		{
			for (size_t k = 0; k < kenten.size(); ++k)
			{
				if (kenten[k].fLen > 0 && kenten[k].fStart > cp && kenten[k].fStart < stop)
					stop = kenten[k].fStart;
			}
		}

		std::string cls;
		const bool16 wrapped = (cover != nil && KentenClassOf(cover->fValue, cls)) ? kTrue : kFalse;
		if (wrapped)
		{
			out += "<em class=\"kenten-";
			out += cls;
			out += "\">";
		}

		const size_t a = ByteAtCodePoint(text, byteAt, cp);
		const size_t b = ByteAtCodePoint(text, byteAt, stop);
		if (b > a)
			WriteText(text.substr(a, b - a), out);

		if (wrapped)
			out += "</em>";

		cp = stop;
	}
}

/*	TakeEm
	One <em> or </em>, wherever it stands - in a paragraph, or inside a reading's base.

	★**A CLASS THIS FORMAT DID NOT WRITE IS NOT AN ERROR.** No class, or somebody's own styling
	class, means the default mark: the whole reason this is HTML is that a person - or Claude, asked
	in a chat to put kenten on a word - can write <em>word</em> and have it work. A class of OURS
	that will not parse IS an error, because it is a mistake rather than a request.
*/
bool16 TakeEm(const std::string& s, size_t at, size_t after, bool16 closing,
			  const std::string& para, EmState& em, KCMAttrSpanList& outKenten,
			  std::string& whyNot)
{
	const size_t kPrefixLen = 7;			// "kenten-"

	if (!closing)
	{
		if (em.fOpen)
		{
			whyNot = "an <em> begins inside another one";
			return kFalse;
		}

		std::string value = kKentenDefaultValue;
		std::string cls;
		if (ClassOfTag(s, at, after, cls) && cls.compare(0, kPrefixLen, "kenten-") == 0)
		{
			if (!KentenValueOfClass(cls.substr(kPrefixLen), value))
			{
				whyNot = "an <em> carries a mark that cannot be read: " + cls;
				return kFalse;
			}
		}

		em.fOpen = kTrue;
		em.fStart = CountCodePoints(para);
		em.fValue = value;
		return kTrue;
	}

	if (!em.fOpen)
	{
		whyNot = "an </em> closes a mark that never began";
		return kFalse;
	}

	// ⚠A MARK OVER NO CHARACTERS IS NO MARK - the same rule the readings follow.
	const int32 len = CountCodePoints(para) - em.fStart;
	if (len > 0)
		outKenten.push_back(KCMAttrSpan(em.fStart, len, em.fValue));

	em.fOpen = kFalse;
	em.fValue.clear();
	return kTrue;
}

/*	TakeSpan
	One <span class="uXXXX">, wherever it stands: the character it names joins the paragraph.

	★**THE CLOSING TAG IS OPTIONAL.** HTML5 ignores the '/' in <span/>, so a self-closed one would
	swallow the rest of the document - which is why the format spells it with a real closing tag
	and why the reader does not depend on finding one. When </span> stands RIGHT THERE it is eaten;
	when it does not, nothing is assumed and the text carries on.

	⚠**BUT TEXT INSIDE ONE IS REFUSED.** "<span class=\"ufffc\">something</span>" is not a shape
	 this format has, and reading it either way - dropping the words or keeping them - would be a
	 guess about what the writer meant.
	⚠A stray </span> is ignored rather than refused, for the same reason the opening one does not
	 need it: it carries no meaning of its own.
*/
bool16 TakeSpan(const std::string& s, size_t at, size_t after, bool16 closing,
				std::string& para, size_t& outNext, std::string& whyNot)
{
	outNext = after;

	if (closing)
		return kTrue;

	std::string cls;
	if (!ClassOfTag(s, at, after, cls) || cls.size() < 2 || Lower(cls[0]) != 'u')
	{
		whyNot = "a <span> carries a class this format does not write: " + cls;
		return kFalse;
	}

	int32 cp = 0;
	for (size_t k = 1; k < cls.size(); ++k)
	{
		const int32 digit = HexDigit(cls[k]);
		if (digit < 0)
		{
			whyNot = "a <span> names a character that cannot be read: " + cls;
			return kFalse;
		}
		cp = cp * 16 + digit;
		if (cp > 0x10FFFF)
		{
			whyNot = "a <span> names a character outside Unicode: " + cls;
			return kFalse;
		}
	}
	if (cp <= 0)
	{
		whyNot = "a <span> names no character: " + cls;
		return kFalse;
	}

	KCMParaText::AppendUtf8(para, cp);

	// The closing tag, when it stands right there - and a refusal when words stand in between.
	size_t j = after;
	while (j < s.size() && s[j] != '<')
		++j;
	if (j < s.size())
	{
		std::string name;
		bool16 innerClosing = kFalse;
		size_t innerAfter = 0;
		if (TagAt(s, j, name, innerClosing, innerAfter) && name == "span" && innerClosing)
		{
			if (j > after)
			{
				whyNot = "a <span> standing for an invisible character carries text";
				return kFalse;
			}
			outNext = innerAfter;
		}
	}
	return kTrue;
}

bool SpanStartsEarlier(const KCMAttrSpan& a, const KCMAttrSpan& b)
{
	return a.fStart < b.fStart;
}

/*	WriteParaHtml
	One paragraph's text with its readings standing over it.

	★★**THE SHAPE OF THE <rt> IS THE MONO/GROUP SETTING.** One reading over the whole base is a
	GROUP ruby; a reading per character is MONO, and it is written as ONE <ruby> holding the base
	split into pieces with an <rt> after each. That is KCM's own spelling and NOT Adobe's - measured
	2026-09-15, Adobe's own HTML export drops the difference entirely (both come out <ruby><rt>,
	with the same generated class). It is carried because turning one into the other IS a change
	even when every reading stays the same, and the panel says which it now is.

	⚠**A LONE MONO SPAN COMES BACK AS GROUP.** One character with one reading is written the same
	 way either way, so the reader cannot tell - and does not guess, it answers GROUP. Whoever
	 applies the result keeps the setting the document already had (the design says so, 3-3).
*/
/** Every note reference standing exactly at cp.

	★**THE LINK'S TARGET IS THE NOTE'S ORDINAL, ITS TEXT IS THE NUMBER THE PAGE PRINTS.** The two
	are the same in a document that numbers its notes from one and never restarts, and different in
	one that does - so pairing on the ordinal is what keeps a restarting document's links honest
	while the reader still sees the number InDesign would print. */
void WriteNotesAt(const Para& p, int32 cp, int32& ordinal, std::string& out)
{
	for (size_t k = 0; k < p.fNoteAt.size(); ++k)
	{
		if (p.fNoteAt[k] != cp)
			continue;

		++ordinal;
		const int32 shown = (k < p.fNoteNum.size()) ? p.fNoteNum[k] : ordinal;
		char buf[96];
		std::snprintf(buf, sizeof(buf), "<sup><a href=\"#n%d\">%d</a></sup>",
					  static_cast<int>(ordinal), static_cast<int>(shown));
		out += buf;
	}
}

/** The nearest note position strictly after cp, or `limit` when there is none. */
int32 NextNoteAfter(const Para& p, int32 cp, int32 limit)
{
	int32 stop = limit;
	for (size_t k = 0; k < p.fNoteAt.size(); ++k)
	{
		if (p.fNoteAt[k] > cp && p.fNoteAt[k] < stop)
			stop = p.fNoteAt[k];
	}
	return stop;
}

void WriteParaHtml(const Para& p, int32& noteOrdinal, std::string& out)
{
	std::vector<int32> byteAt;
	KCMTextDiff::ToCodePoints(p.fText, nil, &byteAt);
	const int32 cpCount = static_cast<int32>(byteAt.size());

	std::vector<KCMAttrSpan> spans = p.fRuby;
	std::sort(spans.begin(), spans.end(), SpanStartsEarlier);

	int32 cp = 0;
	size_t next = 0;
	int32 notesWrittenUpTo = -1;
	while (cp < cpCount || next < spans.size())
	{
		// ⚠ONCE PER POSITION. The span-skipping branch below leaves cp where it is, and a note
		//   written twice is a note the reader would have to guess about.
		if (cp > notesWrittenUpTo)
		{
			WriteNotesAt(p, cp, noteOrdinal, out);
			notesWrittenUpTo = cp;
		}

		// A span measuring nothing is not a span, and one that ends behind us cannot be placed:
		// both are dropped rather than guessed at (KCMParaText.h applies the same rule to a
		// reading with no text under it).
		if (next < spans.size()
			&& (spans[next].fLen <= 0 || spans[next].fStart + spans[next].fLen <= cp))
		{
			++next;
			continue;
		}

		if (next < spans.size() && spans[next].fStart <= cp)
		{
			// ---- the run: this span, plus every MONO one that carries straight on from it -----
			size_t last = next;
			if (spans[next].fGroup == kFalse)
			{
				int32 end = spans[next].fStart + spans[next].fLen;
				while (last + 1 < spans.size()
					   && spans[last + 1].fGroup == kFalse
					   && spans[last + 1].fLen > 0
					   && spans[last + 1].fStart == end)
				{
					++last;
					end = spans[last].fStart + spans[last].fLen;
				}
			}

			out += "<ruby>";
			for (size_t k = next; k <= last; ++k)
			{
				// ★THE BASE GOES THROUGH WriteRun LIKE EVERYTHING ELSE, so a kenten standing on
				//   these same characters comes out as an <em> inside the reading.
				WriteRun(p.fText, byteAt, p.fKenten, spans[k].fStart,
						 spans[k].fStart + spans[k].fLen, out);
				out += "<rt>";
				WriteText(spans[k].fValue, out);
				out += "</rt>";
			}
			out += "</ruby>";

			cp = spans[last].fStart + spans[last].fLen;
			next = last + 1;
			continue;
		}

		// ---- plain text, as far as the next reading (or the end) ------------------------------
		int32 stop = (next < spans.size() && spans[next].fStart > cp) ? spans[next].fStart
																	 : cpCount;
		// ★A NOTE REFERENCE IS A PLACE, SO IT CUTS THE TEXT like a reading does.
		stop = NextNoteAfter(p, cp, stop);
		if (stop <= cp)
			break;					// nothing left to write; a malformed span cannot loop us

		WriteRun(p.fText, byteAt, p.fKenten, cp, stop, out);
		cp = stop;
	}

	// ⚠A NOTE HANGING OFF THE LAST CHARACTER stands at the paragraph's end, where the loop above
	//   has already stopped.
	if (cpCount > notesWrittenUpTo)
		WriteNotesAt(p, cpCount, noteOrdinal, out);
}

/*	WriteCellParas
	A cell's paragraphs.

	★**ONE PARAGRAPH IS WRITTEN WITHOUT <p>**, which is a departure from Adobe's export (it always
	writes one) and from the <li> above. The design chose it for the reader's sake: a table of
	one-word cells is read at a glance without <p> in the way, and the reader accepts both.
*/
void WriteCellParas(const std::vector<Para>& paras, std::string& out)
{
	int32 insideCell = 0;			// a cell's own notes are not the body's

	if (paras.size() == 1)
	{
		WriteParaHtml(paras[0], insideCell, out);
		return;
	}
	for (size_t k = 0; k < paras.size(); ++k)
	{
		out += "<p>";
		WriteParaHtml(paras[k], insideCell, out);
		out += "</p>";
	}
}

/*	WriteTable
	One table.

	⚠**A NESTED TABLE IS NOT WRITTEN HERE.** A Cell holds paragraphs, not tables - the nested one
	 is a separate entry in Story::fTables, and nothing in this shape says which cell it belongs
	 to. The reader takes them (document order, their own entries); putting one back is Task 9's
	 problem and is written down as unsolved rather than half-done.
*/
void WriteTable(const Table& t, std::string& out)
{
	out += "<table>";

	bool16 inHead = kFalse;
	for (size_t r = 0; r < t.fRows.size(); ++r)
	{
		const Row& row = t.fRows[r];
		if (row.fHeader && !inHead)
		{
			out += "<thead>";
			inHead = kTrue;
		}
		else if (!row.fHeader && inHead)
		{
			out += "</thead>";
			inHead = kFalse;
		}

		out += "<tr>";
		for (size_t c = 0; c < row.fCells.size(); ++c)
		{
			const Cell& cell = row.fCells[c];
			out += "<td";
			if (cell.fColSpan != 1)
			{
				char buf[32];
				std::snprintf(buf, sizeof(buf), " colspan=\"%d\"", static_cast<int>(cell.fColSpan));
				out += buf;
			}
			if (cell.fRowSpan != 1)
			{
				char buf[32];
				std::snprintf(buf, sizeof(buf), " rowspan=\"%d\"", static_cast<int>(cell.fRowSpan));
				out += buf;
			}
			out += ">";
			WriteCellParas(cell.fParas, out);
			out += "</td>";
		}
		out += "</tr>";
	}
	if (inHead)
		out += "</thead>";

	out += "</table>\r\n";
}

/*	kKentenLooks
	InDesign's marks, and the CSS that draws each one.

	★★**ALL ELEVEN OF INDESIGN'S KINDS HAVE A CSS SPELLING** - text-emphasis-style is in the
	standard for exactly this purpose - and the two that are easy to doubt are the double circles:
	the bullseye (janome) is `open double-circle` and the fisheye is `filled double-circle`. The
	eleventh, a custom mark, is a quoted string, which is written from the document's own data.

	⚠**WHAT CSS DOES NOT HAVE is the finer setting**: the mark's SIZE, its FONT, and a numeric
	 OFFSET (it has the position keyword and the colour, not those three). That costs this format
	 nothing, because KCM does not read them either - KCMTextRead takes the KIND and, for a custom
	 mark, its character, and KCMApplyKentenKind writes the kind with "the look left alone". So
	 those settings stay in the document, untouched, on both sides of the trip.
	⚠**AND CSS IS WIDER IN ONE PLACE**: it allows a string of several characters, while InDesign's
	 custom mark is a single BMP character (an int16 attribute). That one is refused rather than
	 silently cut down to its first character.
	⚠NOTHING HERE IS EVER READ BACK. The class carries the truth; this is the look.
*/
struct KentenLook
{
	const char*	fName;
	const char*	fStyle;
};

const KentenLook kKentenLooks[] =
{
	{ "BlackSesameDot",		"filled sesame" },
	{ "WhiteSesameDot",		"open sesame" },
	{ "BlackCircle",		"filled circle" },
	{ "WhiteCircle",		"open circle" },
	{ "SmallBlackCircle",	"filled dot" },
	{ "SmallWhiteCircle",	"open dot" },
	{ "BlackTriangle",		"filled triangle" },
	{ "WhiteTriangle",		"open triangle" },
	{ "Bullseye",			"open double-circle" },
	{ "Fisheye",			"filled double-circle" }
};
const size_t kKentenLookCount = sizeof(kKentenLooks) / sizeof(kKentenLooks[0]);

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

/*	WriteKentenStyles
	A rule for each mark THIS story actually uses, and none for the rest.
*/
void WriteKentenStyles(const Story& s, std::string& out)
{
	std::vector<std::string> seen;
	CollectKenten(s.fBody, seen);
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
		{
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				CollectKenten(s.fTables[t].fRows[r].fCells[c].fParas, seen);
		}
	}
	for (size_t n = 0; n < s.fNotes.size(); ++n)
		CollectKenten(s.fNotes[n], seen);

	for (size_t i = 0; i < seen.size(); ++i)
	{
		std::string cls;
		if (!KentenClassOf(seen[i], cls))
			continue;

		std::string style;
		const size_t kCustomLen = 7;		// "Custom:"
		if (seen[i].size() > kCustomLen && seen[i].compare(0, kCustomLen, "Custom:") == 0)
		{
			// ★The mark itself, quoted. It comes from the document, never from a literal here.
			style = "\"";
			const std::string mark = seen[i].substr(kCustomLen);
			for (size_t c = 0; c < mark.size(); ++c)
			{
				if (mark[c] == '"' || mark[c] == '\\')
					style += '\\';
				style += mark[c];
			}
			style += "\"";
		}
		else
		{
			for (size_t k = 0; k < kKentenLookCount; ++k)
			{
				if (seen[i] == kKentenLooks[k].fName)
				{
					style = kKentenLooks[k].fStyle;
					break;
				}
			}
			if (style.empty())
				continue;				// a kind we have no drawing for: the em rule covers it
		}

		out += ".kenten-";
		out += cls;
		out += "{-webkit-text-emphasis-style:";
		out += style;
		out += ";text-emphasis-style:";
		out += style;
		out += "}\r\n";
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
	// ★★THE STYLESHEET IS WHERE THIS FORMAT EXPLAINS ITSELF. A kenten IS stress emphasis, CSS has
	//   text-emphasis-style for exactly that, so <em> is the mark - and this rule says so to anyone
	//   who opens the file, including a reader who writes one of their own by hand.
	out += "em{font-style:normal;-webkit-text-emphasis-style:filled sesame;"
		   "text-emphasis-style:filled sesame}\r\n";
	WriteKentenStyles(s, out);
	// ★★AND THE INVISIBLE CHARACTERS GET A FACE. Each one is an empty <span> whose class is its
	//   code point, so the browser shows nothing at all unless the stylesheet draws something -
	//   and a reader who cannot see a thing will delete it. The marks are SYMBOLS rather than
	//   words on purpose: the stylesheet then needs no language, and these strings stay ASCII in
	//   the source (CSS's own \XXXX escape carries the character, so no literal here is non-ASCII
	//   - which is the rule this file keeps for everything it writes).
	out += "span[class^=\"u\"]{color:#999;font-size:.85em}\r\n";
	out += "span[class^=\"u\"]::before{content:\"\\25CC\"}\r\n";		// dotted circle: something is here
	out += ".u000a::before{content:\"\\23CE\"}\r\n";					// return symbol
	out += ".u000a::after{content:\"\\A\";white-space:pre}\r\n";		// and it really breaks the line
	out += ".ufffc::before{content:\"\\25A3\"}\r\n";					// framed square: anchored object
	out += ".u0018::before{content:\"#\"}\r\n";						// auto page number / variable
	out += ".u0019::before{content:\"\\00A7\"}\r\n";					// section marker
	out += ".u0008::before{content:\"\\21E5\"}\r\n";					// right indent tab
	out += ".u0007::before{content:\"\\21B1\"}\r\n";					// indent to here
	out += ".u00ad::before{content:\"\\2010\"}\r\n";					// discretionary hyphen
	out += ".ue02c::before{content:\"\\2318\"}\r\n";					// index marker
	out += "</style></head>\r\n";
	out += "<body>\r\n";

	int32 noteOrdinal = 0;
	for (size_t i = 0; i < s.fBody.size(); ++i)
	{
		// ★**"THIS PARAGRAPH IS THE REST OF THE ONE BEFORE THE TABLE."** A table can stand in the
		//   middle of a paragraph (measured - KCMTextRead::TakeAttrFor says where), and the two
		//   halves arrive as two paragraphs. class="c" is what tells the reader they were one.
		bool16 continuation = kFalse;
		for (size_t t = 0; t < s.fTables.size() && !continuation; ++t)
		{
			if (s.fTables[t].fSplitsPara && i > 0
				&& s.fTables[t].fParaIndex == static_cast<int32>(i) - 1)
				continuation = kTrue;
		}

		out += continuation ? "<p class=\"c\">" : "<p>";
		WriteParaHtml(s.fBody[i], noteOrdinal, out);
		out += "</p>\r\n";

		// The tables that hang off this paragraph, in the order they were given.
		for (size_t t = 0; t < s.fTables.size(); ++t)
		{
			if (s.fTables[t].fParaIndex == static_cast<int32>(i))
				WriteTable(s.fTables[t], out);
		}
	}

	// ★THE NOTES STAND AT THE END, LINKED, which is how Adobe's own HTML export writes them and
	//   what makes them work in a browser: the reference is a link and the note is its target.
	//   ⚠An ENDNOTE's words are not here - they are a story of their own, with a file of their own.
	if (!s.fNotes.empty())
	{
		out += "<ol>\r\n";
		for (size_t n = 0; n < s.fNotes.size(); ++n)
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "<li id=\"n%d\">", static_cast<int>(n + 1));
			out += buf;

			// ⚠A NOTE'S PARAGRAPHS ARE PARAGRAPHS - ruby, kenten and the invisible characters all
			//   belong there, so they go through the very same writer.
			int32 insideNote = 0;
			for (size_t k = 0; k < s.fNotes[n].size(); ++k)
			{
				out += "<p>";
				WriteParaHtml(s.fNotes[n][k], insideNote, out);
				out += "</p>";
			}
			out += "</li>\r\n";
		}
		out += "</ol>\r\n";
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
	KCMAttrSpanList paraRuby;		// the readings met so far, in this paragraph's own count
	KCMAttrSpanList paraKenten;		// and the marks, in the same count
	EmState em;
	std::vector<int32> paraNoteAt;	// where a note hangs off this paragraph
	std::vector<int32> paraNoteNum;	// and the number its page prints

	// ★THE NOTES ARE COLLECTED BY ID and put in order at the end, because the <ol> stands after
	//   the body: nothing can be paired until the whole document has been read.
	std::vector<int32> refOrdinals;					// every ordinal a <sup> pointed at
	std::vector<int32> noteIds;						// the id of each <li> met
	std::vector< std::vector<Para> > noteBodies;	// and its paragraphs
	std::vector<Para> noteParas;					// the <li> being read now
	int32 noteId = 0;
	bool16 inNote = kFalse;

	std::vector<TableFrame> tables;	// the tables open right now; more than one means nesting
	bool16 inCell = kFalse;
	bool16 implicitPara = kFalse;	// a cell's text with no <p> around it is still a paragraph

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
					// ★A CELL'S IMPLICIT PARAGRAPH GIVES WAY TO A REAL ONE. <td> opens a paragraph
					//   so that bare text in a cell is not lost; a <p> arriving before any text
					//   means the cell was written the other way, and takes over.
					if (inPara && implicitPara && para.empty() && paraRuby.empty()
						&& paraKenten.empty() && paraNoteAt.empty())
					{
						implicitPara = kFalse;
					}
					else if (inPara)
					{
						whyNot = "a <p> begins inside another one";
						return kFalse;
					}
					else
					{
						inPara = kTrue;
						implicitPara = kFalse;
						para.clear();
						paraRuby.clear();
						paraKenten.clear();
						paraNoteAt.clear();
						paraNoteNum.clear();
						em = EmState();
					}

					// ★★"THIS PARAGRAPH IS THE REST OF THE ONE BEFORE THE TABLE." The table has
					//   already closed by the time this is read, so the mark is applied backwards -
					//   and the offset is simply how long the first half turned out to be.
					std::string cls;
					if (!inCell && !inNote && ClassOfTag(s, i, after, cls) && cls == "c")
					{
						const int32 prevIndex = static_cast<int32>(out.fBody.size()) - 1;
						for (size_t t = out.fTables.size(); t > 0; --t)
						{
							if (out.fTables[t - 1].fParaIndex == prevIndex)
							{
								out.fTables[t - 1].fSplitsPara = kTrue;
								if (prevIndex >= 0)
									out.fTables[t - 1].fOffset =
										CountCodePoints(out.fBody[static_cast<size_t>(prevIndex)].fText);
								break;
							}
						}
					}
				}
				else
				{
					if (!inPara)
					{
						whyNot = "a </p> closes a paragraph that never began";
						return kFalse;
					}
					if (em.fOpen)
					{
						whyNot = "a paragraph ends with an <em> still open";
						return kFalse;
					}
					Para p;
					p.fText = para;
					p.fRuby = paraRuby;
					p.fKenten = paraKenten;
					p.fNoteAt = paraNoteAt;
					p.fNoteNum = paraNoteNum;
					// ★A PARAGRAPH BELONGS TO WHATEVER IT STANDS IN: a cell, a note, or the body.
					if (inCell && !tables.empty()
						&& !tables.back().fTable.fRows.empty()
						&& !tables.back().fTable.fRows.back().fCells.empty())
					{
						tables.back().fTable.fRows.back().fCells.back().fParas.push_back(p);
					}
					else if (inNote)
					{
						noteParas.push_back(p);
					}
					else
					{
						out.fBody.push_back(p);
					}
					inPara = kFalse;
					implicitPara = kFalse;
					para.clear();
					paraRuby.clear();
					paraKenten.clear();
					paraNoteAt.clear();
					paraNoteNum.clear();

					// ⚠TEXT AFTER </p> INSIDE A CELL still belongs to the cell, so the implicit
					//   paragraph comes back. The empty leftover is dropped when </td> arrives.
					if (inCell)
					{
						inPara = kTrue;
						implicitPara = kTrue;
						em = EmState();
					}
				}
				i = after;
				continue;
			}

			if (name == "table")
			{
				if (!closing)
				{
					if (inPara && !implicitPara)
					{
						whyNot = "a <table> stands inside a paragraph";
						return kFalse;
					}

					TableFrame frame;
					frame.fSlot = out.fTables.size();
					frame.fTable.fOrdinal = static_cast<int32>(frame.fSlot);
					// ★WHERE IT STANDS = after the paragraph most recently closed.
					frame.fTable.fParaIndex = static_cast<int32>(out.fBody.size()) - 1;
					out.fTables.push_back(Table());		// the slot, taken at the opening tag
					tables.push_back(frame);
				}
				else
				{
					if (tables.empty())
					{
						whyNot = "a </table> closes a table that never began";
						return kFalse;
					}
					out.fTables[tables.back().fSlot] = tables.back().fTable;
					tables.pop_back();
				}
				i = after;
				continue;
			}

			if (name == "thead")
			{
				if (!tables.empty())
					tables.back().fInHead = closing ? kFalse : kTrue;
				i = after;
				continue;
			}

			if (name == "tbody" || name == "tfoot" || name == "colgroup" || name == "col")
			{
				// Written by editors that know HTML, carrying nothing this format needs.
				i = after;
				continue;
			}

			if (name == "tr")
			{
				if (!closing)
				{
					if (tables.empty())
					{
						whyNot = "a <tr> stands outside a table";
						return kFalse;
					}
					Row row;
					row.fHeader = tables.back().fInHead;
					tables.back().fTable.fRows.push_back(row);
				}
				i = after;
				continue;
			}

			if (name == "td")
			{
				if (tables.empty())
				{
					whyNot = "a <td> stands outside a table";
					return kFalse;
				}

				if (!closing)
				{
					if (tables.back().fTable.fRows.empty())
						tables.back().fTable.fRows.push_back(Row());

					Cell cell;
					std::string span;
					if (AttrOfTag(s, i, after, "colspan", span))
						cell.fColSpan = ReadCount(span);
					if (AttrOfTag(s, i, after, "rowspan", span))
						cell.fRowSpan = ReadCount(span);
					tables.back().fTable.fRows.back().fCells.push_back(cell);

					// ★A CELL OPENS A PARAGRAPH OF ITS OWN, so that text written straight into the
					//   cell - which is how this format writes a cell holding one - is not lost.
					inCell = kTrue;
					inPara = kTrue;
					implicitPara = kTrue;
					para.clear();
					paraRuby.clear();
					paraKenten.clear();
					paraNoteAt.clear();
					paraNoteNum.clear();
					em = EmState();
				}
				else
				{
					if (inPara)
					{
						bool16 hasParas = kFalse;
						if (!tables.back().fTable.fRows.empty()
							&& !tables.back().fTable.fRows.back().fCells.empty()
							&& !tables.back().fTable.fRows.back().fCells.back().fParas.empty())
							hasParas = kTrue;

						// ⚠AN EMPTY CELL STILL HAS ONE PARAGRAPH, because a cell in InDesign always
						//   does. What is dropped is the leftover implicit one after </p>.
						if (!implicitPara || !para.empty() || !hasParas)
						{
							Para p;
							p.fText = para;
							p.fRuby = paraRuby;
							p.fKenten = paraKenten;
							p.fNoteAt = paraNoteAt;
							p.fNoteNum = paraNoteNum;
							if (!tables.back().fTable.fRows.empty()
								&& !tables.back().fTable.fRows.back().fCells.empty())
								tables.back().fTable.fRows.back().fCells.back().fParas.push_back(p);
						}
						inPara = kFalse;
						implicitPara = kFalse;
					}
					inCell = kFalse;
				}
				i = after;
				continue;
			}

			if (name == "sup")
			{
				if (closing)
				{
					i = after;				// a stray </sup> means nothing of its own
					continue;
				}
				if (!inBody || !inPara)
				{
					whyNot = "a <sup> stands outside a paragraph";
					return kFalse;
				}

				size_t closeAt = 0;
				size_t closeAfter = 0;
				if (!FindClosing(s, after, "sup", closeAt, closeAfter))
				{
					whyNot = "a <sup> was never closed";
					return kFalse;
				}

				const std::string inside = s.substr(after, closeAt - after);
				const size_t hash = inside.find("#n");
				if (hash == std::string::npos)
				{
					whyNot = "a <sup> carries no link to a note "
							 "(this format writes <sup><a href=\"#n1\">1</a></sup>)";
					return kFalse;
				}

				int32 ordinal = 0;
				size_t q = hash + 2;
				while (q < inside.size() && inside[q] >= '0' && inside[q] <= '9')
				{
					ordinal = ordinal * 10 + (inside[q] - '0');
					++q;
				}
				if (ordinal <= 0)
				{
					whyNot = "a <sup> points at no note in particular";
					return kFalse;
				}

				// ★The number the page prints is the LINK'S TEXT; the ordinal stands in when that
				//   text is not a number (a note numbered with a letter, say).
				int32 shown = ordinal;
				const size_t gt = inside.find('>', q);
				if (gt != std::string::npos)
				{
					size_t r = gt + 1;
					int32 value = 0;
					bool16 any = kFalse;
					while (r < inside.size() && inside[r] >= '0' && inside[r] <= '9')
					{
						value = value * 10 + (inside[r] - '0');
						++r;
						any = kTrue;
					}
					if (any)
						shown = value;
				}

				paraNoteAt.push_back(CountCodePoints(para));
				paraNoteNum.push_back(shown);
				refOrdinals.push_back(ordinal);
				i = closeAfter;
				continue;
			}

			if (name == "ol")
			{
				i = after;				// the list carries nothing; its items do
				continue;
			}

			if (name == "li")
			{
				if (!closing)
				{
					if (inNote)
					{
						whyNot = "an <li> begins inside another one";
						return kFalse;
					}

					// ★THE ID IS THE PAIRING, and its absence is not fatal: a note with no id of
					//   its own takes the place it stands in.
					noteId = static_cast<int32>(noteIds.size()) + 1;
					std::string id;
					if (AttrOfTag(s, i, after, "id", id) && id.size() > 1 && Lower(id[0]) == 'n')
					{
						int32 value = 0;
						bool16 any = kTrue;
						for (size_t k = 1; k < id.size() && any; ++k)
						{
							if (id[k] < '0' || id[k] > '9')
								any = kFalse;
							else
								value = value * 10 + (id[k] - '0');
						}
						if (any && value > 0)
							noteId = value;
					}
					inNote = kTrue;
					noteParas.clear();
				}
				else
				{
					if (!inNote)
					{
						whyNot = "an </li> closes a note that never began";
						return kFalse;
					}
					noteIds.push_back(noteId);
					noteBodies.push_back(noteParas);
					noteParas.clear();
					inNote = kFalse;
				}
				i = after;
				continue;
			}

			// ⚠**REFUSED ON PURPOSE, AND WITH BOTH ANSWERS IN THE MESSAGE.** Somebody who knows
			//   HTML reaches for <br> meaning "a new line here", and InDesign has TWO of those -
			//   a new paragraph and a forced line break inside one. Taking a guess would silently
			//   produce the wrong one, so the reader stops and says how to spell each.
			if (name == "br" || name == "wbr")
			{
				whyNot = "<br> is not read here: a new paragraph is written </p><p>, "
						 "and a forced line break inside one is <span class=\"u000a\"></span>";
				return kFalse;
			}

			if (name == "span")
			{
				if (!inBody || !inPara)
				{
					whyNot = "a <span> stands outside a paragraph";
					return kFalse;
				}
				size_t next = after;
				if (!TakeSpan(s, i, after, closing, para, next, whyNot))
					return kFalse;
				i = next;
				continue;
			}

			if (name == "em")
			{
				if (!inBody || !inPara)
				{
					whyNot = "an <em> stands outside a paragraph";
					return kFalse;
				}
				if (!TakeEm(s, i, after, closing, para, em, paraKenten, whyNot))
					return kFalse;
				i = after;
				continue;
			}

			if (name == "ruby")
			{
				if (!inBody || !inPara)
				{
					whyNot = "a <ruby> stands outside a paragraph";
					return kFalse;
				}
				if (closing)
				{
					whyNot = "a </ruby> closes a reading that never began";
					return kFalse;
				}

				i = after;
				KCMAttrSpanList made;
				int32 fragStartCp = CountCodePoints(para);
				bool16 closed = kFalse;

				while (i < s.size())
				{
					if (s[i] == '<' && i + 1 < s.size() && s[i + 1] == '<' && IsOurTagAt(s, i + 1))
					{
						para += '<';			// the one escape, inside a reading too
						i += 2;
						continue;
					}
					if (s[i] != '<')
					{
						// ★THE BASE GOES STRAIGHT INTO THE PARAGRAPH rather than into a buffer of
						//   its own. An <em> can stand inside a reading - a kenten and a ruby on
						//   the same word is ordinary typesetting - and it has to measure its place
						//   against the paragraph, like every other mark.
						para += s[i];
						++i;
						continue;
					}

					std::string inner;
					bool16 innerClosing = kFalse;
					size_t innerAfter = 0;
					if (!TagAt(s, i, inner, innerClosing, innerAfter))
					{
						para += '<';			// "< 2" inside a base is a character, as anywhere
						++i;
						continue;
					}

					if (inner == "ruby" && innerClosing)
					{
						i = innerAfter;
						closed = kTrue;
						break;
					}

					if (inner == "em")
					{
						if (!TakeEm(s, i, innerAfter, innerClosing, para, em, paraKenten, whyNot))
							return kFalse;
						i = innerAfter;
						continue;
					}

					// ★An invisible character can stand inside a base - a reading over a word that
					//   carries an index marker is ordinary - so the base reads them too.
					if (inner == "span")
					{
						size_t next = innerAfter;
						if (!TakeSpan(s, i, innerAfter, innerClosing, para, next, whyNot))
							return kFalse;
						i = next;
						continue;
					}

					if (inner == "br" || inner == "wbr")
					{
						whyNot = "<br> is not read here: a new paragraph is written </p><p>, "
								 "and a forced line break inside one is <span class=\"u000a\"></span>";
						return kFalse;
					}

					if (inner == "rt" && !innerClosing)
					{
						// ★THE BASE BELONGS TO THE READING THAT FOLLOWS IT. It is already in the
						//   paragraph, so its length is how far the paragraph has come since this
						//   fragment began.
						const int32 baseStart = fragStartCp;
						const int32 baseLen = CountCodePoints(para) - fragStartCp;
						i = innerAfter;

						std::string reading;
						bool16 rtClosed = kFalse;
						while (i < s.size())
						{
							if (s[i] == '<' && i + 1 < s.size() && s[i + 1] == '<' && IsOurTagAt(s, i + 1))
							{
								reading += '<';
								i += 2;
								continue;
							}
							if (s[i] != '<')
							{
								reading += s[i];
								++i;
								continue;
							}

							std::string rtName;
							bool16 rtClosing = kFalse;
							size_t rtAfter = 0;
							if (!TagAt(s, i, rtName, rtClosing, rtAfter))
							{
								reading += '<';
								++i;
								continue;
							}
							if (rtName == "rt" && rtClosing)
							{
								i = rtAfter;
								rtClosed = kTrue;
								break;
							}

							whyNot = "the <" + rtName + "> element cannot stand inside an <rt>";
							return kFalse;
						}

						if (!rtClosed)
						{
							whyNot = "an <rt> was never closed";
							return kFalse;
						}

						// ⚠AN EMPTY READING IS NO READING, and one over no characters cannot be
						//   placed - KCMParaText.h applies both rules to fValue and fLen. The base
						//   text stays in the paragraph either way: dropping the reading must never
						//   drop the words.
						if (baseLen > 0 && !reading.empty())
							made.push_back(KCMAttrSpan(baseStart, baseLen, reading, kFalse));

						fragStartCp = CountCodePoints(para);	// the next fragment starts here
						continue;
					}

					whyNot = "the <" + inner + "> element cannot stand inside a <ruby>";
					return kFalse;
				}

				if (!closed)
				{
					whyNot = "a <ruby> was never closed";
					return kFalse;
				}

				// ★ONE READING IS A GROUP, SEVERAL ARE MONO - the split IS the setting, and
				//   WriteParaHtml writes it the same way round.
				if (made.size() == 1)
					made[0].fGroup = kTrue;
				for (size_t k = 0; k < made.size(); ++k)
					paraRuby.push_back(made[k]);
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
	if (inNote)
	{
		whyNot = "the last note was never closed";
		return kFalse;
	}

	// ---- the notes, in the order their ids run ------------------------------------------------
	std::vector<int32> order = noteIds;
	std::sort(order.begin(), order.end());
	for (size_t k = 0; k < order.size(); ++k)
	{
		for (size_t m = 0; m < noteIds.size(); ++m)
		{
			if (noteIds[m] == order[k])
			{
				out.fNotes.push_back(noteBodies[m]);
				break;
			}
		}
	}

	// ⚠★★**A REFERENCE WITH NOTHING BEHIND IT IS REFUSED.** It means the note was deleted while
	//   its marker was left standing, and reading it as "a note with no words" would put an empty
	//   note into the document - a change nobody asked for, made silently.
	for (size_t k = 0; k < refOrdinals.size(); ++k)
	{
		if (std::find(noteIds.begin(), noteIds.end(), refOrdinals[k]) == noteIds.end())
		{
			whyNot = "a <sup> points at a note that is not in the document";
			return kFalse;
		}
	}

	return kTrue;
}

}	// namespace KCMStoryHtml

// End, KCMStoryHtml.cpp.
