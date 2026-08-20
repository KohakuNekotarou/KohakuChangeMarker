//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The difference between two sequences, by Myers' algorithm.
//
//  *** PORTED FROM KohakuTest's KTTextDiff ON 2026-08-20, UNCHANGED EXCEPT FOR THE NAMES. ***
//  It was measured there against real documents before it was brought here - a two-character
//  Japanese edit selected exactly those two characters, and "sleeping" -> "awake" came back as
//  one change rather than two. The record of that is
//  docs/ai-notes/kt-story-diff-experiment-2026-08-17.md.
//  ⚠TWO COPIES NOW EXIST. If a fault is found in either, fix BOTH or delete the KT one: two
//  copies that quietly disagree are worse than either of them alone.
//
//  Written from the published method - Eugene W. Myers, "An O(ND) Difference Algorithm and Its
//  Variations", Algorithmica 1(2), 1986 - and not from anybody's source. An algorithm is not
//  copyrightable; a particular implementation of it is, and git's (xdiff/, from LibXDiff) is
//  GPL, so none of it is here.
//
//  Everything is reduced to a sequence of int32 tokens before it gets here, which is the same
//  move git makes when it hashes each line to an integer: it means one implementation serves
//  both jobs this plug-in needs - comparing a list of paragraphs, and comparing the characters
//  inside one paragraph - because the two problems are the same problem at different grain.
//  Unlike a hash, the tokens are allocated from a table, so two different paragraphs can never
//  collide into looking equal.
//
//  The common head and tail are stripped before the search starts. That is not a detail: the
//  cost is O((N+M)*D) in the edit distance D, so for the case this exists for - a long document
//  with a few edits in the middle - stripping is what keeps D small enough to matter.
//
//========================================================================================

#ifndef __KESCMTextDiff_h__
#define __KESCMTextDiff_h__

#include <string>
#include <vector>

/** Sequence comparison. All grain sizes go through the same core.
	@ingroup KESCM
*/
namespace KESCMTextDiff
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
		@param maxEdits IN give up once the edit distance exceeds this. The search keeps one
		row per step, so an unbounded run on two unrelated sequences would cost O(D^2) memory;
		a caller that hits the limit is told so rather than made to wait.
		@return kTrue if the comparison completed, kFalse if it gave up at maxEdits - in which
		case changes is left empty and the caller should treat the whole sequence as changed.
	*/
	bool16 Diff(const std::vector<int32>& a, const std::vector<int32>& b,
				std::vector<Change>& changes, int32 maxEdits = 2000);

	/** Merges neighbouring changes that are separated by only a short unchanged run.

		Myers returns the shortest edit script, which is not the same thing as the edit a person
		would describe. Rewriting "sleeping" as "awake" comes back as two changes, because the
		letter 'e' appears in both words and counting it as unchanged makes the script one step
		shorter. Nobody reads it that way.

		The rule is the one Google's diff-match-patch calls a semantic cleanup: if the unchanged
		run between two changes is STRICTLY SHORTER than EACH of the changes around it, swallow it
		and make them one. Requiring it to be shorter than both, rather than than the larger of
		them, is what stops a long edit from dragging unrelated neighbours into itself.

		⚠"Strictly" is the whole of a bug fixed on 2026-08-20. This sentence used to say "no
		longer than", the next one said "shorter than both", and the code followed the first -
		so a gap the SAME size as its neighbours was swallowed. In English that is rare; in
		Japanese it is the ordinary sentence, because one character is one word's worth of
		meaning: 琥珀猫太郎 -> 琥あ珀犬太郎 arrived as a single change reading "珀猫" -> "あ珀犬"
		instead of "あ was inserted" and "猫 became 犬". Nothing that this rule exists for is
		lost by the strictness - those gaps are genuinely smaller than the edits around them.

		The idea is taken from that library's behaviour, not its code - the algorithm is written
		here from the description above.

		⚠ For characters, not for paragraphs. Applied to a list of paragraphs it would mark
		paragraphs that nobody touched as changed, because "the gap between two changed
		paragraphs is short" says nothing about the paragraph sitting in that gap.

		@param changes IN OUT the runs to merge, in order. Repeats until nothing more merges.
	*/
	void MergeNearbyChanges(std::vector<Change>& changes);

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

#endif // __KESCMTextDiff_h__

// End, KESCMTextDiff.h.
