//========================================================================================
//
//  KCMStoryKinds.h
//
//  What kind of change a Story Edits row reports -- the two enums alone.
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
	kKCMStoryAttrKenten = 2	// emphasis marks (kenten), over characters that did not themselves
								// change. ★Reported from 2026-09-01 (user: "if it can be found, I
								// want to find it"), after a day in August and a long pause. The
								// pause is why the number is worth reading twice: **the value was
								// held reserved throughout it**, so turning the feature back on
								// renumbered nothing and invalidated no saved state.
								// ⚠ITS VALUE IS A KIND, NOT A READING ("BlackCircle"), and the
								// panel draws it as the MARK rather than writing it out
								// (ui/KCMKentenMark). Anything that shows it must ask fAttrKind
								// first - the string cannot say which it is.
};

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

#endif // __KCMStoryKinds_h__

// End, KCMStoryKinds.h.
