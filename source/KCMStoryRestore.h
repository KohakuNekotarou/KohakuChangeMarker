//========================================================================================
//
//  KCMStoryRestore.h
//
//  "Restore Source Text" on a change row of Story Edits (2026-09-13, the user's pick: "put this
//  part back the way it was at Task Start"): the older side's words are written over the newer
//  side's range of that one change, through ITextModelCmds - one command, one undo step.
//
//  ★THE FIRST FEATURE THAT EDITS THE USER'S DOCUMENT beyond checks and paws. Its rules:
//   1. one change, one command (ReplaceCmd or InsertCmd), so Ctrl+Z is the whole of it;
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

class ITextModel;

/** Restore change `which` of Story Edits row `nth`. kTrue when the words were written (outMessage
    says how many); kFalse with the reason in outMessage. */
bool16 KCMRestoreChange(int32 nth, int32 which, PMString& outMessage);

/** The opposite: put change `which` of row `nth` back the way it was before it was taken in
    ("Undo the Restore" / "Change Back to the Original", 2026-09-16).

    ★★**A COMMAND, NOT Edit > Undo.** Ctrl+Z reaches only the last thing done; this reaches the one
      change the reader points at, whatever they have done since - and is itself one undo step.
    ★The row remembers both sides of itself, which is why it stays in the list after a take-in, so
      nothing is worked out again: the words come from fBeforeText, a ruby or kenten from fRuby -
      the Target's own value, the one the take-in wrote over.
    ⚠Refused when the change was never taken in, when an undo has already put it back, and for a
      custom kenten mark - the same one the take-in cannot write either. */
bool16 KCMUndoRestoreChange(int32 nth, int32 which, PMString& outMessage);

/** Every change of row `nth`, in ONE undo step (2026-09-15, the user's ask: the same thing for a
    whole story, and for the whole list).

    ★★**THE WALK IS BACKWARDS, SO THE STORY IS COMPARED TWICE RATHER THAN 2N TIMES** - once at the
      start when the reader has typed since the comparison, once at the end to rebuild the row.
      Writing from the end means each write disturbs only text the walk has already passed. The
      obvious loop over KCMRestoreChange would re-diff the whole story for every change.
    ★**A CHANGE THAT CANNOT GO IN IS SKIPPED, NOT A STOP** (the user's call, 2026-09-15): pressing a
      bulk item says "all of them", so the run finishes and the status line counts what went in and
      what did not, naming the first reason.
    ⚠A change already taken in - the Import mode's `=` rows - is not a candidate to take in again.

    @return kTrue when at least one change went in. */
bool16 KCMRestoreAllInStory(int32 nth, PMString& outMessage);

/** The same across every row of the list, still in ONE undo step. @see the note above, which holds
    per story; the rows are walked in order and the counts add up across them. */
bool16 KCMRestoreAllStories(PMString& outMessage);

// ---- the attribute writers, shared with the PDF report (2026-09-13) ------------------------
// The report's Story section (KCMReportTable.cpp) sets real ruby and real kenten over the changed
// characters of a table cell, with exactly the recipe the restore uses on the user's document.
// One recipe, two callers; the report document is a throwaway, so no undo step is wanted there.

/** The ruby strand exists on a story only once something put ruby on it; a story that never had
    any needs it made first. kSuccess when it exists afterwards. */
ErrorCode KCMCreateRubyStrandIfNeeded(ITextModel* model);

/** One reading onto [at, at+len): the three attributes that ARE a reading (on, the string,
    mono-or-group). ⚠Call KCMCreateRubyStrandIfNeeded first on a story that never had ruby. */
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

#endif // __KCMStoryRestore_h__

// End, KCMStoryRestore.h.
