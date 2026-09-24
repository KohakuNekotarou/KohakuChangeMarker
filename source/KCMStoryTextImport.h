//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a folder of edited stories, read back and put into the document
//
//  WHAT THIS IS FOR. The reader exported the document's stories (KCMStoryTextExport) as .docx files,
//  edited them in Word - or handed them to somebody who did - and now hands the files back. This
//  file reads them and makes each story what Word shows.
//
//  ★★★**AN IMPORT PUTS EVERYTHING INTO THE DOCUMENT, AND THE STORY MODE SHOWS WHAT IT DID** (the
//  user's decision, 2026-09-19; the design is docs/superpowers/specs/2026-09-19-kcm-import-direct-
//  design.md). The import takes a Task Start of the document as it stands, puts the words into the
//  document itself as ONE undo step, and starts the Story comparison against that Task Start: the
//  Source is the moment before, the Target is the document with the edits in. Ctrl+Z takes the
//  whole import back, and the older words are in the Source document (the "Restore" items went on
//  2026-09-21). What could NOT go in is listed first, with a red "!" (KCMImportRefusals below).
//  ★★★**WORD'S LAST STATE COUNTS** (since 2026-09-23; the design is docs/superpowers/specs/2026-09-23-
//  kcm-import-sync-design.md): each story is compared with Word's (KCMStorySync) and made that
//  (KCMStorySyncApply), revision marks or none. What was changed in InDesign after the export is
//  written over. (Until then a three-way merge carried only the changes Word's marks recorded.)
//
//  ⛔**UNTIL 2026-09-19 THIS WAS THE OTHER WAY ROUND** - the words went into the task-start COPY and a
//  fourth "Import" mode showed them, with "Change to Imported Text" putting one in at a time. That
//  mode slept here, entered by nothing, until the trial was decided; **its code went on 2026-09-20**
//  (the user's instruction). The backup is the tag backup/2026-09-19-import-as-4th-mode.
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

#include "KCMStoryShape.h"

class IDataBase;
class IDFile;
class SysFileList;			// what the open dialog hands back - the reader picks several at once

/** What one folder of edited stories holds: each story's own uid, and what was read for it.

    ⚠The two vectors are parallel and always the same length. A file that could not be read is in
      neither - it is counted and named in the message instead, never guessed at. */
struct KCMStoryTextSet
{
	std::vector<UID>					fUids;
	std::vector<KCMStoryShape::Story>	fStories;		// the story as Word shows it
	// (⛔fOrigins / fOriginKnown - the story as it stood when written, rebuilt from Word's revision
	//  marks - stood here until 2026-09-23. The import makes the story what Word shows, marks or none.)
	std::vector<PMString>				fFileNames;		// parallel: the file's own name, for a "!" row
														// that stands for a file with no story (2026-09-19)
	// (⛔fIsDocx - "read from a .docx" - stood here until 2026-09-24: kTrue for every entry since the
	//  HTML spelling went, and read by nobody. Every story here stands in the SPLIT shape around its
	//  tables (KCMStoryDocx.h, SplitAtTables); the comparison puts it back into the document's.)
};

/** One thing the last import could not put in - the material of a "!" row in Story Edits
	(2026-09-19, the user's ask: "what could not be imported, a red ! in the Δ column, at the top").

	★**EVERY REFUSAL THE POUR ALREADY COUNTS, AND NO NEW JUDGEMENT**: a place the comparison held
	  (KCMStorySync's kHeld - a table reshaped in Word into a shape InDesign cannot hold, say), a story
	  it left whole, a paragraph the write refused, an attribute kept back, a file with no story.
	  KCMStoryList::Build turns them into rows. */
struct KCMImportRefusal
{
	UID			fStory;			// the document's story (the uid the file is named after)
	PMString	fKind;			// the ID column's word: "Word" / "Table" / "Place" / "Para" / "Attr" / "File"
	PMString	fWhereAndWhy;	// the text cell: where, and why the document's words were kept
	PMString	fFileName;		// the file's own name - what a row for a story the document lacks shows
	bool16		fWholeStory;	// the whole story was left as it stands (tables, or no story)
	// ★**HELD BACK IS NOT REFUSED** (2026-09-22): the document was protected FROM something the
	//   file said - today only a tate-chu-yoko under a warichu, which Word cannot carry. It earns
	//   a "!" row like the rest, because the reader has to know, but it must NOT be counted among
	//   the things that "could not go in": nothing failed, and the document is the better for it.
	// ⚠**ADDED AT THE END ON PURPOSE** - the same discipline the facade's vtable keeps, since
	//  another plug-in may be holding this record's shape ([[facade-vtable-slot-append-only]]).
	bool16		fHeldBack;

	KCMImportRefusal() : fStory(kInvalidUID), fWholeStory(kFalse), fHeldBack(kFalse) {}
};

/** What the last import could not put in, in the order the pour met it. Empty when nothing was.
	Dropped by the next import, by a cancelled one, and by the model's shutdown (KCMPeek.cpp). (Until
	2026-09-21 it also went with the origin - KCMReleaseOrigin - which went that day.) */
const std::vector<KCMImportRefusal>& KCMImportRefusals();
void KCMClearImportRefusals();

/** Read each chosen file. It has to be a .docx to be one of ours.

    ★**THE TAG INSIDE THE FILE IS THE PAIRING** (2026-09-19), not the name: a .docx carries a
      customXml part naming the story it was written from, so a file renamed by the reader still
      goes where it belongs and a file copied onto another story's name does not. ★A .docx made in
      Word from nothing carries no tag, and is paired by the number its name begins with
      ("269.docx", "269 - chapter.docx"; 2026-09-23).
      ⚠Until 2026-09-21 there was a second spelling whose NAME was the pairing ("269.html"); it was
      retired with the rest of the HTML road.
    ★**CHOSEN AND THEN PASSED OVER IS SAID OUT LOUD.** A folder walk could pass over a stranger's
      file without a word, because nobody had asked for that one; a file the reader picked
      themselves is counted and named in whyNot - they meant it, and that count is the only thing
      that can tell them why it did not go in.
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

    ★**THE TASK START IS THE IMPORT'S** (the user's rule: "an import always takes a Task"). It is a
      copy saved on disk (2026-09-21): the reader is asked where, and a cancel there ends the import
      with nothing done. One the reader had taken before is replaced.

    ★**EACH STORY IS MADE WHAT WORD SHOWS** (2026-09-23): KCMStorySync compares it with Word's and
      says what to write, KCMStorySyncApply writes it - paragraphs changed, added and removed,
      footnotes made and taken away - minimally, so that the ruby and the kenten on the parts nobody
      edited are still there afterwards. ★The ruby, the kenten and the rest go in as a SECOND PASS
      over each story, after its words are in (KCMStoryAttrPour says why that way round).
    ⚠**WHAT CANNOT BE MADE WORD'S IS HELD, BY NAME** - a table Word reshaped into a shape InDesign
      cannot hold (rows of unequal length, a nested table where a row or a merge would go), a change
      touching an invisible character (an anchored object, a page number, an index marker: moved or
      deleted from outside only by accident), a tate-chu-yoko inside a warichu Word cannot carry. Each
      is a "!" row. ★A table whose rows, columns or merges changed is made Word's (S1/S2; since the
      evening of 2026-09-24 again - KCMStoryTextImport.cpp says what the user accepted with it).

    ★★**ONE PROGRESS BAR FROM THE FIRST FILE TO THE LAST STORY** (2026-09-17, the user's choice): it
      appears after the same three seconds as every other bar and carries Cancel. Saving the Task
      Start copy is one call into InDesign, so the bar stands still through it and a Cancel pressed
      there is answered at the next safe point. A cancel during the writes takes them all back.

    @param files the files the reader chose.
    @param outMessage what happened, for the panel's status line.
    @return kFalse when nothing could be read or nothing could be applied, or the reader cancelled. */
bool16 KCMImportStoryText(const SysFileList& files, PMString& outMessage);

#endif // __KCMStoryTextImport_h__

// End, KCMStoryTextImport.h.
