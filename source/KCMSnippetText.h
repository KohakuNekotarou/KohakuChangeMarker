//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The shapes a story's paragraphs are handed over in (KCMAttrSpan / KCMParaAttrs), and the
//  PURE FUNCTIONS that cut them into rows. Nothing here touches the SDK.
//
//  **WHY THIS IS A HEADER OF ITS OWN.** What is in it turns one value into another and touches
//  nothing else, so it can be measured outside InDesign: the test is work\kescm-snippet-test,
//  and it includes THIS FILE as it stands -- not a copy that can drift, the way KTTextDiff
//  drifted from KCMTextDiff.
//
//  ★THE NAME IS HISTORICAL. Until 2026-09-03 this header also held the parser that read a
//  story's text, ruby and kenten out of the snippet XML (IDMS / InCopy interchange), and the
//  counters that reconciled positions counted from that XML with the document's own. The story
//  is read straight from the text model now (KCMTextRead.cpp), which fills the same two structs
//  below - so the diff, the rows, the jump and the marks never noticed the change. What went,
//  and why: docs/superpowers/specs/2026-08-31-kcm-story-direct-read-design.md.
//
//  @warning **AN EMPTY RUBY STRING IS NO RUBY**, which is the official rule and not an
//   invention here: GetRubyStrandInfo turns its attribute flag off when the string it read has
//   length 0 ("if the ruby string is empty in the ruby strand, consider ruby to be off"). The
//   reader honours it (KCMTextRead::ScanRuby), so fValue below is never empty.
//
//========================================================================================

#ifndef __KCMSnippetText_h__
#define __KCMSnippetText_h__

#include "BaseType.h"		// int32, bool16 (nothing else - this header does not even use nil)

#include <string>
#include <vector>

/** One stretch of characters carrying ONE character attribute, inside one paragraph.

	**POSITIONS ARE CODE POINTS**, counted the way InDesign counts text positions, so a number
	worked out here lines up with the paragraph offsets the diff already produces (a surrogate
	pair is one).

	**ONE TYPE FOR RUBY AND KENTEN.** They are different mechanisms in the SDK -- ruby is a
	STRAND (IRubyAttrStrand, run-based) and kenten is a set of CHARACTER ATTRIBUTES -- but what
	the panel needs of them is identical: a stretch of characters, and a value that says what is
	sitting over it. Writing the comparison twice would mean fixing it twice.
	So fValue holds the READING for ruby and the KIND for kenten ("KentenBlackCircle").
*/
struct KCMAttrSpan
{
	int32		fStart;		// first character of the base text, within its paragraph
	int32		fLen;		// how many characters the attribute covers
	std::string	fValue;		// the reading (ruby) or the kind (kenten), UTF-8. @warning never empty

	/** kTrue for GROUP ruby -- one reading spread over several base characters (琥珀 -> こはく)
		-- against MONO ruby, where each character has its own (琥 -> こ, 珀 -> はく).

		It is carried because the two are different typesetting, so turning one into the other IS
		a change even when every reading stays the same. InDesign writes it as RubyType="GroupRuby"
		and omits the attribute for mono, so mono is the default here too. The pair is the SDK's
		own: IRubyStyle::RubyKind, kRubyKind_Group / kRubyKind_Mono.
		@warning RUBY ONLY. Kenten has no such distinction -- it is per character by nature -- so
		  its spans always leave this kFalse, and the comparison then never reports a difference
		  in it. */
	bool16		fGroup;

	KCMAttrSpan() : fStart(0), fLen(0), fGroup(kFalse) {}
	KCMAttrSpan(int32 start, int32 len, const std::string& value, bool16 group = kFalse)
		: fStart(start), fLen(len), fValue(value), fGroup(group) {}
};

typedef std::vector<KCMAttrSpan> KCMAttrSpanList;

/** Everything one paragraph carries OVER its characters -- the attributes a change can hide in
	while the words themselves stay identical.

	**WHY A STRUCT RATHER THAN ANOTHER OUT-PARAMETER.** Ruby was the first, kenten is the second,
	and the reader's signature would grow a parameter for each. This way the reader answers one
	thing per paragraph and a third attribute costs a field, not a new argument at every call
	site. ⚠A field added here has to be FILLED by KCMTextRead::ReadStory - it is the only
	  source, and a field it leaves empty reads as "never changed" (see the warning there).
	@warning **what is deliberately NOT in here: applied styles.** Finding those was considered
	  and rejected. A paragraph whose text is unchanged and whose style was swapped keeps reading
	  "None".
*/
struct KCMParaAttrs
{
	KCMAttrSpanList	fRuby;

	/** ★**READ AND REPORTED AGAIN SINCE 2026-09-01** (user's call). It was compared for a day in
		August, switched off, and switched back on in the one place that decides it -
		KCMStoryDiffRun's AddAttributeChanges (called AddAttrOnlyChanges until 2026-09-01, when it
		stopped being attr-ONLY - see there). **Keeping the reading through the months it was not
		reported is what made turning it back on one call**: had the reader stopped filling this,
		the knowledge that five characters marked with one kind are ONE range - which cost a snippet
		from the user to get right, and which is the opposite of ruby, where the same five come out
		as five - would have had to be found a second time. (The reader is KCMTextRead::ScanKenten
		now, reading kTAKentenKindBoss off the character attribute strand; the rule is the same.)
		@warning the value is a KIND ("BlackCircle"), never something a reader reads aloud. Whoever
		 draws it asks Change::fAttrKind first; see the note on KCMAttrSpan::fValue. */
	KCMAttrSpanList	fKenten;

	/** Which table cell this paragraph IS, if it is one at all.

		**WHY IT EXISTS: A CELL IS A PLACE.** The text of a cell paragraph and of a body paragraph
		are indistinguishable after the fact, and SplitRunAtPlaces below has to cut a row where the
		place changes - otherwise one row spans a body edit and a cell edit and points at neither
		(measured on the tablespan document: one row of 22 characters for two edits of one).

		**THEY ARE ON EVERY PARAGRAPH OF THE CELL, not just its first.** A cell can hold several:
		anyone who presses Return inside one, and EVERY MERGED CELL, because merging moves the other
		cells' paragraphs into the survivor (measured -- four cells came back as one holding
		'c0/c1/c2/c3'). The XML parser once lost the identity at the <Br />, so the halves after the
		first looked like BODY text sitting inside a table and the story was refused; the reader
		takes the place from the THREAD (one thread per cell), so every paragraph of it carries it.

		Where the cell's text stands is not in here: the reader takes each paragraph's TextIndex
		from the walk (ITextStoryThreadDict::QueryThread(GetGridID(GridAddress)) -> GetTextStart,
		the road SnpIterTableUseDictHier calls the recommended one). ★The cells come AFTER the
		whole of the body in that order, as ITableTextContent.h states ("ALWAYS at greater
		TextIndex than the Text Story Thread that the Table Model is anchored in").

		fTableOrdinal counts EVERY table in the order of its thread block, nested ones included -
		  the order the document keeps them in (KCMTextRead.cpp, TableAt / EarlierBlock). */
	enum { kNotACell = -1 };

	int32				fTableOrdinal;	// kNotACell, or 0.. = which table this cell belongs to
	int32				fCellRow;		// grid row of that cell, -1 when not a cell
	int32				fCellCol;		// grid column of that cell, -1 when not a cell

	/** Which footnote of the story this paragraph stands in, kNotAFootnote when it is not in one.

		★A FOOTNOTE IS A PLACE, exactly as a cell is, and for the same reason IsCell() exists: the
		text of a footnote paragraph and of a body paragraph are indistinguishable after the fact,
		and SplitRunAtPlaces has to cut a run where the PLACE changes - otherwise one row spans a
		body edit and a footnote edit and points at neither.
		⚠A FOOTNOTE IS NOT A CELL. It has no table, no row and no column, so it gets a field of its
		  own rather than a borrowed fTableOrdinal - and a paragraph is never both.
		★Numbered in the order the threads come out (TextIndex order); a footnote is a thread that
		  is neither the body nor a cell (KCMTextRead.cpp, ReadStory). The XML parser this replaced
		  did not know <Footnote> at all and folded the note into the body, which is why a story
		  with one was refused outright - the fault the direct read was measured on. */
	enum { kNotAFootnote = -1 };

	int32				fFootnoteOrdinal;

	KCMParaAttrs()
		: fTableOrdinal(kNotACell), fCellRow(-1), fCellCol(-1),
		  fFootnoteOrdinal(kNotAFootnote) {}

	/** Whether this paragraph is a table cell.

		**A CELL OF A NESTED TABLE IS A CELL LIKE ANY OTHER.** It used to be marked apart
		(kNestedCell) so that the story could be refused; the reader takes an inner table's cells
		from their own threads exactly as it takes an outer table's, so there is nothing to mark. */
	bool16 IsCell() const { return fTableOrdinal >= 0; }

	/** Whether this paragraph stands inside a footnote. */
	bool16 IsFootnote() const { return fFootnoteOrdinal >= 0; }
};

namespace KCMSnippetText
{

/** Append one code point to a UTF-8 string. */
inline void AppendUtf8(std::string& out, int32 codePoint)
{
	if (codePoint < 0x80)
	{
		out += static_cast<char>(codePoint);
	}
	else if (codePoint < 0x800)
	{
		out += static_cast<char>(0xC0 | (codePoint >> 6));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
	else if (codePoint < 0x10000)
	{
		out += static_cast<char>(0xE0 | (codePoint >> 12));
		out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (codePoint >> 18));
		out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (codePoint & 0x3F));
	}
}

/** How many CODE POINTS a UTF-8 string holds -- continuation bytes (10xxxxxx) are not counted.

	This is the unit the whole comparison works in, so a four-byte character counts once here
	exactly as it counts once as a TextIndex.
*/
inline int32 CountCodePoints(const std::string& utf8)
{
	int32 n = 0;
	for (size_t i = 0; i < utf8.size(); ++i)
	{
		if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80)
			++n;
	}
	return n;
}

/** The text of a RUN of paragraphs, with the break characters put back.

	**IT STANDS BESIDE IndexInStory ON PURPOSE.** The two are one convention seen from both ends
	-- this one says how a run's paragraphs are strung together, that one says where a position
	in the resulting string lands in the document -- and they were in different files while only
	one of them knew about the invisible characters a table adds. What came of that is below, at
	IndexInStory.

	@param paragraphs every paragraph of the story.
	@param start the first paragraph of the run.
	@param count how many paragraphs the run covers.
*/
inline std::string JoinParagraphs(const std::vector<std::string>& paragraphs, int32 start, int32 count)
{
	std::string out;
	for (int32 i = 0; i < count; ++i)
	{
		const int32 index = start + i;
		if (index < 0 || index >= static_cast<int32>(paragraphs.size()))
			continue;
		if (i > 0)
			out += "\n";
		out += paragraphs[index];
	}
	return out;
}

/** A run of consecutive paragraphs that all sit in the same PLACE: the story's body, or one
	cell.

	**WHY A ROW IS CUT HERE.** A cell IS a paragraph, so two paragraphs that both changed and
	  happen to be next to each other in the paragraph list go into ONE run of the paragraph diff --
	  even when one of them is body text and the other is inside the table. The row then reads as
	  one edit spanning the words before the table, the cell, and whatever follows, and its mark
	  covers all the unchanged text in between (measured: a row covering 22 characters for two
	  edits of one character each).
	@warning adjacent paragraphs in the same place STILL share a row -- that part is right, and a
	  cell holding several paragraphs depends on it.
	★Since the direct read the list is in TextIndex order - the whole body first, then every cell,
	  then every footnote - so the body is ONE run and the cut falls where the body ends. (The XML
	  put the cells where the table stood, and the body appeared more than once.)
*/
struct ParaRegion
{
	int32	fStart;		///< first paragraph of the run
	int32	fCount;		///< how many paragraphs
	int32	fTable;		///< KCMParaAttrs::kNotACell for body text, else which table
	int32	fRow;		///< grid row when it is a cell, -1 otherwise
	int32	fCol;		///< grid column when it is a cell, -1 otherwise
	int32	fFootnote;	///< KCMParaAttrs::kNotAFootnote for body text and cells, else which footnote

	ParaRegion() : fStart(0), fCount(0), fTable(KCMParaAttrs::kNotACell), fRow(-1), fCol(-1),
				   fFootnote(KCMParaAttrs::kNotAFootnote) {}

	/** The same place - not the same paragraphs.

		⚠**THE FOOTNOTE HAS TO BE ASKED ABOUT HERE AND FILLED IN BY ParagraphRegions, OR NEITHER
		  WORKS.** If only one of the two is done, every region carries the same -1 and the runs are
		  cut exactly as they were before - the code reads as though footnotes were separated while
		  nothing separates them. */
	bool16 SamePlaceAs(const ParaRegion& other) const
	{
		return fTable == other.fTable && fRow == other.fRow && fCol == other.fCol
			&& fFootnote == other.fFootnote;
	}
};

/** The places a run of paragraphs passes through, in order. */
inline void ParagraphRegions(const std::vector<KCMParaAttrs>& attrs, int32 start, int32 count,
							 std::vector<ParaRegion>& out)
{
	out.clear();
	for (int32 i = start; i < start + count; ++i)
	{
		ParaRegion here;
		here.fStart = i;
		here.fCount = 1;
		if (i >= 0 && static_cast<size_t>(i) < attrs.size())
		{
			if (attrs[i].IsCell())
			{
				here.fTable = attrs[i].fTableOrdinal;
				here.fRow = attrs[i].fCellRow;
				here.fCol = attrs[i].fCellCol;
			}
			// ⚠NOT an `else`: the two are separate fields and a paragraph could in principle carry
			//   both (a table inside a footnote). Asking them one at a time keeps that possible.
			here.fFootnote = attrs[i].fFootnoteOrdinal;
		}

		if (!out.empty() && out.back().SamePlaceAs(here))
			++out.back().fCount;
		else
			out.push_back(here);
	}
}

/** One piece of a split run: the paragraphs it covers on each side. */
struct RegionPair
{
	int32	fSourceStart;
	int32	fSourceCount;
	int32	fTargetStart;
	int32	fTargetCount;

	RegionPair() : fSourceStart(0), fSourceCount(0), fTargetStart(0), fTargetCount(0) {}
};

/** Add one piece to the answer.

	**ONE PLACE**, because SplitRunAtPlaces below describes a piece in four different situations
	and every one of them has to fill in all four fields. Written out at each, a field added to
	RegionPair would be set at three of them and forgotten at the fourth -- and the piece that
	forgot it would still compile and still look right.
*/
inline void AppendPair(std::vector<RegionPair>& out,
					   int32 sourceStart, int32 sourceCount, int32 targetStart, int32 targetCount)
{
	RegionPair piece;
	piece.fSourceStart = sourceStart;
	piece.fSourceCount = sourceCount;
	piece.fTargetStart = targetStart;
	piece.fTargetCount = targetCount;
	out.push_back(piece);
}

/** Cut one run of the paragraph diff into one piece per PLACE (see ParaRegion).

	**WHEN IT DOES NOT CUT, IT SAYS SO BY ANSWERING WITH ONE PIECE.** Three shapes are cut:
	  - a pure insertion: every piece goes in at the same spot in the older version;
	  - a pure deletion: the mirror of it;
	  - a replacement whose two sides pass through the SAME places in the same order.
	@warning anything else -- the table itself gained or lost cells between the versions, say --
	  is left whole. There is no honest way to pair the halves up, and one row that is too wide is
	  better than several that point at the wrong cells.
*/
inline void SplitRunAtPlaces(const std::vector<KCMParaAttrs>& sourceAttrs,
							 int32 aStart, int32 aCount,
							 const std::vector<KCMParaAttrs>& targetAttrs,
							 int32 bStart, int32 bCount,
							 std::vector<RegionPair>& out)
{
	out.clear();

	std::vector<ParaRegion> aRegions;
	std::vector<ParaRegion> bRegions;
	ParagraphRegions(sourceAttrs, aStart, aCount, aRegions);
	ParagraphRegions(targetAttrs, bStart, bCount, bRegions);

	if (aCount == 0 && bRegions.size() > 1)
	{
		// Nothing of the older version is involved: every piece goes in at the same spot.
		for (size_t i = 0; i < bRegions.size(); ++i)
			AppendPair(out, aStart, 0, bRegions[i].fStart, bRegions[i].fCount);
		return;
	}

	if (bCount == 0 && aRegions.size() > 1)
	{
		for (size_t i = 0; i < aRegions.size(); ++i)
			AppendPair(out, aRegions[i].fStart, aRegions[i].fCount, bStart, 0);
		return;
	}

	if (aRegions.size() > 1 && aRegions.size() == bRegions.size())
	{
		for (size_t i = 0; i < aRegions.size(); ++i)
		{
			if (!aRegions[i].SamePlaceAs(bRegions[i]))
			{
				out.clear();				// the versions do not pass through the same places
				AppendPair(out, aStart, aCount, bStart, bCount);
				return;
			}

			AppendPair(out, aRegions[i].fStart, aRegions[i].fCount,
					   bRegions[i].fStart, bRegions[i].fCount);
		}
		return;
	}

	AppendPair(out, aStart, aCount, bStart, bCount);	// the run, left whole
}

/** Where an offset into that joined string lands in the document, as a TextIndex.

	**WHY THIS IS NOT `base + offset`.** JoinParagraphs puts ONE character between two
	paragraphs; the document may not have them that close together at all. Two faults of this
	shape were found on one day:
	  (1) a table's own character and a row's terminator sit at exactly such a boundary, so a
	      change covering two ADJACENT paragraphs put the second one short by them -- MEASURED on
	      the real table snippet: at 21 where the document has 22, and at 31 against 32;
	  (2) and a table's CELLS are not between the paragraphs at all (they stand past the whole
	      body, ITableTextContent.h), so the distance across a table is nothing like the sum of the
	      paragraphs between -- MEASURED: a change to the paragraph after a table selected a
	      character inside a cell instead.
	@warning SILENT, and no length check can catch either: a total that agrees says nothing about
	  where the characters are. Both are answered the same way: every paragraph's position is
	  LOOKED UP, never added up.

	@param paragraphs every paragraph of the story.
	@param starts one document position per paragraph, as the reader took them from the walk
		(KCMTextRead::ReadStory, one TextIndex per paragraph - body, cells and footnotes alike).
	@param start the first paragraph of the run.
	@param count how many paragraphs the run covers.
	@param base where to answer from when the run covers no paragraph of its own (an insertion
		between two paragraphs): the caller's position for the next surviving paragraph.
	@param joinedOffset a position in JoinParagraphs' answer, in CODE POINTS, 0 .. its length.
	@return the same position as a TextIndex into the story.
*/
inline int32 IndexInStory(const std::vector<std::string>& paragraphs,
						  const std::vector<int32>& starts,
						  int32 start, int32 count, int32 base, int32 joinedOffset)
{
	// **IT LOOKS EVERY PARAGRAPH UP INSTEAD OF ADDING UP THE HIDDEN CHARACTERS BETWEEN THEM.**
	//   The old form walked from the run's base and, at each paragraph break, added the count of
	//   characters a table leaves there. That is right only while the document lays paragraphs
	//   out in the order they are listed, and A TABLE BREAKS EXACTLY THAT: the text model keeps
	//   a table's cells AFTER the whole of the story's own text (ITableTextContent.h), so no
	//   amount of adding gets from the paragraph before a table to the one after it. MEASURED:
	//   a change to the paragraph following a table selected a character inside a cell instead.
	//   Positions come from the table `starts`, which the reader filled from the walk. This walk
	//     only has to decide WHICH paragraph the offset falls in.
	int32 joined = 0;		// where the paragraph being looked at begins, inside the joined string
	for (int32 i = 0; i < count; ++i)
	{
		const int32 which = start + i;
		if (which < 0 || which >= static_cast<int32>(paragraphs.size())
			|| which >= static_cast<int32>(starts.size()))
			break;

		const int32 len = CountCodePoints(paragraphs[which]);

		// @warning AT the break belongs to the paragraph BEFORE it, which is the rule the old form
		//   kept ("the offset is at or before it -- nothing to add") and the paragraph-start table
		//   agrees with: the next paragraph's start is where the character AFTER the break sits.
		if (joinedOffset <= joined + len)
			return starts[which] + (joinedOffset - joined);

		joined += len + 1;		// the paragraph, and the one character JoinParagraphs puts after it
	}

	// Past the end of the run - or a run with no paragraphs of its own, which is how an insertion
	// between two paragraphs arrives. The caller's base is where the next surviving paragraph
	// begins, and that is the right answer for the empty case.
	if (count > 0)
	{
		const int32 last = start + count - 1;
		if (last >= 0 && last < static_cast<int32>(paragraphs.size())
			&& last < static_cast<int32>(starts.size()))
			return starts[last] + CountCodePoints(paragraphs[last]);
	}
	return base;
}

/** True when two spans lists differ -- the question "did only the ruby change?" is this one
	asked about a paragraph whose text came out identical.

	@warning compared as an ordered list, not as a set: moving the same ruby onto different
	  characters is a change, and so is reordering two of them.
*/
inline bool16 SpansDiffer(const KCMAttrSpanList& a, const KCMAttrSpanList& b)
{
	if (a.size() != b.size())
		return kTrue;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].fStart != b[i].fStart || a[i].fLen != b[i].fLen || a[i].fValue != b[i].fValue)
			return kTrue;
		// Mono turned into group is a change even when every reading is the same: 琥珀 read as
		//   こ+はく and 琥珀 read as こはく are different typesetting, and the reader asked to see it.
		if ((a[i].fGroup != 0) != (b[i].fGroup != 0))
			return kTrue;
	}
	return kFalse;
}

}	// namespace KCMSnippetText

#endif // __KCMSnippetText_h__

// End, KCMSnippetText.h.
