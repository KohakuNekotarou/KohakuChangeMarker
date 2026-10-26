//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  A "=" RECORD'S TEXT FOCUS, OF KCM'S OWN CLASS (2026-10-26 - docs/superpowers/specs/2026-10-26-kcm-equal-follow-focus-
//  design.md, the author: "use text focus so that the '=' rows do not disappear after an edit of the text or a Refresh"):
//  a focus the user's Undo and Redo put back. KFC's KFCRowFocus, copied (KFC has used it for its result rows since
//  2026-10-14). InDesign's own focus (kTextFocusBoss) follows typing and deleting but not an Undo or a Redo (measured);
//  a focus whose range is a snapshot VIEW interface, marked dirty before every change and targeting its document's
//  database, follows both (measured in KT - docs/ai-notes/text-focus-undo-spike-2026-10-14.md: four Undos and four
//  Redos, a paragraph pasted and undone, a Change All). kKCMRecordFocusBoss is kTextFocusBoss with IID_ITEXTFOCUS
//  replaced and nothing else - the shape of InDesign's own selection and walker foci (kTextSelectionFocusBoss and three
//  more, the 20.5 dump). Made by ITextFocusManager::NewFocus(range, kKCMRecordFocusBoss) - KCMRecordFoci, which also
//  sets and clears its target database.
//
//  ONE THING IT CANNOT FOLLOW, AND SAYS SO: an Undo or a Redo of a change to its story that it holds no snapshot of - an
//  edit made before the focus was undone (older than the Apply that made it), or its story deleted and brought back.
//  InDesign then calls the view interface's ResetView, and the focus is LOST from then on - never trusted again
//  (IsLost): the record is placed by its readings, as before there were foci (KCMStoryList::FollowRecords).
//  ⚠★SAYS SO ONLY WHEN IT WAS MADE OUTSIDE A STEP (measured 2026-10-26, the full test's equal-follow E11 and E14): a focus
//   KCM makes INSIDE a write's step is never told when a step OLDER than that one is undone - no ResetView, its range
//   simply stays where the write's step left it - while one made after a step, outside any (the comparison's, after an
//   Apply All), is reset on that step's Undo (E14: twenty FOCUS lost lines for ten records, Undo and Redo). The first kind
//   can only fall behind while its record shows live: an older step is undone only after the focus's own step (Undo goes
//   back in order), and that one leaves the record Undone - whose focus FollowRecords puts back on its twin, the change
//   the comparison has just found again.
//
//========================================================================================

#ifndef __KCMRecordFocus_h__
#define __KCMRecordFocus_h__

class ITextFocus;

namespace KCMRecordFocus
{
	/** Has this focus met an Undo or a Redo it could not follow (ResetView)? false for a focus of any other class. */
	bool IsLost(const ITextFocus* focus);
}

#endif // __KCMRecordFocus_h__

// End, KCMRecordFocus.h.
