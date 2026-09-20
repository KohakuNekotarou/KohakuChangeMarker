//========================================================================================
//
//  KCMStoryRestore.h
//
//  "Restore Source Text" on a change row of Story Edits (2026-09-13, the user's pick: "put this
//  part back the way it was at Task Start"): the older side's words are written over the newer
//  side's range of that one change, through ITextModelCmds - one command, one undo step.
//
//  ★THE FIRST FEATURE THAT EDITS THE USER'S DOCUMENT beyond checks and paws. Its rules:
//   1. one change, one command (ReplaceCmd, InsertCmd or DeleteCmd - KCMCreateWordsWriteCmd picks),
//      so Ctrl+Z is the whole of it;
//   2. it writes only where the change's positions still mean what they meant when the diff ran:
//      the Target story's text change counter must equal the one recorded on the row
//      (KCMStoryRow::fTargetTextCount). ★★**WHEN IT DOES NOT, THE STORY IS COMPARED AGAIN**
//      (2026-09-15, the user's decision - until then it refused and asked for a refresh by hand,
//      which made an accidental keystroke a dead end for every remaining change in that story).
//      That one story is re-diffed and this change looked up afresh by its SOURCE range - the
//      side an edit in the Target cannot move. Only a paragraph that now READS differently stops
//      the write, and only once, so that nothing goes in that the reader has not seen;
//   3. the older words are read RAW from the Source story (the armed Source, or the task-start
//      copy rehydrated for the call) at the change's fSourceStart..fSourceEnd - never from the
//      row's excerpt, which is cut and has its break characters replaced for display;
//   4. words, ruby and kenten (2026-09-13, the user's ask: "ruby too - changed, added, removed;
//      mono and group told apart; kenten if it can be done"). The ruby is written as the three
//      attributes that ARE a reading (on / the string / mono-or-group), the kenten as its KIND;
//      the look of either is left alone. Local formatting of restored words follows the
//      Target's surroundings. Refused with a reason: a custom kenten mark, and an attribute
//      change in a paragraph whose words changed too (the words go back first).
//  After the write the row's story is diffed again (KCMStoryDiffRun::RunOne) and the panel told.
//
//========================================================================================
#ifndef __KCMStoryRestore_h__
#define __KCMStoryRestore_h__

#include "BaseType.h"		// bool16 / int32 / TextIndex / ErrorCode
#include "PMString.h"

class ICommand;
class ITextModel;
class WideString;

/** Restore change `which` of Story Edits row `nth`. kTrue when the words were written (outMessage
    says how many); kFalse with the reason in outMessage. */
bool16 KCMRestoreChange(int32 nth, int32 which, PMString& outMessage);

/** Whether anything may be written back into the Target at all: kTrue only while the Source is a
    rehydrated ORIGIN - a Task Start, or the Import mode's own snapshot.

    ★★**THE USER'S RULE (2026-09-16): COMPARING TWO DOCUMENTS OFFERS NO RESTORE.** The Source is a
      document the reader can open and copy from, so the plug-in does not write for them there.
      Against a Task Start the older text exists nowhere else, which is what the items are for.
    ⚠The lent database (KIDMCP's Compare, an invisible copy) counts as two documents here - the
     rule read literally as "only against a Task Start". One line to change if that is to differ.
    ★ONE PLACE: the UI hides the items on it (facade CanWriteToTarget) and every write refuses on it. */
bool16 KCMStoryWritesAllowed();

/** The opposite: put change `which` of row `nth` back the way it was before it was taken in
    ("Undo the Restore" / "Change Back to the Original", 2026-09-16).

    ★★**A COMMAND, NOT Edit > Undo.** Ctrl+Z reaches only the last thing done; this reaches the one
      change the reader points at, whatever they have done since - and is itself one undo step.
    ★The row remembers both sides of itself, which is why it stays in the list after a take-in, so
      nothing is worked out again: the words come from fBeforeRaw (the Target's own characters,
      read before the take-in wrote - ⚠never fBeforeText, which is the row's quote), a ruby or
      kenten from fRuby - the Target's own value, the one the take-in wrote over.
    ⚠Refused when the change was never taken in, when an undo has already put it back, and for a
      custom kenten mark - the same one the take-in cannot write either. */
bool16 KCMUndoRestoreChange(int32 nth, int32 which, PMString& outMessage);

/** The one command that makes [at, at+count) of `model` read `words` - for every writer of WORDS: the
    restore, "Change Back to the Original" and the import's pour into the copy (2026-09-17).

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
// characters of a table cell, with exactly the recipe the restore uses on the user's document.
// One recipe, two callers; the report document is a throwaway, so no undo step is wanted there.

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
    (2026-09-17: the Import mode takes it in). Its X/Y offsets are its look and are left alone. */
ErrorCode KCMApplyTcy(ITextModel* model, TextIndex at, int32 len, bool16 on);

/** Warichu ON or OFF over [at, at+len) - kTAWarichuAttrBoss alone, OFF written as kFalse (2026-09-17:
    the Import mode takes it in). Its settings (lines, size, alignment...) are its look and are left alone. */
ErrorCode KCMApplyWarichu(ITextModel* model, TextIndex at, int32 len, bool16 on);

#endif // __KCMStoryRestore_h__

// End, KCMStoryRestore.h.
