//========================================================================================
//
//  KCMPageMarksDoc.h
//
//  The reader's own marks -- the tick (Check) and the cat-paw stamps -- as they live INSIDE the
//  document, as SCRIPT LABELS on the pages that carry them. This file owns the label FORMAT and
//  nothing else; who may write it is settled next door (KCMPageMarksCmd.h).
//
//  ***** WHY THE DOCUMENT AT ALL *****
//
//  Everything else this plug-in draws is session state: it is never written to the .indd, and a
//  reader who opens the file without KCM sees the document as it is. The tick and the paw are the
//  exception the user asked for (2026-09-07): they are the READER'S OWN marks -- "I have looked at
//  this page", "look here" -- and work like that is worth keeping with the file rather than beside
//  it. The registrations (Added/Removed) are NOT kept: they are an INPUT to the comparison rather
//  than a mark a reader leaves, and the user ruled them out in the same breath.
//
//  ***** WHY SCRIPT LABELS, AND NOT A BOSS OF OUR OWN *****
//
//  The user's condition: no new persistent boss -- use something the application already has. Of
//  the three that could hold this (script label / BlackBoxData / an unplaced story) the label is
//  the only one that is all three of: readable from scripting, carried through IDML, and attached
//  TO THE PAGE. The last one decides it -- one blob on the document would be broken by
//  re-ordering, inserting or deleting a page, while a label travels with the page it is on.
//  (BlackBoxData is opaque and does not reach IDML; an unplaced story would be treated as text --
//  found by searches, spell-checked, exported.)
//
//  ***** THE RULES THIS FILE KEEPS *****
//
//  WRITING IS ALWAYS SOMETHING THE READER DID. Nothing here runs by itself. Writing a label
//    changes the document -- it makes it modified, exactly as any edit does. Until 2026-09-07 that
//    was answered by making the write a menu item; it is now answered by making the write
//    UNDOABLE, so the reader can take it back the way they take back any other edit.
//  READING IS FREE, so restoring may be automatic (the document-opened responder does it).
//  ⚠**OUR KEYS ARE PREFIXED AND WE TOUCH NOTHING ELSE.** IScriptLabel has no "remove one key", and
//    ClearTags() would take EVERY label off the page -- including the reader's own and other
//    plug-ins'. Taking ours off therefore means reading the whole list, dropping ours, and writing
//    the rest back unchanged.
//
//========================================================================================

#ifndef __KCMPageMarksDoc_h__
#define __KCMPageMarksDoc_h__

#include "BaseType.h"		// bool16, int32, ErrorCode
#include "OMTypes.h"		// UID
#include <vector>

#include "KCMPawStamp.h"	// KCMPawStamp -- written and read by value

class IDataBase;

/** Put exactly these marks on ONE page: our two labels are set to match, and every label that is
	not ours is written back untouched. An unticked page with no paws loses our keys entirely.

	⚠★★**ONLY kKCMSetPageMarksCmdBoss MAY CALL THIS.** It writes the document, so a caller that
	  reaches past the command gets a change that Undo cannot take back and that the session store
	  never hears about. The door is KCMMarksWrite() in KCMPageMarksCmd.h.
	@return kSuccess, or the failure that stopped it. */
ErrorCode	KCMMarksWriteOnePage(IDataBase* db, UID page, bool16 check,
								 const std::vector<KCMPawStamp>& paws);

/** Read the whole document's labels and make the session store say exactly that.

	This is the refill the marks observer runs, and the restore the document-opened responder runs;
	they are one function because they are one question ([[one-question-one-place]]). It REPLACES
	rather than merges -- "put back what the document says", not "add to what is here" -- and it
	notifies the pages whose picture changed, which it works out by comparing the store before with
	the store after. **A page that LOST a mark is in no current-state set**, so nothing but that
	comparison can name it, and missing one leaves a stale tick on screen.

	Reads only: not one byte of the document is written here.
	Either count may be nil.
	@return how many pages carried a label of ours, or -1 when the document could not be read. */
int32	KCMMarksSyncFromDocument(IDataBase* db, int32* outChecks, int32* outPaws);

/** Take every mark of ours off every page of this document, as one undoable step. Labels that are
	not ours are left exactly as they were.
	@return how many pages were changed, or -1 when the document could not be used. */
int32	KCMMarksClearFromDocument(IDataBase* db);

#endif // __KCMPageMarksDoc_h__

// End, KCMPageMarksDoc.h.
