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

#endif // __KCMParagraphStyle_h__

// End, KCMParagraphStyle.h.
