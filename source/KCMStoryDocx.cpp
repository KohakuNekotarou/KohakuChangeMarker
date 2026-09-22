//========================================================================================
//
//  KCMStoryDocx.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storyhtml-test).
#include "VCPlugInHeaders.h"

#include "KCMStoryDocx.h"
#include "KCMTextDiff.h"		// ToCodePoints - the one walk over UTF-8, shared with the HTML writer
#include "KCMXmlTree.h"		// the reading half walks Word's parts through this
#include "KCMParaText.h"		// AppendUtf8 / SetSpanValuesToText - the reading half builds spans the way the HTML reader does

#include <cstddef>
#include <cstdio>
#include <utility>

namespace KCMStoryDocx
{

namespace
{

/*	What a kenten style is called, before its class: "圏点-" (2026-09-22, the user's call -
	"圏点は、日本人しかつかわないので"). The name is what a person reads and picks in Word's style
	gallery, so it is written in the language of the people who use the feature; KCMStoryShape's
	KentenClassOf gives the half that follows it, in the same language.

	⚠★★★**A .docx WRITTEN BEFORE TODAY SAYS "kenten-" AND IS NO LONGER READ** (the user chose the
	  clean switch over carrying both spellings). Its kenten arrive as a mark this reader cannot
	  name, so the story is refused by name rather than coming in wrong - and the words themselves
	  are unaffected.
	⚠**A UTF-8 ESCAPE, NOT A LITERAL**: no /utf-8 in this project, so a literal would be compiled in
	  the machine's ANSI codepage and land in a UTF-8 XML as mojibake (KCMStoryShape says it in
	  full, beside the class names).
*/
const char kKentenStylePrefix[] = "\xE5\x9C\x8F\xE7\x82\xB9\x2D";		// 圏点-
const size_t kKentenStylePrefixLen = sizeof(kKentenStylePrefix) - 1;

/*	Look
	What one character carries BESIDES a ruby. Two neighbours with the same Look share a run.

	★**THE SPANS ARE KEPT BY INDEX, NOT AS FLAGS**, for the warichu's sake: Word ties the runs of
	  one "two lines in one" region together by <w:eastAsianLayout w:id>, so every run of one
	  warichu has to carry ONE id - including the runs a tate-chu-yoko inside it cuts off. The
	  id is therefore the warichu's own number whenever there is one, and the tate-chu-yoko's
	  otherwise; paragraphs are separate regions anyway, so the numbers start again in each.
*/
struct Look
{
	const std::string*	fKentenClass;	// nil, or the class KentenClassOf gave ("BlackCircle")
	int32				fTcy;			// which tate-chu-yoko span covers it, -1 for none
	int32				fWarichu;		// which warichu span covers it, -1 for none

	Look() : fKentenClass(nil), fTcy(-1), fWarichu(-1) {}

	bool16 SameAs(const Look& o) const
	{
		if ((fKentenClass == nil) != (o.fKentenClass == nil))
			return kFalse;
		if (fKentenClass != nil && *fKentenClass != *o.fKentenClass)
			return kFalse;
		return (fTcy == o.fTcy && fWarichu == o.fWarichu) ? kTrue : kFalse;
	}
};

void AppendEscaped(const std::string& utf8, size_t from, size_t to, std::string& out)
{
	for (size_t i = from; i < to && i < utf8.size(); ++i)
	{
		const char c = utf8[i];
		if (c == '&')		out += "&amp;";
		else if (c == '<')	out += "&lt;";
		else if (c == '>')	out += "&gt;";
		else if (c == '"')	out += "&quot;";	// harmless in text, needed in an attribute
		else				out += c;
	}
}

void AppendHex(int32 cp, bool16 upper, std::string& out)
{
	char buf[16] = { 0 };
	std::snprintf(buf, sizeof(buf), upper ? "%04X" : "%04x", static_cast<unsigned int>(cp));
	out += buf;
}

void AppendNumber(int32 n, std::string& out)
{
	char buf[16] = { 0 };
	std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(n));
	out += buf;
}

/** <w:rPr> for a Look, or nothing at all. The order inside is the schema's: the style first. */
void AppendRunProps(const Look& look, std::string& out)
{
	if (look.fKentenClass == nil && look.fTcy < 0 && look.fWarichu < 0)
		return;

	out += "<w:rPr>";
	if (look.fKentenClass != nil)
	{
		out += "<w:rStyle w:val=\"";
		out += kKentenStylePrefix;
		AppendEscaped(*look.fKentenClass, 0, look.fKentenClass->size(), out);
		out += "\"/>";
	}
	if (look.fTcy >= 0 || look.fWarichu >= 0)
	{
		out += "<w:eastAsianLayout w:id=\"";
		AppendNumber((look.fWarichu >= 0) ? (look.fWarichu + 1) * 100 : look.fTcy + 1, out);
		out += "\"";
		if (look.fWarichu >= 0)
			out += " w:combine=\"1\"";
		if (look.fTcy >= 0)
			out += " w:vert=\"1\"";
		out += "/>";
	}
	out += "</w:rPr>";
}

/** One invisible character as a locked content control. ★THE TAG IS THE TRUTH - see the header.
	★THE RUN INSIDE CARRIES THE CHARACTER'S OWN LOOK (2026-09-19, stage 2): a kenten or a
	  tate-chu-yoko standing over an invisible character does so in InDesign too, and without the
	  <w:rPr> here the span came back from the reader cut in two at the placeholder. */
/** The word a placeholder shows instead of its four hex digits, or nil for "show the digits".

	★★**ONE MEANING, ONE WORD** (2026-09-23, the user's call: turn the hex into words a few at a
	  time, because a word is read at a glance where "0018" is not).
	⚠**ONLY WHERE THE CODE POINT SAYS EXACTLY ONE THING.** U+0018 is the automatic page number AND
	 the automatic text AND any text variable - InDesign gives all three the same character - so
	 naming it after one of them would be a lie the reader in Word cannot check. It keeps its
	 digits until something other than the character itself can tell them apart.
	⚠**THE TAG IS WHAT THE READER MATCHES ON, NEVER THIS.** A word can be added or changed here
	 without touching the round trip, and a .docx written before the word existed still reads. */
const char* WordForPlaceholder(int32 cp)
{
	switch (cp)
	{
		case 0x0005:	return "ENDNOTE";	// an endnote hangs here; its words are a story of their own
		case 0xFFFC:	return "OBJECT";	// an anchored object
		case 0x0018:	return "VARIABLE";	// ★the automatic page number, the automatic text and any
											//  text variable are ONE character in InDesign. The user's
											//  call (2026-09-23): all three are variables to a reader,
											//  and a word they can read beats four digits they cannot.
		case 0xE02C:	return "INDEX";		// an index marker
		default:		return nil;
	}
}

void AppendPlaceholder(int32 cp, const Look& look, std::string& out)
{
	out += "<w:sdt><w:sdtPr><w:alias w:val=\"U+";
	AppendHex(cp, kTrue, out);
	out += "\"/><w:tag w:val=\"u";
	AppendHex(cp, kFalse, out);
	out += "\"/><w:lock w:val=\"sdtContentLocked\"/></w:sdtPr><w:sdtContent><w:r>";
	AppendRunProps(look, out);
	// ★★★**THE HEX IS WRITTEN HERE, AND THE WORD IS PUT IN AFTERWARDS** - see
	//   PutWordsIntoPlaceholders. Writing the word here instead makes it part of what the
	//   fingerprint is taken over, and every .docx written before the word was added stops
	//   matching its own tag (measured 2026-09-23: four tests failed the moment three characters
	//   were given words at this spot).
	out += "<w:t>\xE2\x9F\xA6";
	AppendHex(cp, kTrue, out);
	out += "\xE2\x9F\xA7</w:t></w:r></w:sdtContent></w:sdt>";
}

/*	PutWordsIntoPlaceholders
	Replace ⟦XXXX⟧ with ⟦WORD⟧ inside every placeholder whose character has a word, in an element
	that has already been written.

	★★★**AFTER THE FINGERPRINT, ALWAYS.** The tag's fingerprint is taken over the bytes
	  WriteStoryElements produces, so whatever those bytes hold is part of a story's IDENTITY. What
	  a placeholder SHOWS is not identity - it is for the person editing in Word - and the two have
	  to be kept apart, or adding a word today makes every file written yesterday unreadable as its
	  own origin. Doing it here costs one pass over the text and keeps the identity on the hex.
	⚠**EACH REPLACEMENT IS BOUNDED BY ITS OWN <w:sdt>**, so ⟦ ⟧ that a person typed into the story
	 is never touched - the search starts after that control's alias and stops at its end.
	⚠**THE READER NEVER LOOKS AT THIS.** It matches on <w:tag>, so a file written before a word
	 existed, or with a word this build does not know, reads exactly the same.
*/
void PutWordsIntoPlaceholders(std::string& xml)
{
	const std::string head = "<w:alias w:val=\"U+";
	size_t at = 0;
	for (;;)
	{
		const size_t a = xml.find(head, at);
		if (a == std::string::npos)
			break;
		const size_t hexFrom = a + head.size();
		const size_t quote = xml.find('"', hexFrom);
		const size_t end = xml.find("</w:sdt>", hexFrom);
		if (quote == std::string::npos || end == std::string::npos)
			break;

		const std::string hex = xml.substr(hexFrom, quote - hexFrom);
		int32 cp = 0;
		bool16 sound = hex.empty() ? kFalse : kTrue;
		for (size_t k = 0; k < hex.size() && sound; ++k)
		{
			const char c = hex[k];
			if (c >= '0' && c <= '9')			cp = cp * 16 + (c - '0');
			else if (c >= 'A' && c <= 'F')		cp = cp * 16 + (c - 'A' + 10);
			else								sound = kFalse;
		}
		const char* const word = sound ? WordForPlaceholder(cp) : nil;
		if (word != nil)
		{
			const std::string shown = "\xE2\x9F\xA6" + hex + "\xE2\x9F\xA7";
			const size_t found = xml.find(shown, quote);
			if (found != std::string::npos && found < end)
			{
				std::string put = "\xE2\x9F\xA6";
				put += word;
				put += "\xE2\x9F\xA7";
				xml.replace(found, shown.size(), put);
				at = found + put.size();
				continue;
			}
		}
		at = quote;
	}
}

/** The mark that says an ENDNOTE hangs here (2026-09-23, the user's call: the reader editing in
	Word has to be able to see that a note is attached at this point).

	★**IT IS A PLACEHOLDER LIKE ANY OTHER** - same tag, same lock - so the reader takes it back by
	  the route every placeholder takes, and nobody editing in Word can delete or move it.
	★**WHAT DIFFERS IS ONLY WHAT IT SHOWS**: "U+0005" tells a reader nothing, and this one exists to
	  be read by a person. ⚠The tag, not the shown text, is what the reader matches on - so this word
	  can be changed without touching the round trip.
	⚠**THE NOTE'S WORDS ARE NOT HERE AND NEVER WILL BE.** They are a story of their own with a file
	 of its own, which is where they are edited. */
void AppendEndnoteMark(const Look& look, std::string& out)
{
	AppendPlaceholder(0x0005, look, out);
}

void AppendNoteReference(int32 note, std::string& out)
{
	out += "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteReference w:id=\"";
	AppendNumber(note + 1, out);		// ids -1 and 0 are Word's own separators; the notes start at 1
	out += "\"/></w:r>";
}

bool16 IsBreakOrTab(int32 cp)
{
	return (cp == 0x000A || cp == 0x0009) ? kTrue : kFalse;
}

/** Whether a character has to travel as a placeholder rather than as itself.

	KCMStoryShape::IsInvisible is the rule, and XML adds two characters to it: U+FFFE and U+FFFF
	are not characters XML 1.0 allows at all, and ONE of them anywhere makes the whole package
	unreadable - HTML shrugs at them, which is why the shared rule does not name them. (The
	control characters, which XML forbids too, are IsInvisible's already.) */
bool16 NeedsPlaceholder(int32 cp)
{
	if (KCMStoryShape::IsInvisible(cp) || cp == 0xFFFE || cp == 0xFFFF)
		return kTrue;

	// ★TWO MORE, FOUND ON 2026-09-19 BY ASKING "IS THE ESCAPING ENTIRELY SOUND?" and written as
	//   failing tests first:
	//   - a LONE SURROGATE (U+D800..U+DFFF). Text that has been through a bad conversion can hold
	//     one; it is not an XML character, and one of them makes the package unreadable.
	//   - U+000D. It "never arrives" - it IS the paragraph boundary - and if it ever did, an XML
	//     parser would turn it into a line feed without a word. A placeholder says it was there.
	if ((cp >= 0xD800 && cp <= 0xDFFF) || cp == 0x000D)
		return kTrue;

	return kFalse;
}

/*	AppendRuns
	The characters [from, to) of a paragraph, as runs.

	⚠**inRuby: A PLACEHOLDER CANNOT STAND IN A <w:rubyBase>** - the schema takes runs there and
	 nothing else - so the paragraph is refused rather than the character dropped.
	★A forced break and a tab are asked about BEFORE IsInvisible: the break is one of its
	 characters, and here it is spelt as itself.
*/
bool16 AppendRuns(const std::string& text, const std::vector<int32>& cps,
				  const std::vector<int32>& byteAt, const std::vector<Look>& looks,
				  int32 from, int32 to, bool16 inRuby, std::string& out, std::string& whyNot)
{
	int32 i = from;
	while (i < to)
	{
		const size_t at = static_cast<size_t>(i);
		const int32 cp = cps[at];

		if (IsBreakOrTab(cp))
		{
			out += "<w:r>";
			AppendRunProps(looks[at], out);
			out += (cp == 0x000A) ? "<w:br/></w:r>" : "<w:tab/></w:r>";
			++i;
			continue;
		}

		if (NeedsPlaceholder(cp))
		{
			if (inRuby)
			{
				whyNot = "a ruby stands over an invisible character (U+";
				AppendHex(cp, kTrue, whyNot);
				whyNot += "), which a Word ruby cannot hold";
				return kFalse;
			}
			AppendPlaceholder(cp, looks[at], out);
			++i;
			continue;
		}

		int32 j = i + 1;
		while (j < to
			   && !IsBreakOrTab(cps[static_cast<size_t>(j)])
			   && !NeedsPlaceholder(cps[static_cast<size_t>(j)])
			   && looks[static_cast<size_t>(j)].SameAs(looks[at]))
			++j;

		const size_t b0 = static_cast<size_t>(byteAt[at]);
		const size_t b1 = (static_cast<size_t>(j) < byteAt.size())
						  ? static_cast<size_t>(byteAt[static_cast<size_t>(j)]) : text.size();
		out += "<w:r>";
		AppendRunProps(looks[at], out);
		out += "<w:t xml:space=\"preserve\">";
		AppendEscaped(text, b0, b1, out);
		out += "</w:t></w:r>";
		i = j;
	}
	return kTrue;
}

/** Give every character of each span its number, clipped to the paragraph. */
void PaintSpans(const KCMAttrSpanList& spans, int32 n, std::vector<Look>& looks, bool16 warichu)
{
	for (size_t k = 0; k < spans.size(); ++k)
	{
		const int32 from = (spans[k].fStart > 0) ? spans[k].fStart : 0;
		const int32 to = (spans[k].fStart + spans[k].fLen < n) ? spans[k].fStart + spans[k].fLen : n;
		for (int32 i = from; i < to; ++i)
		{
			if (warichu)
				looks[static_cast<size_t>(i)].fWarichu = static_cast<int32>(k);
			else
				looks[static_cast<size_t>(i)].fTcy = static_cast<int32>(k);
		}
	}
}

}	// anonymous namespace

bool16 WriteParagraphContent(const KCMStoryShape::Para& p, std::string& out, std::string& whyNot)
{
	std::vector<int32> cps;
	std::vector<int32> byteAt;
	KCMTextDiff::ToCodePoints(p.fText, &cps, &byteAt);
	const int32 n = static_cast<int32>(cps.size());

	// ---- what each character carries ---------------------------------------------------------
	std::vector<Look> looks(static_cast<size_t>(n));

	// The classes are settled BEFORE anything is written, so a kind with no name refuses the
	// paragraph whole instead of leaving half of it in `out`.
	std::vector<std::string> kentenClasses(p.fKenten.size());
	for (size_t k = 0; k < p.fKenten.size(); ++k)
	{
		if (!KCMStoryShape::KentenClassOf(p.fKenten[k].fValue, kentenClasses[k]))
		{
			whyNot = "a kenten kind this format cannot name: " + p.fKenten[k].fValue;
			return kFalse;
		}
	}
	for (size_t k = 0; k < p.fKenten.size(); ++k)
	{
		const int32 from = (p.fKenten[k].fStart > 0) ? p.fKenten[k].fStart : 0;
		const int32 to = (p.fKenten[k].fStart + p.fKenten[k].fLen < n)
						 ? p.fKenten[k].fStart + p.fKenten[k].fLen : n;
		for (int32 i = from; i < to; ++i)
			looks[static_cast<size_t>(i)].fKentenClass = &kentenClasses[k];
	}
	PaintSpans(p.fTcy, n, looks, kFalse);
	PaintSpans(p.fWarichu, n, looks, kTrue);

	// ⚠**A READING IS TEXT TOO, AND IT HAS NOWHERE TO PUT A PLACEHOLDER**: a <w:rt> takes a run and
	//  nothing else. A reading holding a character that cannot travel as itself would otherwise go
	//  into the XML raw - and a control character there makes the whole package unreadable (found
	//  by reading this again on 2026-09-19, and true: the test was written first and failed).
	for (size_t k = 0; k < p.fRuby.size(); ++k)
	{
		std::vector<int32> reading;
		KCMTextDiff::ToCodePoints(p.fRuby[k].fValue, &reading, nil);
		for (size_t c = 0; c < reading.size(); ++c)
		{
			if (NeedsPlaceholder(reading[c]) || IsBreakOrTab(reading[c]))
			{
				whyNot = "a ruby's reading holds a character that cannot be written as itself (U+";
				AppendHex(reading[c], kTrue, whyNot);
				whyNot += ")";
				return kFalse;
			}
		}
	}

	std::vector<int32> rubyOf(static_cast<size_t>(n), -1);
	for (size_t k = 0; k < p.fRuby.size(); ++k)
	{
		const int32 from = (p.fRuby[k].fStart > 0) ? p.fRuby[k].fStart : 0;
		const int32 to = (p.fRuby[k].fStart + p.fRuby[k].fLen < n) ? p.fRuby[k].fStart + p.fRuby[k].fLen : n;
		for (int32 i = from; i < to; ++i)
		{
			if (rubyOf[static_cast<size_t>(i)] < 0)
				rubyOf[static_cast<size_t>(i)] = static_cast<int32>(k);
		}
	}

	// ---- the walk ------------------------------------------------------------------------------
	//
	// Written into a string of its own, so that a refusal half way leaves `out` as it was found.
	std::string made;
	const std::vector<KCMStoryShape::NoteRef>& refs = p.fNoteRefs;
	const std::vector<int32>& endMarks = p.fEndnoteAt;
	size_t ref = 0;
	size_t endMark = 0;
	int32 i = 0;

	while (i < n)
	{
		while (ref < refs.size() && refs[ref].fAt <= i)
		{
			AppendNoteReference(refs[ref].fNote, made);
			++ref;
		}
		// ★**THE ENDNOTE MARKS GO IN ON THE SAME WALK** - see AppendEndnoteMark. They carry no
		//   look of their own (the marker is not in the text, so there is no run to read one from).
		while (endMark < endMarks.size() && endMarks[endMark] <= i)
		{
			AppendEndnoteMark(Look(), made);
			++endMark;
		}

		const int32 k = rubyOf[static_cast<size_t>(i)];
		if (k >= 0)
		{
			int32 j = i;
			while (j < n && rubyOf[static_cast<size_t>(j)] == k)
				++j;

			if (ref < refs.size() && refs[ref].fAt < j)
			{
				whyNot = "a footnote reference stands inside a ruby's base text, which a Word ruby cannot hold";
				return kFalse;
			}
			// ⚠**A PLACEHOLDER CANNOT STAND IN A <w:rubyBase>** (AppendRuns says so) - and an
			//  endnote mark IS one, so the same refusal has to cover it rather than writing a
			//  package Word will not open.
			if (endMark < endMarks.size() && endMarks[endMark] < j)
			{
				whyNot = "an endnote's marker stands inside a ruby's base text, which a Word ruby cannot hold";
				return kFalse;
			}

			const std::string& reading = p.fRuby[static_cast<size_t>(k)].fValue;
			made += "<w:r><w:ruby><w:rubyPr><w:rubyAlign w:val=\"distributeSpace\"/><w:hps w:val=\"10\"/>"
					"<w:hpsRaise w:val=\"18\"/><w:hpsBaseText w:val=\"21\"/><w:lid w:val=\"ja-JP\"/></w:rubyPr>"
					"<w:rt><w:r><w:rPr><w:sz w:val=\"10\"/></w:rPr><w:t xml:space=\"preserve\">";	// a reading's own spaces are its own
			AppendEscaped(reading, 0, reading.size(), made);
			made += "</w:t></w:r></w:rt><w:rubyBase>";
			if (!AppendRuns(p.fText, cps, byteAt, looks, i, j, kTrue, made, whyNot))
				return kFalse;
			made += "</w:rubyBase></w:ruby></w:r>";
			i = j;
			continue;
		}

		// ⚠**THE RUN HAS TO STOP WHERE A MARK STANDS.** AppendRuns writes [i, j) in one go, so a
		//  place inside that range is never reached by the walk - which is how the first endnote
		//  mark written here came out nowhere at all (measured 2026-09-23, by a test that asked for
		//  the offset back rather than only asking whether the two sides agreed: they agreed on
		//  NOTHING being there).
		int32 j = i + 1;
		while (j < n && rubyOf[static_cast<size_t>(j)] < 0
			   && !(ref < refs.size() && refs[ref].fAt == j)
			   && !(endMark < endMarks.size() && endMarks[endMark] == j))
			++j;
		if (!AppendRuns(p.fText, cps, byteAt, looks, i, j, kFalse, made, whyNot))
			return kFalse;
		i = j;
	}

	// The references standing at the paragraph's end (and any whose place is past it).
	for (; ref < refs.size(); ++ref)
		AppendNoteReference(refs[ref].fNote, made);
	for (; endMark < endMarks.size(); ++endMark)
		AppendEndnoteMark(Look(), made);

	out += made;
	return kTrue;
}

namespace
{

/*	★★★A TABLE INSIDE A PARAGRAPH IS NOT CARRIED TO WORD, AND NOT READ BACK FROM IT (the design,
	4-5 as rewritten 2026-09-19 evening - the user's rule: "the document decides").

	Word has no table inside a paragraph: a table stands between paragraphs. InDesign's one paragraph
	"A[T]B" and its three paragraphs "A / [T] / B" are therefore written the same way - A, the
	table, B - and NOTHING in the file says which it was. Two spellings tried before this one said
	it: a paragraph style (stages 1-3), then a pair of locked marks (stage 3b), and both were found
	hard to use in Word (the user: the marks were in the way, a copied one could not be deleted).

	What settles the shape instead is the DOCUMENT AS IT STANDS when the file comes back: the import
	rejoins the read paragraphs around each table exactly as the document holds that table
	(RejoinTables), and a paragraph break a person put next to a table in Word, or took away there,
	carries no meaning. The cost, taken knowingly: splitting or joining a paragraph AT a table cannot
	be done from Word - that is done in InDesign.

	The one shape this file writes and reads is the SPLIT one (SplitAtTables): every table alone in
	an empty paragraph of its own, the words before it a paragraph, the words after it a paragraph;
	and Word's two rules kept - a table cannot end its container, and two tables cannot touch - by
	an empty paragraph exactly where they ask for one. The fingerprint is taken of that shape, so it
	is the same whether InDesign's own shape was one paragraph or three. */

// A table inside a table inside a table... The data cannot really do this (a nested table comes
// later in Story::fTables than the one it stands in), so this only stops a malformed Story from
// walking for ever.
const int32 kDeepestNesting = 32;

/** The characters [from, to) of a paragraph, with every span that reaches into them, re-based.

	★**A NOTE REFERENCE AT A CUT BELONGS TO THE PIECE BEFORE IT**: the pieces are cut where tables
	  stand, and a reference at that very place is written in front of the table - once. So a
	  piece takes the references with from < fAt <= to, and the FIRST piece takes fAt == 0 too.
	  ⚠**"FIRST" IS SAID BY THE CALLER, NOT READ OFF from == 0**: a table standing at the very head
	   of its paragraph makes a first piece that is [0, 0) and a second that ALSO starts at 0, and
	   both took the reference (2026-09-19 - found by reading, and a failing test before the fix).
	⚠A RUBY CUT BY A TABLE comes out as two rubies with the same reading, one on each side. That is
	  what the page shows, and it is not what the Story said - so the export's own check (stage 2)
	  will refuse such a story rather than let it round-trip into two. */
KCMStoryShape::Para Slice(const KCMStoryShape::Para& p, const std::vector<int32>& byteAt,
						 int32 from, int32 to, bool16 first)
{
	KCMStoryShape::Para piece;

	const size_t n = byteAt.size();
	const size_t b0 = (static_cast<size_t>(from) < n) ? static_cast<size_t>(byteAt[static_cast<size_t>(from)])
													  : p.fText.size();
	const size_t b1 = (static_cast<size_t>(to) < n) ? static_cast<size_t>(byteAt[static_cast<size_t>(to)])
													: p.fText.size();
	piece.fText = p.fText.substr(b0, b1 - b0);

	const KCMAttrSpanList* const lists[4] = { &p.fRuby, &p.fKenten, &p.fTcy, &p.fWarichu };
	KCMAttrSpanList* const made[4] = { &piece.fRuby, &piece.fKenten, &piece.fTcy, &piece.fWarichu };
	for (int32 which = 0; which < 4; ++which)
	{
		for (size_t k = 0; k < lists[which]->size(); ++k)
		{
			KCMAttrSpan span = (*lists[which])[k];
			const int32 s0 = (span.fStart > from) ? span.fStart : from;
			const int32 s1 = (span.fStart + span.fLen < to) ? span.fStart + span.fLen : to;
			if (s1 <= s0)
				continue;
			span.fStart = s0 - from;
			span.fLen = s1 - s0;
			made[which]->push_back(span);
		}
	}

	for (size_t k = 0; k < p.fNoteRefs.size(); ++k)
	{
		const int32 at = p.fNoteRefs[k].fAt;
		if ((at > from && at <= to) || (first && at <= from))
		{
			KCMStoryShape::NoteRef ref = p.fNoteRefs[k];
			ref.fAt = at - from;
			piece.fNoteRefs.push_back(ref);
		}
	}
	// ★**THE ENDNOTE MARKS ARE SHARED OUT ON THE SAME TERMS** (2026-09-23). ⚠Leaving them out of
	//   this is how the first version of the mark reached no file at all: every paragraph comes
	//   through here, table or no table, so a field this does not copy simply does not exist by the
	//   time anything is written. **Measured, not reasoned about** - the test asked for the offset
	//   back rather than only asking whether the two sides agreed.
	for (size_t k = 0; k < p.fEndnoteAt.size(); ++k)
	{
		const int32 at = p.fEndnoteAt[k];
		if ((at > from && at <= to) || (first && at <= from))
			piece.fEndnoteAt.push_back(at - from);
	}
	return piece;
}

/** One <w:p>. An empty one is <w:p/>. */
bool16 AppendParagraph(const KCMStoryShape::Para& piece, std::string& out, std::string& whyNot)
{
	std::string content;
	if (!WriteParagraphContent(piece, content, whyNot))
		return kFalse;

	if (content.empty())
	{
		out += "<w:p/>";
		return kTrue;
	}
	out += "<w:p>";
	out += content;
	out += "</w:p>";
	return kTrue;
}

/*	Slot
	One <w:tc> of a row as Word wants it: an anchor cell of the Story's, or a cell a vertical merge
	from above covers.
*/
struct Slot
{
	int32	fCell;		// index into Row::fCells, or -1 for a covered cell
	int32	fSpan;		// grid columns
	bool16	fRestarts;	// an anchor that reaches down into the rows below

	Slot() : fCell(-1), fSpan(1), fRestarts(kFalse) {}
};

struct Carry
{
	int32	fRowsLeft;
	int32	fSpan;

	Carry() : fRowsLeft(0), fSpan(1) {}
};

/** Lay one row out over the grid. `carry` is the merges reaching down from the rows above, by the
	grid column each starts at, and is updated for the row below. @return the columns the row used. */
int32 LayRowOut(const KCMStoryShape::Row& row, std::vector<Carry>& carry, std::vector<Slot>& outSlots)
{
	outSlots.clear();

	int32 col = 0;
	size_t next = 0;
	for (;;)
	{
		if (static_cast<size_t>(col) < carry.size() && carry[static_cast<size_t>(col)].fRowsLeft > 0)
		{
			Slot covered;
			covered.fSpan = carry[static_cast<size_t>(col)].fSpan;
			--carry[static_cast<size_t>(col)].fRowsLeft;
			outSlots.push_back(covered);
			col += covered.fSpan;
			continue;
		}

		if (next >= row.fCells.size())
		{
			// The anchors are used up. A merge reaching down further to the right still needs its
			// covered cell; a ragged gap before it is left a gap.
			size_t further = static_cast<size_t>(col);
			while (further < carry.size() && carry[further].fRowsLeft <= 0)
				++further;
			if (further >= carry.size())
				break;
			col = static_cast<int32>(further);
			continue;
		}

		const KCMStoryShape::Cell& cell = row.fCells[next];
		Slot anchor;
		anchor.fCell = static_cast<int32>(next);
		anchor.fSpan = (cell.fColSpan > 0) ? cell.fColSpan : 1;
		anchor.fRestarts = (cell.fRowSpan > 1) ? kTrue : kFalse;

		if (carry.size() < static_cast<size_t>(col + anchor.fSpan))
			carry.resize(static_cast<size_t>(col + anchor.fSpan));
		if (anchor.fRestarts)
		{
			carry[static_cast<size_t>(col)].fRowsLeft = cell.fRowSpan - 1;
			carry[static_cast<size_t>(col)].fSpan = anchor.fSpan;
		}

		outSlots.push_back(anchor);
		col += anchor.fSpan;
		++next;
	}
	return col;
}

bool16 AppendBlocks(const KCMStoryShape::Story& s, const std::vector<KCMStoryShape::Para>& paras,
					int32 inTable, int32 inRow, int32 inCell, int32 depth, std::string& out,
					std::string& whyNot);

bool16 AppendTable(const KCMStoryShape::Story& s, size_t index, int32 depth, std::string& out,
				   std::string& whyNot)
{
	const KCMStoryShape::Table& table = s.fTables[index];

	// ---- the grid, settled before a byte is written: <w:tblGrid> comes first in the markup ------
	std::vector< std::vector<Slot> > rows(table.fRows.size());
	int32 columns = 1;
	{
		std::vector<Carry> carry;
		for (size_t r = 0; r < table.fRows.size(); ++r)
		{
			const int32 used = LayRowOut(table.fRows[r], carry, rows[r]);
			if (used > columns)
				columns = used;
		}
	}
	const int32 columnWidth = 9000 / columns;		// twips; Word re-fits them, this is a start

	out += "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders>"
		   "<w:top w:val=\"single\" w:sz=\"4\"/><w:left w:val=\"single\" w:sz=\"4\"/>"
		   "<w:bottom w:val=\"single\" w:sz=\"4\"/><w:right w:val=\"single\" w:sz=\"4\"/>"
		   "<w:insideH w:val=\"single\" w:sz=\"4\"/><w:insideV w:val=\"single\" w:sz=\"4\"/>"
		   "</w:tblBorders></w:tblPr><w:tblGrid>";
	for (int32 c = 0; c < columns; ++c)
	{
		out += "<w:gridCol w:w=\"";
		AppendNumber(columnWidth, out);
		out += "\"/>";
	}
	out += "</w:tblGrid>";

	for (size_t r = 0; r < table.fRows.size(); ++r)
	{
		out += "<w:tr>";
		if (table.fRows[r].fHeader)
			out += "<w:trPr><w:tblHeader/></w:trPr>";

		for (size_t k = 0; k < rows[r].size(); ++k)
		{
			const Slot& slot = rows[r][k];
			out += "<w:tc><w:tcPr><w:tcW w:w=\"";
			AppendNumber(columnWidth * slot.fSpan, out);
			out += "\" w:type=\"dxa\"/>";
			if (slot.fSpan > 1)
			{
				out += "<w:gridSpan w:val=\"";
				AppendNumber(slot.fSpan, out);
				out += "\"/>";
			}
			if (slot.fCell < 0)
				out += "<w:vMerge/>";
			else if (slot.fRestarts)
				out += "<w:vMerge w:val=\"restart\"/>";
			out += "</w:tcPr>";

			if (slot.fCell < 0)
			{
				out += "<w:p/>";
			}
			else if (!AppendBlocks(s, table.fRows[r].fCells[static_cast<size_t>(slot.fCell)].fParas,
								   static_cast<int32>(index), static_cast<int32>(r), slot.fCell,
								   depth + 1, out, whyNot))
			{
				return kFalse;
			}
			out += "</w:tc>";
		}
		out += "</w:tr>";
	}
	out += "</w:tbl>";
	return kTrue;
}

bool16 AppendBlocks(const KCMStoryShape::Story& s, const std::vector<KCMStoryShape::Para>& paras,
					int32 inTable, int32 inRow, int32 inCell, int32 depth, std::string& out,
					std::string& whyNot)
{
	if (depth > kDeepestNesting)
	{
		whyNot = "the tables are nested deeper than this format writes";
		return kFalse;
	}

	if (paras.empty())
	{
		out += "<w:p/>";		// a cell of Word's cannot be empty
		return kTrue;
	}

	// ★THE STORY IS IN THE SPLIT SHAPE BY THE TIME IT GETS HERE (SplitAtTables, run by WriteBlocks on
	//   a copy): every table stands alone in an empty paragraph of its own, so a paragraph either IS
	//   a table or holds words, and the blocks come out one per paragraph. Where the split shape has
	//   an empty paragraph, Word wants one - after a table at the end of its container, and between
	//   two tables - and it is written as <w:p/> like any other.
	for (size_t i = 0; i < paras.size(); ++i)
	{
		bool16 wasTable = kFalse;
		for (size_t t = 0; t < s.fTables.size(); ++t)
		{
			const KCMStoryShape::Table& table = s.fTables[t];
			if (table.fInTable != inTable || table.fParaIndex != static_cast<int32>(i))
				continue;
			if (inTable >= 0 && (table.fInRow != inRow || table.fInCell != inCell))
				continue;
			if (!AppendTable(s, t, depth, out, whyNot))
				return kFalse;
			wasTable = kTrue;
			break;			// one table per paragraph in this shape; the split put it there
		}
		if (wasTable)
			continue;
		if (!AppendParagraph(paras[i], out, whyNot))
			return kFalse;
	}
	return kTrue;
}

}	// anonymous namespace

bool16 WriteBlocks(const KCMStoryShape::Story& s, const std::vector<KCMStoryShape::Para>& paras,
				   int32 inTable, int32 inRow, int32 inCell, std::string& out, std::string& whyNot)
{
	// ★ON A COPY IN THE SPLIT SHAPE (SplitAtTables): the one shape this file writes, whatever shape the
	//   story came in - see the note above AppendParagraph. `paras` names WHICH run of paragraphs
	//   (the body, or a cell), and the same run of the copy is what is written.
	KCMStoryShape::Story split = s;
	SplitAtTables(split, kFalse /*as written: a ruby cut by a table is written on both sides*/);
	const std::vector<KCMStoryShape::Para>* run = &split.fBody;
	if (inTable >= 0)
	{
		if (static_cast<size_t>(inTable) >= split.fTables.size()
			|| static_cast<size_t>(inRow) >= split.fTables[static_cast<size_t>(inTable)].fRows.size()
			|| static_cast<size_t>(inCell) >= split.fTables[static_cast<size_t>(inTable)].fRows[static_cast<size_t>(inRow)].fCells.size())
		{
			whyNot = "no such cell";
			return kFalse;
		}
		run = &split.fTables[static_cast<size_t>(inTable)].fRows[static_cast<size_t>(inRow)].fCells[static_cast<size_t>(inCell)].fParas;
	}
	(void)paras;		// the caller's run, named; the copy's is what is written

	// Into a string of its own: a refusal from deep inside a cell leaves `out` as it was found.
	std::string made;
	if (!AppendBlocks(split, *run, inTable, inRow, inCell, 0, made, whyNot))
		return kFalse;
	out += made;
	return kTrue;
}

namespace
{

const char* const kXmlDeclaration = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
const char* const kWordNamespace = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"";
const char* const kStoryTagNamespace = "urn:kohaku:kcm:story:1";

/*	The built-in kenten kinds, and the nearest of Word's four emphasis marks for each.

	★★**THIS IS NOW THE ONLY PLACE THE BUILT-IN KINDS ARE ENUMERATED** (2026-09-21). There used to
	 be a second copy in KCMStoryHtml.cpp - the same names with CSS beside each instead of Word's
	 marks - and it went with the HTML spelling. ⚠Nothing else needs such a list: a kind this build
	 has never heard of travels under its own name whether or not it is written here
	 (KCMStoryShape::KentenClassOf). What is below is only the LOOK Word is asked for, and what says
	 the names are still spelt right is a test rather than a comment: work/kcm-storydocx-test,
	 TestKentenNames, puts every name below through KCMStoryShape::KentenValueOfClass and
	 KentenClassOf.
	★The look is only the nearest one - sesame marks are Word's comma, the hollow ones its circle,
	 everything else its dot. The NAME carries the kind (the header says why).
*/
struct KentenStyle
{
	const char*	fValue;			// KCM's own name for the kind; the class is asked for, not kept here
	const char*	fWordMark;
};

// ⚠★★**THE VALUE, NOT THE CLASS** (2026-09-22). The class is Japanese now and KCMStoryShape's table
//   is the one place that says which Japanese word a kind travels under - so this holds KCM's own
//   value and asks for the class, rather than keeping a second copy of ten names that would be free
//   to drift ([[one-question-one-place]]).
const KentenStyle kBuiltInKenten[] =
{
	{ "BlackSesameDot",		"comma" },
	{ "WhiteSesameDot",		"comma" },
	{ "BlackCircle",		"dot" },
	{ "WhiteCircle",		"circle" },
	{ "SmallBlackCircle",	"dot" },
	{ "SmallWhiteCircle",	"circle" },
	{ "BlackTriangle",		"dot" },
	{ "WhiteTriangle",		"circle" },
	{ "Bullseye",			"circle" },
	{ "Fisheye",			"circle" }
};
const size_t kBuiltInKentenCount = sizeof(kBuiltInKenten) / sizeof(kBuiltInKenten[0]);

void AppendKentenStyle(const std::string& cls, const char* wordMark, std::string& out)
{
	out += "<w:style w:type=\"character\" w:customStyle=\"1\" w:styleId=\"";
	out += kKentenStylePrefix;
	AppendEscaped(cls, 0, cls.size(), out);
	out += "\"><w:name w:val=\"";
	out += kKentenStylePrefix;
	AppendEscaped(cls, 0, cls.size(), out);
	out += "\"/><w:basedOn w:val=\"DefaultParagraphFont\"/><w:qFormat/><w:rPr><w:em w:val=\"";
	out += wordMark;
	out += "\"/></w:rPr></w:style>";
}

bool16 WriteStyles(const KCMStoryShape::Story& s, std::string& out, std::string& whyNot)
{
	out = kXmlDeclaration;
	out += "<w:styles ";
	out += kWordNamespace;
	// ★THE DEFAULTS: 10.5pt, and East Asian text that is JAPANESE. The ruby written above gives its
	//   own sizes against a 10.5pt base (hpsBaseText 21), so without this the base is Word's 10pt
	//   and the two disagree; and without the language Word breaks the lines of a Japanese story
	//   by another language's rules. No font is named: that is the reader's Word's business.
	out += ">"
		   "<w:docDefaults><w:rPrDefault><w:rPr><w:sz w:val=\"21\"/><w:szCs w:val=\"21\"/>"
		   "<w:lang w:val=\"en-US\" w:eastAsia=\"ja-JP\"/></w:rPr></w:rPrDefault></w:docDefaults>"
		   "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/>"
		   "<w:qFormat/></w:style>"
		   "<w:style w:type=\"character\" w:default=\"1\" w:styleId=\"DefaultParagraphFont\">"
		   "<w:name w:val=\"Default Paragraph Font\"/><w:uiPriority w:val=\"1\"/><w:semiHidden/></w:style>";
	// (no paragraph style of ours: the slot is left for InDesign's paragraph style names - kMarkContinues says why)

	for (size_t i = 0; i < kBuiltInKentenCount; ++i)
	{
		std::string builtInClass;
		if (!KCMStoryShape::KentenClassOf(kBuiltInKenten[i].fValue, builtInClass))
		{
			// Only reachable if this table and KCMStoryShape's have drifted apart, which is what
			// TestKentenNames is there to catch before a build ever runs.
			whyNot = "a built-in kenten kind has no class: ";
			whyNot += kBuiltInKenten[i].fValue;
			return kFalse;
		}
		AppendKentenStyle(builtInClass, kBuiltInKenten[i].fWordMark, out);
	}

	// The custom marks this story uses, sorted: the order the story happens to use them in must
	// not reach the bytes.
	std::vector<std::string> values;
	KCMStoryShape::CollectKentenValues(s, values);
	std::vector<std::string> custom;
	for (size_t v = 0; v < values.size(); ++v)
	{
		std::string cls;
		if (!KCMStoryShape::KentenClassOf(values[v], cls))
		{
			whyNot = "a kenten kind this format cannot name: " + values[v];
			return kFalse;
		}
		bool16 builtIn = kFalse;
		for (size_t i = 0; i < kBuiltInKentenCount && !builtIn; ++i)
		{
			std::string builtInClass;
			if (KCMStoryShape::KentenClassOf(kBuiltInKenten[i].fValue, builtInClass))
				builtIn = (cls == builtInClass) ? kTrue : kFalse;
		}
		if (!builtIn)
			custom.push_back(cls);
	}
	for (size_t a = 1; a < custom.size(); ++a)			// a handful at most: an insertion sort
	{
		for (size_t b = a; b > 0 && custom[b] < custom[b - 1]; --b)
			custom[b].swap(custom[b - 1]);
	}
	for (size_t c = 0; c < custom.size(); ++c)
	{
		if (c == 0 || custom[c] != custom[c - 1])
			AppendKentenStyle(custom[c], "dot", out);
	}

	out += "</w:styles>";
	return kTrue;
}

/** Tick off every note the paragraphs refer to. kFalse for a reference to a note that is not there. */
bool16 TickReferences(const std::vector<KCMStoryShape::Para>& paras, std::vector<bool16>& seen,
					  std::string& whyNot)
{
	for (size_t i = 0; i < paras.size(); ++i)
	{
		for (size_t k = 0; k < paras[i].fNoteRefs.size(); ++k)
		{
			const int32 note = paras[i].fNoteRefs[k].fNote;
			if (note < 0 || static_cast<size_t>(note) >= seen.size())
			{
				whyNot = "a reference points at footnote ";
				AppendNumber(note + 1, whyNot);
				whyNot += ", which the story does not have";
				return kFalse;
			}
			// ⚠TWICE IS AS BAD AS NEVER: two <w:footnoteReference> with one id is not a document Word
			//  can keep, and InDesign cannot make one either - so this is a damaged Story, refused.
			if (seen[static_cast<size_t>(note)])
			{
				whyNot = "footnote ";
				AppendNumber(note + 1, whyNot);
				whyNot += " is referred to more than once";
				return kFalse;
			}
			seen[static_cast<size_t>(note)] = kTrue;
		}
	}
	return kTrue;
}

/** Word cannot keep a note nothing refers to, so every note has to be reached from the text. */
bool16 EveryNoteIsReferredTo(const KCMStoryShape::Story& s, std::string& whyNot)
{
	std::vector<bool16> seen(s.fNotes.size(), kFalse);

	if (!TickReferences(s.fBody, seen, whyNot))
		return kFalse;
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
		{
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
			{
				if (!TickReferences(s.fTables[t].fRows[r].fCells[c].fParas, seen, whyNot))
					return kFalse;
			}
		}
	}

	for (size_t n = 0; n < seen.size(); ++n)
	{
		if (!seen[n])
		{
			whyNot = "footnote ";
			AppendNumber(static_cast<int32>(n) + 1, whyNot);
			whyNot += " has no reference in the text, and Word cannot keep a note without one";
			return kFalse;
		}
	}
	return kTrue;
}

/** <w:footnotes ...>...</w:footnotes>, with no XML declaration in front of it. */
bool16 WriteFootnotesElement(const KCMStoryShape::Story& s, std::string& out, std::string& whyNot)
{
	out = "<w:footnotes ";
	out += kWordNamespace;
	out += ">"
		   "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
		   "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/>"
		   "</w:r></w:p></w:footnote>";

	for (size_t n = 0; n < s.fNotes.size(); ++n)
	{
		out += "<w:footnote w:id=\"";
		AppendNumber(static_cast<int32>(n) + 1, out);
		out += "\">";

		// A note with no paragraph at all still gets one: its own mark has to stand somewhere.
		const size_t count = s.fNotes[n].empty() ? 1 : s.fNotes[n].size();
		for (size_t i = 0; i < count; ++i)
		{
			std::string content;
			if (i < s.fNotes[n].size() && !WriteParagraphContent(s.fNotes[n][i], content, whyNot))
				return kFalse;

			out += "<w:p>";
			if (i == 0)
				out += "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:footnoteRef/></w:r>";
			out += content;
			out += "</w:p>";
		}
		out += "</w:footnote>";
	}
	out += "</w:footnotes>";
	return kTrue;
}

void AddPart(const char* name, const std::string& bytes, std::vector<KCMZipStore::Entry>& parts)
{
	KCMZipStore::Entry e;
	e.fName = name;
	e.fBytes = bytes;
	parts.push_back(e);
}

}	// anonymous namespace

namespace
{

/** The two elements that ARE the story - <w:document> and, when it has notes, <w:footnotes> - with
	no XML declaration in front of either. Everything that can refuse a story refuses it here. */
bool16 WriteStoryElements(const KCMStoryShape::Story& s, std::string& outDocument,
						  std::string& outFootnotes, std::string& whyNot)
{
	outDocument.clear();
	outFootnotes.clear();

	if (!EveryNoteIsReferredTo(s, whyNot))
		return kFalse;

	outDocument = "<w:document ";
	outDocument += kWordNamespace;
	outDocument += "><w:body>";
	// (No legend since 2026-09-19 evening - the user's call: a plain file. The marks it explained
	//  are gone too; see the note above AppendParagraph.)
	if (!WriteBlocks(s, s.fBody, -1, 0, 0, outDocument, whyNot))
		return kFalse;
	outDocument += "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>";
	if (s.fVertical)
		outDocument += "<w:textDirection w:val=\"tbRl\"/>";
	outDocument += "</w:sectPr></w:body></w:document>";

	if (!s.fNotes.empty() && !WriteFootnotesElement(s, outFootnotes, whyNot))
		return kFalse;
	return kTrue;
}

/** "<bytes>-<crc32, 8 hex digits>" of the two elements, one after the other. */
void FingerprintOf(const std::string& documentElement, const std::string& footnotesElement,
				   std::string& out)
{
	const std::string both = documentElement + footnotesElement;
	char buf[40] = { 0 };
	std::snprintf(buf, sizeof(buf), "%u-%08x", static_cast<unsigned int>(both.size()),
				  KCMZipStore::Crc32(both.data(), both.size()));
	out = buf;
}

}	// anonymous namespace

bool16 Fingerprint(const KCMStoryShape::Story& s, std::string& outFingerprint, std::string& whyNot)
{
	outFingerprint.clear();

	std::string documentElement;
	std::string footnotesElement;
	if (!WriteStoryElements(s, documentElement, footnotesElement, whyNot))
		return kFalse;

	FingerprintOf(documentElement, footnotesElement, outFingerprint);
	return kTrue;
}

bool16 WriteParts(const KCMStoryShape::Story& s, int32 uid,
				  std::vector<KCMZipStore::Entry>& outParts, std::string& whyNot)
{
	outParts.clear();
	whyNot.clear();

	const bool16 hasNotes = s.fNotes.empty() ? kFalse : kTrue;

	// ---- everything that can refuse, before any part is made -----------------------------------
	std::string documentElement;
	std::string footnotesElement;
	if (!WriteStoryElements(s, documentElement, footnotesElement, whyNot))
		return kFalse;

	// ★★★**THE FINGERPRINT IS TAKEN HERE, BEFORE THE WORDS GO IN** (2026-09-23). What a placeholder
	//   SHOWS is for the person editing in Word; what the fingerprint is taken over is the story's
	//   identity. Keeping the two apart is what lets a word be added to a later build without every
	//   .docx written by an earlier one ceasing to match its own tag - which is exactly what
	//   happened when the words were written at the source (four tests, on files kept from earlier
	//   runs, failed at once).
	std::string fingerprint;
	FingerprintOf(documentElement, footnotesElement, fingerprint);
	PutWordsIntoPlaceholders(documentElement);
	PutWordsIntoPlaceholders(footnotesElement);

	std::string styles;
	if (!WriteStyles(s, styles, whyNot))
		return kFalse;

	// ---- the parts -------------------------------------------------------------------------------
	std::string types = kXmlDeclaration;
	types += "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
			 "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
			 "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
			 "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
			 "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
			 "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>";
	if (hasNotes)
		types += "<Override PartName=\"/word/footnotes.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/>";
	types += "<Override PartName=\"/customXml/itemProps1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.customXmlProperties+xml\"/>"
			 "</Types>";
	AddPart("[Content_Types].xml", types, outParts);

	std::string rootRels = kXmlDeclaration;
	rootRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
				"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
				"</Relationships>";
	AddPart("_rels/.rels", rootRels, outParts);

	AddPart("word/document.xml", std::string(kXmlDeclaration) + documentElement, outParts);

	std::string docRels = kXmlDeclaration;
	docRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
			   "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
			   "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
			   "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml\" Target=\"../customXml/item1.xml\"/>";
	if (hasNotes)
		docRels += "<Relationship Id=\"rId4\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes\" Target=\"footnotes.xml\"/>";
	docRels += "</Relationships>";
	AddPart("word/_rels/document.xml.rels", docRels, outParts);

	AddPart("word/styles.xml", styles, outParts);

	std::string settings = kXmlDeclaration;
	settings += "<w:settings ";
	settings += kWordNamespace;
	// ★★TRACKING IS ON, AND PROTECTED AS "TRACKED CHANGES ONLY" (2026-09-19, the user's pick): the
	//   import tells Word's changes from everybody else's by Word's own revision marks, so an editor
	//   who switches tracking off by accident costs themselves that (the import then compares the
	//   whole text - Fingerprint, in the header). Word's protection of this kind makes the switch
	//   unavailable. ⚠NO PASSWORD, deliberately: it is there to stop an accident, not a person -
	//   whoever means to lift it can, from Word's own Review tab.
	//   The order is the schema's (CT_Settings): trackRevisions, then documentProtection.
	settings += "><w:trackRevisions/><w:documentProtection w:edit=\"trackedChanges\" w:enforcement=\"1\"/>";
	if (hasNotes)
		settings += "<w:footnotePr><w:footnote w:id=\"-1\"/><w:footnote w:id=\"0\"/></w:footnotePr>";
	settings += "</w:settings>";
	AddPart("word/settings.xml", settings, outParts);

	if (hasNotes)
		AddPart("word/footnotes.xml", std::string(kXmlDeclaration) + footnotesElement, outParts);

	// ---- the tag: which story this is, and a fingerprint of it as written ------------------------
	//
	// ★★★**NOT A WORD OF THE STORY IS IN HERE** (the user's decision, 2026-09-19, going back on the
	//   "origin" this part held for half a day - the whole story a second time, hidden). A file is
	//   handed on, and used again for something else; text nobody can see would travel with it,
	//   and would still name the old story after the visible one had been replaced.
	//   WHAT WORD CHANGED IS TOLD BY WORD'S OWN REVISION MARKS. The fingerprint is what says
	//   whether those marks are the whole truth: the import rebuilds the story as it stood (the
	//   deletions put back, the insertions left out), takes ITS fingerprint, and compares. The same
	//   -> only Word's changes are shown. Different -> tracking was off for some of the editing,
	//   or the changes were accepted, or the file holds something else by now: the import then
	//   compares the whole text, the way the HTML import does, and says that it did.
	// ⚠**NO LINE BREAK AFTER THE DECLARATION.** Word does not copy this part through a save, it
	//  parses it and writes it out again, and what it writes has none (measured 2026-09-19: the
	//  part came back two bytes shorter, the CRLF and nothing else). Written the way Word writes
	//  it, the part is the same bytes before and after - which is what lets a test say so.
	// (the fingerprint was taken at the top of this function, before the placeholders were given
	//  their words - see PutWordsIntoPlaceholders for why it has to be that way round)

	std::string tag = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><kcm:story xmlns:kcm=\"";
	tag += kStoryTagNamespace;
	tag += "\" uid=\"";
	AppendNumber(uid, tag);
	// (⛔`document="<name>"` stood here until 2026-09-22 - the user's call: a document's name can
	//  change, so it is not something to write down or to check against. The UID pairs the file with
	//  a story and the fingerprint says whether the file still matches what was exported; a rename
	//  moves neither. ⚠A .docx written before today still carries the attribute, and the reader
	//  simply does not look at it.)
	tag += "\" format=\"1\" fingerprint=\"";
	tag += fingerprint;
	tag += "\"/>";
	AddPart("customXml/item1.xml", tag, outParts);

	// The item's id is fixed: it names the KIND of part, and a fresh one per file would be the one
	// thing making two exports of a story differ.
	std::string props = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\r\n"
						"<ds:datastoreItem ds:itemID=\"{5B0F4C3A-1D2E-4F60-8A7B-9C0D1E2F3A4B}\" "
						"xmlns:ds=\"http://schemas.openxmlformats.org/officeDocument/2006/customXml\">"
						"<ds:schemaRefs><ds:schemaRef ds:uri=\"";
	props += kStoryTagNamespace;
	props += "\"/></ds:schemaRefs></ds:datastoreItem>";
	AddPart("customXml/itemProps1.xml", props, outParts);

	std::string itemRels = kXmlDeclaration;
	itemRels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
				"<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps\" Target=\"itemProps1.xml\"/>"
				"</Relationships>";
	AddPart("customXml/_rels/item1.xml.rels", itemRels, outParts);

	return kTrue;
}

bool16 Write(const KCMStoryShape::Story& s, int32 uid,
			 std::string& outDocx, std::string& whyNot)
{
	outDocx.clear();

	std::vector<KCMZipStore::Entry> parts;
	if (!WriteParts(s, uid, parts, whyNot))
		return kFalse;

	KCMZipStore::Write(parts, outDocx);
	return kTrue;
}

//========================================================================================
//  THE READING HALF
//========================================================================================

namespace
{

/** "269" -> 269. kFalse for anything that is not a plain decimal number. */
bool16 ParseDecimal(const std::string& text, int32& out)
{
	if (text.empty() || text.size() > 10)
		return kFalse;
	int32 value = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] < '0' || text[i] > '9')
			return kFalse;
		value = value * 10 + (text[i] - '0');
	}
	out = value;
	return kTrue;
}

}	// anonymous namespace

bool16 ReadTag(const std::string& customXmlPart, Tag& out, std::string& whyNot)
{
	out = Tag();
	whyNot.clear();

	KCMXmlTree tree;
	if (!tree.Parse(customXmlPart.data(), customXmlPart.size(), whyNot))
	{
		// ★NOT OURS, THEN - Word keeps other applications' parts, and one of those that is not even
		//   well-formed XML is not a reason to refuse the story (the re-check, R3). Ours is written
		//   well-formed and Word rewrites it well-formed.
		whyNot.clear();
		return kTrue;
	}
	const int32 root = tree.Root();
	if (root < 0 || !tree.Is(root, kStoryTagNamespace, "story"))
		return kTrue;			// somebody else's part: present kFalse, and nothing wrong

	out.fPresent = kTrue;

	const std::string* format = tree.Attr(root, "format");
	if (format == nil || !ParseDecimal(*format, out.fFormat))
	{
		whyNot = "the story tag names no format";
		return kFalse;
	}
	if (out.fFormat != 1)
	{
		whyNot = "the story tag is of format ";
		AppendNumber(out.fFormat, whyNot);
		whyNot += ", and this build reads format 1";
		return kFalse;
	}

	const std::string* uid = tree.Attr(root, "uid");
	if (uid == nil || !ParseDecimal(*uid, out.fUid) || out.fUid <= 0)
	{
		whyNot = "the story tag's uid is not a number";
		return kFalse;
	}

	// (⛔The "document" attribute was read here into out.fDocument until 2026-09-22. Nothing ever
	//  read the field, and the name it held can change under a Save As - so it is neither written
	//  nor read now. An older file's attribute is ignored rather than refused.)
	const std::string* fingerprint = tree.Attr(root, "fingerprint");
	if (fingerprint == nil || fingerprint->empty())
	{
		whyNot = "the story tag carries no fingerprint";
		return kFalse;
	}
	out.fFingerprint = *fingerprint;
	return kTrue;
}

//----------------------------------------------------------------------------------------
//  The story itself, on either side of the revision marks.
//
//  ★THE SAME WALK TWICE. One tree, one set of rules, and a Side that says which of <w:ins> and
//    <w:del> is taken and which <w:rPr> a changed run wears. Everything that can refuse refuses
//    the same way on both sides, so a file either reads on both or on neither.
//----------------------------------------------------------------------------------------

namespace
{

const char* const kW = KCMXmlTree::kWordNs;
const char* const kMc = KCMXmlTree::kMcNs;

/** What one character carries on the way in: the kenten VALUE (the reader answers values, not
	classes), and whether a tate-chu-yoko or a warichu covers it. ⚠Not the writer's Look: that one
	keeps span INDICES, and the reader has no spans yet - it makes them from runs that touch. */
struct RLook
{
	std::string	fKenten;
	bool16		fTcy;
	bool16		fWarichu;

	RLook() : fTcy(kFalse), fWarichu(kFalse) {}
};

/** A paragraph being built, in code points. */
struct Building
{
	std::string							fText;
	int32								fLen;			// code points in fText
	KCMAttrSpanList						fRuby;
	KCMAttrSpanList						fKenten;		// touching runs of one kind are one span
	KCMAttrSpanList						fTcy;			// fValue filled by Finish
	KCMAttrSpanList						fWarichu;
	std::vector<KCMStoryShape::NoteRef>	fNoteRefs;		// fNote holds the footnote ID until ResolveNotes ranks it
	std::vector<int32>					fEndnoteAt;		// ★where an ENDNOTE mark stood - never a character of fText
	int32								fMarkRevision;	// 0 none, +1 the paragraph mark was inserted, -1 deleted
	// the field being collected, if any
	int32								fFieldDepth;	// 0 none, 1 between begin and end
	bool16								fInResult;		// between separate and end
	std::string							fFieldCode;
	RLook								fFieldLook;		// the look of the run holding the begin

	Building() : fLen(0), fMarkRevision(0), fFieldDepth(0), fInResult(kFalse) {}
};

/** kTrue for a content control that was ours and is no longer read: the two table marks and the
	legend of stage 3b (kcm-continues / kcm-continued / kcm-legend). A file written that day still
	opens; what those controls said is decided by the document now (the note above AppendParagraph),
	so their contents are passed over rather than read as words. */
bool16 IsRetiredOwnControl(const KCMXmlTree& t, int32 sdt)
{
	const int32 pr = t.Child(sdt, kW, "sdtPr");
	const int32 tag = (pr >= 0) ? t.Child(pr, kW, "tag") : -1;
	const std::string* v = (tag >= 0) ? t.Attr(tag, "val") : nil;
	return (v != nil && v->size() > 4 && v->compare(0, 4, "kcm-") == 0) ? kTrue : kFalse;
}

typedef std::vector< std::pair<std::string, std::string> > StyleNames;	// styleId -> w:name

/** What the reader carries down the tree. */
struct Reader
{
	const KCMXmlTree*		fTree;
	Side					fSide;
	std::vector<Mark>*		fMarks;
	StyleNames				fStyleNames;
	KCMStoryShape::Story*	fStory;
	std::string				fWhy;

	Reader() : fTree(nil), fSide(kSideAfterWord), fMarks(nil), fStory(nil) {}
};

bool16 Refuse(Reader& rd, const std::string& why)
{
	rd.fWhy = why;
	return kFalse;
}

/** "w:drawing" - the name a refusal shows. The prefix is Word's own spelling for its namespace. */
std::string QName(const KCMXmlTree& t, int32 node)
{
	const KCMXmlNode& n = t.At(node);
	if (n.fNs == kW)	return "w:" + n.fName;
	if (n.fNs == kMc)	return "mc:" + n.fName;
	if (n.fNs.empty())	return n.fName;
	return "{" + n.fNs + "}" + n.fName;
}

bool16 IsBlank(const std::string& s)
{
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n')
			return kFalse;
	}
	return kTrue;
}

/** A w:vert / w:combine value: "1", "true" and "on" mean on; absent or anything else means off. */
bool16 IsOn(const std::string* v)
{
	if (v == nil)
		return kFalse;
	return (*v == "1" || *v == "true" || *v == "on") ? kTrue : kFalse;
}

/** The first word of a field code, for a message: " PAGE \* MERGEFORMAT" -> "PAGE". */
std::string FirstWord(const std::string& code)
{
	size_t i = 0;
	while (i < code.size() && (code[i] == ' ' || code[i] == '\t'))
		++i;
	size_t j = i;
	while (j < code.size() && code[j] != ' ' && code[j] != '\t')
		++j;
	return code.substr(i, j - i);
}

void NoteMark(Reader& rd, int32 node)
{
	if (rd.fMarks == nil)
		return;
	Mark m;
	const std::string* author = rd.fTree->Attr(node, "author");
	const std::string* date = rd.fTree->Attr(node, "date");
	if (author != nil)	m.fAuthor = *author;
	if (date != nil)	m.fDate = *date;
	rd.fMarks->push_back(m);
}

/** The w:name a styleId stands for, or the id itself when styles.xml did not say. ★THE NAME
	CARRIES THE KIND: Word makes ids of its own for a style typed by hand ("kentenBlackTriangle"
	for a style named kenten-BlackTriangle), and the name is what the person typed. */
std::string StyleName(const Reader& rd, const std::string& id)
{
	for (size_t k = 0; k < rd.fStyleNames.size(); ++k)
	{
		if (rd.fStyleNames[k].first == id)
			return rd.fStyleNames[k].second;
	}
	return id;
}

void CollectStyleNames(const KCMXmlTree& styles, StyleNames& out)
{
	const int32 root = styles.Root();
	if (root < 0 || !styles.Is(root, kW, "styles"))
		return;
	const KCMXmlNode& n = styles.At(root);
	for (size_t k = 0; k < n.fChildren.size(); ++k)
	{
		const int32 style = n.fChildren[k];
		if (!styles.Is(style, kW, "style"))
			continue;
		const std::string* type = styles.Attr(style, "type");
		const std::string* id = styles.Attr(style, "styleId");
		if (type == nil || *type != "character" || id == nil)
			continue;
		const int32 name = styles.Child(style, kW, "name");
		const std::string* val = (name >= 0) ? styles.Attr(name, "val") : nil;
		if (val != nil)
			out.push_back(std::make_pair(*id, *val));
	}
}

/** The one <w:rPr> a run wears on the side being read, and what it says. */
bool16 LookOf(Reader& rd, int32 rPr, RLook& out)
{
	out = RLook();
	if (rPr < 0)
		return kTrue;
	const KCMXmlTree& t = *rd.fTree;

	int32 use = rPr;
	const int32 change = t.Child(rPr, kW, "rPrChange");
	if (change >= 0)
	{
		NoteMark(rd, change);
		if (rd.fSide == kSideOriginAsWritten)
		{
			// ★THE CHANGE RECORD HOLDS THE PROPERTIES AS THEY WERE (measured 2026-09-19): a kenten
			//   style taken off in Word leaves <w:rPrChange><w:rPr><w:rStyle .../></w:rPr></w:rPrChange>,
			//   and one put on leaves an empty <w:rPr/> in there.
			use = t.Child(change, kW, "rPr");
			if (use < 0)
				return kTrue;
		}
	}

	const int32 style = t.Child(use, kW, "rStyle");
	if (style >= 0)
	{
		const std::string* id = t.Attr(style, "val");
		if (id != nil)
		{
			const std::string name = StyleName(rd, *id);
			if (name.size() > kKentenStylePrefixLen
				&& name.compare(0, kKentenStylePrefixLen, kKentenStylePrefix) == 0)
			{
				if (!KCMStoryShape::KentenValueOfClass(name.substr(kKentenStylePrefixLen), out.fKenten))
					return Refuse(rd, "a kenten style this reader cannot read: " + name);
			}
			// ⚠★★★**A .docx WRITTEN BEFORE 2026-09-22 SAYS "kenten-"**, and it is turned away BY NAME.
			//   The kenten styles were renamed into Japanese that day and the old spelling is not
			//   read (the user chose the clean switch). Without this the old name would be just
			//   another character style nobody here cares about, and the mark would go SILENTLY -
			//   which is the one thing no reader of this round trip is allowed to do. Saying it
			//   costs a "!" row and tells the reader exactly what happened to their file.
			else if (name.size() > 7 && name.compare(0, 7, "kenten-") == 0)
			{
				return Refuse(rd, "a kenten style from an older Kohaku Change Marker, whose styles "
								  "were named in English: " + name + " (export the stories again to "
								  "get a file this build can read)");
			}
		}
	}
	if (out.fKenten.empty())
	{
		// Word's own emphasis mark, put on from its UI: the nearest of InDesign's kinds (the design,
		// section 4-1).
		// ★★**THE THREE LINES ARE THE USER'S, AS OF 2026-09-22** - they were written "still to be
		//   confirmed" and now are: 、 is InDesign's default (the sesame mark), ・ is the black
		//   circle, ○ the white one. ⚠**THIS TABLE ONLY DECIDES WHAT THE BUTTON MEANS.** A person
		//   who picks a style from Word's gallery instead - which is how all eleven kinds, and a
		//   custom mark, are applied there - never comes through here: the style's NAME says which
		//   kind it is and nothing is guessed. ⚠underDot is InDesign's black circle too: the
		//   position it asks for is a LOOK, and only the kind travels (the user's call the same
		//   day - "種類だけで良いです").
		const int32 em = t.Child(use, kW, "em");
		const std::string* v = (em >= 0) ? t.Attr(em, "val") : nil;
		if (v != nil)
		{
			if (*v == "comma")							out.fKenten = KCMStoryShape::kKentenDefaultValue;
			else if (*v == "dot" || *v == "underDot")	out.fKenten = "BlackCircle";
			else if (*v == "circle")					out.fKenten = "WhiteCircle";
		}
	}
	const int32 layout = t.Child(use, kW, "eastAsianLayout");
	if (layout >= 0)
	{
		out.fTcy = IsOn(t.Attr(layout, "vert"));
		out.fWarichu = IsOn(t.Attr(layout, "combine"));
	}
	return kTrue;
}

/** Grow the last span when it reaches exactly here and (when asked) means the same, else start one. */
void Extend(KCMAttrSpanList& spans, int32 at, const std::string& value, bool16 compareValue)
{
	if (!spans.empty() && spans.back().fStart + spans.back().fLen == at
		&& (!compareValue || spans.back().fValue == value))
	{
		++spans.back().fLen;
		return;
	}
	spans.push_back(KCMAttrSpan(at, 1, value, kFalse));
}

void Put(Building& b, int32 cp, const RLook& look)
{
	KCMParaText::AppendUtf8(b.fText, cp);
	if (!look.fKenten.empty())
		Extend(b.fKenten, b.fLen, look.fKenten, kTrue);
	if (look.fTcy)
		Extend(b.fTcy, b.fLen, "-", kFalse);		// the value is the text it covers: Finish fills it
	if (look.fWarichu)
		Extend(b.fWarichu, b.fLen, "-", kFalse);
	++b.fLen;
}

void PutText(Building& b, const std::string& utf8, const RLook& look)
{
	std::vector<int32> cps;
	KCMTextDiff::ToCodePoints(utf8, &cps, nil);
	for (size_t i = 0; i < cps.size(); ++i)
		Put(b, cps[i], look);
}

/** A <w:sdt> whose tag is "uXXXX": the invisible character it stands for. */
bool16 PlaceholderOf(const KCMXmlTree& t, int32 sdt, int32& outCp)
{
	const int32 pr = t.Child(sdt, kW, "sdtPr");
	const int32 tag = (pr >= 0) ? t.Child(pr, kW, "tag") : -1;
	const std::string* v = (tag >= 0) ? t.Attr(tag, "val") : nil;
	if (v == nil || v->size() < 5 || (*v)[0] != 'u')
		return kFalse;
	int32 cp = 0;
	for (size_t i = 1; i < v->size(); ++i)
	{
		const char c = (*v)[i];
		int32 d = -1;
		if (c >= '0' && c <= '9')		d = c - '0';
		else if (c >= 'a' && c <= 'f')	d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')	d = c - 'A' + 10;
		if (d < 0)
			return kFalse;
		cp = cp * 16 + d;
		if (cp > 0x10FFFF)
			return kFalse;
	}
	if (cp <= 0)
		return kFalse;
	outCp = cp;
	return kTrue;
}

/*	ParseEqRuby
	Word's OTHER spelling of a ruby (measured 2026-09-19, Word 2007, from the Phonetic Guide):
	    EQ \* jc2 \* "Font:MS Mincho" \* hps10 \o\ad(\s\up 9(READING),BASE)
	The \o's arguments are split at the commas standing at depth 0; the first holds the reading
	inside the parentheses after \s; the second is the base text itself. A backslash in front of
	( ) , or \ makes that character literal.
*/
bool16 ParseEqRuby(const std::string& code, std::string& outReading, std::string& outBase)
{
	size_t i = 0;
	while (i < code.size() && (code[i] == ' ' || code[i] == '\t'))
		++i;
	if (i + 2 > code.size() || (code[i] != 'E' && code[i] != 'e') || (code[i + 1] != 'Q' && code[i + 1] != 'q'))
		return kFalse;
	if (i + 2 < code.size() && code[i + 2] != ' ' && code[i + 2] != '\t')
		return kFalse;

	const size_t o = code.find("\\o", i + 2);
	if (o == std::string::npos)
		return kFalse;
	const size_t open = code.find('(', o);
	if (open == std::string::npos)
		return kFalse;

	std::vector<std::string> args(1);
	int32 depth = 0;
	bool16 closed = kFalse;
	for (size_t k = open + 1; k < code.size(); ++k)
	{
		const char c = code[k];
		if (c == '\\' && k + 1 < code.size()
			&& (code[k + 1] == '(' || code[k + 1] == ')' || code[k + 1] == ',' || code[k + 1] == '\\'))
		{
			args.back() += code[k + 1];
			++k;
			continue;
		}
		if (c == '(')
		{
			++depth;
			args.back() += c;
			continue;
		}
		if (c == ')')
		{
			if (depth == 0)
			{
				closed = kTrue;
				break;
			}
			--depth;
			args.back() += c;
			continue;
		}
		if (c == ',' && depth == 0)
		{
			args.push_back(std::string());
			continue;
		}
		args.back() += c;
	}
	if (!closed || args.size() != 2)
		return kFalse;

	const std::string& a = args[0];
	const size_t s = a.find("\\s");
	if (s == std::string::npos)
		return kFalse;
	const size_t ro = a.find('(', s);
	const size_t rc = a.rfind(')');
	if (ro == std::string::npos || rc == std::string::npos || rc <= ro)
		return kFalse;
	outReading = a.substr(ro + 1, rc - ro - 1);
	outBase = args[1];
	return (!outReading.empty() && !outBase.empty()) ? kTrue : kFalse;
}

/** The field code, once its end is met: an EQ ruby is put as text with a reading over it; any
	other field is refused - its result is Word's rendering, not anybody's words. */
bool16 SettleField(Reader& rd, Building& b)
{
	std::string reading, base;
	if (!ParseEqRuby(b.fFieldCode, reading, base))
	{
		const std::string first = FirstWord(b.fFieldCode);
		if (first == "EQ" || first == "eq")
			return Refuse(rd, "a field this reader cannot read: " + b.fFieldCode.substr(0, 60));
		return Refuse(rd, "a field, which is not text: " + first);
	}
	const int32 start = b.fLen;
	PutText(b, base, b.fFieldLook);
	const int32 len = b.fLen - start;
	b.fRuby.push_back(KCMAttrSpan(start, len, reading, (len > 1) ? kTrue : kFalse));
	b.fFieldCode.clear();
	return kTrue;
}

bool16 ReadContent(Reader& rd, int32 node, Building& b);

/** The characters of a <w:rt>, on this side: runs of text, and nothing that is not text - a tab or
	a break in a reading would otherwise be dropped without a word (found by the re-check, R1). */
bool16 ReadingText(Reader& rd, int32 node, std::string& out)
{
	const KCMXmlTree& t = *rd.fTree;
	const KCMXmlNode& n = t.At(node);
	for (size_t k = 0; k < n.fChildren.size(); ++k)
	{
		const int32 c = n.fChildren[k];
		const KCMXmlNode& cn = t.At(c);
		if (cn.IsText())
		{
			if (!IsBlank(cn.fText))
				return Refuse(rd, "text stands outside a run");
			continue;
		}
		if (cn.fNs != kW)
			return Refuse(rd, "a reading holds something that is not text: " + QName(t, c));
		const std::string& name = cn.fName;
		if (name == "r" || name == "hyperlink" || name == "smartTag" || name == "customXml")
		{
			if (!ReadingText(rd, c, out))
				return kFalse;
		}
		else if (name == "ins" || name == "moveTo")
		{
			if (rd.fSide == kSideAfterWord && !ReadingText(rd, c, out))
				return kFalse;
		}
		else if (name == "del" || name == "moveFrom")
		{
			if (rd.fSide == kSideOriginAsWritten && !ReadingText(rd, c, out))
				return kFalse;
		}
		else if (name == "t" || name == "delText")
		{
			out += t.TextBelow(c);
		}
		else if (name == "rPr" || name == "smartTagPr" || name == "customXmlPr" || name == "proofErr"
				 || name == "bookmarkStart" || name == "bookmarkEnd" || name == "lastRenderedPageBreak")
		{
			continue;
		}
		else
		{
			return Refuse(rd, "a reading holds something that is not text: " + QName(t, c));
		}
	}
	return kTrue;
}

bool16 ReadRuby(Reader& rd, int32 ruby, Building& b)
{
	const KCMXmlTree& t = *rd.fTree;
	const int32 rt = t.Child(ruby, kW, "rt");
	const int32 base = t.Child(ruby, kW, "rubyBase");
	if (rt < 0 || base < 0)
		return Refuse(rd, "a ruby without a reading or a base");
	std::string reading;
	if (!ReadingText(rd, rt, reading))
		return kFalse;
	const int32 start = b.fLen;
	if (!ReadContent(rd, base, b))
		return kFalse;
	const int32 len = b.fLen - start;
	if (len <= 0)
		return kTrue;			// a reading over nothing is no reading (the HTML rule)
	// ★ONE READING OVER SEVERAL CHARACTERS IS GROUP, over one it is MONO: the way the elements are
	//   cut IS the setting (the header), and this is the only answer a <w:ruby> can give.
	b.fRuby.push_back(KCMAttrSpan(start, len, reading, (len > 1) ? kTrue : kFalse));
	return kTrue;
}

bool16 ReadRunChildren(Reader& rd, int32 node, const RLook& look, Building& b);

bool16 ReadRun(Reader& rd, int32 r, Building& b)
{
	RLook look;
	if (!LookOf(rd, rd.fTree->Child(r, kW, "rPr"), look))
		return kFalse;
	return ReadRunChildren(rd, r, look, b);
}

/** The children of a <w:r> - or of an <mc:Fallback> standing inside one, which holds the same
	kinds of thing (the re-check, R5: a newer Word wraps what it draws in AlternateContent, and the
	fallback is where the older spelling is). */
bool16 ReadRunChildren(Reader& rd, int32 node, const RLook& look, Building& b)
{
	const KCMXmlTree& t = *rd.fTree;
	const KCMXmlNode& n = t.At(node);
	for (size_t k = 0; k < n.fChildren.size(); ++k)
	{
		const int32 c = n.fChildren[k];
		const KCMXmlNode& cn = t.At(c);
		if (cn.IsText())
		{
			if (!IsBlank(cn.fText))
				return Refuse(rd, "text stands outside a run");
			continue;
		}
		if (cn.fNs == kMc && cn.fName == "AlternateContent")
		{
			const int32 fallback = t.Child(c, kMc, "Fallback");
			if (fallback >= 0 && !ReadRunChildren(rd, fallback, look, b))
				return kFalse;
			continue;
		}
		if (cn.fNs != kW)
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));

		const std::string& name = cn.fName;
		if (name == "rPr")
			continue;

		// ---- inside a field: the code is collected, the result is not read -----------------------
		if (b.fFieldDepth > 0 && name != "fldChar")
		{
			if (name == "instrText" || name == "delInstrText")
			{
				if (!b.fInResult)
					b.fFieldCode += t.TextBelow(c);
				continue;
			}
			if (b.fInResult)
				continue;				// Word's rendering of the field, not text
			return Refuse(rd, "text inside a field's code");
		}

		if (name == "t" || name == "delText")
		{
			PutText(b, t.TextBelow(c), look);
		}
		else if (name == "tab")
		{
			Put(b, 0x0009, look);
		}
		else if (name == "br")
		{
			const std::string* type = t.Attr(c, "type");
			if (type != nil && (*type == "page" || *type == "column"))
				return Refuse(rd, "a page or column break, which is not a character");
			Put(b, 0x000A, look);
		}
		else if (name == "cr")
		{
			Put(b, 0x000A, look);
		}
		else if (name == "noBreakHyphen")
		{
			Put(b, 0x2011, look);
		}
		else if (name == "softHyphen")
		{
			Put(b, 0x00AD, look);
		}
		else if (name == "footnoteReference")
		{
			const std::string* id = t.Attr(c, "id");
			int32 value = 0;
			if (id == nil || !ParseDecimal(*id, value))
				return Refuse(rd, "a footnote reference with no id");
			KCMStoryShape::NoteRef ref;
			ref.fAt = b.fLen;
			ref.fNote = value;
			b.fNoteRefs.push_back(ref);
		}
		else if (name == "ruby")
		{
			if (!ReadRuby(rd, c, b))
				return kFalse;
		}
		else if (name == "fldChar")
		{
			const std::string* type = t.Attr(c, "fldCharType");
			const std::string kind = (type != nil) ? *type : std::string();
			if (kind == "begin")
			{
				if (b.fFieldDepth > 0)
					return Refuse(rd, "a field inside a field");
				b.fFieldDepth = 1;
				b.fInResult = kFalse;
				b.fFieldCode.clear();
				b.fFieldLook = look;
			}
			else if (kind == "separate")
			{
				b.fInResult = kTrue;
			}
			else if (kind == "end")
			{
				if (b.fFieldDepth == 0)
					return Refuse(rd, "a field that ends without having begun");
				b.fFieldDepth = 0;
				b.fInResult = kFalse;
				if (!SettleField(rd, b))
					return kFalse;
			}
			else
			{
				return Refuse(rd, "a field character of a kind this reader does not know: " + kind);
			}
		}
		else if (name == "footnoteRef" || name == "separator" || name == "continuationSeparator"
				 || name == "lastRenderedPageBreak")
		{
			continue;
		}
		else
		{
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));
		}
	}
	return kTrue;
}

/** The contents of a <w:p>, or of anything standing inside one that holds runs. */
bool16 ReadContent(Reader& rd, int32 node, Building& b)
{
	const KCMXmlTree& t = *rd.fTree;
	const KCMXmlNode& n = t.At(node);
	for (size_t k = 0; k < n.fChildren.size(); ++k)
	{
		const int32 c = n.fChildren[k];
		const KCMXmlNode& cn = t.At(c);
		if (cn.IsText())
		{
			if (!IsBlank(cn.fText))
				return Refuse(rd, "text stands outside a run");
			continue;
		}
		if (cn.fNs == kMc)
		{
			if (cn.fName == "AlternateContent")
			{
				const int32 fallback = t.Child(c, kMc, "Fallback");
				if (fallback >= 0 && !ReadContent(rd, fallback, b))
					return kFalse;
				continue;
			}
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));
		}
		if (cn.fNs != kW)
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));

		const std::string& name = cn.fName;
		if (name == "pPr")
		{
			continue;					// read by ReadParagraph
		}
		else if (name == "r")
		{
			if (!ReadRun(rd, c, b))
				return kFalse;
		}
		else if (name == "ins" || name == "moveTo")
		{
			NoteMark(rd, c);
			if (rd.fSide == kSideAfterWord && !ReadContent(rd, c, b))
				return kFalse;
		}
		else if (name == "del" || name == "moveFrom")
		{
			NoteMark(rd, c);
			if (rd.fSide == kSideOriginAsWritten && !ReadContent(rd, c, b))
				return kFalse;
		}
		else if (name == "sdt")
		{
			// a control of ours from stage 3b (a table mark, the legend): passed over, never words
			if (IsRetiredOwnControl(t, c))
				continue;
			int32 cp = 0;
			if (PlaceholderOf(t, c, cp))
			{
				// ★THE TAG IS THE TRUTH; the run inside is decoration Word re-cuts as it likes. Its
				//   look, though, is the character's own (the writer puts it there).
				RLook look;
				const int32 content = t.Child(c, kW, "sdtContent");
				const int32 run = (content >= 0) ? t.Child(content, kW, "r") : -1;
				if (run >= 0 && !LookOf(rd, t.Child(run, kW, "rPr"), look))
					return kFalse;
				// ★★★**AN ENDNOTE MARK IS A PLACE, NOT A CHARACTER** (2026-09-23). U+0005 is never a
				//   character of a story's text on this road - KCMTextRead takes it out of the
				//   document's side too - so putting one in here would make the file disagree with
				//   the document about every offset after it, for a character neither side holds.
				if (cp == 0x0005)
				{
					b.fEndnoteAt.push_back(b.fLen);
					continue;
				}
				Put(b, cp, look);
				continue;
			}
			const int32 content = t.Child(c, kW, "sdtContent");
			if (content >= 0 && !ReadContent(rd, content, b))
				return kFalse;
		}
		else if (name == "hyperlink" || name == "smartTag" || name == "customXml" || name == "dir" || name == "bdo")
		{
			if (!ReadContent(rd, c, b))
				return kFalse;
		}
		else if (name == "bookmarkStart" || name == "bookmarkEnd" || name == "proofErr"
				 || name == "commentRangeStart" || name == "commentRangeEnd"
				 || name == "permStart" || name == "permEnd"
				 || name == "moveFromRangeStart" || name == "moveFromRangeEnd"
				 || name == "moveToRangeStart" || name == "moveToRangeEnd"
				 || name == "smartTagPr" || name == "customXmlPr")		// the properties of what is seen through (R4)
		{
			continue;
		}
		else if (name == "fldSimple")
		{
			const std::string* instr = t.Attr(c, "instr");
			return Refuse(rd, "a field, which is not text: " + FirstWord(instr != nil ? *instr : std::string()));
		}
		else
		{
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));
		}
	}
	return kTrue;
}

bool16 ReadParagraph(Reader& rd, int32 p, Building& b)
{
	const KCMXmlTree& t = *rd.fTree;
	b = Building();

	const int32 pPr = t.Child(p, kW, "pPr");
	if (pPr >= 0)
	{
		// (w:pStyle is not read: the style slot is left for the day it carries InDesign's paragraph
		//  style name - the design, 11-4)
		if (t.Child(pPr, kW, "numPr") >= 0)
			return Refuse(rd, "an automatic number: the number is not a character, so it would be lost");
		if (t.Child(pPr, kW, "sectPr") >= 0)
			return Refuse(rd, "a section break inside the text");

		// ★THE PARAGRAPH MARK'S OWN REVISION (measured 2026-09-19): a paragraph split in Word puts
		//   <w:ins> on the FIRST half's mark; two joined put <w:del> on the first one's. The <w:p>
		//   elements stay as they are either way; joining is the reader's job (ReadBlocks).
		const int32 rPr = t.Child(pPr, kW, "rPr");
		if (rPr >= 0)
		{
			const int32 ins = t.Child(rPr, kW, "ins");
			const int32 del = t.Child(rPr, kW, "del");
			if (ins >= 0)
			{
				NoteMark(rd, ins);
				b.fMarkRevision = 1;
			}
			if (del >= 0)
			{
				NoteMark(rd, del);
				b.fMarkRevision = -1;
			}
		}
	}

	if (!ReadContent(rd, p, b))
		return kFalse;
	if (b.fFieldDepth > 0)
		return Refuse(rd, "a field runs past the end of its paragraph");
	return kTrue;
}

void Finish(Building& b, KCMStoryShape::Para& out)
{
	out = KCMStoryShape::Para();
	out.fText = b.fText;
	out.fRuby = b.fRuby;
	out.fKenten = b.fKenten;
	out.fTcy = b.fTcy;
	out.fWarichu = b.fWarichu;
	KCMParaText::SetSpanValuesToText(out.fTcy, out.fText);
	KCMParaText::SetSpanValuesToText(out.fWarichu, out.fText);
	out.fNoteRefs = b.fNoteRefs;
	out.fEndnoteAt = b.fEndnoteAt;
}

int32 CodePointsIn(const std::string& utf8)
{
	std::vector<int32> byteAt;
	KCMTextDiff::ToCodePoints(utf8, nil, &byteAt);
	return static_cast<int32>(byteAt.size());
}

/** Spans of `more`, moved right by `shift`, onto `into` - a span that reaches the one before it
	becomes part of it when `join` (and, when `compareValue`, only if it means the same). */
void AppendShifted(KCMAttrSpanList& into, const KCMAttrSpanList& more, int32 shift, bool16 join,
				   bool16 compareValue)
{
	for (size_t k = 0; k < more.size(); ++k)
	{
		KCMAttrSpan s = more[k];
		s.fStart += shift;
		if (join && !into.empty() && into.back().fStart + into.back().fLen == s.fStart
			&& (!compareValue || into.back().fValue == s.fValue))
		{
			into.back().fLen += s.fLen;
			continue;
		}
		into.push_back(s);
	}
}

/*	JoinOnto
	`next` is the rest of `prev`: the half after a table, or the paragraph after a mark that was
	inserted or deleted in Word.

	★KENTEN, TATE-CHU-YOKO AND WARICHU CUT BY A TABLE ARE ONE SPAN AGAIN; A RUBY IS NOT. The writer
	  cuts every span at a table's place, and the first three are runs of an attribute, which the
	  document reports as one span across the cut. A ruby cut in two is two readings on the page
	  and comes back as two - which is how the export's own check refuses such a story (the
	  writer's Slice says so).
*/
void JoinOnto(KCMStoryShape::Para& prev, const KCMStoryShape::Para& next)
{
	const int32 shift = CodePointsIn(prev.fText);
	prev.fText += next.fText;
	AppendShifted(prev.fRuby, next.fRuby, shift, kFalse, kFalse);
	AppendShifted(prev.fKenten, next.fKenten, shift, kTrue, kTrue);
	AppendShifted(prev.fTcy, next.fTcy, shift, kTrue, kFalse);
	AppendShifted(prev.fWarichu, next.fWarichu, shift, kTrue, kFalse);
	KCMParaText::SetSpanValuesToText(prev.fTcy, prev.fText);
	KCMParaText::SetSpanValuesToText(prev.fWarichu, prev.fText);
	for (size_t k = 0; k < next.fNoteRefs.size(); ++k)
	{
		KCMStoryShape::NoteRef ref = next.fNoteRefs[k];
		ref.fAt += shift;
		prev.fNoteRefs.push_back(ref);
	}
}

/** Where a run of blocks stands: the body, a cell of a table, or a footnote. */
const int32 kInBody = -1;
const int32 kInNote = -2;

bool16 ReadBlocks(Reader& rd, int32 container, int32 inTable, int32 inRow, int32 inCell,
				  std::vector<KCMStoryShape::Para>& out);

/*	The table, read back.

	★THE GRID IS WALKED THE WAY THE WRITER LAID IT OUT (LayRowOut): a cell's grid column is the
	  sum of the spans before it in its row, and a <w:vMerge> with no "restart" is a COVERED cell -
	  it is not a cell of the Story's, it makes the anchor above it, in the same grid column, one
	  row taller. The anchor open in each grid column is remembered as (row, cell) into the table
	  being built, which is why a row is pushed before the next one is read.
	★A ROW INSERTED IN WORD IS THE AFTER SIDE'S ONLY, ONE DELETED IS THE ORIGIN'S ONLY (<w:trPr>'s
	  <w:ins> / <w:del>); a cell inserted, deleted or merged in Word (cellIns, cellDel, cellMerge)
	  is refused - the grid can no longer be told.
*/
struct GridOpen
{
	std::vector<int32>	fRow;		// per grid column: the row of the anchor reaching down, or -1
	std::vector<int32>	fCell;		// and which cell of that row
};

bool16 ReadCells(Reader& rd, int32 container, int32 slot, int32 rowIndex, KCMStoryShape::Row& row,
				 int32& col, GridOpen& open);

bool16 ReadCell(Reader& rd, int32 tc, int32 slot, int32 rowIndex, KCMStoryShape::Row& row, int32& col,
				GridOpen& open)
{
	const KCMXmlTree& t = *rd.fTree;
	KCMStoryShape::Story& s = *rd.fStory;

	int32 span = 1;
	bool16 restarts = kFalse;
	bool16 covered = kFalse;
	const int32 tcPr = t.Child(tc, kW, "tcPr");
	if (tcPr >= 0)
	{
		const int32 gridSpan = t.Child(tcPr, kW, "gridSpan");
		const std::string* gs = (gridSpan >= 0) ? t.Attr(gridSpan, "val") : nil;
		if (gs != nil && (!ParseDecimal(*gs, span) || span < 1))
			return Refuse(rd, "a cell whose grid span cannot be read: " + *gs);
		const int32 vMerge = t.Child(tcPr, kW, "vMerge");
		if (vMerge >= 0)
		{
			const std::string* v = t.Attr(vMerge, "val");
			if (v != nil && *v == "restart")
				restarts = kTrue;
			else
				covered = kTrue;
		}
		if (t.Child(tcPr, kW, "cellIns") >= 0 || t.Child(tcPr, kW, "cellDel") >= 0
			|| t.Child(tcPr, kW, "cellMerge") >= 0)
			return Refuse(rd, "a table cell was inserted, deleted or merged in Word");
	}

	if (open.fRow.size() < static_cast<size_t>(col + span))
	{
		open.fRow.resize(static_cast<size_t>(col + span), -1);
		open.fCell.resize(static_cast<size_t>(col + span), -1);
	}

	if (covered)
	{
		const int32 anchorRow = open.fRow[static_cast<size_t>(col)];
		const int32 anchorCell = open.fCell[static_cast<size_t>(col)];
		if (anchorRow >= 0)
		{
			// The anchor above grows by this row; the covered cell itself is nothing of the Story's.
			++s.fTables[static_cast<size_t>(slot)].fRows[static_cast<size_t>(anchorRow)]
				.fCells[static_cast<size_t>(anchorCell)].fRowSpan;
			col += span;
			return kTrue;
		}
		// A "continue" with nothing open above it (the row above is the other side's): a plain cell.
	}

	KCMStoryShape::Cell cell;
	cell.fColSpan = span;
	const int32 cellIndex = static_cast<int32>(row.fCells.size());
	for (int32 c = col; c < col + span; ++c)
	{
		open.fRow[static_cast<size_t>(c)] = restarts ? rowIndex : -1;
		open.fCell[static_cast<size_t>(c)] = restarts ? cellIndex : -1;
	}

	if (!ReadBlocks(rd, tc, slot, rowIndex, cellIndex, cell.fParas))
		return kFalse;
	row.fCells.push_back(cell);
	col += span;
	return kTrue;
}

bool16 ReadCells(Reader& rd, int32 container, int32 slot, int32 rowIndex, KCMStoryShape::Row& row,
				 int32& col, GridOpen& open)
{
	const KCMXmlTree& t = *rd.fTree;
	const KCMXmlNode& n = t.At(container);
	for (size_t k = 0; k < n.fChildren.size(); ++k)
	{
		const int32 c = n.fChildren[k];
		const KCMXmlNode& cn = t.At(c);
		if (cn.IsText())
		{
			if (!IsBlank(cn.fText))
				return Refuse(rd, "text stands outside a paragraph");
			continue;
		}
		if (cn.fNs != kW)
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));
		const std::string& name = cn.fName;
		if (name == "tc")
		{
			if (!ReadCell(rd, c, slot, rowIndex, row, col, open))
				return kFalse;
		}
		else if (name == "sdt" || name == "customXml")
		{
			const int32 content = (name == "sdt") ? t.Child(c, kW, "sdtContent") : c;
			if (content >= 0 && !ReadCells(rd, content, slot, rowIndex, row, col, open))
				return kFalse;
		}
		else if (name == "trPr" || name == "tblPrEx" || name == "bookmarkStart" || name == "bookmarkEnd"
				 || name == "proofErr" || name == "customXmlPr")
		{
			continue;
		}
		else
		{
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));
		}
	}
	return kTrue;
}

bool16 ReadRow(Reader& rd, int32 tr, int32 slot, GridOpen& open)
{
	const KCMXmlTree& t = *rd.fTree;
	KCMStoryShape::Story& s = *rd.fStory;

	KCMStoryShape::Row row;
	const int32 trPr = t.Child(tr, kW, "trPr");
	if (trPr >= 0)
	{
		const int32 header = t.Child(trPr, kW, "tblHeader");
		if (header >= 0)
		{
			const std::string* v = t.Attr(header, "val");
			row.fHeader = (v == nil || (*v != "0" && *v != "false" && *v != "off")) ? kTrue : kFalse;
		}
		const int32 ins = t.Child(trPr, kW, "ins");
		const int32 del = t.Child(trPr, kW, "del");
		if (ins >= 0)
		{
			NoteMark(rd, ins);
			if (rd.fSide == kSideOriginAsWritten)
				return kTrue;			// a row Word added: not the origin's
		}
		if (del >= 0)
		{
			NoteMark(rd, del);
			if (rd.fSide == kSideAfterWord)
				return kTrue;			// a row Word took out: not the after side's
		}
	}

	const int32 rowIndex = static_cast<int32>(s.fTables[static_cast<size_t>(slot)].fRows.size());
	int32 col = 0;
	if (!ReadCells(rd, tr, slot, rowIndex, row, col, open))
		return kFalse;
	s.fTables[static_cast<size_t>(slot)].fRows.push_back(row);
	return kTrue;
}

bool16 ReadRows(Reader& rd, int32 container, int32 slot, GridOpen& open)
{
	const KCMXmlTree& t = *rd.fTree;
	const KCMXmlNode& n = t.At(container);
	for (size_t k = 0; k < n.fChildren.size(); ++k)
	{
		const int32 c = n.fChildren[k];
		const KCMXmlNode& cn = t.At(c);
		if (cn.IsText())
		{
			if (!IsBlank(cn.fText))
				return Refuse(rd, "text stands outside a paragraph");
			continue;
		}
		if (cn.fNs != kW)
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));
		const std::string& name = cn.fName;
		if (name == "tr")
		{
			if (!ReadRow(rd, c, slot, open))
				return kFalse;
		}
		else if (name == "sdt" || name == "customXml")
		{
			const int32 content = (name == "sdt") ? t.Child(c, kW, "sdtContent") : c;
			if (content >= 0 && !ReadRows(rd, content, slot, open))
				return kFalse;
		}
		else if (name == "tblPr" || name == "tblGrid" || name == "bookmarkStart" || name == "bookmarkEnd"
				 || name == "proofErr" || name == "customXmlPr")
		{
			continue;
		}
		else
		{
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));
		}
	}
	return kTrue;
}

/** One <w:tbl>, standing in paragraph `paraIndex` of its holder at `offset` code points. */
bool16 ReadTable(Reader& rd, int32 tbl, int32 inTable, int32 inRow, int32 inCell, int32 paraIndex, int32 offset)
{
	KCMStoryShape::Story& s = *rd.fStory;

	// The slot is taken at the opening tag, so the tables inside this one come after it - the
	// document order Story::fTables promises (and the writer's AppendTable walks).
	const int32 slot = static_cast<int32>(s.fTables.size());
	s.fTables.push_back(KCMStoryShape::Table());
	{
		KCMStoryShape::Table& table = s.fTables[static_cast<size_t>(slot)];
		table.fOrdinal = slot;
		table.fSplitsPara = kTrue;
		table.fParaIndex = paraIndex;
		table.fOffset = offset;
		table.fInTable = inTable;
		table.fInRow = inRow;
		table.fInCell = inCell;
	}
	GridOpen open;
	return ReadRows(rd, tbl, slot, open);
}

/*	ReadBlocks
	The children of a <w:body>, a <w:tc> or a <w:footnote>: paragraphs, and tables among them, read
	in THE SPLIT SHAPE (the note above AppendParagraph): a table is a paragraph of its own holding
	nothing else, and the paragraph after it is a paragraph of its own. Which paragraph of the
	document a table belongs to is not this reader's to say (RejoinTables, at the import).

	★THE PARAGRAPH AFTER A MARK THAT THIS SIDE TREATS AS GONE JOINS THE ONE BEFORE IT (inserted, on
	  the origin side; deleted, on the after side) - a paragraph split or joined in Word.
*/
bool16 ReadBlocks(Reader& rd, int32 container, int32 inTable, int32 inRow, int32 inCell,
				  std::vector<KCMStoryShape::Para>& out)
{
	const KCMXmlTree& t = *rd.fTree;
	const KCMXmlNode& n = t.At(container);
	bool16 pendingJoin = kFalse;

	for (size_t k = 0; k < n.fChildren.size(); ++k)
	{
		const int32 c = n.fChildren[k];
		const KCMXmlNode& cn = t.At(c);
		if (cn.IsText())
		{
			if (!IsBlank(cn.fText))
				return Refuse(rd, "text stands outside a paragraph");
			continue;
		}
		if (cn.fNs == kMc && cn.fName == "AlternateContent")
		{
			const int32 fallback = t.Child(c, kMc, "Fallback");
			if (fallback >= 0 && !ReadBlocks(rd, fallback, inTable, inRow, inCell, out))
				return kFalse;
			continue;
		}
		if (cn.fNs != kW)
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));

		const std::string& name = cn.fName;
		if (name == "p")
		{
			Building b;
			if (!ReadParagraph(rd, c, b))
				return kFalse;
			KCMStoryShape::Para para;
			Finish(b, para);
			const bool16 marksJoin = ((rd.fSide == kSideOriginAsWritten && b.fMarkRevision > 0)
									  || (rd.fSide == kSideAfterWord && b.fMarkRevision < 0)) ? kTrue : kFalse;
			if (pendingJoin)
			{
				if (out.empty())
					return Refuse(rd, "a joined paragraph stands first: there is nothing for it to join");
				JoinOnto(out.back(), para);
			}
			else
			{
				out.push_back(para);
			}
			pendingJoin = marksJoin;
		}
		else if (name == "tbl")
		{
			if (inTable == kInNote)
				return Refuse(rd, "a table stands inside a footnote");
			if (pendingJoin)
				return Refuse(rd, "a paragraph mark next to a table was inserted or deleted");
			// ★A PARAGRAPH OF ITS OWN, ALWAYS - the split shape. Whose paragraph it is in the document
			//   is settled at the import (RejoinTables), from the document.
			out.push_back(KCMStoryShape::Para());
			if (!ReadTable(rd, c, inTable, inRow, inCell, static_cast<int32>(out.size()) - 1, 0))
				return kFalse;
		}
		else if (name == "sectPr" || name == "tcPr")
		{
			continue;					// the body's section (read by ReadSide); a cell's properties (read by ReadCell)
		}
		else if (name == "sdt" || name == "customXml")
		{
			if (name == "sdt" && IsRetiredOwnControl(t, c))
				continue;					// the legend of stage 3b: ours, and never text
			const int32 content = (name == "sdt") ? t.Child(c, kW, "sdtContent") : c;
			if (content >= 0 && !ReadBlocks(rd, content, inTable, inRow, inCell, out))
				return kFalse;
		}
		else if (name == "bookmarkStart" || name == "bookmarkEnd" || name == "proofErr" || name == "customXmlPr")
		{
			continue;
		}
		else
		{
			return Refuse(rd, "an element this reader does not know: " + QName(t, c));
		}
	}
	return kTrue;
}

/** Every run of paragraphs a story has: the body, then each cell in table order. */
void HoldersOf(KCMStoryShape::Story& s, std::vector< std::vector<KCMStoryShape::Para>* >& out)
{
	out.clear();
	out.push_back(&s.fBody);
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
		{
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				out.push_back(&s.fTables[t].fRows[r].fCells[c].fParas);
		}
	}
}

/*	ResolveNotes
	The footnotes this side refers to, in ascending order of Word's ids, each read from
	footnotes.xml; the references' fNote turned from an id into that rank.

	★NOTES NOBODY REFERS TO ON THIS SIDE ARE NOT THIS SIDE'S: the separators, and a note whose
	  reference was deleted in Word - the note is still in the file (measured 2026-09-19) and it is
	  the other side's.
*/
bool16 ResolveNotes(Reader& rd, const KCMXmlTree* notesTree)
{
	KCMStoryShape::Story& s = *rd.fStory;
	std::vector< std::vector<KCMStoryShape::Para>* > holders;
	HoldersOf(s, holders);

	std::vector<int32> ids;			// ascending, each once
	for (size_t h = 0; h < holders.size(); ++h)
	{
		const std::vector<KCMStoryShape::Para>& paras = *holders[h];
		for (size_t i = 0; i < paras.size(); ++i)
		{
			for (size_t k = 0; k < paras[i].fNoteRefs.size(); ++k)
			{
				const int32 id = paras[i].fNoteRefs[k].fNote;
				size_t at = 0;
				while (at < ids.size() && ids[at] < id)
					++at;
				if (at < ids.size() && ids[at] == id)
				{
					std::string why = "footnote ";
					AppendNumber(id, why);
					return Refuse(rd, why + " is referred to more than once");
				}
				ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(at), id);
			}
		}
	}
	if (ids.empty())
		return kTrue;

	const int32 root = (notesTree != nil) ? notesTree->Root() : -1;
	if (root < 0 || !notesTree->Is(root, kW, "footnotes"))
	{
		std::string why = "a reference to footnote ";
		AppendNumber(ids[0], why);
		return Refuse(rd, why + ", which the file does not have");
	}

	const KCMXmlTree* const saved = rd.fTree;
	rd.fTree = notesTree;
	const KCMXmlNode& all = notesTree->At(root);
	for (size_t n = 0; n < ids.size(); ++n)
	{
		std::string wanted;
		AppendNumber(ids[n], wanted);
		int32 found = -1;
		for (size_t k = 0; k < all.fChildren.size() && found < 0; ++k)
		{
			const int32 note = all.fChildren[k];
			if (!notesTree->Is(note, kW, "footnote"))
				continue;
			const std::string* type = notesTree->Attr(note, "type");
			if (type != nil && (*type == "separator" || *type == "continuationSeparator"))
				continue;
			const std::string* id = notesTree->Attr(note, "id");
			if (id != nil && *id == wanted)
				found = note;
		}
		if (found < 0)
		{
			rd.fTree = saved;
			return Refuse(rd, "a reference to footnote " + wanted + ", which the file does not have");
		}
		std::vector<KCMStoryShape::Para> paras;
		if (!ReadBlocks(rd, found, kInNote, 0, 0, paras))
		{
			rd.fTree = saved;
			return kFalse;
		}
		s.fNotes.push_back(paras);
	}
	rd.fTree = saved;

	for (size_t h = 0; h < holders.size(); ++h)
	{
		std::vector<KCMStoryShape::Para>& paras = *holders[h];
		for (size_t i = 0; i < paras.size(); ++i)
		{
			for (size_t k = 0; k < paras[i].fNoteRefs.size(); ++k)
			{
				int32& note = paras[i].fNoteRefs[k].fNote;
				size_t rank = 0;
				while (rank < ids.size() && ids[rank] != note)
					++rank;
				note = static_cast<int32>(rank);
			}
		}
	}
	return kTrue;
}

void SettleParas(std::vector<KCMStoryShape::Para>& paras)
{
	for (size_t i = 0; i < paras.size(); ++i)
	{
		for (size_t k = 0; k < paras[i].fRuby.size(); ++k)
		{
			if (!paras[i].fRuby[k].fGroup && paras[i].fRuby[k].fLen > 1)
				paras[i].fRuby[k].fGroup = kTrue;
		}
	}
}

}	// anonymous namespace

bool16 ReadSide(const std::string& documentXml, const std::string& footnotesXml, const std::string& stylesXml,
				Side side, KCMStoryShape::Story& out, std::vector<Mark>* outMarks, std::string& whyNot)
{
	out = KCMStoryShape::Story();
	whyNot.clear();

	KCMXmlTree document;
	if (!document.Parse(documentXml.data(), documentXml.size(), whyNot))
	{
		whyNot = "word/document.xml: " + whyNot;
		return kFalse;
	}
	KCMXmlTree notes;
	const bool16 hasNotes = footnotesXml.empty() ? kFalse : kTrue;
	if (hasNotes && !notes.Parse(footnotesXml.data(), footnotesXml.size(), whyNot))
	{
		whyNot = "word/footnotes.xml: " + whyNot;
		return kFalse;
	}

	Reader rd;
	rd.fTree = &document;
	rd.fSide = side;
	rd.fMarks = outMarks;
	rd.fStory = &out;
	if (!stylesXml.empty())
	{
		// An unreadable styles part is not fatal: the style ids then stand for themselves.
		KCMXmlTree styles;
		std::string ignored;
		if (styles.Parse(stylesXml.data(), stylesXml.size(), ignored))
			CollectStyleNames(styles, rd.fStyleNames);
	}

	const int32 root = document.Root();
	if (root < 0 || !document.Is(root, kW, "document"))
	{
		whyNot = "word/document.xml does not hold a w:document";
		return kFalse;
	}
	const int32 body = document.Child(root, kW, "body");
	if (body < 0)
	{
		whyNot = "word/document.xml has no w:body";
		return kFalse;
	}
	const int32 sect = document.Child(body, kW, "sectPr");
	if (sect >= 0)
	{
		const int32 dir = document.Child(sect, kW, "textDirection");
		const std::string* v = (dir >= 0) ? document.Attr(dir, "val") : nil;
		if (v != nil && (*v == "tbRl" || *v == "tbRlV"))
			out.fVertical = kTrue;
	}

	if (!ReadBlocks(rd, body, kInBody, 0, 0, out.fBody) || !ResolveNotes(rd, hasNotes ? &notes : nil))
	{
		whyNot = rd.fWhy;
		return kFalse;
	}
	return kTrue;
}

bool16 Read(const std::vector<KCMZipStore::Entry>& parts, ReadResult& out, std::string& whyNot)
{
	out = ReadResult();
	whyNot.clear();

	const std::string* document = nil;
	const std::string* footnotes = nil;
	const std::string* styles = nil;
	for (size_t i = 0; i < parts.size(); ++i)
	{
		const std::string& name = parts[i].fName;
		if (name == "word/document.xml")		document = &parts[i].fBytes;
		else if (name == "word/footnotes.xml")	footnotes = &parts[i].fBytes;
		else if (name == "word/styles.xml")		styles = &parts[i].fBytes;
		else if (name.compare(0, 14, "customXml/item") == 0 && name.size() > 4
				 && name.compare(name.size() - 4, 4, ".xml") == 0 && name.find("itemProps") == std::string::npos)
		{
			// ★FOUND BY NAMESPACE, NOT BY NUMBER: Word may renumber the items it keeps.
			Tag tag;
			if (!ReadTag(parts[i].fBytes, tag, whyNot))
			{
				whyNot = name + ": " + whyNot;
				return kFalse;
			}
			if (tag.fPresent && !out.fTag.fPresent)
				out.fTag = tag;
		}
	}
	if (document == nil)
	{
		whyNot = "the package has no word/document.xml";
		return kFalse;
	}

	const std::string empty;
	if (!ReadSide(*document, footnotes ? *footnotes : empty, styles ? *styles : empty,
				  kSideAfterWord, out.fAfter, &out.fMarks, whyNot))
		return kFalse;
	if (!ReadSide(*document, footnotes ? *footnotes : empty, styles ? *styles : empty,
				  kSideOriginAsWritten, out.fOrigin, nil, whyNot))
		return kFalse;
	return kTrue;
}

bool16 OriginMatchesTag(const ReadResult& r, std::string& outWhy)
{
	outWhy.clear();
	if (!r.fTag.fPresent)
	{
		outWhy = "the file carries no story tag";
		return kFalse;
	}
	std::string print;
	if (!Fingerprint(r.fOrigin, print, outWhy))
		return kFalse;
	if (print != r.fTag.fFingerprint)
	{
		outWhy = "revision tracking does not account for every change - the whole text was compared";
		return kFalse;
	}
	return kTrue;
}

namespace
{

/** The tables standing in one run of paragraphs (the body, or one cell), in document order. */
void TablesIn(const KCMStoryShape::Story& s, int32 inTable, int32 inRow, int32 inCell, std::vector<size_t>& out)
{
	out.clear();
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		const KCMStoryShape::Table& table = s.fTables[t];
		if (table.fInTable != inTable)
			continue;
		if (inTable >= 0 && (table.fInRow != inRow || table.fInCell != inCell))
			continue;
		out.push_back(t);
	}
}

/** Whether a paragraph says nothing: no words, no reference. */
bool16 SaysNothing(const KCMStoryShape::Para& p)
{
	return (p.fText.empty() && p.fNoteRefs.empty()) ? kTrue : kFalse;
}

/** A piece of a paragraph cut at `from`, as the reader would read it back: a tate-chu-yoko's and a
	warichu's value is the characters it now covers, and A RUBY THAT BEGAN BEFORE THE CUT IS NOT ON
	THIS PIECE. The reader reads such a ruby as two readings, one on each side - and that is not
	what the story said, so the export's own check has to see a difference here and refuse the
	story (Slice's note). The kenten, tate-chu-yoko and warichu cut the same way ARE rejoined by the
	reader (JoinOnto), so their halves stay. */
KCMStoryShape::Para PieceAsRead(const KCMStoryShape::Para& p, const std::vector<int32>& byteAt,
								int32 from, int32 to, bool16 first)
{
	KCMStoryShape::Para piece = Slice(p, byteAt, from, to, first);
	KCMParaText::SetSpanValuesToText(piece.fTcy, piece.fText);
	KCMParaText::SetSpanValuesToText(piece.fWarichu, piece.fText);
	if (from > 0)
	{
		KCMAttrSpanList kept;
		for (size_t k = 0; k < piece.fRuby.size(); ++k)
		{
			const KCMAttrSpan& r = piece.fRuby[k];
			bool16 beganBefore = kFalse;
			for (size_t o = 0; o < p.fRuby.size() && !beganBefore; ++o)
			{
				const KCMAttrSpan& orig = p.fRuby[o];
				if (orig.fStart < from && orig.fStart + orig.fLen > from && r.fStart == 0
					&& orig.fValue == r.fValue && orig.fStart + orig.fLen - from >= r.fLen)
					beganBefore = kTrue;
			}
			if (!beganBefore)
				kept.push_back(r);
		}
		piece.fRuby.swap(kept);
	}
	return piece;
}

/*	SplitParas
	One run of paragraphs into the split shape. Every table becomes an empty paragraph of its own;
	the words before it and after it become paragraphs. Where Word needs a paragraph and the story
	has no words for one - after a table that ends its run, and between two tables - an empty
	paragraph is put there; where Word does not need one, none is made: a table at the head of its
	paragraph opens with the table (unless a note's reference stands at that very place, which
	belongs in front of the table and has nowhere else to go - Slice), and an empty tail before an
	ordinary paragraph is not written.
	★THIS IS THE ONE PLACE THAT DECIDES THE SHAPE: the writer writes it block for block, the reader
	  reads it back the same, and RejoinTables undoes it from the document's shape. */
void SplitParas(KCMStoryShape::Story& s, std::vector<KCMStoryShape::Para>& paras, int32 inTable, int32 inRow, int32 inCell,
				bool16 asRead)
{
	std::vector<size_t> tables;
	TablesIn(s, inTable, inRow, inCell, tables);

	std::vector<KCMStoryShape::Para> made;
	size_t k = 0;			// the next table of this run
	for (size_t i = 0; i < paras.size(); ++i)
	{
		const KCMStoryShape::Para& p = paras[i];
		std::vector<int32> byteAt;
		KCMTextDiff::ToCodePoints(p.fText, nil, &byteAt);
		const int32 n = static_cast<int32>(byteAt.size());

		int32 pos = 0;
		bool16 first = kTrue;
		while (k < tables.size() && s.fTables[tables[k]].fParaIndex == static_cast<int32>(i))
		{
			int32 at = s.fTables[tables[k]].fOffset;
			if (at < pos)	at = pos;
			if (at > n)		at = n;

			// ★AS WRITTEN (Slice: a ruby cut by the table goes on both pieces) for the writer, AS READ
			//   (PieceAsRead: on the first only) for the settle - so that the export's own check sees
			//   the difference and refuses such a story, rather than writing half a reading.
			const KCMStoryShape::Para piece = asRead ? PieceAsRead(p, byteAt, pos, at, first)
													 : Slice(p, byteAt, pos, at, first);
			if (!(first && at == 0 && piece.fNoteRefs.empty()))
				made.push_back(piece);

			made.push_back(KCMStoryShape::Para());			// the table, alone
			s.fTables[tables[k]].fParaIndex = static_cast<int32>(made.size()) - 1;
			s.fTables[tables[k]].fOffset = 0;
			pos = at;
			first = kFalse;
			++k;
		}

		const KCMStoryShape::Para tail = asRead ? PieceAsRead(p, byteAt, pos, n, first)
											   : Slice(p, byteAt, pos, n, first);
		if (first || !SaysNothing(tail))
		{
			made.push_back(tail);
			continue;
		}
		// an empty tail after a table: only where Word asks for a paragraph
		const bool16 last = (i + 1 == paras.size());
		bool16 nextOpensWithTable = kFalse;
		if (!last && k < tables.size() && s.fTables[tables[k]].fParaIndex == static_cast<int32>(i + 1)
			&& s.fTables[tables[k]].fOffset == 0)
		{
			nextOpensWithTable = kTrue;
			for (size_t r = 0; r < paras[i + 1].fNoteRefs.size(); ++r)
				if (paras[i + 1].fNoteRefs[r].fAt == 0)
					nextOpensWithTable = kFalse;		// its head piece is written, with the reference
		}
		if (last || nextOpensWithTable)
			made.push_back(tail);
	}
	paras.swap(made);
}

/*	RejoinParas
	One run of paragraphs from the split shape back into the shape `shape` holds - the document's
	- table by table: the k-th table of this run goes where the k-th table of the document's run
	stands. Words the file added or took away stay as they are; only the breaks NEXT TO a table are
	decided here, and they are decided by the document.
	⚠When the two runs do not hold the same number of tables nothing is done: the import's own
	 check (TablesAgree) refuses such a story by name. */
void RejoinParas(KCMStoryShape::Story& s, std::vector<KCMStoryShape::Para>& paras,
				 const KCMStoryShape::Story& shape, const std::vector<KCMStoryShape::Para>& shapeParas,
				 int32 inTable, int32 inRow, int32 inCell)
{
	std::vector<size_t> mine, theirs;
	TablesIn(s, inTable, inRow, inCell, mine);
	TablesIn(shape, inTable, inRow, inCell, theirs);
	if (mine.size() != theirs.size())
		return;

	std::vector<KCMStoryShape::Para> made;
	size_t k = 0;
	for (size_t i = 0; i < paras.size(); ++i)
	{
		if (!(k < mine.size() && s.fTables[mine[k]].fParaIndex == static_cast<int32>(i)))
		{
			made.push_back(paras[i]);
			continue;
		}

		// what the document says about ITS k-th table of this run
		const KCMStoryShape::Table& theirTable = shape.fTables[theirs[k]];
		const int32 p = theirTable.fParaIndex;
		const bool16 pValid = (p >= 0 && static_cast<size_t>(p) < shapeParas.size()) ? kTrue : kFalse;
		const int32 pLen = pValid ? CodePointsIn(shapeParas[static_cast<size_t>(p)].fText) : 0;
		bool16 joinBefore = (theirTable.fOffset > 0) ? kTrue : kFalse;
		if (k > 0 && shape.fTables[theirs[k - 1]].fParaIndex == p)
			joinBefore = kTrue;
		if (pValid)
		{
			for (size_t r = 0; r < shapeParas[static_cast<size_t>(p)].fNoteRefs.size(); ++r)
				if (shapeParas[static_cast<size_t>(p)].fNoteRefs[r].fAt <= theirTable.fOffset)
					joinBefore = kTrue;		// a reference in front of the table: the head piece was written
		}
		bool16 joinAfter = (theirTable.fOffset < pLen) ? kTrue : kFalse;
		if (k + 1 < theirs.size() && shape.fTables[theirs[k + 1]].fParaIndex == p)
			joinAfter = kTrue;

		// the table goes onto the paragraph before it, or opens one
		if (joinBefore && !made.empty())
		{
			s.fTables[mine[k]].fParaIndex = static_cast<int32>(made.size()) - 1;
			s.fTables[mine[k]].fOffset = CodePointsIn(made.back().fText);
		}
		else
		{
			made.push_back(KCMStoryShape::Para());
			s.fTables[mine[k]].fParaIndex = static_cast<int32>(made.size()) - 1;
			s.fTables[mine[k]].fOffset = 0;
		}

		// the paragraph after the table, when it is words rather than the next table
		const bool16 nextIsTable = (k + 1 < mine.size() && s.fTables[mine[k + 1]].fParaIndex == static_cast<int32>(i + 1)) ? kTrue : kFalse;
		if (i + 1 < paras.size() && !nextIsTable)
		{
			const KCMStoryShape::Para& next = paras[i + 1];
			if (joinAfter)
			{
				JoinOnto(made.back(), next);
				++i;
			}
			else if (SaysNothing(next))
			{
				// Word's own paragraph, or the document's? The document's when it holds an empty
				// paragraph right after the table's; Word's otherwise, and then it is not a paragraph.
				bool16 theirsHasEmptyAfter = kFalse;
				if (pValid && static_cast<size_t>(p + 1) < shapeParas.size() && SaysNothing(shapeParas[static_cast<size_t>(p + 1)]))
				{
					theirsHasEmptyAfter = kTrue;
					if (k + 1 < theirs.size() && shape.fTables[theirs[k + 1]].fParaIndex == p + 1)
						theirsHasEmptyAfter = kFalse;	// that "empty" paragraph is the next table's
				}
				if (!theirsHasEmptyAfter)
					++i;
			}
		}
		++k;
	}
	paras.swap(made);
}

}	// anonymous namespace

void SplitAtTables(KCMStoryShape::Story& s, bool16 asRead)
{
	// ⚠The body first, then the cells in table order: SplitParas renumbers the tables of the run it
	//   is given and no other, and a cell's run is named by the ordinal of the table that holds it,
	//   which the split never changes.
	SplitParas(s, s.fBody, -1, 0, 0, asRead);
	for (size_t t = 0; t < s.fTables.size(); ++t)
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				SplitParas(s, s.fTables[t].fRows[r].fCells[c].fParas, static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c), asRead);
}

void RejoinTables(KCMStoryShape::Story& s, const KCMStoryShape::Story& shape)
{
	if (s.fTables.size() != shape.fTables.size())
		return;
	RejoinParas(s, s.fBody, shape, shape.fBody, -1, 0, 0);
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
		{
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
			{
				if (r < shape.fTables[t].fRows.size() && c < shape.fTables[t].fRows[r].fCells.size())
					RejoinParas(s, s.fTables[t].fRows[r].fCells[c].fParas, shape,
								shape.fTables[t].fRows[r].fCells[c].fParas,
								static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c));
			}
		}
	}
}

void SettleForThisFormat(KCMStoryShape::Story& s)
{
	// ★THE SHAPE FIRST: what this spelling cannot tell apart begins with where a table stands in its
	//   paragraph (SplitAtTables says why), and the readings and the empty cells below are settled
	//   on the split paragraphs.
	SplitAtTables(s);
	SettleParas(s.fBody);
	for (size_t t = 0; t < s.fTables.size(); ++t)
	{
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
		{
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
			{
				// ★A CELL OF WORD'S HOLDS A PARAGRAPH, ALWAYS: the writer puts <w:p/> into one that
				//   has none, and that is one empty paragraph on the way back.
				std::vector<KCMStoryShape::Para>& paras = s.fTables[t].fRows[r].fCells[c].fParas;
				if (paras.empty())
					paras.push_back(KCMStoryShape::Para());
				SettleParas(paras);
			}
		}
	}
	for (size_t n = 0; n < s.fNotes.size(); ++n)
	{
		// The same for a note: its mark has to stand in a paragraph, so the writer makes one (R2).
		if (s.fNotes[n].empty())
			s.fNotes[n].push_back(KCMStoryShape::Para());
		SettleParas(s.fNotes[n]);
	}
}

}	// namespace KCMStoryDocx

// End, KCMStoryDocx.cpp.
