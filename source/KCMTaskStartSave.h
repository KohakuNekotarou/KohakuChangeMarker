//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  SAVE A COPY OF THE DOCUMENT, AND CHOOSE THAT FILE AS THE SOURCE. That is the whole of it
//  (2026-09-21, the user's design). Nothing is opened here and no comparison is started: what
//  the reader gets is the ordinary "a Source has been chosen" state, with a path on the panel.
//
//  ★**WHY A FILE AND NOT A SNAPSHOT IN MEMORY.** The shape this replaces held the document's INX
//  in memory and rebuilt a copy from it for every comparison, which meant living with whatever
//  the import did to the copy - a lost paragraph range, a bitten table, a text frame that came
//  back narrower than it went out. A file written by InDesign and read back by InDesign has no
//  such gap. It also keeps the uids, so the page pairing and the story pairing work on the
//  ordinary two-document path with no translation table at all.
//
//  ★**THE READER PRESSES SAVE.** A dialog opens on the document's own folder with a name made
//  from the document's name and the time. A cancel ends the whole thing, changing nothing.
//
//  Main thread only. Nothing here is read from the drawing path.
//
//========================================================================================
#ifndef __KCMTaskStartSave_h__
#define __KCMTaskStartSave_h__

#include "BaseType.h"
#include "PMString.h"

class IDataBase;

/** Whether Task Start may be pressed: **a document to copy, and nothing else**.
    ★THE ONE PLACE - the menu's grey state and the command both ask this.
    The document is the chosen Target when there is one, otherwise the active document, so the
    item is live whenever either exists. */
bool16 KCMCanTakeTaskStartCopy();

/** Save a copy of that document to a file the reader picks, and choose the file as the Source.

    The steps, in order:
      1. decide which document - the chosen Target, else the active one (the user's rule);
      2. raise the save dialog on THAT document's folder, with a suggested name;
      3. ⚠**a cancel ends it here, changing nothing at all** - no stop, no choice, no message;
      4. write the copy (IDocFileHandler::SaveACopy);
      5. stop a running comparison, because its marks were made from a pair that is changing;
      6. choose that same document as the Target, and the new file as the Source.

    ★**THE TARGET IS THE DOCUMENT THAT WAS COPIED, named outright** - not "whatever is active
      now". Step 5 can bring a different window to the front, and re-asking would then choose it.

    @param outWhyNot the reason, for the status line. ★★**EMPTY MEANS THE READER CANCELLED**, and
      the caller says nothing at all in that case (the user's rule: "cancelled, and that is the
      end of it"). Anything else is a real failure and belongs on the status line.
    @return kTrue when the copy was written and the pair chosen. */
bool16 KCMTakeTaskStartCopy(PMString& outWhyNot);

/** "<document>_TaskStart_20260921-143052.indd" - the name the save dialog opens with.
    The document's own name without its extension, or "Untitled" when it has none to give. */
void KCMSuggestedTaskStartName(IDataBase* docDB, PMString& out);

#endif // __KCMTaskStartSave_h__

// End, KCMTaskStartSave.h.
