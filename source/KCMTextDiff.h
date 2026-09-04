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
//  ⚠★★**THE TWO COPIES HAVE DIVERGED, AND THIS IS THE LIVE ONE.** Measured 2026-09-04: 27 KB
//   here against 11 KB there, and every one of the differences is an improvement made on this
//   side -- the linear-space search (FindMiddleSnake), MergeNearbyChanges' "strictly shorter"
//   rule, and AlignChangeBoundaries. **KT's copy is the August implementation, still compiled and
//   still used by KT/KTStoryDiff.cpp.**
//   ⇒ **Read this file first.** A fault found here does not need carrying over there; a fault
//     found there may well be one this file has already fixed.
//   ⚠**The old instruction "fix BOTH" is withdrawn**: it was written when the two were the same
//     file, and following it now would mean porting three separate improvements backwards into an
//     experiment. ⬜Whether KT's copy should be retired instead is undecided (2026-09-04).
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

	/** Compares two token sequences.
		@param a IN the baseline sequence.
		@param b IN the target sequence.
		@param changes OUT the runs of difference, in order. Emptied first.
		@param maxEdits IN an optional ceiling on the edit distance. **0 -- the default -- means no
		ceiling, and that is what every caller uses**: slower is acceptable here, wrong is not.

		**Why there used to be one, and why there need not be now.** The search once kept one row
		per step so that the path could be walked back, which costs O(D^2) MEMORY -- about 31 MB at
		the old ceiling of 2000, and four times that for every doubling. The ceiling was the only
		thing holding that number down, and the price was that any story needing more edits than
		the ceiling got no detail at all. The search now runs in LINEAR SPACE (Myers' own
		divide-and-conquer refinement), so distance costs time and not memory: **50,000 tokens
		against 50,000 unrelated ones needs 3.2 MB.**
		★The figure is worth being able to re-derive rather than trust: the search keeps TWO rows of
		4*(N+M)+7 int32s, so 50,000 + 50,000 gives 400,007 entries each, 1.6 MB a row and 3.2 MB the
		pair. ⚠**This line said 1.6 MB until 2026-09-04** -- one row's worth, written as though it
		were both.

		@warning what a ceiling would still buy is a bound on TIME, which is O((N+M)*D). Measured
		 in work/textdiff-test: 8,000 vs 8,000 with one percent changed = **1 ms**; 3,000 vs 3,000
		 sharing nothing = **84 ms**; 50,000 vs 50,000 sharing nothing = **24 s**. Only the last is
		 uncomfortable, and it is a shape real text does not take -- two versions of a document
		 share most of their characters even when every sentence was rewritten.

		@return kTrue if the comparison completed, kFalse if it hit maxEdits -- in which case
		changes is left empty and the caller should treat the whole sequence as changed.
		With the default there is no ceiling to hit, so kFalse cannot happen; the callers still
		check, because that is the contract and not an accident of today's default.
	*/
	bool16 Diff(const std::vector<int32>& a, const std::vector<int32>& b,
				std::vector<Change>& changes, int32 maxEdits = 0);

	/** Merges neighbouring changes that are separated by only a short unchanged run.

		Myers returns the shortest edit script, which is not the same thing as the edit a person
		would describe. Rewriting "sleeping" as "awake" comes back as two changes, because the
		letter 'e' appears in both words and counting it as unchanged makes the script one step
		shorter. Nobody reads it that way.

		The rule is the one Google's diff-match-patch calls a semantic cleanup: if the unchanged
		run between two changes is STRICTLY SHORTER than EACH of the changes around it, swallow it
		and make them one. Requiring it to be shorter than both, rather than than the larger of
		them, is what stops a long edit from dragging unrelated neighbours into itself.

		@warning **"strictly" is the whole of a bug.** This sentence once said "no longer than",
		 the next one said "shorter than both", and the code followed the first -- so a gap the
		 SAME size as its neighbours was swallowed. In English that is rare; in Japanese it is the
		 ordinary sentence, because one character is one word's worth of meaning:
		 琥珀猫太郎 -> 琥あ珀犬太郎 arrived as a single change reading "珀猫" -> "あ珀犬" instead
		 of "あ was inserted" and "猫 became 犬". Nothing that this rule exists for is lost by the
		 strictness -- those gaps are genuinely smaller than the edits around them.

		The idea is taken from that library's behaviour, not its code -- the algorithm is written
		here from the description above.

		@warning for characters, not for paragraphs. Applied to a list of paragraphs it would mark
		 paragraphs that nobody touched as changed, because "the gap between two changed paragraphs
		 is short" says nothing about the paragraph sitting in that gap.

		@param changes IN OUT the runs to merge, in order. Repeats until nothing more merges.
	*/
	void MergeNearbyChanges(std::vector<Change>& changes);

	/** Slides each change to whichever of its EQUIVALENT positions reads most naturally.

		**A CHANGE CAN USUALLY SIT IN MORE THAN ONE PLACE AND MEAN THE SAME THING**, and Myers has
		no reason to prefer one. When the character just after a change is the same as the
		character it starts with, the whole run can be rotated one step to the right and rebuild
		exactly the same text:

		    新版で [す・ここが違いま] す        <- what Myers returned
		    新版です [・ここが違います]          <- the same edit, one step right

		Both are shortest edit scripts; only the second is the edit a person would describe.
		@warning this is not a tie-break inside the search -- the search is finished and correct. It
		 is a cleanup afterwards, and it cannot change the edit distance: every rotation moves one
		 common character from one side of the run to the other, so the counts never move.

		**HOW THE POSITION IS CHOSEN.** Every reachable rotation is tried and scored by what sits
		at its two boundaries, on both sides. What scores well is a boundary a reader would put a
		break at: the end of the text, a space, a mark of punctuation, or a change of script
		(kana to kanji, kanji to latin, and so on).
		@warning **Japanese is why the scoring is written in terms of SCRIPT rather than of word
		 breaks.** diff-match-patch, where this idea comes from, leans on spaces and line ends --
		 and a Japanese sentence has neither. What it does have is the boundary between 漢字 and
		 かな and 記号, which marks a word just as reliably.

		@warning FOR CHARACTERS, NOT FOR PARAGRAPHS -- the same restriction MergeNearbyChanges
		 carries, and for a plainer reason: the tokens of a paragraph diff are numbers standing for
		 whole paragraphs, and asking what script a paragraph is written in is meaningless.

		**NEIGHBOURS ARE NOT CROSSED.** A change may only rotate into the unchanged run on either
		side of it, never into or past the change next door -- two changes that swapped places
		would no longer be in reading order, and the row that quotes them walks them in order.

		@param a IN the baseline sequence the changes were computed from.
		@param b IN the target sequence.
		@param changes IN OUT the runs to align, in order. Counts are never touched; only the
			two start positions move, and they move together.
	*/
	void AlignChangeBoundaries(const std::vector<int32>& a, const std::vector<int32>& b,
							   std::vector<Change>& changes);

	/** Turns strings into tokens, giving equal strings equal numbers.
		@param strings IN the strings to number.
		@param table IN OUT the numbering so far. Pass the same table for both sequences - two
		sequences numbered from separate tables would share no tokens and every element would
		look changed.
		@param tokens OUT one token per string.
	*/
	void Tokenize(const std::vector<std::string>& strings,
				  std::vector<std::string>& table, std::vector<int32>& tokens);

	/** Splits UTF-8 into code points, one token each.

		Code points rather than bytes or UTF-16 units because that is the unit InDesign counts
		text positions in - a surrogate pair is one TextIndex - so an offset counted here can be
		handed to the text model unchanged.

		@param utf8 IN the text.
		@param codePoints OUT one entry per code point. Emptied first.
		@param byteOffsets OUT optional; for each code point, where it starts in utf8. Pass nil
		when only the comparison is wanted.
	*/
	void ToCodePoints(const std::string& utf8, std::vector<int32>& codePoints,
					  std::vector<int32>* byteOffsets = nil);
}

#endif // __KCMTextDiff_h__

// End, KCMTextDiff.h.
