//========================================================================================
//
//  IKCMStoryEditsFacade.h
//
//  Reading the Story Edits list: which stories changed between the two versions, and where a
//  given story begins in a given document.
//
//  Comparing pixels can only say "this page looks different". Story Edits answers the next
//  question -- whether the words changed, only the formatting changed, or something attached to
//  the story such as a table changed -- by matching ITextModel's change counters story by story
//  (KCMStoryStamp.h). This interface is how the panel reads what that produced.
//
//  READ ONLY EXCEPT FOR ONE METHOD, AND THE EXCEPTION IS WORTH READING. The list is built by one
//  comparison and thrown away by the next, and both of those happen in the model (KCMCore.cpp,
//  and the shutdown path in KCMPeek.cpp). No UI file builds it, clears it or empties it at
//  shutdown, so there is no Build() here: **a method on a boundary that nobody calls is a promise
//  nobody keeps.**
//
//  RefreshRow() is the one that writes, and it is here rather than on IKCMCompareFacade beside
//  "Refresh Page Comparison" because of what it is asked in terms of: a ROW NUMBER. That is this
//  list's own vocabulary - the compare facade knows about documents and pages and has never heard
//  of a row - and an interface that has to borrow another one's index to be called is the wrong
//  interface.
//
//  THE LAST FOUR METHODS ARE NOT ABOUT THE LIST. They answer "where does this story start in this
//  document", which the navigation asks of the SOURCE document for a story the list only ever read
//  out of the target -- the two versions can hold the same story in different places. They take a
//  database and a UID and nothing else, so they are model questions: no window has to exist for
//  them to have an answer.
//
//========================================================================================

#ifndef __IKCMStoryEditsFacade_h__
#define __IKCMStoryEditsFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMPoint.h"			// PBPMPoint -- a pasteboard point, what a jump centres on
#include "PMString.h"
#include "UIDRef.h"				// UID

// Project includes:
#include "KCMBoundaryID.h"	// IID_IKCMSTORYEDITSFACADE. The boundary header rather than KCMID.h,
							// for the reason given at the same spot in IKCMCompareFacade.h.
#include "KCMStoryKinds.h"	// KCMStoryChangeKind. A header of TYPES ONLY, which is what a header
							// the UI includes has to be: this used to reach the enum through
							// KCMStoryStamp.h, whose three model-side free functions the UI could
							// then see and could not link to.

class IDataBase;

class IKCMStoryEditsFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMSTORYEDITSFACADE };

	/** One row of the Story Edits list, in the fields the panel actually reads.

		A COPY, NOT A POINTER INTO THE LIST. The model hands out const KCMStoryRow* internally,
		which is safe only because a row is read and used inside one call while nothing can
		rebuild the list underneath it. Across the boundary that reasoning stops being local, so
		the row is copied out and the caller owns it.

		Two of the model row's fields stay behind. fPageIndex is the list's sort key, used while
		the rows are being built and never read afterwards -- putting it on the boundary would
		publish an internal ordering decision as if the UI were entitled to it. fChanges is the
		child list, which is handed over one difference at a time by GetChange below.
	*/
	struct Row
	{
		/** The story, in the document that holds it -- the row's identity, shown as a number.

			WHICH DOCUMENT IS ANSWERED BY fKinds. Every row is a target story except a REMOVED
			one, which exists only in the source. The three fields below are read out of that same
			document, so a caller that jumps to a row must pick its database the same way:
			GetArmedSourceDB() when kKCMStoryKindRemoved is set, GetArmedTargetDB() otherwise.
			@warning a uid is meaningless in the other document -- it can name a different object
			there rather than nothing -- so this is not a detail a caller may skip. */
		UID			fStoryUID;
		PMString	fText;		// the first readable words. NOT shortened here: the row's text cell is
								// kEllipsizeMiddle and decides for itself at whatever width it has
		uint32		fKinds;		// OR of KCMStoryChangeKind -- named on the right of the row
		UID			fFrameUID;	// the story's FIRST frame, what a click scrolls to. kInvalidUID for a
								// story in no frame at all, which cannot be jumped to
		UID			fPageUID;	// where the story starts; kInvalidUID when it starts on the pasteboard

		/** Whether the two versions' TEXT was actually diffed for this row.

			IT IS HOW AN EMPTY CHILD LIST IS READ. GetChangeCount answers 0 for three unrelated
			reasons - the pixel mode diffs nothing, the story could not be compared, or it was
			compared and the words agree - and only the last one is worth telling the reader
			about. kTrue with no children is that case, and the row draws "None" for it.
			@warning it does NOT mean the story is unchanged: the change counters moved, or there
			would be no row (KCMStoryStamp.h). It means the WORDS come out the same. */
		bool16		fTextCompared;

		/** WHICH KIND OF ATTRIBUTE this row's children found a difference in, when they found one.
			**0 = none; 1 = ruby; 2 = kenten.**

			A NUMBER RATHER THAN A FLAG, so that a second attribute costs one more value here and
			one more label - not another field and another branch everywhere. Kenten is that second
			value: it was reported for a day in August, withdrawn, and reported again from
			2026-09-01 (user's call), through this same number - which is the design working. **A
			value must never be renumbered once it has shipped**, and 2 has now shipped twice.

			@warning NOT DERIVED FROM fKinds, which comes from the two documents' CHANGE COUNTERS
			and is deliberately left alone by a row refresh ("read it again and it says the same").
			This one comes from the DIFF - it is only known after the two versions were compared -
			so mixing it into fKinds would break that promise. */
		int32		fAttrKind;

		/** HOW MANY DIFFERENT attribute kinds the children found - 0, 1 or 2 today. fAttrKind names
			the first; this is what lets the row say "Ruby+" when a kenten moved as well (2026-09-03,
			user's ask), the same '+' the row already puts after "Text" when a second counter moved.
			**Added at the END of the struct** - the UI reads Row by value, so the two halves have to
			be built together whenever this struct changes (they always are). */
		int32		fAttrKindCount;

		Row()
			: fStoryUID(kInvalidUID), fKinds(kKCMStoryKindNone), fFrameUID(kInvalidUID),
			  fPageUID(kInvalidUID), fTextCompared(kFalse), fAttrKind(0), fAttrKindCount(0) {}
	};

	// ---- the list ------------------------------------------------------------------------

	/** How many rows the list holds. 0 both when nothing has been compared and when a comparison
		found no edited story -- the two are told apart by asking IKCMCompareFacade::IsArmed(),
		which is what the section heading and the placeholder row already do. */
	virtual int32	GetRowCount() = 0;

	/** Fill out with row nth (0-based). kFalse when nth is out of range, leaving out untouched.

		Asked one row at a time on purpose. Both callers want exactly one: the tree writes the
		widget for a single node, and a click reports the row it landed on. (Contrast the page
		pairing on IKCMMarkData, which is handed over whole because its callers each build a
		map of the lot.) */
	virtual bool16	GetRow(int32 nth, Row& out) = 0;

	// ---- the children: what differs inside a story (the Story compare mode) -----------------

	/** One difference inside a story, in the fields the tree and the click actually read.

		A COPY, for the same reason Row is one, and FLATTENED: the model's enums come across as
		int32. An enum on the boundary would have to be defined somewhere both plug-ins agree on,
		and that is a decision to take when there is a second reason to take it - the row's fKinds
		is already an int with named bits and nothing has suffered for it. */
	struct Change
	{
		/** What fWhat's numbers mean, named.

			THE FIELD STAYS int32 - this is not the enum the note above declined to put on the
			boundary. Nothing about the struct's layout changes; what changes is that a caller
			COMPARING fWhat can say what it is comparing against. GetChangeWhat's whole return
			value is this one number: the tree asks it of every row to decide the row's height, and
			`== 1` at that call site would be a bare number three files away from the only place
			that explains it. */
		enum { kWhatText = 0, kWhatAttr = 1 };

		int32		fKind;			// 0 = replace, 1 = insert, 2 = delete
		// 0 = text, 1 = attribute (ruby, so far).
		// @warning it changes how the three fText / fOtherText pieces are to be read: for a TEXT
		//   change the row shows whichever side changed, so a deletion puts the NEWER text in
		//   fOtherText - but a ruby change always puts the target in fText and the source in
		//   fOtherText, because the characters exist in both versions and there is no side to
		//   choose. Anything deciding "which document is this text from" must look here first.
		int32		fWhat;
		TextIndex	fTargetStart;	// in the NEWER document
		TextIndex	fTargetEnd;		// an END, not a length (RangeData.h:69)
		// IN THE OLDER DOCUMENT, AND ALWAYS A REAL PLACE. An INSERTION comes through with
		//   fSourceEnd == fSourceStart: the spot the new words were typed into, with no characters
		//   of its own. That mirrors what fTargetStart/fTargetEnd already do for a DELETION.
		//   @warning ask fSourceEnd > fSourceStart when what is needed is CHARACTERS. A flag
		//     answering "is there anything to select over there" is not the same question as "is
		//     there a place to look at over there", and every caller that wanted the second one
		//     off such a flag failed to move the older window for an insertion.
		TextIndex	fSourceStart;
		TextIndex	fSourceEnd;

		// The words to show, in three pieces: context, the changed characters, context.
		// For a DELETION they come from the older side's text (see KCMStoryList.h).
		// Concatenated they are one string; the boundary between them cannot be recovered on this
		//   side once it is gone (it is a code point index, and PMString counts UTF-16), which is
		//   why it is carried across rather than worked out here.
		PMString	fTextPre;
		PMString	fText;
		PMString	fTextPost;

		// The OTHER side of the same edit, in the same three pieces - what the panel's message area
		// shows while this row is selected.
		// NOT "the old side": the row already shows whichever side CHANGED, so this is the old
		//   text for a replacement or an insertion and the NEW text for a deletion (where the row is
		//   showing what was removed, and what the reader wants beside it is what stands there now).
		//   The full reasoning is on KCMStoryChange in KCMStoryList.h.
		// The middle is empty where nothing stood on that side; the context pieces are not, which
		//   is what makes an empty middle read as a place rather than as an absence.
		PMString	fOtherTextPre;
		PMString	fOtherText;
		PMString	fOtherTextPost;

		// ---- the readings, and ONLY meaningful when fWhat is 1 (attribute) ----
		//
		// A RUBY ROW SHOWS TWO THINGS AT ONCE - the base text and the reading over it, set the way
		//   ruby actually is - so they cannot be one string. The three fText pieces carry the base
		//   text with its context, exactly as they do for a text change; these carry the readings.
		// fRuby belongs to the side the row shows and fOtherRuby to the other, the same pairing as
		//   fText / fOtherText. Either can be empty: ruby added has no old reading, ruby removed has
		//   no new one.
		// @warning mono and group ruby are both in here and the difference is NOT in the string -
		//   it is in the span (fTargetStart/fTargetEnd): one reading over several characters,
		//   against one reading each. See KCMParaText.h for how the two are told apart in the
		//   snippet.
		PMString	fRuby;
		PMString	fOtherRuby;

		// WHICH attribute this is: 0 = none, 1 = ruby, 2 = kenten (reported again since
		// 2026-09-01 - KCMStoryKinds.h).
		// fWhat SAYS "not the words", THIS SAYS WHAT INSTEAD - and the panel needs both, because
		//   the two are not the same question. fWhat does not promise the value is something a
		//   reader READS: kenten filled fRuby / fOtherRuby with a KIND ("KentenBlackCircle"), and a
		//   message area asking fWhat drew that name over the older text as though it were a
		//   reading.
		// @warning so "does this carry a reading" - and "is this row drawn on two lines" - is THIS
		//   field, never fWhat. Today they happen to give the same answer again, ruby being the
		//   only kind reported; that is exactly the state in which a stand-in survives unnoticed.
		int32		fAttrKind;

		Change()
			: fKind(0), fWhat(0), fTargetStart(0), fTargetEnd(0),
			  fSourceStart(0), fSourceEnd(0), fAttrKind(0) {}
	};

	/** How many differences row nth holds.

		0 IS THE NORMAL ANSWER IN THE PIXEL MODE - nothing runs the text diff there, so no row has
		children and the tree stays one level deep. It is also the answer for a story that could not
		be compared (added, too different, or a failed length check); the row is still there. */
	virtual int32	GetChangeCount(int32 nth) = 0;

	/** Fill out with difference which of row nth. kFalse when either index is out of range,
		leaving out untouched. One at a time, for the same reason GetRow is. */
	virtual bool16	GetChange(int32 nth, int32 which, Change& out) = 0;

	/** WHICH ATTRIBUTE this difference is in - Change::fAttrKind, and nothing else (0 = none,
		1 = ruby, 2 = kenten). 0 for a text change and for an index that names no change.

		WHY THE ONE FIELD HAS A CALL OF ITS OWN. The tree asks this of every row it lays out, to
		decide how TALL the row is - a ruby change is drawn on two lines, the reading above the
		characters it belongs to. GetChange would answer the same question, but it copies nine
		PMStrings to do it, and the height is asked for again on every scroll and every rebuild.
		This copies one int.
		@warning it must answer "which attribute", not fWhat. "Is this an attribute" was a correct
		stand-in for "is this drawn on two lines" only while ruby was the sole attribute; kenten
		was an attribute that is NOT drawn on two lines (its value is a name, not a reading). Ruby
		is once again the only kind reported, which is precisely the state a stand-in survives in. */
	virtual int32	GetChangeAttrKind(int32 nth, int32 which) = 0;

	/** kTrue when this change has something to SHOW on an upper line - a reading, or a kind whose
		mark can be drawn. kFalse for a text change, for an index that names no change, and for an
		attribute that was REMOVED.

		★★WHY IT IS SEPARATE FROM GetChangeAttrKind (2026-09-01, user's call: "when the ruby or the
		kenten is gone, make it one line"). The kind says which attribute differs; this says whether
		the newer version still carries one. A removed ruby is still a ruby change - so the row must
		still be able to name it - but there is nothing to put above the text, and a row laid out on
		two lines with an empty upper one is a gap the reader has to interpret.
		⚠**The row's height and the row's drawing must ask the same question**, which is why this
		 is one call rather than each of them testing the string it happens to hold.
		★AS CHEAP AS GetChangeAttrKind: it reads one field off the change, no strings copied. */
	virtual bool16	GetChangeHasAttrValue(int32 nth, int32 which) = 0;

	/** Compare row nth's story again against the older document, and replace its differences with
		what stands there now - "Refresh Story Comparison" on the row's right-click menu.

		THE ROW IS BROUGHT UP TO DATE AS WELL AS ITS CHILDREN - the words it shows, the frame a
		click scrolls to and that frame's page are all re-read from the target document. (Refresh
		the children alone and the row goes on quoting the sentence the reader has just rewritten.)
		@warning two things are deliberately left alone: the kinds named on the row's right, which
		come from the change counters rather than from the text, and the row's PLACE in the list -
		one row is being refreshed, not the order.

		THE ROW STAYS, whatever comes of it. A story brought back into agreement comes out with
		NO children and keeps its row, marked "None" - the panel is answering "what differs now",
		not "does this row still belong here". See KCMStoryDiffRun.h for why the change counters
		cannot answer the second question anyway.

		The model tells the panel to redraw the list itself, through the same notification a
		comparison sends. The caller does not have to rebuild anything.

		@param nth the row, in the order the list is in now.
		@return how many differences the row has after this, or -1 when the story could not be
			compared - out of range, an added story, or the diff refused it. Nothing is redrawn
			for a -1: the list is exactly as it was. */
	virtual int32	RefreshRow(int32 nth) = 0;

	// ---- where a story begins, in either document ------------------------------------------

	/** The first frame this story is placed in. kInvalidUID when the document has no such story
		or it sits in no frame.

		Matching by story UID works because saving under a new name carries the UIDs across
		(KCMStoryStamp.h). For two documents that are not versions of each other nothing lines up,
		and the rows simply come out as "Added". */
	virtual UID		GetFirstFrameUID(IDataBase* db, UID storyUID) = 0;

	/** Where the story BEGINS on the page, as a pasteboard point -- what a jump should centre.

		Not the same as the first frame's centre: in a tall frame the centre is the middle of the
		text and what a reader wants is the beginning of it.

		@param outFrame [out] the frame that beginning sits in. Untouched when this answers kFalse.
		@param outPb [out] the point, in pasteboard coordinates. Untouched when this answers kFalse.
		@return kFalse when there is no such story, or none of its parcels are placed -- callers
			fall back to centring GetFirstFrameUID's frame. */
	virtual bool16	GetStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb) = 0;

	/** Where one CHARACTER of a story sits, in pasteboard coordinates - what a jump to a change
		centres on, rather than the story's beginning above.

		The point is where the CARET would stand in front of that character, and vertically the
		middle of its line. Ported from KBSJump.cpp, which carries the same recipe from KESCL - see
		the implementation in KCMStoryList.cpp for what the two earlier copies paid for.

		@warning UNLIKE EVERY OTHER READ ON THIS BOUNDARY, THIS ONE CAN DIRTY THE DOCUMENT: where a
		  character sits is a result of composition, so an out-of-date composition has to be brought
		  up to date before the question means anything. **The caller holds a
		  IDataBase::SaveRestoreModifiedState.**

		@param index the character. Outside the story as it stands NOW, or overset, answers kFalse -
			the check is made on this side, because the source-side caller is handed an index the
			diff worked out and has no length to measure it against.
		@param outPb [out] untouched when this answers kFalse.
		@return kFalse when there is no such position to point at - callers fall back to
			GetStoryStartPoint. */
	virtual bool16	GetStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb) = 0;

	/** Which frame holds one CHARACTER of a story - the frame a jump to a change has to bring into
		view, as against GetFirstFrameUID above, which answers where the story starts.

		WHY BOTH THIS AND GetStoryPointAt ARE NEEDED FOR ONE JUMP, and why they are asked together:
		  pasteboard coordinates are spread-relative, so a jump has to put the right SPREAD on
		  screen (this) before centring the POINT (that one). Taking the frame from anywhere else -
		  the story's first frame, or a frame resolved before the composition was brought up to
		  date - scrolls the window to a point belonging to a different spread, which is not a
		  small error but another page.

		Returns the page item, not the text column.
		@warning GetFirstFrameUID returns a column UID; these two are NOT interchangeable.

		@warning LIKE GetStoryPointAt, THIS CAN DIRTY THE DOCUMENT - it composes if the composition
		  is out of date, and it must, for the reason above. **The caller holds a
		  IDataBase::SaveRestoreModifiedState.**

		@param index the character. Outside the story as it stands now answers kInvalidUID.
		@return kInvalidUID when there is no such story or character, or it is overset or in no
			frame - callers keep the fallback frame they already had. */
	virtual UID		GetStoryFrameAt(IDataBase* db, UID storyUID, TextIndex index) = 0;
};

#endif // __IKCMStoryEditsFacade_h__

// End, IKCMStoryEditsFacade.h.
