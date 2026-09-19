//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a folder of edited stories, read back and put into the document
//
//  WHAT THIS IS FOR. The reader exported the document's stories (KCMStoryTextExport), edited them
//  outside InDesign - in an editor, in a browser, or by handing the file to somebody else - and
//  now hands the files back. This file reads them and writes those words into the document.
//
//  ★★★**AN IMPORT PUTS EVERYTHING INTO THE DOCUMENT, AND THE STORY MODE SHOWS WHAT IT DID** (the
//  user's decision, 2026-09-19 - a trial; the design is
//  docs/superpowers/specs/2026-09-19-kcm-import-direct-design.md). The import takes a Task Start of
//  the document as it stands, pours the edited words into the document itself as ONE undo step, and
//  starts the Story comparison against that Task Start: the Source is the moment before, the Target
//  is the document with the edits in. "Restore Source Text" takes one change back, "Undo the
//  Restore" puts it in again, Ctrl+Z takes the whole import back. What could NOT go in is listed
//  first, with a red "!" (KCMImportRefusals below).
//
//  ⚠**UNTIL 2026-09-19 THIS WAS THE OTHER WAY ROUND** - the words went into the task-start COPY and a
//  fourth "Import" mode showed them, with "Change to Imported Text" putting one in at a time. That
//  mode's code is still here (KCMInImportMode, KCMEndImportMode, the held set) and is never entered
//  now; it sleeps until the trial is decided (the backup is the tag
//  backup/2026-09-19-import-as-4th-mode). Do not wire anything new to it.
//
//  ★**THE FILE NAMES ARE THE DOCUMENT'S OWN STORY UIDS**, because the export read that document -
//  and the words go into that document, so the pairing is the uid itself.
//
//========================================================================================
#ifndef __KCMStoryTextImport_h__
#define __KCMStoryTextImport_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KCMStoryHtml.h"

class IDataBase;
class IDFile;
class SysFileList;			// what the open dialog hands back - the reader picks several at once

/** What one folder of edited stories holds: each story's own uid, and what was read for it.

    ⚠The two vectors are parallel and always the same length. A file that could not be read is in
      neither - it is counted and named in the message instead, never guessed at. */
struct KCMStoryTextSet
{
	std::vector<UID>					fUids;
	std::vector<KCMStoryHtml::Story>	fStories;		// what the file says NOW (a .docx: the after-Word side)

	// ★★A .docx WHOSE REVISION MARKS ACCOUNT FOR EVERYTHING ALSO SAYS WHAT THE STORY WAS WHEN IT WAS
	//   WRITTEN (2026-09-19, stage 2 of the docx plan: KCMStoryDocx::Read rebuilds it from the marks,
	//   and OriginMatchesTag says whether they are the whole truth). Held here for stage 3, which
	//   will show ONLY Word's changes by merging origin, after and the document; the pour reads
	//   fStories alone until then. ⚠ALWAYS kFalse and empty for an .html - that format has no origin.
	std::vector<KCMStoryHtml::Story>	fOrigins;		// parallel; empty unless fOriginKnown
	std::vector<bool16>					fOriginKnown;
	std::vector<PMString>				fFileNames;		// parallel: the file's own name, for a "!" row
														// that stands for a file with no story (2026-09-19)
};

/** One thing the last import could not put in - the material of a "!" row in Story Edits
	(2026-09-19, the user's ask: "what could not be imported, a red ! in the Δ column, at the top").

	★**EVERY REFUSAL THE POUR ALREADY COUNTS, AND NO NEW JUDGEMENT**: a conflict the three-way merge
	  named, a story whose tables disagree, a cell or note not in the file, a paragraph the write
	  refused, an attribute kept back, a file with no story. KCMStoryList::Build turns them into rows. */
struct KCMImportRefusal
{
	UID			fStory;			// the document's story (the uid the file is named after)
	PMString	fKind;			// the ID column's word: "Word" / "Table" / "Place" / "Para" / "Attr" / "File"
	PMString	fWhereAndWhy;	// the text cell: where, and why the document's words were kept
	PMString	fFileName;		// the file's own name - what a row for a story the document lacks shows
	bool16		fWholeStory;	// the whole story was left as it stands (tables, or no story)

	KCMImportRefusal() : fStory(kInvalidUID), fWholeStory(kFalse) {}
};

/** What the last import could not put in, in the order the pour met it. Empty when nothing was.
	Lives as long as the origin the import took: dropped by KCMReleaseOrigin (a new Task Start, Stop,
	Clear, a close), by the next import, and by the model's shutdown. */
const std::vector<KCMImportRefusal>& KCMImportRefusals();
void KCMClearImportRefusals();

/** Read each chosen file. A name has to be "<decimal uid>.html" to be one of ours.

    ★**THE FILE NAME IS THE PAIRING**, and it stays that way now that files are chosen by hand
      (the user's decision, 2026-09-15). What changed with them is the SILENCE: a folder walk could
      pass over a stranger's file without a word, because nobody had asked for that one, while a
      file the reader picked themselves is counted and named in whyNot - they meant it, and the
      name is the only thing that can tell them why it did not go in.
    ⚠A file whose markup cannot be read is SKIPPED and named in whyNot, so that one bad file does
      not cost the other twenty. kFalse means nothing at all could be read.

    @param files the files the reader chose, in the dialog's own order.
    @param out cleared first, then filled.
    @param whyNot what went wrong - filled even when this answers kTrue, when some file was skipped.
    @param outCancelled set kTrue when the reader pressed Cancel on the progress bar between two files
           (2026-09-17: the import's one bar covers the reading too - KCMProgressBar.h). Nothing is
           read then, and this answers kFalse. */
bool16 KCMReadStoryTextFiles(const SysFileList& files, KCMStoryTextSet& out, PMString& whyNot,
							 bool16* outCancelled = nil);

/** The set held for the import mode, or nil when none is held. */
const KCMStoryTextSet* KCMHeldStoryText();

/** Hold a copy of `set`, dropping whatever was held before. */
void KCMHoldStoryText(const KCMStoryTextSet& set);

/** Drop it. Called by KCMReleaseOrigin - the two belong to each other - and by the model's
    shutdown, because this static holds PMStrings and std::strings (KCMStoryList.h states the rule
    and what forgetting it costs). */
void KCMReleaseStoryText();

/** Pour `set` into `db` - the reader's own document (2026-09-19; until then the task-start copy).

    ★**THE STORIES ARE PAIRED BY THEIR OWN UID**: the file is named after the document's story, and
      the words go into that document, so nothing else has to be matched.
    ★**ONE ABORTABLE COMMAND SEQUENCE.** Every write of every story is one undo step, and a Cancel
      on the progress bar - asked between two stories, the safe point KCMProgressBar.h names -
      aborts the sequence: nothing of the pour stays. `outCancelled` says so.
    ★**WHAT COULD NOT GO IN IS NOTED** (KCMImportRefusals) as well as counted in `outMessage`, so
      that the "!" rows and the status line come from one walk.

    @return kFalse when nothing at all could be applied. */
bool16 KCMPourStoryText(IDataBase* db, const KCMStoryTextSet& set, PMString& outMessage,
						bool16& outCancelled);

/** "Import Story Text..." from end to end (2026-09-19): read the chosen files, take a Task Start,
    pour the words into the document, and start the Story comparison against that Task Start.

    ★**THE TASK START IS THE IMPORT'S** (the user's rule: "an import always takes a Task"). One the
      reader had taken before is replaced - How to Use says so; no dialog asks. It is parked only so
      that a cancel or a failure can put it back exactly as it was.

    ⚠**ONLY CHANGES INSIDE A PARAGRAPH ARE APPLIED, so far.** A place whose paragraph COUNT differs
      is refused with a reason rather than guessed at: adding and removing paragraphs needs the end
      of a thread to be known exactly, and that is measured work not yet done. Everything else -
      the words inside each paragraph - goes in minimally, so that the ruby and the kenten on the
      parts nobody edited are still there afterwards.
    ★★**AND THE RUBY AND THE KENTEN THEMSELVES GO IN TOO** (2026-09-16, the user's ask: "ruby only,
      the base and the ruby together, kenten as well"). They are a SECOND PASS over each story,
      after its words are in and the story has been read again - KCMStoryAttrPour, which states why
      it has to be that way round and why it touches only a paragraph whose words already match.
    ⚠**A CHANGE TOUCHING AN INVISIBLE CHARACTER IS REFUSED.** An anchored object's character, a
      page number, an index marker: these can be moved or deleted from outside only by accident,
      and the file format carries them precisely so that this check can be made.

    ★★**ONE PROGRESS BAR FROM THE FIRST FILE TO THE LAST STORY** (2026-09-17, the user's choice): it
      appears after the same three seconds as every other bar and carries Cancel. Taking the
      document's state and building the copy are single calls into InDesign, so the bar stands still
      through them and a Cancel pressed there is answered at the next safe point. A cancel anywhere
      leaves the document unchanged and gives the reader's own Task Start back.

    @param files the files the reader chose.
    @param outMessage what happened, for the panel's status line.
    @return kFalse when nothing could be read or nothing could be applied, or the reader cancelled. */
bool16 KCMImportStoryText(const SysFileList& files, PMString& outMessage);

/** Whether the fourth mode is up. ⚠**ALWAYS kFalse SINCE 2026-09-19**: nothing holds the words any
    more (KCMImportStoryText pours them straight in), so the mode is never entered. Kept, with what
    follows, for the trial's sake - see the file comment.

    ★★★**IT IS MODAL, AND THAT IS THE POINT** (the user's rule): while an import is showing, the
      other three modes and Task Start are greyed, and Finish Import (the Start/Stop item, renamed
      while importing - 2026-09-17) is the way out. Because no
      other comparison can run inside it, the reader's own Task Start can simply be parked for its
      duration - which is what lets one origin slot serve both. */
bool16 KCMInImportMode();

/** Leave it: the import's own origin goes, the reader's parked Task Start comes back, and the mode
    that was showing before returns. Called by Stop Comparison (Finish Import while importing); doing
    nothing when no import is up. */
void KCMEndImportMode();

#endif // __KCMStoryTextImport_h__

// End, KCMStoryTextImport.h.
