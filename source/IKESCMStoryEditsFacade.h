//========================================================================================
//
//  IKESCMStoryEditsFacade.h
//
//  Reading the Story Edits list: which stories changed between the two versions, and where a
//  given story begins in a given document.
//
//  Created 2026-08-13 for the model/UI split (Stage 1), Task 14.
//
//  Comparing pixels can only say "this page looks different". Story Edits answers the next
//  question -- whether the words changed, only the formatting changed, or something attached to
//  the story such as a table changed -- by matching ITextModel's change counters story by story
//  (KESCMStoryStamp.h). This interface is how the panel reads what that produced.
//
//  ★READ ONLY EXCEPT FOR ONE METHOD, AND THE EXCEPTION IS WORTH READING. The list is built by one
//  comparison and thrown away by the next, and both of those happen in the model (KESCMCore.cpp,
//  and the shutdown path in KESCMPeek.cpp). No UI file builds it, clears it or empties it at
//  shutdown, so there is no Build() here: a method on a boundary that nobody calls is a promise
//  nobody keeps. (The plan's draft had a Rebuild() on the strength of a second caller in "Refresh
//  Page Comparison"; grepping for it before writing this file found that caller is model-side too.)
//
//  RefreshRow() is the one that writes, and it is here rather than on IKESCMCompareFacade beside
//  "Refresh Page Comparison" because of what it is asked in terms of: a ROW NUMBER. That is this
//  list's own vocabulary - the compare facade knows about documents and pages and has never heard
//  of a row - and an interface that has to borrow another one's index to be called is the wrong
//  interface (2026-08-21).
//
//  ★THE LAST TWO METHODS ARE NOT ABOUT THE LIST. They answer "where does this story start in
//  this document", which the navigation asks of the SOURCE document for a story the list only
//  ever read out of the target -- the two versions can hold the same story in different places
//  (user's observation, 2026-08-10). They take a database and a UID and nothing else, so they
//  are model questions: no window has to exist for them to have an answer.
//
//========================================================================================

#ifndef __IKESCMStoryEditsFacade_h__
#define __IKESCMStoryEditsFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMPoint.h"			// PBPMPoint -- a pasteboard point, what a jump centres on
#include "PMString.h"
#include "UIDRef.h"				// UID

// Project includes:
#include "KESCMBoundaryID.h"	// IID_IKESCMSTORYEDITSFACADE。★2026-08-17 に KESCMID.h から絞った
								// (理由は IKESCMCompareFacade.h の同じ位置)
#include "KESCMStoryStamp.h"	// KESCMStoryChangeKind を借りるため。⚠2026-08-17 訂正＝旧「a type only」は
								// 不正確で、このヘッダーは free function の宣言も 3 本連れてくる(実測)

class IDataBase;

class IKESCMStoryEditsFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKESCMSTORYEDITSFACADE };

	/** One row of the Story Edits list, in the five fields the panel actually reads.

		★A COPY, NOT A POINTER INTO THE LIST. The model hands out const KESCMStoryRow* today,
		which is safe only because a row is read and used inside one call while nothing can
		rebuild the list underneath it. Across the boundary that reasoning stops being local, so
		the row is copied out and the caller owns it.

		★fPageIndex is not here. It is the list's sort key, used while the rows are being built
		and never read afterwards -- putting it on the boundary would publish an internal
		ordering decision as if the UI were entitled to it.
	*/
	struct Row
	{
		/** The story, in the document that holds it -- the row's identity, shown as a number.

			★★WHICH DOCUMENT IS ANSWERED BY fKinds (2026-08-21). Every row is a target story except
			a REMOVED one, which exists only in the source. The three fields below are read out of
			that same document, so a caller that jumps to a row must pick its database the same way:
			GetArmedSourceDB() when kKESCMStoryKindRemoved is set, GetArmedTargetDB() otherwise.
			⚠A uid is meaningless in the other document -- it can name a different object there
			rather than nothing -- so this is not a detail a caller may skip. */
		UID			fStoryUID;
		PMString	fText;		// the first readable words. NOT shortened here: the row's text cell is
								// kEllipsizeMiddle and decides for itself at whatever width it has
		uint32		fKinds;		// OR of KESCMStoryChangeKind -- named on the right of the row
		UID			fFrameUID;	// the story's FIRST frame, what a click scrolls to. kInvalidUID for a
								// story in no frame at all, which cannot be jumped to
		UID			fPageUID;	// where the story starts; kInvalidUID when it starts on the pasteboard

		/** Whether the two versions' TEXT was actually diffed for this row (2026-08-21).

			★IT IS HOW AN EMPTY CHILD LIST IS READ. GetChangeCount answers 0 for three unrelated
			reasons - the pixel mode diffs nothing, the story could not be compared, or it was
			compared and the words agree - and only the last one is worth telling the reader
			about. kTrue with no children is that case, and the row draws "None" for it.
			⚠It does NOT mean the story is unchanged: the change counters moved, or there would
			be no row (KESCMStoryStamp.h). It means the WORDS come out the same. */
		bool16		fTextCompared;

		/** WHICH KIND OF ATTRIBUTE this row's children found a difference in, when they found one
			(2026-08-22). 0 = none; 1 = ruby.

			★A NUMBER RATHER THAN A FLAG, because ruby is the first of these and not the last:
			KENTEN (圏点) is meant to follow, and it is a different mechanism again - ruby is a
			STRAND (IRubyAttrStrand, run-based) while kenten is a set of CHARACTER ATTRIBUTES
			(kTAKenten*Boss on kCharAttrStrandBoss). What they have in common is exactly this: the
			text is untouched and something over it moved. ⇒ Adding kenten means one more value
			here and one more label, not another field and another branch everywhere.

			⚠NOT DERIVED FROM fKinds, which comes from the two documents' CHANGE COUNTERS and is
			  deliberately left alone by a row refresh ("read it again and it says the same"). This
			  one comes from the DIFF - it is only known after the two versions were compared - so
			  mixing it into fKinds would break that promise. */
		int32		fAttrKind;

		Row()
			: fStoryUID(kInvalidUID), fKinds(kKESCMStoryKindNone), fFrameUID(kInvalidUID),
			  fPageUID(kInvalidUID), fTextCompared(kFalse), fAttrKind(0) {}
	};

	// ---- the list ------------------------------------------------------------------------

	/** How many rows the list holds. 0 both when nothing has been compared and when a comparison
		found no edited story -- the two are told apart by asking IKESCMCompareFacade::IsArmed(),
		which is what the section heading and the placeholder row already do. */
	virtual int32	GetRowCount() = 0;

	/** Fill out with row nth (0-based). kFalse when nth is out of range, leaving out untouched.

		★Asked one row at a time on purpose. Both callers want exactly one: the tree writes the
		widget for a single node, and a click reports the row it landed on. (Contrast the page
		pairing on IKESCMMarkData, which is handed over whole because its callers each build a
		map of the lot.) */
	virtual bool16	GetRow(int32 nth, Row& out) = 0;

	// ---- the children: what differs inside a story (Story Changes mode, 2026-08-20) --------

	/** One difference inside a story, in the fields the tree and the click actually read.

		★A COPY, for the same reason Row is one, and ★FLATTENED: the model's enums come across as
		int32. An enum on the boundary would have to be defined somewhere both plug-ins agree on,
		and that is a decision to take when there is a second reason to take it - the row's fKinds
		is already an int with named bits and nothing has suffered for it. */
	struct Change
	{
		/** What fWhat's numbers mean, named (2026-08-22).

			★THE FIELD STAYS int32 - this is not the enum the note above declined to put on the
			boundary. Nothing about the struct's layout changes; what changes is that a caller
			COMPARING fWhat can say what it is comparing against. The second reason that note was
			waiting for arrived with GetChangeWhat, whose whole return value is this one number:
			the tree asks it of every row to decide the row's height, and `== 1` at that call site
			would be a bare number three files away from the only place that explains it. */
		enum { kWhatText = 0, kWhatAttr = 1 };

		int32		fKind;			// 0 = replace, 1 = insert, 2 = delete
		// 0 = text, 1 = attribute.
		// ★★1 ARRIVED ON 2026-08-22 AND MEANS RUBY SO FAR (this header said "ALWAYS 0 today" until
		//   then). ⚠It changes how the three fText / fOtherText pieces are to be read: for a TEXT
		//   change the row shows whichever side changed, so a deletion puts the NEWER text in
		//   fOtherText - but a ruby change always puts the target in fText and the source in
		//   fOtherText, because the characters exist in both versions and there is no side to
		//   choose. Anything deciding "which document is this text from" must look here first.
		int32		fWhat;
		TextIndex	fTargetStart;	// in the NEWER document
		TextIndex	fTargetEnd;		// ★an END, not a length (RangeData.h:69)
		TextIndex	fSourceStart;	// in the OLDER document; meaningless unless fHasSource
		TextIndex	fSourceEnd;
		bool16		fHasSource;		// kFalse for an insertion - nothing in the older version to point at

		// The words to show, in three pieces: context, the changed characters, context.
		// ★For a DELETION they come from the older side's text (see KESCMStoryList.h).
		// ★THREE SINCE 2026-08-20, so the row can draw the change at full strength and fade the
		//   rest around it. Concatenated they are the one string fText used to be; the boundary
		//   between them cannot be recovered on this side once it is gone (it is a code point
		//   index, and PMString counts UTF-16), which is why it is carried across rather than
		//   worked out here.
		PMString	fTextPre;
		PMString	fText;
		PMString	fTextPost;

		// The OTHER side of the same edit, in the same three pieces - what the panel's message area
		// shows while this row is selected (2026-08-20).
		// ★NOT "the old side": the row already shows whichever side CHANGED, so this is the old
		//   text for a replacement or an insertion and the NEW text for a deletion (where the row is
		//   showing what was removed, and what the reader wants beside it is what stands there now).
		//   The full reasoning is on KESCMStoryChange in KESCMStoryList.h.
		// ★The middle is empty where nothing stood on that side; the context pieces are not, which
		//   is what makes an empty middle read as a place rather than as an absence.
		PMString	fOtherTextPre;
		PMString	fOtherText;
		PMString	fOtherTextPost;

		// ---- the readings, and ONLY meaningful when fWhat is 1 (attribute) ----
		//
		// ★A RUBY ROW SHOWS TWO THINGS AT ONCE - the base text and the reading over it, set the way
		//   ruby actually is - so they cannot be one string. The three fText pieces carry the base
		//   text with its context, exactly as they do for a text change; these carry the readings.
		// ★fRuby belongs to the side the row shows and fOtherRuby to the other, the same pairing as
		//   fText / fOtherText. Either can be empty: ruby added has no old reading, ruby removed has
		//   no new one.
		// ⚠Mono and group ruby are both in here and the difference is NOT in the string - it is in
		//   the span (fTargetStart/fTargetEnd): one reading over several characters, against one
		//   reading each. See KESCMSnippetText.h for how the two are told apart in the snippet.
		PMString	fRuby;
		PMString	fOtherRuby;

		// WHICH attribute this is (2026-08-22): 0 = none, 1 = ruby, 2 = kenten (圏点).
		// ★★fWhat SAYS "not the words", THIS SAYS WHAT INSTEAD - and the panel needs both, because
		//   it draws them differently. A ruby is drawn on TWO LINES with the reading above the
		//   characters; a kenten is not, because its value is a NAME ("KentenBlackCircle") rather
		//   than something a reader reads, and it is named in the Change column instead (user's
		//   call, 2026-08-22).
		// ⚠So "is this row drawn on two lines" is THIS field, never fWhat. Asking fWhat was right
		//   while ruby was the only attribute, and would have given every kenten row a permanently
		//   empty upper line the moment the second one arrived.
		int32		fAttrKind;

		Change()
			: fKind(0), fWhat(0), fTargetStart(0), fTargetEnd(0),
			  fSourceStart(0), fSourceEnd(0), fHasSource(kFalse), fAttrKind(0) {}
	};

	/** How many differences row nth holds.

		★0 IS THE NORMAL ANSWER IN THE PIXEL MODE - nothing runs the text diff there, so no row has
		children and the tree stays one level deep. It is also the answer for a story that could not
		be compared (added, too different, or a failed length check); the row is still there. */
	virtual int32	GetChangeCount(int32 nth) = 0;

	/** Fill out with difference which of row nth. kFalse when either index is out of range,
		leaving out untouched. One at a time, for the same reason GetRow is. */
	virtual bool16	GetChange(int32 nth, int32 which, Change& out) = 0;

	/** WHICH ATTRIBUTE this difference is in - Change::fAttrKind, and nothing else (0 = none,
		1 = ruby, 2 = kenten). 0 for a text change and for an index that names no change.

		★WHY THE ONE FIELD HAS A CALL OF ITS OWN (2026-08-22). The tree asks this of every row it
		lays out, to decide how TALL the row is - a ruby change is drawn on two lines, the reading
		above the characters it belongs to. GetChange would answer the same question, but it copies
		nine PMStrings to do it, and the height is asked for again on every scroll and every
		rebuild. This copies one int.
		⚠★IT ANSWERED fWhat UNTIL KENTEN ARRIVED, later the same day. "Is this an attribute" was a
		  correct stand-in for "is this drawn on two lines" only while ruby was the sole attribute;
		  kenten is an attribute that is NOT drawn on two lines (its value is a name, not a
		  reading), so the question had to become the one it was really asking. */
	virtual int32	GetChangeAttrKind(int32 nth, int32 which) = 0;

	/** Compare row nth's story again against the older document, and replace its differences with
		what stands there now - "Refresh Story Comparison" on the row's right-click menu.

		★THE ROW IS BROUGHT UP TO DATE AS WELL AS ITS CHILDREN - the words it shows, the frame a
		click scrolls to and that frame's page are all re-read from the target document. (The
		first build refreshed only the children, and a row went on quoting the sentence the reader
		had just rewritten; user's report, 2026-08-21.) ⚠Two things are deliberately left alone:
		the kinds named on the row's right, which come from the change counters rather than from
		the text, and the row's PLACE in the list - one row is being refreshed, not the order.

		★THE ROW STAYS, whatever comes of it. A story brought back into agreement comes out with
		NO children and keeps its row, marked "None" - the panel is answering "what differs now",
		not "does this row still belong here" (user's call, 2026-08-21). See KESCMStoryDiffRun.h
		for why the change counters cannot answer the second question anyway.

		★The model tells the panel to redraw the list itself, through the same notification a
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
		(KESCMStoryStamp.h:46-51). For two documents that are not versions of each other nothing
		lines up, and the rows simply come out as "Added". */
	virtual UID		GetFirstFrameUID(IDataBase* db, UID storyUID) = 0;

	/** Where the story BEGINS on the page, as a pasteboard point -- what a jump should centre.

		Not the same as the first frame's centre: in a tall frame the centre is the middle of the
		text and what a reader wants is the beginning of it (user's call, 2026-08-10).

		@param outFrame [out] the frame that beginning sits in. Untouched when this answers kFalse.
		@param outPb [out] the point, in pasteboard coordinates. Untouched when this answers kFalse.
		@return kFalse when there is no such story, or none of its parcels are placed -- callers
			fall back to centring GetFirstFrameUID's frame. */
	virtual bool16	GetStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb) = 0;

	/** Where one CHARACTER of a story sits, in pasteboard coordinates - what a jump to a change
		centres on, rather than the story's beginning above (user's request, 2026-08-22).

		★The point is where the CARET would stand in front of that character, and vertically the
		middle of its line. Ported from KBSJump.cpp, which carries the same recipe from KESCL - see
		the implementation in KESCMStoryList.cpp for what the two earlier copies paid for.

		⚠★★UNLIKE EVERY OTHER READ ON THIS BOUNDARY, THIS ONE CAN DIRTY THE DOCUMENT: where a
		  character sits is a result of composition, so an out-of-date composition has to be brought
		  up to date before the question means anything. **The caller holds a
		  IDataBase::SaveRestoreModifiedState.**

		@param index the character. Outside the story as it stands NOW, or overset, answers kFalse -
			the check is made on this side, because the source-side caller is handed an index the
			diff worked out and has no length to measure it against (2026-08-22 bug recheck).
		@param outPb [out] untouched when this answers kFalse.
		@return kFalse when there is no such position to point at - callers fall back to
			GetStoryStartPoint. */
	virtual bool16	GetStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb) = 0;

	/** Which frame holds one CHARACTER of a story - the frame a jump to a change has to bring into
		view, as against GetFirstFrameUID above, which answers where the story starts
		(2026-08-22 bug recheck).

		★★WHY BOTH THIS AND GetStoryPointAt ARE NEEDED FOR ONE JUMP, and why they are asked
		  together: pasteboard coordinates are spread-relative, so a jump has to put the right
		  SPREAD on screen (this) before centring the POINT (that one). Taking the frame from
		  anywhere else - the story's first frame, or a frame resolved before the composition was
		  brought up to date - scrolls the window to a point belonging to a different spread, which
		  is not a small error but another page.

		★Returns the page item, not the text column. ⚠GetFirstFrameUID returns a column UID; these
		  two are NOT interchangeable.

		⚠★★LIKE GetStoryPointAt, THIS CAN DIRTY THE DOCUMENT - it composes if the composition is
		  out of date, and it must, for the reason above. **The caller holds a
		  IDataBase::SaveRestoreModifiedState.**

		@param index the character. Outside the story as it stands now answers kInvalidUID.
		@return kInvalidUID when there is no such story or character, or it is overset or in no
			frame - callers keep the fallback frame they already had. */
	virtual UID		GetStoryFrameAt(IDataBase* db, UID storyUID, TextIndex index) = 0;
};

#endif // __IKESCMStoryEditsFacade_h__

// End, IKESCMStoryEditsFacade.h.
