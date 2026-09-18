//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a story three ways: as written, as Word left it, as it stands now
//
//  WHAT THIS IS FOR. A story goes out as a .docx, is edited in Word with revision tracking on, and
//  comes back - and in the meantime the document may have been edited in InDesign too. The Import
//  mode should then show WHAT WAS CHANGED IN WORD and nothing else (the design, section 1-1: the
//  user's own example is "琥珀猫の大冒険" edited to "琥珀猫の小さな冒険" in Word while InDesign
//  made it "琥珀犬の大冒険" - what the reader wants to see is 大 -> 小さな, and what they get
//  after taking it in is "琥珀犬の小さな冒険"). That is a three-way merge, and this is it:
//
//      Merge(origin, after, now) = now + (after - origin), one change at a time
//
//  where `origin` is the story as it was written (rebuilt from Word's own revision marks by
//  KCMStoryDocx::Read, and trusted only when its fingerprint is the tag's - OriginMatchesTag),
//  `after` is the story as Word shows it, and `now` is the story as the document holds it.
//
//  *** THE OUTPUT GOES INTO THE POUR UNCHANGED. *** What comes out is a Story that is `now` plus
//  Word's changes, so KCMStoryTextImport pours it exactly as it pours an .html: the places, the
//  paragraph steps, the words, the ruby and the kenten, the Import mode, the rows, Undo - none of
//  it knows a merge happened. That is the design's section 3, and it is why this file is a pure
//  function on KCMStoryHtml::Story with no SDK type in it (built and tested outside InDesign in
//  work/kcm-storydocx-test, like everything else on this road).
//
//  *** A CONFLICT KEEPS THE DOCUMENT'S WORDS, AND IS NAMED. *** Word's change and the document's
//  change to the same words - overlapping OR TOUCHING (an insertion at the very place the other
//  side inserted counts) - is not something this file decides; that one change is left out, the
//  document's words stand, and the refusal says where (the design, sections 6-4 and 12). Nothing
//  is ever dropped without being counted.
//
//  *** WITH NO CHANGE IN WORD THE OUTPUT IS `now`, WHATEVER InDesign DID. *** (Section 6-7.) A
//  file imported twice therefore shows nothing the second time - the control every stage of this
//  road has kept.
//
//========================================================================================
#ifndef __KCMStoryMerge_h__
#define __KCMStoryMerge_h__

#include "BaseType.h"
#include "KCMStoryHtml.h"	// Story, Para - the shape every side is in

#include <string>
#include <vector>

namespace KCMStoryMerge
{

/** One paragraph's merge. Exposed for the harness; Merge (below) is what the import calls. */
struct ParaResult
{
	KCMStoryHtml::Para			fMerged;	// `now` plus Word's changes
	int32						fApplied;	// Word's changes taken, each counted once
	std::vector<std::string>	fWhys;		// one per change of Word's NOT taken: why the document's words were kept

	ParaResult() : fApplied(0) {}
};

/** origin, after (Word), now (the document) -> now plus Word's changes to these words and to the
	ruby, kenten, tate-chu-yoko and warichu over them.

	★**CHARACTER BY CHARACTER, TWO DIFFS**: A = Diff(origin, after) is what Word did, B = Diff(origin,
	  now) is what InDesign did, both raw Myers over code points (no MergeNearbyChanges and no
	  AlignChangeBoundaries: the first would widen what counts as "the same words" and the second
	  would move the two diffs off one coordinate system). A change of A is taken when no change
	  of B overlaps or touches it, and its origin position is carried to `now` by the length B's
	  earlier changes added or took away. ⚠Applied BACK TO FRONT, so that a change already taken
	  has moved nothing that a change still to be taken will land on; ToNow itself does not depend
	  on that order, because B is fixed in origin coordinates.
	★**A footnote reference standing inside a change of Word's makes that change a conflict**: the
	  reference is the document's and cannot travel with words nobody can see in Word. */
void MergePara(const KCMStoryHtml::Para& origin, const KCMStoryHtml::Para& after, const KCMStoryHtml::Para& now,
			   ParaResult& out);

}	// namespace KCMStoryMerge

#endif // __KCMStoryMerge_h__

// End, KCMStoryMerge.h.
