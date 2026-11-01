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

#include <algorithm>
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
		as five - would have had to be found a second time. (The reader is KCMTextRead::ScanCharMarks
		now, reading kTAKentenKindBoss off the character attribute strand; the rule is the same.)
		@warning the value is a KIND ("BlackCircle"), never something a reader reads aloud. Whoever
		 draws it asks Change::fAttrKind first; see the note on KCMAttrSpan::fValue. */
	KCMAttrSpanList	fKenten;

	/** ★**FOOTNOTE AND ENDNOTE MARKERS, CARRIED AS IF THEY WERE MARKS OVER THE TEXT**
		(2026-09-08, user's request: "the page shows a 1 above the character - show it in the row
		the way ruby is shown").

		⚠**THEY ARE NOT ATTRIBUTES AND THAT IS THE POINT.** A reference is a CHARACTER standing in
		 the text (U+0004, a footnote's and an endnote's alike), not something laid over one. Read
		 as text it is invisible, so the panel showed a "□" and the reader could not tell what had
		 happened - which is exactly what the user reported. Carried here, the difference comes out
		 as one attribute change with the note's NUMBER above the words, the way the page prints it.

		★**THE VALUE IS THE NUMBER InDesign ITSELF WOULD PRINT**, taken from IFootnoteNumber rather
		 than counted here, so a document that restarts its numbering per page or per section still
		 agrees with the row (KCMTextRead::ScanNotes).

		⚠★★★**THE SPAN SITS ON A CHARACTER BESIDE THE MARKER**, one character long: the one before it, or - a marker the paragraph
		 BEGINS with has none - the paragraph's first (KCMTextRead's TakeNotesFor; measured 2026-11-01: a footnote at a paragraph's
		 start was reported by its words alone, an endnote not at all). It has to be a character: ReadStory TAKES the marker out of
		 the text (U+0004 is not in fText), and a span over no text is not a span - so a paragraph with no character has none. The
		 one before is also where the page puts the number: at the top right of the word the note hangs off.
		 ⚠Nothing reads the marker's PLACE from this - the exports take it from the owned items (KCMStoryTextExport.cpp).
		@warning an ENDNOTE's own words are NOT here: they live in another story
		 (kEndnoteStoryBoss), which arrives as a story row of its own. Only the marker is reported. */
	KCMAttrSpanList	fFootnote;
	KCMAttrSpanList	fEndnote;

	/** ★**WARICHU AND TATE-CHU-YOKO** (2026-09-16, user's request): the manual ON/OFF of each,
		kTAWarichuAttrBoss / kTATatechuyokoAttrBoss, character attributes (KCMTextRead's
		ScanCharMarks). Measured the same day: one character can carry both - a tate-chu-yoko
		inside a warichu, and the other way round - so they are two lists, not one.
		★★**THE VALUE IS THE CHARACTERS THE SPAN COVERS**, not a flag. That is what the row lifts onto
		 its own line, and it is what makes rewriting the words inside one a change of that kind as
		 well as a change of the text.
		 ⇒ **Filled from the paragraph's own text** (SetSpanValuesToText, in ClosePara), so a table's
		   uncounted characters never land in it and the value is exactly what the row shows. */
	KCMAttrSpanList	fWarichu;
	KCMAttrSpanList	fTcy;

	/** ★**AN OBJECT PLACED IN THE TEXT** (2026-10-27, the author: "can you show it? into 2.0 if you can"): a span of
		one character on the object's anchor (U+FFFC, which the text counts), its value "<what it is>\t<fingerprint>"
		(KCMObjectPrint.h). Two values that differ are a change of the object's CONTENT - a MathML, a fill, a QR code made
		anew - while the anchor character, the same either way, says nothing.
		⚠**FILLED ONLY WHEN THE READER IS ASKED TO** (KCMTextRead::ReadStory's `withObjects` - the Story mode's comparison
		 of a pair that shares UIDs). Every other reader of a story leaves it empty: a fingerprint is a SaveAll of every
		 object, and nothing else needs one. */
	KCMAttrSpanList	fObject;

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
		  the order the document keeps them in (KCMTableShape.cpp, EarlierBlock). */
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

	/** How many of those positions stand BEFORE the paragraph's first character - a table or a note
		reference the paragraph begins with (2026-09-17, the import matrix's A17).
		⚠**THEY ARE NOT IN fUncountedAt AND NOT IN THE PARAGRAPH'S START**: the reader moves the start
		 past them (KCMTextRead), so ModelOffsetInParagraph never meets them and they stand directly
		 BEHIND the start in the model. Only the pairing of objects across two versions asks for them
		 (RunObjectOffsets) - without them "表の前の文 [table] 表の後の文" emptied to "[table]" had one object
		 on one side and none on the other, and could not be cut. */
	int32				fLeadingUncounted;

	/** How far behind the paragraph's start the first note reference it BEGINS with stands; 0 when it begins with none. What a row on that
		reference takes in (KCMStoryDiffRun's AddAttrChange). ⚠**NOT fLeadingUncounted**, which counts a table as well: a footnote after the first
		word behind a table the paragraph begins with sits on offset 0 too, and its row reached back over the table's anchor (measured 2026-11-02). */
	int32				fLeadingNoteBack;

	/** Where each TABLE stands in this paragraph - its anchor (kTextChar_Table) alone, one per table, not the row
		continuations - as offsets in the text's own count, ascending; a table the paragraph BEGINS with stands at 0
		(2026-10-26, the import matrix's X06/Y06).
		★★**A TABLE'S PLACE IS PART OF THE PARAGRAPH.** "表の前の文 [table] 表の後の文" and "表の前の文表の後の文 [table]" read
		  the same words, and the paragraph diff took them for the same paragraph: words moved across a table came out
		  as no change at all (ParagraphKey and RunTableAnchors are what read this - the import's own planner has asked
		  the same question since 2026-09-24, KCMStorySync's ComparePara).
		⚠**fUncountedAt cannot answer it**: it holds a note's marker as well as a table's characters, and a table's
		 rows as well as its anchor - a row added would read as a table that moved. */
	std::vector<int32>	fTableAnchorsAt;

	KCMParaAttrs()
		: fTableOrdinal(kNotACell), fCellRow(-1), fCellCol(-1),
		  fFootnoteOrdinal(kNotAFootnote), fLeadingUncounted(0), fLeadingNoteBack(0) {}

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

/** One table standing in a paragraph, as the DOCUMENT has it (2026-09-17): the text offset it stands
	at, and its anchor's model offset from the paragraph's start. ⚠**A table standing at the very start
	of a paragraph has a NEGATIVE model offset**: the reader moves the paragraph's start past the
	table's own characters (KCMTextRead), so the anchor is behind it. */
struct KCMTableInPara
{
	int32	fTextOffset;
	int32	fModelOffset;
};

/** One piece of a text change, cut where an OBJECT stands (CutChangeAtObjects).
	A and B are the two versions: [fAStart, +fACount) and [fBStart, +fBCount), in their texts' counts. */
struct KCMObjectPiece
{
	int32	fAStart;
	int32	fACount;
	int32	fBStart;
	int32	fBCount;
	/** How many of the objects stand BEFORE this piece - the same number on both sides, which is what
		places a side with no characters: between object fObjectsBefore - 1 and object fObjectsBefore.
		-1 = nothing is known about the objects (the change came back whole, see the return value). */
	int32	fObjectsBefore;
};

/** Cut one text change where the objects standing in it are - tables and note references, the
	characters the text leaves out (KCMParaAttrs::fUncountedAt) - pairing the k-th object of one version
	with the k-th of the other (2026-09-17, the import matrix's G1 and G2).

	★★★**THE TEXT CANNOT SAY WHICH SIDE OF AN OBJECT THE WORDS ARE ON.** "表の前の文" [table] "表の後の文" is
	  one paragraph reading "表の前の文表の後の文" with the table at offset 5, and words typed at the END of
	  the first half and at the START of the second are one insertion at offset 5. Measured: `表の前の文追加`
	  came back as `表の前の文` [table] `追加表の後の文`; `表の前の文章` + `後の文` (A30) put 章 behind the table.
	  ★**THE OTHER VERSION KNOWS**: its own object stands at an offset of ITS text, so the words before
	  that offset belong before the object.
	★★**AND A CHANGE ACROSS AN OBJECT IS SEVERAL CHANGES.** Deleting "文表" around a table is two
	  deletions with the table left between them; taken as one, its range holds the table's anchor and
	  the write is refused - rightly, since removing the anchor removes the table.

	Each object is BEFORE the change (at or before its start on both sides), AFTER it (at or after its
	end on both sides) or IN it (within both ranges) - and one standing in it cuts it. A piece with no
	characters on either side is left out.
	@param aObjects / bObjects the objects' text offsets in each version, ascending. Several at one offset
	  are fine (a table of three rows is three).
	@return kFalse when the objects cannot be paired with the change - the counts differ, or an object
	  stands on different sides of it in the two versions (it moved). `out` then holds the change whole
	  with fObjectsBefore -1, which is exactly what was done before this existed. */
inline bool16 CutChangeAtObjects(const std::vector<int32>& aObjects, const std::vector<int32>& bObjects,
								 int32 aStart, int32 aCount, int32 bStart, int32 bCount,
								 std::vector<KCMObjectPiece>& out)
{
	out.clear();
	KCMObjectPiece whole;
	whole.fAStart = aStart;
	whole.fACount = aCount;
	whole.fBStart = bStart;
	whole.fBCount = bCount;
	whole.fObjectsBefore = -1;

	if (aObjects.size() != bObjects.size())
	{
		out.push_back(whole);
		return kFalse;
	}

	const int32 aEnd = aStart + aCount;
	const int32 bEnd = bStart + bCount;
	int32 before = 0;
	std::vector<size_t> inside;
	int stage = 0;		// 0 = before, 1 = in, 2 = after - and it never goes back
	for (size_t j = 0; j < aObjects.size(); ++j)
	{
		const int32 a = aObjects[j];
		const int32 b = bObjects[j];
		int where = -1;
		if (a <= aStart && b <= bStart)
			where = 0;
		else if (a >= aEnd && b >= bEnd)
			where = 2;
		else if (a >= aStart && a <= aEnd && b >= bStart && b <= bEnd)
			where = 1;
		if (where < stage)
		{
			out.push_back(whole);
			return kFalse;
		}
		stage = where;
		if (where == 0)
			++before;
		else if (where == 1)
			inside.push_back(j);
	}

	int32 aFrom = aStart;
	int32 bFrom = bStart;
	for (size_t k = 0; k <= inside.size(); ++k)
	{
		const int32 aTo = (k < inside.size()) ? aObjects[inside[k]] : aEnd;
		const int32 bTo = (k < inside.size()) ? bObjects[inside[k]] : bEnd;
		if (aTo > aFrom || bTo > bFrom)
		{
			KCMObjectPiece piece;
			piece.fAStart = aFrom;
			piece.fACount = aTo - aFrom;
			piece.fBStart = bFrom;
			piece.fBCount = bTo - bFrom;
			piece.fObjectsBefore = before + static_cast<int32>(k);
			out.push_back(piece);
		}
		aFrom = aTo;
		bFrom = bTo;
	}
	return kTrue;
}

/** How many of the objects standing exactly AT `textOffset` a position there must stay in front of,
	when `objectsBefore` objects are to stand before it (a KCMObjectPiece's fObjectsBefore).

	★A START is asked with ModelOffsetInParagraph / IndexInStory, which answer "after everything
	  standing there"; this is how far to step back from that. 0 when nothing is known (-1), and never
	  past the objects standing exactly there - the answer is clamped, so an inconsistent count cannot
	  put a position inside the words before them.
	@param objects the objects' text offsets on this side, ascending. */
inline int32 ObjectsToStepBack(const std::vector<int32>& objects, int32 textOffset, int32 objectsBefore)
{
	if (objectsBefore < 0)
		return 0;
	int32 atOrBefore = 0;
	int32 strictlyBefore = 0;
	for (size_t k = 0; k < objects.size(); ++k)
	{
		if (objects[k] <= textOffset)
			++atOrBefore;
		if (objects[k] < textOffset)
			++strictlyBefore;
	}
	int32 wanted = objectsBefore;
	if (wanted < strictlyBefore)
		wanted = strictlyBefore;
	if (wanted > atOrBefore)
		wanted = atOrBefore;
	return atOrBefore - wanted;
}

/** A text range of one paragraph, [textStart, textStart + textLen), as a range of the document's count - for a
	WRITE over those characters (a ruby, a kenten, a tate-chu-yoko, a warichu).

	★★**THE END IS "JUST PAST THE LAST CHARACTER" - ModelOffsetInParagraph(last) + 1** (2026-09-25, the Word round trip
	  re-check, item 3). ModelOffsetInParagraph answers for a START, so asked for the end it took in whatever stood right
	  after the range: a table's anchor and its rows, or a NOTE'S MARKER - a kenten put on the two characters in front
	  of a footnote reference fell on the reference as well, and KCMTextRead, which does not count the marker, could
	  never see it had. ApplyParagraph has crossed a write's end this way since 2026-09-17; the attribute writers asked
	  the other question until this day. ⚠A range reaching ACROSS such a position still covers it - it stands between
	  two of the range's own characters, and nothing can be written around it.
	@param textLen 0 or less: nothing, at textStart's place (outLen 0). */
inline void ModelRangeInParagraph(const KCMParaAttrs& attrs, int32 textStart, int32 textLen, int32& outFrom, int32& outLen)
{
	outFrom = ModelOffsetInParagraph(attrs, textStart);
	outLen = (textLen > 0) ? ModelOffsetInParagraph(attrs, textStart + textLen - 1) + 1 - outFrom : 0;
}

/** The two kinds of object that stand BETWEEN a paragraph's characters without being counted in its text (KCMParaAttrs::
	fUncountedAt): a table (its anchor, and one continuation per row after the first) and a note's marker (a footnote's
	reference, an endnote's). For InsertBeforeObject. */
const int32 kObjectTable = 0;
const int32 kObjectNote = 1;

/** Where words put in at one text offset go among the objects standing EXACTLY there (2026-09-25, the Word round trip
	re-check, item 1).

	★★**THE TEXT CANNOT SAY WHICH SIDE OF A NOTE'S REFERENCE NEW WORDS ARE ON, AND THE VERSION BEING WRITTEN CAN** - the
	  same thing CutChangeAtObjects says of a table. Word puts words typed where a reference stands IN FRONT of it
	  (measured 2026-09-23), and the comparison plans it so (KCMStorySync's MapPastChangeAt); the writer put every
	  insertion after everything standing there (ModelOffsetInParagraph), so "である¹" + "とされ" typed before the ¹ came
	  out "である¹とされ" - the note's number moved into the new words, and nothing said so.
	@param kinds the objects standing there, in the MODEL'S order (kObjectTable / kObjectNote).
	@param tablesBefore how many of the TABLES there stand before the words in the version being written; -1 = not known
		(CutChangeAtObjects could not pair them): after every one, which is what was done before this existed.
	@param notesBefore how many of the NOTE MARKERS there stand before the words.
	@return the index into `kinds` the words go in front of - kinds.size() is after all of them - or -1 when no one
		place gives both counts (a marker that must precede stands behind a table that must follow, or the other way:
		the caller refuses rather than guess). */
inline int32 InsertBeforeObject(const std::vector<int32>& kinds, int32 tablesBefore, int32 notesBefore)
{
	int32 tables = 0;
	int32 notes = 0;
	for (size_t k = 0; k < kinds.size(); ++k)
	{
		if (kinds[k] == kObjectTable)
			++tables;
		else
			++notes;
	}
	const int32 wantTables = (tablesBefore < 0 || tablesBefore > tables) ? tables : tablesBefore;
	const int32 wantNotes = (notesBefore < 0) ? 0 : ((notesBefore > notes) ? notes : notesBefore);

	// Each step adds one to one of the two counts, so the place where both are met - if there is one - is the only one.
	int32 seenTables = 0;
	int32 seenNotes = 0;
	for (size_t k = 0; k <= kinds.size(); ++k)
	{
		if (seenTables == wantTables && seenNotes == wantNotes)
			return static_cast<int32>(k);
		if (k == kinds.size() || seenTables > wantTables || seenNotes > wantNotes)
			break;
		if (kinds[k] == kObjectTable)
			++seenTables;
		else
			++seenNotes;
	}
	return -1;
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

/** What a row shows for one character of a story's text: the code point to draw, or -1 for nothing. ONE TABLE FOR THE STORY ROWS AND
	THE TRACK ROWS (KCMTrackRead's AppendShown) - and DISPLAY ONLY: what a comparison or a write works on keeps every character.

	★**A CHARACTER THAT DRAWS NOTHING READS AS NOTHING HAVING HAPPENED**, so what can change gets a sign of its own, and it is decided
	  here, once (the excerpt reaches the reader on the row and in the message area). Measured on the panel 2026-11-01: a page
	  number and a section name drew a box, and a tab sent the words after it past the cell's end.
	  - a paragraph end, a forced line break: the marks InDesign itself draws with Show Hidden Characters on (a pilcrow, a return arrow)
	  - an anchored object: an anchor
	  - a tab and a right indent tab: an arrow
	  - a page number or a text variable (one code for all of them - memory indesign-special-text-characters), a section name: a sign
	    that stands for what the page prints there
	  - a glyph with no character of its own (the special glyphs): a box, which is what it is
	⚠**THE MARKS THAT TAKE NO ROOM ON THE PAGE TAKE NONE HERE**, the same list as a story row's first words (KCMStoryList's RowCharOf):
	 the XML tag's and a note's mark, an index marker, a discretionary hyphen, a zero-width space - and the other control
	 characters InDesign stands in the text (the end of a nested style, indent to here), which draw nothing.
	⚠A character of the private use area is left alone: that is where a gaiji font lives. */
inline int32 ShownCodePoint(int32 cp)
{
	switch (cp)
	{
		case 0x000D:	return 0x00B6;		// PILCROW SIGN
		case 0x000A:	return 0x21B5;		// DOWNWARDS ARROW WITH CORNER LEFTWARDS
		case 0xFFFC:	return 0x2693;		// ANCHOR
		case 0x0009:
		case 0x0008:	return 0x2192;		// RIGHTWARDS ARROW
		case 0x0018:	return 0x2116;		// NUMERO SIGN
		case 0x0019:	return 0x00A7;		// SECTION SIGN
		case 0x001A:
		case 0xFFFD:	return 0x25A1;		// WHITE SQUARE
		case 0xFEFF:
		case 0xE02C:
		case 0x00AD:
		case 0x200B:	return -1;
		default:		return (cp < 0x0020) ? -1 : cp;
	}
}

/** A piece of a story's text as a row shows it (ShownCodePoint). DISPLAY ONLY: what comes out no longer matches the text it came from. */
inline std::string MarkUpForRow(const std::string& utf8)
{
	std::string out;
	out.reserve(utf8.size() + 8);
	for (size_t i = 0; i < utf8.size(); )
	{
		const unsigned char lead = static_cast<unsigned char>(utf8[i]);
		const size_t length = (lead < 0x80) ? 1 : ((lead & 0xE0) == 0xC0) ? 2 : ((lead & 0xF0) == 0xE0) ? 3 : ((lead & 0xF8) == 0xF0) ? 4 : 1;
		int32 cp = (length == 1) ? lead : (length == 2) ? (lead & 0x1F) : (length == 3) ? (lead & 0x0F) : (lead & 0x07);
		for (size_t k = 1; k < length && i + k < utf8.size(); ++k)
			cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
		const int32 shown = ShownCodePoint(cp);
		if (shown == cp)
			out.append(utf8, i, length);
		else if (shown >= 0)
			AppendUtf8(out, shown);
		i += length;
	}
	return out;
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
	@param present whether this side carries the mark. */
inline void PlanLayers(bool16 changedIsWarichu, const KCMAttrSpanList& warichu,
					   const KCMAttrSpanList& tcy, int32 start, int32 len, bool16 present,
					   LayerPlan& out)
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

/** The character JoinParagraphs puts between two paragraphs of a run: a RETURN, which no paragraph holds - the reader closes a
	paragraph at its return (KCMTextRead::ReadStory).
	★**A FORCED LINE BREAK (U+000A) STANDS INSIDE ITS PARAGRAPH, AND THIS IS WHAT TELLS THE TWO APART.** The join used U+000A until
	2026-11-01, so a forced line break read as a paragraph end on the row (measured: "one two" -> "one" LF "two" drew a pilcrow) and
	stopped a merge a paragraph end has to stop (KCMTextDiff::MergeNearbyChanges's doNotSwallow) although writing across one takes
	no paragraph's style with it. */
const int32 kJoinedParagraphBreak = 0x000D;

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
			AppendUtf8(out, kJoinedParagraphBreak);
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

/** The place one paragraph stands in, as a region of its own. A paragraph the list does not reach is in the body. */
inline ParaRegion PlaceOfParagraph(const std::vector<KCMParaAttrs>& attrs, int32 i)
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
	return here;
}

/** The places a run of paragraphs passes through, in order. */
inline void ParagraphRegions(const std::vector<KCMParaAttrs>& attrs, int32 start, int32 count,
							 std::vector<ParaRegion>& out)
{
	out.clear();
	for (int32 i = start; i < start + count; ++i)
	{
		const ParaRegion here = PlaceOfParagraph(attrs, i);
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

	★**A LIST, NOT A RANGE, AND EACH ENTRY IS ONE OF TextChar.h's.** Footnote and endnote reference
	  (0004 for both; 0005 is only TextChar.h's name for the second), table anchor and row
	  continuation (0016/0017), page number and every text
	  variable (0018 - measured to be the one code they all share in a document), section name
	  (0019), the special glyphs (001A, FFFD - IsSpecialGlyphCharacter), the XML tag and note anchor mark
	  (FEFF), an anchored object (FFFC) and the index marker (E02C).
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
		case 0xFFFD:	// kTextChar_SpecialGlyph
		case 0xE02C:	// kTextChar_IndexMarker
			return kTrue;
		default:
			return kFalse;
	}
}

/** kTrue when cp is the place holder of a glyph with no character of its own: the glyph is in the kTextAttrSpecialGlyphBoss on it
	(SnpInsertGlyph.cpp's OPTION 5). Written back as text it comes back with no glyph (measured: U+FFFD alone) - and a COPY carries the
	attribute and puts it back whole, with nothing to make anew under a new UID (measured), so this is the one object's character a copy
	need not refuse.
	⚠U+FFFD is TextChar.h's "an incoming character whose value is unrepresentable in Unicode" as well, which the list above would turn
	away for nothing - but neither a story's contents nor an insertion point takes one (measured: the character is dropped). */
inline bool16 IsSpecialGlyphCharacter(int32 cp)
{
	return (cp == 0x001A || cp == 0xFFFD) ? kTrue : kFalse;
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
	@param count how many paragraphs the run covers - never 0: paragraphs on one side only do not come here
		(KCMStoryDiffRun's AddWholeParagraphs), and such a run answers 0.
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
						  int32 start, int32 count, int32 joinedOffset)
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

	// Past the end of the run: the last paragraph's end.
	const int32 last = start + count - 1;
	if (count > 0 && last >= 0 && last < static_cast<int32>(paragraphs.size())
		&& last < static_cast<int32>(starts.size()))
		return starts[last] + ModelOffsetInParagraph(AttrsOfParagraph(attrs, last),
													 CountCodePoints(paragraphs[last]));
	return 0;
}

/** Where paragraph `which` really begins in the document - before a table or a note reference it starts
	with, which its recorded start stands after (KCMParaAttrs::fLeadingUncounted). */
inline int32 ParagraphLineStart(const std::vector<int32>& starts, const std::vector<KCMParaAttrs>& attrs,
								int32 which)
{
	return starts[static_cast<size_t>(which)] - AttrsOfParagraph(attrs, which).fLeadingUncounted;
}

/** Where paragraph `which`'s RETURN stands in the document: after its words and anything standing at
	their end (2026-09-17 afternoon - a whole paragraph added or removed is cut at returns). */
inline int32 ParagraphReturn(const std::vector<std::string>& paragraphs, const std::vector<int32>& starts,
							 const std::vector<KCMParaAttrs>& attrs, int32 which)
{
	return starts[static_cast<size_t>(which)]
		   + ModelOffsetInParagraph(AttrsOfParagraph(attrs, which),
									CountCodePoints(paragraphs[static_cast<size_t>(which)]));
}

/** kTrue when paragraph i of one version and paragraph j of the other stand in the same place (the body,
	the same cell, the same note). */
inline bool16 ParagraphsSharePlace(const std::vector<KCMParaAttrs>& attrsA, int32 i,
								   const std::vector<KCMParaAttrs>& attrsB, int32 j)
{
	return PlaceOfParagraph(attrsA, i).SamePlaceAs(PlaceOfParagraph(attrsB, j));
}

/** The objects standing inside a RUN of paragraphs - every paragraph's fUncountedAt - as offsets into
	JoinParagraphs' answer, ascending (2026-09-17: what CutChangeAtObjects pairs across two versions).
	★The ones a paragraph BEGINS with (fLeadingUncounted) are at its offset 0 and come first. They stand
	 behind the paragraph's start in the model, which is exactly where ObjectsToStepBack steps back to
	 from IndexInStory's answer for that offset. */
inline void RunObjectOffsets(const std::vector<std::string>& paragraphs,
							 const std::vector<KCMParaAttrs>& attrs,
							 int32 start, int32 count, std::vector<int32>& out)
{
	out.clear();
	int32 joined = 0;
	for (int32 i = 0; i < count; ++i)
	{
		const int32 which = start + i;
		if (which < 0 || which >= static_cast<int32>(paragraphs.size()))
			break;
		const KCMParaAttrs& para = AttrsOfParagraph(attrs, which);
		for (int32 k = 0; k < para.fLeadingUncounted; ++k)
			out.push_back(joined);
		for (size_t k = 0; k < para.fUncountedAt.size(); ++k)
			out.push_back(joined + para.fUncountedAt[k]);
		joined += CountCodePoints(paragraphs[which]) + 1;
	}
}

/** The TABLES standing inside a RUN of paragraphs - every paragraph's fTableAnchorsAt - as offsets into JoinParagraphs'
	answer, ascending (2026-10-26, the import matrix's X06/Y06): the marks the character pass cuts at
	(KCMTextDiff::DiffCharacters), one per table. */
inline void RunTableAnchors(const std::vector<std::string>& paragraphs, const std::vector<KCMParaAttrs>& attrs,
							int32 start, int32 count, std::vector<int32>& out)
{
	out.clear();
	int32 joined = 0;
	for (int32 i = 0; i < count; ++i)
	{
		const int32 which = start + i;
		if (which < 0 || which >= static_cast<int32>(paragraphs.size()))
			break;
		const KCMParaAttrs& para = AttrsOfParagraph(attrs, which);
		for (size_t k = 0; k < para.fTableAnchorsAt.size(); ++k)
			out.push_back(joined + para.fTableAnchorsAt[k]);
		joined += CountCodePoints(paragraphs[which]) + 1;
	}
}

/** What the paragraph diff compares a paragraph BY (2026-10-26, the import matrix's X06/Y06): its words - and, when a
	table stands in it, where the tables stand (KCMParaAttrs::fTableAnchorsAt), so that the same words with a table moved
	are another paragraph. A paragraph without a table is its words, exactly as before.
	⚠The places follow a U+0000, which no paragraph's text holds: the words cannot run on into them. */
inline std::string ParagraphKey(const std::string& text, const KCMParaAttrs& attrs)
{
	if (attrs.fTableAnchorsAt.empty())
		return text;
	std::string key = text;
	key.push_back('\0');
	for (size_t k = 0; k < attrs.fTableAnchorsAt.size(); ++k)
	{
		if (k > 0)
			key.push_back(',');
		key += std::to_string(attrs.fTableAnchorsAt[k]);
	}
	return key;
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

/** One run of a character diff of two versions of a paragraph: [fAStart, +fACount) of version A stands as [fBStart, +fBCount)
	of version B, in code points. In order, and not touching (what KCMTextDiff::Diff answers). */
struct KCMCharChange
{
	int32	fAStart;
	int32	fACount;
	int32	fBStart;
	int32	fBCount;
};

/** What became of one character of version A in version B. */
enum KCMCharFate
{
	kKCMCharSurvives,		///< it stands in B: outAt is where
	kKCMCharRewritten,		///< other words stand in its place: outAt is the character as many along among them, or the last of them
	kKCMCharGone			///< nothing stands in its place: outAt is the place it left
};

inline KCMCharFate FateOfCharacter(const std::vector<KCMCharChange>& changes, int32 at, int32& outAt)
{
	int32 shift = 0;
	for (size_t c = 0; c < changes.size(); ++c)
	{
		if (at < changes[c].fAStart)
			break;
		if (at < changes[c].fAStart + changes[c].fACount)
		{
			if (changes[c].fBCount <= 0)
			{
				outAt = changes[c].fBStart;
				return kKCMCharGone;
			}
			const int32 along = at - changes[c].fAStart;
			outAt = changes[c].fBStart + ((along < changes[c].fBCount) ? along : changes[c].fBCount - 1);
			return kKCMCharRewritten;
		}
		shift = (changes[c].fBStart + changes[c].fBCount) - (changes[c].fAStart + changes[c].fACount);
	}
	outAt = at + shift;
	return kKCMCharSurvives;
}

/** One mark of the two versions and what is to be said of it: a pair (both indices), or a mark one version alone has (the other
	index -1), with the place its characters stand in the other version, or the words that took their place. */
struct KCMMarkPair
{
	int32	fA;
	int32	fB;
	int32	fPlaceStart;
	int32	fPlaceLen;

	KCMMarkPair(int32 a, int32 b, int32 placeStart, int32 placeLen) : fA(a), fB(b), fPlaceStart(placeStart), fPlaceLen(placeLen) {}
};

/** Where the characters of a mark stand in the other version, or the words that took their place.
	@return kFalse when nothing does - every character of it is simply gone (the head of PairMarksAcross says why). */
inline bool16 PlaceOfMarkAlone(const std::vector<KCMCharChange>& changes, const KCMAttrSpan& span, int32& outStart, int32& outLen)
{
	outStart = 0;
	outLen = 0;
	bool16 placed = kFalse;
	int32 last = 0;
	for (int32 k = 0; k < span.fLen; ++k)
	{
		int32 at = 0;
		if (FateOfCharacter(changes, span.fStart + k, at) == kKCMCharGone)
			continue;
		if (!placed)
			outStart = at;
		last = at;
		placed = kTrue;
	}
	if (placed)
		outLen = last + 1 - outStart;
	return placed;
}

/** What is to be said of the marks of one kind over two versions of a paragraph, in the order of their places in version B.
	@param changes the character diff of the two; empty for a paragraph whose words are the same.

	★★★**THE TEXT IS THE SUBJECT, THE MARKS ITS ATTENDANTS** (the author's rules, 2026-09-01), and what the character diff says of a mark's
	  characters is what decides:
	  - **the same mark over the same characters, wherever they now stand, is nothing** (rewrite the words before a ruby and it has merely moved -
	    measured 2026-09-08: "one edit came out as three rows");
	  - **a mark both versions have over the place is compared**, whether or not the characters under it changed ("when the ruby changes AND the
	    kanji under it changes, report both");
	  - **a mark whose characters are all simply GONE is no change of its own**: "the deletion row already says so" (銀河 deleted with its reading);
	  - **a mark whose characters were REWRITTEN is one** ("when the kanji changes and the ruby is removed, I want two rows" - 琥珀 -> 真珠);
	  - **a mark taken off characters that stay is one**, and its place in the other version is where they stand; off characters rewritten, the
	    words that took their place.
	★**ASKED OF THE DIFF SINCE 2026-11-01.** Until then a filter asked whether the characters could be found anywhere in the other paragraph and
	  told a rewrite from a deletion by the paragraph's length, and the marks were then paired by their offsets, which no longer name the same
	  characters once the words have moved. Measured wrong four ways: 銀河 twice, the first deleted, reported the SECOND's reading removed; a reading changed
	  behind words of another length came out removed and added; 琥珀 -> 真 with its reading gone lost the second row; 銀河 deleted with words typed
	  elsewhere gained one.
	★★**A MARK WHOSE FIRST CHARACTER STAYED CLAIMS ITS PARTNER FIRST, AND A MARK WITH NOTHING LEFT CLAIMS NONE.** The place a deleted character leaves is
	  where what follows it now begins, so a word deleted right in front of another marked word stands on that word's mark - measured: 銀河鉄道 -> 鉄道, or the 琥
	  of a mono ruby, was three rows for one deletion, the first reading "changed" into the second and the second "removed".
	⚠The spans of two versions are each in their own count; a pair is found by carrying A's start across the diff. A first character that went
	 leaves the place it left, which is where a mark standing on what follows begins; one rewritten stands as many along among the words that took its place,
	 so that each reading of a mono ruby is compared with its own. */
inline void PairMarksAcross(const std::vector<KCMCharChange>& changes, const KCMAttrSpanList& a, const KCMAttrSpanList& b,
							std::vector<KCMMarkPair>& out)
{
	out.clear();
	std::vector<char> taken(b.size(), 0);
	const auto claim = [&](int32 place) -> int32
	{
		for (size_t k = 0; k < b.size(); ++k)
		{
			if (taken[k] == 0 && b[k].fStart == place)
			{
				taken[k] = 1;
				return static_cast<int32>(k);
			}
		}
		return -1;
	};
	std::vector<int32> partners(a.size(), -1);
	for (size_t i = 0; i < a.size(); ++i)
	{
		int32 mapped = 0;
		if (FateOfCharacter(changes, a[i].fStart, mapped) == kKCMCharSurvives)
			partners[i] = claim(mapped);
	}
	for (size_t i = 0; i < a.size(); ++i)
	{
		int32 start = 0;
		int32 len = 0;
		const bool16 placed = PlaceOfMarkAlone(changes, a[i], start, len);
		int32 mapped = 0;
		if (placed && FateOfCharacter(changes, a[i].fStart, mapped) != kKCMCharSurvives)
			partners[i] = claim(mapped);
		if (partners[i] >= 0)
			out.push_back(KCMMarkPair(static_cast<int32>(i), partners[i], 0, 0));
		else if (placed)
			out.push_back(KCMMarkPair(static_cast<int32>(i), -1, start, len));
	}

	// The marks B alone has: the same question, asked the other way across the diff.
	std::vector<KCMCharChange> reversed(changes);
	for (size_t c = 0; c < reversed.size(); ++c)
	{
		std::swap(reversed[c].fAStart, reversed[c].fBStart);
		std::swap(reversed[c].fACount, reversed[c].fBCount);
	}
	for (size_t k = 0; k < b.size(); ++k)
	{
		int32 start = 0;
		int32 len = 0;
		if (taken[k] == 0 && PlaceOfMarkAlone(reversed, b[k], start, len))
			out.push_back(KCMMarkPair(-1, static_cast<int32>(k), start, len));
	}

	// In the order of their places in B.
	std::stable_sort(out.begin(), out.end(), [&b](const KCMMarkPair& x, const KCMMarkPair& y)
	{
		const int32 xAt = (x.fB >= 0) ? b[static_cast<size_t>(x.fB)].fStart : x.fPlaceStart;
		const int32 yAt = (y.fB >= 0) ? b[static_cast<size_t>(y.fB)].fStart : y.fPlaceStart;
		return xAt < yAt;
	});
}

/** Do a mark of each version stand over the same characters: every character under the one that stayed in the other version stands under the
	other, both ways? What came or went with the words is the text row's to say.
	★**WHY A LENGTH DOES NOT ANSWER IT WHEN THE WORDS CHANGED** (measured 2026-11-01): one character deleted or typed inside five with one kenten made
	the two spans differ in length, and a row of its own read BlackCircle against BlackCircle - nothing a reader could use, for one keystroke the text
	row had already said. Over words that are the same it is exactly the length, which is the whole point there (琥珀 read as こ+はく against こはく). */
inline bool16 MarksCoverSameCharacters(const std::vector<KCMCharChange>& changes, const KCMAttrSpan& a, const KCMAttrSpan& b)
{
	const auto staysUnder = [](const std::vector<KCMCharChange>& across, const KCMAttrSpan& from, const KCMAttrSpan& over) -> bool16
	{
		for (int32 k = 0; k < from.fLen; ++k)
		{
			int32 at = 0;
			if (FateOfCharacter(across, from.fStart + k, at) == kKCMCharSurvives && (at < over.fStart || at >= over.fStart + over.fLen))
				return kFalse;
		}
		return kTrue;
	};
	std::vector<KCMCharChange> reversed(changes);
	for (size_t c = 0; c < reversed.size(); ++c)
	{
		std::swap(reversed[c].fAStart, reversed[c].fBStart);
		std::swap(reversed[c].fACount, reversed[c].fBCount);
	}
	return (staysUnder(changes, a, b) && staysUnder(reversed, b, a)) ? kTrue : kFalse;
}

/** How many rows the marks of one kind come to, read across this character diff: a mark one version alone has, and a pair not the same value over
	the same characters. What chooses between two equally short diffs (KCMStoryDiffRun's CharChangesOf). */
inline int32 RowsOfMarks(const std::vector<KCMCharChange>& changes, const KCMAttrSpanList& a, const KCMAttrSpanList& b)
{
	std::vector<KCMMarkPair> pairs;
	PairMarksAcross(changes, a, b, pairs);
	int32 rows = 0;
	for (size_t p = 0; p < pairs.size(); ++p)
	{
		if (pairs[p].fA < 0 || pairs[p].fB < 0
			|| a[static_cast<size_t>(pairs[p].fA)].fValue != b[static_cast<size_t>(pairs[p].fB)].fValue
			|| !MarksCoverSameCharacters(changes, a[static_cast<size_t>(pairs[p].fA)], b[static_cast<size_t>(pairs[p].fB)]))
			++rows;
	}
	return rows;
}

/** What has to be WRITTEN to turn one paragraph's attribute spans into another's: the spans to
	take off (`outClear`) and the spans to put on (`outApply`).

	★★★**A SPAN BOTH SIDES CARRY IS IN NEITHER LIST, AND THAT IS THE WHOLE REASON THIS EXISTS.** A
	  reading is three attributes and its LOOK is twenty-seven more (KCMStoryRestore.cpp's
	  ClearRuby names them); a kenten's kind is one attribute and its look is several. Taking the
	  attributes off a paragraph and writing the incoming ones back would leave the reading right
	  and throw away every look the reader had set on the parts nobody edited - which is exactly
	  what the import exists NOT to do: the ruby and the kenten on the parts nobody edited have to
	  be there afterwards, untouched (KCMStoryAttrPour.h says it at length).

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

/**	Take out of `clearTcy` every stretch a warichu of the document covers, and answer how many were
	taken out.

	★★★**WORD CANNOT HOLD A TATE-CHU-YOKO INSIDE A WARICHU, AND SAYS NOTHING WHEN IT DROPS ONE**
	  (measured against real Word 2007 on 2026-09-22 - the notes are in
	  docs/ai-notes/kcm-tcy-word-roundtrip-2026-09-22.md). One run cannot wear w:combine and
	  w:vert at once: opening and saving a story that has one is enough for w:vert to go, and it
	  goes without a revision mark, so the file comes back saying - truthfully, about itself -
	  that there is no tate-chu-yoko there. Poured as it stands, that would take the reader's own
	  tate-chu-yoko off a document nobody edited.

	★**THE DIRECTION OF THE TEXT HAS NOTHING TO DO WITH IT.** The same paragraph written once
	  vertically and once horizontally, with nothing else different, loses it both ways. The one
	  condition is standing on the same run as a warichu.

	⚠**ONLY THE CLEARS ARE HELD BACK.** A tate-chu-yoko the file ASKS FOR still goes in: Word puts
	 one on by taking the run out of the warichu, and that is a real edit the reader made and can
	 see. What cannot be believed is the ABSENCE of one under a warichu.

	⚠**THE WARICHU LIST IS THE DOCUMENT'S**, not the file's: the file's warichu may have been
	 split by the very edit that is in question, while the document still stands where the export
	 left it.

	@param docWarichu the warichu spans the document carries now.
	@param clearTcy   in/out: the tate-chu-yoko spans the pour was about to take off.
*/
inline int32 KeepTcyInsideWarichu(const KCMAttrSpanList& docWarichu, KCMAttrSpanList& clearTcy)
{
	if (docWarichu.empty() || clearTcy.empty())
		return 0;

	int32 kept = 0;
	KCMAttrSpanList keepClearing;
	for (size_t i = 0; i < clearTcy.size(); ++i)
	{
		const int32 s = clearTcy[i].fStart;
		const int32 e = clearTcy[i].fStart + clearTcy[i].fLen;
		bool16 underWarichu = kFalse;
		for (size_t k = 0; k < docWarichu.size() && !underWarichu; ++k)
		{
			const int32 ws = docWarichu[k].fStart;
			const int32 we = docWarichu[k].fStart + docWarichu[k].fLen;
			underWarichu = (ws < e && s < we) ? kTrue : kFalse;		// one character of overlap is enough
		}
		if (underWarichu)
			++kept;
		else
			keepClearing.push_back(clearTcy[i]);
	}
	clearTcy.swap(keepClearing);
	return kept;
}

}	// namespace KCMParaText

#endif // __KCMParaText_h__

// End, KCMParaText.h.
