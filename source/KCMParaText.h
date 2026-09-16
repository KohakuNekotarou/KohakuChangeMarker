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
//  ★**IT WAS CALLED KCMSnippetText.h UNTIL 2026-09-04**, and the old name is worth knowing for
//  two reasons: the notes written before that date use it (they are records of their own time and
//  were deliberately left alone), and it says what this header used to be. Until 2026-09-03 it
//  ALSO held the parser that read a story's text, ruby and kenten out of the snippet XML (IDMS /
//  InCopy interchange), and the counters that reconciled positions counted from that XML against
//  the document's own. The story is read straight from the text model now (KCMTextRead.cpp),
//  which fills the same two structs below - so the diff, the rows, the jump and the marks never
//  noticed the change, and the name was the last thing still pointing at the old route. What
//  went, and why: docs/superpowers/specs/2026-08-31-kcm-story-direct-read-design.md.
//  ⚠**work\kescm-snippet-test keeps its own name** - it measures this header and KCMStoryMarkRanges.h,
//   and renaming a directory that four notes point at buys less than it costs.
//
//  @warning **AN EMPTY RUBY STRING IS NO RUBY**, which is the official rule and not an
//   invention here: GetRubyStrandInfo turns its attribute flag off when the string it read has
//   length 0 ("if the ruby string is empty in the ruby strand, consider ruby to be off"). The
//   reader honours it (KCMTextRead::ScanRuby), so fValue below is never empty.
//
//========================================================================================

#ifndef __KCMParaText_h__
#define __KCMParaText_h__

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
		a change even when every reading stays the same -- and since 2026-09-08 the panel SAYS which
		it now is, on the ruby row's upper line ("Mono" / "Group"; KCMStoryList.h, fRubyGroup).
		The pair is the SDK's own: IRubyStyle::RubyKind, kRubyKind_Group / kRubyKind_Mono.
		InDesign writes GROUP out as RubyType="GroupRuby" and leaves the attribute OFF for mono, so
		mono is the default here too -- ⚠**but that is true of a TEXT RUN only.** A STYLE DEFINITION
		writes every property it holds, PerCharacterRuby among them, so "PerCharacterRuby" found in
		a snippet has most likely been found inside a ParagraphStyle and is not a ruby at all
		(measured 2026-09-08: the sentence above was briefly "corrected" into a wrong one on the
		strength of exactly that hit -- [[investigate-with-tools-not-shell]], tell a DEFINITION from
		a USE before counting).
		★NOTHING HERE RESTS ON EITHER, since 2026-09-03: the setting is READ from the document
		(kTAMojiRubyBoss in KCMTextRead.cpp), never inferred from an attribute being present.
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

	/** ★**FOOTNOTE AND ENDNOTE MARKERS, CARRIED AS IF THEY WERE MARKS OVER THE TEXT**
		(2026-09-08, user's request: "the page shows a 1 above the character - show it in the row
		the way ruby is shown").

		⚠**THEY ARE NOT ATTRIBUTES AND THAT IS THE POINT.** A reference is a CHARACTER standing in
		 the text (U+0004 for a footnote, U+0005 for an endnote), not something laid over one. Read
		 as text it is invisible, so the panel showed a "□" and the reader could not tell what had
		 happened - which is exactly what the user reported. Carried here, the difference comes out
		 as one attribute change with the note's NUMBER above the words, the way the page prints it.

		★**THE VALUE IS THE NUMBER InDesign ITSELF WOULD PRINT**, taken from IFootnoteNumber rather
		 than counted here, so a document that restarts its numbering per page or per section still
		 agrees with the row (KCMTextRead::ScanNotes).

		⚠★★★**THE SPAN SITS ON THE CHARACTER BEFORE THE MARKER**, one character long: the marker's
		 own place is fStart + fLen, never fStart. It has to be that way - ReadStory TAKES the
		 marker out of the text (U+0004 / U+0005 are not in fText), so a span standing where it
		 stood would measure no characters at all and TakeAttrFor drops a span whose text length
		 comes out 0. It is also where the page puts the number: at the top right of the word the
		 note hangs off.
		 ⇒ ★**ANYTHING PUTTING THE REFERENCE BACK INTO TEXT WANTS fStart + fLen** (the HTML round
		   trip's <sup> among them). Reading fStart as the marker's place puts it one character early.
		 ⚠A marker that is its paragraph's FIRST character has no such character before it: the span
		  clips away and the note is NOT reported here at all. (Read from KCMTextRead.cpp on
		  2026-09-15 - the code says so and says why; not separately measured on a document.)
		 ⚠**THIS PARAGRAPH SAID THE OPPOSITE UNTIL 2026-09-15** - "the span is the marker itself, at
		  the marker's own place" - while KCMTextRead::ScanNotes has read `run.fAt = owned[i].fAt - 1`
		  all along, and the notes go through the very same TakeAttrFor as ruby with no correction
		  anywhere. The header was the wrong one, which is why it is written out here rather than
		  quietly replaced.
		@warning an ENDNOTE's own words are NOT here: they live in another story
		 (kEndnoteStoryBoss), which arrives as a story row of its own. Only the marker is reported. */
	KCMAttrSpanList	fFootnote;
	KCMAttrSpanList	fEndnote;

	/** ★**WARICHU AND TATE-CHU-YOKO** (2026-09-16, user's request): the manual ON/OFF of each,
		kTAWarichuAttrBoss / kTATatechuyokoAttrBoss on the character strand (KCMTextRead's
		ScanBoolAttribute). Measured the same day: one character can carry both - a tate-chu-yoko
		inside a warichu, and the other way round - so they are two lists, not one.
		★★**THE VALUE IS THE CHARACTERS THE SPAN COVERS**, not a flag. That is what the row lifts onto
		 its own line, and it is what makes rewriting the words inside one a change of that kind as
		 well as a change of the text.
		 ⇒ **Filled from the paragraph's own text** (SetSpanValuesToText, in ClosePara), so a table's
		   uncounted characters never land in it and the value is exactly what the row shows. */
	KCMAttrSpanList	fWarichu;
	KCMAttrSpanList	fTcy;

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

	/** The positions inside this paragraph that THE DOCUMENT COUNTS AND THE TEXT DOES NOT, given
		in the text's own count and in order. Almost always empty.

		★★★**WHY A PARAGRAPH NEEDS THIS AT ALL: A TABLE CAN STAND IN THE MIDDLE OF ONE.** The
		model holds kTextChar_Table for a table's anchor plus one kTextChar_TableContinued per row
		after the first, and those are not text - the reader leaves them out of fText, or the panel
		would show a gap and the diff would count them as characters that changed. Measured
		2026-09-01: inserting a table at the third insertion point of "あいうえ" leaves ONE
		paragraph reading [あ い **0016** う え CR] (work/kcm-selftest/midtable).
		⇒ **From there on, the two counts disagree**: う is the third character of the text and the
		fourth position of the document.

		⚠**WHAT WENT WRONG WITHOUT IT (measured 2026-09-04, before it existed).** KCMTextRead
		 already took the uncounted characters back out when it reported a ruby's offset - so the
		 PANEL was right - but everything that turned an offset back into a document position added
		 it to the paragraph's start and stopped there. Double-clicking the one reported change of
		 the midtable pair selected `Character index=2, charCode=16` - **the table's own anchor**,
		 one place short of the う whose reading had changed. A table of several rows would be short
		 by its row count.
		⇒ The conversion belongs to the paragraph, which is why it rides here rather than being
		  worked out again at each of the two places that need it (ModelOffsetInParagraph below,
		  used by IndexInStory and by KCMStoryDiffRun's AddAttrChange).

		@warning **FILLED BY THE READER, LIKE EVERY FIELD HERE** (KCMTextRead::ClosePara). Left
		 empty it reads as "the two counts agree", which is true of every paragraph without a table
		 standing inside it - and was the assumption the whole diff made until this field existed. */
	std::vector<int32>	fUncountedAt;

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

namespace KCMParaText
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

/** The attributes of one paragraph of a list, or a paragraph that counts both ways the same when
	the list does not reach it.

	★**A LIST THAT FALLS SHORT IS ANSWERED, NOT REFUSED.** Every caller here already walks past
	indices that are out of range for the paragraphs themselves, and the answer wanted for a
	paragraph nobody described is the one every ordinary paragraph gives: nothing uncounted.
*/
inline const KCMParaAttrs& AttrsOfParagraph(const std::vector<KCMParaAttrs>& attrs, int32 which)
{
	static const KCMParaAttrs kNothingUncounted;
	return (which >= 0 && static_cast<size_t>(which) < attrs.size()) ? attrs[which]
																	 : kNothingUncounted;
}

/** Where a position inside one paragraph stands in THE DOCUMENT'S count, given the TEXT'S.

	**THE TWO COUNTS ARE THE SAME NUMBER until a table stands inside the paragraph**, and then they
	part company by one for the table's anchor and one more for each row after the first (see
	KCMParaAttrs::fUncountedAt for the measurement). Everything the comparison hands out - a span's
	fStart, an offset into a joined run - is counted in the TEXT, because that is what the reader
	sees and what the panel draws; everything the document is then asked about - a mark, a
	selection, a jump - is counted in the MODEL. This is the one place that crosses between them.

	⚠**AT a skipped position, the answer is the position AFTER it.** A character standing at text
	 offset t sits after every uncounted position at or before t: the table's anchor comes between
	 the two characters, so the one following it has moved along by one. `<=`, not `<`.

	⚠★★**ONE NUMBER, TWO PLACES - AND THIS ANSWERS FOR THE START.** A range is named by two
	 offsets and the SAME offset means different things at its two ends: as a START, text offset 2
	 of "あい[表]うえ" is う, which stands AFTER the table; as an END (exclusive), it is the place
	 just past い, which is BEFORE it. No single answer is right for both, and this one is the
	 start's - so a range that ENDS exactly where a table stands comes back one position wide of
	 the mark, taking the anchor in with it.
	 ⇒ **That is the direction chosen deliberately.** The other reading puts a range that BEGINS
	   after a table one character short, which is the fault measured on 2026-09-04 (a mark and a
	   selection landing on the table's anchor instead of on the character whose ruby had changed).
	   A mark one position wide costs the reader nothing they can see - the anchor carries no wax
	   of its own, so nothing is drawn over it - while a mark one position short points at the
	   wrong thing, which is the one answer this comparison must never give.
	 ⚠It shows up only where a table stands INSIDE a paragraph, and only for a range that stops
	  exactly at it.

	@param attrs the paragraph's own attributes - only fUncountedAt is read.
	@param textOffset a position in the paragraph's text, 0 .. its length. Its own end is a valid
		position (a range that stops at the last character asks for it).
	@return the same position as an offset from the paragraph's start in the document's count.
*/
inline int32 ModelOffsetInParagraph(const KCMParaAttrs& attrs, int32 textOffset)
{
	// ★A WALK, DELIBERATELY - the same decision KCMTextRead's CountUncounted made and for the same
	//   reason: a paragraph holds one of these per table standing inside it, which is almost always
	//   none and never many, so anything cleverer would cost more to read than it saves to run.
	int32 skipped = 0;
	for (size_t k = 0; k < attrs.fUncountedAt.size() && attrs.fUncountedAt[k] <= textOffset; ++k)
		++skipped;
	return textOffset + skipped;
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

/** The characters from code point `start`, `len` of them, as UTF-8 - clipped to the string, and
	empty when nothing of it is inside.

	@warning CODE POINTS, the unit every offset in this header counts in (a four-byte character is
	  one), never bytes. */
inline std::string SliceCodePoints(const std::string& utf8, int32 start, int32 len)
{
	if (start < 0 || len <= 0)
		return std::string();

	size_t from = std::string::npos;
	int32 seen = 0;
	for (size_t i = 0; i < utf8.size(); ++i)
	{
		if ((static_cast<unsigned char>(utf8[i]) & 0xC0) == 0x80)
			continue;						// a continuation byte - not the start of a character
		if (seen == start)
			from = i;
		if (seen == start + len)
			return utf8.substr(from, i - from);
		++seen;
	}
	return (from == std::string::npos) ? std::string() : utf8.substr(from);
}

/** Give every span, as its value, the characters it covers in `paraText` - and drop a span that
	covers none.

	★**WHY A SPAN'S VALUE IS ITS OWN TEXT** is KCMParaAttrs::fWarichu's story. ★Dropping the one that
	  comes out empty keeps the rule the reader holds every kind to - no span has an empty value
	  (this header's head, "AN EMPTY RUBY STRING IS NO RUBY") - and the comparison and the panel
	  both rely on it. */
inline void SetSpanValuesToText(KCMAttrSpanList& spans, const std::string& paraText)
{
	KCMAttrSpanList kept;
	kept.reserve(spans.size());
	for (size_t i = 0; i < spans.size(); ++i)
	{
		KCMAttrSpan span(spans[i]);
		span.fValue = SliceCodePoints(paraText, span.fStart, span.fLen);
		if (!span.fValue.empty())
			kept.push_back(span);
	}
	spans.swap(kept);
}

/** A range of the paragraph's code points, [fFrom, fTo). */
struct LayerRange
{
	int32	fFrom;
	int32	fTo;

	LayerRange() : fFrom(0), fTo(0) {}
	LayerRange(int32 from, int32 to) : fFrom(from), fTo(to) {}
};

/** The lines of one side of a LAYERED change (warichu / tate-chu-yoko - KCMStoryKinds.h,
	KCMAttrKindIsLayered), as ranges of the paragraph's code points (2026-09-16, the user's drawings):

	    12      34          <- line 2: one PIECE over each hole of line 1
	    わり｜ちゅう｜のぶん   <- line 1: ONE piece over the hole of line 0, with its own holes
	    琥珀｜猫太郎          <- line 0: the paragraph, with ONE hole

	★**LINE 0 HAS ONE HOLE, LINE 1 MAY HAVE SEVERAL** (the user: "there may be several
	  tate-chu-yoko inside one warichu"). Only the layer this change stands in is lifted out of the
	  paragraph; another warichu elsewhere in the same paragraph stays plain text in the context. */
struct LayerPlan
{
	int32					fCount;			///< how many lines: 2 or 3 (0 before PlanLayers)
	int32					fChanged;		///< the line holding the change (1 or 2) - where the row's sign goes

	LayerRange				fHole;			///< line 0's one hole

	bool16					fMiddleIsBar;	///< line 1 is a bar alone: the mark is not on this side
	LayerRange				fMiddle;		///< line 1's characters (unless it is a bar)
	std::vector<LayerRange>	fUpperHoles;	///< line 1's holes, in order; line 2 has one piece over each

	int32					fChangedPiece;	///< which piece of line 2 is the change; -1 when the change is line 1
	bool16					fChangedPieceIsBar;	///< that piece is a bar: the mark is not on this side

	/** ★★Whether line 0 (the paragraph) is drawn at all (2026-09-16, the user: "the ID column says
		割注, so a tate-chu-yoko changing inside a warichu needs no text line - the warichu's line and
		the tate-chu-yoko's are enough; three lines only where three are needed"). kFalse exactly
		when the change stands inside a layer of the other kind: the lines are then 1 and 2 alone,
		fCount is 2, and fHole says nothing anybody draws.
		⚠fChanged, fMiddle and fUpperHoles keep their numbering (1 = the middle layer, 2 = the upper)
		 whether or not line 0 is shown - only fCount, the number of lines DRAWN, changes. */
	bool16					fShowsText;

	LayerPlan() : fCount(0), fChanged(0), fMiddleIsBar(kFalse), fChangedPiece(-1),
				  fChangedPieceIsBar(kFalse), fShowsText(kTrue) {}
};

/** Which lines one side of a warichu or tate-chu-yoko change is drawn on.

	★**NESTING IS CONTAINMENT.** A span of the OTHER layered kind that contains the change is line 1
	  and the change stands over it on line 2, beside every other span of the change's own kind inside
	  it; the spans of the other kind inside the change are line 2 over it. Over the SAME range the
	  warichu is the outer one. Ranges that merely cross are not nested.
	★**A SIDE WITHOUT THE MARK** draws a bar where the mark would stand, and the characters it covered
	  on the other side are hidden behind a bar below it - for both kinds (the user: "the two bars in
	  the same place"; "deleted looks the same as tate-chu-yoko"). Nothing is lifted over that bar.
	⚠A change with NO characters on this side (the words changed too) is a bar over a bar at `start`.

	@param changedIsWarichu kTrue for a warichu change, kFalse for a tate-chu-yoko.
	@param warichu / tcy this side's paragraph's spans of each kind, in order.
	@param start / len the change's range on this side, in the paragraph's code points.
	@param present whether this side carries the mark.
	@param paraLen the paragraph's length in code points. */
inline void PlanLayers(bool16 changedIsWarichu, const KCMAttrSpanList& warichu,
					   const KCMAttrSpanList& tcy, int32 start, int32 len, bool16 present,
					   int32 paraLen, LayerPlan& out)
{
	out = LayerPlan();
	const KCMAttrSpanList& own = changedIsWarichu ? warichu : tcy;
	const KCMAttrSpanList& other = changedIsWarichu ? tcy : warichu;
	const int32 end = start + ((len > 0) ? len : 0);

	// The span of the other kind this change stands inside. ⚠The warichu is the outer one of two
	//   over the same range, so a warichu change is never inside a tate-chu-yoko of its own extent.
	int32 outer = -1;
	if (len > 0)
	{
		for (size_t k = 0; k < other.size() && outer < 0; ++k)
		{
			const int32 s = other[k].fStart;
			const int32 e = other[k].fStart + other[k].fLen;
			const bool16 same = (s == start && e == end) ? kTrue : kFalse;
			if (s <= start && end <= e && (!same || !changedIsWarichu))
				outer = static_cast<int32>(k);
		}
	}

	if (outer >= 0)
	{
		const int32 os = other[outer].fStart;
		const int32 oe = other[outer].fStart + other[outer].fLen;
		// ★TWO LINES, NOT THREE: the layer it stands in, and itself - the text line is left out (see
		//   fShowsText). A change of the OUTER layer with this one inside it is the three-line case.
		out.fCount = 2;
		out.fShowsText = kFalse;
		out.fChanged = 2;
		out.fHole = LayerRange(os, oe);
		out.fMiddle = LayerRange(os, oe);

		// Every span of the change's own kind inside the outer one is a hole of line 1, the change
		// among them. A side WITHOUT the mark has no span there, so the change's range is put in
		// (and a span of this kind overlapping it - which would make two holes over the same
		// characters - is left out).
		bool16 placed = kFalse;
		for (size_t k = 0; k < own.size(); ++k)
		{
			const int32 s = own[k].fStart;
			const int32 e = own[k].fStart + own[k].fLen;
			if (s < os || e > oe)
				continue;
			const bool16 isChange = (present && s == start && e == end) ? kTrue : kFalse;
			if (!isChange && s < end && start < e)
				continue;								// overlaps the change's own range
			if (!placed && !present && start < s)
			{
				out.fChangedPiece = static_cast<int32>(out.fUpperHoles.size());
				out.fUpperHoles.push_back(LayerRange(start, end));
				placed = kTrue;
			}
			if (isChange)
			{
				out.fChangedPiece = static_cast<int32>(out.fUpperHoles.size());
				placed = kTrue;
			}
			out.fUpperHoles.push_back(LayerRange(s, e));
		}
		if (!placed)
		{
			out.fChangedPiece = static_cast<int32>(out.fUpperHoles.size());
			out.fUpperHoles.push_back(LayerRange(start, end));
		}
		out.fChangedPieceIsBar = present ? kFalse : kTrue;
		return;
	}

	out.fChanged = 1;
	out.fHole = LayerRange(start, end);
	if (!present)
	{
		out.fCount = 2;
		out.fMiddleIsBar = kTrue;
		return;
	}

	out.fMiddle = LayerRange(start, end);
	for (size_t k = 0; k < other.size(); ++k)
	{
		const int32 s = other[k].fStart;
		const int32 e = other[k].fStart + other[k].fLen;
		const bool16 same = (s == start && e == end) ? kTrue : kFalse;
		if (start <= s && e <= end && (!same || changedIsWarichu))
			out.fUpperHoles.push_back(LayerRange(s, e));
	}
	out.fCount = out.fUpperHoles.empty() ? 2 : 3;
}

/** The characters of `outer`, with every span of `inner` standing wholly inside it replaced by one
	placeholder - the text of a layer as it is once the layer above it is taken out.
	@param outerWinsTies whether an inner span over exactly the same range counts as inside (the
	  warichu is the outer one of two over the same range). */
inline std::string MaskNested(const std::string& paraText, const KCMAttrSpan& outer,
							  const KCMAttrSpanList& inner, bool16 outerWinsTies)
{
	std::string out;
	const int32 end = outer.fStart + outer.fLen;
	int32 at = outer.fStart;
	for (size_t k = 0; k < inner.size(); ++k)
	{
		const int32 s = inner[k].fStart;
		const int32 e = inner[k].fStart + inner[k].fLen;
		if (s < at || e > end)
			continue;
		if (s == outer.fStart && e == end && !outerWinsTies)
			continue;
		out += SliceCodePoints(paraText, at, s - at);
		out += '\x01';				// a stand-in for "a layer stood here"; never a document's own character
		at = e;
	}
	out += SliceCodePoints(paraText, at, end - at);
	return out;
}

/** kTrue when two versions of one warichu (or tate-chu-yoko) differ ONLY inside the layer standing in
	it - so the change belongs to that inner layer and not to this one.

	★**ONLY THE INNERMOST LAYER REPORTS A CHANGE OF ITS WORDS** (2026-09-16, the user's call): 77 -> 88
	  inside a tate-chu-yoko inside a warichu is the text row and the tate-chu-yoko row. The warichu's
	  own value changed too - it is its characters - and without this it would be a third row for the
	  same keystroke. A warichu character OUTSIDE the tate-chu-yoko is still the warichu's. */
inline bool16 OnlyNestedDiffers(const std::string& textA, const KCMAttrSpan& outerA,
								const KCMAttrSpanList& innerA,
								const std::string& textB, const KCMAttrSpan& outerB,
								const KCMAttrSpanList& innerB, bool16 outerWinsTies)
{
	return (MaskNested(textA, outerA, innerA, outerWinsTies)
			== MaskNested(textB, outerB, innerB, outerWinsTies)) ? kTrue : kFalse;
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

/** kTrue when cp is a character InDesign hangs an OBJECT on - one that text commands cannot bring
	back, because what they write is the character and the object lives somewhere else.

	★★★**MEASURED 2026-09-16 (the night the user asked for this rule): writing one back gives the
	  character ALONE.** A deleted anchored rectangle "restored" as X U+FFFC Y with no page item in
	  the story; the comparison then answered "0 changes left", so nothing even said so.
	⇒ Every write that puts words into the reader's document asks this of what goes in AND of what
	  comes out (the user's rule: in the Task Start mode, offer to restore a change only when its
	  text holds none of these). Deleting one is refused too - InDesign takes the object with the
	  character, so a restore would delete a table or a frame the reader added.

	★**A LIST, NOT A RANGE, AND EACH ENTRY IS ONE OF TextChar.h's.** Footnote/endnote reference
	  (0004/0005), table anchor and row continuation (0016/0017), page number and every text
	  variable (0018 - measured to be the one code they all share in a document), section name
	  (0019), the non-Roman special glyph (001A), the XML tag and note anchor mark (FEFF), an
	  anchored object (FFFC) and the index marker (E02C).
	⚠**NOT THE REST OF THE E0xx BAND.** Those are Find/Change wildcards that are never stored in a
	 document (the variables among them measured as 0018 in the text), and the private use area is
	 where a gaiji font lives - blocking it would refuse ordinary text.
	⚠**A PARAGRAPH BREAK, A FORCED LINE BREAK AND A TAB ARE NOT HERE.** They carry nothing, and a
	 paragraph break was restored correctly on the application the same day. */
inline bool16 IsObjectCharacter(int32 cp)
{
	switch (cp)
	{
		case 0x0004:	// kTextChar_FootnoteMarker
		case 0x0005:	// kTextChar_EndnoteMarker
		case 0x0016:	// kTextChar_Table
		case 0x0017:	// kTextChar_TableContinued
		case 0x0018:	// kTextChar_PageNumber / kTextChar_AutoText
		case 0x0019:	// kTextChar_SectionName
		case 0x001A:	// kTextChar_NonRomanSpecialGlyph
		case 0xFEFF:	// kTextChar_ZeroSpaceNoBreak - the XML tag's mark, and a note's anchor
		case 0xFFFC:	// kTextChar_ObjectReplacementCharacter - an anchored object
		case 0xE02C:	// kTextChar_IndexMarker
			return kTrue;
		default:
			return kFalse;
	}
}

/** kTrue when the words of one run may be written back across it: the two sides stand in the same
	PLACES (see ParaRegion), or neither side involves a place other than the body.

	★★**MEASURED 2026-09-16: A DELETED TABLE'S CELL "RESTORED" INTO NOWHERE.** The table was gone,
	  so its cells' paragraphs had no paragraph of their own on the target side; the change's
	  position fell back to the end of the story, "Restored 2 character(s)" was reported, and
	  neither the DOM nor a snippet export could find the words afterwards. The cell cannot be put
	  back by writing text - the table is an object - so the change is not offered.
	★**A FOOTNOTE IS A PLACE THE SAME WAY**, and a deleted footnote's paragraphs have the same
	  nowhere to go.

	@param aStart/aCount the run on the SOURCE side, in paragraphs.
	@param bStart/bCount the run on the TARGET side. ⚠**WHEN bCount IS 0 the words would go in at
		the start of paragraph bStart**, so that paragraph's place is what they would land in, and
		it is compared: a body paragraph deleted just before a table's cells would otherwise be
		written into the first cell. ⚠aCount 0 is NOT treated the same way - restoring an insertion
		deletes the target's words where they stand, and nothing lands anywhere.
	@return kFalse only when a cell or a footnote is involved and the places disagree. The body
		against the body is not this function's question, and answers kTrue. */
inline bool16 WordsCanBeWrittenAcross(const std::vector<KCMParaAttrs>& sourceAttrs,
									  int32 aStart, int32 aCount,
									  const std::vector<KCMParaAttrs>& targetAttrs,
									  int32 bStart, int32 bCount)
{
	std::vector<ParaRegion> aRegions;
	std::vector<ParaRegion> bRegions;
	ParagraphRegions(sourceAttrs, aStart, aCount, aRegions);
	if (bCount == 0 && aCount > 0 && bStart >= 0 && static_cast<size_t>(bStart) < targetAttrs.size())
		ParagraphRegions(targetAttrs, bStart, 1, bRegions);		// where the words would land
	else
		ParagraphRegions(targetAttrs, bStart, bCount, bRegions);

	bool16 containerInvolved = kFalse;
	for (size_t i = 0; i < aRegions.size() && !containerInvolved; ++i)
		if (aRegions[i].fTable != KCMParaAttrs::kNotACell || aRegions[i].fFootnote != KCMParaAttrs::kNotAFootnote)
			containerInvolved = kTrue;
	for (size_t i = 0; i < bRegions.size() && !containerInvolved; ++i)
		if (bRegions[i].fTable != KCMParaAttrs::kNotACell || bRegions[i].fFootnote != KCMParaAttrs::kNotAFootnote)
			containerInvolved = kTrue;
	if (!containerInvolved)
		return kTrue;

	if (aRegions.size() != bRegions.size())
		return kFalse;
	for (size_t i = 0; i < aRegions.size(); ++i)
		if (!aRegions[i].SamePlaceAs(bRegions[i]))
			return kFalse;
	return kTrue;
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
	@param attrs the same paragraphs' attributes. **ONLY fUncountedAt IS READ**, and only to cross
		from the text's count into the document's - see ModelOffsetInParagraph. A list shorter than
		`paragraphs` (or an empty one) is not an error: a paragraph it does not reach is taken to
		count the two the same way, which is what every paragraph without a table inside it does.
	@param start the first paragraph of the run.
	@param count how many paragraphs the run covers.
	@param base where to answer from when the run covers no paragraph of its own (an insertion
		between two paragraphs): the caller's position for the next surviving paragraph.
	@param joinedOffset a position in JoinParagraphs' answer, in CODE POINTS, 0 .. its length.
	@return the same position as a TextIndex into the story.

	@warning **THE OFFSET COMING IN IS THE TEXT'S AND THE ANSWER IS THE DOCUMENT'S**, and until
	 2026-09-04 this added the one straight onto the other. That is right for every paragraph whose
	 characters are all it holds, which is why nothing caught it: a table standing INSIDE a
	 paragraph is what makes the two disagree, and the old XML route refused such a story outright,
	 so nothing downstream had ever met one.
*/
inline int32 IndexInStory(const std::vector<std::string>& paragraphs,
						  const std::vector<int32>& starts,
						  const std::vector<KCMParaAttrs>& attrs,
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
			return starts[which] + ModelOffsetInParagraph(AttrsOfParagraph(attrs, which),
														  joinedOffset - joined);

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
			return starts[last] + ModelOffsetInParagraph(AttrsOfParagraph(attrs, last),
														 CountCodePoints(paragraphs[last]));
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
		// ★fGroup IS NOT COMPARED (2026-09-12, the user's decision: "mono turned into group is not
		//   a change - take that judgement out"). It was one from 2026-09-08 to 2026-09-12, on the
		//   reasoning that こ+はく and こはく are different typesetting; the user withdrew it. The
		//   flag is still READ (KCMAttrSpan::fGroup) and still carried to the panel (fRubyGroup),
		//   because the row template once showed it - nothing judges by it any more.
	}
	return kFalse;
}

/** What has to be WRITTEN to turn one paragraph's attribute spans into another's: the spans to
	take off (`outClear`) and the spans to put on (`outApply`).

	★★★**A SPAN BOTH SIDES CARRY IS IN NEITHER LIST, AND THAT IS THE WHOLE REASON THIS EXISTS.** A
	  reading is three attributes and its LOOK is twenty-seven more (KCMStoryRestore.cpp's
	  ClearRuby names them); a kenten's kind is one attribute and its look is several. Taking the
	  attributes off a paragraph and writing the incoming ones back would leave the reading right
	  and throw away every look the reader had set on the parts nobody edited - which is exactly
	  what the import exists NOT to do (KCMStoryHtml.h: "so that the ruby and the kenten on the
	  parts nobody edited are still there afterwards").

	★**THE ORDER IS ALL THE CLEARS AND THEN ALL THE APPLIES**, and the caller must keep it: two
	  spans can overlap - a reading that grew covers where the old one stood - and an apply that
	  ran before the clear beside it would be wiped by it. Nothing has to be written back to front,
	  though, the way text does: an attribute never changes how many characters there are.

	⚠**fGroup IS NOT COMPARED**, exactly as SpansDiffer does not compare it (2026-09-12, the user's
	  decision: mono turned into group is not a change). An applied span carries whatever fGroup it
	  came with, because a span being written has to say which it is.

	⚠**PAIRING CONSUMES A MATCH**, so the same value standing in two places is two spans: a
	  paragraph reading ねこ twice, with one of them since taken off, must not pair both of its
	  spans against the one that is left.

	★ORDER WITHIN A LIST IS NOT A DIFFERENCE HERE, though it is one to SpansDiffer above. That one
	  answers "did anything change?"; this one answers "what do I write?", and for the same spans
	  listed in another order the answer is: nothing.

	@param doc the spans the document carries now.
	@param file the spans it should carry - the reader's edited file.
*/
inline void PlanSpanChanges(const KCMAttrSpanList& doc, const KCMAttrSpanList& file,
							KCMAttrSpanList& outClear, KCMAttrSpanList& outApply)
{
	outClear.clear();
	outApply.clear();

	// ⚠std::vector<char> rather than <bool>: the bool specialisation is a bitfield and this header
	//   is built by a harness outside InDesign as well as by the plug-in.
	std::vector<char> paired(file.size(), 0);

	for (size_t i = 0; i < doc.size(); ++i)
	{
		bool16 matched = kFalse;
		for (size_t k = 0; k < file.size() && !matched; ++k)
		{
			if (paired[k] != 0)
				continue;
			if (doc[i].fStart == file[k].fStart && doc[i].fLen == file[k].fLen
				&& doc[i].fValue == file[k].fValue)
			{
				paired[k] = 1;
				matched = kTrue;
			}
		}
		if (!matched)
			outClear.push_back(doc[i]);
	}

	for (size_t k = 0; k < file.size(); ++k)
	{
		if (paired[k] == 0)
			outApply.push_back(file[k]);
	}
}

}	// namespace KCMParaText

#endif // __KCMParaText_h__

// End, KCMParaText.h.
