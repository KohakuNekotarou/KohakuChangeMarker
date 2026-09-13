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
//   2. it writes only where the change's positions still mean what they meant when the diff
//      ran: the Target story's text change counter must equal the one recorded on the row
//      (KCMStoryRow::fTargetTextCount), otherwise it refuses and asks for a refresh;
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

/** The kenten KIND onto [at, at+len) (IKentenStyle::Kenten_None = off; the look is left alone). */
ErrorCode KCMApplyKentenKind(ITextModel* model, TextIndex at, int32 len, int16 kind);

/** The kenten kind for the name the comparison reports ("BlackCircle" ...). kFalse for a name
    this build cannot write ("Custom" among them). */
bool16 KCMKentenKindOf(const PMString& name, int16& outKind);

#endif // __KCMStoryRestore_h__

// End, KCMStoryRestore.h.
