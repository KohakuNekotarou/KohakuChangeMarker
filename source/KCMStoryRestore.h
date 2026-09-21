//========================================================================================
//
//  KCMStoryRestore.h
//
//  The shared TEXT WRITERS of KCM: the one command that makes a range read given words, and the
//  attribute writers for ruby, kenten, tate-chu-yoko and warichu.
//
//  ⛔**THIS FILE WAS THE RESTORE UNTIL 2026-09-21** ("Restore Source Text", 2026-09-13; "Undo the
//  Restore", 2026-09-16; "Restore All in This Story"). All of it went on the user's word - "the
//  Source document is in front of you, so if you want it back, take it from there" - once a Task
//  Start had become a copy saved to a file that Start opens in a window. ⇒ **KCM writes nothing
//  into the reader's text from a menu any more.** What survives is what the restore SHARED:
//    - the import's pour into the document (KCMStoryTextImport.cpp / KCMStoryAttrPour.cpp), which
//      is still how edits made outside InDesign come back;
//    - the PDF report's Story table (KCMReportTable.cpp / KCMReport.cpp), which sets real ruby and
//      real kenten over the changed characters of its cells - one recipe, never two.
//  ⚠The report's document is a throwaway, so no undo step is wanted there, and the import makes a
//   sequence of its own: **nothing in this file begins one.**
//
//========================================================================================
#ifndef __KCMStoryRestore_h__
#define __KCMStoryRestore_h__

#include "BaseType.h"		// bool16 / int32 / TextIndex / ErrorCode
#include "PMString.h"

class ICommand;
class ITextModel;
class WideString;

// (⛔Four declarations stood here and went on 2026-09-21 with the restore: KCMRestoreChange,
//  KCMRestoreAllInStory, KCMUndoRestoreChange, and KCMStoryWritesAllowed - the ONE place that
//  decided whether anything could be written back into the Target at all. ★The facade slots that
//  called them are KEPT AND EMPTY, because KIDMCP calls that facade through its vtable
//  ([[facade-vtable-slot-append-only]]) - IKCMStoryEditsFacade.h says so at each one.)

/** The one command that makes [at, at+count) of `model` read `words` - for every writer of WORDS.
    It had three callers until 2026-09-21 (the restore, the undo of one, and the import's pour);
    since the restore went, the import's pour is the whole of it.

    ★★**A DELETION IS A DeleteCmd** (the user's call: "match the official way"). Every deletion in the
     SDK's samples is one (codesnippets/SnpTextModelHelper.cpp:109, hiddentext/HidTxtCommands.cpp:264,
     the footnote and endnote snippets), and ReplaceCmd is only ever handed text to put in (:142).
     KCM used to write a deletion as ReplaceCmd with an EMPTY string. ⚠**That was NOT what crashed the
     import on 2026-09-17** (work/kcm-crash-2026-09-17-emptytags*.xml): after the switch it crashed at
     the same place, and a trace showed the positions had gone stale (KCMApplyStoryTextToCopy says
     how). The switch stays because it is the official form.
    ★Otherwise ReplaceCmd when something is there to replace, InsertCmd when nothing is.
    @return an AddRef'd command for an InterfacePtr to take, or nil when there is nothing to write
      (no characters out and none in) or the story has no ITextModelCmds. */
ICommand* KCMCreateWordsWriteCmd(ITextModel* model, TextIndex at, int32 count, const WideString& words);

// ---- the attribute writers, shared with the PDF report (2026-09-13) ------------------------
// The report's Story section (KCMReportTable.cpp) sets real ruby and real kenten over the changed
// characters of a table cell, with exactly the recipe the import pours into the document with.
// One recipe, two callers; the report document is a throwaway, so no undo step is wanted there.
// (⛔The third caller was the restore, until 2026-09-21. The recipe stayed because the other two
//  were never its own - it shared theirs.)

/** The ruby strand exists on a story only once something put ruby on it; a story that never had
    any needs it made first. kSuccess when it exists afterwards. */
ErrorCode KCMCreateRubyStrandIfNeeded(ITextModel* model);

/** One reading onto [at, at+len): the attributes that ARE a reading (on, the string - and "group" for a
    group reading; a MONO reading writes no setting and takes a group override off, leaving it to the
    paragraph style - the user's rule of 2026-09-17). ⚠Call KCMCreateRubyStrandIfNeeded first on a story
    that never had ruby. */
ErrorCode KCMApplyRuby(ITextModel* model, TextIndex at, int32 len, const PMString& reading, bool16 group);

/** Ruby OFF [at, at+len), by clearing the overrides of all thirty ruby attributes.

    ★**IT IS THIRTY AND NOT THREE** (KIDMCPRuby.cpp's list): taking off only the three that are the
      reading leaves the twenty-seven that are its look behind, where the comparison then reports
      them as a difference nobody made. Exported for the import's third caller, 2026-09-16. */
ErrorCode KCMClearRuby(ITextModel* model, TextIndex at, int32 len);

/** The kenten KIND onto [at, at+len) (IKentenStyle::Kenten_None = off; the look is left alone). */
ErrorCode KCMApplyKentenKind(ITextModel* model, TextIndex at, int32 len, int16 kind);

/** The kenten kind for the name the comparison reports ("BlackCircle" ...). kFalse for a name
    this build cannot write ("Custom" among them). */
bool16 KCMKentenKindOf(const PMString& name, int16& outKind);

/** Tate-chu-yoko ON or OFF over [at, at+len) - kTATatechuyokoAttrBoss alone, OFF written as kFalse
    (2026-09-17; restorable wherever it stands since 2026-09-20). Its X/Y offsets are its look and
    are left alone. */
ErrorCode KCMApplyTcy(ITextModel* model, TextIndex at, int32 len, bool16 on);

/** Warichu ON or OFF over [at, at+len) - kTAWarichuAttrBoss alone, OFF written as kFalse
    (2026-09-17; restorable wherever it stands since 2026-09-20). Its settings (lines, size,
    alignment...) are its look and are left alone. */
ErrorCode KCMApplyWarichu(ITextModel* model, TextIndex at, int32 len, bool16 on);

#endif // __KCMStoryRestore_h__

// End, KCMStoryRestore.h.
