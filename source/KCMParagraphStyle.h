//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Paragraph styles for the paragraphs an import or a restore puts in (2026-09-17, the user's rule):
//  a new paragraph gets the style pressing Return after the one before it would give it - that
//  paragraph's NEXT STYLE, or its own style when the next style is [Same Style] - chained over several
//  new paragraphs, the way pressing Return again and again chains them.
//  ★"This is very important - whether it is there or not changes how usable the whole thing is."
//
//========================================================================================

#ifndef __KCMParagraphStyle_h__
#define __KCMParagraphStyle_h__

#include "BaseType.h"		// int32 / TextIndex / ErrorCode
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

class ITextModel;
class IDataBase;

/** The paragraph style applied at `at`, or kInvalidUID. */
UID KCMParagraphStyleAt(ITextModel* model, TextIndex at);

/** The style a paragraph typed after one in `style` gets: its next style, or `style` itself when the
	next style is [Same Style] (measured on the DOM: a style's nextStyle is ITSELF by default; kInvalidUID
	is taken the same way). */
UID KCMNextParagraphStyle(IDataBase* db, UID style);

/** Gives the new paragraphs [newStart, newStart + newLength) the styles pressing Return after the
	paragraph standing at `prevAt` would give them, chained (ITextModelCmds::ApplyStyleCmd with
	autoNextStyle).
	★**NOTHING IS WRITTEN WHEN THE CHAIN STAYS ON THE PREVIOUS PARAGRAPH'S STYLE.** New paragraphs put in
	  right after its last character have ITS style and its overrides already (measured 2026-09-17: "\rNEW"
	  before a paragraph's return came back in that paragraph's style with its left indent) - exactly what
	  Return gives. A different style is applied with the overrides replaced: the previous paragraph's
	  hand adjustments belong to it, not to a paragraph of another style.
	@return kSuccess when nothing needed writing too. */
ErrorCode KCMApplyNextStyleAfter(ITextModel* model, TextIndex prevAt, TextIndex newStart, int32 newLength);

/** A paragraph style's full path, "Group:Sub:Name" (IStyleGroupHierarchy::GetFullPath), or empty. */
PMString KCMParagraphStylePath(IDataBase* db, UID style);

/** The paragraph style whose full path is `path` in the document `db` holds, or kInvalidUID. */
UID KCMFindParagraphStyle(IDataBase* db, const PMString& path);

/** One paragraph style over [start, start + length), keeping or replacing the overrides. */
ErrorCode KCMApplyParagraphStyle(ITextModel* model, TextIndex start, int32 length, UID style,
								 bool16 replaceOverrides);

/** The paragraphs standing AFTER a paragraph about to be taken out, in the same thread, and whether
	each of them wore the NEXT STYLE of the paragraph before it - the mark of a chain pressing Return
	made (2026-09-19, the user: "1 2 3 4, and 2 goes: 3 and 4 have to move along the next styles, or
	the styles are wrong even though the words did not change").
	★**THE CHAIN IS READ BEFORE THE WRITE**, when the paragraph being taken out is still there to be
	  the first link's "before". Filled by KCMSnapshotChainAfter; read by KCMRechainAfterRemoval. */
struct KCMChainAfter
{
	std::vector<TextIndex>	fStarts;	// each following paragraph's first character, BEFORE the removal
	std::vector<bool16>		fChained;	// whether its style was the next style of the paragraph before it
};

/** Reads the chain after the paragraph [removedFrom, removedTo) - either "\rTEXT" (the return before it and
	its words) or "TEXT\r" at the start of its place, the two shapes a whole-paragraph removal takes. */
void KCMSnapshotChainAfter(ITextModel* model, TextIndex removedFrom, TextIndex removedTo, KCMChainAfter& out);

/** After the removal: gives the following paragraphs the next styles chained from the paragraph now
	standing before them - AS FAR AS THE CHAIN WENT and no further. A paragraph whose style was not its
	predecessor's next style was chosen by hand, and stops the walk; the overrides of a re-styled
	paragraph are kept (they are its own). Nothing is written where the style already agrees.
	@param removedCount how many characters the removal took out (every start in `chain` moved by it).
	@param returnBefore kTrue for the "\rTEXT" shape: the removal began with the return of the paragraph
		before, and that paragraph now ends with the return standing AT removedFrom - which is where its
		style is read, so that an EMPTY paragraph before (its return was the thread's first character)
		still counts as standing there. ⚠Until 2026-09-19 evening removedFrom == threadStart was read as
		"the first paragraph of its place went" for both shapes, and an empty first paragraph's followers
		were never re-chained (measured: a taken out, then b - c stayed C instead of A's next, B).
		kFalse for "TEXT\r" at the start of its place: nothing stands before the chain.
	@return kSuccess when nothing needed writing too. */
ErrorCode KCMRechainAfterRemoval(ITextModel* model, TextIndex removedFrom, int32 removedCount,
								 const KCMChainAfter& chain, bool16 returnBefore);

/** The styles of `count` consecutive paragraphs, the first being the one that holds `anchor`, the rest
	the ones after it in the same thread (fewer when the thread ends first). What a whole-paragraph
	take-in remembers before it writes (2026-09-19, KCMStoryChange::fBeforeParaStyles). */
void KCMReadParagraphStyles(ITextModel* model, TextIndex anchor, int32 count, std::vector<UID>& out);

/** Reads the chain of the paragraphs from `firstAt` to the end of its thread - whether each wears the
	NEXT STYLE of the paragraph before it - the way KCMSnapshotChainAfter does for a removal, here for a
	paragraph about to be PUT BACK in front of them ("Undo the Restore"). `prevAt` is a character of the
	paragraph standing before `firstAt`, or -1 (or a position before the thread) when none does: the first
	paragraph is then "not chained". fStarts are BEFORE the write; only fChained is read afterwards. */
void KCMSnapshotChainFrom(ITextModel* model, TextIndex firstAt, TextIndex prevAt, KCMChainAfter& out);

/** Puts the styles back after "Undo the Restore" has written a whole paragraph's words - on the paragraph
	before it, on the paragraph put back, and on the paragraphs after it (2026-09-19, the user's rule, in
	TWO LAYERS):
	  the paragraph before (`firstDerived` == 1)  -> exactly what was remembered: nothing about it changed,
	                                                 so nothing is derived (and only while it still wears
	                                                 what the take-in left it, `asLeft`);
	  the paragraph put back (index firstDerived) -> the NEXT STYLE of the paragraph above when that style
	                                                 names one, else what was remembered ([Same Style] names
	                                                 none); with no record, the next style or nothing;
	  the paragraphs after it                     -> the same two layers while the record still describes
	                                                 them (`asLeft` agrees); from the first one it does not -
	                                                 or beyond the record - the CHAIN alone, the mirror of
	                                                 the take-out's KCMRechainAfterRemoval: a paragraph that
	                                                 wore the next style of the paragraph before it BEFORE
	                                                 the write (`followersBefore`) gets the next style of
	                                                 the paragraph now before it, when that names one; the
	                                                 first that did not stops the walk.
	★Why both: measured 2026-09-19 evening. Three new paragraphs a b c taken out in the order c, b, a and
	 put back in the order c, b, a: c came back under the empty first paragraph and rightly took its next
	 style (B); b then came back between them, and c - which b's record could not hold, c having been gone
	 when b was taken out - stayed B where the chain says C. The chain is what knows about paragraphs the
	 record never saw; the record is what knows a style the chain cannot derive ([Same Style], a style
	 chosen by hand).
	The paragraph above is read after IT has been restored, so a chain propagates. Overrides are kept; a
	style that no longer exists is skipped; nothing is written where the style already agrees.
	@param remembered / asLeft KCMStoryChange::fBeforeParaStyles / fAfterParaStyles (either may be empty).
	@param followersBefore KCMSnapshotChainFrom, taken BEFORE the words were written.
	@return kSuccess when nothing needed writing too. */
ErrorCode KCMRestoreParagraphStyles(ITextModel* model, TextIndex anchor, const std::vector<UID>& remembered,
									const std::vector<UID>& asLeft, int32 firstDerived,
									const KCMChainAfter& followersBefore);

#endif // __KCMParagraphStyle_h__

// End, KCMParagraphStyle.h.
