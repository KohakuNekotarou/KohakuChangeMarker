//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - a folder of edited stories, read back and offered against the document
//
//  WHAT THIS IS FOR. The reader exported the document's stories (KCMStoryTextExport) as .docx files,
//  edited them in Word - or handed them to somebody who did - and now hands the files back. This
//  file reads them, makes a copy of the document what Word shows, and lets the Story mode say what
//  that would change in the document.
//
//  ★★★**THE FILES' WORDS GO INTO A TASK START IN MEMORY, NEVER INTO THE DOCUMENT** (the author's trial
//  of 2026-10-24, adopted 2026-10-25 - docs/superpowers/specs/2026-10-24-kcm-import-as-source-design.md,
//  the tidy 2026-10-24-kcm-import-tidy-design.md). The import takes a Task Start of the document in
//  memory (KCMTakeImportClone), pours the files into IT, and starts the Story comparison: the Source is
//  the document as the files would make it, the Target the document as it stands - which the import
//  does not change. Each row is a proposal, and Apply puts it in, unsigned and untracked
//  (IKCMStoryEditsFacade). What could NOT go into the copy is listed first, with a red "!"
//  (KCMImportRefusals below).
//  ★★★**WORD'S LAST STATE COUNTS** (since 2026-09-23; the design is docs/superpowers/specs/2026-09-23-
//  kcm-import-sync-design.md): each story is compared with Word's (KCMStorySync) and made that
//  (KCMStorySyncApply), revision marks or none. (Until then a three-way merge carried only the changes
//  Word's marks recorded.)
//
//  ⛔**TWO ROADS WENT BEFORE THIS ONE.** Until 2026-09-19 the words went into the task-start copy and a
//  fourth "Import" mode showed them (its code went on 2026-09-20 - the tag backup/2026-09-19-import-as-
//  4th-mode). From 2026-09-19 to 2026-10-25 the import put every word into the DOCUMENT, tracked and
//  signed "KohakuChangeMarker", against a Task Start saved to a file - with "Reject This Import Change"
//  and "Redo from Word" on the rows; that road and both items went with the tidy (2026-10-25).
//
//  ★**THE FILE NAMES ARE THE DOCUMENT'S OWN STORY UIDS**, because the export read that document -
//  and the copy holds the same uids, so the pairing is the uid itself.
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

/** One text box of a page file ("Export Pages to Word...", 2026-10-25 - KCMPageDocx.h): which frame's part of which
	story it came from, the fingerprint of that part when exported, and what Word holds now. The import puts it back into
	its story at the range it came from (KCMPageImport.h) and the story then goes the way a story file goes. */
struct KCMPageBoxEdit
{
	UID						fStory;
	UID						fFrame;
	int32					fStart;			// the part's range when exported (TextIndex)
	int32					fEnd;
	int32					fPrintBytes;	// KCMPageSplice::Fingerprint of the part when exported
	uint32					fPrintCrc;
	int32					fBoxN;			// the box's number on its page
	KCMStoryShape::Story	fContent;		// what the box holds
	PMString				fFileName;		// the page file's own name - for the "!" row
	PMString				fWhyNot;		// non-empty: the file itself already says this box cannot go in

	KCMPageBoxEdit() : fStory(kInvalidUID), fFrame(kInvalidUID), fStart(0), fEnd(0), fPrintBytes(0), fPrintCrc(0), fBoxN(0) {}
};

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

	/** ★THE PAGE FILES' BOXES (2026-10-25), as read - not yet stories. Once the import holds its copy of the document,
		KCMSpliceBoxesIntoSet turns them into whole stories here (fUids / fStories / fFileNames) and empties this. ⚠At the
		end on purpose, as KCMImportRefusal's newest field is. */
	std::vector<KCMPageBoxEdit>			fBoxes;
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
    ★**A PAGE FILE IS READ AS ITS BOXES** (2026-10-25, "Export Pages to Word..." - KCMPageDocx.h): asked FIRST, by its
      page tag, and its boxes go into out.fBoxes - even one the file itself refuses (gone in Word, there twice), with
      its reason, so it has a "!" row. Words typed outside every box are named in whyNot.

    @param files the files the reader chose, in the dialog's own order.
    @param out cleared first, then filled.
    @param whyNot what went wrong - filled even when this answers kTrue, when some file was skipped.
    @param outCancelled set kTrue when the reader pressed Cancel on the progress bar between two files
           (2026-09-17: the import's one bar covers the reading too - KCMProgressBar.h). Nothing is
           read then, and this answers kFalse. */
bool16 KCMReadStoryTextFiles(const SysFileList& files, KCMStoryTextSet& out, PMString& whyNot,
							 bool16* outCancelled = nil);

/** How a pour is made (2026-10-24): where the words go is the caller's - the import's copy (the import itself), or the
	document (Apply All Changes, Apply All Changes in This Story) - and these say the rest. ★ALWAYS UNSIGNED AND
	UNTRACKED: what a pour writes keeps no change history (the old road's signature went with it on 2026-10-25). */
struct KCMPourOptions
{
	bool16				fNoteRefusals;	// what could not go in becomes the "!" rows (KCMImportRefusals)
	// (⛔fKeepFor - the database Word's content was kept under, per story, for Apply - went on 2026-10-26 with the keep
	//  itself: Apply reads the import's copy instead, KCMImportSourceStory below - the import tidy, S5.)
	std::vector<UID>	fOnly;			// empty = every story of the set; otherwise these alone
	PMString			fStepName;		// the undo step's name
	PMString			fBarTitle;		// the progress bar's title

	KCMPourOptions() : fNoteRefusals(kTrue),
					   fStepName("Import Story Text"), fBarTitle("Putting the edited text into the document...")
	{
		fStepName.SetTranslatable(kFalse);
		fBarTitle.SetTranslatable(kFalse);
	}
};

/** Pour `set` into `db`, the way `options` says.

    ★**THE STORIES ARE PAIRED BY THEIR OWN UID**: the file is named after the document's story, and the copy and
      the document hold the same uids, so nothing else has to be matched.
    ★**ONE ABORTABLE COMMAND SEQUENCE.** Every write of every story is one undo step, and a Cancel on the progress
      bar - asked between two stories, the safe point KCMProgressBar.h names - aborts the sequence: nothing of the
      pour stays. `outCancelled` says so.
    ★**WHAT COULD NOT GO IN IS NOTED** (KCMImportRefusals, when fNoteRefusals) as well as counted in `outMessage`,
      so that the "!" rows and the status line come from one walk.
    @return kFalse when nothing at all could be applied. */
bool16 KCMPourStoryTextWith(IDataBase* db, const KCMStoryTextSet& set, const KCMPourOptions& options,
							PMString& outMessage, bool16& outCancelled);

/** "Import Story Text..." from end to end (2026-10-24, adopted 2026-10-25 - the author: "right now the state before
    the change is the Source and the Target is what gets changed - I want that the other way round"): read the chosen
    files, take a Task Start in memory of the task document (KCMTakeImportClone - the chosen Target, else the one in
    front), pour the files' words into IT, and start the Story comparison: the Source is the document as the files
    would make it, the Target the document as it stands - which nothing here changes. The rows are what would change;
    Apply puts them in (IKCMStoryEditsFacade).

    ★**EACH STORY OF THE COPY IS MADE WHAT WORD SHOWS** (2026-09-23): KCMStorySync compares it with Word's and says
      what to write, KCMStorySyncApply writes it - paragraphs changed, added and removed, footnotes made and taken
      away - minimally, so that the ruby and the kenten on the parts nobody edited are still there afterwards. ★The
      ruby, the kenten and the rest go in as a SECOND PASS over each story, after its words are in (KCMStoryAttrPour
      says why that way round).
    ⚠**WHAT CANNOT BE MADE WORD'S IS HELD, BY NAME** - a table Word reshaped into a shape InDesign cannot hold (rows
      of unequal length, a nested table where a row or a merge would go), a change touching an invisible character
      (an anchored object, a page number, an index marker), a tate-chu-yoko inside a warichu Word cannot carry. Each
      is a "!" row. ★A table whose rows, columns or merges changed is made Word's (S1/S2 - KCMStoryTextImport.cpp
      says what the user accepted with it).
    ★★**ONE PROGRESS BAR FROM THE FIRST FILE TO THE LAST STORY** (2026-09-17, the user's choice): it appears after the
      same three seconds as every other bar and carries Cancel; a cancel lets the copy go and changes nothing.

    @param files the files the reader chose.
    @param outMessage what happened, for the panel's status line.
    @return kFalse when nothing could be read, the copy could not be taken, or the reader cancelled. */
bool16 KCMImportStoryTextAsSource(const SysFileList& files, PMString& outMessage);

/** ★WHAT APPLY PUTS IN, read from the import's copy (2026-10-26, the import tidy S5 - design 2): story `story` of the
    armed Source - which has to be an import's copy (KCMIsImportClone) - read the way the export reads a story
    (KCMStoryFromDocument, its footnote references placed), written in Word's format and read back
    (KCMStoryDocx::WriteParts, then Read's fAfter): the split shape KCMStorySync::Compare expects of Word's side, the same
    reading a file exported from the copy would give.
    ★★WHY THE COPY AND NOT THE FILES: the rows are the comparison of the document with this copy, so Apply writes what
      the list showed. A part of a file the pour could not put in (a "!" row - held, refused) is not in the copy, and so
      it is not written either. (Until 2026-10-26 Word's content was also kept as read, per story, under the document -
      KCMWordKeep - a second copy of the files that could disagree with the first.)
    @param story the COPY's story - the row's Source story (KCMStoryRow::fSourceStoryUID: the document's own uid for a
           row paired by UID, another for a row paired by the place of its first frame).
    @param out Word's side of the story, on kTrue.
    @param outWhyNot why not, on kFalse: the copy is not the Source any more (the comparison was stopped or the Source
           changed - import again), or the story cannot be read or does not go through Word's format.
    @return kFalse when nothing can be said - nothing is to be written then. */
bool16 KCMImportSourceStory(UID story, KCMStoryShape::Story& out, PMString& outWhyNot);

#endif // __KCMStoryTextImport_h__

// End, KCMStoryTextImport.h.
