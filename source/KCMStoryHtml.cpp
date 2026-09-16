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
	knows HTML adds tbody and colgroup by itself), the two it IGNORES (br and wbr - a writer
	who knows HTML reaches for <br>, and InDesign has two kinds of break where HTML has one
	word, so guessing is worse than dropping it) and the four it REFUSES (sup, a, ol and li -
	what a footnote used to be written with; the message says what it is written with now).

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
	bool16	fInCell;	// a <td> of THIS table is open - the cell a nested table would stand in

	TableFrame() : fSlot(0), fInHead(kFalse), fInCell(kFalse) {}
};

/** Whether the innermost table has a cell open.

	★★**ONE BOOLEAN FOR THE WHOLE READ COULD NOT SAY THIS.** The </td> of a NESTED table would
	  clear it, and the outer cell - still open, still collecting paragraphs - would look closed
	  from then on. The answer belongs to each table, so it is kept with each table. */
bool16 InsideCell(const std::vector<TableFrame>& tables)
{
	return (!tables.empty() && tables.back().fInCell) ? kTrue : kFalse;
}

/** A paragraph that is waiting: everything read so far, put aside while a table standing inside it
	is read. See paraStack in Read. */
struct ParaState
{
	std::string		fText;
	KCMAttrSpanList	fRuby;
	KCMAttrSpanList	fKenten;
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
	The code points [from, to) of the paragraph, cut where the kenten changes and wrapped in <em>,
	with every note reference standing in that stretch written where it stands.

	★**EVERY PIECE OF TEXT THIS FILE WRITES GOES THROUGH HERE**, the base of a reading included -
	which is what lets a kenten and a ruby stand on the same word: the <em> simply ends up inside
	the <ruby>, where HTML is happy to have it. Writing the two separately would have meant deciding
	which of them owns a character they share, and there is no answer to that.

	⚠A MARK WHOSE NAME CANNOT BE A CLASS is written as plain text - the words are never lost, only
	 the mark. It cannot arise from KCMTextRead (its names are ASCII), and a silent loss would be
	 worse than the alternative only if there were an alternative: Write's contract is that it
	 never fails.
*/
void WriteTable(const Story& s, const Table& t, int32 depth, std::string& out);

/** The tables of `inside` that stand exactly at `cp`, written there. */
void WriteTablesAt(const Story& s, const std::vector<size_t>& inside, int32 cp, int32 depth,
				   std::string& out)
{
	for (size_t k = 0; k < inside.size(); ++k)
	{
		if (s.fTables[inside[k]].fOffset == cp)
			WriteTable(s, s.fTables[inside[k]], depth, out);
	}
}

/** The nearest table position strictly after cp, or `limit` when there is none. */
int32 NextTableAfter(const Story& s, const std::vector<size_t>& inside, int32 cp, int32 limit)
{
	int32 stop = limit;
	for (size_t k = 0; k < inside.size(); ++k)
	{
		const int32 at = s.fTables[inside[k]].fOffset;
		if (at > cp && at < stop)
			stop = at;
	}
	return stop;
}

void WriteRun(const Story& s, const Para& p, const std::vector<int32>& byteAt,
			  int32 from, int32 to, const std::vector<size_t>& inside, int32 depth,
			  bool16 writeTables, std::string& out)
{
	const std::string& text = p.fText;
	const KCMAttrSpanList& kenten = p.fKenten;

	int32 cp = from;
	while (cp < to)
	{
		// ★★**EVERY TABLE OF THIS PARAGRAPH IS WRITTEN WHERE IT STANDS**, wherever the run is -
		//   inside a reading's base included, for the same reason the note references used to be
		//   emitted here: this is the one place every piece of text goes through.
		if (writeTables)
			WriteTablesAt(s, inside, cp, depth, out);

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

		// ...and a table standing in the middle of this stretch cuts it, so that the next turn of
		// the loop stands exactly where the table does.
		if (writeTables)
			stop = NextTableAfter(s, inside, cp, stop);


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
void WriteParaHtml(const Story& s, const Para& p, const std::vector<size_t>& inside,
				   int32 depth, std::string& out)
{
	std::vector<int32> byteAt;
	KCMTextDiff::ToCodePoints(p.fText, nil, &byteAt);
	const int32 cpCount = static_cast<int32>(byteAt.size());

	std::vector<KCMAttrSpan> spans = p.fRuby;
	std::sort(spans.begin(), spans.end(), SpanStartsEarlier);

	int32 cp = 0;
	size_t next = 0;
	while (cp < cpCount || next < spans.size())
	{

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

			// ★★**A TABLE ANCHORED INSIDE A READING'S BASE IS WRITTEN IN FRONT OF THE <ruby>.** It
			//   cannot stand inside one - the markup would be nonsense and a browser would not draw
			//   it - so it is moved to the head of the reading, which is as close to its real place
			//   as this shape allows. ⚠**THE MODEL IS NOT CHANGED**: fOffset still says where the
			//   document has it, and this rounding is the markup's alone.
			//   ⚠MEASURED 2026-09-16: without this, a table anchored where a reading began came out
			//    between <ruby> and <rt>, which is neither valid nor readable.
			{
				const int32 rubyEnd = spans[last].fStart + spans[last].fLen;
				for (size_t u = 0; u < inside.size(); ++u)
				{
					const int32 at = s.fTables[inside[u]].fOffset;
					if (at >= cp && at < rubyEnd)
						WriteTable(s, s.fTables[inside[u]], depth, out);
				}
			}

			out += "<ruby>";
			for (size_t k = next; k <= last; ++k)
			{
				// ★THE BASE GOES THROUGH WriteRun LIKE EVERYTHING ELSE, so a kenten standing on
				//   these same characters comes out as an <em> inside the reading - and so does a
				//   note reference, which is how one standing inside a reading keeps its place.
					WriteRun(s, p, byteAt, spans[k].fStart, spans[k].fStart + spans[k].fLen,
							 inside, depth, kFalse, out);
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
		const int32 stop = (next < spans.size() && spans[next].fStart > cp) ? spans[next].fStart
																		   : cpCount;
		if (stop <= cp)
			break;					// nothing left to write; a malformed span cannot loop us

		WriteRun(s, p, byteAt, cp, stop, inside, depth, kTrue, out);
		cp = stop;
	}

	// ⚠A TABLE AT THE VERY END of the paragraph stands where no run covers - every run is
	//   half-open and stops before it. A paragraph holding NOTHING but a table is this case with
	//   cpCount == 0, which is why it needs no special path of its own.
	WriteTablesAt(s, inside, cpCount, depth, out);
}

/*	WriteTable
	One table, and the tables standing inside its cells.

	★★★**A NESTED TABLE IS WRITTEN WHERE IT STANDS** (the user's request, 2026-09-16): inside the
	<td> of the cell that holds it, after that cell's own paragraphs - because a cell is a small
	body, and a table stands among a body's paragraphs in exactly the same way. It used to come out
	as a second table AFTER the outer one, which is where the flat list had put it and where nobody
	reading the file would look for it.

	⚠**THE DATA IS STILL FLAT, ONLY THE MARKUP IS NESTED.** Story::fTables is one list in document
	 order and each entry says which cell it belongs to (Table::fInTable), so the ordinals stay a
	 single count - which is what the comparison pairs the two sides on, and what lets a nested
	 table be just another table to everything downstream.
*/
/** One step of indent: four spaces, as the user asked for (2026-09-16). */
void Indent(int32 depth, std::string& out)
{
	for (int32 k = 0; k < depth; ++k)
		out += "    ";
}

void WriteTable(const Story& s, const Table& t, int32 depth, std::string& out)
{
	// ★★**A TABLE IS PRETTY-PRINTED** (the user's request, 2026-09-16): one tag per line, four
	//   spaces a step, so that a reader editing the file can see the shape of the table at a
	//   glance. ⚠**THIS COSTS NOTHING IN CORRECTNESS** because every newline and space here is
	//   OUTSIDE a <p>, and outside a <p> nothing is text - which is the one rule this format has.
	// ★★★**EVERY TABLE STANDS INSIDE A <p>** (the user's decision, 2026-09-16), at the place in
	//   that paragraph's text where the document has it - so the opening tag follows whatever came
	//   before it on the same line and is NOT indented. A paragraph holding nothing but a table is
	//   <p><table>...</table></p>, which is the same rule with no text either side rather than a
	//   case of its own. ⚠It is not valid HTML - a <p> may not contain a table - but a browser
	//   shows it correctly (measured in Chrome), and the reader that has to understand it is ours.
	out += "<table>\r\n";

	bool16 inHead = kFalse;
	for (size_t r = 0; r < t.fRows.size(); ++r)
	{
		const Row& row = t.fRows[r];
		if (row.fHeader && !inHead)
		{
			Indent(depth + 1, out);
			out += "<thead>\r\n";
			inHead = kTrue;
		}
		else if (!row.fHeader && inHead)
		{
			Indent(depth + 1, out);
			out += "</thead>\r\n";
			inHead = kFalse;
		}

		// A row inside <thead> sits one step deeper than one outside it.
		const int32 rowDepth = depth + (inHead ? 2 : 1);

		Indent(rowDepth, out);
		out += "<tr>\r\n";
		for (size_t c = 0; c < row.fCells.size(); ++c)
		{
			const Cell& cell = row.fCells[c];
			Indent(rowDepth + 1, out);
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
			out += ">\r\n";

			// ★**A CELL IS A SMALL BODY**: its paragraphs, and after each one the tables that stand
			//   there. A cell always holds at least one paragraph - InDesign's cells do, and Read
			//   puts one back when a file leaves it out - so a cell holding nothing but a table
			//   still writes <p></p> first. That is the same rule as everywhere else and not a
			//   special case: every character lives in a paragraph, and everything outside one is
			//   layout.
			for (size_t k = 0; k < cell.fParas.size(); ++k)
			{
				// ★**A CELL IS A SMALL BODY**: every paragraph is a <p>, and the tables standing in
				//   it are written inside that <p>, at the place in its text where they stand.
				std::vector<size_t> inside;
				for (size_t u = 0; u < s.fTables.size(); ++u)
				{
					if (s.fTables[u].fInTable == t.fOrdinal
						&& s.fTables[u].fInRow == static_cast<int32>(r)
						&& s.fTables[u].fInCell == static_cast<int32>(c)
						&& s.fTables[u].fParaIndex == static_cast<int32>(k))
					{
						inside.push_back(u);
					}
				}

				Indent(rowDepth + 2, out);
				out += "<p>";
				WriteParaHtml(s, cell.fParas[k], inside, rowDepth + 2, out);
				out += "</p>\r\n";
			}

			Indent(rowDepth + 1, out);
			out += "</td>\r\n";
		}
		Indent(rowDepth, out);
		out += "</tr>\r\n";
	}
	if (inHead)
	{
		Indent(depth + 1, out);
		out += "</thead>\r\n";
	}

	// ⚠**NO NEWLINE AFTER IT.** Whatever follows the table is the rest of the same paragraph -
	//  more text, or the </p> - and a newline here would be a character inside a <p>.
	Indent(depth, out);
	out += "</table>";
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

/*	WriteKentenRule
	One rule: the class this format spells a mark with, and the CSS that draws it.
*/
void WriteKentenRule(const std::string& cls, const std::string& style, std::string& out)
{
	out += ".kenten-";
	out += cls;
	out += "{-webkit-text-emphasis-style:";
	out += style;
	out += ";text-emphasis-style:";
	out += style;
	out += "}\r\n";
}

/*	CustomKentenStyle
	The quoted mark for a "Custom:X" value, and kFalse for anything that is not one.

	★The mark comes from the document, never from a literal here - which is what keeps this file's
	 own bytes ASCII while the sheet it writes carries whatever character the reader chose.
*/
bool16 CustomKentenStyle(const std::string& value, std::string& outStyle)
{
	const size_t kCustomLen = 7;		// "Custom:"
	if (value.size() <= kCustomLen || value.compare(0, kCustomLen, "Custom:") != 0)
		return kFalse;

	outStyle = "\"";
	const std::string mark = value.substr(kCustomLen);
	for (size_t c = 0; c < mark.size(); ++c)
	{
		if (mark[c] == '"' || mark[c] == '\\')
			outStyle += '\\';
		outStyle += mark[c];
	}
	outStyle += "\"";
	return kTrue;
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

void WriteStylesheet(const std::vector<std::string>& kentenValues, std::string& outCss)
{
	outCss.clear();

	// ★**THE SIZE IS THE READING SIZE** (the user's request, 2026-09-15): 1.5 times what the
	//   browser would have chosen. These files are read on a screen rather than set on a page, so
	//   the document's own point sizes have nothing to say here. The measure stays in em, which is
	//   why the line still holds its 40 characters - the characters grow, the column does not.
	outCss += "body{font-size:1.5em;line-height:2.2;margin:2em;max-width:40em}\r\n";
	outCss += "p{margin:0 0 .7em}\r\n";		// so a paragraph and a forced line break look different

	// ★**AND THE VERTICAL SETTING** (2026-09-16): writing-mode turns the page, and the ruby and the
	//   kenten turn with it - the browser puts them where the page puts them. The measure turns too,
	//   so the 40em that was a line's LENGTH becomes its height, and the page is given a height to
	//   fill or the columns would have nowhere to run.
	outCss += "body.vertical{-webkit-writing-mode:vertical-rl;writing-mode:vertical-rl;"
			  "max-width:none;max-height:40em;height:88vh}\r\n";
	outCss += "table{border-collapse:collapse;margin:1em 0}\r\n";
	outCss += "td{border:1px solid #999;padding:.3em .8em;vertical-align:top}\r\n";
	outCss += "ol{border-top:1px solid #ccc;margin-top:2em;padding-top:1em}\r\n";

	// ★★THE STYLESHEET IS WHERE THIS FORMAT EXPLAINS ITSELF. A kenten IS stress emphasis, CSS has
	//   text-emphasis-style for exactly that, so <em> is the mark - and this rule says so to anyone
	//   who opens the file, including a reader who writes one of their own by hand.
	outCss += "em{font-style:normal;-webkit-text-emphasis-style:filled sesame;"
			  "text-emphasis-style:filled sesame}\r\n";

	// ★**EVERY BUILT-IN KIND, WHETHER THIS FOLDER USES IT OR NOT.** The sheet is the folder's, and
	//   a reader who types <em class="kenten-BlackTriangle"> into one of these files by hand has to
	//   see a triangle when the page reloads.
	for (size_t k = 0; k < kKentenLookCount; ++k)
		WriteKentenRule(kKentenLooks[k].fName, kKentenLooks[k].fStyle, outCss);

	// ⚠**THE CUSTOM MARKS GET AN ORDER OF THEIR OWN.** They arrive in the order the stories were
	//  read, and that is a property of the export rather than of the document: sorted here so that
	//  two exports of one document produce the same bytes and a folder does not diff against
	//  itself. Sorting the UTF-8 puts them in code point order, which is an order and is stable -
	//  nobody reads this file top to bottom looking for a mark.
	std::vector<std::string> customs;
	for (size_t i = 0; i < kentenValues.size(); ++i)
	{
		std::string style;
		if (CustomKentenStyle(kentenValues[i], style))
			customs.push_back(kentenValues[i]);
	}
	std::sort(customs.begin(), customs.end());
	customs.erase(std::unique(customs.begin(), customs.end()), customs.end());

	for (size_t i = 0; i < customs.size(); ++i)
	{
		std::string cls, style;
		if (KentenClassOf(customs[i], cls) && CustomKentenStyle(customs[i], style))
			WriteKentenRule(cls, style, outCss);
	}

	// ★★AND THE INVISIBLE CHARACTERS GET A FACE. Each one is an empty <span> whose class is its
	//   code point, so the browser shows nothing at all unless the stylesheet draws something -
	//   and a reader who cannot see a thing will delete it. The marks are SYMBOLS rather than
	//   words on purpose: the stylesheet then needs no language, and these strings stay ASCII in
	//   the source (CSS's own \XXXX escape carries the character, so no literal here is non-ASCII
	//   - which is the rule this file keeps for everything it writes).
	outCss += "span[class^=\"u\"]{color:#999;font-size:.85em}\r\n";
	outCss += "span[class^=\"u\"]::before{content:\"\\25CC\"}\r\n";	// dotted circle: something is here
	outCss += ".u000a::before{content:\"\\23CE\"}\r\n";				// return symbol
	outCss += ".u000a::after{content:\"\\A\";white-space:pre}\r\n";	// and it really breaks the line
	outCss += ".ufffc::before{content:\"\\25A3\"}\r\n";				// framed square: anchored object
	outCss += ".u0018::before{content:\"#\"}\r\n";					// auto page number / variable
	outCss += ".u0019::before{content:\"\\00A7\"}\r\n";				// section marker
	outCss += ".u0008::before{content:\"\\21E5\"}\r\n";				// right indent tab
	outCss += ".u0007::before{content:\"\\21B1\"}\r\n";				// indent to here
	outCss += ".u00ad::before{content:\"\\2010\"}\r\n";				// discretionary hyphen
	outCss += ".ue02c::before{content:\"\\2318\"}\r\n";				// index marker
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
	// ★★**THE LOOK TRAVELS WITH THE FILE** (2026-09-16, the user's decision - going back on the
	//   folder-wide stylesheet of the day before). One file carried out of its folder - mailed,
	//   pasted into a chat, opened on its own - has to LOOK right, because looking at it in a
	//   browser is how the reader checks what they have edited. The reason for pulling the look
	//   out was that somebody wanting bigger text would otherwise edit thirty files; the reader
	//   edits inside <p> and nowhere else, so that reason went away.
	out += "<style>\r\n";
	{
		std::vector<std::string> kenten;
		CollectKentenValues(s, kenten);
		std::string css;
		WriteStylesheet(kenten, css);
		out += css;
	}
	out += "</style></head>\r\n";

	// ★**A VERTICAL STORY IS SHOWN VERTICALLY** (2026-09-16). CSS has writing-mode for exactly
	//   this, and a browser then puts the ruby where the page puts it and the kenten beside the
	//   character rather than above it - so what the reader checks looks like what they are
	//   editing. The class is on <body>, which is outside every <p> and therefore none of the
	//   reader's business; Read takes it back off so the trip loses nothing.
	out += s.fVertical ? "<body class=\"vertical\">\r\n" : "<body>\r\n";

	for (size_t i = 0; i < s.fBody.size(); ++i)
	{
		// ★★★**EVERY PARAGRAPH IS A <p>, AND ITS TABLES ARE INSIDE IT** (the user's decision,
		//   2026-09-16). One rule, no cases: a paragraph holding nothing but a table is
		//   <p><table>...</table></p>, and one with a table in the middle of a sentence is
		//   <p>琥珀猫<table>...</table>ねこねこ</p>. The reader adds a paragraph by copying a <p>,
		//   and never meets a table standing on its own where they might type in front of it.
		// ⚠**THE BODY'S OWN TABLES ONLY.** A table standing in a cell is written by WriteTable,
		//  inside the <td> that holds it - it is in this same flat list, and fInTable tells them
		//  apart.
		std::vector<size_t> inside;
		for (size_t t = 0; t < s.fTables.size(); ++t)
		{
			if (s.fTables[t].fInTable < 0 && s.fTables[t].fParaIndex == static_cast<int32>(i))
				inside.push_back(t);
		}

		out += "<p>";
		WriteParaHtml(s, s.fBody[i], inside, 0, out);
		out += "</p>\r\n";
	}

	// ★★★**A NOTE IS A LIST ITEM HOLDING PARAGRAPHS** (the user's decision, 2026-09-16, replacing
	//   the <p class="note1"> of the same afternoon). The paragraphs inside carry NOTHING of their
	//   own, and that is the point: adding one is copying a <p>, and a copied <p> cannot end up
	//   belonging to the wrong note. A class on every paragraph could, and would do it silently.
	// ★**THE ORDER IS THE PAIRING** - the first <li> is note 1 - so there are no ids to keep in
	//   step with anything. The <sup> that used to point at them is not written at all any more.
	// ⚠**AN EMPTY NOTE IS STILL AN <li>**, or every note after it would shift up by one.
	if (!s.fNotes.empty())
	{
		out += "<ol>\r\n";
		for (size_t n = 0; n < s.fNotes.size(); ++n)
		{
			Indent(1, out);
			out += "<li>\r\n";

			// A note's paragraphs hold no tables - InDesign does not let one be put in a footnote -
			// so the list of tables handed to the writer is empty.
			const std::vector<size_t> noTables;
			for (size_t k = 0; k < s.fNotes[n].size(); ++k)
			{
				Indent(2, out);
				out += "<p>";
				WriteParaHtml(s, s.fNotes[n][k], noTables, 2, out);
				out += "</p>\r\n";
			}
			Indent(1, out);
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

	// ★★★**A PARAGRAPH IS PUT ASIDE WHILE A TABLE INSIDE IT IS READ** (2026-09-16). A table may
	//   stand in the middle of a paragraph, and its cells hold paragraphs of their own, so the one
	//   being read has to wait somewhere while they are. One buffer could not do it: the cells
	//   would write into the paragraph the table interrupted.
	std::vector<ParaState> paraStack;
	// ★**A NOTE IS AN <li>, AND ITS PARAGRAPHS ARE WHATEVER STANDS INSIDE IT** (2026-09-16), so
	//   nothing has to be paired up at the end and the paragraphs carry nothing of their own.
	bool16 inNote = kFalse;


	std::vector<TableFrame> tables;	// the tables open right now; more than one means nesting

	size_t i = 0;
	while (i < s.size())
	{
		const char c = s[i];

		if (c != '<')
		{
			if (inBody && inPara)
			{
				para += c;
			}
			else if (inBody && c != ' ' && c != '\t' && c != '\r' && c != '\n')
			{
				// ⚠★★★**EVERY CHARACTER BELONGS TO A PARAGRAPH** (the user's rule, 2026-09-16).
				//   Whitespace outside <p> is layout - that is what lets a table be indented and
				//   a long paragraph be wrapped in an editor - but a WORD outside <p> is text
				//   that this reader would otherwise drop without saying anything.
				// ★**MEASURED, NOT IMAGINED**: a file exported before this rule wrote its cells
				//   as <td>品名</td>, and reading it back gave every cell one EMPTY paragraph,
				//   with no error and every test still passing (2026-09-16). Refusing here is
				//   what turns that silent loss into a sentence somebody can act on.
				whyNot = "there is text outside a <p>: every character has to be inside a "
						 "paragraph, written <p>...</p>";
				return kFalse;
			}
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
					else
					{
						inPara = kTrue;
						para.clear();
						paraRuby.clear();
						paraKenten.clear();
						em = EmState();
					}

					// ★★"THIS PARAGRAPH IS THE REST OF THE ONE BEFORE THE TABLE." The table has
					//   already closed by the time this is read, so the mark is applied backwards -
					//   and the offset is simply how long the first half turned out to be.
					std::string cls;
					const bool16 hasClass = ClassOfTag(s, i, after, cls) ? kTrue : kFalse;

					// ★**A NOTE'S PARAGRAPH CARRIES NOTHING OF ITS OWN**: which note it belongs to
					//   is the <li> it stands in, so the only class this format still writes on a
					//   paragraph is the "rest of the one before the table" below.
					if (hasClass && !InsideCell(tables) && !inNote && cls == "c")
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
					// ★A PARAGRAPH BELONGS TO WHATEVER IT STANDS IN: a cell, a note, or the body.
					if (InsideCell(tables)
						&& !tables.back().fTable.fRows.empty()
						&& !tables.back().fTable.fRows.back().fCells.empty())
					{
						tables.back().fTable.fRows.back().fCells.back().fParas.push_back(p);
					}
					else if (inNote)
					{
						// The <li> made the note when it opened, so there is always one to add to.
						if (!out.fNotes.empty())
							out.fNotes.back().push_back(p);
					}
					else
					{
						out.fBody.push_back(p);
					}
					inPara = kFalse;
					para.clear();
					paraRuby.clear();
					paraKenten.clear();

					// ★**NOTHING REOPENS HERE.** Text between </p> and the next <p> is layout,
					//   inside a cell exactly as in the body.
				}
				i = after;
				continue;
			}

			if (name == "table")
			{
				if (!closing)
				{
					if (em.fOpen)
					{
						whyNot = "a <table> stands inside an <em> that is still open";
						return kFalse;
					}

					// ★★★**WHERE THE TAG STANDS IS WHAT IT MEANS** (the user's decision,
					//   2026-09-16). INSIDE a <p> the table is part of that paragraph and its place
					//   among the characters is kept; OUTSIDE one it IS the next paragraph, because
					//   in the document a table of its own makes a paragraph holding
					//   [anchor][continued...][CR] and nothing else.
					//   ⚠**A <p> HOLDING A TABLE IS NOT VALID HTML** - a browser closes the <p> at
					//    the table - but it SHOWS it correctly (measured in Chrome, 2026-09-16) and
					//    this reader is the one that has to understand it. Nothing else can carry
					//    where in a sentence the table stood.
					if (!inPara)
					{
						whyNot = "a <table> stands outside a paragraph: every table is written "
								 "inside the <p> of the paragraph it belongs to, and a paragraph "
								 "that holds nothing else is <p><table>...</table></p>";
						return kFalse;
					}

					// ★WHERE IN THE PARAGRAPH, in the text's own count - and the paragraph goes
					//   aside while the table's own cells are read into a fresh one.
					TableFrame frame;
					frame.fSlot = out.fTables.size();
					frame.fTable.fOrdinal = static_cast<int32>(frame.fSlot);
					frame.fTable.fSplitsPara = kTrue;
					frame.fTable.fOffset = CountCodePoints(para);

					{
						ParaState saved;
						saved.fText = para;
						saved.fRuby = paraRuby;
						saved.fKenten = paraKenten;
						paraStack.push_back(saved);
					}

					para.clear();
					paraRuby.clear();
					paraKenten.clear();
					em = EmState();
					inPara = kFalse;

					// ★WHERE IT STANDS, in whatever holds it. A table inside a <td> belongs to that
					//   cell and counts that cell's paragraphs; one in the body counts the body's.
					//   ⚠A table INSIDE a paragraph names the paragraph that has not been closed
					//    yet, which is the one about to be pushed - hence no -1.
					if (InsideCell(tables))
					{
						const TableFrame& parent = tables.back();
						const Row& row = parent.fTable.fRows.back();
						frame.fTable.fInTable = parent.fTable.fOrdinal;
						frame.fTable.fInRow = static_cast<int32>(parent.fTable.fRows.size()) - 1;
						frame.fTable.fInCell = static_cast<int32>(row.fCells.size()) - 1;
						// ⚠**THE PARAGRAPH IT NAMES HAS NOT BEEN CLOSED YET** - it is the one about to be
						//  pushed when its </p> arrives, so the count is taken as it stands, with no -1.
						frame.fTable.fParaIndex =
							static_cast<int32>(row.fCells.back().fParas.size());
					}
					else
					{
						frame.fTable.fInTable = -1;
						frame.fTable.fParaIndex = static_cast<int32>(out.fBody.size());
					}

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
					if (inPara)
					{
						whyNot = "a </table> closes a table with a paragraph still open inside it";
						return kFalse;
					}

					const bool16 wasInsidePara = tables.back().fTable.fSplitsPara;
					out.fTables[tables.back().fSlot] = tables.back().fTable;
					tables.pop_back();

					// ★**THE PARAGRAPH THE TABLE INTERRUPTED GOES ON**, exactly where it left off:
					//   everything after the table joins the same paragraph, which is what makes
					//   "琥珀猫[table]ねこねこ" one paragraph on both sides of the trip.
					if (wasInsidePara && !paraStack.empty())
					{
						para = paraStack.back().fText;
						paraRuby = paraStack.back().fRuby;
						paraKenten = paraStack.back().fKenten;
						paraStack.pop_back();
						em = EmState();
						inPara = kTrue;
					}
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

					// ★★**A CELL OPENS NO PARAGRAPH.** Its paragraphs are its <p>s, the same as
					//   anywhere else - which is what lets a table be pretty-printed: every
					//   newline and space between <td> and <p> is outside a paragraph, and
					//   outside a paragraph nothing is text.
					tables.back().fInCell = kTrue;
					para.clear();
					paraRuby.clear();
					paraKenten.clear();
					em = EmState();
				}
				else
				{
					if (inPara)
					{
						whyNot = "a </td> closes a cell with a paragraph still open";
						return kFalse;
					}

					// ⚠**AN EMPTY CELL STILL HAS ONE PARAGRAPH**, because a cell in InDesign always
					//   does. <td></td> is how this format writes one, so the paragraph is put back
					//   here rather than asked of the file.
					if (!tables.empty()
						&& !tables.back().fTable.fRows.empty()
						&& !tables.back().fTable.fRows.back().fCells.empty()
						&& tables.back().fTable.fRows.back().fCells.back().fParas.empty())
					{
						tables.back().fTable.fRows.back().fCells.back().fParas.push_back(Para());
					}
					tables.back().fInCell = kFalse;
				}
				i = after;
				continue;
			}

			// ⚠**A NOTE'S REFERENCE IS NOT WRITTEN ANY MORE** (2026-09-16), so reading one would be
			//   a guess at what somebody meant by it. Both are named in kOurTags all the same: that
			//   is what lets this message be printed instead of the tag quietly becoming text.
			if (name == "sup" || name == "a")
			{
				whyNot = "the <" + name + "> element is not part of this format: a note's words "
						 "are an <li> of the <ol> after the body, and nothing in the body points "
						 "at them";
				return kFalse;
			}

			if (name == "ol")
			{
				i = after;			// the list carries nothing of its own; its items do
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
					if (inPara)
					{
						whyNot = "an <li> begins inside a paragraph";
						return kFalse;
					}

					// ★**THE ORDER IS THE PAIRING**: this is the next note, whatever it turns out
					//   to hold. An <li> with nothing in it is an empty note and still counts, or
					//   every note after it would shift up by one.
					inNote = kTrue;
					out.fNotes.push_back(std::vector<Para>());
				}
				else
				{
					if (!inNote)
					{
						whyNot = "an </li> closes a note that never began";
						return kFalse;
					}
					inNote = kFalse;
				}
				i = after;
				continue;
			}

			// ★**IGNORED, NOT REFUSED** (the user's rule, 2026-09-16). Somebody who knows HTML
			//   reaches for <br> meaning "a new line here", and InDesign has TWO of those - a new
			//   paragraph, which is </p><p>, and a forced line break inside one, which is
			//   <span class="u000a"></span>. Guessing between them would silently produce the
			//   wrong one; stopping the whole import over a stray tag is worse than letting it
			//   pass. So it is dropped, exactly like the whitespace and the newlines around it.
			if (name == "br" || name == "wbr")
			{
				i = after;
				continue;
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
						i = innerAfter;		// ignored here too - see the note in the body
						continue;
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

							// ★★AN INVISIBLE CHARACTER CAN STAND IN A READING, and the writer puts
							//   it there as a <span> like anywhere else. ⚠MEASURED 2026-09-15:
							//   refusing it here meant the writer could produce a file its own
							//   reader would not take - a reading holding a zero width space was
							//   enough.
							if (rtName == "span")
							{
								size_t next = rtAfter;
								if (!TakeSpan(s, i, rtAfter, rtClosing, reading, next, whyNot))
									return kFalse;
								i = next;
								continue;
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
			{
				inBody = closing ? kFalse : kTrue;

				// ★THE ONE THING <body> CARRIES: which way the story is set. Written by the
				//   exporter from the document, read back here so the trip loses nothing - and if
				//   a reader turns it on or off by hand, that is an answer too.
				if (!closing)
				{
					std::string cls;
					out.fVertical = (ClassOfTag(s, i, after, cls) && cls == "vertical")
									? kTrue : kFalse;
				}
			}
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
	return kTrue;
}

}	// namespace KCMStoryHtml

// End, KCMStoryHtml.cpp.
