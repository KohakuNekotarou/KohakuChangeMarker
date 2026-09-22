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
//  function on KCMStoryShape::Story with no SDK type in it (built and tested outside InDesign in
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
#include "KCMStoryShape.h"	// Story, Para - the shape every side is in

#include <string>
#include <vector>

namespace KCMStoryMerge
{

/** One change of Word's that was taken, as it landed: `fNowCount` characters at `fNowAt` of `now`
	became `fNewCount`. What a table's place in the paragraph is moved by (Merge). */
struct Edit
{
	int32	fNowAt;
	int32	fNowCount;
	int32	fNewCount;
};

/** One paragraph's merge. Exposed for the harness; Merge (below) is what the import calls. */
struct ParaResult
{
	KCMStoryShape::Para			fMerged;	// `now` plus Word's changes
	int32						fApplied;	// Word's changes taken, each counted once
	std::vector<std::string>	fWhys;		// one per change of Word's NOT taken: why the document's words were kept
	std::vector<Edit>			fEdits;		// the word changes taken, in ascending order of fNowAt

	ParaResult() : fApplied(0) {}
};

/** One change of Word's that was not taken: where, and why the document's words were kept. */
struct Refusal
{
	std::string	fWhere;		// "body paragraph 3", "table 0 row 1 cell 0 paragraph 1", "note 1 paragraph 1", "the notes"
	std::string	fWhy;
};

/** Where in a story a place stands. fTable < 0 is the body; otherwise it is that table's cell.

	★**A NOTE IS NEVER A PLACE HERE.** A footnote cannot hold a footnote's reference in anything
	  this road carries, so a reference always stands in the body or in a cell. */
struct PlaceRef
{
	int32	fTable;		// -1 = the body
	int32	fRow;
	int32	fCell;

	PlaceRef() : fTable(-1), fRow(0), fCell(0) {}
};

/** One footnote Word ADDED: where its reference goes, and the words the note holds.

	★★★**THE MERGE ONLY PLANS IT.** Nothing here can add a note to KCMStoryShape::Story and have it
	  mean anything: a note is made in the document by putting its MARKER in the text, and InDesign
	  builds the note around that character (kCreateFootnoteCmdBoss). So the merged Story still
	  holds the document's own notes, and this says what the pour has to do besides pouring words.
	fAt counts the MERGED paragraph's code points, the same way NoteRef::fAt counts its own. */
struct NoteAdd
{
	PlaceRef							fPlace;
	int32								fPara;		// index into that place's MERGED paragraphs
	int32								fAt;		// code point offset inside that paragraph
	std::vector<KCMStoryShape::Para>	fParas;		// the note's own words, as `after` has them

	NoteAdd() : fPara(0), fAt(0) {}
};

/** One footnote Word TOOK AWAY: which of the DOCUMENT's notes it is.

	★**ONE MOVE UNDOES IT**: the pour deletes that note's reference character, and InDesign takes
	  the note with it (measured 2026-09-22). ⚠It takes any note nested inside it too. */
struct NoteRemove
{
	int32	fNowNote;	// index into now.fNotes

	NoteRemove() : fNowNote(0) {}
};

struct Result
{
	KCMStoryShape::Story		fMerged;		// the document as it stands now, plus Word's changes
	int32					fApplied;		// Word's changes taken, each counted once
	std::vector<Refusal>	fConflicts;		// Word's changes not taken, each named
	bool16					fStoryRefused;	// the whole story was left as it stands: its tables cannot be lined up
	std::string				fWhy;			// when fStoryRefused
	// ★**ONE TABLE LEFT ALONE IS NOT THE WHOLE STORY LEFT ALONE** (2026-09-22, the user's call).
	//   A table whose shape the three sides do not agree about keeps the document's own contents,
	//   cell for cell, and is named here; everything else in the story is merged as usual. ⚠The
	//   whole story is still refused when the NUMBER of tables differs - see Merge.
	std::vector<Refusal>	fTableRefusals;	// tables left exactly as the document has them
	// ★**THE NOTES WORD ADDED AND TOOK AWAY** (2026-09-22, the user's call: take them in). These are
	//   instructions for the pour, not part of fMerged - see NoteAdd. ⚠A note whose words alone
	//   changed is NOT here: that is an ordinary merge of the note's paragraphs.
	std::vector<NoteAdd>	fNoteAdds;
	std::vector<NoteRemove>	fNoteRemoves;

	Result() : fApplied(0), fStoryRefused(kFalse) {}
};

/** origin (as written), after (as Word left it), now (the document as it stands) -> Result.

	★**PLACE BY PLACE** (the design, 6-2): the body, each cell, each note is a list of paragraphs, and
	  each list is merged on its own - first paragraph by paragraph (A = Diff(origin, after) and
	  B = Diff(origin, now) over the paragraphs' texts), then, for a paragraph both sides kept,
	  character by character (MergePara). A paragraph Word added, took out or split is taken when
	  the document did not touch the paragraphs around it; else it is a conflict (6-5).
	★**THE TABLES HAVE TO AGREE THREE WAYS** (6-1) - the same count, rows and cells on every side.
	  ⚠**WHAT A DISAGREEMENT COSTS CHANGED ON 2026-09-22** (the user's call): only the NUMBER of
	  tables still refuses the whole story (fStoryRefused), because a table added or taken away
	  moves the paragraphs around it and nothing can be lined up. A table whose ROWS or CELLS
	  disagree is left exactly as the document has it and named in fTableRefusals - the body, the
	  notes and the other tables are merged as usual. A paragraph-level change that holds a table is
	  a conflict; a character-level change moves a table standing after it in its paragraph, and one
	  straddling the table's place is a conflict.
	★**THE NOTES' NUMBER HAS TO AGREE THREE WAYS** too; when it does not, the notes stand as they are
	  (one conflict, "the notes") and the body and the cells are merged all the same.
	★**THE OUTPUT IS "NOW PLUS WORD'S CHANGES"**: with nothing from Word it is Same as `now` (6-7). */
void Merge(const KCMStoryShape::Story& origin, const KCMStoryShape::Story& after, const KCMStoryShape::Story& now,
		   Result& out);

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
	  reference is the document's and cannot travel with words nobody can see in Word. So does a
	  position in `keep` (a table's place in the paragraph, in now's coordinates): a change may end
	  or begin there, not straddle it.
	@param keep positions of `now` no change may straddle; nil for none. */
void MergePara(const KCMStoryShape::Para& origin, const KCMStoryShape::Para& after, const KCMStoryShape::Para& now,
			   ParaResult& out, const std::vector<int32>* keep = nil);

}	// namespace KCMStoryMerge

#endif // __KCMStoryMerge_h__

// End, KCMStoryMerge.h.
