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
#include "WideString.h"	// KCMEqualRecord::fLiveWords - the words KCM's write took out (2026-09-25)
// (⛔WideString.h was included for KCMStoryChange::fBeforeRaw - the characters themselves rather
//  than a quote of them - which went with the restore on 2026-09-21.)

#include <vector>
#include <boost/shared_ptr.hpp>	// KCMRecordCopy::fParaOverrides - the shape ITextModelCmds::ApplyCmd takes (2026-10-24)

#include "KCMStoryStamp.h"	// KCMStoryDiff / KCMStoryChangeKind
#include "KCMStoryLayers.h"	// KCMStoryChange::fLayers - a warichu / tate-chu-yoko change, line by line
#include "KCMCopyStore.h"	// KCMRecordCopy::fCopy - the words Restore from Copy pastes back (2026-10-24)

class IDataBase;
class AttributeBossList;

/** A span of Target text, [fFrom, fTo) - one cell a table change marks (KCMStoryChange::fMarkSpans). */
struct KCMTextSpan
{
	TextIndex	fFrom;
	TextIndex	fTo;
	KCMTextSpan() : fFrom(0), fTo(0) {}
	KCMTextSpan(TextIndex from, TextIndex to) : fFrom(from), fTo(to) {}
};

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
				kRefused,	// ★one thing an IMPORT could not put in (2026-09-19). fTextPre holds the
							//   KIND word for the ID column ("Word" / "Table" / "Place" / "Para" /
							//   "Attr" / "File") and fText the place and the reason. Nothing else on
							//   the change means anything, and it never carries a position.
							//   (⛔It also carried kKCMWriteBlockedKind, so that no menu offered to
							//    write it back, until the restore went on 2026-09-21.)
				kTable };	// ★★A TABLE WHOSE SHAPE DIFFERS FROM TASK START'S (2026-09-19 night, the
							//   user: "fold every change of that table into one row"). fKind says how:
							//   kInsert = the table is only here, kDelete = only in Task Start, kReplace =
							//   rows, columns or merged cells differ. The ranges are the table's ANCHOR
							//   characters on each side (a caret on the side that lacks it); the cells
							//   that changed are in fMarkSpans; fText opens with the shape word
							//   ("2×2→3×2"). The fields it uses are at the end of the struct.

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

	// (⛔**WHAT A REPLACED CHANGE REMEMBERED WENT ON 2026-09-21**, with the restore that made one.
	//  Eleven fields: the Target story's counter at the moment of the write (which is how "is it
	//  still taken in" was answered without a flag - the counter winds back on an undo, so the two
	//  could never disagree); the range and the three pieces as they stood AFTER the write; the same
	//  four as they stood BEFORE it, held apart from the change's own because the story is diffed
	//  again straight afterwards; and fBeforeRaw, the Target's own characters, read before anything
	//  was written. ⚠That last one was NEVER the row's quote - measured 2026-09-16: written back,
	//  eighty characters came back as sixty and a paragraph break came back as the character U+00B6.)

	// (⛔fWriteBlock went on 2026-09-21: why this change must not be written back. Nothing writes.)

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

	// (⛔fAfterNewParagraph went the same day: it marked the second "+" of "+ +", so that the panel
	//  could ask before one was taken in ahead of the other and given a different style.)

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

	// (⛔**THE TWO PARAGRAPH-STYLE RECORDS WENT ON 2026-09-21**: what the paragraphs wore before a
	//  whole-paragraph take-in wrote, and what the take-in left them wearing. They were how "a b c
	//  styled A B C: take b out and put it back, and everything reads C" was answered - the undo put
	//  the styles back in two layers, and only while a following paragraph still wore what the
	//  take-in had left it. ★The reasoning is kept in docs/ai-notes/kcm-restore-retired-2026-09-21.md.)

	// ---- a TABLE change (fWhat == kTable), 2026-09-19 night ------------------------------------------
	//
	// ⚠**`fWholeCell` ("Cell" in the ID column) stood here for one evening** and went the same night: the
	//   user chose to fold a table's cell changes into one Table row instead. The facade's field of that
	//   name stays for its layout and answers kFalse.

	// (⛔**FOUR IDS WENT ON 2026-09-21**: the Target's table and Task Start's (a table is named by its
	//  own uid, never by its position - an ordinal made a table inserted in the MIDDLE shift every
	//  table after it into a wrong pairing), the table that stood nearest before it in Task Start,
	//  and the characters of body between the two. The last pair was how a Table − knew where to go
	//  back: Task Start's anchor alone landed INSIDE a word once another table had changed.
	//  ★**The PAIRING by id is still done** - KCMStoryDiffRun does it while it builds the row - it is
	//  only no longer carried on the row, because only a write into the document needed it there.)

	/** ★**THE CELLS THAT CHANGED**, in Target coordinates - what the marks light and what the jump aims at
		(the user: "the changed cells should be marked; jump to the top-left of them"). One span per cell:
		the words that changed in a paired cell, a whole cell that is only here (a new row or column), a cell
		whose merge differs. Empty for Table −, whose one span is the caret where the table stood. */
	std::vector<KCMTextSpan>	fMarkSpans;

	/** What the Story column opens with: "2×2→3×2" / "2×2 merged" / "2×2" (UTF-8; KCMTableShapeWord). */
	std::string	fShapeWord;

	// (⛔**AND FIVE MORE THINGS A TABLE ROW CARRIED FOR THE RESTORE**, all gone 2026-09-21: the two
	//  shape signatures; the note that no snippet is kept per table; fRedoSnippet, the live table's
	//  own XML kept so that "Undo the Restore" could put it back; and the id and shape the restore
	//  left standing, which is how "is this table still as I left it" was answered.)

	/** ★**THE TWO TABLES THIS ROW IS ABOUT, BY THEIR OWN IDS** (2026-09-25, "Match the Source" - design 16-2): the
		Target's table and the Source's, each the dictionary uid in its own database, as KCMStoryDiffRun paired them.
		kInvalidUID on a side that has no table (Table + / Table −) and for a Source read from the cache (no document
		to name one in). ⚠Appended at the END, for the reason stated above fReplacedCount's neighbours. */
	UID			fTargetTableUID;
	UID			fSourceTableUID;

	KCMStoryChange()
		: fKind(kReplace), fWhat(kText), fTargetStart(0), fTargetEnd(0), fRubyGroup(kFalse), fOtherRubyGroup(kFalse),
		  fSourceStart(0), fSourceEnd(0),
		  fAttrKind(kKCMStoryAttrNone), fOverset(kFalse),
		  fWholeParagraph(kFalse), fPlace(0), fBreakAt(0), fTargetTableUID(kInvalidUID), fSourceTableUID(kInvalidUID) {}
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

/** Why a "=" record holds no copy for Restore from Copy (2026-10-24, the import-as-Source road - design section 11-3).
	Numbers, so the facade's plain int32 and this agree by definition. */
enum KCMCopyWhyNot
{
	kKCMCopyKept = 0,				// a copy is held
	kKCMCopyNotTaken = 1,			// none was asked for: a record of a road that keeps none (a mark restored, a table matched or applied)
	kKCMCopyHoldsObject = 2,		// the words hold an object's character - put back, it would be made anew (a new UID)
	kKCMCopyTouchesLink = 3,		// the range touches a hyperlink's or cross-reference's source - a paste makes a second one
	kKCMCopyParagraphRoad = 4,		// it went in with its paragraph (an object or a return in the change) - Ctrl+Z only
	kKCMCopyGroup = 5,				// it went in with other paragraphs at one place - Ctrl+Z only
	kKCMCopyFailed = 6,				// the copy could not be made (a test build's copy-fail switch too)
	kKCMCopyMark = 7,				// a mark's record (ruby, kenten, warichu, tate-chu-yoko) - this version keeps none
	kKCMCopyWholeStory = 8			// (2026-10-26) it went in with its whole story (Apply All Changes, ... in This Story) - and a
									//   record standing when such a pour wrote its story over lets its copy go: Ctrl+Z only
};

/** What Restore from Copy needs of one "=" record of Apply This Change (2026-10-24, design section 11-3). */
struct KCMRecordCopy
{
	KCMCopyStore::CopyRef	fCopy;		// the document's words at the live range, copied just before Apply wrote over them
	int32					fWhyNot;	// KCMCopyWhyNot - kKCMCopyKept when fCopy is held
	/** ★THE PARAGRAPH BEFORE A WHOLE PARAGRAPH Apply put in with the break first ("\rNEW") or took out from the return
		before it - the write changes which return ends that paragraph, so a copy alone cannot bring its formatting back
		(KFC measured the same, its R13b). Its style and own overrides as they read before the write, put back after the
		restore over the paragraph holding the record's start (KFC's C16 road). kInvalidUID = nothing to put back. */
	UID										fParaStyle;
	boost::shared_ptr<AttributeBossList>	fParaOverrides;		// nil = none
	/** ★A WHOLE PARAGRAPH WRITTEN WITH ITS OWN RETURN (2026-10-24 night, Restore from Source by copy - design 1-8): the
		change's range is "\rNEW" (the break before it - KCMStoryChange::fBreakAt), but the write went one character on,
		over "NEW\r", so that the paragraph came with its own return and the one before kept its own. 1 = the copy is of
		that shifted range and is put back one character on as well; 0 = the record's own range (every other copy). */
	int32									fShift;
	KCMRecordCopy() : fWhyNot(kKCMCopyNotTaken), fParaStyle(kInvalidUID), fShift(0) {}
};

/** A "=" RECORD (2026-09-24, stage 2 C - design section 15 of docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md;
	named for what it is since 2026-10-25): a place KCM WROTE so that the Target reads as the Source's there - Restore
	from Source (the Source's words or mark), Match the Source (a table) or Apply This Change (the import's proposal,
	the Source being the import's copy) - and the row stays on the list with "=" in its sign column, so that the reader
	sees what was written and, where a copy was kept, Restore from Copy has a place to act from. (It began as the record
	of "Reject This Import Change", retired on 2026-10-25 with the import that wrote into the document.)

	★THE COUNTERS DECIDE UNDONE AND PUT BACK (design 15-1-3, and the retired restore's lesson - docs/ai-notes/
	  kcm-restore-retired-2026-09-21.md section 3-2): the story's counter (KCMStoryDiffRun::CountForKind, by
	  fCounterKind) is read at the moment of asking, and an undo winds it back on its own, so no flag can
	  disagree with the document. Standing = "=" (KCM's write stands - the Source's state); Undone = that write
	  was undone (shown as the live change it was); PutBack = Restore from Copy put the words before it back
	  (likewise - "Redone" until 2026-10-25, when "Redo from Word" did it). RunOne drops an Undone or PutBack
	  record that has no live twin (PruneEqual), and Build starts with none.
	  ⚠★★BUT NOT ALONE (2026-09-25, measured with Reject and Redo from Word): an undo followed by ANOTHER write in
	   the story moves the counter past the undone step as well, so the counters took an undone put-back for a
	   put-back (and an undone write for a write) - the document now checks them (fLiveWords below;
	   KCMStoryList.cpp StateNow).
	★★★**AND "=" IS A COMPARISON, NOT A FLAG** (2026-09-24 night, the user: "it was taken back, so it MUST be
	  the same - if a rejected tracked change did not come all the way back that is InDesign's fault, and it
	  has to show"). A record the counters call Standing is shown "=" only when the Target's characters at
	  [fNowStart, fNowEnd) ARE the Source's at the change's Source range - and, for an attribute record, the
	  marks of its kind over them too (KCMRecordReadsAsSource). Otherwise it is STALE: not shown, and KEPT (an
	  undo of what made it stale shows the "=" again - KCMEqualState), the live difference left to the comparison.
	  The check is cached against the counter it was made at (fCheckedAt): a story that has not changed is not
	  read again. */
struct KCMEqualRecord
{
	KCMStoryChange	fLive;			// the change as the diff made it - its ranges are its LIVE ones, slid along by later writes
	TextIndex		fNowStart;		// where the Source's words stand in the Target while the record is Standing
	TextIndex		fNowEnd;
	uint32			fWrittenAt;		// CountForKind right after KCM's write (the restore, the match, the Apply)
	uint32			fPutBackAt;		// the same right after Restore from Copy put the words back; 0 = never put back
	int32			fCounterKind;	// kKCMStoryAttrNone for words, else the attribute's kind - which counter to ask
	mutable KCMStoryChange fShown;	// what GetMergedChange hands out: fLive with the ranges of the moment
	mutable bool16	fChecked;		// the "=" comparison below has been made at least once
	mutable uint32	fCheckedAt;		// ... at this counter value
	mutable bool16	fCheckedSame;	// ... and found the place to read as the Source's
	/** ★WHICH WORDS THE RECORDS AFTER THIS ONE ARE PLACED FOR: kTrue = the Source's (the record Standing), kFalse =
		the live ones (Undone or PutBack). The document changes length at this place with every write of KCM's (the
		restore, the match, the Apply), every Restore from Copy, every undo and every redo of one, and only the first two
		are KCM's own; the rest arrive with no signal. So the later records are not slid at write time but RECONCILED
		whenever the list is read (KCMStoryList's MergedOrder): where this flag disagrees with the state, they slide by
		the two lengths and the flag follows. Measured 2026-09-24 (with Redo from Word): slid at the put-back alone, an
		undo of it left the next record two characters off. */
	bool16			fPlacedForSource;
	/** ★★THE WORDS KCM'S WRITE TOOK OUT - the change's live words as they stood before it (2026-09-25; fHasLiveWords
		kFalse when not captured: the records of a mark restored, a table matched or applied). The counters cannot tell
		"put back" from "put back, undone, and then ANOTHER write in the story" - both move the counter past fPutBackAt -
		so a record was taken for put back and pruned while its "=" stood in the document (measured 2026-09-25 with
		Reject and Redo from Word: two paragraphs taken back, the first redone, Ctrl+Z, the second redone - the first's
		"=" vanished). With these words the document settles it: they stand at the live range = the record is live; they
		do not and the Source's do = KCM's write stands. ⚠A deletion's live range is a caret (no words), and there the
		Source's words alone decide, as before. */
	WideString		fLiveWords;
	bool16			fHasLiveWords;
	mutable bool16	fCheckedLive;		// ... and whether fLiveWords stood at the live range, at fCheckedAt
	mutable bool16	fCheckedLiveSourceToo;	// ... and the Source reads them at its range too - then they tell nothing
	/** ★THE COPY RESTORE FROM COPY PASTES BACK (2026-10-24 - the words' records of Apply This Change and of Restore from
		Source: KCMRecordCopy). Restore from Copy leaves the record in place, PutBack: an undo of the restore makes the
		record Standing again, and the copy is still in the store (KCMCopyStore never drops one copy at a time). */
	KCMRecordCopy	fCopy;
	/** ★★ITS TEXT FOCUS (2026-10-26 - KCMRecordFoci.h; docs/superpowers/specs/2026-10-26-kcm-equal-follow-focus-design.md):
		the number of a focus of KCM's own class standing on the record's words - the Source's while it is Standing, the
		live ones while it is Undone or PutBack - which InDesign moves with every edit before it and the user's Undo and
		Redo put back. 0 = none (made before foci, the focus-off switch, let go). ★WHILE IT IS FOLLOWED THE FOCUS SAYS
		WHERE THE RECORD IS: its start is the head both ranges begin at (fNowStart and fLive.fTargetStart - every road that
		places a record puts the two at one head), and the reconcile does not slide it (KCMStoryList's SyncFoci). Whether the
		place is "=" is still the reading (StateNow) - the focus says where, never what. */
	int32			fFocusId;
	/** The last SyncFoci found the focus followed (not lost, not gone) - and the head was taken from it. */
	bool16			fFollowed;
	/** ★WHERE ITS LIVE TWIN STOOD AT THE LAST COMPARISON (2026-10-26; -1 = not known - FollowRecords sets it): a followed
		record moves with a hand edit at once, the comparison's live changes only when the row is compared again - and a
		record shown live hides its twin by this place (MergedOrder), so that the two do not stand as one change twice. */
	TextIndex		fComparedLiveStart;
	TextIndex		fComparedLiveEnd;
	KCMEqualRecord() : fNowStart(0), fNowEnd(0), fWrittenAt(0), fPutBackAt(0), fCounterKind(0),
						  fChecked(kFalse), fCheckedAt(0), fCheckedSame(kFalse), fPlacedForSource(kTrue),
						  fHasLiveWords(kFalse), fCheckedLive(kFalse), fCheckedLiveSourceToo(kFalse),
						  fFocusId(0), fFollowed(kFalse), fComparedLiveStart(-1), fComparedLiveEnd(-1) {}
};

/** Standing = shown "=", Undone / PutBack = shown as the live change it was, Stale = the counters say Standing
	but the place does not read as the Source's (KCMEqualRecord says why that is not shown "="). A Stale
	record is not shown at all and is KEPT: the reading is made again when the counter moves, so an undo of
	the write that made it stale shows the "=" again (MergedOrder). */
enum KCMEqualState { kKCMEqualStanding = 0, kKCMEqualUndone = 1, kKCMEqualPutBack = 2, kKCMEqualStale = 3 };

/** The record's state for a counter read now - the COUNTERS' half of the answer; whether a Standing record
	is Stale is asked of the documents (KCMStoryList::EqualStateOf). ★">=" and never "==" (section 3-2): a
	later write in the same story moves the counter on, and the record is still standing. */
inline KCMEqualState KCMEqualStateOf(const KCMEqualRecord& r, uint32 counterNow)
{
	if (r.fPutBackAt != 0 && counterNow >= r.fPutBackAt)
		return kKCMEqualPutBack;
	return (counterNow >= r.fWrittenAt) ? kKCMEqualStanding : kKCMEqualUndone;
}

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

	/** ★THE SOURCE STORY THE ROW COMPARES AGAINST (2026-10-26) - KCMStoryDiff::fSourceStoryUID, copied by Build: the
		same uid for a UID pair and a Removed row, the Source's own story for a row paired by place
		(kKCMStoryKindByPosition), kInvalidUID for an Added row and a row standing for a file. ★**Every reader of the
		Source side asks this** - the diff (KCMStoryDiffRun), the "=" records' readings, the restores and the import's
		Apply (KCMFacades.cpp), the Source window's marks (KCMStoryMarkBuild) and the jump (through the facade's Row).
		@warning it is NOT read out of the row's own document like the fields below: it names a story of the SOURCE. */
	UID			fSourceStoryUID;

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

		⛔It was **EMPTY IN THE PIXEL MODE** until 2026-10-11, which is what kept the tree flat there
		(the hierarchy adapter asked how many children a row had and got 0). The Pixel mode keeps no
		rows at all now, and its list is one note the adapter answers by mode.

		**EMPTY when the story could not be compared** -- it had no partner in the older
		document (an added story), or the edit distance ran past the limit, or the length check
		failed. The row still appears; only the detail is missing. A story that changed must
		never disappear because the detail could not be worked out. */
	std::vector<KCMStoryChange> fChanges;

	/** The Target story's text change counter (ITextModel::GetTextChangeCount) at the moment
		fChanges were built. 0 until the diff runs.
		(⛔The restore read it to refuse writing into a story whose counter had moved since - the
		 change's positions name the text as it was then. It went on 2026-09-21.) */
	uint32		fTargetTextCount;

	/** ★The story's AGGREGATE change counter (ITextModel::GetChangeCount) when fChanges were built, and every value it
		has had at a comparison of this row (2026-09-25). The counter goes back on an Undo and forward again on a Redo,
		to exactly the values it had; plain typing makes new ones. So "the story is back at a value it was compared at,
		or below the last one" is an Undo or a Redo, and that is when the row is compared again on its own
		(KCMStoryList::NeedsCompareAgain, KCMStoryFollowObserver). ⚠The aggregate and not fTargetTextCount's text
		counter: a ruby or a kenten does not move the text counter (KCMStoryDiffRun::CountForKind says so, measured). */
	uint32				fAllCount;
	std::vector<uint32>	fComparedAt;

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

	// (⛔**fReplacedChanges WENT ON 2026-09-21** with the restore: the changes the reader had already
	//  taken in, kept in READING order and merged with the live ones in text order for the panel.
	//  ★**They were not in fChanges, and that is what made the re-diff safe** - every write was
	//  followed by comparing the story again, which empties fChanges and refills it from what the
	//  comparison finds. ⚠The ordering rule was paid for: until 2026-09-19 two records at one
	//  position stood in the order they were taken in, which is not knowable from the positions, and
	//  two new paragraphs taken out and put back came back as "¶ba".)

	/** ★What an IMPORT could not put into this story, one entry each (2026-09-19, the user's ask).
		Shown BEFORE fChanges, each with a red "!" in the Δ column - and the row
		itself carries kKCMStoryKindRefused, which is what puts it at the top of the list (RowIsBefore).

		**NOT IN fChanges, for the reason fReplacedChanges is not:** RunOne empties that on every
		refresh. These are facts about the FILE the reader handed over, not about the text, so a
		refresh cannot find them again. Build refills them from the import's own list
		(KCMImportRefusals), which lives as long as the origin does. */
	std::vector<KCMStoryChange> fRefusals;

	/** ★The "=" records - the places KCM wrote as the Source's (2026-09-24, stage 2 C) - see KCMEqualRecord. **NOT IN fChanges, for
		the reason fRefusals is not**: RunOne empties that on every refresh, and a record has to outlive the refresh
		that finds nothing where it stands. Kept in TEXT order (KCMEqualOrder.h's slot rule) and merged with the
		live changes by GetMergedChange. SetRowChanges leaves it alone; PruneEqual is what thins it. */
	std::vector<KCMEqualRecord> fEqual;

	/** ★THE RECORDS' FOCI WERE LAST READ AT THIS STATE OF THE STORY (2026-10-26 - KCMStoryList's SyncFoci): its aggregate
		change counter (KCMStoryDiffRun::CountForKind). A focus moves only with the story's text - an edit, an Undo, a Redo,
		each of which moves the counter - and with KCM's own moves and makes, which drop this mark (fFociSynced). So while
		the counter stands where it was, the heads read last are the heads now, and the panel's many reads of one row do not
		ask every focus each time. */
	uint32		fFociSyncedAt;
	bool16		fFociSynced;

	KCMStoryRow()
		: fStoryUID(kInvalidUID), fSourceStoryUID(kInvalidUID), fKinds(kKCMStoryKindNone), fFrameUID(kInvalidUID),
		  fPageUID(kInvalidUID), fPageIndex(kMaxInt32), fTextCompared(kFalse),
		  fAttrKind(kKCMStoryAttrNone), fAttrKindCount(0), fHasTextChange(kFalse), fTargetTextCount(0), fAllCount(0),
		  fFociSyncedAt(0), fFociSynced(kFalse) {}
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
	@param caret kTrue when `index` names a GAP rather than a character - a zero-width change: a
		deletion seen from the Target, an insertion seen from the Source (2026-09-24). The one place the
		two differ is a gap in front of a TABLE: the character there is the table's own, and its wax
		is the table frame's line, whose origin is the table's top-left corner - right for a row that
		points AT the table, wrong for the words that went in before it. A caret there is answered as
		the FAR EDGE of the last character before the table (KCMCaretOnTableChars).
	@return kFalse when the story is not there, or that position is OVERSET or in no frame --
		callers fall back to KCMStoryStartPoint.
*/
bool16 KCMStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb, bool16 caret = kFalse);

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
	@param caret kTrue for a GAP rather than a character - KCMStoryPointAt says what it changes. Here it
		makes a caret in front of a table the frame of the character BEFORE the table, the one the
		point is answered for: the two readings of one jump have to be of one place, and a table that
		starts a new column puts its own characters in the next parcel.
	@return kInvalidUID when there is no such story, no such character, or the character is OVERSET
		or in no frame -- callers keep whatever fallback frame they already had.
*/
UID KCMStoryFrameAt(IDataBase* db, UID storyUID, TextIndex index, bool16 caret = kFalse);

/** A caret standing on a table's own characters - kTextChar_Table for the anchor plus one
	kTextChar_TableContinued per row after the first (KCMParaText.h) - and where it really stands.

	★★MEASURED 2026-09-24 (the user's 「あ[表]い」→「あえ[表]い」, matrix case A12): the Source side of
	that insertion is the anchor's index, and the composition answers for it with THE TABLE FRAME'S OWN
	WAX LINE - a run with no glyphs whose origin is the table's top-left corner. So the jump centred the
	Source window on the table (page x 41.5pt, where the line before it ends at 396.9pt), and the caret
	mark [anchor, anchor+1) met a run that maps it to no glyph and drew nothing; the marker's own step
	back (a caret whose character draws nothing) could not help, because the character before is in
	another run. The place the reader means is AFTER the last character before the table: where the
	words went in. Three readers apply the same rule: KCMStoryPointAt / KCMStoryFrameAt (the jump) and
	the two caret builders (KCMStoryMarkBuild, KCMStoryMarker::AddFlashRange).

	@param at the caret's index.
	@param outAfter [out] one past the last character before the table's characters - so that
		KCMMarkRange::CaretAfter(outAfter) and "the far edge of character outAfter-1" name the place.
		Untouched when this answers kFalse.
	@return kFalse when `at` is not on a table's own character, or nothing stands before them (a table
		at the very start of the story). */
bool16 KCMCaretOnTableChars(IDataBase* db, UID storyUID, TextIndex at, TextIndex& outAfter);

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

		★★THE "=" RECORDS OF THE SAME PAIR ARE CARRIED OVER (2026-10-26, the author: "the '=' rows must not disappear after
		a Refresh"): built again for the two databases it was last built for - Refresh Comparison - the records of every
		row, their foci and copies with them, are put on the new row of the same Target and Source story, and a story that
		has records and no row gets one (the text diff then finds what still differs in it; DropRowsWithNoContentChange
		keeps a row holding records). ⚠Built for another pair - a new Start, a new import's copy - nothing is carried: the
		records are about the documents they were made in, and Stop has emptied the list before any of those anyway.

		@param targetDB the newer document. nil clears the list.
		@param sourceDB the older document -- where a Removed row's story, text and page are read
		       from. nil is tolerated: those rows are then dropped, exactly as an unreadable story is.
		@param diffs what KCMStoryEdits::Compare produced for this comparison.
	*/
	void Build(IDataBase* targetDB, IDataBase* sourceDB, const std::vector<KCMStoryDiff>& diffs);

	/** Empty the list - every record's focus let go with it (KCMRecordFoci). Called on Stop and when a compared document
		closes. */
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

	/** Record the story's aggregate change counter for row nth at the moment its changes were attached
		(KCMStoryRow::fAllCount / fComparedAt - 2026-09-25). Out-of-range nth is ignored. */
	void SetRowComparedAt(int32 nth, uint32 allCount);

	// (⛔**SEVEN DECLARATIONS WENT ON 2026-09-21** with the restore they served: AddReplacedChange,
	//  ReplacedSlotFor, AddReplacedChangeAt, ReplacedSlotOfMerged, RemoveReplacedChangeAt,
	//  ClearReplacedChanges and ShiftReplacedChanges. They kept the row's record of what the reader
	//  had taken in - in reading order, slid along by later writes in the same story.)

	/** Mark the row of `storyUID` as one an import could not fill (kKCMStoryKindRefused), making the
		row when the comparison built none - the story's counter did not move because nothing went in.
		★The row is found BY ITS SOURCE STORY (fSourceStoryUID, 2026-10-26): `storyUID` is the import copy's story, and a
		  row paired by place compares a Target story of another uid against it.

		★**CALLED FROM Build AND NOWHERE ELSE** (2026-09-19): the list starts empty on every build,
		  so the "!" rows are put back each time from KCMImportRefusals. ⚠Before the sort.
		★★**A STORY THE DOCUMENT DOES NOT HOLD** (a file named after a uid that is not there, or names
		  something that is not a story) gets a row that stands for the FILE: `textWhenNoStory` in the
		  text cell, no frame, no page - **and fStoryUID = kInvalidUID**. That is the one value every
		  reader of this list already passes over (the diff, the marks, the jump), so a uid that names
		  nothing - or a different object - is never handed to the document.
		@return the row's index, for AddRefusalChange. */
	int32 AddRefusalRow(IDataBase* targetDB, UID storyUID, const PMString& textWhenNoStory);

	/** One refusal under row `nth`: `kind` for the ID column, `whereAndWhy` for the text cell.
		By index rather than by uid, because a row standing for a file has no uid. Out of range does
		nothing. ⚠Before the sort, like AddRefusalRow: the index is only good until then. */
	void AddRefusalChange(int32 nth, const PMString& kind, const PMString& whereAndWhy);

	// ---- the "=" records: the places KCM wrote as the Source's (2026-09-24, stage 2 C - design section 15) ------

	/** Remember a change KCM wrote as the Source's (a mark restored, a table matched, a table applied): `live` as the
		diff made it, [nowStart, nowEnd) where the Source's words now stand, `counter` the story's counter right after
		the write (CountForKind by `counterKind`). Inserted at its slot (KCMEqualSlotFor by the live start), and the
		records after it slid by what the write removed and put in. A record whose fLive is the TWIN of `live` (the same
		what, kind and live range - a change written, undone and written again) is updated in place instead.
		`focusId` (2026-10-26): the record's focus, placed inside the write's step (FocusBeforeWrite / FocusAfterWrite) -
		0 = none, and then a twin's old focus is let go too (the write moved it, and nothing put it back on its place). */
	void AddEqual(int32 nth, const KCMStoryChange& live, TextIndex nowStart, TextIndex nowEnd,
					 uint32 counter, int32 counterKind, int32 focusId = 0);

	/** The same for EVERY ROW ONE WRITE MADE THE SOURCE'S - the words' roads: Apply This Change, Apply's paragraph road,
		Restore from Source (2026-09-25 - first for a reject of one InDesign change that several rows stood for; two
		paragraphs taken away one after the other are one deletion). `lives` are those rows as the diff made them,
		`nowStarts` where each one's Source words now stand, `liveWords` the words the write took out of each.
		★The other records slide ONCE, by the whole write, and the group's rows never slide one another: each is placed
		at its own share, its live range where its change would stand with the rest of the group written - so putting
		one row back, an undo of the whole, and the write again all find each row where it is (measured 2026-09-25 with
		Reject and Redo from Word: slid row by row, the second paragraph's record lost its live twin after an undo and a
		second record was made beside it). A record that is already one of the rows (its twin) is updated in place.
		`copies` (2026-10-24): one per row of `lives`, what Restore from Copy will paste back - nil when the road keeps
		none, whose records then hold none (kKCMCopyNotTaken).
		`focusIds` (2026-10-26): one per row of `lives`, the focus FocusBeforeWrite gave it and FocusAfterWrite placed inside
		the write's step - nil or 0 = none (AddEqual's rule for a twin's old one). A record holding that number already is
		its twin, whatever its ranges say. */
	void AddEqualGroup(int32 nth, const std::vector<KCMStoryChange>& lives, const std::vector<TextIndex>& nowStarts,
						  const std::vector<WideString>& liveWords, uint32 counter, int32 counterKind,
						  const std::vector<KCMRecordCopy>* copies = nil, const std::vector<int32>* focusIds = nil);

	// ---- ★THE FOCI OF A WRITE'S RECORDS, PLACED INSIDE ITS STEP (2026-10-26 - KCMRecordFoci.h; the spec's 3-4) ---------------
	//   A record's focus follows an Undo and a Redo of a step only if the step holds a snapshot of it, so the facade's
	//   writes - Apply This Change, Restore from Source, Match the Source, Restore from Copy - give and place the foci of
	//   the records they are making INSIDE their own step: before the write the focus stands where the words stand (made
	//   there when the record has none) and the write itself marks it (Inserted / Deleted); after the write it is put where
	//   the record's words now stand. The step's Undo then puts it back, its Redo forward. The ids go to AddEqual /
	//   AddEqualGroup / MarkPutBack after the step - or to LetWriteFocus when the step was not kept.

	/** BEFORE THE WRITE, inside its step: the focus of the record whose live change is `live` (found by `knownFocusId` when
		given, else as its twin - IsTwin), when it has one that follows; otherwise one made now on [nowStart, nowEnd) -
		where the words the write is about to change stand now (a lost one is let go first). @return its number; 0 = none
		(not the main thread, the focus-off switch, the story not there). */
	int32 FocusBeforeWrite(int32 nth, const KCMStoryChange& live, TextIndex nowStart, TextIndex nowEnd, int32 knownFocusId = 0);

	/** AFTER THE WRITE, inside its step, before it ends: focus `focusId` marked and put on [start, end) - where the
		record's words stand now (KCMRecordFoci::MoveTo with inStep). Nothing for 0. */
	void FocusAfterWrite(int32 focusId, TextIndex start, TextIndex end);

	/** A step that was not kept (aborted, a write refused, the step-fail switch): focus `focusId` let go when no record of
		row `nth` holds it (one FocusBeforeWrite made for this write); a record's own is left to the record - the abort put
		it back where the step found it. */
	void LetWriteFocus(int32 nth, int32 focusId);

	/** ★★THE OTHER RECORDS AT THE WRITE'S EDGE, WHERE THE LIST'S ORDER PUTS THEM (2026-10-26 - the apply matrix's
		WDD-apply-restore-both: two paragraphs taken out at one Source caret, the second put back from its copy first, and
		the first's "=" went AFTER it - Restore from Copy of the first then put the two back the wrong way round). InDesign
		moves a caret standing where a write inserts with the text after it (its lean): right for a record the list orders
		after the written one, wrong for one it orders before - and, where the write also took words out, for any. So every
		followed record of row `nth` that is not one of `writtenIds` and whose place began exactly at `writeAt` before the
		write is put, inside the step (marked first), where the rule the records were slid by before foci puts it
		(KCMShiftEqualFrom): past the `inserted` characters when nothing was `removed` and it is ordered from the shifted
		ones on - every other record when `allOthers` (AddEqualGroup's step 2: Apply This Change, Restore from Source),
		else those after the written record (`written` - found by writtenIds[0] or as its twin; a new record's slot) - and
		at `writeAt` otherwise. Places anywhere else are InDesign's and the rule's alike, and left to InDesign. */
	void PlaceFociAtWriteEdge(int32 nth, const std::vector<int32>& writtenIds, const KCMStoryChange& written, bool16 allOthers,
							  TextIndex writeAt, int32 removed, int32 inserted);

	/** Whether `record` (a record of row `nth`, or a copy of one) has a focus that follows - its place is then the focus's,
		which an edit before it cannot have left behind (the caret test of Restore from Copy is not needed for it). */
	bool16 RecordFollowed(int32 nth, const KCMEqualRecord& record);

	/** ★THE SWEEP (the spec's 3-8, the insurance): every focus no record of the list holds any more let go - RunOne calls
		it after each comparison. A record that went by a road that did not let its focus go leaves nothing behind. */
	void LetStrayFoci();

	/** The record behind merged index `which`, or nil for a live change, a refusal, or an index out of range. */
	const KCMEqualRecord* EqualAt(int32 nth, int32 which);

	/** Row `nth`'s live changes as the diff made them (model ranges, break and all) - empty when out of range
		(2026-09-25, for the retired reject; asked by Apply since 2026-10-24 - which rows one write covers, whether a
		story has any left). */
	void LiveChanges(int32 nth, std::vector<KCMStoryChange>& out);

	/** The record's state now: the story's counter in `targetDB` (KCMStoryDiffRun::CountForKind) says Standing,
		Undone or PutBack, and a Standing record is then READ - its place in the Target against the Source's
		(KCMRecordReadsAsSource) - and answers Stale when the two differ. ⚠With the Source document not open the
		reading cannot be made, and the counters' answer stands. */
	KCMEqualState EqualStateOf(int32 nth, const KCMEqualRecord& record, IDataBase* targetDB);

	/** ★Whether story row `nth` reads "=" itself (2026-10-26 night - IKCMStoryEditsFacade::IsEveryChangeEqual): no refusal,
		at least one entry, and every entry of the merged list a record placed for the Source - as ONE MergedOrder
		reconciles them against the armed Target now (a stale record sits out of the list; a record shown live is undone
		or put back). The same answer as asking GetChange of each child for fReplaced - ★which the tree did until then, and
		each GetChange ran MergedOrder twice (GetMergedChange, EqualAt): 2n + 1 merges of a row of n "=" on every update. */
	bool16 EveryChangeEqual(int32 nth);

	/** Whether the record whose fLive is the twin of `live` (the same what, kind and live range) is on row `nth` and
		Standing - the "=" the reader will see. kFalse when there is no such record any more (it read as not the
		Source's and was dropped) or it is not Standing. ★Asked by Restore from Source of a mark right after it
		refreshed the row (and by the reject, until 2026-10-25), so that a write that did NOT leave the Source's state
		behind is said out loud (2026-09-24 night: "if it did not come all the way back, that is InDesign's fault, and it
		has to show"). */
	bool16 EqualStanding(int32 nth, const KCMStoryChange& live, IDataBase* targetDB);

	/** After Restore from Copy put the words back: fPutBackAt = counter, and the records after it slide back by what the
		put-back put in. ⚠fNowStart / fNowEnd are left as they are - they say where the Source's words stand whenever the
		record is Standing again (an undo of the put-back), and the state chooses between them and the live range. The
		record is found by its focus (`record`'s fFocusId) when it has one, else by its fLive (what, kind, live range).
		`focusId` (2026-10-26): the focus Restore from Copy placed inside its step (FocusBeforeWrite / FocusAfterWrite) -
		taken by a record that had none or a lost one; 0 changes nothing. */
	void MarkPutBack(int32 nth, const KCMEqualRecord& record, uint32 counter, int32 focusId = 0);

	/** Drop the records in the Undone or PutBack state that have no live twin in fChanges - RunOne, right after
		SetRowChanges (design 15-1-3): the diff has found the change again, or the reader edited it away. */
	void PruneEqual(int32 nth, IDataBase* targetDB);

	/** ★THE "=" RECORDS FOLLOW EDITS MADE OUTSIDE KCM (2026-10-26, the import tidy S10): every record of row `nth` placed
		again from the comparison RunOne has just attached (SetRowChanges) - before PruneEqual, and only when the story was
		compared. A record's SOURCE range never moves; its Target place moves with every hand edit before it, which KCM is
		never told of (its own writes slide the records - AddEqual, AddEqualGroup). In order, for each record:
		  (0) ★★A RECORD WHOSE FOCUS FOLLOWS (2026-10-26 evening - KCMEqualRecord::fFocusId) stands where its focus is, and
		      nothing is looked for: a place that no longer reads as the Source's was edited by hand there - Stale, and an
		      undo of the edit brings the "=" back - never moved onto the same words somewhere else. A lost focus is let go
		      and the record is looked for as below;
		  (1) a live change with its what, kind and Source range - and its length, and its words where kept - is its own
		      change live again (undone, or put back): the live range is that change's, the "=" place at its start;
		  (2) otherwise the places its Source start maps to (KCMSourceToTargetCandidates), then its present place - the
		      first that reads as the Source's (a caret: CaretReadsAsSource) is taken; none = left where it was, and it
		      reads Stale as before.
		  A place (1) or (2) found is given a focus (made outside a step: an Undo of what came before loses it again, and the
		  comparison after that looks again).
		Then every record not Stale is marked placed for its state (fPlacedForSource): the places are this comparison's,
		so MergedOrder's reconcile has nothing left to slide - and where each live twin stands now (fComparedLiveStart). */
	void FollowRecords(int32 nth, IDataBase* targetDB);

	/** ★A CARET'S PLACE, READ BY WHAT STANDS ON EITHER SIDE OF IT (2026-10-24 night, the re-check; moved here from
		KCMFacades.cpp on 2026-10-26 - FollowRecords asks it too): a "=" at a caret - a change that took the document's words
		out - has nothing of its own to read, and an empty stretch reads as the Source's empty words ANYWHERE. So once its
		place had been moved by a write the records do not slide with, the stale place still read as "=" and Restore from
		Copy pasted the words there (measured after Apply All, before the pour was made to let the records go: four
		characters early). Up to two characters each way are read instead, against the Source's on either side of the
		Source's own caret. A Source that cannot be read answers kTrue, as RecordReadsAsSource does.
		`sourceStory` is the row's Source story (KCMStoryRow::fSourceStoryUID) - another uid than `story` for a row paired by
		the place of its first frame.
		⚠THE COST: a change not in yet that stands within two characters of the caret makes it answer kFalse at the right
		place too - Restore from Copy is refused there until that change is in (Ctrl+Z still takes the Apply back). The safe
		side: a refusal leaves the document as it is, a wrong answer pastes words where they were not. */
	bool16 CaretReadsAsSource(IDataBase* targetDB, UID story, UID sourceStory, TextIndex at, TextIndex sourceAt);

	// ⛔DropRecords (2026-10-24 night - 2026-10-26) let go of every record of a row after the import's pour (Apply All Changes,
	//   Apply All Changes in This Story): a pour does not slide the records the way KCM's own writes do, and measured then, a
	//   deletion's "=" (a caret, which reads as the Source's empty words anywhere) came back after Apply All and its Restore
	//   from Copy pasted the words four characters before their place. The rows went with it (design 9-3 #6), which the
	//   author asked to change on 2026-10-26 - RecordPour below keeps them, without their copies, and the compare places them.

	/** ★★AFTER A POUR THE ROWS STAY, AS "=" (2026-10-26, the author: "Apply on the Story row makes the rows go - keep them",
		and the same for Apply All Changes on the heading) - the import's pour calls this where it let them all go: row
		`nth`'s records are KEPT, each letting its copy go (kKCMCopyWholeStory - the pour wrote the story over, so where a
		copy would paste is not known), and one record is added for each of `lives` - the row's live changes the pour was
		asked to put in - at its Source range. ★WHAT MAKES IT SAFE SINCE DropRecords' MEASUREMENT (above): the compare after a
		pour places every record again from its Source range (FollowRecords, the import tidy S10), and one whose place does
		not read as the Source shows as what still differs. Nothing slides here. A record that is one of them already (its
		twin) is updated in place.
		★THE ROW'S FOCI ARE LET GO HERE (2026-10-26): the pour wrote the story over, so where a focus was moved to says
		nothing - and the pour's step is its own (KCMPourStoryTextWith). The compare right after places every record by its
		readings and gives each a focus; an Undo or a Redo of the pour finds those lost (made after it) and the comparison
		that follows looks again - the road every record took before foci.
		@param liveWords per change, the Target's words at its live range before the pour (what an undone write reads).
		@param counters / counterKinds per change, CountForKind after the pour, and its kind (kKCMStoryAttrNone for words and
			tables, the mark's own for a mark). */
	void RecordPour(int32 nth, const std::vector<KCMStoryChange>& lives, const std::vector<WideString>& liveWords,
					const std::vector<uint32>& counters, const std::vector<int32>& counterKinds);

	/** The row of the Target's story `storyUID` (a row standing for a story only the Source has is not one), or -1. */
	int32 RowOfTargetStory(UID storyUID);

	/** ★Whether row `nth` has to be compared again because an Undo or a Redo moved its story (2026-09-25 - the list
		followed nothing on Ctrl+Z: its "=" and its positions stayed as they were, and the panel's right-click then met
		"the list was out of date"). kTrue when the story's AGGREGATE change counter (KCMStoryRow::fAllCount - the
		aggregate, because the text counter does not see a mark) is not the one the row was compared at AND it went back
		below it or came back to a value the row WAS compared at (fComparedAt) - or when a "=" record's state no longer
		matches where its followers are placed (KCM's write or its put-back undone or done again). ⚠Plain typing moves the
		counter to new values and is answered kFalse: the row is not compared on every keystroke. */
	bool16 NeedsCompareAgain(int32 nth, IDataBase* targetDB);

	// ⛔GetRowComparedHistory / SetRowComparedHistory went on 2026-10-11 with their one caller, the PDF report's
	//   StoryDetailLoan, which put a row's comparison history back after diffing the rows on loan. The report now
	//   builds rows of its own (in the modes that keep no list) and empties the list afterwards.

	// ---- what the panel sees: the two lists as one ------------------------------------------
	//
	// ★★★**ONE INDEX SPACE, DEFINED IN ONE PLACE.** The panel asks four separate questions about
	//   "change number N of row M" (how many, which one, which attribute, does it carry a value),
	//   and the restore asked a fifth until 2026-09-21. Letting some of them count the live changes
	//   and others count the merged list would not fail loudly - it would answer about the WRONG
	//   CHANGE, which is the shape of bug this plug-in has spent the most time on
	//   ([[one-question-one-place]]). So every one of them goes through the two below.

	/** How many children row nth shows: the refusals first (2026-09-19), then the live diff's changes.
		(⛔A third list - the changes the reader had taken in - was merged in between until 2026-09-21.) */
	int32 GetMergedChangeCount(int32 nth);

	/** The change an index names, or nil when either index is out of range.

		★The first fRefusals.size() indices are the refusals, in the order the import noted them; the
		  rest are the live changes, in the order the diff made them.
		⚠**The name is history**: it merged three lists until the restore went on 2026-09-21, and it is
		 kept so that the panel's child index means the same thing on both sides of that day. */
	const KCMStoryChange* GetMergedChange(int32 nth, int32 which);

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
		★★A ROW HOLDING "=" RECORDS STAYS (2026-10-26): a story whose every change went in reads as the Source's, and Refresh
		 Comparison - which builds the list again, its records carried over (Build) - would otherwise drop the very row that
		 shows them. The records are the content: what KCM put in, and Restore from Copy's way back.
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

	/** One row's own fields for `storyUID` in `db` - the first words, the first frame, the page and the page
		index - read the way Build reads them (ReadRowFromDocument, then the page list). For a list that is not
		this one: the Track mode's rows are drawn by the same machinery (KCMTrackRead, 2026-10-05).
		@return kFalse when the story cannot be read. fKinds and the children are left as `out` had them. */
	bool16 ReadRowForStory(IDataBase* db, UID storyUID, KCMStoryRow& out);

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
		off them - what the marks and the jump use), and `other` (the source side's words).
		★★`suid` LAST (2026-10-26): the story line's Source story (KCMStoryRow::fSourceStoryUID - another number than
		`uid` for a row paired by place, whose kinds say `ByPosition`), `-` on every other line. At the end so that the
		readers that count columns from the left read what they always read. */
	void RowsAsTsv(PMString& out);
}

#endif // __KCMStoryList_h__

// End, KCMStoryList.h.
