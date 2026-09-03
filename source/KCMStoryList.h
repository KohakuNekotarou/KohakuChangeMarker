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

#include <vector>

#include "KCMStoryStamp.h"	// KCMStoryDiff / KCMStoryChangeKind

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

		**IT DECIDES HOW fText / fOtherText ARE TO BE READ.** A text change shows whichever side
		changed, so a deletion puts the newer words in fOtherText; a ruby change always puts the
		target in fText and the source in fOtherText, because the characters are in BOTH versions
		and there is no side to choose.
		@warning anything asking "which document is this text from" has to test this field before
		  fKind. KCMStoryJump's message-area label did not, and labelled a removed ruby's source
		  text "Target Text:". */
	enum What { kText, kAttr };

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

		**WHICH SIDE THESE COME FROM DEPENDS ON THE KIND:** the newer text for a replacement or an
		insertion, and the OLDER text for a deletion. What was removed is precisely what the reader
		needs to see, and the newer side has nothing there to show. (A script reading a report can
		live with the newer side alone and blank deletions; a panel cannot -- it is a column of
		empty rows.)

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

		**"THE OTHER SIDE", NOT "THE OLD SIDE", and the distinction is the whole point.** The row
		already shows the side that CHANGED, and which side that is depends on the kind:
		    replacement -> the row shows the new words,  so this holds the OLD ones
		    insertion   -> the row shows what was added, so this holds the old text with nothing
		                   between the context (there was nothing there to show)
		    deletion    -> the row shows what was REMOVED, so this holds the NEW text -- the words
		                   that closed up over the gap
		Naming it "old" would have made the deletion case a lie, and a deletion is exactly the case
		where the reader most wants to see what is there now.

		fOtherText is empty for an insertion (nothing stood there) and for a deletion (nothing
		stands there now). The two context pieces are not: they are the words on either side, which
		is what makes the empty middle readable as a place rather than as an absence. */
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
		  (KCMSnippetText.h), and fTargetStart/fTargetEnd is the span this change is about. */
	PMString	fRuby;
	PMString	fOtherRuby;

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

	int32		fParaIndex;		// which paragraph it fell in. Not drawn; kept for ordering and for
								// anything later that wants to group changes by paragraph

	KCMStoryChange()
		: fKind(kReplace), fWhat(kText), fTargetStart(0), fTargetEnd(0),
		  fSourceStart(0), fSourceEnd(0),
		  fAttrKind(kKCMStoryAttrNone), fParaIndex(0) {}
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
							// is kEllipsizeMiddle and does that itself, at whatever width it has
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

	KCMStoryRow()
		: fStoryUID(kInvalidUID), fKinds(kKCMStoryKindNone), fFrameUID(kInvalidUID),
		  fPageUID(kInvalidUID), fPageIndex(kMaxInt32), fTextCompared(kFalse),
		  fAttrKind(kKCMStoryAttrNone), fAttrKindCount(0) {}
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
}

#endif // __KCMStoryList_h__

// End, KCMStoryList.h.
