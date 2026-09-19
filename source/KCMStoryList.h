//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The list of stories that changed between the two versions, ready to be shown as rows.
//
//  This file turns KCMStoryStamp's raw findings into something a person can read: the first
//  words of each story, which page it starts on, and what kind of change it saw. It knows nothing
//  about trees or widgets -- the Story Edits tree reads this and only this.
//
//  **THE LIST IS A FILE-STATIC GLOBAL, NOT A BOSS.** It is built by one comparison and thrown
//  away by the next, so there is nothing to persist and nobody to share it with. KBS keeps its
//  result list exactly this way (KBSResultModel's gChapters). The one obligation that comes with
//  it: ShutdownCleanup() has to empty it during a controlled shutdown, because these rows hold
//  PMStrings, and nothing of ours should still be holding storage when the DLL unloads.
//
//  @warning **there is no central list of such statics, and looking for one is the mistake.**
//   Each module empties its own, in its own ShutdownCleanup, and the shutdown service calls them
//   one by one (KCMPeek.cpp). KBS learned it the hard way -- five of its statics were found
//   still holding storage into DLL unload, one discovery at a time -- and states the rule in as
//   many words: the list is checked against the STATICS, not against the file
//   (KBSReplaceConfirmDialog::ShutdownCleanup). A new static here means a new line HERE.
//
//========================================================================================

#ifndef __KCMStoryList_h__
#define __KCMStoryList_h__

#include "PMString.h"
#include "PMPoint.h"	// PBPMPoint - where a story begins, for the jump
#include "UIDRef.h"
#include "WideString.h"	// KCMStoryChange::fBeforeRaw - the characters themselves, not a quote of them

#include <vector>

#include "KCMStoryStamp.h"	// KCMStoryDiff / KCMStoryChangeKind
#include "KCMStoryLayers.h"	// KCMStoryChange::fLayers - a warichu / tate-chu-yoko change, line by line

class IDataBase;

/** One difference inside one story: where it is, what sort it is, and what it reads.

	**ONLY THE STORY CHANGES MODE PRODUCES THESE.** The change counters KCMStoryStamp reads can
	say THAT a story changed; these say WHERE, which only the text diff can answer.

	**POSITIONS ARE KEPT FOR BOTH DOCUMENTS, and that is deliberate.** Clicking a change moves the
	newer window to it and the older window to the matching place; both sides are worked out here,
	while the diff still has them (KCMTextDiff::Change carries the a-side range as well as the
	b-side one). Working the older side out at click time would mean diffing again.
*/
struct KCMStoryChange
{
	enum Kind { kReplace, kInsert, kDelete };

	/** What sort of thing changed. kAttr means ruby, and so far nothing else.

		**fText IS THE TARGET SIDE AND fOtherText THE SOURCE SIDE, FOR EVERY KIND AND BOTH WHATS.**
		⚠This sentence said until 2026-09-12 that a text change "shows whichever side changed, so a
		  deletion puts the newer words in fOtherText". That stopped being true on 2026-09-01, when
		  the row became the newer version without exception (KCMStoryDiffRun's Add says why), and
		  the sentence outlived the rule by eleven days - long enough for the change-row copy item
		  to be written from it and grey itself on every deleted paragraph. The fields are filled
		  in one place (Add / AddAttributeChange: SetExcerptPieces from the target into fText and
		  from the source into fOtherText) and that place, not this comment, is the authority. */
	enum What { kText, kAttr,
				kRefused };	// ★one thing an IMPORT could not put in (2026-09-19). fTextPre holds the
							//   KIND word for the ID column ("Word" / "Table" / "Place" / "Para" /
							//   "Attr" / "File") and fText the place and the reason; fWriteBlock is
							//   kKCMWriteBlockedKind so no menu offers to write it. Nothing else on
							//   the change means anything, and it never carries a position.

	Kind		fKind;
	What		fWhat;

	// ---- the newer document (Target) -- what a click jumps to and selects ----
	TextIndex	fTargetStart;
	TextIndex	fTargetEnd;		// AN END, NOT A LENGTH -- as RangeData::End() is against its Length()

	// ---- the older document (Source) ----
	/** **ALWAYS A REAL PLACE, AND EMPTY FOR AN INSERTION.** fSourceEnd == fSourceStart means
		"this spot in the older version, and no characters" -- the gap the new words were typed
		into. That is the same shape the NEWER side already has for a deletion, so + and - are
		mirror images of one another: each has a range on the side it exists and a caret on the
		side it does not.

		@warning **ask fSourceEnd > fSourceStart, never "is there a source at all".** A single
		  bool16 stood here once and was kFalse for an insertion, which folded two questions into
		  one flag ([[one-question-one-place]]): "is there a place over there" (always yes) and "is
		  there anything to SELECT over there" (no, for an insertion). The first dragged the second
		  down with it, and the older window stopped moving for insertions altogether. */
	TextIndex	fSourceStart;
	TextIndex	fSourceEnd;

	/** The words the row shows, in THREE PIECES: what stands before the change, the changed
		characters themselves, and what stands after.

		**ALWAYS THE NEWER (TARGET) TEXT, WHATEVER THE KIND** (2026-09-01, the user's decision - the
		reasoning is at KCMStoryDiffRun's Add). A deletion therefore has an EMPTY middle piece: the
		newer side has nothing there to show, and the removed words are in fOtherText, where the
		message area shows them. ⚠Until 2026-09-12 this paragraph still described the rule of
		2026-08-20 (the OLDER text for a deletion), which had been withdrawn.

		**THREE PIECES SO THE ROW CAN DRAW THE CHANGE AT FULL STRENGTH AND FADE THE CONTEXT** --
		the way a KBS hit row draws its match. Concatenated they are one plain string again.

		**THE SPLIT IS MADE WHERE THE INFORMATION IS.** Which characters were the change is known in
		code points, inside a string that has already been cut at both ends and had its break
		characters replaced (KCMStoryDiffRun's Slice). Handing the panel one string and an offset
		would ask it to count code points in a PMString, whose own index is UTF-16.

		**THE ELLIPSES BELONG TO THE CONTEXT PIECES.** An ellipsis stands for words that were cut
		away, and those are always context, never the change -- so a faded ellipsis is right. */
	PMString	fTextPre;
	PMString	fText;
	PMString	fTextPost;

	/** The OTHER side of the same edit, in the same three pieces -- what the panel's message area
		shows while the row is selected, with the changed part coloured differently from the rest.

		**THE SOURCE (OLDER) SIDE, FOR EVERY KIND** - since 2026-09-01 the row is always the newer
		version, so "the other side" and "the old side" are the same side:
		    replacement -> the row shows the new words,  this holds the OLD ones
		    insertion   -> the row shows what was added, this holds the old text with nothing
		                   between the context (there was nothing there)
		    deletion    -> the row shows the new text with nothing between the context,
		                   this holds what was REMOVED
		The name "Other" is older than that rule (it was chosen when a deletion's row showed the
		older side and this field then held the NEWER text) and has been kept so that nothing needs
		renaming across the two plug-ins; read it as "source".

		fOtherText is empty for an insertion (nothing stood there); fText is empty for a deletion
		(nothing stands there now). The context pieces are never empty: they are the words on either
		side, which is what makes an empty middle readable as a place rather than as an absence. */
	PMString	fOtherTextPre;
	PMString	fOtherText;
	PMString	fOtherTextPost;

	/** ---- ruby, and ONLY meaningful when fWhat is kAttr ----

		**WHY RUBY NEEDED FIELDS OF ITS OWN** rather than being written into fText: the row has to
		show the BASE TEXT and the READING at the same time, one above the other, the way ruby is
		actually set -- so the two cannot be one string. fText/fTextPre/fTextPost carry the base text
		with its context exactly as they do for a text change; these two carry the readings.

		fRuby is the reading on the side the row shows, fOtherRuby the one on the other side -- the
		same pairing as fText / fOtherText, so a row never has to ask which document it is looking at.
		Either can be empty: ruby added has no old reading, ruby removed has no new one.

		@warning **MONO AND GROUP RUBY BOTH LAND HERE AND THE DIFFERENCE IS NOT IN THE STRING.** One
		  reading over two characters and two readings over one character each can produce the same
		  characters; what tells them apart is the SPAN. The spans are what the diff compared
		  (KCMParaText.h), and fTargetStart/fTargetEnd is the span this change is about. */
	PMString	fRuby;
	PMString	fOtherRuby;

	/** How the ruby above is SET -- kTrue for GROUP (one reading over several characters), kFalse
		for MONO (one reading per character). fRubyGroup belongs to the side the row shows and
		fOtherRubyGroup to the other, the same pairing as fRuby / fOtherRuby.

		⚠**CARRIED, BUT NO LONGER JUDGED OR SHOWN** (2026-09-12, the user's decision: "a change of
		ruby kind is not a change - take the judgement out, and the G / M display with it"). From
		2026-09-08 to 2026-09-12 CompareParagraphAttr tested fGroup beside the value and the length
		and the ruby row showed "Mono" / "Group"; both are gone. The fields stay so that the
		facade's layout does not move, and because KCMAttrSpan still reads the setting from the
		document - nothing judges by it or displays it now.
		@warning **MEANINGLESS WHERE THERE IS NO RUBY ON THAT SIDE**, and the empty string beside it
		  is what says so: a removed ruby has no fRuby, and its fRubyGroup is kFalse because it has
		  to be something, not because the ruby that is gone was mono.
		@warning **RUBY ONLY.** Kenten travels in the same fields (fAttrKind says which), and it has
		  no such distinction -- it is per character by nature -- so both stay kFalse there. */
	bool16		fRubyGroup;
	bool16		fOtherRubyGroup;

	/** WHICH attribute this is, when fWhat is kAttr. kKCMStoryAttrNone for a text change.

		**fWhat SAYS "not the words", THIS SAYS WHAT INSTEAD** -- and the panel needs both, because
		fWhat does not promise the VALUE is something a reader reads. Kenten proves it: its change
		fills these very fields with a KIND ("BlackCircle"), so anything asking fWhat alone treats a
		name as a reading - which is what the message area did in August, and the reason the feature
		was withdrawn that day rather than the comparison being wrong.
		@warning **"does this carry a reading", and "is this drawn on two lines", is THIS field,
		  never fWhat.** Ruby being the only kind reported today, the two happen to agree again --
		  which is exactly the state in which a stand-in survives unnoticed.

		The row has a field of the same name and the same values (KCMStoryRow::fAttrKind), worked
		out from these by SetRowChanges -- the row names the attribute, the children carry it. */
	KCMStoryAttrKind fAttrKind;

	/** Whether the text this change names is OVERSET - not composed into any frame (2026-09-15).

		★**DECIDED ONCE, WHEN THE DIFF MAKES THE CHANGE**, and not asked again while the panel
		draws. The question costs a parcel-list lookup, the row is drawn many times over, and a
		"read-only" call that recomposes is a trap this SDK sets more than once
		([[text-composition-damage-and-recompose]]). Every path that rebuilds a row runs the diff,
		so the answer cannot go stale while the row stands.
		⚠**A REPLACED change keeps the answer it had when it was taken in.** Writing different
		words can in principle push a line over the edge; the row would then say OV a moment
		later than it might. Refreshing the row settles it, and that is the same door everything
		else in this list uses. */
	bool16		fOverset;

	// ---- what a REPLACED change remembers (the Import mode only, 2026-09-15) ------------------
	//
	// ★**A REPLACED CHANGE KEEPS BOTH SIDES OF ITSELF.** The row has to be drawable in two states:
	//   as it stands now (replaced), and as it stood before (after the reader presses Ctrl+Z).
	//   Working the older one out again is not possible - the words it named are no longer in the
	//   story - so it is kept at the moment of the write and never recomputed.
	//
	// ⚠**THERE IS NO "replaced" FLAG HERE, AND THAT IS DELIBERATE.** A flag would be a second
	//   answer to a question fReplacedCount already answers, and the two would part company the
	//   first time the reader undid the write: the flag would still say yes over text that had
	//   gone back ([[one-question-one-place]]). The row is drawn as replaced when, and only when,
	//   this equals the story's text change counter NOW - and that counter goes back on an undo.

	/** The Target story's counter at the moment this change was replaced, or 0 when it never was.
		⇒ **"Is it replaced?" is `fReplacedCount != 0 && the story has got at least this far`**,
		asked wherever the answer is needed and stored nowhere (KCMFacades' StillReplaced).

		⚠★★★**WHICH COUNTER DEPENDS ON THE KIND, AND ONLY KCMStoryDiffRun::CountForKind KNOWS**
		 (2026-09-16). A change to the WORDS is measured by the story's TEXT change counter; a
		 ruby or a kenten by the AGGREGATE one, because the text counter does not move for an
		 attribute at all - measured 4, 4, 4 across a ruby write and its undo, against 13, 14, 13
		 from the aggregate. Written and read through that one function, so the two ends cannot
		 disagree about what the number means. */
	uint32		fReplacedCount;

	/** The range the replacement occupies NOW, in the Target -- what a jump aims at and what the
		cell draws as the changed part. Meaningless while fReplacedCount is 0. */
	TextIndex	fReplacedStart;
	TextIndex	fReplacedEnd;

	/** The three pieces AS THEY STAND AFTER the replacement (context, the words that went in,
		context), cut the way the diff cuts its own -- KCMStoryDiffRun::SliceAround. */
	PMString	fReplacedTextPre;
	PMString	fReplacedText;
	PMString	fReplacedTextPost;

	/** The range and the three pieces AS THEY STOOD BEFORE it.

		★**THEY CANNOT SHARE fTargetStart / fText***: after the write the story is diffed again,
		and those fields then belong to a different comparison of a text this change is no longer
		part of. The before-state has to be held apart from them or it is quietly overwritten. */
	TextIndex	fBeforeStart;
	TextIndex	fBeforeEnd;
	PMString	fBeforeTextPre;
	PMString	fBeforeText;
	PMString	fBeforeTextPost;

	/** The Target's OWN CHARACTERS over [fBeforeStart, fBeforeEnd), read from the text model at the
		moment of the take-in, before anything was written - what "Undo the Restore" writes back.

		⚠★★★**NEVER fBeforeText.** That is the ROW'S QUOTE: KCMStoryDiffRun's Slice cuts it to
		 kExcerptCodePoints and MarkUpBreaks turns a paragraph break into a pilcrow. Written back
		 into the story it cost the reader their words - measured 2026-09-16: eighty characters came
		 back as sixty, and a paragraph break came back as the character U+00B6 in a single paragraph.
		Empty for a change that is not a replaced TEXT change. */
	WideString	fBeforeRaw;

	/** Why this change must not be written back - KCMStoryWriteBlock, kKCMWriteAllowed for a change
		that may. Decided by the diff for a TEXT change (KCMStoryDiffRun), shown by the menu, and asked
		again by the write against the characters as they stand then (KCMStoryRestore). */
	int32		fWriteBlock;

	/** ★**THE LINES A WARICHU OR TATE-CHU-YOKO CHANGE IS DRAWN ON** (2026-09-16) - fLayers for the
		side the row shows (Target), fOtherLayers for the message area's side (Source), the same
		pairing as fText / fOtherText. fCount is 0 for every other kind of change, which then draws
		from the pieces above as it always has. Filled by KCMStoryDiffRun's AddAttrChange. */
	KCMStoryLayers	fLayers;
	KCMStoryLayers	fOtherLayers;

	/** ★★**A WHOLE PARAGRAPH, ADDED OR REMOVED** (2026-09-17 afternoon, the user's rule: "a <p> added or
		removed is a paragraph added or removed", one row per paragraph). The ranges then carry the
		paragraph's BREAK as well as its words - measured the same day: without it a paragraph taken in
		ran into the next one, and one taken out left an empty paragraph behind.
		The break is the one BEFORE the paragraph ("\rNEW" right before the return of the paragraph it
		follows - the way pressing Return puts a paragraph in, and joining two paragraphs keeps the upper
		one's style), or the paragraph's own when it is the first of its place ("NEW\r" at the start of
		the one it precedes). KCMStoryDiffRun's AddWholeParagraphs makes them; KCMStoryRestore gives a
		paragraph taken in the next style of the one before it.
		⚠**THE BREAK IS FOR THE WRITE, NOT FOR THE READER** (2026-09-19): shown as it stands, the range
		put the mark and the selection on the END OF THE PARAGRAPH ABOVE (its return). fBreakAt below
		says which end holds it, and the facade cuts it off everything it hands out (KCMShownSpan). */
	bool16		fWholeParagraph;

	/** kTrue for a paragraph to take in whose paragraph BEFORE it is also one to take in - the second
		"+" of "+ +". Taking it in first gives it the next style of whatever stands before it in the
		document then, which is not what taking them in order gives; the panel asks first (the user's
		request). */
	bool16		fAfterNewParagraph;

	/** ★**WHERE THE CHANGED WORDS STAND: the body, a table cell, or a footnote** (2026-09-19, the user:
		"for a change inside a cell, show Cell Text in the ID column; Text for an ordinary one"). A
		KCMStoryPlace value. Decided by the diff from the paragraph the change's target position falls
		in (KCMStoryDiffRun's MarkPlaces), the way fOverset is; the panel only reads it.
		⚠Appended at the END, for the reason stated above fReplacedCount's neighbours. */
	int32		fPlace;

	/** ★**WHERE IN A WHOLE PARAGRAPH'S RANGES THE BREAK STANDS** - a KCMStoryBreakAt value (2026-09-19,
		the user: "the mark reaches the end of the paragraph above - I want that gone").
		The ranges above are what the WRITE uses, and they hold the break on purpose (fWholeParagraph says
		why). What the reader is SHOWN - the standing marks, the jump's flash and centring, the double
		click's selection - is the paragraph's words alone, and this says which end to cut to get them.
		The cutting is done in ONE place, the facade's GetChange (KCMFacades.cpp, through KCMShownSpan), so
		that nothing on the UI side has to know the break is there. kKCMBreakNone for every other change. */
	int32		fBreakAt;

	/** ★**THE PARAGRAPH STYLES A WHOLE-PARAGRAPH TAKE-IN FOUND, BEFORE IT WROTE** (2026-09-19, the user:
		"a b c styled A B C: take b out and put it back, and everything reads C"). Read by KCMStoryRestore
		at the moment of the take-in, one entry per paragraph from the one holding fBeforeStart on: the
		paragraph before (when fBreakAt is kKCMBreakLeads), the paragraph going out, and the following
		paragraphs the take-out moved along their next styles. Empty for every other change.
		★**"Undo the Restore" puts them back in TWO LAYERS** (the user's rule): the paragraph before gets
		  what it had; the paragraph put back and the ones after it get the NEXT STYLE of the paragraph above
		  when that style names one, and what is remembered here when it does not ([Same Style]) -
		  KCMRestoreParagraphStyles. Without a record the chain alone is used, as the take-in itself does.
		⚠UIDs, not names: a style renamed meanwhile still applies; one deleted meanwhile is skipped. */
	std::vector<UID>	fBeforeParaStyles;

	/** ★**AND WHAT THE TAKE-IN LEFT THOSE SAME PARAGRAPHS WEARING** - one entry per entry of
		fBeforeParaStyles: the paragraph before as the take-in left it, kInvalidUID for the paragraph that
		went out (it is not there to read), and for each following paragraph the style the take-out moved
		it to (its own, where the chain did not move it).
		★**THE UNDO PUTS A FOLLOWING PARAGRAPH BACK ONLY WHILE IT STILL WEARS THIS** (2026-09-19 evening,
		  measured: a, b, c taken out in turn and a put back gave z - the paragraph that had stood after
		  c all along - the next style of A, because the record made for a still counted b and c below it
		  and z was standing in b's place). The paragraphs below a change can be taken out and put back
		  by OTHER changes between its take-in and its undo, so the record cannot know by position which
		  paragraph it is looking at; what it can know is whether the paragraph there still looks the way
		  it left it. One that does not stops the walk - a different paragraph, or one the reader
		  restyled by hand, and neither is this record's to change. The paragraph put back itself is
		  never checked: the write just gave it whatever it inherited. */
	std::vector<UID>	fAfterParaStyles;

	/** ★**A WHOLE CELL, ADDED OR REMOVED** (2026-09-19 night, the user: "when a table appears where there
		was nothing, the rows should say Cell +"). kTrue on a whole-paragraph change whose paragraph is a
		cell paragraph AND whose cell has NO paragraph outside the run the change came from - every
		paragraph of that cell was added (or removed) together, so the cell itself is what is new (or gone).
		A paragraph added INSIDE a cell that already stood - the cell's other paragraphs are paired with the
		other side - keeps kFalse and is a "Paragraph" like any other.
		★Decided from the paragraphs, not from the grid address: a column inserted at the left shifts every
		  address after it, while "does this cell have a paired paragraph" does not move. Which column it
		  was is not named (the user: "knowing that a column was added is enough").
		★The ID column says "Cell" for it (KCMStoryTreeWidgetMgr::PlaceIdLabel), the script door's `whole`
		  column says 2. Nothing is written back for it - no menu offers to (kKCMWriteBlockedPlaces stands;
		  the user: "to remove a table, select it and delete it").
		⚠Appended at the END, for the reason stated above fReplacedCount's neighbours. */
	bool16		fWholeCell;

	KCMStoryChange()
		: fKind(kReplace), fWhat(kText), fTargetStart(0), fTargetEnd(0), fRubyGroup(kFalse), fOtherRubyGroup(kFalse),
		  fSourceStart(0), fSourceEnd(0),
		  fAttrKind(kKCMStoryAttrNone), fOverset(kFalse),
		  fReplacedCount(0), fReplacedStart(0), fReplacedEnd(0),
		  fBeforeStart(0), fBeforeEnd(0), fWriteBlock(kKCMWriteAllowed),
		  fWholeParagraph(kFalse), fAfterNewParagraph(kFalse), fPlace(0), fBreakAt(0), fWholeCell(kFalse) {}
};

/** KCMStoryChange::fBreakAt - which end of a whole paragraph's range holds the paragraph break that the
	write needs and the reader is not shown. */
enum KCMStoryBreakAt
{
	kKCMBreakNone	= 0,	///< no break in the range (every change that is not a whole paragraph)
	kKCMBreakLeads	= 1,	///< the FIRST character is the return of the paragraph before ("\rNEW")
	kKCMBreakTrails	= 2		///< the LAST character is the paragraph's own return ("NEW\r")
};

/** The span the reader is SHOWN for a change: [from, to) with the paragraph break cut off it, as fBreakAt
	says. An empty range (a caret) is left alone - there is nothing in it to cut. Applied by the facade to
	every range it hands out (KCMFacades.cpp) and by the diff where it asks a question on the reader's
	behalf (KCMStoryDiffRun's MarkOverset). */
inline void KCMShownSpan(int32 breakAt, TextIndex& from, TextIndex& to)
{
	if (to <= from)
		return;
	if (breakAt == kKCMBreakLeads)
		++from;
	else if (breakAt == kKCMBreakTrails)
		--to;
}

/** KCMStoryChange::fPlace - and IKCMStoryEditsFacade::Change::fPlace, which carries the same value.
	★Numbers, not an enum class, so that the facade's plain int32 and this agree by definition. */
enum KCMStoryPlace
{
	kKCMPlaceBody = 0,
	kKCMPlaceCell = 1,
	kKCMPlaceNote = 2
};

/** One row of the Story Edits section. */
struct KCMStoryRow
{
	/** The story, IN THE DOCUMENT THAT HOLDS IT -- the target for every row except a Removed one,
		which exists only in the source. Which document that is, is answered by
		fKinds & kKCMStoryKindRemoved, and by nothing else on this row: a second field naming the
		document could disagree with the kind.

		@warning **every field below that names a place is read out of that same document.** fText,
		fFrameUID, fPageUID and fPageIndex all come from one db, chosen per row in Build. */
	UID			fStoryUID;
	PMString	fText;		// first readable words. NOT shortened for display - the row's text cell
							// is kEllipsizeMiddle and does that itself, at whatever width it has.
							// ★A table in them stands as the sign U+25A6 (2026-09-19; KCMStoryList.cpp,
							//   kKCMTableSign) - display only, the child rows and the diff never see it
	uint32		fKinds;		// OR of KCMStoryChangeKind - named on the right of the row
	UID			fFrameUID;	// the story's FIRST frame - what a click scrolls to. kInvalidUID for an
							// unplaced story (no frame at all), which cannot be jumped to
	UID			fPageUID;	// where the story starts; kInvalidUID when it starts on the pasteboard
	int32		fPageIndex;	// sort key. kMaxInt32 when there is no page, so those sink to the end.
							// @warning it is the SECOND key: removed rows are grouped after every
							// target row first, and only then ordered by this (RowIsBefore)

	/** The differences found inside this story, in reading order.

		**EMPTY IN THE PIXEL MODE**, and that is what makes the tree flat there: the hierarchy
		adapter asks how many children a row has and gets 0, so no branch grows. Nothing has to
		switch trees between the modes.

		**ALSO EMPTY when the story could not be compared** -- it had no partner in the older
		document (an added story), or the edit distance ran past the limit, or the length check
		failed. The row still appears; only the detail is missing. A story that changed must
		never disappear because the detail could not be worked out. */
	std::vector<KCMStoryChange> fChanges;

	/** The Target story's text change counter (ITextModel::GetTextChangeCount) at the moment
		fChanges were built. "Restore Source Text" (KCMStoryRestore.cpp) refuses to write into a
		story whose counter has moved since: the change's positions name the text as it was then,
		and an edit in between would put the older words somewhere else. 0 until the diff runs. */
	uint32		fTargetTextCount;

	/** Whether the two versions' TEXT was actually put side by side for this row.

		**IT IS WHAT MAKES AN EMPTY fChanges READABLE**, and that is the whole reason it exists.
		Three quite different situations leave a row with no children, and without this field they
		are indistinguishable in the panel:
		    the pixel mode          -- no text diff is ever run
		    could not be compared   -- added story, or the diff refused it
		    compared, and the same  -- the words agree; only formatting or a table moved
		Only the third one is news the reader wants, and it is the one they see after repairing a
		story and refreshing it. With this, the row can say "None" for that case alone.

		@warning **not "the story is unchanged".** The change counters moved or the row would not be
		  here (KCMStoryStamp.h); what this says is that the WORDS come out the same. */
	bool16		fTextCompared;

	/** WHICH KIND OF ATTRIBUTE the children found a difference in, when they found one -- so that
		the row can name it ("Ruby") rather than falling back on "Attr".

		**A NUMBER, NOT A FLAG**, so that a second attribute is one more value here and one more
		label -- not another field, and not another branch in every place that draws a row. Kenten is
		that second value: withdrawn in August and reported again from 2026-09-01, and **both times
		the comparison alone decided it** -- which is the shape working as intended.
		@warning **not part of fKinds.** That one comes from the two documents' change COUNTERS, and
		  a row refresh deliberately leaves it alone because reading the counters again gives the
		  same answer. This is a finding of the DIFF -- it does not exist until the two versions have
		  been compared -- so putting it there would break that promise. */
	KCMStoryAttrKind fAttrKind;

	/** HOW MANY DIFFERENT KINDS of attribute the children found - 0, 1 or 2 today.

		★THE ONE MORE FACT SetRowChanges said a "Ruby+" would need (2026-09-03, user's ask: a story
		whose text stood still while BOTH its ruby and its kenten moved read as "Ruby" alone, and
		the kenten half of the edit was invisible from the list). fAttrKind names the FIRST kind
		seen; this says whether there were more, and KindLabel appends the same '+' it appends to
		"Text" when a second counter moved. **A count and not a second kind field**, so that a
		third attribute costs nothing here: the label is "first kind" plus "there is more".
		⚠Worked out beside fAttrKind, from the same walk, and nowhere else. */
	int32			fAttrKindCount;

	/** Whether the diff found a change in the WORDS - as opposed to in something standing over them.

		★★★**IT EXISTS BECAUSE A NOTE'S MARKER IS A CHARACTER** (2026-09-08). The Change column
		names an attribute only when the words did not move, and it asked the CHANGE COUNTERS for
		that. Ruby and kenten are not characters, so the counters agreed with the diff and nobody
		noticed the difference between the two questions. A footnote reference IS a character: adding
		one moves the text counter, so the counters say "the words changed" while the diff - which
		takes the marker out of the text on purpose (KCMTextRead) - has found nothing of the kind.
		The row then read "Text+" for an edit whose only visible difference was a note.
		⇒ **The column now asks what was FOUND, not what the counters reported.** The counters are
		still the fallback for a row nobody diffed (KCMStoryRowFilter.h says where).
		⚠It is the diff's answer, so it means nothing unless fTextCompared is kTrue. */
	bool16			fHasTextChange;

	/** The changes in this row's story that the reader has already replaced, ★**IN READING ORDER**
		(2026-09-19): ascending by fReplacedStart, and two that stand at the SAME position in the
		order their words stood in the text. ⚠Until 2026-09-19 two at one position stood in the order
		they were taken in, and that order is not knowable from the positions - so a write at that
		position could not tell which of them it was in front of, and pushed the wrong one: two new
		paragraphs taken out and put back came back as "¶ba", or the first was refused as "no longer
		where it was" (its record pushed to -2). ReplacedSlotFor / AddReplacedChangeAt keep the order;
		ShiftReplacedChanges moves only the records from a given slot on.

		★★**THEY ARE NOT IN fChanges, AND THAT IS WHAT MAKES THE RE-DIFF SAFE.** Every replacement
		is followed by comparing the story again (KCMStoryRestore.cpp), which clears fChanges and
		refills it from what the comparison finds - and finds nothing where the words now agree.
		Anything living there would be thrown away on the next write. This list survives it, and
		the panel is handed the two merged in text order (KCMStoryRowMerge).

		**EMPTIED BY "Refresh Story Comparison"**, which is a fresh start (the user's call,
		2026-09-15) and so clears this as well as the diff. Empty in every other mode. */
	std::vector<KCMStoryChange> fReplacedChanges;

	/** ★What an IMPORT could not put into this story, one entry each (2026-09-19, the user's ask).
		Shown BEFORE fChanges and fReplacedChanges, each with a red "!" in the Δ column - and the row
		itself carries kKCMStoryKindRefused, which is what puts it at the top of the list (RowIsBefore).

		**NOT IN fChanges, for the reason fReplacedChanges is not:** RunOne empties that on every
		refresh. These are facts about the FILE the reader handed over, not about the text, so a
		refresh cannot find them again. Build refills them from the import's own list
		(KCMImportRefusals), which lives as long as the origin does. */
	std::vector<KCMStoryChange> fRefusals;

	KCMStoryRow()
		: fStoryUID(kInvalidUID), fKinds(kKCMStoryKindNone), fFrameUID(kInvalidUID),
		  fPageUID(kInvalidUID), fPageIndex(kMaxInt32), fTextCompared(kFalse),
		  fAttrKind(kKCMStoryAttrNone), fAttrKindCount(0), fHasTextChange(kFalse), fTargetTextCount(0) {}
};

/** The first frame a story is placed in -- where a jump to that story should go.

	**TWO DOCUMENTS ASK THIS, WHICH IS WHY IT IS NOT PRIVATE TO THE LIST.** Building the rows asks
	the target for it, and a click asks the SOURCE for the same story's frame, because the two
	versions can hold the story in DIFFERENT PLACES -- the older window cannot be aimed by page
	number alone. Matching by story UID works for the same reason the whole feature does: saving
	under a new name carries the UIDs across (KCMStoryStamp.h, "WHY TWO VERSIONS CAN BE MATCHED
	AT ALL").

	@warning for two documents that are NOT versions of each other a UID means nothing in common,
	 and the rows simply come out as "Added" -- the same reading as everywhere else in this
	 feature. Report it plainly; do not try to detect it.

	@param db which document to ask.
	@param storyUID the story. Anything that is not a placed story answers kInvalidUID.
	@return the first frame's UID, or kInvalidUID when there is no story there or it sits in no frame.
*/
UID KCMStoryFirstFrameUID(IDataBase* db, UID storyUID);

/** Where a story BEGINS on the page, as a pasteboard point -- what a jump to it should centre.

	The first frame's centre is not the same thing: in a tall frame the centre is the middle of the
	text, and what a reader wants is the beginning of it. So this walks the parcels forward from the
	first and takes the leading corner of the first one actually placed.

	**VERTICAL TEXT NEEDS NO SPECIAL CASE, AND THAT IS MEASURED FOR THIS CORNER** rather than
	inherited from the overset scan's measurement of the opposite one. The corner is taken in
	PARCEL-LOCAL coordinates and GetParcelToFrameMatrix absorbs the writing direction. Two frames of
	identical size in one document, one vertical and one horizontal, with the point printed beside
	all four corners of its own frame in one coordinate space:
	    vertical   -> the point landed on the frame's TOP-RIGHT, where line 1 begins (DOM: h=146.75
	                  against a right edge of 150, baseline 22.86 against a top edge of 20)
	    horizontal -> the point landed on the frame's TOP-LEFT, where line 1 begins (DOM: h=20.00)

	**AND THE FRAMES ARE NOT ROTATED** -- rotationAngle was 0 for both. It is the INNER COORDINATE
	SPACE of a vertical frame that is turned a quarter turn, so IGeometry's inner rectangle has its
	Left/Right running down the PAGE'S VERTICAL axis and its Top/Bottom across it (measured: the
	inner rectangle's Top-to-Bottom span came back as the frame's WIDTH). Anything that reads an
	inner rectangle and takes "Left" to mean "towards the left of the page" is wrong on vertical
	text. This file is safe because it never interprets a corner -- it only hands corners to the
	matrices. @warning do not add a writing-direction branch here.

	@param db which document to ask -- the newer one for the click, the older one for its window.
	@param storyUID the story.
	@param outFrame [out] the frame that beginning sits in. Untouched when this answers kFalse.
	@param outPb [out] the point, in pasteboard coordinates. Untouched when this answers kFalse.
	@return kFalse when there is no story there, or none of its parcels are placed -- callers fall
		back to centring the first frame (KCMStoryFirstFrameUID).
*/
bool16 KCMStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb);

/** Where ONE character of a story sits, in pasteboard coordinates -- the point a jump to a CHANGE
	should centre, as against the story's beginning above. A row has to land on the edit, not on the
	top of the story the edit is in.

	**WHAT IS RETURNED IS THE CARET'S PLACE, not the middle of the character:** the horizontal
	figure is the escapement up to the glyph BEFORE index, which is where the caret stands when you
	click just in front of that character. The vertical figure is the middle of that line's height,
	so the line -- not its baseline -- is what ends up in the middle of the window.

	It follows vertical text and rotated frames with no branch of its own, because the position
	comes out of the wax run's own to-pasteboard matrix.

	@warning **THE CALLER MUST HOLD A IDataBase::SaveRestoreModifiedState.** This composes the story
	 if the composition is out of date, and composing dirties the document -- unavoidably, because
	 where a character sits IS the composition (see the note on the implementation).

	@param db which document to ask -- either version; the caller picks.
	@param storyUID the story.
	@param index the character. **An index outside the story AS IT STANDS NOW answers kFalse here**,
		rather than being left to the caller to clamp: the source side is handed an index the diff
		worked out against the OLDER document, which nothing has measured against that document as
		it stands now.
	@param outPb [out] the point. Untouched when this answers kFalse.
	@return kFalse when the story is not there, or that position is OVERSET or in no frame --
		callers fall back to KCMStoryStartPoint.
*/
bool16 KCMStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb);

/** Which frame holds ONE character of a story -- the frame a jump to a CHANGE has to bring into
	view, as against KCMStoryFirstFrameUID above, which answers where the story STARTS.

	**WHY A JUMP NEEDS THIS AND NOT THE FIRST FRAME.** Pasteboard coordinates are spread-relative,
	so the view has to be showing the right spread before a point means anything (KCMChangeNav.cpp's
	KCMEnsureSpreadInView says so in as many words). In a story threaded across several spreads the
	first frame names the wrong spread for any edit that is not in it, and the scroll then lands on
	another page entirely rather than slightly off. Both windows had that fault for as long as they
	were given the story's first frame instead.

	**IT COMPOSES FIRST, and so must anything else the same jump asks:** the frame a character is in
	and the point it sits at are both readings of the composition, and a jump that takes one from
	each of two different compositions scrolls to a point that belongs somewhere else.

	**RETURNS THE PAGE ITEM, not the text column** -- unlike KCMStoryFirstFrameUID, which returns a
	column UID. The column is what holds the text; its parent is the frame with the geometry.

	@warning **THE CALLER MUST HOLD A IDataBase::SaveRestoreModifiedState**, for the same reason
	 KCMStoryPointAt's caller must: composing dirties the document.

	@param db which document to ask -- either version; the caller picks.
	@param storyUID the story.
	@param index the character. Outside the story as it stands now answers kInvalidUID.
	@return kInvalidUID when there is no such story, no such character, or the character is OVERSET
		or in no frame -- callers keep whatever fallback frame they already had.
*/
UID KCMStoryFrameAt(IDataBase* db, UID storyUID, TextIndex index);

namespace KCMStoryList
{
	/** Replace the list with one row per entry in diffs, each read out of the document that holds it.

		Rows come out in page order. A story that starts on the pasteboard, or on a master page, has
		no page index and sorts to the end rather than being dropped -- it is still a real edit.

		**REMOVED ROWS ARE READ OUT OF THE SOURCE** and are grouped after every target row, in the
		source's own page order. Their page numbers belong to the OLDER document, and mixing them
		into the target's numbering would put two documents' page numbers in one column with
		nothing to tell them apart. It is the order Export Changed Pages already uses -- target
		pages, target masters, then the source's deletions.

		Reads only. Nothing here composes, which is the property KCMStoryStamp.h records under
		"READING COUNTERS COMPOSES NOTHING": looking at what changed costs no recomposition.

		@param targetDB the newer document. nil clears the list.
		@param sourceDB the older document -- where a Removed row's story, text and page are read
		       from. nil is tolerated: those rows are then dropped, exactly as an unreadable story is.
		@param diffs what KCMStoryEdits::Compare produced for this comparison.
	*/
	void Build(IDataBase* targetDB, IDataBase* sourceDB, const std::vector<KCMStoryDiff>& diffs);

	/** Empty the list. Called on Stop and when a compared document closes. */
	void Clear();

	int32 GetRowCount();

	/** The nth row, or nil when nth is out of range. Callers get a pointer rather than a reference
		so that an index the tree asks for after the list was rebuilt cannot walk off the end.
	*/
	const KCMStoryRow* GetRow(int32 nth);

	/** A row's fText for text INDESIGN WILL TYPESET (the PDF report), with the table sign spelled
		as the word `[table]`: the palette's UI font draws U+25A6, a document's default font does
		not (2026-09-19 - it came out as the notdef box). The panel, the facade and the script door
		keep the sign; only what goes into a document passes through here. */
	PMString RowTextForTypesetting(const PMString& rowText);

	/** Give row nth the differences the text diff found inside it (Story Changes mode).

		**A SEPARATE STEP FROM Build, ON PURPOSE.** Build orders the rows by page, and a change
		names its row by position in that finished order -- so the diff runs after the ordering,
		not inside it. Doing both at once would mean the diff had to know the sort key.

		**THE ONLY WAY TO WRITE A ROW.** GetRow hands out a const pointer precisely so that nothing
		can edit the list behind its back; this is the one door, and it is the one KCMStoryDiffRun
		knocks on.

		**THE TWO FACTS TRAVEL TOGETHER.** "What differs" and "was it compared at all" are answered
		by one attempt and are meaningless apart: an empty list means nothing until you know whether
		anybody looked. Two setters would let a caller write one and forget the other, and the row
		would then be claiming something nobody measured ([[one-question-one-place]]).

		@param nth the row. Out of range does nothing -- the list may have been rebuilt underneath
			a caller that is still walking the previous one.
		@param changes what to attach. Copied; the caller keeps ownership of its own vector.
		@param textCompared kTrue when the two versions' text was actually diffed -- see
			KCMStoryRow::fTextCompared. An empty `changes` with kTrue is "the words agree";
			with kFalse it is "nobody could look".
	*/
	void SetRowChanges(int32 nth, const std::vector<KCMStoryChange>& changes, bool16 textCompared);

	/** Record the Target story's text change counter for row nth (KCMStoryRow::fTargetTextCount),
		read by the caller at the moment it attached the row's changes. Out-of-range nth is ignored. */
	void SetRowTargetTextCount(int32 nth, uint32 count);

	/** Keep `done` on row nth as a change the reader has already replaced (the Import mode).

		★**A DOOR OF ITS OWN, BESIDE SetRowChanges.** GetRow hands out a const pointer precisely so
		that nothing edits a row behind the list's back, and a replaced change is written at a
		different moment than the diff's children are - after the story has been compared again.
		Out-of-range nth is ignored, as everywhere else here.

		⚠**INSERTED IN fReplacedStart ORDER, NOT APPENDED**, after any record standing at the same
		position. ★**FOR PUTTING A KEPT LIST BACK** (DropUndoneReplaced), where the records come in
		the order they already had. ⚠**NOT for a record that has just been WRITTEN** - a write can
		leave its record at the same position as one that stands AFTER it in the text (a take-out
		of two adjacent paragraphs), and only the slot asked for BEFORE the write knows which side it
		belongs on: ReplacedSlotFor, then AddReplacedChangeAt.
		@see KCMStoryRow::fReplacedChanges for why it is not simply appended to fChanges. */
	void AddReplacedChange(int32 nth, const KCMStoryChange& done);

	/** ★**WHERE A RECORD FOR A WRITE AT `at` BELONGS** in row nth's replaced list (2026-09-19): after
		every record whose fReplacedStart is before `at`, and after every CARET (a record with no
		characters) standing exactly at `at` - the same tie rule KCMStoryRowMerge shows the panel (a
		replaced change is the thing standing there; the live one is beside it) - and before the rest,
		a record WITH characters starting at `at` included (its words stand from `at` on, so the write
		goes in front of them or over them; the re-check of 2026-09-19 night). ⚠**ASK BEFORE THE
		WRITE**, while the positions are the ones the write is about to be made against.
		@return the slot: also the first slot ShiftReplacedChanges moves for this write. */
	int32 ReplacedSlotFor(int32 nth, TextIndex at);

	/** Put `done` into row nth's replaced list at `slot` (from ReplacedSlotFor, plus however many
		records the caller has put in before it since). Out-of-range slots are clamped. */
	void AddReplacedChangeAt(int32 nth, int32 slot, const KCMStoryChange& done);

	/** The slot in row nth's replaced list that merged index `which` names, or -1 when that index
		is out of range or names a refusal or a LIVE change. The one place the merged index is turned
		into a position in fReplacedChanges (GetMergedChange makes the same walk). */
	int32 ReplacedSlotOfMerged(int32 nth, int32 which);

	/** Take the record at `slot` out of row nth's replaced list. Out of range does nothing.
		@return kTrue when a record went. */
	bool16 RemoveReplacedChangeAt(int32 nth, int32 slot);

	/** Forget row nth's replaced changes. "Refresh Story Comparison" is a fresh start (the user's
		call, 2026-09-15), so it clears these as well as the diff. Out-of-range nth is ignored. */
	void ClearReplacedChanges(int32 nth);

	/** Mark the row of `storyUID` as one an import could not fill (kKCMStoryKindRefused), making the
		row when the comparison built none - the story's counter did not move because nothing went in.

		★**CALLED FROM Build AND NOWHERE ELSE** (2026-09-19): the list starts empty on every build,
		  so the "!" rows are put back each time from KCMImportRefusals. ⚠Before the sort.
		★★**A STORY THE DOCUMENT DOES NOT HOLD** (a file named after a uid that is not there, or names
		  something that is not a story) gets a row that stands for the FILE: `textWhenNoStory` in the
		  text cell, no frame, no page - **and fStoryUID = kInvalidUID**. That is the one value every
		  reader of this list already passes over (the diff, the undo observer, the marks, the jump),
		  so a uid that names nothing - or a different object - is never handed to the document.
		@return the row's index, for AddRefusalChange. */
	int32 AddRefusalRow(IDataBase* targetDB, UID storyUID, const PMString& textWhenNoStory);

	/** One refusal under row `nth`: `kind` for the ID column, `whereAndWhy` for the text cell.
		By index rather than by uid, because a row standing for a file has no uid. Out of range does
		nothing. ⚠Before the sort, like AddRefusalRow: the index is only good until then. */
	void AddRefusalChange(int32 nth, const PMString& kind, const PMString& whereAndWhy);

	/** Move row nth's replaced records FROM `firstSlot` ON to follow a write that removed `removed`
		characters at `from` and put `inserted` in their place.

		★★**BECAUSE THE RE-DIFF DOES NOT TOUCH THEM.** Comparing the story again names the LIVE
		changes afresh against the text as it now stands, which is exactly why a second
		replacement works at all - but a change that has already been replaced is no longer in
		that comparison, so nothing would move it. Replacing words earlier in the story makes the
		text longer or shorter, and everything after it slides by that much.

		★★★**WHICH RECORDS MOVE IS DECIDED BY SLOT, NOT BY POSITION ALONE** (2026-09-19). A record
		is a CARET when what it took in was an insertion (its words are gone), and two carets can
		stand at one position - the two new paragraphs of the user's report, after both were taken
		out. Position cannot say which of them the write is in front of; the list's order can, and it
		is kept in reading order for exactly this. The caller names the first slot to move: for a
		take-in, ReplacedSlotFor (the records at or before the write stay); for "Undo the Restore",
		the slot after the record being undone (everything before it in the text stays).
		★A record from that slot on that stands past the removed characters slides by
		 `inserted - removed`; one standing INSIDE them (its words were just overwritten by another
		 change - possible only when the diff swallowed it into a wider one) is collapsed to a caret
		 after the new words, where an undo of it is refused rather than written somewhere wrong.
		⚠**THE OLD RULE WAS "position >= from moves, by delta"** and it did two wrong things to a
		 caret standing exactly at `from`: a removal starting there pushed it NEGATIVE (the first
		 paragraph's record went to -2 and its undo was refused as "no longer where it was"), and an
		 insertion there pushed a record that stood BEFORE the write along with the ones after it
		 (two paragraphs put back came out as "¶ba"). Both measured on the running application.
		⚠Call it BEFORE the new record is added. Out-of-range nth does nothing. */
	void ShiftReplacedChanges(int32 nth, int32 firstSlot, TextIndex from, int32 removed, int32 inserted);

	// ---- what the panel sees: the two lists as one ------------------------------------------
	//
	// ★★★**ONE INDEX SPACE, DEFINED IN ONE PLACE.** The panel asks four separate questions about
	//   "change number N of row M" (how many, which one, which attribute, does it carry a value)
	//   and "Restore Source Text" asks a fifth. Letting some of them count the live changes and
	//   others count the merged list would not fail loudly - it would answer about the WRONG
	//   CHANGE, which is the shape of bug this plug-in has spent the most time on
	//   ([[one-question-one-place]]). So every one of them goes through the two below.

	/** How many children row nth shows: the refusals first (2026-09-19), then the live diff's changes
		and the replaced ones merged in text order. */
	int32 GetMergedChangeCount(int32 nth);

	/** The change a merged index names, or nil when either index is out of range.

		★The first fRefusals.size() indices are the refusals, in the order the import noted them; the
		  rest are the live and the replaced changes in text order (KCMStoryRowMerge).
		@param outIsReplaced kTrue when it came from fReplacedChanges. ⚠**Ask this rather than
			looking at the change itself**: fReplacedCount says when it was replaced, not whether
			it is being SHOWN as replaced, and the two differ after an undo. */
	const KCMStoryChange* GetMergedChange(int32 nth, int32 which, bool16& outIsReplaced);

	/** Drop the rows whose story differs only in HOW IT IS SET -- a font, a colour, a style, a
		table stroke -- and keep the ones whose CONTENT differs: the words, or the ruby written over
		them. Text and ruby are the whole of what the Story Edits list reports.

		**THE RULE IS KCMStoryRowFilter.h AND IS NOT REPEATED HERE.** It is a free function over
		three plain numbers precisely so that it can be measured outside InDesign
		(work/kescm-rowfilter-test), and so that the two comparison modes cannot drift apart --
		the mode is never asked; the row's own fTextCompared says how much is known about it.

		**CALL IT LAST, AFTER Build AND AFTER THE DIFF** -- and there is no way to make that
		optional. Before the diff, every row in the story mode looks exactly like a row nobody
		diffed, so a story whose only edit was a ruby would be dropped on its Attr counter a moment
		before the diff was about to find that ruby. KCMRebuildStoryEdits owns the order and is the
		only caller.

		**IT RENUMBERS THE LIST**, which is safe for exactly one reason: nothing outside is holding
		a row index yet. The changes were attached by position moments earlier (SetRowChanges),
		and the tree is not told to rebuild until KCMRebuildStoryEdits sends its notification
		after this returns. @warning calling it at any later moment -- from a refresh, say -- would
		 move the rows under a tree that is already showing them.

		@warning **a refresh does not call this.** Repairing a story until it compares equal must
		 leave its row standing (KCMStoryDiffRun::RunOne): refreshing answers "what differs now",
		 not "does this row still belong here". This answers the second question, and only while the
		 list is being built.
	*/
	void DropRowsWithNoContentChange();

	/** Read row nth's own fields out of the target document again: the words it shows, the frame a
		click scrolls to, and the page that frame is on.

		**A REFRESH HAS TO RE-READ THE ROW, not only re-run the diff.** "Refresh Story Comparison"
		once left the ROW as the comparison had built it, so a reader who edited the first sentence
		of a story saw the refreshed row still quoting the old one. The row is drawn from the
		document, so a refresh has to read the document for it too -- otherwise the panel is showing
		two different moments in one line.

		@warning **fPageIndex is deliberately not touched.** It is the sort key Build used to ORDER
		 the list, and the list is not re-sorted here -- one row is being refreshed, not the sequence.
		 Writing a new index into a sequence that keeps its old order would leave the two
		 disagreeing, and the disagreement would only show up later as rows that sort wrongly after
		 the next rebuild. A story that has genuinely moved to another page therefore shows its new
		 page and keeps its old place in the list until the next full comparison, which is the
		 honest half-answer.

		@warning **fKinds is not touched either:** it comes from the two documents' change counters
		 rather than from the target's text, and those only ever move forward (KCMStoryStamp.h) --
		 re-reading them would cost a walk of both documents to produce the same answer.

		@param nth the row. Out of range does nothing.
		@param targetDB the newer document. nil does nothing.
	*/
	void RefreshRowFromDocument(int32 nth, IDataBase* targetDB);

	/** Empty the list during a controlled shutdown. See the file comment for why this exists. */
	void ShutdownCleanup();

	/** The whole list as tab-separated text, for app.kcmStoryRows.

		★★★**WHY A READING PORT AT ALL** (2026-09-08). The list's cells are DRAWN BY HAND
		(ui/KCMStoryCellView), so nothing outside this plug-in can see what a row says: not the
		reading over a ruby row, not the "Mono"/"Group" word, not a footnote's number. Checking the
		footnote work that day meant PHOTOGRAPHING the panel and reading the picture, once per
		measurement - the single biggest cost of the day. KBS reached the same answer first
		(app.kfcResults), and its reason is the one that matters here too: **a count proves
		nothing.** "2 changes" is true of the right answer and of several wrong ones.

		★**IT REPORTS FACTS, NOT THE PANEL'S WORDING.** The Change column's word ("Text+",
		"Ruby+", "Endnote") is composed in the UI half (KCMStoryTreeWidgetMgr::KindLabel), which
		lives in another plug-in and cannot be reached from here - and copying that rule into this
		file would be the same judgement in two places ([[one-question-one-place]]), which is how
		the two would come to disagree. What comes out instead is everything the rule is made of,
		so a reader can work out the word and, more importantly, can see WHY it is that word.

		Two kinds of line, told apart by the second column:

		    row  change  uid  kinds  flags               attr      kind     value  text
		    0    -       256  Text   compared,hasText,…  Footnote  -        -      あいうえお。…
		    0    0       -    -      -                   Footnote  insert   1      あいうえお。…
		    0    1       -    -      -                   -         insert   -      注A

		@param out [out] the text. A header line, then the rows. Empty list = header only, which is
			a real answer and reads differently from the property being missing.
	*/
	/** ★**THE CHILDREN ARE IN THE PANEL'S INDEX SPACE** (2026-09-19 night): refusals, then the live
		and the taken-in changes merged in text order - the numbers kcmTakeInChange / kcmUndoRestore
		count in. Eight columns follow `text`: `state` (live / replaced / undone), `whole` (1 for a
		paragraph added or removed whole), `place` (body / cell / note), the SHOWN ranges
		`tstart tend sstart send` (through the facade, so a whole paragraph's break is already cut
		off them - what the marks and the jump use), and `other` (the source side's words). */
	void RowsAsTsv(PMString& out);
}

#endif // __KCMStoryList_h__

// End, KCMStoryList.h.
