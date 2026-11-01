//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The difference between two sequences, by Myers' algorithm.
//
//  **ORIGINALLY PORTED FROM KohakuTest's KTTextDiff**, and at the time unchanged except for the
//  names. It was measured there against real documents before it was brought here -- a
//  two-character Japanese edit selected exactly those two characters, and "sleeping" -> "awake"
//  came back as one change rather than two. The record is
//  docs/ai-notes/kt-story-diff-experiment-2026-08-17.md.
//
//  ⚠**THE OTHER COPY IS KIDMCP's KIDMCPTextDiff, AND THIS ONE IS THE MASTER.** A change made here is
//   made there in the same piece of work: work/kidmcp-textdiff-sync.py compares the two with the
//   names substituted and the comments taken out, and fails when they differ.
//
//  Written from the published method -- Eugene W. Myers, "An O(ND) Difference Algorithm and Its
//  Variations", Algorithmica 1(2), 1986 -- and not from anybody's source. An algorithm is not
//  copyrightable; a particular implementation of it is, and git's (xdiff/, from LibXDiff) is
//  GPL, so none of it is here.
//
//  Everything is reduced to a sequence of int32 tokens before it gets here, which is the same
//  move git makes when it hashes each line to an integer: it means one implementation serves
//  both jobs this plug-in needs -- comparing a list of paragraphs, and comparing the characters
//  inside one paragraph -- because the two problems are the same problem at different grain.
//  Unlike a hash, the tokens are allocated from a table, so two different paragraphs can never
//  collide into looking equal.
//
//  The common head and tail are stripped before the search starts. That is not a detail: the
//  cost is O((N+M)*D) in the edit distance D, so for the case this exists for -- a long document
//  with a few edits in the middle -- stripping is what keeps D small enough to matter.
//
//========================================================================================

#ifndef __KCMTextDiff_h__
#define __KCMTextDiff_h__

#include <map>
#include <string>
#include <vector>

/** Sequence comparison. All grain sizes go through the same core.
	@ingroup KCM
*/
namespace KCMTextDiff
{
	/** One run of difference, given as a range on each side.

		A range with aCount 0 is an insertion, one with bCount 0 is a deletion, and one with
		both is a replacement. Runs are returned in order and never touch: adjacent edits are
		merged, so "changed this paragraph" arrives as one entry rather than as a deletion
		followed by an insertion.
	*/
	struct Change
	{
		int32 aStart;	///< first differing index in the baseline sequence
		int32 aCount;	///< how many baseline elements are gone; 0 for a pure insertion
		int32 bStart;	///< first differing index in the target sequence
		int32 bCount;	///< how many target elements are new; 0 for a pure deletion

		Change() : aStart(0), aCount(0), bStart(0), bCount(0) {}
	};

	/** Compares two token sequences. **There is no ceiling on the edit distance**: slower is acceptable
		here, wrong is not.
		@param a IN the baseline sequence.
		@param b IN the target sequence.
		@param changes OUT the runs of difference, in order. Emptied first.

		The search runs in LINEAR SPACE (Myers' own divide-and-conquer refinement), so distance costs
		time and not memory: **50,000 tokens against 50,000 unrelated ones needs 3.2 MB** - it keeps TWO
		rows of 4*(N+M)+7 int32s, so 400,007 entries each, 1.6 MB a row and 3.2 MB the pair.

		@warning the time is O((N+M)*D). Measured in work/textdiff-test: 8,000 vs 8,000 with one percent
		 changed = **1 ms**; 3,000 vs 3,000 sharing nothing = **84 ms**; 50,000 vs 50,000 sharing nothing
		 = **24 s**. Only the last is uncomfortable, and it is a shape real text does not take -- two
		 versions of a document share most of their characters even when every sentence was rewritten.

		@return kTrue if the comparison completed. kFalse only from a guard that cannot fire
		(FindMiddleSnake, "Unreachable"): changes is then left empty and the caller should treat the
		whole sequence as changed.
	*/
	bool16 Diff(const std::vector<int32>& a, const std::vector<int32>& b,
				std::vector<Change>& changes);

	/** The changes between a and b, found piece by piece where fixed marks cut them (S3b):
		a's piece k is [aCuts[k-1], aCuts[k]) and it is diffed only against b's piece k. The changes come
		back in whole-sequence counts, ascending. Words moved from one side of a mark to the other come
		back as an insertion on one side and a deletion on the other - which is what a table standing in
		a paragraph needs, since its anchor cannot move ("ab[T]cd" and "abcd[T]" are the same text).
		aCuts and bCuts must be the same size, ascending; no marks is a plain Diff.
		@return kFalse when the cut counts differ, a cut is out of order or out of range, or a piece's
		Diff does not complete - and then changes is empty. */
	bool16 DiffInPieces(const std::vector<int32>& a, const std::vector<int32>& aCuts,
						const std::vector<int32>& b, const std::vector<int32>& bCuts,
						std::vector<Change>& changes);

	/** The Story comparison's character pass, in one place (the import matrix's X06/Y06): Diff, then
		MergeNearbyChanges (never swallowing `doNotSwallow`), then AlignChangeBoundaries (`widenWords`) - each of the three
		inside one piece between fixed marks, the pieces cut the way DiffInPieces cuts them.
		THE MARKS ARE THE TABLES STANDING IN THE RUN (KCMParaText::RunTableAnchors): a table's anchor cannot move, so
		  "ab[T]cd" and "abcd[T]" differ by the words that crossed it - put in on one side of the table and taken out on
		  the other. Merged or slid across a mark, the two would meet into one change holding the table.
		aMarks and bMarks the same size, ascending, within their sequences; none is the whole sequence as one piece.
		@return kFalse when the marks do not pair or a piece's Diff does not complete - and then changes is empty. */
	bool16 DiffCharacters(const std::vector<int32>& a, const std::vector<int32>& aMarks,
						  const std::vector<int32>& b, const std::vector<int32>& bMarks,
						  std::vector<Change>& changes, int32 doNotSwallow, bool16 widenWords);

	/** Whether this code point shapes the character before it rather than standing on its own: a variation selector, a combining mark,
		a kana voiced mark, an emoji modifier, a zero-width joiner (memory wrservices-unicode-facts). A cut between the two draws another
		glyph, or nothing at all. */
	bool16 IsClusterExtender(int32 cp);

	/** Merges neighbouring changes that are separated by only a short unchanged run.

		Myers returns the shortest edit script, which is not the same thing as the edit a person
		would describe. Rewriting "sleeping" as "awake" comes back as two changes, because the
		letter 'e' appears in both words and counting it as unchanged makes the script one step
		shorter. Nobody reads it that way.

		The rule is the one Google's diff-match-patch calls a semantic cleanup: if the unchanged
		run between two changes is STRICTLY SHORTER than EACH of the changes around it, swallow it
		and make them one. Requiring it to be shorter than both, rather than than the larger of
		them, is what stops a long edit from dragging unrelated neighbours into itself.

		@warning **STRICTLY, NOT "no longer than".** A gap the SAME size as its neighbours is rare in
		 English and the ordinary sentence in Japanese, where one character is one word's worth of meaning:
		 with "no longer than", 琥珀猫太郎 -> 琥あ珀犬太郎 arrives as a single change reading "珀猫" -> "あ珀犬"
		 instead of "あ was inserted" and "猫 became 犬".

		The idea is taken from that library's behaviour, not its code -- the algorithm is written
		here from the description above.

		@warning for characters, not for paragraphs. Applied to a list of paragraphs it would mark
		 paragraphs that nobody touched as changed, because "the gap between two changed paragraphs
		 is short" says nothing about the paragraph sitting in that gap.

		⚠**AND A PARAGRAPH BREAK IS NEVER SWALLOWED** (the user's report). A break is
		 ONE character, so it is exactly the "short gap" this rule exists to absorb - and absorbing
		 it turns three rewritten lines into ONE change covering both breaks. Writing that change
		 replaces the breaks themselves, and **a paragraph style lives on its paragraph**: measured
		 on a document whose three paragraphs were StyleA, StyleB and StyleC, restoring such a
		 change left all three reading StyleC. The words came back and the styles did not.
		 ⇒ Pass `tokens` and the break's own token to keep changes on their own side of it. The
		   case where the break ITSELF changed - a paragraph joined or split - is untouched: there
		   the break is not in an unchanged gap at all, so it is still restorable, which it has to
		   be (putting it back is literally writing that character).

		@param changes IN OUT the runs to merge, in order. Repeats until nothing more merges.
		@param tokens the sequence the changes are indexed into (the A side), or nil for no such
			test. The gap between two changes is the same on both sides, so one side answers.
		@param doNotSwallow a token that blocks a merge when the gap holds it - the paragraph
			break for a joined run of paragraphs. Ignored when `tokens` is nil.
	*/
	void MergeNearbyChanges(std::vector<Change>& changes,
							const std::vector<int32>* tokens = 0,
							int32 doNotSwallow = -1);

	/** Slides each change to whichever of its EQUIVALENT positions reads most naturally, widens
		it to the whole word where it starts or ends inside a LATIN one (letters or digits), and
		folds together any two changes that have come to touch.

		**A CHANGE CAN USUALLY SIT IN MORE THAN ONE PLACE AND MEAN THE SAME THING**, and Myers has
		no reason to prefer one. When the character just after a change is the same as the
		character it starts with, the whole run can be rotated one step to the right and rebuild
		exactly the same text:

		    新版で [す・ここが違いま] す        <- what Myers returned
		    新版です [・ここが違います]          <- the same edit, one step right

		Both are shortest edit scripts; only the second is the edit a person would describe.
		@warning this is not a tie-break inside the search -- the search is finished and correct. It
		 is a cleanup afterwards. **THE ROTATION cannot change the edit distance**: every rotation
		 moves one common character from one side of the run to the other, so the counts never move.
		 ⚠**The Latin widening below CAN, and does** -- read to the end before relying on this.

		**HOW THE POSITION IS CHOSEN.** Every reachable rotation is tried and scored by what sits
		at its two boundaries, on both sides. What scores well is a boundary a reader would put a
		break at: the end of the text, a space, a mark of punctuation, or a change of script
		(kana to kanji, kanji to latin, and so on).
		@warning **Japanese is why the scoring is written in terms of SCRIPT rather than of word
		 breaks.** diff-match-patch, where this idea comes from, leans on spaces and line ends --
		 and a Japanese sentence has neither. What it does have is the boundary between 漢字 and
		 かな and 記号, which marks a word just as reliably.

		**AND THEN, ONLY INSIDE A LATIN WORD, THE RUN IS WIDENED**. Rotation alone
		leaves "OLDWORD" -> "NEWWORD" as OLD -> NEW: Myers factors out the common tail "WORD", and
		no rotation can swallow it back ('O' != 'W'). The word is then drawn in two pieces -- "OLD"
		in the change colour, "WORD" in the context colour -- and **a word drawn in two draw calls
		cannot reproduce the shaping of the whole word**, so the seam shows as a gap or an overlap.
		The change is stuck at a boundary **this file's own scoring calls the worst there is**:
		BoundaryScore returns 0 for a break inside one script class. The score could say the
		position was bad. So once the change is placed,
		identical characters are absorbed at either end while the boundary falls inside Latin.
		@warning **THIS ONE GROWS THE COUNTS** -- both of them, by the same amount, absorbing only
		 characters that are IDENTICAL on both sides, so the change still rebuilds `b` from `a`
		 exactly. What it no longer preserves is the NUMBER of edited characters, which can only
		 grow. The offline test (work/textdiff-test/align-test.cpp) checks the rebuild on every
		 case and holds the edit count to ">= what Myers computed" rather than "==".
		@warning ⚠**LATIN ONLY, AND THAT IS THE WHOLE POINT.** Japanese has no spaces, so nearly
		 every edit inside it sits at a 0-scoring boundary too -- and widening those to the nearest
		 space or punctuation would swallow whole sentences. That would destroy exactly what this
		 file was measured on: a two-character Japanese edit selecting exactly those two characters.
		 Latin runs are the only place where a boundary inside one script class is also a boundary
		 inside a WORD. "Latin" means **Latin letters (ASCII, fullwidth, and the accented ones of
		 Latin-1 and Latin Extended) AND digits**: "café" -> "cafés", "Müller" ->
		 "Möller", "2024" -> "2025" and "iPhone15" -> "iPhone16" are each one word to a reader, and
		 each would be quoted as the one or two characters Myers found.

		@warning FOR CHARACTERS, NOT FOR PARAGRAPHS -- the same restriction MergeNearbyChanges
		 carries, and for a plainer reason: the tokens of a paragraph diff are numbers standing for
		 whole paragraphs, and asking what script a paragraph is written in is meaningless.

		**NEIGHBOURS ARE NOT CROSSED.** A change may only rotate into the unchanged run on either
		side of it, never into or past the change next door -- two changes that swapped places
		would no longer be in reading order, and the row that quotes them walks them in order.
		**But they may be REACHED, and two that then touch are made one**. A run may
		widen right up to its neighbour, and once no unchanged character is left between the two
		they are one change by this file's own definition (Change: "never touch"). One Latin word
		edited in two places -- "axb" -> "aaxc" -- comes back as the one word, not as "ax" -> "aax"
		followed by "b" -> "c". ⚠**Only a gap of ZERO is folded**; MergeNearbyChanges' size rule is
		NOT re-run here, because the widening has just changed the sizes it would compare.

		@param a IN the baseline sequence the changes were computed from.
		@param b IN the target sequence.
		@param changes IN OUT the runs to align, in order. The rotation moves only the two start
			positions, and moves them together; the Latin widening moves the starts AND grows both
			counts by the same amount; the final fold can reduce the NUMBER of entries, never the
			characters they cover.
		@param widenWords IN whether to do the Latin widening at all. **THE TWO PLUG-INS THAT SHARE
			THIS ENGINE WANT OPPOSITE ANSWERS**. KCM DRAWS the change, and a word drawn
			in two pieces shows a seam, so it passes kTrue. KIDMCP PRINTS the change for a reader who
			wants to know which characters moved -- "AA[-BB-]{+ZZ+}CC" says more to that reader than
			"[-AABBCC-]{+AAZZCC+}", and its positions go straight into a script's itemByRange -- so it
			passes kFalse. The rotation and the fold of touching changes happen either way: those
			are correctness, this one is presentation. The engine stays identical in both copies;
			only the call differs (work/kidmcp-textdiff-sync.py compares the engine, not the callers).
	*/
	void AlignChangeBoundaries(const std::vector<int32>& a, const std::vector<int32>& b,
							   std::vector<Change>& changes, bool16 widenWords);

	/** Turns strings into tokens, giving equal strings equal numbers.
		@param strings IN the strings to number.
		@param table IN OUT the numbering so far. Pass the same table for both sequences - two
		sequences numbered from separate tables would share no tokens and every element would
		look changed.
		@param tokens OUT one token per string.
	*/
	void Tokenize(const std::vector<std::string>& strings,
				  std::map<std::string, int32>& table, std::vector<int32>& tokens);

	/** Splits UTF-8 into code points, one token each.

		Code points rather than bytes or UTF-16 units because that is the unit InDesign counts
		text positions in - a surrogate pair is one TextIndex - so an offset counted here can be
		handed to the text model unchanged.

		@param utf8 IN the text.
		@param codePoints OUT optional; one entry per code point, emptied first. Pass nil when only
		the byte offsets are wanted.
		@param byteOffsets OUT optional; for each code point, where it starts in utf8. Pass nil
		when only the comparison is wanted.

		**BOTH ANSWERS ARE OPTIONAL**: cutting an excerpt needs the byte boundaries and nothing else.
		@warning nil for both is legal and does nothing, which is the honest answer to being asked
		 for nothing.
	*/
	void ToCodePoints(const std::string& utf8, std::vector<int32>* codePoints,
					  std::vector<int32>* byteOffsets = nil);
}

#endif // __KCMTextDiff_h__

// End, KCMTextDiff.h.
