//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start KEPT IN MEMORY (2026-10-13)
//
//  "Task Start" takes the document's state at this moment and makes it the Source, with no dialog and no file:
//  the document's database is cloned (IDataBase::Clone) and the clone is chosen as the Source the way a lent
//  database is (KCMExternalSource.h). "Task Start (Save)..." is the file way (KCMTaskStartSave.h), unchanged.
//
//  ★THE USER'S WORDS: "throw the clone away properly - when the Source changes, when the document is closed".
//   This file is the clone's only owner. It lets the clone go:
//     - when the Source choice moves to anything else (KCMPairChoice.cpp asks here, at each setter of the slot);
//     - at kBeforeCloseDoc of the document it was taken from (a revert is a close and a re-open - measured);
//     - at the after-close sweep, as the second line, if that document is no longer open;
//     - at the model's shutdown (through KCMClearChosenDocs).
//  ★SAVING DOES NOT LET IT GO: a clone held through Save, Save As and Save a Copy still gives the older state and
//   the files are whole (measured 2026-10-13, docs/ai-notes/kcm-task-start-clone-spike-2026-10-13.md).
//
//  ⚠★★★A CLONE IS NOT INDEPENDENT OF ITS SOURCE: deleting one after its document has gone killed InDesign
//   (2026-08-31). It is deleted only while that document is still open; when it is not, it is let go WITHOUT
//   deleting - a leak, not a crash.
//  ⚠★★NOTHING MAY NAME IT WHEN IT IS DELETED: a comparison drawing from it is ended first - on every road but the
//   model's shutdown, where nothing draws any more - and the Story mode's marks are swept of it (Forget, in the
//   .cpp). KCMIsSameDoc follows any pointer that is not registered.
//  ⚠IT IS READ AND DRAWN, NOTHING ELSE: no command is run on it, the INX of its document root is never written
//   (that killed InDesign, 2026-09-09) and it is never handed to the PDF export (which writes the LIVE document's
//   state when given one, 2026-10-13). Its definitions are read from the live document when it is taken.
//  ⚠THE GUIDE SAYS DATABASE CLONING IS NOT FOR THIRD PARTIES (vol1, three places); the header carries no mark.
//   The owner's call (2026-10-13): try it on Exchange. If it is turned down, the menu item goes (ui/KCMUI.fr) and
//   nothing here is reached.
//
//  Main thread only.
//
//========================================================================================
#ifndef __KCMTaskStartClone_h__
#define __KCMTaskStartClone_h__

#include "BaseType.h"
#include "UIDRef.h"				// UID - the import's stories
#include "KCMResourceParse.h"	// KCMResourceList - the definitions taken with the clone

#include <vector>

class IDataBase;
class IDocumentList;
class PMString;

/** Whether "Task Start" may be pressed: a document to take - the question "Task Start (Save)..." asks. */
bool16	KCMCanTakeTaskStartClone();

/** Take the state of the document (the chosen Target, else the active one) into memory and choose it as the Source.
	In order: the clone; the live document's definitions (the Resources mode's older side); a running comparison
	stopped; the previous one let go; the clone registered as the lent Source and the pair chosen. Nothing starts.
	@param outWhyNot the status line's words when it was not taken - nothing was changed then.
	@return kTrue when it was taken. */
bool16	KCMTakeTaskStartClone(PMString& outWhyNot);

/** kTrue when db is the clone held now. A pointer comparison; nil answers kFalse. */
bool16	KCMIsTaskStartClone(IDataBase* db);

/** Let the clone go: a comparison drawing from it stopped first (when stopIfDrawing), the Source choice and the lent
	registration forgotten, and the clone deleted while the document it was taken from is still open (otherwise it
	is not deleted). Nothing when none is held.
	@param stopIfDrawing kFalse ONLY from KCMClearChosenDocs, which is the model's shutdown as well as "Clear Target
	  and Source" (that item stops the comparison itself first) - nothing may touch the screen during a shutdown. */
void	KCMLetGoTaskStartClone(bool16 stopIfDrawing);

/** kBeforeCloseDoc: `closing` is still alive. When it is the document the clone was taken from, the clone goes - a
	comparison drawing from it stopped, then forgotten, then deleted. ★STOPPED HERE, not left to the after-close
	sweep (corrected 2026-10-13, before the first build): liveness tests do answer "gone" the moment it is forgotten,
	but the drawing's identity tests (KCMIsSameDoc on sSrcDB and on the armed Source) ask no liveness first and would
	follow the deleted pointer until the sweep - the lent Source's contract is "end the comparison, then delete"
	(KCMExternalSource.h). Main thread only. */
void	KCMTaskStartCloneDocClosing(IDataBase* closing);

/** The after-close sweep's second line: when the document the clone was taken from is no longer in `docList` (a
	close kBeforeCloseDoc did not report), the clone is forgotten and NOT deleted. */
void	KCMTaskStartCloneSweep(IDocumentList* docList);

/** The definitions read from the live document when the clone was taken - the Resources mode's older side, since a
	clone cannot be exported (KCMResourceSnapshot.cpp). kFalse, with the reason ("source: ..."), when none were. */
bool16	KCMTaskStartCloneResources(KCMResourceList& out, PMString& whyNot);

// ---- ★THE IMPORT'S COPY (2026-10-24, the import-as-Source road - docs/superpowers/specs/2026-10-24-kcm-import-as-
// source-design.md). "Import Story Text..." takes a Task Start in memory, pours the files into IT (not into the
// document), and the Story mode then shows what the document would become. The clone is the same one this file
// holds - one owner, one life (let go as every Task Start in memory is) - with two facts more.

/** KCMTakeTaskStartClone for an import: no definitions read (the import's Source is compared in the Story mode only),
	and the Source line says `label` (followed by the time). Nothing starts and nothing is poured. */
bool16	KCMTakeImportClone(const PMString& label, PMString& outWhyNot);

/** The stories the import filled - the only ones the comparison lists. Nothing when no import's clone is held. */
void	KCMSetImportCloneStories(const std::vector<UID>& stories);

/** kTrue when db is the clone held now AND it was taken for an import. ★The one place that answers "is the Source the
	imported text" - the Start's refusal, the list's filter, the facade's IsImportSource and Apply all ask it. */
bool16	KCMIsImportClone(IDataBase* db);

/** kTrue when the import's clone is held and the import filled `story`. */
bool16	KCMImportCloneHasStory(UID story);

#endif // __KCMTaskStartClone_h__

// End, KCMTaskStartClone.h.
