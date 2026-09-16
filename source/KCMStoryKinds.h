//========================================================================================
//
//  KCMStoryKinds.h
//
//  What kind of change a Story Edits row reports -- the enums, and the questions every widget asks
//  of an attribute kind (KCMAttrKindHasMarkLine / KCMAttrKindIsLayered, 2026-09-16), which need
//  nothing but the values and so cross the boundary with them.
//
//  WHY THEY ARE A FILE OF THEIR OWN. Both halves read them: the model fills a row's fKinds, and
//  the UI names the kinds on the row and picks which window a Removed row jumps to. They used to
//  be reached by including KCMStoryStamp.h, which also declares the three KCMStoryEdits free
//  functions that read and compare a document's counters -- model-side work the UI can see and
//  cannot link to. A header that crosses the boundary should carry the type and nothing else,
//  the way KCMBookResult.h already does.
//
//  @warning the VALUES are the contract, not the enum name: IKCMStoryEditsFacade carries fKinds
//  as a uint32 and fAttrKind as an int32, so a value that has shipped must never be renumbered
//  (the boundary header lists the ones it carries).
//
//========================================================================================
#ifndef __KCMStoryKinds_h__
#define __KCMStoryKinds_h__

#include "BaseType.h"		// uint32

/** Which kind of change moved. Values are OR'd together: one edit can move more than one of them.

	The first three map one-to-one onto ITextModel's three sub-counters. The last two are not
	counters -- they mean one side has no story with this UID at all, so there is nothing to have
	compared.
*/
enum KCMStoryChangeKind
{
	kKCMStoryKindNone		= 0,
	kKCMStoryKindText		= 1,	// characters inserted, removed or replaced
	kKCMStoryKindAttr		= 2,	// effective attributes -- INCLUDING applied styles and overrides,
									// and, measured, table strokes and cells as well
	kKCMStoryKindOther	= 4,	// the Other counter. Nothing has been found that moves it: the
									// table and inline edits its documentation names all landed on
									// Attr or Text instead (see KCMStoryStamp.h). Kept because the
									// header defines it, and because Compare names it for the row
									// whose aggregate moved while no sub-counter did
	kKCMStoryKindAdded	= 8,	// no story with this UID on the source side
	kKCMStoryKindRemoved	= 16	// no story with this UID on the TARGET side: the story was in the
									// older version and is gone from the newer one.
									// **THE ROW THEN LIVES IN THE SOURCE DOCUMENT**, and it is the only
									// kind for which that is true -- see KCMStoryDiff::fStoryUID
};

/** Which kind of attribute a row's CHILDREN found a difference in.

	**NOT THE SAME SORT OF THING AS KCMStoryChangeKind ABOVE**, which is why it is a separate enum
	rather than more bits in that one. Those come from the two documents' CHANGE COUNTERS -- read
	them again and they say the same, which is why a row refresh leaves them alone. This comes from
	the DIFF: it does not exist until the two versions have actually been compared.

	**THE ORDER MEANS NOTHING** -- these are names, not ranks. Ruby came first because a Japanese
	document uses it constantly, and because a ruby-only edit is precisely the case the reader
	found being reported as "None". **TEXT, RUBY AND KENTEN ARE WHAT IS REPORTED** (kenten added in
	August, withdrawn the next day, and back since 2026-09-01) -- and the two attributes are
	different mechanisms: ruby is a STRAND (IRubyAttrStrand, run-based, written in the snippet
	as RubyFlag 1/2 over one CharacterStyleRange per character) while kenten is a set of CHARACTER
	ATTRIBUTES (the twenty kTAKenten*Boss on kCharAttrStrandBoss, its kind in kTAKentenKindBoss
	with Kenten_None for off). What the panel cared about is the one thing they share: the text did
	not move and something over it did.

	@warning carried across the model/UI boundary as a plain int32 (IKCMStoryEditsFacade's
	  Row::fAttrKind), the same way KCMStoryChange::What is. **ADDING A VALUE MEANS TOUCHING BOTH
	  SIDES**, and a value must never be renumbered once it has shipped. The boundary header lists
	  the values it carries, and kenten's 2 is not among them -- which is that contract working.
*/
enum KCMStoryAttrKind
{
	kKCMStoryAttrNone = 0,	// the children are text changes, or there are none
	kKCMStoryAttrRuby = 1,	// a reading over characters that did not themselves change
	kKCMStoryAttrKenten = 2,	// emphasis marks (kenten), over characters that did not themselves
								// change. ★Reported from 2026-09-01 (user: "if it can be found, I
								// want to find it"), after a day in August and a long pause. The
								// pause is why the number is worth reading twice: **the value was
								// held reserved throughout it**, so turning the feature back on
								// renumbered nothing and invalidated no saved state.
								// ⚠ITS VALUE IS A KIND, NOT A READING ("BlackCircle"), and the
								// panel draws it as the MARK rather than writing it out
								// (ui/KCMKentenMark). Anything that shows it must ask fAttrKind
								// first - the string cannot say which it is.
	kKCMStoryAttrFootnote = 3,	// ★a FOOTNOTE hanging off one character (2026-09-08, user's
								// request: "the page shows a 1 above the character - show it in the
								// row the way ruby is shown"). ITS VALUE IS THE NUMBER AS THE PAGE
								// PRINTS IT ("1"), asked of InDesign rather than counted here, so a
								// document that restarts its numbering still agrees with the row.
								// ⚠**THE MARKER IS A CHARACTER AND THE OTHER TWO ARE NOT.** Ruby and
								// kenten are attributes over text that stays; a footnote reference is
								// U+0004 standing IN the text. It travels as an attribute all the
								// same, because what the reader needs of it is identical - a value
								// sitting over a place - and because the alternative (a text change
								// of one invisible character) is what the panel used to show: a "□"
								// nobody could read.
	kKCMStoryAttrEndnote = 4,	// ★the same for an ENDNOTE (U+0005). ⚠**ITS TEXT LIVES IN ANOTHER
								// STORY** (kEndnoteStoryBoss - measured 2026-09-08: adding one makes
								// app.documents[0].stories go from 1 to 2), so only the marker is
								// reported here; the note's own words arrive as a story row of their
								// own, exactly as they did before.
	kKCMStoryAttrWarichu = 5,	// ★WARICHU set on or taken off characters (2026-09-16, user's request -
								// reversing 2026-09-09's "leave warichu out"). kTAWarichuAttrBoss, the
								// ON/OFF alone: its line count, size and alignment are not compared
								// (the Pixel mode sees those). ★ITS VALUE IS THE CHARACTERS IT COVERS.
								// ★LAYERED: drawn as a line of its own over the text (KCMStoryLayers.h).
	kKCMStoryAttrTcy = 6		// ★TATE-CHU-YOKO set on or taken off characters (2026-09-16, the same
								// request), the manual one: kTATatechuyokoAttrBoss. Its X/Y offsets
								// and the automatic tate-chu-yoko (a paragraph setting) are not
								// compared. ★ITS VALUE IS THE CHARACTERS IT COVERS; LAYERED, over a
								// warichu when it stands inside one.
								// ⚠**Neither of the two is written back** (user's call): their changes
								// carry kKCMWriteBlockedKind - ★**except a tate-chu-yoko in the Import
								// mode** (2026-09-17, the user's call), which is taken in.
};

/** Whether a change of this kind is drawn on TWO LINES - a value (or a mark) standing over the
	characters it belongs to.

	★★★**ALWAYS, WHETHER OR NOT THIS SIDE HAS A VALUE** (2026-09-16, the user's rule: "when a ruby is
	  removed, two lines - a bar over the kanji; kenten the same", and footnotes and endnotes with
	  them). The side that has no ruby, no kenten or no note draws a BAR on the upper line, over where
	  it would stand. **This reverses 2026-09-01** ("when the ruby or the kenten is gone, make it one
	  line"), which kept the height and the drawing in step by asking whether the value was empty;
	  now nothing asks that, and the two stay in step because both ask THIS.
	★ONE QUESTION IN ONE PLACE for everything that lays such a change out: the change row's height
	  and drawing (KCMStoryTreeWidgetMgr, KCMStoryCellView), the message area (KCMStatusTextView,
	  reached through KCMStoryJump) and the PDF report (KCMReport). A kind added above is two lines
	  exactly when it is added here. */
inline bool16 KCMAttrKindHasMarkLine(int32 attrKind)
{
	switch (attrKind)
	{
		case kKCMStoryAttrRuby:
		case kKCMStoryAttrKenten:
		case kKCMStoryAttrFootnote:
		case kKCMStoryAttrEndnote:
		case kKCMStoryAttrWarichu:
		case kKCMStoryAttrTcy:
			return kTrue;
		default:
			return kFalse;
	}
}

/** Whether a change of this kind is drawn in LAYERS - the text below, a warichu over the place it
	stands, a tate-chu-yoko over the place IT stands - rather than as a value over its characters.

	★WARICHU AND TATE-CHU-YOKO (2026-09-16, the user's drawing: "77" over "わりちゅう｜のぶん" over
	  "琥珀｜猫太郎"). The two are pieces of text set apart from the line they stand in, and one can
	  stand inside the other, so each is lifted onto a line of its own and leaves a bar where it was.
	  The model works out the lines (KCMParaText::PlanLayers) and carries them as strings
	  (KCMStoryLayers); nothing on the UI side decides what goes on which line. */
inline bool16 KCMAttrKindIsLayered(int32 attrKind)
{
	return (attrKind == kKCMStoryAttrWarichu || attrKind == kKCMStoryAttrTcy) ? kTrue : kFalse;
}

/** The two kinds that mean "this story has no partner in the other version".

	**ONE PLACE TO ASK IT.** Added and Removed differ in WHICH document holds the story, but they
	agree on everything that follows from having nobody to compare against: no text diff is run for
	them, they cannot be refreshed, and their label stands alone with no '+' after it. Everything
	that wants that answer asks this rather than testing kKCMStoryKindAdded on its own
	([[one-question-one-place]]).

	@warning **the jump is the exception, and it must NOT use this:** which window moves is exactly
	 the thing the two kinds disagree about. It tests kKCMStoryKindRemoved by itself
	 (ui/KCMStoryJump.cpp).
*/
const uint32 kKCMStoryKindUnpaired = kKCMStoryKindAdded | kKCMStoryKindRemoved;

/** Why a TEXT change cannot be written back into the reader's document (2026-09-16, the user's rule:
	"in the Task Start mode, restore only when the range holds no special character - and only then
	show the item").

	★**DECIDED BY THE DIFF, CARRIED TO THE MENU, ASKED AGAIN BY THE WRITE.** The comparison knows
	  both sides' paragraphs and characters, so it names the reason once per change; the UI hides
	  "Restore Source Text" / "Change to Imported Text" on a change that has one; and the write
	  itself asks the same questions of the characters as they stand at that moment, because the
	  reader can type between the two (KCMStoryRestore.cpp).
	@warning carried across the model/UI boundary as a plain int32 (IKCMStoryEditsFacade's
	  Change::fWriteBlock) - the values are the contract and must not be renumbered. */
enum KCMStoryWriteBlock
{
	kKCMWriteAllowed = 0,
	kKCMWriteBlockedPlaces = 1,		// a table cell or a footnote whose place is not on the other side
									// (KCMParaText::WordsCanBeWrittenAcross) - measured, a deleted
									// table's cell "restored" into a position nothing could see
	kKCMWriteBlockedObjects = 2,	// the words going in or coming out hold a character InDesign hangs
									// an object on (KCMParaText::IsObjectCharacter) - measured, an
									// anchored rectangle came back as U+FFFC alone
	kKCMWriteBlockedKind = 3		// ★a kind of change that is shown and never written back - warichu
									// and tate-chu-yoko (2026-09-16, user's call; a tate-chu-yoko in the
									// Import mode is taken in since 2026-09-17). Set by the diff
									// (KCMStoryDiffRun's AddAttrChange), so the menu hides the item
									// rather than offering one that then refuses
};

#endif // __KCMStoryKinds_h__

// End, KCMStoryKinds.h.
