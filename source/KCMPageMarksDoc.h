//========================================================================================
//
//  KCMPageMarksDoc.h
//
//  The reader's own marks -- the tick (Check) and the cat-paw stamps -- written INTO the document,
//  as SCRIPT LABELS on the pages that carry them.
//
//  ***** WHY THE DOCUMENT AT ALL *****
//
//  Everything else this plug-in draws is session state: it is never written to the .indd, and a
//  reader who opens the file without KCM sees the document as it is. The tick and the paw are the
//  exception the user asked for (2026-09-07): they are the READER'S OWN marks -- "I have looked at
//  this page", "look here" -- and work like that is worth keeping with the file rather than beside
//  it. ⚠The registrations (Added/Removed) are NOT kept: they are an INPUT to the comparison
//  rather than a mark a reader leaves, and the user ruled them out in the same breath.
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
//  ★**WRITING IS ALWAYS SOMETHING THE READER ASKED FOR.** Nothing here runs by itself. Writing
//    a label changes the document -- it makes it modified, exactly as any edit does -- and KCM's
//    standing promise is that comparing leaves the document as it was found. So the write is a
//    menu item, and only the menu item.
//  ★**READING IS FREE**, so restoring may be automatic (the document-opened responder does it).
//  ⚠★★**OUR KEYS ARE PREFIXED AND WE TOUCH NOTHING ELSE.** IScriptLabel has no
//    "remove one key", and ClearTags() would take EVERY label off the page -- including the
//    reader's own and other plug-ins'. Taking ours off therefore means reading the whole list,
//    dropping ours, and writing the rest back unchanged.
//
//========================================================================================

#ifndef __KCMPageMarksDoc_h__
#define __KCMPageMarksDoc_h__

#include "BaseType.h"

class IDataBase;

/** Write this document's ticks and paws onto its pages, and take our labels off the pages that no
	tlonger carry either. Master pages are included -- a tick may sit on one.
	@return how many pages were changed, or -1 when the document could not be used. */
int32	KCMMarksSaveToDocument(IDataBase* db);

/** Read the labels back and REPLACE what this session holds for that document (restoring is "put
	tback what was saved", not "merge with what is here"). Either count may be nil.
	@return how many pages carried one of our labels, or -1 when the document could not be read. */
int32	KCMMarksRestoreFromDocument(IDataBase* db, int32* outChecks, int32* outPaws);

/** Take every label of OURS off every page, leaving all other labels exactly as they were.
	@return how many pages were changed, or -1 when the document could not be used. */
int32	KCMMarksClearFromDocument(IDataBase* db);

#endif // __KCMPageMarksDoc_h__

// End, KCMPageMarksDoc.h.
